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

/* ===========================================================================
 *  FEATURE GUARDS  (*_FEED_READY)
 * ---------------------------------------------------------------------------
 *  Embedded Coder PRUNES an unconnected root inport, so C that references a
 *  model port before the model has it fails the CM7 build at LINK. Each flag
 *  below is 0 while the model lacks its ports and 1 once they exist and are
 *  wired. Set one back to 0 if you ever revert the model past that feature.
 *
 *  Flag                     Requires these Simulink root ports
 *  -----------------------  ---------------------------------------------------
 *  TORQUE_SLEW_FEED_READY   in : Torque_Rate_Up, Torque_Rate_Down    (single)
 *  START_GUI_FEED_READY     in : Start_Button_GUI                   (boolean)
 *  SPEED_GATE_FEED_READY    in : Bench_Speed_Bypass                 (boolean)
 *  RESET_FEED_READY         in : Reset_Req                          (boolean)
 *  VEL_MODE_FEED_READY      in : Bench_Velocity_Mode                (boolean)
 *                           out: Set_Controller_Mode_0/_1  (uint8, width 8),
 *                                Set_Controller_Mode_0_req/_1_req   (boolean),
 *                                Velocity_Mode_Active               (boolean)
 *  USER_LED_FEED_READY      out: User_LED_3, User_LED_4             (boolean)
 *
 *  TRAP: leaving a flag at 0 while the ports DO exist is not harmless. The
 *  inports then read 0 - and for the torque slew a rate of 0 clamps every delta
 *  to zero, FREEZING torque at 0. Fail-safe, but the car makes no torque and
 *  nothing tells you why.
 *
 *  What each feature actually does, and why, is in docs/CONTROL_STRATEGY.md:
 *    slew  = section 7    start/reset = section 9    speed gate = section 6
 *    velocity mode = section 8
 * =========================================================================== */
#define TORQUE_SLEW_FEED_READY   1
#define START_GUI_FEED_READY     1
#define SPEED_GATE_FEED_READY    1
#define RESET_FEED_READY         1
#define VEL_MODE_FEED_READY      1
#define USER_LED_FEED_READY      0   /* User_LED_3/4 (PD10/PD11): outports not
                                      * added to the model yet, so the two LEDs
                                      * stay at their boot state (off). Their
                                      * MEANING is yours to define in Simulink. */

/* ---- Gearbox-protection torque slew rate (HARDCODED safety limit) ----------
 * Protects the Neu 2530 / 5:1 planetary from shock-loading. The slew is
 * SYMMETRIC (both rise and fall) because with regen the fall is a real driver
 * command and the drive<->regen reversal through zero is the worst gear event.
 *
 * A ramp TIME is the fixed invariant, not a raw Nm/s: the rate is derived from
 * the live Motor_Torque_Max cap, so it stays gentle whatever the cap is set to
 * and can never accidentally become aggressive. 0.20 s to full torque = 22 Nm/s
 * at the current 4.4 Nm cap (x5 at the gearbox output); a full reversal spans
 * about twice that, so ~0.40 s.
 *
 * It lives HERE, not in params.def, because a gear-protection limit is
 * safety-critical: no console `set` and no edited flash image may move it.
 * Fed to a Rate Limiter Dynamic placed BEFORE the Inverter_Enable gate, so a
 * fault still zeroes torque INSTANTLY regardless of the limiter's ramp state.
 * Full rationale: docs/CONTROL_STRATEGY.md section 7.
 */
#define TORQUE_FULL_RAMP_S   0.20f                        /* 0 -> full torque, seconds */
#define TORQUE_SLEW_PER_S    (1.0f / TORQUE_FULL_RAMP_S)  /* rate = this * Motor_Torque_Max [Nm/s] */

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
	/* ACTIVE-LOW: R51 pulls PD12 up to +3V3; the button shorts it to GND when
	 * pressed. So GPIO_PIN_RESET (LOW) = pressed, GPIO_PIN_SET (HIGH) = idle. */
	HCU_V2_Simulink_U.Start_Button = (HAL_GPIO_ReadPin(User_Button_1_GPIO_Port, User_Button_1_Pin) == GPIO_PIN_RESET);

	/* GUI / console START request: the SAME "start", but from the laptop. It
	 * arrives as a tunable (the GUI START button - and `set Start_Button_GUI 1` -
	 * pulses it), so it is fed to its OWN boolean inport here; int32 0/1 narrows to
	 * boolean implicitly, exactly like Bench_Engine_Off below. In Simulink you OR
	 * this inport with Start_Button above, so the two start sources share the
	 * identical ready-to-drive sequence and every interlock. It can REQUEST start;
	 * it can never bypass a safety gate. (Kept here, next to the physical button,
	 * rather than in the MODEL_PARAM group so the two start inputs sit together.)
	 * Compiled out until the model has the matching inport - see
	 * START_GUI_FEED_READY at the top of this file. */
#if START_GUI_FEED_READY
	MODEL_PARAM(Start_Button_GUI);
#endif

	/* Fault-acknowledge / Error-state exit -> the model's single `Reset_Req` inport.
	 * Two sources OR'd here: the physical reset button (User Button 2, PD13) and the
	 * GUI Clear-Fault button (Error_Reset_GUI, a momentary-pulsed param). Kept next to
	 * the start inputs since it is the same "driver request" shape. It can only REQUEST
	 * the exit; the chart still requires the fault to be gone (Reset_Req && !BMS_Fault)
	 * before it leaves Error, and it lands in HV_OFF. ACTIVE-LOW button, same wiring as
	 * the start button (R-pull-up to +3V3, pressed shorts to GND); if User Button 2 is
	 * NOT active-low, swap GPIO_PIN_RESET for GPIO_PIN_SET. Compiled out until the model
	 * has the inport - see RESET_FEED_READY at the top of this file. */
#if RESET_FEED_READY
	HCU_V2_Simulink_U.Reset_Req =
	    (HAL_GPIO_ReadPin(User_Button_2_GPIO_Port, User_Button_2_Pin) == GPIO_PIN_RESET)
	    || (g_params.Error_Reset_GUI != 0);
#endif

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

    /* Brake-pressure zero offset -> model. Subtracted from Brake_Pressure before
     * the Regen_Shape lookup (then clamped >=0) so the sensor's resting reading
     * decodes as 0 regen - otherwise a ~3-count rest offset lands in the regen
     * branch and silently overrides the accelerator. The analog of Steering_Centre
     * for the brake. Only touches the regen curve; the FSAE brake-plausibility and
     * brake-flag checks still read raw Brake_Pressure. See params.def. */
    MODEL_PARAM(Brake_Zero_Offset);

    /* Torque-vectoring tunables -> model (front-axle steering split; Option A).
     * Same convention: each name matches a params.def line + a Simulink Inport.
     * TV_Gain default 0 = vectoring inert until deliberately tuned up. */
    MODEL_PARAM(TV_Gain);
    MODEL_PARAM(Steering_Centre);
    MODEL_PARAM(Steering_Deadzone);
    MODEL_PARAM(Max_Torque_Split);

    /* Bench VELOCITY-test scaling -> model. Turns the final torque request into a
     * velocity setpoint for wheels-off spin tests (ODrive switched to
     * VELOCITY_CONTROL - see Bench_Velocity_Mode). Default 0 = feature inert.
     * See params.def. */
    MODEL_PARAM(Vel_Scale);

    /* Bench VELOCITY control-mode select -> model. Boolean; 0 = torque control
     * (race), 1 = velocity control (wheels-off bench). The model turns this into
     * the ODrive Set_Controller_Mode frame streamed over CAN (see dispatch) AND
     * into Velocity_Mode_Active, which picks which setpoint frame C streams.
     * Guarded by VEL_MODE_FEED_READY (top of file). */
#if VEL_MODE_FEED_READY
    MODEL_PARAM(Bench_Velocity_Mode);
#endif

    /* Bench engine-off drive-enable BYPASS -> model. Boolean; ORs the engine-sync
     * gate open so the Safety_Supervisor can reach DRIVE with the engine off for a
     * bench test. Bypasses ONLY that gate; every other interlock stays live.
     * Default 0 = normal (real sync required). See params.def for the warning. */
    MODEL_PARAM(Bench_Engine_Off);

    /* Bench SPEED-gate BYPASS -> model. Boolean; ORs the Option-1 launch speed
     * gate open so a jacked, wheels-off car can spin the motors from 0 km/h. The
     * road-speed signal itself needs NO feed here - it already arrives in the APPS
     * frame (APPS[4..5]) and is decoded in Simulink. Compiled out until the model
     * has the matching inport - see SPEED_GATE_FEED_READY at the top of this file. */
#if SPEED_GATE_FEED_READY
    MODEL_PARAM(Bench_Speed_Bypass);
#endif

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

    /* ---- 3a. continuous setpoint: transmit the ACTIVE mode's frame EVERY tick
     * ODrive's rx watchdog disarms the axis if the stream stops, so the active
     * frame MUST repeat every 10 ms. The model commands safe (zero) torque under
     * any fault.
     *
     * CRITICAL - send ONE frame, never both. Set_Input_Torque (0x0E) and
     * Set_Input_Vel (0x0D) BOTH write the ODrive's controller.input_torque:
     * 0x0D's bytes 4..7 are a torque feed-forward that lands in that same
     * register. Streaming both every tick makes them fight over it - in
     * TORQUE_CONTROL the 0x0D feed-forward (0) stamps the real torque request back
     * to 0 the moment after 0x0E set it. So we stream only the frame that matches
     * the mode the model has actually commanded the ODrives into.
     *   Velocity_Mode_Active mirrors the Set_Controller_Mode asserted in 3a-mode:
     *     0 -> Set_Input_Torque  0x00E / 0x02E, payload = Torque_Left/Right
     *     1 -> Set_Input_Vel     0x00D / 0x02D, payload = Velocity_Left/Right
     *          (= final torque request x Vel_Scale, built in Simulink; bytes 4..7
     *           = torque feed-forward = 0).
     */
#if VEL_MODE_FEED_READY
    if (HCU_V2_Simulink_Y.Velocity_Mode_Active) {
        Can_Send(2, 0x00D, HCU_V2_Simulink_Y.Velocity_Left,  8);
        Can_Send(2, 0x02D, HCU_V2_Simulink_Y.Velocity_Right, 8);
    } else {
        Can_Send(2, 0x00E, HCU_V2_Simulink_Y.Torque_Left,  8);
        Can_Send(2, 0x02E, HCU_V2_Simulink_Y.Torque_Right, 8);
    }
#else
    /* Velocity mode not wired in: stream TORQUE only (the race-safe default).
     * Never send the velocity frame here - its torque-FF field would zero
     * input_torque and cripple torque control. */
    Can_Send(2, 0x00E, HCU_V2_Simulink_Y.Torque_Left,  8);
    Can_Send(2, 0x02E, HCU_V2_Simulink_Y.Torque_Right, 8);
#endif

    /* ---- 3a-mode. controller-mode select: torque (race) vs velocity (bench) --
     * ODrive Set_Controller_Mode (cmd 0x0B): node 0 = 0x00B, node 1 = 0x02B.
     * Payload = Control_Mode (uint32 LE, bytes 0..3) + Input_Mode (bytes 4..7):
     *   torque   -> 1 (TORQUE_CONTROL)   + 1 (PASSTHROUGH)
     *   velocity -> 2 (VELOCITY_CONTROL) + 2 (VEL_RAMP)
     * The model builds the payload from Bench_Velocity_Mode and raises _req ONLY
     * while that ODrive is IDLE (the pre-arm window) - so the mode is applied
     * before the arm below and never rewritten on a live axis. Sent BEFORE the
     * arm so, on the tick an ODrive is about to be armed, the mode leads it. A
     * dark ODrive (HV not up) just drops the frame; the BMS keeps bus 2 ACKed so
     * this never bus-offs. */
#if VEL_MODE_FEED_READY
    CAN_TX_GATED(2, 0x00B, Set_Controller_Mode_0);
    CAN_TX_GATED(2, 0x02B, Set_Controller_Mode_1);
#endif

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
