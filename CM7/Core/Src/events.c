/*
 * events.c  --  the event recorder (see events.h)
 *
 *  Created on: 22 Sep 2026
 *      Author: Yusha
 *
 * WHAT gets watched is CM7/Core/Inc/event_signals.def - you never edit this file
 * to add a model signal. The previous-value table, the name table and the poll
 * loop are all built from that one list with the same X-macro trick telem.c and
 * params.c use, so the firmware and the .def can never disagree.
 *
 * Everything here is compare-and-maybe-print. It runs right after Model_Step()
 * in the superloop, never in an ISR, and costs nothing at all on a step where
 * nothing changed - which is almost every step.
 */

#include "events.h"
#include "console.h"            /* Console_Out() - the busy-safe TX primitive  */
#include "HCU_V2_Simulink.h"    /* the model inbox/outbox we observe (_U / _Y) */
#include "scheduler.h"          /* g_sched_ticks (the timestamp) + overruns    */
#include "air_safety.h"         /* g_air_safety - the fail-safe latch          */
#include "can.h"                /* g_can_stats  - bus recoveries / rx loss     */
#include "logger.h"             /* g_log_stats  - dropped log records          */
#include <stdio.h>
#include <string.h>

/* A pathological signal that toggles every step would emit 100 lines/s and tie
 * the superloop to the USB link. Cap what one step may emit; anything beyond is
 * counted as suppressed and reported by `events`. In normal running a step
 * raises zero or one event, so this never engages. */
#define EVENT_MAX_PER_POLL   8u

/* ------------------------------------------------------------------ */
/* The watched set, built from event_signals.def                      */
/* ------------------------------------------------------------------ */
enum {
    EVENT_COUNT =
#define EVENT_Y(name)   + 1
#define EVENT_U(name)   + 1
#include "event_signals.def"
#undef EVENT_Y
#undef EVENT_U
};

static const char *const s_names[EVENT_COUNT] = {
#define EVENT_Y(name)   #name,
#define EVENT_U(name)   #name,
#include "event_signals.def"
#undef EVENT_Y
#undef EVENT_U
};

/* Previous value per watched signal. double holds every model scalar type
 * exactly (they are all <= 32-bit int or float), so one table covers the lot. */
static double  s_prev[EVENT_COUNT];
static uint8_t s_seeded = 0;    /* first poll seeds, it does not emit */

/* ------------------------------------------------------------------ */
/* Stream state + history ring                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t    tick;
    const char *name;           /* literal / static - never copied */
    double      old_val;
    double      new_val;
} event_rec_t;

static uint8_t     s_on         = 1;    /* events are rare - on by default */
static uint32_t    s_raised     = 0;    /* events raised since boot        */
static uint32_t    s_suppressed = 0;    /* dropped by EVENT_MAX_PER_POLL   */
static event_rec_t s_ring[EVENT_RING_LEN];
static uint32_t    s_ring_n     = 0;    /* total ever stored (wraps the ring) */

/* ------------------------------------------------------------------ */
/* Formatting                                                         */
/* ------------------------------------------------------------------ */
/* newlib-nano printf has no %f. Discrete signals are integral in practice, so
 * print them as plain integers and only fall back to 4 dp if one is not. */
static void fmt_val(char *out, size_t n, double v)
{
    if (v != v) { snprintf(out, n, "nan"); return; }
    int    neg = (v < 0.0);
    double r   = neg ? -v : v;                 /* magnitude */
    if (r < 2147483000.0 && (double)(long)r == r) {
        snprintf(out, n, "%ld", (long)v);      /* the common case: an integer */
        return;
    }
    long whole = (long)r;
    long frac  = (long)((r - (double)whole) * 10000.0 + 0.5);
    if (frac >= 10000) { whole += 1; frac -= 10000; }
    snprintf(out, n, "%s%ld.%04ld", neg ? "-" : "", whole, frac);
}

/* STATIC buffer on purpose - CDC_Transmit_FS does not copy; see the identical
 * note in console.c. Only ever reached from the superloop, never reentrant. */
static void emit(uint32_t tick, const char *name, double old_val, double new_val)
{
    static char buf[96];
    char a[24], b[24];
    fmt_val(a, sizeof a, old_val);
    fmt_val(b, sizeof b, new_val);
    snprintf(buf, sizeof buf, "#E %lu %s %s %s\r\n",
             (unsigned long)tick, name, a, b);
    Console_Out(buf);
}

static void store(uint32_t tick, const char *name, double old_val, double new_val)
{
    event_rec_t *r = &s_ring[s_ring_n % EVENT_RING_LEN];
    r->tick    = tick;
    r->name    = name;
    r->old_val = old_val;
    r->new_val = new_val;
    s_ring_n++;
}

/* ------------------------------------------------------------------ */
/* Raising                                                            */
/* ------------------------------------------------------------------ */
static uint32_t s_this_poll = 0;      /* emissions used by the current poll */

static void raise_internal(const char *name, double old_val, double new_val)
{
    uint32_t tick = g_sched_ticks;
    s_raised++;
    store(tick, name, old_val, new_val);
    if (s_this_poll >= EVENT_MAX_PER_POLL) { s_suppressed++; return; }
    s_this_poll++;
    if (s_on) emit(tick, name, old_val, new_val);
}

void Events_Raise(const char *name, long old_val, long new_val)
{
    raise_internal(name, (double)old_val, (double)new_val);
}

/* ------------------------------------------------------------------ */
/* Firmware-internal watches                                          */
/* ------------------------------------------------------------------ */
/*
 * These are not model signals, so they cannot live in the .def. Flags are
 * watched as edges; COUNTERS are reported only on their first non-zero step -
 * "we started dropping frames at tick N" is the event, while the running total
 * belongs in `stats`. That keeps a degrading bus from flooding the timeline.
 */
static uint8_t  s_air_latched, s_air_armed, s_air_closed, s_pre_closed;
static uint8_t  s_had_overrun, s_had_logdrop, s_had_rx1, s_had_rx2;
static uint8_t  s_had_rec1, s_had_rec2, s_had_txf1, s_had_txf2;

#define WATCH_FLAG(store_, now_, label)  do {                                  \
        uint8_t _n = (uint8_t)(now_);                                          \
        if (_n != (store_)) { raise_internal((label), (store_), _n); (store_) = _n; } \
    } while (0)

#define WATCH_FIRST(store_, counter_, label)  do {                             \
        if (!(store_) && (counter_) > 0u) {                                    \
            (store_) = 1u; raise_internal((label), 0.0, (double)(counter_));   \
        }                                                                      \
    } while (0)

static void poll_firmware(void)
{
    WATCH_FLAG(s_air_latched, g_air_safety.stall_latched, "air.stall_latched");
    WATCH_FLAG(s_air_armed,   g_air_safety.armed,         "air.armed");
    WATCH_FLAG(s_air_closed,  g_air_safety.air_closed,    "air.AIR_closed");
    WATCH_FLAG(s_pre_closed,  g_air_safety.pre_closed,    "air.precharge_closed");

    WATCH_FIRST(s_had_overrun, g_sched_overruns,          "sched.first_overrun");
    WATCH_FIRST(s_had_logdrop, g_log_stats.drops,         "log.first_drop");
    WATCH_FIRST(s_had_rx1,     g_can_stats.bus1_rx_lost,  "can1.first_rx_lost");
    WATCH_FIRST(s_had_rx2,     g_can_stats.bus2_rx_lost,  "can2.first_rx_lost");
    WATCH_FIRST(s_had_rec1,    g_can_stats.bus1_recoveries, "can1.first_recovery");
    WATCH_FIRST(s_had_rec2,    g_can_stats.bus2_recoveries, "can2.first_recovery");
    WATCH_FIRST(s_had_txf1,    g_can_stats.bus1_tx_fail,  "can1.first_tx_fail");
    WATCH_FIRST(s_had_txf2,    g_can_stats.bus2_tx_fail,  "can2.first_tx_fail");
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void Events_Init(void)
{
    s_seeded = 0;                 /* first Events_Poll() seeds without emitting */
    s_raised = 0;
    s_suppressed = 0;
    s_ring_n = 0;
    /* Seed the firmware-side mirrors from the live state so boot itself does
     * not read as a burst of transitions. */
    s_air_latched = g_air_safety.stall_latched;
    s_air_armed   = g_air_safety.armed;
    s_air_closed  = g_air_safety.air_closed;
    s_pre_closed  = g_air_safety.pre_closed;
    s_had_overrun = s_had_logdrop = 0;
    s_had_rx1 = s_had_rx2 = s_had_rec1 = s_had_rec2 = 0;
    s_had_txf1 = s_had_txf2 = 0;
}

void Events_Poll(void)
{
    uint32_t i = 0;
    double   now;

    s_this_poll = 0;

    /* Walk the .def in the same order as s_names[]. The index must advance for
     * every entry, which is why both macros expand to the same shape. */
#define EVENT_CHECK(value)  do {                                               \
        now = (double)(value);                                                 \
        if (!s_seeded) {                                                       \
            s_prev[i] = now;                                                   \
        } else if (now != s_prev[i]) {                                         \
            raise_internal(s_names[i], s_prev[i], now);                        \
            s_prev[i] = now;                                                   \
        }                                                                      \
        i++;                                                                   \
    } while (0)

#define EVENT_Y(name)   EVENT_CHECK(HCU_V2_Simulink_Y.name);
#define EVENT_U(name)   EVENT_CHECK(HCU_V2_Simulink_U.name);
#include "event_signals.def"
#undef EVENT_Y
#undef EVENT_U
#undef EVENT_CHECK

    poll_firmware();

    s_seeded = 1;
    (void)i;
}

void Events_SetStreaming(int on) { s_on = on ? 1u : 0u; }
int  Events_IsStreaming(void)    { return (int)s_on; }
uint32_t Events_Count(void)      { return s_raised; }
uint32_t Events_Watched(void)    { return (uint32_t)EVENT_COUNT; }

void Events_Clear(void)
{
    s_ring_n     = 0;
    s_raised     = 0;
    s_suppressed = 0;
}

void Events_PrintStatus(void)
{
    static char buf[128];
    snprintf(buf, sizeof buf,
             "events %s  watching %lu signals  raised %lu  stored %lu  suppressed %lu\r\n",
             s_on ? "on" : "off",
             (unsigned long)EVENT_COUNT,
             (unsigned long)s_raised,
             (unsigned long)((s_ring_n < EVENT_RING_LEN) ? s_ring_n : EVENT_RING_LEN),
             (unsigned long)s_suppressed);
    Console_Out(buf);
}

void Events_PrintHistory(void)
{
    /* Oldest first, so the GUI can append them to the timeline in order. */
    uint32_t have  = (s_ring_n < EVENT_RING_LEN) ? s_ring_n : EVENT_RING_LEN;
    uint32_t start = s_ring_n - have;

    Console_Out("events:\r\n");
    for (uint32_t k = 0; k < have; k++) {
        const event_rec_t *r = &s_ring[(start + k) % EVENT_RING_LEN];
        emit(r->tick, r->name, r->old_val, r->new_val);
    }
    Console_Out("end\r\n");
}
