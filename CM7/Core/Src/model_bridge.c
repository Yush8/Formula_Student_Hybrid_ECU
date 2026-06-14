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
	CAN_FEED(bus1, CAN1_TEST, test);

	/* Examples (add the matching .def line + Simulink inports, then uncomment):
	 * CAN_FEED(bus1, CAN1_DASH_STATUS,  dash_status);
	 * CAN_FEED(bus2, CAN2_INV_L_STATUS, inv_l_status);
	 */

	HCU_V2_Simulink_U.bus1_ok = can.bus1_ok;   /* whole-network health */
	HCU_V2_Simulink_U.bus2_ok = can.bus2_ok;


//    HCU_V2_Simulink_U.User_Button_1 = (HAL_GPIO_ReadPin(User_Button_1_GPIO_Port, User_Button_1_Pin) == GPIO_PIN_SET);
//
//    HCU_V2_Simulink_U.User_Button_2 = (HAL_GPIO_ReadPin(User_Button_2_GPIO_Port, User_Button_2_Pin) == GPIO_PIN_SET);
//
//    HCU_V2_Simulink_U.Parameter_1 = g_params.Parameter_1;

    /* ---- 2. run one model step ------------------------------------------ */
    HCU_V2_Simulink_step();

    /* ---- 3. drive outputs from the model's outbox -----------------------
     * The model output is logical: TRUE = "LED on". The LED is wired
     * active-low, so ON means driving the pin LOW. That electrical fact is
     * handled here, keeping the model in clean on/off terms.
     */
    HAL_GPIO_WritePin(User_LED_1_GPIO_Port, User_LED_1_Pin, HCU_V2_Simulink_Y.User_LED_1 ? GPIO_PIN_RESET : GPIO_PIN_SET);

    HAL_GPIO_WritePin(User_LED_2_GPIO_Port, User_LED_2_Pin, HCU_V2_Simulink_Y.User_LED_2 ? GPIO_PIN_RESET : GPIO_PIN_SET);

    HAL_GPIO_WritePin(User_LED_3_GPIO_Port, User_LED_3_Pin, HCU_V2_Simulink_Y.User_LED_3 ? GPIO_PIN_RESET : GPIO_PIN_SET);

//    Can_Send(2, 0x0B0, HCU_V2_Simulink_Y.hcu_status, 8);   /* track the false return if you want drop counts */

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
