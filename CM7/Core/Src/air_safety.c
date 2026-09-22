/*
 * air_safety.c
 *
 *  Created on: 1 Jul 2026
 *      Author: Yusha
 *
 * Independent controller-freeze fail-safe. See air_safety.h for the full contract
 * and scope. In short:
 *   - SOLE writer of AIR_Sink / Precharge_Sink. HIGH = sink on = relay energised =
 *     CLOSED; LOW (the GPIO reset/boot default) = OPEN = safe.
 *   - Runs AirSafety_Supervise() from the 100 Hz TIM6 ISR, so it enforces even if
 *     the cooperative superloop is wedged.
 *   - Gate: AIRs may close only when armed AND the model is alive AND not
 *     freeze-latched. It only ever subtracts authority from the model's request.
 *   - It does NOT touch the SDC: a real shutdown opens the AIR coils in hardware,
 *     and the model reads SDC_Monitor itself for intent (docs/ARCHITECTURE.md section 5).
 */

#include "air_safety.h"
#include "main.h"          /* HAL GPIO + the AIR_Sink_* / *_LED_* / Watchdog_* pin macros */
#include "scheduler.h"     /* g_sched_ticks (the tick the heartbeat stamps) + SCHED_RATE_HZ   */

/* ============================== TUNING (const) =============================
 * SAFETY TRIP - deliberately compile-time const, NEVER in params.def, so no
 * console `set` or edited flash image can move it. Time is in milliseconds and
 * converted to model-step ticks via the scheduler rate, so it stays correct if the
 * base rate ever changes.
 */

/* How long the model step may be missing before we declare a freeze and LATCH the
 * AIRs open. 500 ms is chosen to MINIMISE NUISANCE: it is comfortably longer than
 * any legitimate cooperative stall (a big no-host console dump can block the
 * superloop for a few hundred ms - docs/ARCHITECTURE.md section 7), yet still safe - a frozen
 * model stops the ODrive torque setpoint stream, and the ODrive rx-watchdog already
 * disarms the axes (zero torque) within ~100 ms, so propulsion is gone long before
 * this fires. This latch is the final TS-isolation backstop, not the torque cut.
 * A single missed tick / jitter never trips it (the heartbeat only has to advance
 * once per window). Lower it only if you also bound console print time while armed. */
#define AIR_STALL_TRIP_MS    500u

/* ms -> ticks (SCHED_RATE_HZ = 100 -> 1 tick = 10 ms). */
#define AIR_MS_TO_TICKS(ms)    (((ms) * SCHED_RATE_HZ) / 1000u)
#define AIR_STALL_TRIP_TICKS   AIR_MS_TO_TICKS(AIR_STALL_TRIP_MS)

/* ---- External hardware watchdog (STWD100 on the Watchdog pin, §14.3) ----------
 * Compile-time OFF. The firmware supervisor cannot catch its OWN clock/CPU dying;
 * the STWD100 is the only layer that does. Enable (set to 1) ONLY after you have
 * confirmed the chip's timeout window and that AIR_WDT_KICK_MS edges it inside that
 * window - too slow a kick = the watchdog resets the (healthy) MCU = a nasty
 * nuisance. The kick is gated on loop liveness: stop kicking on a freeze so the
 * STWD100 times out and drops the AIRs in hardware. */
#define AIR_SAFETY_EXT_WDT   0
#define AIR_WDT_KICK_MS      20u
#define AIR_WDT_KICK_TICKS   (AIR_MS_TO_TICKS(AIR_WDT_KICK_MS) > 0u ? \
                              AIR_MS_TO_TICKS(AIR_WDT_KICK_MS) : 1u)

/* ============================== STATE =====================================
 * Single-writer discipline (so reads/writes are atomic on the M7, no locks):
 *   - s_req_* / s_model_heartbeat / s_armed : written ONLY by the superloop
 *     (AirSafety_SetRequest); read by the ISR.
 *   - s_stall_latched : written ONLY by the ISR (AirSafety_Supervise); read by the
 *     superloop/console for status.
 * AirSafety_Reset() is the one cross-context write to s_stall_latched and masks the
 * TIM6 IRQ to stay atomic against the ISR.
 */
volatile air_safety_status_t g_air_safety;

static volatile bool     s_req_air         = false; /* model's latest AIR request       */
static volatile bool     s_req_pre         = false; /* model's latest pre-charge request */
static volatile bool     s_armed           = false; /* model has stepped at least once  */
static volatile uint32_t s_model_heartbeat = 0u;    /* g_sched_ticks at the last step    */

static bool s_stall_latched = false;                /* freeze latch (ISR-owned, +Reset)  */

#if AIR_SAFETY_EXT_WDT
static uint32_t s_wdt_div   = 0u;
static uint8_t  s_wdt_level = 0u;
#endif

/* Drive both sinks open. Used at init before anything else. */
static void force_open(void)
{
    HAL_GPIO_WritePin(AIR_Sink_GPIO_Port,       AIR_Sink_Pin,       GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Precharge_Sink_GPIO_Port, Precharge_Sink_Pin, GPIO_PIN_RESET);
}

void AirSafety_Init(void)
{
    /* Start from the safe state: model not yet armed, no latch, sinks driven open. */
    s_req_air = false;
    s_req_pre = false;
    s_armed   = false;
    s_model_heartbeat = 0u;
    s_stall_latched   = false;

    air_safety_status_t z = {0};
    g_air_safety = z;

    force_open();
}

void AirSafety_SetRequest(bool air_closed_req, bool precharge_closed_req)
{
    s_req_air = air_closed_req;
    s_req_pre = precharge_closed_req;
    __DMB();                              /* publish the request before the heartbeat */
    s_model_heartbeat = g_sched_ticks;    /* proof of life: the model ran this tick    */
    s_armed = true;
}

void AirSafety_Supervise(void)
{
    uint32_t now = g_sched_ticks;
    g_air_safety.supervise_calls++;

    /* ---- 1. freeze watchdog (only after the model has armed) ------------- */
    uint32_t age = now - s_model_heartbeat;       /* unsigned: wrap-safe */
    bool alive = (age < AIR_STALL_TRIP_TICKS);
    if (s_armed && !alive && !s_stall_latched) {
        s_stall_latched = true;                   /* freeze declared -> latch open */
        g_air_safety.stall_trips++;
    }

    /* ---- 2. the gate: AIRs may close ONLY when the model is alive -------- */
    bool may_close = s_armed && alive && !s_stall_latched;
    bool air = may_close && s_req_air;
    bool pre = may_close && s_req_pre;

    /* ---- 3. drive the sinks (sole writer) - HIGH = energised = closed ---- */
    HAL_GPIO_WritePin(AIR_Sink_GPIO_Port,       AIR_Sink_Pin,       air ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Precharge_Sink_GPIO_Port, Precharge_Sink_Pin, pre ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* ---- 4. status LEDs (active-low: ON = drive the pin LOW) ------------- */
    HAL_GPIO_WritePin(AIR_LED_GPIO_Port,       AIR_LED_Pin,       air ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(Precharge_LED_GPIO_Port, Precharge_LED_Pin, pre ? GPIO_PIN_RESET : GPIO_PIN_SET);

    /* ---- 5. publish diagnostics ----------------------------------------- */
    g_air_safety.model_alive     = alive;
    g_air_safety.armed           = s_armed;
    g_air_safety.stall_latched   = s_stall_latched;
    g_air_safety.air_closed      = air;
    g_air_safety.pre_closed      = pre;
    g_air_safety.stall_age_ticks = age;

    /* ---- 6. external hardware watchdog kick (compile-time OFF by default) - */
#if AIR_SAFETY_EXT_WDT
    /* Kick ONLY while the loop is provably alive; stop kicking on freeze so the
     * STWD100 times out and drops the AIRs in hardware. */
    if (s_armed && alive && !s_stall_latched) {
        if (++s_wdt_div >= AIR_WDT_KICK_TICKS) {
            s_wdt_div = 0u;
            s_wdt_level ^= 1u;
            HAL_GPIO_WritePin(Watchdog_GPIO_Port, Watchdog_Pin,
                              s_wdt_level ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
#endif
}

bool AirSafety_Reset(void)
{
    bool ok = false;

    /* Atomic against the supervisor: mask just the TIM6 tick IRQ for the few
     * instructions of the check-and-clear (same idea as Can_Snapshot masking the
     * CAN Rx IRQs). Refuse unless the loop is alive again, so a reset can never
     * re-arm while still frozen. */
    HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
    bool alive = s_armed && ((g_sched_ticks - s_model_heartbeat) < AIR_STALL_TRIP_TICKS);
    if (alive) {
        s_stall_latched = false;
        ok = true;
    }
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

    return ok;
}
