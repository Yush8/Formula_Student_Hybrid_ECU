/*
 * model_bridge.h
 *
 *  Created on: 11 Jun 2026
 *      Author: Yusha
 */

#ifndef MODEL_BRIDGE_H
#define MODEL_BRIDGE_H

/*
 * Thin bridge between the hardware (CubeMX/HAL peripherals) and the
 * Simulink-generated control model (HCU_V2_Simulink).
 *
 *   Model_Init()  - call once at startup, after the peripherals are up.
 *   Model_Step()  - call at the model's base sample rate. It does:
 *                       1. gather raw inputs  -> model inbox  (HCU_V2_Simulink_U)
 *                       2. step the model     -> HCU_V2_Simulink_step()
 *                       3. drive outputs      <- model outbox (HCU_V2_Simulink_Y)
 *
 * The model only ever sees logical / raw signals. All hardware reality
 * (which pin, active-low polarity) is handled here in C, never in Simulink.
 * To add a signal later: add a port in the model, add one line below.
 */

void Model_Init(void);
void Model_Step(void);

#endif /* MODEL_BRIDGE_H */
