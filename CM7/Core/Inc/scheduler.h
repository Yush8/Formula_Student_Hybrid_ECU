/*
 * scheduler.h
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Deterministic time base for the model step.
 *
 * TIM6 is the metronome: it fires at the model's base rate, and its interrupt
 * does almost nothing - it just counts ticks. The superloop asks Sched_StepDue()
 * each pass and runs ONE Model_Step() when a tick is due.
 *
 * This replaces the old "if (HAL_GetTick() - last >= 10)" rate limiter. The
 * cadence is now exact (TIM6 is hardware, so it never drifts with loop load),
 * and a missed deadline is COUNTED instead of silently ignored.
 *
 *   Sched_Init()      - call once at boot, AFTER Model_Init() (so the model is
 *                       ready before the first tick can be serviced). It only
 *                       starts the timer interrupt; TIM6 itself is set up by
 *                       CubeMX (MX_TIM6_Init).
 *   Sched_StepDue()   - call every superloop pass; run Model_Step() when true.
 *
 *
 * WHERE THE TIMER IS CONFIGURED
 * TIM6 is owned by CubeMX (Timers -> TIM6, on the CM7 context), the same as
 * every other peripheral. Its Prescaler/Counter Period set the rate. To match
 * Simulink's 0.01 s step at the 75 MHz TIM6 clock:
 *     Prescaler      = 74     -> 75 MHz / 75   = 1 MHz (1 us per count)
 *     Counter Period = 9999   ->  1 MHz / 10000 = 100 Hz = 10.000 ms
 * and enable the "TIM6 global interrupt" in NVIC (preemption priority 5, below
 * the FDCAN interrupts so CAN Rx always pre-empts the tick).
 *
 * >>> THE RATE MUST MATCH SIMULINK <<<
 * SCHED_RATE_HZ below states the intended rate (1 / Simulink fixed-step). If you
 * ever change the model's base sample time, change it in THREE agreeing places:
 * Simulink fixed-step, the CubeMX TIM6 Counter Period, and SCHED_RATE_HZ here.
 * Changing the base rate is rare; day to day you never touch any of this.
 */
#define SCHED_RATE_HZ   100u   /* must equal 1 / Simulink step AND the CubeMX TIM6 rate */

void Sched_Init(void);

/*
 * Returns true once per elapsed tick; run Model_Step() when it does.
 *
 * "Run-latest": if several ticks elapsed since the last call (the loop fell
 * behind), it returns true ONCE and counts the skipped ticks as overruns. It
 * never fires Model_Step() back-to-back to "catch up" - bursting steps would
 * compress time and is wrong for a control loop.
 *
 * On the true branch it also starts the step stopwatch and records how late we
 * were to the tick (see Sched_StepDone and sched_timing_t below).
 */
bool Sched_StepDue(void);

/*
 * Call immediately AFTER Model_Step(). Stops the stopwatch and folds the result
 * into g_sched_timing. Pairing is what makes the numbers mean anything, so keep
 * these two wrapped tightly around the step:
 *
 *     if (Sched_StepDue()) { Model_Step(); Sched_StepDone(); }
 */
void Sched_StepDone(void);

/*
 * How hard the 100 Hz loop is actually working - the difference between "we have
 * not missed a deadline yet" and "we are nowhere near missing one".
 *
 *   step_us     how long Model_Step() itself took.
 *   latency_us  how long after the TIM6 tick edge the step actually STARTED.
 *               This is the jitter that matters: the tick is hardware and never
 *               drifts, but the superloop only notices it between other jobs, so
 *               a slow Console_Poll or telemetry frame shows up here.
 *
 * Budget is 10000 us per tick (SCHED_RATE_HZ). step_us + latency_us staying well
 * under that is the real headroom figure; g_sched_overruns only tells you after
 * you have already run out. The histogram buckets step_us so an occasional long
 * step is visible even though the mean looks fine.
 *
 * Diagnostics only - never fed to the model, same policy as g_can_stats.
 */
#define SCHED_HIST_BUCKETS  8u

typedef struct {
    uint32_t samples;             /* steps measured since boot / stats clear  */
    uint32_t step_us_last;
    uint32_t step_us_min;
    uint32_t step_us_max;
    uint64_t step_us_sum;         /* /samples = mean                          */
    uint32_t lat_us_last;
    uint32_t lat_us_min;
    uint32_t lat_us_max;
    uint64_t lat_us_sum;
    uint32_t hist[SCHED_HIST_BUCKETS];  /* step_us distribution, see the edges */
} sched_timing_t;

extern sched_timing_t g_sched_timing;

/* Upper edge (us, inclusive) of each histogram bucket; the last is "and over". */
extern const uint32_t g_sched_hist_edges[SCHED_HIST_BUCKETS];

/*
 * Diagnostics - for the debugger / future `stat` console command only.
 * Deliberately NOT fed into the model (same policy as g_can_stats).
 *   g_sched_overruns == 0  ->  every deadline has been met since boot.
 */
extern uint32_t          g_sched_overruns;   /* missed model-step deadlines since boot */
extern volatile uint32_t g_sched_ticks;      /* total ticks generated since boot       */

/* Zero the overrun counter and the timing statistics (for the `stats clear`
 * console command). Deliberately leaves g_sched_ticks and the internal serviced
 * count alone: those two track the live time base, and zeroing only one of them
 * would desync the pair and fabricate a huge overrun on the next step. */
void Sched_ClearStats(void);

#endif /* SCHEDULER_H */
