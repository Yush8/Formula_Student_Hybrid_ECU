/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: HCU_V2_Simulink.c
 *
 * Code generated for Simulink model 'HCU_V2_Simulink'.
 *
 * Model version                  : 1.14
 * Simulink Coder version         : 25.2 (R2025b) 28-Jul-2025
 * C/C++ source code generated on : Thu Jun 11 23:14:15 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM Cortex-M
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "HCU_V2_Simulink.h"

/* External inputs (root inport signals with default storage) */
ExtU_HCU_V2_Simulink_T HCU_V2_Simulink_U;

/* External outputs (root outports fed by signals with default storage) */
ExtY_HCU_V2_Simulink_T HCU_V2_Simulink_Y;

/* Real-time model */
static RT_MODEL_HCU_V2_Simulink_T HCU_V2_Simulink_M_;
RT_MODEL_HCU_V2_Simulink_T *const HCU_V2_Simulink_M = &HCU_V2_Simulink_M_;

/* Model step function */
void HCU_V2_Simulink_step(void)
{
  /* Switch: '<Root>/Switch' incorporates:
   *  Constant: '<S1>/Constant'
   *  Inport: '<Root>/test_age'
   *  RelationalOperator: '<S1>/Compare'
   */
  if (HCU_V2_Simulink_U.test_age < 50U) {
    /* Outport: '<Root>/User_LED_3' incorporates:
     *  DataTypeConversion: '<Root>/Data Type Conversion'
     *  Inport: '<Root>/test'
     */
    HCU_V2_Simulink_Y.User_LED_3 = (HCU_V2_Simulink_U.test[0] != 0);
  } else {
    /* Outport: '<Root>/User_LED_3' incorporates:
     *  Constant: '<Root>/Constant'
     */
    HCU_V2_Simulink_Y.User_LED_3 = false;
  }

  /* End of Switch: '<Root>/Switch' */

  /* Outport: '<Root>/User_LED_1' incorporates:
   *  Inport: '<Root>/bus1_ok'
   */
  HCU_V2_Simulink_Y.User_LED_1 = HCU_V2_Simulink_U.bus1_ok;

  /* Outport: '<Root>/User_LED_2' incorporates:
   *  Inport: '<Root>/bus2_ok'
   */
  HCU_V2_Simulink_Y.User_LED_2 = HCU_V2_Simulink_U.bus2_ok;
}

/* Model initialize function */
void HCU_V2_Simulink_initialize(void)
{
  /* (no initialization code required) */
}

/* Model terminate function */
void HCU_V2_Simulink_terminate(void)
{
  /* (no terminate code required) */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
