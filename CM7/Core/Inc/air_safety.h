/*
 * air_safety.h
 *
 *  Created on: 1 Jul 2026
 *      Author: Yusha
 *
 * INDEPENDENT CONTROLLER-FREEZE FAIL-SAFE  (docs/ARCHITECTURE.md section 5).
 *
 * This module does ONE thing that nothing else in the system can do: it stops a
 * FROZEN or CRASHED controller from holding the tractive system live. If the
 * Simulink step hangs with AIR_Enable stuck "closed", the AIR sink stays driven
 * and the AIRs stay shut with nobody in control - and the model cannot detect its
 * own death. So an independent watchdog OUTSIDE the model forces the AIRs (and
 * pre-charge) open when the 100 Hz model step stops advancing.
 *
 * SCOPE - what this is NOT (deliberately trimmed - docs/ARCHITECTURE.md section 5):
 *   - It does NOT read or act on the SDC / shutdown circuit. A real SDC open
 *     de-energises the AIR coils DIRECTLY in hardware (rules-mandated); the model
 *     reads SDC_Monitor itself for shutdown intent (zero torque, open AIR, re-run
 *     pre-charge on re-close). Duplicating that in C would be redundant with both
 *     the hardware and the model, so it lives only where it has context: Simulink.
 *   - It does NOT police a *running* model's decisions. While the model is alive
 *     this layer trusts its AIR request - it only ever VETOES (forces open) on a
 *     freeze, it never closes a relay the model did not ask for. A bug in here is
 *     at worst a nuisance open (safe); it can never weld the TS live.
 *
 * WHERE IT RUNS
 *   AirSafety_Supervise() is called from the TIM6 update ISR (the 100 Hz metronome
 *   that drives the model - see scheduler.c). The ISR pre-empts the cooperative
 *   superloop, so even an infinite loop in the model or a hung console cannot stop
 *   it yanking the AIRs. This module is therefore the SOLE WRITER of the
 *   AIR_Sink / Precharge_Sink pins - the bridge hands a *request* to
 *   AirSafety_SetRequest() instead of driving the pins itself.
 *
 * WHAT IT CANNOT CATCH (and why you still want the external watchdog)
 *   If the CPU/clock/TIM6 itself dies, the supervisor dies with it and the sink
 *   pins hold their last state. Only an EXTERNAL hardware watchdog (STWD100 on the
 *   Watchdog pin, §14.3) catches a total lockup. The kick hook is built in below
 *   but compile-time OFF until its timing window is confirmed - see AIR_SAFETY_EXT_WDT.
 *
 * The freeze threshold is `const` here, NEVER in params.def - no console command or
 * edited flash image may ever move a safety trip (params.def says the same).
 *
 * You never edit this file to add a Simulink signal.
 */
#ifndef AIR_SAFETY_H
#define AIR_SAFETY_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Live diagnostics (watch in the debugger like g_can_stats / g_sdlog_status,
 *      or read it trackside with the `safety` console command). NOT fed to the
 *      model - same debug-only-global policy as the rest of the firmware. */
typedef struct {
    uint8_t  model_alive;       /* 1 = the 100 Hz model step is fresh                 */
    uint8_t  armed;             /* 1 = the model has stepped at least once            */
    uint8_t  stall_latched;     /* 1 = a freeze was declared; AIRs latched open       */
    uint8_t  air_closed;        /* 1 = we are driving the AIR sink CLOSED (energised) */
    uint8_t  pre_closed;        /* 1 = we are driving the pre-charge sink CLOSED      */
    uint32_t stall_age_ticks;   /* model-step ticks since the last proof of life      */
    uint32_t stall_trips;       /* count of freeze latches since boot                 */
    uint32_t supervise_calls;   /* ISR heartbeat - climbing => the supervisor is live */
} air_safety_status_t;

extern volatile air_safety_status_t g_air_safety;

/* Boot init: force both sinks OPEN and clear state. Call once in main() USER CODE 2,
 * AFTER MX_GPIO_Init() and BEFORE Sched_Init() (the TIM6 ISR starts calling
 * AirSafety_Supervise() the moment the scheduler timer is started). */
void AirSafety_Init(void);

/* The model's latest AIR / pre-charge request + proof of life. Called once per
 * model step from the bridge dispatch (replaces the old direct GPIO writes). The
 * supervisor passes these through its freeze gate; it never closes a relay unless
 * the model asked AND the model is alive. This call also stamps the liveness
 * heartbeat - calling it IS the proof the model stepped this tick. */
void AirSafety_SetRequest(bool air_closed_req, bool precharge_closed_req);

/* The independent supervisor. Called every tick from the TIM6 update ISR (see
 * scheduler.c). Runs the freeze watchdog, maintains the latch, drives the sink pins
 * and the AIR/pre-charge LEDs. Tiny + ISR-safe (GPIO + integer compares only; no
 * HAL blocking calls, no console). */
void AirSafety_Supervise(void);

/* Clear the freeze latch (the `safety reset` console command). A stationary action.
 * Refuses (returns false) unless the loop is currently alive again, so you can never
 * reset while still frozen. Briefly masks the TIM6 IRQ so the clear is atomic
 * against the supervisor. */
bool AirSafety_Reset(void);

#endif /* AIR_SAFETY_H */
