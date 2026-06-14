/*
 * sd_logger.c
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 *
 * CM4 consumer side of the dual-core SD log. See sd_logger.h for the role and
 * Shared/hcu_ipc.h for the ring layout. SPSC contract: CM7 is the sole writer of
 * `head`, CM4 (this file) is the sole writer of `tail`. We never touch
 * head/magic/version - CM7 owns ring setup.
 */

#include "sd_logger.h"
#include "fatfs.h"     /* SDFatFS, SDPath + FatFs API (f_mount/f_open/f_write/...) */
#include "main.h"      /* HAL_GetTick(), __DMB() */
#include <string.h>
#include <stdio.h>     /* snprintf */
#include <stdbool.h>   /* bool/true/false (not pulled in by the above on CM4) */

/* The SDMMC2 handle, defined in CM4 main.c (set up by MX_SDMMC2_SD_Init). We
 * own the card's run-time (re)init here so it happens in exactly one place and
 * can be retried cleanly - see sd_card_init(). */
extern SD_HandleTypeDef hsd2;

/* The shared inter-core struct. The .shared_ram NOLOAD section in CM4's linker
 * script places it at HCU_IPC_BASE = 0x38000000 (D3 SRAM4) - the SAME physical
 * address CM7's copy lands at, so the two binaries share one struct. NOLOAD =>
 * CM4 startup never zeroes it (CM7 owns initialisation). */
volatile hcu_ipc_t g_hcu_ipc __attribute__((section(".shared_ram")));

/* last_fr sentinel: card hardware init failed (almost always = no card / not
 * seated). Distinct from any FatFs FRESULT (which are small non-negative). */
#define SDLOG_FR_NO_CARD   (-1)

/* ---- tunables ---------------------------------------------------------- */
#define SDLOG_FILE_MAX_BYTES   (8u * 1024u * 1024u)  /* rotate at ~8 MB/file   */
#define SDLOG_SYNC_MS          1000u                 /* f_sync cadence (~1 s)  */
#define SDLOG_REMOUNT_MS       2000u                 /* retry mount if no card */

/* On-disk per-file header (written once at file open). Decoded off-target in
 * Python. 16 bytes keeps the 8-byte records that follow 8-aligned in the file.
 * version == HCU_IPC_VERSION lets the decoder reject a layout it doesn't know. */
typedef struct {
    uint32_t magic;        /* SDLOG_FILE_MAGIC */
    uint16_t version;      /* HCU_IPC_VERSION (record layout) */
    uint16_t record_size;  /* sizeof(log_record_t) */
    uint16_t year;
    uint8_t  month, day, hour, minute, second;
    uint8_t  pad;
} sdlog_header_t;
#define SDLOG_FILE_MAGIC   0x474C4448u   /* 'HDLG' little-endian */

/* ---- state ------------------------------------------------------------- */
static bool     s_mounted    = false;
static bool     s_file_open  = false;
static FIL      s_fil;
static uint32_t s_file_bytes = 0;
static uint32_t s_next_index = 0;
static uint32_t s_last_sync  = 0;
static uint32_t s_last_mount = 0;

/* 512-byte sector assembly buffer. Static => lives in CM4 D2 RAM, which is the
 * same domain as SDMMC2 and its IDMA, so it is DMA-reachable. */
static uint8_t  s_sector[512];
static uint32_t s_fill = 0;

/* Debug-only diagnostics (see sd_logger.h). Lives in CM4 .bss (zeroed at boot).
 * Watch this in the debugger to see exactly what CM4 is doing. */
volatile sdlog_status_t g_sdlog_status;

/* ---- shared-clock seqlock READER --------------------------------------- */
static void read_now(hcu_datetime_t *out)
{
    uint32_t s1, s2;
    do {
        s1 = g_hcu_ipc.now.seq;
        __DMB();
        *out = g_hcu_ipc.now;          /* struct copy */
        __DMB();
        s2 = g_hcu_ipc.now.seq;
    } while ((s1 & 1u) || (s1 != s2)); /* retry while writer mid-update / torn */
}

uint32_t SdLog_FatTime(void)
{
    hcu_datetime_t n;
    read_now(&n);

    uint16_t year = n.valid ? n.year   : 2026u;
    uint8_t  mon  = n.valid ? n.month  : 1u;
    uint8_t  day  = n.valid ? n.day    : 1u;
    uint8_t  hh   = n.valid ? n.hour   : 0u;
    uint8_t  mi   = n.valid ? n.minute : 0u;
    uint8_t  ss   = n.valid ? n.second : 0u;
    if (year < 1980u) year = 1980u;

    /* FatFs packed format: bits 31..25 year-1980, 24..21 month, 20..16 day,
     * 15..11 hour, 10..5 minute, 4..0 second/2. */
    return ((uint32_t)(year - 1980u) << 25)
         | ((uint32_t)mon << 21)
         | ((uint32_t)day << 16)
         | ((uint32_t)hh  << 11)
         | ((uint32_t)mi  << 5)
         | ((uint32_t)(ss / 2u));
}

/* ---- card hardware (re)init -------------------------------------------- *
 * The ONE place the SD card is brought up at run time. Idempotent: the leading
 * HAL_SD_DeInit() returns the handle to a clean RESET state, so a previous
 * failed/partial init (e.g. the no-card boot attempt that fail-forwarded through
 * Error_Handler) can never poison a later try. DISABLE_SD_INIT in sd_diskio.c
 * means FatFs no longer inits the card, so this is the sole init path - no more
 * double init. Returns true only when a card is up in 4-bit mode.
 *
 * No card present => HAL_SD_Init() fails cleanly here (bounded by the HAL's
 * internal timeouts) and we report it; nothing halts, nothing blocks CM7. */
static bool sd_card_init(void)
{
    HAL_SD_DeInit(&hsd2);                       /* always start from a clean handle */

    if (HAL_SD_Init(&hsd2) != HAL_OK) {
        return false;                          /* no card / not seated */
    }
    if (HAL_SD_ConfigWideBusOperation(&hsd2, SDMMC_BUS_WIDE_4B) != HAL_OK) {
        HAL_SD_DeInit(&hsd2);                   /* don't leave a half-up handle */
        return false;
    }
    return true;
}

/* ---- file handling ----------------------------------------------------- */
static void try_mount(void)
{
    /* Bring the card up ourselves first. If there's no card this fails cleanly
     * and we stay unmounted - SdLog_Service() will retry. */
    if (!sd_card_init()) {
        g_sdlog_status.last_fr = SDLOG_FR_NO_CARD;
        return;
    }

    /* opt = 1: mount now. With DISABLE_SD_INIT this only checks the (already up)
     * card's state and reads the FAT - it does NOT re-init the hardware. */
    FRESULT fr = f_mount(&SDFatFS, SDPath, 1);
    if (fr == FR_OK) {
        s_mounted = true;
        g_sdlog_status.mounted = 1u;
    } else {
        g_sdlog_status.last_fr = (int32_t)fr;
        HAL_SD_DeInit(&hsd2);                   /* leave it clean for the next try */
    }
}

static bool open_new_file(void)
{
    char path[20];

    /* Pick the lowest free LOGxxxx.BIN. FA_CREATE_NEW fails FR_EXIST if the
     * name is taken, so we never need f_stat (works regardless of ffconf). */
    while (s_next_index <= 9999u) {
        snprintf(path, sizeof(path), "%s/LOG%04u.BIN", SDPath, (unsigned)s_next_index);
        FRESULT fr = f_open(&s_fil, path, FA_WRITE | FA_CREATE_NEW);
        if (fr == FR_OK)    break;             /* created it */
        if (fr != FR_EXIST) { g_sdlog_status.last_fr = (int32_t)fr; return false; }
        s_next_index++;
    }
    if (s_next_index > 9999u) return false;    /* card full of logs */

    /* stamp the header with the current wall-clock */
    hcu_datetime_t n;
    read_now(&n);
    sdlog_header_t h;
    memset(&h, 0, sizeof(h));
    h.magic       = SDLOG_FILE_MAGIC;
    h.version     = HCU_IPC_VERSION;
    h.record_size = (uint16_t)sizeof(log_record_t);
    h.year   = n.valid ? n.year   : 0u;
    h.month  = n.month;  h.day = n.day;
    h.hour   = n.hour;   h.minute = n.minute;  h.second = n.second;

    UINT bw = 0;
    FRESULT fr = f_write(&s_fil, &h, sizeof(h), &bw);
    if (fr != FR_OK || bw != sizeof(h)) {
        g_sdlog_status.last_fr = (int32_t)fr;
        f_close(&s_fil);
        return false;
    }
    f_sync(&s_fil);

    s_file_open  = true;
    s_file_bytes = sizeof(h);
    g_sdlog_status.file_open  = 1u;
    g_sdlog_status.file_index = (uint16_t)s_next_index;  /* the index we just used */
    g_sdlog_status.file_bytes = s_file_bytes;
    s_next_index++;                            /* next rotation -> fresh index */
    return true;
}

/* Write whatever has accumulated in the sector buffer (a whole number of
 * records, <= 512 B). On any write error we drop the card so we don't spin on a
 * failing medium - SdLog_Service will try to remount. */
static bool flush_buffer(void)
{
    if (s_fill == 0u) return true;

    UINT bw = 0;
    FRESULT fr = f_write(&s_fil, s_sector, s_fill, &bw);
    if (fr != FR_OK || bw != s_fill) {
        g_sdlog_status.last_fr   = (int32_t)fr;
        f_close(&s_fil);
        s_file_open = false;
        s_mounted   = false;
        s_fill      = 0;          /* drop the unwritten sector - it belonged to the
                                   * file we just closed; don't bleed it into the
                                   * next file if the card comes back */
        g_sdlog_status.file_open = 0u;
        g_sdlog_status.mounted   = 0u;
        return false;
    }
    s_file_bytes += s_fill;
    g_sdlog_status.file_bytes = s_file_bytes;
    s_fill = 0;
    return true;
}

/* ---- public API -------------------------------------------------------- */
void SdLog_Init(void)
{
    s_last_mount = HAL_GetTick();
    try_mount();   /* best-effort; Service remounts if the card shows up later */
}

void SdLog_Service(void)
{
    uint32_t now_ms = HAL_GetTick();
    g_sdlog_status.loops++;           /* heartbeat: climbing => CM4 is running this loop */

    if (!s_mounted) {
        if ((now_ms - s_last_mount) >= SDLOG_REMOUNT_MS) {
            s_last_mount = now_ms;
            try_mount();
        }
        return;
    }

    /* Ring isn't trustworthy until CM7 has stamped magic/version (boot race:
     * CM4 is released by HSEM before CM7 runs Log_Init). */
    if (g_hcu_ipc.magic != HCU_IPC_MAGIC || g_hcu_ipc.version != HCU_IPC_VERSION) {
        return;
    }
    g_sdlog_status.ring_seen = 1u;

    uint32_t head = g_hcu_ipc.head;   /* CM7 publishes this */
    __DMB();                          /* see the records before consuming them */
    uint32_t tail = g_hcu_ipc.tail;   /* we are the sole writer of tail */
    g_sdlog_status.head_seen = head;
    g_sdlog_status.tail_now  = tail;

    while (tail != head) {
        if (!s_file_open && !open_new_file()) {
            return;                   /* can't open a file -> try again next pass */
        }

        /* flush a full sector before it would overflow (records may not divide
         * 512 evenly if the layout grows later, so guard the boundary). */
        if (s_fill + sizeof(log_record_t) > sizeof(s_sector)) {
            if (!flush_buffer()) return;
            if (s_file_bytes >= SDLOG_FILE_MAX_BYTES) {
                f_close(&s_fil);
                s_file_open = false;  /* next iteration opens a fresh file */
                g_sdlog_status.file_open = 0u;
                continue;
            }
        }

        memcpy(&s_sector[s_fill],
               (const void *)&g_hcu_ipc.ring[tail & HCU_LOG_RING_MASK],
               sizeof(log_record_t));
        s_fill += sizeof(log_record_t);

        __DMB();                      /* record consumed before releasing slot */
        g_hcu_ipc.tail = ++tail;      /* hand the slot back to CM7 */
        g_sdlog_status.records++;
        g_sdlog_status.tail_now = tail;
    }

    /* durability: push FatFs's cached sectors to the card about once a second */
    if (s_file_open && (now_ms - s_last_sync) >= SDLOG_SYNC_MS) {
        s_last_sync = now_ms;
        f_sync(&s_fil);
    }
}
