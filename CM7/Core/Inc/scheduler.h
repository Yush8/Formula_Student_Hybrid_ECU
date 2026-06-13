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
 */
bool Sched_StepDue(void);

/*
 * Diagnostics - for the debugger / future `stat` console command only.
 * Deliberately NOT fed into the model (same policy as g_can_stats).
 *   g_sched_overruns == 0  ->  every deadline has been met since boot.
 */
extern uint32_t          g_sched_overruns;   /* missed model-step deadlines since boot */
extern volatile uint32_t g_sched_ticks;      /* total ticks generated since boot       */

#endif /* SCHEDULER_H */
