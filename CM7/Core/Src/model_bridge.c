/*
 * model_bridge.c
 *
 *  Created on: 11 Jun 2026
 *      Author: Yusha
 */


#include "model_bridge.h"
#include "HCU_V2_Simulink.h"   /* generated model: _initialize(), _step(), _U, _Y */
#include "main.h"              /* HAL + the User_Button_1_* / User_LED_1_* pin macros */
#include "params.h"
#include "can.h"
#include "scheduler.h"         /* g_sched_ticks - the model tick stamped into each log record */
#include "logger.h"            /* Log_Write() - drop one record into the CM4 SD-log ring */
#include "air_safety.h"        /* AirSafety_SetRequest() - independent AIR fail-safe (it owns the sink pins) */

/* ---- Gearbox-protection torque slew rate (HARDCODED safety limit) ----------
 * Protects the Neu 2530 / 5:1 planetary from shock-loading: a pedal stab or a
 * throttle<->brake (drive<->regen) reversal is otherwise a torque STEP that
 * slams the gear teeth across the backlash. We slew-limit the model's base
 * torque demand SYMMETRICALLY so every change - including the reversal through
 * zero - is ramped, cushioning the lash reload. (A regen car cannot use the
 * "limit the rise, let the fall be instant" trick: with regen the fall is a
 * real driver command and the reversal is the worst gear event, so BOTH
 * directions must be slewed.)
 *
 * WHY IT LIVES HERE, NOT IN params.def: a gear-protection limit is safety-
 * critical, so (per the params.def rule) it must not be runtime-tunable - no
 * console `set` or edited flash image may move it. It is a firmware const.
 *
 * WHY A RAMP TIME, NOT A RAW Nm/s: the physically safe, transferable quantity
 * is the 0->full-torque ramp TIME. We fix that and let the Nm/s rate follow
 * Motor_Torque_Max, so the ramp stays gentle whatever the cap is set to and can
 * never accidentally become aggressive. 0.20 s to full torque is a soft start
 * for the geartrain yet still responsive; a full drive<->regen reversal (about
 * twice the span) therefore takes ~0.40 s. At the current 4.4 Nm cap that is a
 * 22 Nm/s slew (0.22 Nm per 10 ms tick); x5 at the gearbox output.
 *
 * The rate is fed (in Model_Step) to a symmetric Rate Limiter Dynamic in the
 * Base_Torque_Calculator, spliced BEFORE the Inverter_Enable gate so a fault
 * still zeroes torque INSTANTLY (that gate and the power-limiter scale->0 are
 * downstream multiplies, not affected by the limiter's internal ramp state).
 */
#define TORQUE_FULL_RAMP_S   0.20f                        /* 0 -> full torque, seconds */
#define TORQUE_SLEW_PER_S    (1.0f / TORQUE_FULL_RAMP_S)  /* rate = this * Motor_Torque_Max [Nm/s] */

/* ARMED. The Simulink side exists and is verified against this build: root
 * inports Torque_Rate_Up / Torque_Rate_Down (single) + a symmetric Rate Limiter
 * Dynamic (<S44>) on the shaped torque, before the Inverter_Enable gate. At 1
 * the bridge feeds the slew rates every step.
 *   - If you ever revert the model to a version WITHOUT those inports, set this
 *     back to 0 or CM7 won't link.
 *   - Do NOT leave the inports in the model with this at 0: the rates would
 *     default to 0, the limiter would clamp every delta to 0 and FREEZE torque
 *     at 0 - fail-safe (no runaway) but the car makes no torque at all. */
#define TORQUE_SLEW_FEED_READY   1

/* ---- User status LEDs 3 & 4 (Simulink-driven) ------------------------------
 * User_LED_1/2 are already driven from model outports in the dispatch below.
 * User_LED_3 (PD10) and User_LED_4 (PD11) are two more free status LEDs whose
 * MEANING is yours to define in Simulink - lamp them off anything you like
 * (arm state, fault, TV active, ...). They are set-and-forget in C: the model
 * output is logical (TRUE = "LED on") and the active-low wiring is handled here.
 *
 * GUARDED exactly like the torque slew above: CM7 references
 * HCU_V2_Simulink_Y.User_LED_3 / User_LED_4, which DO NOT EXIST in the model
 * struct until you add the matching root Outports and regenerate - so with this
 * at 0 the two LEDs are simply held at their boot state (off) and the firmware
 * still links today. When you have:
 *     1. added two Boolean root Outports in Simulink named EXACTLY
 *            User_LED_3      (boolean)
 *            User_LED_4      (boolean)
 *     2. regenerated Embedded Coder to CM7\Model,
 * flip this to 1 and the model drives them. (SDC_Monitor_LED is handled
 * separately - it mirrors the SDC line directly in C and needs no outport.) */
#define USER_LED_FEED_READY   0

/* CAN_FEED: copy one demuxed CAN message into the model inbox in a single line.
 *
 *     CAN_FEED( bus1 , CAN1_TEST , test );
 *
 * fills HCU_V2_Simulink_U.test[0..7] with the 8 data bytes and
 *      HCU_V2_Simulink_U.test_age with the ms-since-last-seen.
 *
 * So for every message you add you need TWO Simulink inports, named:
 *     <port>        uint8, width 8     (the data bytes)
 *     <port>_age    uint32             (freshness; UINT32_MAX = never seen)
 *
 * It reads from a local snapshot that MUST be named `can` (see Model_Step).
 */
#define CAN_FEED(busfield, slot, port)                                        \
    do {                                                                      \
        for (int _b = 0; _b < 8; _b++)                                        \
            HCU_V2_Simulink_U.port[_b] = can.busfield[slot].data[_b];         \
        HCU_V2_Simulink_U.port##_age = can.busfield[slot].age_ms;             \
    } while (0)

/* MODEL_PARAM: feed one tunable parameter to the model in a single line.
 *
 *     MODEL_PARAM( kp );
 *
 * expands to   HCU_V2_Simulink_U.kp = g_params.kp;   so a parameter you added
 * in params.def reaches the Simulink model. The parameter name MUST match the
 * Simulink Inport name exactly (same convention as CAN_FEED's port name).
 *
 * You stay in control here: list one MODEL_PARAM line per parameter you
 * actually want the model to read. A firmware-only tunable (say a log rate that
 * never enters the model) simply gets no line - it still lives in params.def,
 * the console and flash, it just isn't fed to Simulink.
 */
#define MODEL_PARAM(name)  (HCU_V2_Simulink_U.name = g_params.name)

/* CAN_TX_GATED: transmit a model-built frame ONLY on the ticks the model asks.
 *
 *     CAN_TX_GATED( 2 , 0x007 , Set_Axis_State_0 );
 *
 * sends HCU_V2_Simulink_Y.Set_Axis_State_0[0..7] on bus 2 with id 0x007, but
 * ONLY while HCU_V2_Simulink_Y.Set_Axis_State_0_req is true. The model raises
 * that flag, holds it until it's satisfied (e.g. the ODrive heartbeat reports
 * the requested state) then drops it - so a one-shot command goes out a few
 * times and stops, instead of being blasted every tick. A frozen model that
 * stops raising the flag goes silent rather than spamming a stale frame.
 *
 * A gated command therefore needs TWO Simulink Outports:
 *     <port>        uint8, width 8     (the 8 data bytes to send)
 *     <port>_req    boolean            (1 = transmit this tick, 0 = stay silent)
 *
 * Use a plain Can_Send (see dispatch) for CONTINUOUS setpoints (torque / vel)
 * instead - ODrive's rx watchdog actually wants those repeated every tick.
 */
#define CAN_TX_GATED(bus, id, port)                                           \
    do {                                                                      \
        if (HCU_V2_Simulink_Y.port##_req)                                     \
            Can_Send((bus), (id), HCU_V2_Simulink_Y.port, 8);                 \
    } while (0)

void Model_Init(void)
{
    /* one-time model setup: clears states, loads parameter defaults */
    HCU_V2_Simulink_initialize();
}

void Model_Step(void)
{
    /* ---- 1. gather inputs into the model's inbox -------------------------
     * Pass the raw electrical level of the button: TRUE = pin is HIGH.
     * Whatever "pressed" / "if low then on" logic you want lives in the
     * Simulink model, not here.
     */

	can_snapshot_t can;
	Can_Snapshot(&can);

	/* ----- map demuxed CAN messages to model inports (one line each) -----
	 * To add a message: add it to canN_messages.def, then add a CAN_FEED line
	 * here, then add the two inports in Simulink. Nothing else to touch.
	 */
	CAN_FEED(bus1, CAN1_ECU_APPS, APPS);
	CAN_FEED(bus1, CAN1_ECU_Hybrid, ECU_Misc);
	CAN_FEED(bus2, CAN2_BMS_Limits, BMS_Limits);
	CAN_FEED(bus2, CAN2_ODrive_0_VI, ODrive_0_VI);
	CAN_FEED(bus2, CAN2_ODrive_1_VI, ODrive_1_VI);
	CAN_FEED(bus2, CAN2_ODrive_0_Heartbeat, ODrive_0_Heartbeat);
	CAN_FEED(bus2, CAN2_ODrive_1_Heartbeat, ODrive_1_Heartbeat);
	CAN_FEED(bus2, CAN2_ODrive_0_Encoder_Estimate, ODrive_0_Encoder_Estimate);
	CAN_FEED(bus2, CAN2_ODrive_1_Encoder_Estimate, ODrive_1_Encoder_Estimate);

	HCU_V2_Simulink_U.bus1_ok = can.bus1_ok;   /* whole-network health */
	HCU_V2_Simulink_U.bus2_ok = can.bus2_ok;

	HCU_V2_Simulink_U.SDC_Monitor = (HAL_GPIO_ReadPin(SDC_Monitor_GPIO_Port, SDC_Monitor_Pin) == GPIO_PIN_SET);
	HCU_V2_Simulink_U.Start_Button = (HAL_GPIO_ReadPin(User_Button_1_GPIO_Port, User_Button_1_Pin) == GPIO_PIN_SET);

	/* ----- feed tunable parameters to the model (one line each) -----------
	 * Add a parameter in params.def (it then appears on the console and GUI
	 * automatically). To also let the MODEL use it, give it a matching Simulink
	 * Inport and add one MODEL_PARAM line here. Uncomment / add as needed:
	 */

    /* BMS power-limiter tunables -> model. Names match params.def + the
     * Simulink Inports. Without these the inports default to 0, which forces
     * the power budget (and the torque-scale factor) to 0 = no torque. */
    MODEL_PARAM(Drive_Efficiency);
    MODEL_PARAM(BMS_Margin);
    MODEL_PARAM(Motor_Torque_Max);
    MODEL_PARAM(Left_Direction);
    MODEL_PARAM(Right_Direction);

    /* Gearbox-protection torque slew -> model (see the note at the top of this
     * file). HARDCODED safe ramp, auto-scaled by the live Motor_Torque_Max cap;
     * up = +rate, lo = -rate feed a symmetric Rate Limiter Dynamic. Deliberately
     * NOT a MODEL_PARAM: the rate is a firmware const, never a runtime tunable. */
#if TORQUE_SLEW_FEED_READY
    {
        const float torque_slew = g_params.Motor_Torque_Max * TORQUE_SLEW_PER_S;
        HCU_V2_Simulink_U.Torque_Rate_Up   =  torque_slew;
        HCU_V2_Simulink_U.Torque_Rate_Down = -torque_slew;
    }
#endif

    /* Regen / ACL (charge) LIMITER tunables -> model (the safety backstops only;
     * the brake->regen curve is a visible Simulink lookup table, not a param).
     * Same convention: each name matches a params.def line + a Simulink Inport. */
    MODEL_PARAM(Regen_Efficiency);
    MODEL_PARAM(Motor_Regen_Max);
    MODEL_PARAM(Regen_Cutoff_Speed);

    /* Torque-vectoring tunables -> model (front-axle steering split; Option A).
     * Same convention: each name matches a params.def line + a Simulink Inport.
     * TV_Gain default 0 = vectoring inert until deliberately tuned up. */
    MODEL_PARAM(TV_Gain);
    MODEL_PARAM(Steering_Centre);
    MODEL_PARAM(Steering_Deadzone);
    MODEL_PARAM(Max_Torque_Split);

    /* Bench VELOCITY-test scaling -> model. Turns the final torque request into a
     * velocity setpoint for wheels-off spin tests (ODrive switched to
     * VELOCITY_CONTROL over USB). Default 0 = feature inert. See params.def. */
    MODEL_PARAM(Vel_Scale);

    /* ---- 2. run one model step ------------------------------------------ */
    HCU_V2_Simulink_step();

    /* ---- 3. drive outputs from the model's outbox -----------------------
     * The model output is logical: TRUE = "LED on". The LED is wired
     * active-low, so ON means driving the pin LOW. That electrical fact is
     * handled here, keeping the model in clean on/off terms.
     */
    HAL_GPIO_WritePin(User_LED_1_GPIO_Port, User_LED_1_Pin, HCU_V2_Simulink_Y.User_LED_1 ? GPIO_PIN_RESET : GPIO_PIN_SET);

    HAL_GPIO_WritePin(User_LED_2_GPIO_Port, User_LED_2_Pin, HCU_V2_Simulink_Y.User_LED_2 ? GPIO_PIN_RESET : GPIO_PIN_SET);

    /* User_LED_3 / User_LED_4: two more Simulink-driven status LEDs, same
     * active-low convention as LED 1/2. Compiled out until the model has the
     * matching outports - see USER_LED_FEED_READY at the top of this file. */
#if USER_LED_FEED_READY
    HAL_GPIO_WritePin(User_LED_3_GPIO_Port, User_LED_3_Pin, HCU_V2_Simulink_Y.User_LED_3 ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(User_LED_4_GPIO_Port, User_LED_4_Pin, HCU_V2_Simulink_Y.User_LED_4 ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif

    /* SDC status LED (PE5): mirror the SDC_Monitor logic level the model was
     * just fed above. Driven straight from C - it is a pass-through of a hardware
     * line, so it needs NO Simulink outport (wiring the SDC_Monitor inport back
     * out to an outport would be a pointless round-trip) and it stays live even
     * if the model freezes, exactly like the AIR/Precharge LEDs mirror their
     * sinks. Active-low: lit whenever SDC_Monitor reads logic-HIGH; if your SDC
     * sense is inverted, swap the two GPIO_PIN_* below. */
    HAL_GPIO_WritePin(SDC_Monitor_LED_GPIO_Port, SDC_Monitor_LED_Pin,
                      HCU_V2_Simulink_U.SDC_Monitor ? GPIO_PIN_RESET : GPIO_PIN_SET);

    /* AIR / pre-charge: the model only REQUESTS; the independent fail-safe owns the
     * sink pins (see air_safety.c). This call also stamps the model's proof-of-life
     * heartbeat - if the model ever stops stepping, the supervisor latches the AIRs
     * open from the TIM6 ISR regardless of this (now stale) request. The C layer can
     * only veto a close, never force one - so it can never weld the TS live. */
    AirSafety_SetRequest(HCU_V2_Simulink_Y.AIR_Enable, HCU_V2_Simulink_Y.Pre_Charge_Enable);

    /* ---- 3a. continuous setpoints: transmit EVERY tick ------------------
     * ODrive Set_Input_Torque (cmd 0x0E): node 0 = 0x00E, node 1 = 0x02E.
     * These MUST repeat every 10 ms - ODrive's rx watchdog disarms the axis if
     * the stream stops, so spamming is correct here. The model is responsible
     * for commanding safe (zero) torque under any fault.
     */
    Can_Send(2, 0x00E, HCU_V2_Simulink_Y.Torque_Left, 8);
    Can_Send(2, 0x02E, HCU_V2_Simulink_Y.Torque_Right, 8);

    /* ---- 3a-vel. bench velocity setpoints: transmit EVERY tick --------------
     * ODrive Set_Input_Vel (cmd 0x0D): node 0 = 0x00D, node 1 = 0x02D. Only has
     * effect while that ODrive is switched to VELOCITY_CONTROL over USB (a
     * wheels-off jack test); in the normal TORQUE_CONTROL race config the ODrive
     * ignores input_vel, so these are harmless to stream in every build. Payload
     * = float32 velocity [turns/s] in bytes 0..3 (built in Simulink as the final
     * torque request x Vel_Scale, so torque-vectoring shows up as a wheel-speed
     * split), bytes 4..7 = torque feed-forward = 0. Same continuous pattern as
     * the torque stream above - ODrive's rx watchdog wants the repeat.
     */
    Can_Send(2, 0x00D, HCU_V2_Simulink_Y.Velocity_Left, 8);
    Can_Send(2, 0x02D, HCU_V2_Simulink_Y.Velocity_Right, 8);

    /* ---- 3b. one-shot / confirmed commands: transmit only when asked -----
     * ODrive Set_Axis_State (cmd 0x07): node 0 = 0x007, node 1 = 0x027.
     * Payload = requested axis state as uint32 LE in bytes 0..3 (8 =
     * CLOSED_LOOP_CONTROL to arm, 1 = IDLE to disarm, 3 = calibration).
     * Gated: the model resends until the heartbeat confirms, then drops _req.
     */
    CAN_TX_GATED(2, 0x007, Set_Axis_State_0);
    CAN_TX_GATED(2, 0x027, Set_Axis_State_1);

    /* ---- 4. log selected signals to the SD card -------------------------
     * You do NOT edit this block. WHAT gets logged is the single list in
     * Shared/log_signals.def - this just packs whatever that file names and
     * drops it in the lock-free ring CM4 drains. RAM-only, never blocks; if CM4
     * is behind, Log_Write quietly drops the newest record. `tick` is the time
     * axis; the _Y/_U fields were already populated in the gather phase above.
     */
    log_record_t rec = {0};
    rec.tick     = g_sched_ticks;
    rec.overruns = g_sched_overruns;   /* missed 10 ms deadlines so far (loop health) */
#define LOG_Y(ctype, port)  rec.port = (ctype)(HCU_V2_Simulink_Y.port);
#define LOG_U(ctype, port)  rec.port = (ctype)(HCU_V2_Simulink_U.port);
#include "../../../Shared/log_signals.def"
#undef LOG_Y
#undef LOG_U
    Log_Write(&rec);
}
