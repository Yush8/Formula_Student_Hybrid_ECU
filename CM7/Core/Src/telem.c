/*
 * telem.c  --  live USB telemetry stream (read-only observer of the model)
 *
 *  Created on: 30 Jun 2026
 *      Author: Yusha
 *
 * See telem.h. WHAT gets streamed is CM7/Core/Inc/telem_signals.def - you never
 * edit this file to add a signal. The frame builder and the schema dump both
 * #include that .def with different macro definitions, exactly like the bridge
 * packs the log record and params.c builds its table - so the firmware and the
 * GUI can never disagree about the signal set.
 *
 * Type handling: the value formatter is chosen with C11 _Generic from the field
 * type in the model struct, so the .def never has to state a type and a wrong
 * type is impossible. boolean_T is the same underlying type as uint8_T, so both
 * print as small unsigned integers (0/1 for a boolean) - that is fine.
 */

#include "telem.h"
#include "console.h"            /* Console_Out() - the busy-safe TX primitive   */
#include "HCU_V2_Simulink.h"    /* the model inbox/outbox we observe (_U / _Y)  */
#include "scheduler.h"          /* g_sched_ticks - the time axis stamped in each frame */
#include "main.h"               /* HAL_GetTick() for rate timing                */
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Streaming state                                                    */
/* ------------------------------------------------------------------ */
#define TELEM_DEFAULT_RATE_HZ  20u
#define TELEM_FRAME_BUF        2560   /* one frame; plenty for the full .def     */

static uint8_t  s_on        = 0;
static uint16_t s_period_ms = 1000u / TELEM_DEFAULT_RATE_HZ;
static uint32_t s_last_ms   = 0;

/* How many signals the .def lists (compile-time, excludes the auto `tick`). */
enum {
    TELEM_SIGNAL_COUNT =
#define TELEM_Y(name)              + 1
#define TELEM_U(name)              + 1
#define TELEM_ARRAY_Y(name, n)     + 1
#define TELEM_ARRAY_U(name, n)     + 1
#include "telem_signals.def"
#undef TELEM_Y
#undef TELEM_U
#undef TELEM_ARRAY_Y
#undef TELEM_ARRAY_U
};

/* ------------------------------------------------------------------ */
/* String builder (bounded; never overflows the frame buffer)         */
/* ------------------------------------------------------------------ */
typedef struct { char *buf; size_t cap; size_t len; } sbuf_t;

static void sb_str(sbuf_t *b, const char *s)
{
    while (*s && b->len < b->cap - 1u) b->buf[b->len++] = *s++;
    b->buf[b->len] = '\0';
}

static void sb_ch(sbuf_t *b, char c)
{
    if (b->len < b->cap - 1u) { b->buf[b->len++] = c; b->buf[b->len] = '\0'; }
}

/* newlib-nano printf has no %f, so format reals by hand (4 dp). */
static void fmt_real(char *out, size_t n, double v)
{
    if (v != v) { snprintf(out, n, "nan"); return; }   /* NaN */
    int neg = (v < 0.0);
    if (neg) v = -v;
    long whole = (long)v;
    long frac  = (long)((v - (double)whole) * 10000.0 + 0.5);
    if (frac >= 10000) { whole += 1; frac -= 10000; }
    snprintf(out, n, "%s%ld.%04ld", neg ? "-" : "", whole, frac);
}

static void sb_i(sbuf_t *b, long v)          { char t[16]; snprintf(t, sizeof t, "%ld", v);  sb_str(b, t); }
static void sb_u(sbuf_t *b, unsigned long v) { char t[16]; snprintf(t, sizeof t, "%lu", v);  sb_str(b, t); }
static void sb_r(sbuf_t *b, double v)        { char t[24]; fmt_real(t, sizeof t, v);         sb_str(b, t); }

/* Pick the formatter from the field's own C type - no type token in the .def. */
#define SB_VAL(b, v) _Generic((v),                                              \
        float: sb_r,          double: sb_r,                                     \
        signed char: sb_i,    short: sb_i,    int: sb_i,    long: sb_i,         \
        unsigned char: sb_u,  unsigned short: sb_u,                            \
        unsigned int: sb_u,   unsigned long: sb_u                              \
    )((b), (v))

/* Likewise a short type label for the `telem list` schema. */
#define VAL_TYPE(v) _Generic((v),                                              \
        float: "f32",         double: "f64",                                   \
        signed char: "i8",    short: "i16",   int: "i32",   long: "i32",       \
        unsigned char: "u8",  unsigned short: "u16",                          \
        unsigned int: "u32",  unsigned long: "u32"                            \
    )

#define EMIT_NAME(nm)  do { sb_ch(&sb, ' '); sb_str(&sb, nm); sb_ch(&sb, '='); } while (0)

#define EMIT_SCALAR(nm, expr)  do {                                            \
        EMIT_NAME(nm); SB_VAL(&sb, (expr));                                    \
    } while (0)

#define EMIT_ARRAY(nm, expr, n)  do {                                          \
        EMIT_NAME(nm);                                                         \
        for (int _i = 0; _i < (n); ++_i) {                                     \
            if (_i) sb_ch(&sb, ',');                                           \
            SB_VAL(&sb, (expr)[_i]);                                           \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Frame builder                                                      */
/* ------------------------------------------------------------------ */
static char s_frame[TELEM_FRAME_BUF];   /* static: USB TX reads from it after   */
                                        /* Console_Out returns (no copy in CDC) */

static void build_frame(void)
{
    sbuf_t sb = { s_frame, sizeof s_frame, 0 };
    sb_str(&sb, "#T");
    EMIT_SCALAR("tick", (unsigned long)g_sched_ticks);
    /* Loop-health counter, auto-added alongside `tick`: it is a scheduler
     * diagnostic, not a model signal, so it is hardcoded here rather than listed
     * in telem_signals.def (exactly like `tick`). */
    EMIT_SCALAR("overruns", (unsigned long)g_sched_overruns);

#define TELEM_Y(name)            EMIT_SCALAR(#name, HCU_V2_Simulink_Y.name);
#define TELEM_U(name)            EMIT_SCALAR(#name, HCU_V2_Simulink_U.name);
#define TELEM_ARRAY_Y(name, n)   EMIT_ARRAY(#name, HCU_V2_Simulink_Y.name, (n));
#define TELEM_ARRAY_U(name, n)   EMIT_ARRAY(#name, HCU_V2_Simulink_U.name, (n));
#include "telem_signals.def"
#undef TELEM_Y
#undef TELEM_U
#undef TELEM_ARRAY_Y
#undef TELEM_ARRAY_U

    sb_str(&sb, "\r\n");
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void Telem_Init(void)
{
    s_on        = 0;
    s_period_ms = (uint16_t)(1000u / TELEM_DEFAULT_RATE_HZ);
    s_last_ms   = HAL_GetTick();
}

void Telem_Service(void)
{
    if (!s_on) return;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last_ms) < s_period_ms) return;
    s_last_ms = now;

    build_frame();
    Console_Out(s_frame);
}

void Telem_SetStreaming(int on)
{
    s_on = on ? 1u : 0u;
    s_last_ms = HAL_GetTick();   /* emit promptly on enable */
}

int Telem_IsStreaming(void) { return s_on; }

void Telem_SetRateHz(uint32_t hz)
{
    if (hz < 1u)                 hz = 1u;
    if (hz > TELEM_MAX_RATE_HZ)  hz = TELEM_MAX_RATE_HZ;
    s_period_ms = (uint16_t)(1000u / hz);
    if (s_period_ms == 0u) s_period_ms = 1u;
}

uint32_t Telem_GetRateHz(void)
{
    return (s_period_ms == 0u) ? TELEM_MAX_RATE_HZ : (1000u / s_period_ms);
}

uint32_t Telem_Count(void) { return (uint32_t)TELEM_SIGNAL_COUNT; }

/* ----- `telem list` : the schema the GUI uses to lay out its grid ----- */
static void schema_line(const char *name, const char *type, int len)
{
    char buf[48];
    snprintf(buf, sizeof buf, "%s %s %d\r\n", name, type, len);
    Console_Out(buf);
}

void Telem_PrintSchema(void)
{
    Console_Out("telem signals:\r\n");
    schema_line("tick", "u32", 1);
    schema_line("overruns", "u32", 1);   /* auto-added loop-health counter (see build_frame) */

#define TELEM_Y(name)            schema_line(#name, VAL_TYPE(HCU_V2_Simulink_Y.name), 1);
#define TELEM_U(name)            schema_line(#name, VAL_TYPE(HCU_V2_Simulink_U.name), 1);
#define TELEM_ARRAY_Y(name, n)   schema_line(#name, VAL_TYPE(HCU_V2_Simulink_Y.name[0]), (n));
#define TELEM_ARRAY_U(name, n)   schema_line(#name, VAL_TYPE(HCU_V2_Simulink_U.name[0]), (n));
#include "telem_signals.def"
#undef TELEM_Y
#undef TELEM_U
#undef TELEM_ARRAY_Y
#undef TELEM_ARRAY_U

    Console_Out("end\r\n");
}

void Telem_PrintStatus(void)
{
    char buf[64];
    snprintf(buf, sizeof buf, "telem %s  rate %lu Hz  signals %lu\r\n",
             s_on ? "on" : "off",
             (unsigned long)Telem_GetRateHz(),
             (unsigned long)Telem_Count());
    Console_Out(buf);
}
