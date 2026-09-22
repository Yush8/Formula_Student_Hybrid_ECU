/*
 * can_sniffer.c  --  raw CAN bus sniffer (see can_sniffer.h)
 *
 *  Created on: 5 Jul 2026
 *      Author: Yusha
 *
 * A read-only observer of both CAN buses. The Rx ISR in can.c hands us every
 * frame via CanSniffer_Capture(); we keep the latest bytes + a frame count per
 * (bus, id) in a small table, and CanSniffer_Service() streams a snapshot of the
 * whole table to the USB console when enabled. Nothing here feeds the model - it
 * is purely for looking at the bus (the CAN twin of telem.c).
 */

#include "can_sniffer.h"
#include "console.h"        /* Console_Out() - the busy-safe TX primitive */
#include "main.h"           /* HAL_GetTick(), HAL_NVIC_*, FDCANx_IT0_IRQn */
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Capture table (written by ISR, read under CAN-IRQ mask)             */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t id;
    uint32_t count;       /* total frames seen for this id */
    uint32_t last_seen;   /* HAL tick at last rx */
    uint8_t  data[8];     /* latest payload; trailing bytes zeroed */
    uint8_t  len;         /* valid bytes this frame (0..8) */
    uint8_t  used;        /* slot occupied */
} sniff_slot_t;

/* Index [0] = bus1 (FDCAN1), [1] = bus2 (FDCAN2). Entries are appended in the
 * order ids are first seen and never removed except by CanSniffer_Clear(), so a
 * slot index is stable - the streaming reader can walk it without a global lock,
 * masking only the brief per-slot copy. */
static volatile sniff_slot_t s_tab[2][CANSNIFF_MAX_IDS];
static volatile uint16_t     s_ids[2];      /* distinct ids tracked per bus */
static volatile uint32_t     s_total[2];    /* total frames captured per bus */
static volatile uint32_t     s_dropped[2];  /* frames whose NEW id didn't fit */

/* ------------------------------------------------------------------ */
/* Streaming state                                                    */
/* ------------------------------------------------------------------ */
#define CANSNIFF_DEFAULT_RATE_HZ  10u

static uint8_t  s_on        = 0;
static uint16_t s_period_ms = 1000u / CANSNIFF_DEFAULT_RATE_HZ;
static uint32_t s_last_ms   = 0;

/* ------------------------------------------------------------------ */
/* Capture (ISR context)                                              */
/* ------------------------------------------------------------------ */
void CanSniffer_Capture(uint8_t bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    if (bus < 1u || bus > 2u) return;
    uint8_t bi = (uint8_t)(bus - 1u);
    if (len > 8u) len = 8u;

    s_total[bi]++;

    volatile sniff_slot_t *tab = s_tab[bi];
    uint16_t n = s_ids[bi];

    /* Update an existing id. */
    for (uint16_t i = 0; i < n; i++) {
        if (tab[i].id == id) {
            for (uint8_t b = 0; b < 8; b++) tab[i].data[b] = (b < len) ? data[b] : 0u;
            tab[i].len       = len;
            tab[i].count++;
            tab[i].last_seen = HAL_GetTick();
            return;
        }
    }

    /* A new id: append if there is room, else count the drop. */
    if (n >= CANSNIFF_MAX_IDS) { s_dropped[bi]++; return; }
    volatile sniff_slot_t *s = &tab[n];
    s->id = id;
    for (uint8_t b = 0; b < 8; b++) s->data[b] = (b < len) ? data[b] : 0u;
    s->len       = len;
    s->count     = 1u;
    s->last_seen = HAL_GetTick();
    s->used      = 1u;
    s_ids[bi]    = (uint16_t)(n + 1u);
}

/* ------------------------------------------------------------------ */
/* Snapshot one slot under a brief CAN-IRQ mask                       */
/* ------------------------------------------------------------------ */
static int snap_slot(uint8_t bi, uint16_t i, sniff_slot_t *out)
{
    /* Mask only the two CAN Rx IRQs (not PRIMASK) so nothing else loses timing.
     * Frames arriving during the copy wait in the 16-deep hardware FIFO. */
    HAL_NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
    HAL_NVIC_DisableIRQ(FDCAN2_IT0_IRQn);

    int used = s_tab[bi][i].used;
    if (used) {
        out->id        = s_tab[bi][i].id;
        out->count     = s_tab[bi][i].count;
        out->last_seen = s_tab[bi][i].last_seen;
        out->len       = s_tab[bi][i].len;
        for (uint8_t b = 0; b < 8; b++) out->data[b] = s_tab[bi][i].data[b];
    }

    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
    HAL_NVIC_EnableIRQ(FDCAN2_IT0_IRQn);
    return used;
}

/* ------------------------------------------------------------------ */
/* Snapshot streaming                                                 */
/* ------------------------------------------------------------------ */
/* The whole snapshot is built into this one static buffer and sent in a single
 * Console_Out, exactly like telem's frame buffer. That is deliberate: the USB
 * CDC TX takes the buffer by POINTER and drains it asynchronously (see
 * CDC_Transmit_FS), so the buffer must stay valid and unchanged for the whole
 * transfer - a stack buffer or a re-used chunk would be corrupted mid-flight.
 * Sized for the worst case (both buses full) so sb_str can never overflow. */
#define CANSNIFF_ROW_MAX     56u   /* "#C b iii cccccccccc aaaaaaaaaa d dddd..\r\n" */
#define CANSNIFF_STREAM_BUF  (CANSNIFF_MAX_IDS * 2u * CANSNIFF_ROW_MAX + 16u)

static char s_stream[CANSNIFF_STREAM_BUF];

typedef struct { char *buf; size_t cap; size_t len; } sbuf_t;

static void sb_str(sbuf_t *b, const char *s)
{
    while (*s && b->len < b->cap - 1u) b->buf[b->len++] = *s++;
    b->buf[b->len] = '\0';
}

static void fmt_row(char *out, size_t cap, uint8_t bus,
                    const sniff_slot_t *s, uint32_t now)
{
    static const char H[] = "0123456789ABCDEF";
    char hex[17];
    uint8_t len = (s->len > 8u) ? 8u : s->len;
    for (uint8_t b = 0; b < len; b++) {
        hex[b * 2]     = H[(s->data[b] >> 4) & 0x0Fu];
        hex[b * 2 + 1] = H[s->data[b] & 0x0Fu];
    }
    hex[len * 2] = '\0';

    /* Age = now - last_seen. `now` is sampled once at the top of the snapshot
     * pass, but each slot's last_seen is copied slightly later, so a frame that
     * arrives (via the Rx ISR) inside that window leaves last_seen just AHEAD of
     * now. The unsigned subtraction would then wrap to ~0xFFFFFFFF. Clamp it: a
     * "future" last_seen means the frame effectively just arrived => age 0. */
    uint32_t age = (now >= s->last_seen) ? (now - s->last_seen) : 0u;

    snprintf(out, cap, "#C %u %03lX %lu %lu %u %s\r\n",
             (unsigned)bus, (unsigned long)s->id,
             (unsigned long)s->count,
             (unsigned long)age,
             (unsigned)len, hex);
}

/* Build one snapshot into s_stream; returns its length (0 => nothing to send). */
static size_t build_snapshot(void)
{
    sbuf_t sb = { s_stream, sizeof s_stream, 0 };
    s_stream[0] = '\0';
    uint32_t now = HAL_GetTick();

    for (uint8_t bi = 0; bi < 2u; bi++) {
        uint16_t n = s_ids[bi];          /* snapshot the bound once */
        for (uint16_t i = 0; i < n; i++) {
            sniff_slot_t s;
            if (!snap_slot(bi, i, &s)) continue;
            char row[CANSNIFF_ROW_MAX + 8u];
            fmt_row(row, sizeof row, (uint8_t)(bi + 1u), &s, now);
            sb_str(&sb, row);
        }
    }
    return sb.len;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void CanSniffer_Init(void)
{
    for (uint8_t bi = 0; bi < 2u; bi++) {
        s_ids[bi]     = 0u;
        s_total[bi]   = 0u;
        s_dropped[bi] = 0u;
        for (uint16_t i = 0; i < CANSNIFF_MAX_IDS; i++) {
            s_tab[bi][i].used = 0u;
        }
    }
    s_on        = 0u;
    s_period_ms = (uint16_t)(1000u / CANSNIFF_DEFAULT_RATE_HZ);
    s_last_ms   = HAL_GetTick();
}

void CanSniffer_Service(void)
{
    if (!s_on) return;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last_ms) < s_period_ms) return;
    s_last_ms = now;
    if (build_snapshot() > 0u) Console_Out(s_stream);
}

void CanSniffer_SetStreaming(int on)
{
    s_on      = on ? 1u : 0u;
    s_last_ms = HAL_GetTick();   /* emit promptly on enable */
}

int CanSniffer_IsStreaming(void) { return s_on; }

void CanSniffer_SetRateHz(uint32_t hz)
{
    if (hz < 1u)                    hz = 1u;
    if (hz > CANSNIFF_MAX_RATE_HZ)  hz = CANSNIFF_MAX_RATE_HZ;
    s_period_ms = (uint16_t)(1000u / hz);
    if (s_period_ms == 0u) s_period_ms = 1u;
}

uint32_t CanSniffer_GetRateHz(void)
{
    return (s_period_ms == 0u) ? CANSNIFF_MAX_RATE_HZ : (1000u / s_period_ms);
}

void CanSniffer_Clear(void)
{
    /* Reset the tables under the CAN-IRQ mask so a frame arriving mid-clear can't
     * leave a half-cleared slot. Counters are zeroed too - a fresh start. */
    HAL_NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
    HAL_NVIC_DisableIRQ(FDCAN2_IT0_IRQn);
    for (uint8_t bi = 0; bi < 2u; bi++) {
        s_ids[bi]     = 0u;
        s_total[bi]   = 0u;
        s_dropped[bi] = 0u;
        for (uint16_t i = 0; i < CANSNIFF_MAX_IDS; i++) {
            s_tab[bi][i].used = 0u;
        }
    }
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
    HAL_NVIC_EnableIRQ(FDCAN2_IT0_IRQn);
}

void CanSniffer_PrintTable(void)
{
    Console_Out("cansniff table:\r\n");
    if (build_snapshot() > 0u) Console_Out(s_stream);
    Console_Out("end\r\n");
}

void CanSniffer_PrintStatus(void)
{
    char buf[128];
    snprintf(buf, sizeof buf,
             "cansniff %s  rate %lu Hz  bus1 %u/%u ids (%lu frm, %lu drop)  "
             "bus2 %u/%u ids (%lu frm, %lu drop)\r\n",
             s_on ? "on" : "off",
             (unsigned long)CanSniffer_GetRateHz(),
             (unsigned)s_ids[0], (unsigned)CANSNIFF_MAX_IDS,
             (unsigned long)s_total[0], (unsigned long)s_dropped[0],
             (unsigned)s_ids[1], (unsigned)CANSNIFF_MAX_IDS,
             (unsigned long)s_total[1], (unsigned long)s_dropped[1]);
    Console_Out(buf);
}
