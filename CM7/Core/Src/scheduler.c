/*
 * scheduler.c
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 *
 * Deterministic model-step time base. See scheduler.h for the contract and the
 * rate setting (SCHED_RATE_HZ).
 *
 * TIM6 itself is configured by CubeMX (MX_TIM6_Init) - clock, prescaler/period
 * and NVIC all live in the .ioc, the intended way. This module only:
 *   - starts the timer interrupt (Sched_Init),
 *   - counts ticks in the HAL update callback,
 *   - hands ticks to the superloop with run-latest semantics (Sched_StepDue).
 * You never edit this file to add a Simulink signal.
 */

#include "scheduler.h"
#include "main.h"        /* HAL TIM API + the CubeMX-generated htim6 handle */
#include "air_safety.h"  /* AirSafety_Supervise() - the independent AIR fail-safe runs in this ISR */

extern TIM_HandleTypeDef htim6;   /* defined by CubeMX in main.c (MX_TIM6_Init) */

/* g_sched_ticks: written only in the timer callback (the canonical tick count).
 * s_serviced / g_sched_overruns: written only by the loop. Single-writer each,
 * 32-bit aligned -> reads/writes are atomic on the M7, no masking needed. */
volatile uint32_t g_sched_ticks    = 0;
static   uint32_t s_serviced       = 0;
uint32_t          g_sched_overruns = 0;

/* ------------------------------------------------------------------ */
/* Step timing  (see sched_timing_t in scheduler.h)                   */
/* ------------------------------------------------------------------ */
/*
 * The clock here is TIM6's own counter, not the CPU cycle counter. CubeMX runs
 * TIM6 at 1 MHz with a 10000-count period (see scheduler.h), so CNT reads
 * directly in microseconds-since-the-tick-edge and needs no assumption about
 * the core clock - if the CPU frequency ever changes these numbers stay right.
 * A step longer than one period is handled by counting the ticks it spanned.
 */
sched_timing_t g_sched_timing;

const uint32_t g_sched_hist_edges[SCHED_HIST_BUCKETS] = {
    100u, 250u, 500u, 1000u, 2000u, 4000u, 8000u, 0xFFFFFFFFu  /* last = and over */
};

static uint32_t s_t0_tick;     /* tick count when the step started    */
static uint32_t s_t0_cnt;      /* TIM6 CNT   when the step started    */
static uint32_t s_lat_us;      /* lateness measured for the step now running */

/* Consistent (tick, CNT) pair: re-read if TIM6 wrapped between the two reads,
 * which would otherwise pair a new tick with an old count and fabricate a 10 ms
 * step. The loop runs twice at worst. */
static void read_time(uint32_t *tick, uint32_t *cnt)
{
    uint32_t t1, c, t2;
    do {
        t1 = g_sched_ticks;
        c  = htim6.Instance->CNT;
        t2 = g_sched_ticks;
    } while (t1 != t2);
    *tick = t1;
    *cnt  = c;
}

static void timing_add(uint32_t step_us, uint32_t lat_us)
{
    sched_timing_t *g = &g_sched_timing;

    if (g->samples == 0u) {
        g->step_us_min = step_us;  g->step_us_max = step_us;
        g->lat_us_min  = lat_us;   g->lat_us_max  = lat_us;
    } else {
        if (step_us < g->step_us_min) g->step_us_min = step_us;
        if (step_us > g->step_us_max) g->step_us_max = step_us;
        if (lat_us  < g->lat_us_min)  g->lat_us_min  = lat_us;
        if (lat_us  > g->lat_us_max)  g->lat_us_max  = lat_us;
    }
    g->step_us_last = step_us;
    g->lat_us_last  = lat_us;
    g->step_us_sum += step_us;
    g->lat_us_sum  += lat_us;
    g->samples++;

    for (uint32_t b = 0u; b < SCHED_HIST_BUCKETS; b++) {
        if (step_us <= g_sched_hist_edges[b]) { g->hist[b]++; break; }
    }
}

void Sched_Init(void)
{
    /* Drop any update flag left from MX_TIM6_Init so the first tick is a full
     * period away, then enable the periodic update interrupt. */
    __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&htim6);
}

bool Sched_StepDue(void)
{
    uint32_t ticks, cnt;
    read_time(&ticks, &cnt);

    if (ticks == s_serviced)
    {
        return false;                        /* no new tick since last step */
    }

    uint32_t skipped = (ticks - s_serviced) - 1u;    /* ticks we skipped over */
    g_sched_overruns += skipped;
    s_serviced        = ticks;               /* jump to latest, no catch-up burst */

    /* Start the stopwatch, and record how late we were to this tick. CNT is us
     * since the most recent edge; any skipped tick is a whole period on top, so
     * lateness stays honest when the loop has fallen behind. */
    s_t0_tick = ticks;
    s_t0_cnt  = cnt;
    s_lat_us  = skipped * (htim6.Instance->ARR + 1u) + cnt;
    return true;
}

void Sched_StepDone(void)
{
    uint32_t t1, c1;
    read_time(&t1, &c1);
    uint32_t period = htim6.Instance->ARR + 1u;          /* 10000 us */
    uint32_t step_us = (t1 - s_t0_tick) * period + c1 - s_t0_cnt;
    timing_add(step_us, s_lat_us);
}

void Sched_ClearStats(void)
{
    /* The overrun tally and the timing statistics. g_sched_ticks / s_serviced
     * must stay in step (see scheduler.h). Single-writer from the loop, same
     * context as the only other writer (Sched_StepDue) - no masking needed. */
    g_sched_overruns = 0u;
    for (uint32_t b = 0u; b < SCHED_HIST_BUCKETS; b++) g_sched_timing.hist[b] = 0u;
    g_sched_timing.samples      = 0u;
    g_sched_timing.step_us_sum  = 0u;
    g_sched_timing.lat_us_sum   = 0u;
    g_sched_timing.step_us_min  = 0u;
    g_sched_timing.step_us_max  = 0u;
    g_sched_timing.lat_us_min   = 0u;
    g_sched_timing.lat_us_max   = 0u;
}

/* HAL update callback, reached via TIM6_DAC_IRQHandler -> HAL_TIM_IRQHandler
 * (both generated by CubeMX). It is shared by every timer, so guard on the
 * instance - today only TIM6 uses it. Kept tiny: just count the tick. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        g_sched_ticks++;
        /* Independent AIR fail-safe: enforced HERE, in the ISR, so it keeps working
         * even if the cooperative superloop (or the model) hangs. Tiny + ISR-safe
         * (GPIO + integer compares only). It reads the just-incremented tick to age
         * the model's proof-of-life heartbeat. See air_safety.c. */
        AirSafety_Supervise();
    }
}
