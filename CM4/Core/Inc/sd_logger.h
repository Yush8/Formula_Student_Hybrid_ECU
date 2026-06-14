/*
 * sd_logger.h
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 *
 * CM4 SD-logging consumer. CM7 fills a lock-free ring in shared D3 SRAM4
 * (g_hcu_ipc); CM4 drains it on its own time, packs the fixed-size records into
 * 512-byte sectors and writes them to microSD via FatFs, rotating to a new
 * LOGxxxx.BIN file by size. The card is the slow, stall-prone device - keeping
 * it entirely on CM4 is what guarantees the CM7 100 Hz control loop can never be
 * blocked by an SD flush (HANDOFF s16.1).
 *
 * Mount is ATTEMPTED, not asserted: no card (or a write error) => logging is
 * silently disabled and CM7 is utterly unaffected ("any doubt -> safe").
 */
#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdint.h>

/* Reach the shared inter-core contract by a SOURCE-RELATIVE path, deliberately
 * (same reasoning as CM7/logger.h): Shared/ is not a CubeMX-managed include dir,
 * so a hand-added "-I../../Shared" gets wiped on every regen. A relative include
 * is resolved against THIS header's directory (CM4/Core/Inc) no matter who pulls
 * it in. Path: Core/Inc -> CM4 -> repo root -> Shared. */
#include "../../../Shared/hcu_ipc.h"   /* log_record_t, the ring, g_hcu_ipc */

/* Mount the card (best-effort) and prime the drain state. Call once at boot,
 * after MX_FATFS_Init(). If the card is absent it retries inside SdLog_Service. */
void SdLog_Init(void);

/* Drain the ring to the card. Call every CM4 superloop pass. Does nothing until
 * CM7 has published the ring (magic valid) and a card is mounted. */
void SdLog_Service(void);

/* FatFs get_fattime() value, packed from the CM7-published wall-clock
 * (g_hcu_ipc.now). fatfs.c's get_fattime() forwards to this. */
uint32_t SdLog_FatTime(void);

/* ---- Debug-only diagnostics (watch in the debugger; not used for logic) ----
 * Same convention as g_can_stats / g_sched_overruns. Read this one struct to
 * see what CM4 is doing:
 *   loops      climbing  => CM4 is alive and running SdLog_Service()
 *   mounted    1         => the card mounted OK
 *   ring_seen  1         => CM4 sees CM7's ring (magic/version match @0x38000000)
 *   records    climbing  => records are being drained to the card
 *   last_fr    -1         => no card / not seated (HW init failed) - the normal
 *                           "running with no SD" state; CM4 retries every ~2 s
 *   last_fr    >0         => last FatFs error (FRESULT code)
 * Quick reads: loops==0 -> CM4 not running/flashed; loops climbing & mounted==0 &
 * last_fr==-1 -> no card (expected: logging off, car unaffected); loops>0 &
 * mounted==0 & last_fr>0 -> card present but FAT/format/seating; mounted==1 &
 * ring_seen==0 -> shared-memory address mismatch between cores. */
typedef struct {
    uint32_t loops;        /* SdLog_Service() call count (heartbeat)       */
    uint32_t records;      /* records drained to the card so far           */
    uint32_t file_bytes;   /* bytes written to the current file            */
    uint32_t head_seen;    /* last g_hcu_ipc.head CM4 observed             */
    uint32_t tail_now;     /* CM4's current tail                           */
    uint16_t file_index;   /* index of the current/last LOGxxxx.BIN        */
    uint8_t  mounted;      /* 1 once f_mount() succeeded                   */
    uint8_t  file_open;    /* 1 while a log file is open                   */
    uint8_t  ring_seen;    /* 1 once magic+version matched                 */
    int32_t  last_fr;      /* last non-OK FatFs FRESULT (0 = none yet)     */
} sdlog_status_t;
extern volatile sdlog_status_t g_sdlog_status;

#endif /* SD_LOGGER_H */
