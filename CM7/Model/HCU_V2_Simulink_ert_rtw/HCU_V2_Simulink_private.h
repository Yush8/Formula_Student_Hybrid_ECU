/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: HCU_V2_Simulink_private.h
 *
 * Code generated for Simulink model 'HCU_V2_Simulink'.
 *
 * Model version                  : 1.51
 * Simulink Coder version         : 25.2 (R2025b) 28-Jul-2025
 * C/C++ source code generated on : Tue Jun 30 00:22:55 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM Cortex-M
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#ifndef HCU_V2_Simulink_private_h_
#define HCU_V2_Simulink_private_h_
#include "rtwtypes.h"
#include "HCU_V2_Simulink_types.h"
#include "HCU_V2_Simulink.h"

extern uint16_T HCU_V2_Simulink_BitShift(uint16_T rtu_u);
extern void HCU_V2_Simulink_MATLABFunction(const uint8_T rtu_CAN_Data[8],
  real32_T *rty_Bus_Voltage, real32_T *rty_Bus_Current);

#endif                                 /* HCU_V2_Simulink_private_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
