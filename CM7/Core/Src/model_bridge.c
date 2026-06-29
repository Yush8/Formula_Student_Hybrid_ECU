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
//	CAN_FEED(bus1, CAN1_TEST, test);
	CAN_FEED(bus1, CAN1_ECU_APPS, APPS);
	CAN_FEED(bus2, CAN2_BMS_Limits, BMS_Limits);
	CAN_FEED(bus2, CAN2_ODrive_0_VI, ODrive_0_VI);
	CAN_FEED(bus2, CAN2_ODrive_1_VI, ODrive_1_VI);
	CAN_FEED(bus2, CAN2_ODrive_0_Heartbeat, ODrive_0_Heartbeat);
	CAN_FEED(bus2, CAN2_ODrive_1_Heartbeat, ODrive_1_Heartbeat);

	/* Examples (add the matching .def line + Simulink inports, then uncomment):
	 * CAN_FEED(bus1, CAN1_DASH_STATUS,  dash_status);
	 * CAN_FEED(bus2, CAN2_INV_L_STATUS, inv_l_status);
	 */

	HCU_V2_Simulink_U.bus1_ok = can.bus1_ok;   /* whole-network health */
	HCU_V2_Simulink_U.bus2_ok = can.bus2_ok;

	HCU_V2_Simulink_U.SDC_Monitor = (HAL_GPIO_ReadPin(SDC_Monitor_GPIO_Port, SDC_Monitor_Pin) == GPIO_PIN_SET);
	HCU_V2_Simulink_U.Start_Button = (HAL_GPIO_ReadPin(User_Button_1_GPIO_Port, User_Button_1_Pin) == GPIO_PIN_SET);


//    HCU_V2_Simulink_U.User_Button_1 = (HAL_GPIO_ReadPin(User_Button_1_GPIO_Port, User_Button_1_Pin) == GPIO_PIN_SET);
//
//    HCU_V2_Simulink_U.User_Button_2 = (HAL_GPIO_ReadPin(User_Button_2_GPIO_Port, User_Button_2_Pin) == GPIO_PIN_SET);

	/* ----- feed tunable parameters to the model (one line each) -----------
	 * Add a parameter in params.def (it then appears on the console and GUI
	 * automatically). To also let the MODEL use it, give it a matching Simulink
	 * Inport and add one MODEL_PARAM line here. Uncomment / add as needed:
	 */
//    MODEL_PARAM(kp);
//    MODEL_PARAM(ki);
//    MODEL_PARAM(torque_limit);
//    MODEL_PARAM(Parameter_1);

    /* ---- 2. run one model step ------------------------------------------ */
    HCU_V2_Simulink_step();

    /* ---- 3. drive outputs from the model's outbox -----------------------
     * The model output is logical: TRUE = "LED on". The LED is wired
     * active-low, so ON means driving the pin LOW. That electrical fact is
     * handled here, keeping the model in clean on/off terms.
     */
    HAL_GPIO_WritePin(User_LED_1_GPIO_Port, User_LED_1_Pin, HCU_V2_Simulink_Y.User_LED_1 ? GPIO_PIN_RESET : GPIO_PIN_SET);

    HAL_GPIO_WritePin(User_LED_2_GPIO_Port, User_LED_2_Pin, HCU_V2_Simulink_Y.User_LED_2 ? GPIO_PIN_RESET : GPIO_PIN_SET);

    HAL_GPIO_WritePin(AIR_Sink_GPIO_Port, AIR_Sink_Pin, HCU_V2_Simulink_Y.AIR_Enable ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_GPIO_WritePin(Precharge_Sink_GPIO_Port, Precharge_Sink_Pin, HCU_V2_Simulink_Y.Pre_Charge_Enable ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* ---- 3a. continuous setpoints: transmit EVERY tick ------------------
     * ODrive Set_Input_Torque (cmd 0x0E): node 0 = 0x00E, node 1 = 0x02E.
     * These MUST repeat every 10 ms - ODrive's rx watchdog disarms the axis if
     * the stream stops, so spamming is correct here. The model is responsible
     * for commanding safe (zero) torque under any fault.
     */
    Can_Send(2, 0x00E, HCU_V2_Simulink_Y.Torque_Left, 8);
    Can_Send(2, 0x02E, HCU_V2_Simulink_Y.Torque_Right, 8);

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
    rec.tick = g_sched_ticks;
#define LOG_Y(ctype, port)  rec.port = (ctype)(HCU_V2_Simulink_Y.port);
#define LOG_U(ctype, port)  rec.port = (ctype)(HCU_V2_Simulink_U.port);
#include "../../../Shared/log_signals.def"
#undef LOG_Y
#undef LOG_U
    Log_Write(&rec);
}
