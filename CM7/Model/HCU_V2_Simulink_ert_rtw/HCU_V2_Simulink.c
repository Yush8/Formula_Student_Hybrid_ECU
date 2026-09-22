/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: HCU_V2_Simulink.c
 *
 * Code generated for Simulink model 'HCU_V2_Simulink'.
 *
 * Model version                  : 1.157
 * Simulink Coder version         : 25.2 (R2025b) 28-Jul-2025
 * C/C++ source code generated on : Tue Sep 22 15:01:02 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM Cortex-M
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "HCU_V2_Simulink.h"
#include "rtwtypes.h"
#include "HCU_V2_Simulink_private.h"
#include <string.h>
#include <stddef.h>
#include <math.h>

/* Named constants for Chart: '<S5>/Safety_Supervisor' */
#define HCU_V2_Simulink_IN_DRIVE       ((uint8_T)1U)
#define HCU_V2_Simulink_IN_Error_State ((uint8_T)2U)
#define HCU_V2_Simulink_IN_HV_OFF      ((uint8_T)3U)
#define HCU_V2_Simulink_IN_PRE_CHARGE  ((uint8_T)4U)
#define HCU_V2_Simulink_IN_RELAY_SWAP  ((uint8_T)5U)
#define HCU_V2_Simulink_IN_STANDBY     ((uint8_T)6U)
#define HCU_V_IN_VALIDATING_ERROR_STATE ((uint8_T)8U)
#define HCU__IN_VALIDATING_ERROR_STATE1 ((uint8_T)9U)
#define HCU__IN_VALIDATING_ERROR_STATE2 ((uint8_T)10U)
#define HCU__IN_VALIDATING_ERROR_STATE3 ((uint8_T)11U)
#define HCU__IN_VALIDATING_ERROR_STATE4 ((uint8_T)12U)
#define IN_VALIDATING_APPS_IMPLAUSIBILI ((uint8_T)7U)
#define IN_VALIDATING_FAULT_FROM_STANDB ((uint8_T)13U)

/* Block signals (default storage) */
B_HCU_V2_Simulink_T HCU_V2_Simulink_B;

/* Block states (default storage) */
DW_HCU_V2_Simulink_T HCU_V2_Simulink_DW;

/* External inputs (root inport signals with default storage) */
ExtU_HCU_V2_Simulink_T HCU_V2_Simulink_U;

/* External outputs (root outports fed by signals with default storage) */
ExtY_HCU_V2_Simulink_T HCU_V2_Simulink_Y;

/* Real-time model */
static RT_MODEL_HCU_V2_Simulink_T HCU_V2_Simulink_M_;
RT_MODEL_HCU_V2_Simulink_T *const HCU_V2_Simulink_M = &HCU_V2_Simulink_M_;

/* Forward declaration for local functions */
static void HCU_V2_Simulink_DRIVE(void);
static void HCU_V2_Simulink_PRE_CHARGE(void);
static void HCU_V2_Simulink_RELAY_SWAP(void);
static void HCU_V2_Simulink_STANDBY(const boolean_T *OR, const boolean_T *OR_e);
real32_T look1_iflf_binlcpw(real32_T u0, const real32_T bp0[], const real32_T
  table[], uint32_T maxIndex)
{
  real32_T frac;
  real32_T yL_0d0;
  uint32_T iLeft;

  /* Column-major Lookup 1-D
     Search method: 'binary'
     Use previous index: 'off'
     Interpolation method: 'Linear point-slope'
     Extrapolation method: 'Clip'
     Use last breakpoint for index at or above upper limit: 'off'
     Remove protection against out-of-range input in generated code: 'off'
   */
  /* Prelookup - Index and Fraction
     Index Search method: 'binary'
     Extrapolation method: 'Clip'
     Use previous index: 'off'
     Use last breakpoint for index at or above upper limit: 'off'
     Remove protection against out-of-range input in generated code: 'off'
   */
  if (u0 <= bp0[0U]) {
    iLeft = 0U;
    frac = 0.0F;
  } else if (u0 < bp0[maxIndex]) {
    uint32_T bpIdx;
    uint32_T iRght;

    /* Binary Search */
    bpIdx = maxIndex >> 1U;
    iLeft = 0U;
    iRght = maxIndex;
    while (iRght - iLeft > 1U) {
      if (u0 < bp0[bpIdx]) {
        iRght = bpIdx;
      } else {
        iLeft = bpIdx;
      }

      bpIdx = (iRght + iLeft) >> 1U;
    }

    frac = (u0 - bp0[iLeft]) / (bp0[iLeft + 1U] - bp0[iLeft]);
  } else {
    iLeft = maxIndex - 1U;
    frac = 1.0F;
  }

  /* Column-major Interpolation 1-D
     Interpolation method: 'Linear point-slope'
     Use last breakpoint for index at or above upper limit: 'off'
     Overflow mode: 'portable wrapping'
   */
  yL_0d0 = table[iLeft];
  return (table[iLeft + 1U] - yL_0d0) * frac + yL_0d0;
}

/*
 * Output and update for atomic system:
 *    '<S1>/Bit Shift'
 *    '<S1>/Bit Shift1'
 *    '<S1>/Bit Shift2'
 *    '<S2>/Bit Shift'
 *    '<S2>/Bit Shift1'
 *    '<S2>/Bit Shift2'
 *    '<S3>/Bit Shift'
 *    '<S3>/Bit Shift1'
 *    '<S3>/Bit Shift2'
 */
uint16_T HCU_V2_Simulink_BitShift(uint16_T rtu_u)
{
  /* MATLAB Function: '<S11>/bit_shift' */
  return (uint16_T)(rtu_u << 8);
}

/*
 * Output and update for atomic system:
 *    '<S4>/MATLAB Function'
 *    '<S4>/MATLAB Function1'
 */
void HCU_V2_Simulink_MATLABFunction(const uint8_T rtu_CAN_Data[8], real32_T
  *rty_Vel_Estimate)
{
  uint32_T x;
  x = (uint32_T)rtu_CAN_Data[5] << 8 | rtu_CAN_Data[4] | ((uint32_T)
    rtu_CAN_Data[6] << 16 | (uint32_T)rtu_CAN_Data[7] << 24);
  memcpy((void *)rty_Vel_Estimate, (void *)&x, (size_t)1 * sizeof(real32_T));
}

/*
 * Output and update for atomic system:
 *    '<S8>/MATLAB Function'
 *    '<S8>/MATLAB Function1'
 */
void HCU_V2_Simulin_MATLABFunction_i(const uint8_T rtu_CAN_Data[8], real32_T
  *rty_Bus_Voltage, real32_T *rty_Bus_Current)
{
  uint32_T b_x;
  uint32_T x;
  x = (uint32_T)rtu_CAN_Data[1] << 8 | rtu_CAN_Data[0] | ((uint32_T)
    rtu_CAN_Data[2] << 16 | (uint32_T)rtu_CAN_Data[3] << 24);
  b_x = (uint32_T)rtu_CAN_Data[5] << 8 | rtu_CAN_Data[4] | ((uint32_T)
    rtu_CAN_Data[6] << 16 | (uint32_T)rtu_CAN_Data[7] << 24);
  memcpy((void *)rty_Bus_Voltage, (void *)&x, (size_t)1 * sizeof(real32_T));
  memcpy((void *)rty_Bus_Current, (void *)&b_x, (size_t)1 * sizeof(real32_T));
}

/* Function for Chart: '<S5>/Safety_Supervisor' */
static void HCU_V2_Simulink_DRIVE(void)
{
  /* Outport: '<Root>/AIR_Enable' */
  HCU_V2_Simulink_Y.AIR_Enable = true;

  /* Outport: '<Root>/Pre_Charge_Enable' */
  HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

  /* Outport: '<Root>/Inverter_Enable' */
  HCU_V2_Simulink_Y.Inverter_Enable = true;

  /* Outport: '<Root>/State_Enum' */
  HCU_V2_Simulink_Y.State_Enum = 5U;

  /* Outport: '<Root>/BMS_Fault' incorporates:
   *  Inport: '<Root>/SDC_Monitor'
   *  Outport: '<Root>/AIR_Enable'
   *  Outport: '<Root>/APPS_Implausibility'
   *  Outport: '<Root>/Fault_Code'
   *  Outport: '<Root>/Inverter_Enable'
   *  Outport: '<Root>/State_Enum'
   */
  if (HCU_V2_Simulink_Y.BMS_Fault) {
    HCU_V2_Simulink_DW.Fault_Cause = 20U;
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU__IN_VALIDATING_ERROR_STATE2;
  } else if (HCU_V2_Simulink_Y.APPS_Implausibility) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = IN_VALIDATING_APPS_IMPLAUSIBILI;
  } else if (!HCU_V2_Simulink_U.SDC_Monitor) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;
    HCU_V2_Simulink_Y.AIR_Enable = false;
    HCU_V2_Simulink_Y.Inverter_Enable = false;
    HCU_V2_Simulink_Y.State_Enum = 1U;
    HCU_V2_Simulink_DW.Fault_Cause = 0U;
    HCU_V2_Simulink_Y.Fault_Code = 0U;
  }

  /* End of Outport: '<Root>/BMS_Fault' */
}

/* Function for Chart: '<S5>/Safety_Supervisor' */
static void HCU_V2_Simulink_PRE_CHARGE(void)
{
  real32_T tmp;

  /* Outport: '<Root>/Pre_Charge_Enable' */
  HCU_V2_Simulink_Y.Pre_Charge_Enable = true;

  /* Outport: '<Root>/State_Enum' */
  HCU_V2_Simulink_Y.State_Enum = 3U;

  /* Outport: '<Root>/Pack_Voltage' */
  tmp = 0.9F * HCU_V2_Simulink_Y.Pack_Voltage;

  /* Outport: '<Root>/Bus_Voltage_0' incorporates:
   *  Inport: '<Root>/SDC_Monitor'
   *  Outport: '<Root>/AIR_Enable'
   *  Outport: '<Root>/BMS_Fault'
   *  Outport: '<Root>/Bus_Voltage_1'
   *  Outport: '<Root>/Fault_Code'
   *  Outport: '<Root>/Inverter_Enable'
   *  Outport: '<Root>/Pre_Charge_Enable'
   *  Outport: '<Root>/State_Enum'
   */
  if ((HCU_V2_Simulink_Y.Bus_Voltage_0 > tmp) &&
      (HCU_V2_Simulink_Y.Bus_Voltage_1 > tmp)) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_RELAY_SWAP;
    HCU_V2_Simulink_Y.AIR_Enable = true;
    HCU_V2_Simulink_Y.State_Enum = 4U;
  } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 300) {
    HCU_V2_Simulink_DW.Fault_Cause = 40U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_Error_State;
    HCU_V2_Simulink_Y.Fault_Code = 40U;
    HCU_V2_Simulink_Y.AIR_Enable = false;
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_Y.Inverter_Enable = false;
    HCU_V2_Simulink_Y.State_Enum = 6U;
  } else if (HCU_V2_Simulink_Y.BMS_Fault) {
    HCU_V2_Simulink_DW.Fault_Cause = 20U;
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V_IN_VALIDATING_ERROR_STATE;
  } else if (!HCU_V2_Simulink_U.SDC_Monitor) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;
    HCU_V2_Simulink_Y.AIR_Enable = false;
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_Y.Inverter_Enable = false;
    HCU_V2_Simulink_Y.State_Enum = 1U;
    HCU_V2_Simulink_DW.Fault_Cause = 0U;
    HCU_V2_Simulink_Y.Fault_Code = 0U;
  }

  /* End of Outport: '<Root>/Bus_Voltage_0' */
}

/* Function for Chart: '<S5>/Safety_Supervisor' */
static void HCU_V2_Simulink_RELAY_SWAP(void)
{
  /* Outport: '<Root>/Pre_Charge_Enable' */
  HCU_V2_Simulink_Y.Pre_Charge_Enable = true;

  /* Outport: '<Root>/AIR_Enable' */
  HCU_V2_Simulink_Y.AIR_Enable = true;

  /* Outport: '<Root>/State_Enum' */
  /* Inport: '<Root>/SDC_Monitor' incorporates:
   *  Outport: '<Root>/BMS_Fault'
   */
  HCU_V2_Simulink_Y.State_Enum = 4U;
  if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_DRIVE;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

    /* Outport: '<Root>/Inverter_Enable' */
    HCU_V2_Simulink_Y.Inverter_Enable = true;

    /* Outport: '<Root>/State_Enum' */
    HCU_V2_Simulink_Y.State_Enum = 5U;
  } else if (HCU_V2_Simulink_Y.BMS_Fault) {
    HCU_V2_Simulink_DW.Fault_Cause = 20U;
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU__IN_VALIDATING_ERROR_STATE1;
  } else if (!HCU_V2_Simulink_U.SDC_Monitor) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

    /* Outport: '<Root>/AIR_Enable' */
    HCU_V2_Simulink_Y.AIR_Enable = false;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

    /* Outport: '<Root>/Inverter_Enable' */
    HCU_V2_Simulink_Y.Inverter_Enable = false;

    /* Outport: '<Root>/State_Enum' */
    HCU_V2_Simulink_Y.State_Enum = 1U;
    HCU_V2_Simulink_DW.Fault_Cause = 0U;

    /* Outport: '<Root>/Fault_Code' */
    HCU_V2_Simulink_Y.Fault_Code = 0U;
  }

  /* End of Inport: '<Root>/SDC_Monitor' */
}

/* Function for Chart: '<S5>/Safety_Supervisor' */
static void HCU_V2_Simulink_STANDBY(const boolean_T *OR, const boolean_T *OR_e)
{
  /* Outport: '<Root>/State_Enum' */
  HCU_V2_Simulink_Y.State_Enum = 2U;

  /* Outport: '<Root>/Brake_Pressure' incorporates:
   *  Inport: '<Root>/SDC_Monitor'
   *  Outport: '<Root>/AIR_Enable'
   *  Outport: '<Root>/APPS_Implausibility'
   *  Outport: '<Root>/BMS_Fault'
   *  Outport: '<Root>/Fault_Code'
   *  Outport: '<Root>/Inverter_Enable'
   *  Outport: '<Root>/Pre_Charge_Enable'
   *  Outport: '<Root>/State_Enum'
   */
  if ((HCU_V2_Simulink_Y.Brake_Pressure > 10.0F) && (*OR_e) && (*OR)) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_PRE_CHARGE;
    HCU_V2_Simulink_Y.Pre_Charge_Enable = true;
    HCU_V2_Simulink_Y.State_Enum = 3U;
  } else if (HCU_V2_Simulink_Y.APPS_Implausibility) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = IN_VALIDATING_FAULT_FROM_STANDB;
  } else if (HCU_V2_Simulink_Y.BMS_Fault) {
    HCU_V2_Simulink_DW.Fault_Cause = 20U;
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU__IN_VALIDATING_ERROR_STATE3;
  } else if (!HCU_V2_Simulink_U.SDC_Monitor) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;
    HCU_V2_Simulink_Y.AIR_Enable = false;
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_Y.Inverter_Enable = false;
    HCU_V2_Simulink_Y.State_Enum = 1U;
    HCU_V2_Simulink_DW.Fault_Cause = 0U;
    HCU_V2_Simulink_Y.Fault_Code = 0U;
  }

  /* End of Outport: '<Root>/Brake_Pressure' */
}

/* Model step function */
void HCU_V2_Simulink_step(void)
{
  int32_T i;
  int32_T rtb_Switch1;
  int32_T rtb_Switch_k;
  int32_T tmp;
  real32_T cutoff;
  real32_T rtb_Regen_Shape;
  real32_T rtb_Subtract;
  uint16_T rtb_y;
  uint16_T rtb_y_hv;
  uint16_T rtb_y_n;
  boolean_T OR;
  boolean_T OR_e;
  boolean_T rtb_Compare_dm;
  boolean_T rtb_Compare_dt;
  boolean_T rtb_Compare_h;
  boolean_T rtb_Compare_ph;

  /* SignalConversion generated from: '<S6>/Vector Concatenate2' incorporates:
   *  Concatenate: '<S6>/Vector Concatenate2'
   */
  for (i = 0; i < 7; i++) {
    HCU_V2_Simulink_B.VectorConcatenate2_m[i + 1] = 0U;
  }

  /* End of SignalConversion generated from: '<S6>/Vector Concatenate2' */

  /* SignalConversion generated from: '<S9>/Vector Concatenate' incorporates:
   *  Concatenate: '<S9>/Vector Concatenate'
   *  Constant: '<S9>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate_i[4] = 0U;

  /* SignalConversion generated from: '<S9>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S9>/Vector Concatenate1'
   *  Constant: '<S9>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1_e[4] = 0U;

  /* SignalConversion generated from: '<S10>/Vector Concatenate' incorporates:
   *  Concatenate: '<S10>/Vector Concatenate'
   *  Constant: '<S10>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate[4] = 0U;

  /* SignalConversion generated from: '<S10>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S10>/Vector Concatenate1'
   *  Constant: '<S10>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1[4] = 0U;

  /* SignalConversion generated from: '<S9>/Vector Concatenate' incorporates:
   *  Concatenate: '<S9>/Vector Concatenate'
   *  Constant: '<S9>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate_i[5] = 0U;

  /* SignalConversion generated from: '<S9>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S9>/Vector Concatenate1'
   *  Constant: '<S9>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1_e[5] = 0U;

  /* SignalConversion generated from: '<S10>/Vector Concatenate' incorporates:
   *  Concatenate: '<S10>/Vector Concatenate'
   *  Constant: '<S10>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate[5] = 0U;

  /* SignalConversion generated from: '<S10>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S10>/Vector Concatenate1'
   *  Constant: '<S10>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1[5] = 0U;

  /* SignalConversion generated from: '<S9>/Vector Concatenate' incorporates:
   *  Concatenate: '<S9>/Vector Concatenate'
   *  Constant: '<S9>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate_i[6] = 0U;

  /* SignalConversion generated from: '<S9>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S9>/Vector Concatenate1'
   *  Constant: '<S9>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1_e[6] = 0U;

  /* SignalConversion generated from: '<S10>/Vector Concatenate' incorporates:
   *  Concatenate: '<S10>/Vector Concatenate'
   *  Constant: '<S10>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate[6] = 0U;

  /* SignalConversion generated from: '<S10>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S10>/Vector Concatenate1'
   *  Constant: '<S10>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1[6] = 0U;

  /* SignalConversion generated from: '<S9>/Vector Concatenate' incorporates:
   *  Concatenate: '<S9>/Vector Concatenate'
   *  Constant: '<S9>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate_i[7] = 0U;

  /* SignalConversion generated from: '<S9>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S9>/Vector Concatenate1'
   *  Constant: '<S9>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1_e[7] = 0U;

  /* SignalConversion generated from: '<S10>/Vector Concatenate' incorporates:
   *  Concatenate: '<S10>/Vector Concatenate'
   *  Constant: '<S10>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate[7] = 0U;

  /* SignalConversion generated from: '<S10>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S10>/Vector Concatenate1'
   *  Constant: '<S10>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1[7] = 0U;

  /* SignalConversion generated from: '<S7>/Vector Concatenate2' incorporates:
   *  Concatenate: '<S7>/Vector Concatenate2'
   *  Constant: '<S7>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate2[1] = 0U;

  /* SignalConversion generated from: '<S7>/Vector Concatenate2' incorporates:
   *  Concatenate: '<S7>/Vector Concatenate2'
   *  Constant: '<S7>/Constant4'
   */
  HCU_V2_Simulink_B.VectorConcatenate2[5] = 0U;

  /* SignalConversion generated from: '<S7>/Vector Concatenate2' incorporates:
   *  Concatenate: '<S7>/Vector Concatenate2'
   *  Constant: '<S7>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate2[2] = 0U;

  /* SignalConversion generated from: '<S7>/Vector Concatenate2' incorporates:
   *  Concatenate: '<S7>/Vector Concatenate2'
   *  Constant: '<S7>/Constant4'
   */
  HCU_V2_Simulink_B.VectorConcatenate2[6] = 0U;

  /* SignalConversion generated from: '<S7>/Vector Concatenate2' incorporates:
   *  Concatenate: '<S7>/Vector Concatenate2'
   *  Constant: '<S7>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate2[3] = 0U;

  /* SignalConversion generated from: '<S7>/Vector Concatenate2' incorporates:
   *  Concatenate: '<S7>/Vector Concatenate2'
   *  Constant: '<S7>/Constant4'
   */
  HCU_V2_Simulink_B.VectorConcatenate2[7] = 0U;

  /* RelationalOperator: '<S14>/Compare' incorporates:
   *  Constant: '<S14>/Constant'
   *  Inport: '<Root>/APPS_age'
   */
  rtb_Compare_dt = (HCU_V2_Simulink_U.APPS_age > 0U);

  /* Outputs for Atomic SubSystem: '<S2>/Bit Shift1' */
  /* DataTypeConversion: '<S2>/Data Type Conversion3' incorporates:
   *  Inport: '<Root>/ECU_Misc'
   */
  rtb_y_hv = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.ECU_Misc[2]);

  /* End of Outputs for SubSystem: '<S2>/Bit Shift1' */

  /* Outputs for Atomic SubSystem: '<S2>/Bit Shift2' */
  /* DataTypeConversion: '<S2>/Data Type Conversion8' incorporates:
   *  Inport: '<Root>/ECU_Misc'
   */
  rtb_y = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.ECU_Misc[4]);

  /* End of Outputs for SubSystem: '<S2>/Bit Shift2' */

  /* Outputs for Atomic SubSystem: '<S1>/Bit Shift' */
  /* DataTypeConversion: '<S1>/Data Type Conversion' incorporates:
   *  Inport: '<Root>/APPS'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.APPS[0]);

  /* End of Outputs for SubSystem: '<S1>/Bit Shift' */

  /* Switch: '<S1>/Switch' */
  if (rtb_Compare_dt) {
    /* Product: '<S38>/Product4' incorporates:
     *  DataTypeConversion: '<S1>/Data Type Conversion1'
     *  DataTypeConversion: '<S1>/Data Type Conversion2'
     *  DataTypeConversion: '<S1>/Data Type Conversion4'
     *  Gain: '<S1>/Gain'
     *  Inport: '<Root>/APPS'
     *  S-Function (sfix_bitop): '<S1>/Bitwise OR'
     */
    HCU_V2_Simulink_Y.Base_Torque_Demand = (real32_T)(int16_T)((uint32_T)rtb_y_n
      | HCU_V2_Simulink_U.APPS[1]) * 0.0122070312F;
  } else {
    /* Product: '<S38>/Product4' incorporates:
     *  Constant: '<S1>/Constant'
     */
    HCU_V2_Simulink_Y.Base_Torque_Demand = 0.0F;
  }

  /* End of Switch: '<S1>/Switch' */

  /* Outputs for Atomic SubSystem: '<S1>/Bit Shift1' */
  /* DataTypeConversion: '<S1>/Data Type Conversion3' incorporates:
   *  Inport: '<Root>/APPS'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.APPS[2]);

  /* End of Outputs for SubSystem: '<S1>/Bit Shift1' */

  /* Switch: '<S1>/Switch1' */
  if (rtb_Compare_dt) {
    /* Product: '<S47>/delta fall limit' incorporates:
     *  DataTypeConversion: '<S1>/Data Type Conversion5'
     *  DataTypeConversion: '<S1>/Data Type Conversion6'
     *  DataTypeConversion: '<S1>/Data Type Conversion7'
     *  Gain: '<S1>/Gain1'
     *  Inport: '<Root>/APPS'
     *  S-Function (sfix_bitop): '<S1>/Bitwise OR1'
     */
    HCU_V2_Simulink_Y.Steering_Angle = (real32_T)(int16_T)((uint32_T)rtb_y_n |
      HCU_V2_Simulink_U.APPS[3]) * 0.0122070312F;
  } else {
    /* Product: '<S47>/delta fall limit' incorporates:
     *  Constant: '<S1>/Constant'
     */
    HCU_V2_Simulink_Y.Steering_Angle = 0.0F;
  }

  /* End of Switch: '<S1>/Switch1' */

  /* Outport: '<Root>/APPS_Implausibility' incorporates:
   *  Abs: '<S36>/Abs'
   *  Constant: '<S43>/Constant'
   *  RelationalOperator: '<S43>/Compare'
   *  Sum: '<S36>/Subtract'
   */
  HCU_V2_Simulink_Y.APPS_Implausibility = (fabsf
    (HCU_V2_Simulink_Y.Base_Torque_Demand - HCU_V2_Simulink_Y.Steering_Angle) >
    10.0F);

  /* RelationalOperator: '<S21>/Compare' incorporates:
   *  Constant: '<S21>/Constant'
   *  Inport: '<Root>/ECU_Misc_age'
   */
  rtb_Compare_ph = (HCU_V2_Simulink_U.ECU_Misc_age < 50U);

  /* Switch: '<S2>/Switch2' */
  if (rtb_Compare_ph) {
    /* Outport: '<Root>/Brake_Pressure' incorporates:
     *  DataTypeConversion: '<S2>/Data Type Conversion10'
     *  DataTypeConversion: '<S2>/Data Type Conversion11'
     *  DataTypeConversion: '<S2>/Data Type Conversion9'
     *  Gain: '<S2>/Gain2'
     *  Inport: '<Root>/ECU_Misc'
     *  S-Function (sfix_bitop): '<S2>/Bitwise OR2'
     */
    HCU_V2_Simulink_Y.Brake_Pressure = (real32_T)(int16_T)((uint32_T)rtb_y |
      HCU_V2_Simulink_U.ECU_Misc[5]) * 0.01F;
  } else {
    /* Outport: '<Root>/Brake_Pressure' incorporates:
     *  Constant: '<S2>/Constant'
     */
    HCU_V2_Simulink_Y.Brake_Pressure = 0.0F;
  }

  /* End of Switch: '<S2>/Switch2' */

  /* Outputs for Atomic SubSystem: '<S1>/Bit Shift2' */
  /* DataTypeConversion: '<S1>/Data Type Conversion8' incorporates:
   *  Inport: '<Root>/APPS'
   */
  rtb_y = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.APPS[4]);

  /* End of Outputs for SubSystem: '<S1>/Bit Shift2' */

  /* Outputs for Atomic SubSystem: '<S3>/Bit Shift2' */
  /* DataTypeConversion: '<S3>/Data Type Conversion10' incorporates:
   *  Inport: '<Root>/BMS_Limits'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.BMS_Limits[5]);

  /* End of Outputs for SubSystem: '<S3>/Bit Shift2' */

  /* Outport: '<Root>/Pack_Voltage' incorporates:
   *  DataTypeConversion: '<S3>/Data Type Conversion8'
   *  DataTypeConversion: '<S3>/Data Type Conversion9'
   *  Gain: '<S3>/Gain2'
   *  Inport: '<Root>/BMS_Limits'
   *  S-Function (sfix_bitop): '<S3>/Bitwise OR2'
   */
  HCU_V2_Simulink_Y.Pack_Voltage = (real32_T)((uint32_T)rtb_y_n |
    HCU_V2_Simulink_U.BMS_Limits[4]) * 0.1F;

  /* Outputs for Atomic SubSystem: '<S3>/Bit Shift' */
  /* DataTypeConversion: '<S3>/Data Type Conversion1' incorporates:
   *  Inport: '<Root>/BMS_Limits'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.BMS_Limits[1]);

  /* End of Outputs for SubSystem: '<S3>/Bit Shift' */

  /* RelationalOperator: '<S39>/Compare' incorporates:
   *  Constant: '<S28>/Constant'
   *  Inport: '<Root>/BMS_Limits_age'
   *  RelationalOperator: '<S28>/Compare'
   */
  HCU_V2_Simulink_Y.Engine_Synced = (HCU_V2_Simulink_U.BMS_Limits_age < 350U);

  /* Switch: '<S3>/Switch' incorporates:
   *  Constant: '<S3>/Constant1'
   *  DataTypeConversion: '<S3>/Data Type Conversion'
   *  Inport: '<Root>/BMS_Limits'
   *  S-Function (sfix_bitop): '<S3>/Bitwise OR'
   */
  if (HCU_V2_Simulink_Y.Engine_Synced) {
    i = (int32_T)((uint32_T)rtb_y_n | HCU_V2_Simulink_U.BMS_Limits[0]);
  } else {
    i = 0;
  }

  /* End of Switch: '<S3>/Switch' */

  /* Outputs for Atomic SubSystem: '<S3>/Bit Shift1' */
  /* DataTypeConversion: '<S3>/Data Type Conversion6' incorporates:
   *  Inport: '<Root>/BMS_Limits'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.BMS_Limits[3]);

  /* End of Outputs for SubSystem: '<S3>/Bit Shift1' */

  /* Switch: '<S3>/Switch1' incorporates:
   *  Constant: '<S3>/Constant1'
   *  DataTypeConversion: '<S3>/Data Type Conversion3'
   *  Inport: '<Root>/BMS_Limits'
   *  S-Function (sfix_bitop): '<S3>/Bitwise OR1'
   */
  if (HCU_V2_Simulink_Y.Engine_Synced) {
    rtb_Switch1 = (int32_T)((uint32_T)rtb_y_n | HCU_V2_Simulink_U.BMS_Limits[2]);
  } else {
    rtb_Switch1 = 0;
  }

  /* End of Switch: '<S3>/Switch1' */

  /* Outport: '<Root>/BMS_Fault' incorporates:
   *  Constant: '<S44>/Constant'
   *  Constant: '<S45>/Constant'
   *  Logic: '<S37>/AND'
   *  RelationalOperator: '<S44>/Compare'
   *  RelationalOperator: '<S45>/Compare'
   */
  HCU_V2_Simulink_Y.BMS_Fault = ((i == 0) && (rtb_Switch1 == 0));

  /* Outputs for Atomic SubSystem: '<S2>/Bit Shift' */
  /* DataTypeConversion: '<S2>/Data Type Conversion' incorporates:
   *  Inport: '<Root>/ECU_Misc'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.ECU_Misc[0]);

  /* End of Outputs for SubSystem: '<S2>/Bit Shift' */

  /* Switch: '<S2>/Switch' incorporates:
   *  Constant: '<S2>/Constant'
   *  DataTypeConversion: '<S2>/Data Type Conversion1'
   *  DataTypeConversion: '<S2>/Data Type Conversion2'
   *  Inport: '<Root>/ECU_Misc'
   *  S-Function (sfix_bitop): '<S2>/Bitwise OR'
   */
  if (rtb_Compare_ph) {
    rtb_Switch_k = (int16_T)((uint32_T)rtb_y_n | HCU_V2_Simulink_U.ECU_Misc[1]);
  } else {
    rtb_Switch_k = 0;
  }

  /* End of Switch: '<S2>/Switch' */

  /* RelationalOperator: '<S39>/Compare' incorporates:
   *  Constant: '<S39>/Constant'
   */
  HCU_V2_Simulink_Y.Engine_Synced = (rtb_Switch_k == 3);

  /* Logic: '<S5>/OR' incorporates:
   *  Inport: '<Root>/Bench_Engine_Off'
   */
  OR = (HCU_V2_Simulink_Y.Engine_Synced || HCU_V2_Simulink_U.Bench_Engine_Off);

  /* Logic: '<Root>/OR' incorporates:
   *  Inport: '<Root>/Start_Button'
   *  Inport: '<Root>/Start_Button_GUI'
   */
  OR_e = (HCU_V2_Simulink_U.Start_Button_GUI || HCU_V2_Simulink_U.Start_Button);

  /* MATLAB Function: '<S8>/MATLAB Function' incorporates:
   *  Inport: '<Root>/ODrive_0_VI'
   */
  HCU_V2_Simulin_MATLABFunction_i(HCU_V2_Simulink_U.ODrive_0_VI,
    &HCU_V2_Simulink_Y.Bus_Current_1, &HCU_V2_Simulink_Y.Bus_Current_0);

  /* RelationalOperator: '<S62>/Compare' incorporates:
   *  Constant: '<S62>/Constant'
   *  Inport: '<Root>/ODrive_0_VI_age'
   */
  rtb_Compare_h = (HCU_V2_Simulink_U.ODrive_0_VI_age <= 120U);

  /* Switch: '<S8>/Switch' */
  if (rtb_Compare_h) {
    /* Outport: '<Root>/Bus_Voltage_0' */
    HCU_V2_Simulink_Y.Bus_Voltage_0 = HCU_V2_Simulink_Y.Bus_Current_1;
  } else {
    /* Outport: '<Root>/Bus_Voltage_0' incorporates:
     *  Constant: '<S8>/Constant'
     */
    HCU_V2_Simulink_Y.Bus_Voltage_0 = 0.0F;
  }

  /* End of Switch: '<S8>/Switch' */

  /* MATLAB Function: '<S8>/MATLAB Function1' incorporates:
   *  Inport: '<Root>/ODrive_1_VI'
   */
  HCU_V2_Simulin_MATLABFunction_i(HCU_V2_Simulink_U.ODrive_1_VI,
    &HCU_V2_Simulink_Y.Velocity_1, &HCU_V2_Simulink_Y.Bus_Current_1);

  /* RelationalOperator: '<S63>/Compare' incorporates:
   *  Constant: '<S63>/Constant'
   *  Inport: '<Root>/ODrive_1_VI_age'
   */
  rtb_Compare_dm = (HCU_V2_Simulink_U.ODrive_1_VI_age <= 120U);

  /* Switch: '<S8>/Switch2' */
  if (rtb_Compare_dm) {
    /* Outport: '<Root>/Bus_Voltage_1' */
    HCU_V2_Simulink_Y.Bus_Voltage_1 = HCU_V2_Simulink_Y.Velocity_1;
  } else {
    /* Outport: '<Root>/Bus_Voltage_1' incorporates:
     *  Constant: '<S8>/Constant1'
     */
    HCU_V2_Simulink_Y.Bus_Voltage_1 = 0.0F;
  }

  /* End of Switch: '<S8>/Switch2' */

  /* Chart: '<S5>/Safety_Supervisor' incorporates:
   *  Inport: '<Root>/Reset_Req'
   *  Outport: '<Root>/APPS_Implausibility'
   *  Outport: '<Root>/BMS_Fault'
   */
  if (HCU_V2_Simulink_DW.temporalCounter_i1 < 511) {
    HCU_V2_Simulink_DW.temporalCounter_i1++;
  }

  if (HCU_V2_Simulink_DW.is_active_c3_HCU_V2_Simulink == 0) {
    HCU_V2_Simulink_DW.is_active_c3_HCU_V2_Simulink = 1U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

    /* Outport: '<Root>/AIR_Enable' */
    HCU_V2_Simulink_Y.AIR_Enable = false;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

    /* Outport: '<Root>/Inverter_Enable' */
    HCU_V2_Simulink_Y.Inverter_Enable = false;

    /* Outport: '<Root>/State_Enum' */
    HCU_V2_Simulink_Y.State_Enum = 1U;
    HCU_V2_Simulink_DW.Fault_Cause = 0U;

    /* Outport: '<Root>/Fault_Code' */
    HCU_V2_Simulink_Y.Fault_Code = 0U;
  } else {
    switch (HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink) {
     case HCU_V2_Simulink_IN_DRIVE:
      HCU_V2_Simulink_DRIVE();
      break;

     case HCU_V2_Simulink_IN_Error_State:
      /* Outport: '<Root>/AIR_Enable' */
      HCU_V2_Simulink_Y.AIR_Enable = false;

      /* Outport: '<Root>/Pre_Charge_Enable' */
      HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

      /* Outport: '<Root>/Inverter_Enable' */
      HCU_V2_Simulink_Y.Inverter_Enable = false;

      /* Outport: '<Root>/State_Enum' */
      HCU_V2_Simulink_Y.State_Enum = 6U;
      if ((HCU_V2_Simulink_U.Reset_Req != 0.0) && (!HCU_V2_Simulink_Y.BMS_Fault))
      {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 1U;
        HCU_V2_Simulink_DW.Fault_Cause = 0U;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = 0U;
      }
      break;

     case HCU_V2_Simulink_IN_HV_OFF:
      /* Outport: '<Root>/AIR_Enable' */
      HCU_V2_Simulink_Y.AIR_Enable = false;

      /* Outport: '<Root>/Pre_Charge_Enable' */
      HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

      /* Outport: '<Root>/Inverter_Enable' */
      HCU_V2_Simulink_Y.Inverter_Enable = false;

      /* Outport: '<Root>/State_Enum' */
      HCU_V2_Simulink_Y.State_Enum = 1U;
      if (!HCU_V2_Simulink_Y.BMS_Fault) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_STANDBY;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 2U;
      } else {
        HCU_V2_Simulink_DW.Fault_Cause = 20U;
        HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU__IN_VALIDATING_ERROR_STATE4;
      }
      break;

     case HCU_V2_Simulink_IN_PRE_CHARGE:
      HCU_V2_Simulink_PRE_CHARGE();
      break;

     case HCU_V2_Simulink_IN_RELAY_SWAP:
      HCU_V2_Simulink_RELAY_SWAP();
      break;

     case HCU_V2_Simulink_IN_STANDBY:
      HCU_V2_Simulink_STANDBY(&OR, &OR_e);
      break;

     case IN_VALIDATING_APPS_IMPLAUSIBILI:
      if (!HCU_V2_Simulink_Y.APPS_Implausibility) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_DRIVE;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = true;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = true;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 5U;
      } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = false;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 1U;
        HCU_V2_Simulink_DW.Fault_Cause = 0U;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = 0U;
      }
      break;

     case HCU_V_IN_VALIDATING_ERROR_STATE:
      if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = HCU_V2_Simulink_DW.Fault_Cause;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = false;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 6U;
      } else if (!HCU_V2_Simulink_Y.BMS_Fault) {
        HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_PRE_CHARGE;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = true;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 3U;
      }
      break;

     case HCU__IN_VALIDATING_ERROR_STATE1:
      if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = HCU_V2_Simulink_DW.Fault_Cause;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = false;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 6U;
      } else if (!HCU_V2_Simulink_Y.BMS_Fault) {
        HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_RELAY_SWAP;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = true;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = true;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 4U;
      }
      break;

     case HCU__IN_VALIDATING_ERROR_STATE2:
      if (!HCU_V2_Simulink_Y.BMS_Fault) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_DRIVE;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = true;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = true;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 5U;
      } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = HCU_V2_Simulink_DW.Fault_Cause;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = false;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 6U;
      }
      break;

     case HCU__IN_VALIDATING_ERROR_STATE3:
      if (!HCU_V2_Simulink_Y.BMS_Fault) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_STANDBY;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 2U;
      } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = HCU_V2_Simulink_DW.Fault_Cause;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = false;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 6U;
      }
      break;

     case HCU__IN_VALIDATING_ERROR_STATE4:
      if (!HCU_V2_Simulink_Y.BMS_Fault) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = false;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 1U;
        HCU_V2_Simulink_DW.Fault_Cause = 0U;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = 0U;
      } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = HCU_V2_Simulink_DW.Fault_Cause;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = false;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 6U;
      }
      break;

     default:
      /* case IN_VALIDATING_FAULT_FROM_STANDBY: */
      if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;

        /* Outport: '<Root>/Inverter_Enable' */
        HCU_V2_Simulink_Y.Inverter_Enable = false;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 1U;
        HCU_V2_Simulink_DW.Fault_Cause = 0U;

        /* Outport: '<Root>/Fault_Code' */
        HCU_V2_Simulink_Y.Fault_Code = 0U;
      } else if (!HCU_V2_Simulink_Y.APPS_Implausibility) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_STANDBY;

        /* Outport: '<Root>/State_Enum' */
        HCU_V2_Simulink_Y.State_Enum = 2U;
      }
      break;
    }
  }

  /* End of Chart: '<S5>/Safety_Supervisor' */

  /* Switch: '<S6>/Switch' incorporates:
   *  Outport: '<Root>/Inverter_Enable'
   */
  if (HCU_V2_Simulink_Y.Inverter_Enable) {
    /* Switch: '<S6>/Switch' incorporates:
     *  Constant: '<S6>/Constant2'
     */
    HCU_V2_Simulink_B.Switch_e = 8U;
  } else {
    /* Switch: '<S6>/Switch' incorporates:
     *  Constant: '<S6>/Constant3'
     */
    HCU_V2_Simulink_B.Switch_e = 1U;
  }

  /* End of Switch: '<S6>/Switch' */

  /* Outport: '<Root>/Set_Axis_State_0_req' incorporates:
   *  Inport: '<Root>/ODrive_0_Heartbeat'
   *  RelationalOperator: '<S6>/Equal'
   */
  HCU_V2_Simulink_Y.Set_Axis_State_0_req = (HCU_V2_Simulink_B.Switch_e !=
    HCU_V2_Simulink_U.ODrive_0_Heartbeat[4]);

  /* Outport: '<Root>/Set_Axis_State_1_req' incorporates:
   *  Inport: '<Root>/ODrive_1_Heartbeat'
   *  RelationalOperator: '<S6>/Equal1'
   */
  HCU_V2_Simulink_Y.Set_Axis_State_1_req = (HCU_V2_Simulink_B.Switch_e !=
    HCU_V2_Simulink_U.ODrive_1_Heartbeat[4]);

  /* Sum: '<S38>/Subtract' incorporates:
   *  Inport: '<Root>/Brake_Zero_Offset'
   *  Outport: '<Root>/Brake_Pressure'
   */
  rtb_Regen_Shape = HCU_V2_Simulink_Y.Brake_Pressure -
    HCU_V2_Simulink_U.Brake_Zero_Offset;

  /* Saturate: '<S38>/Saturation' */
  if (rtb_Regen_Shape > 60.0F) {
    rtb_Regen_Shape = 60.0F;
  } else if (rtb_Regen_Shape < 0.0F) {
    rtb_Regen_Shape = 0.0F;
  }

  /* Lookup_n-D: '<S38>/Regen_Shape' incorporates:
   *  Saturate: '<S38>/Saturation'
   */
  rtb_Regen_Shape = look1_iflf_binlcpw(rtb_Regen_Shape,
    HCU_V2_Simulink_ConstP.Regen_Shape_bp01Data,
    HCU_V2_Simulink_ConstP.Regen_Shape_tableData, 5U);

  /* MinMax: '<S36>/Min' */
  HCU_V2_Simulink_Y.APPS_Clean = fminf(HCU_V2_Simulink_Y.Base_Torque_Demand,
    HCU_V2_Simulink_Y.Steering_Angle);

  /* Switch: '<S1>/Switch2' */
  if (rtb_Compare_dt) {
    /* Switch: '<S1>/Switch2' incorporates:
     *  DataTypeConversion: '<S1>/Data Type Conversion10'
     *  DataTypeConversion: '<S1>/Data Type Conversion11'
     *  DataTypeConversion: '<S1>/Data Type Conversion9'
     *  Gain: '<S1>/Gain2'
     *  Inport: '<Root>/APPS'
     *  S-Function (sfix_bitop): '<S1>/Bitwise OR2'
     */
    HCU_V2_Simulink_Y.Vehicle_Speed = (real32_T)(int16_T)((uint32_T)rtb_y |
      HCU_V2_Simulink_U.APPS[5]) * 0.036F;
  } else {
    /* Switch: '<S1>/Switch2' incorporates:
     *  Constant: '<S1>/Constant'
     */
    HCU_V2_Simulink_Y.Vehicle_Speed = 0.0F;
  }

  /* End of Switch: '<S1>/Switch2' */

  /* Relay: '<S38>/Relay' */
  HCU_V2_Simulink_DW.Relay_Mode = ((HCU_V2_Simulink_Y.Vehicle_Speed >= 5.0F) ||
    ((!(HCU_V2_Simulink_Y.Vehicle_Speed <= 3.0F)) &&
     HCU_V2_Simulink_DW.Relay_Mode));

  /* Switch: '<S38>/Switch' incorporates:
   *  Constant: '<S46>/Constant'
   *  Inport: '<Root>/Bench_Speed_Bypass'
   *  Inport: '<Root>/Motor_Regen_Max'
   *  Inport: '<Root>/Motor_Torque_Max'
   *  Lookup_n-D: '<S38>/Accel_Shape'
   *  MinMax: '<S36>/Min'
   *  Product: '<S38>/Product'
   *  Product: '<S38>/Product1'
   *  Product: '<S38>/Product3'
   *  RelationalOperator: '<S46>/Compare'
   *  Switch: '<S38>/Switch1'
   *  UnaryMinus: '<S38>/Unary Minus'
   */
  if (rtb_Regen_Shape > 0.0F) {
    rtb_Regen_Shape = -(rtb_Regen_Shape * HCU_V2_Simulink_U.Motor_Regen_Max);
  } else {
    if (HCU_V2_Simulink_U.Bench_Speed_Bypass > 0.0) {
      /* Switch: '<S38>/Switch1' incorporates:
       *  Constant: '<S38>/Constant'
       */
      tmp = 1;
    } else {
      /* Switch: '<S38>/Switch1' incorporates:
       *  Relay: '<S38>/Relay'
       */
      tmp = HCU_V2_Simulink_DW.Relay_Mode;
    }

    rtb_Regen_Shape = look1_iflf_binlcpw(HCU_V2_Simulink_Y.APPS_Clean,
      HCU_V2_Simulink_ConstP.Accel_Shape_bp01Data,
      HCU_V2_Simulink_ConstP.Accel_Shape_tableData, 6U) *
      HCU_V2_Simulink_U.Motor_Torque_Max * (real32_T)tmp;
  }

  /* End of Switch: '<S38>/Switch' */

  /* Delay: '<S47>/Delay' */
  if (HCU_V2_Simulink_DW.icLoad) {
    /* Sum: '<S47>/Difference Inputs2'
     *
     * Block description for '<S47>/Difference Inputs2':
     *
     *  Add in CPU
     */
    HCU_V2_Simulink_DW.Delay_DSTATE = rtb_Regen_Shape;
  }

  /* Product: '<S47>/delta fall limit' incorporates:
   *  Inport: '<Root>/Torque_Rate_Up'
   *  Product: '<S47>/delta rise limit'
   *  SampleTimeMath: '<S47>/sample time'
   *
   * About '<S47>/sample time':
   *  y = K where K = ( w * Ts )
   *   */
  HCU_V2_Simulink_Y.Steering_Angle = HCU_V2_Simulink_U.Torque_Rate_Up * 0.01F;

  /* Sum: '<S47>/Difference Inputs1' incorporates:
   *  Delay: '<S47>/Delay'
   *
   * Block description for '<S47>/Difference Inputs1':
   *
   *  Add in CPU
   */
  rtb_Regen_Shape -= HCU_V2_Simulink_DW.Delay_DSTATE;

  /* Switch: '<S49>/Switch2' incorporates:
   *  RelationalOperator: '<S49>/LowerRelop1'
   */
  if (!(rtb_Regen_Shape > HCU_V2_Simulink_Y.Steering_Angle)) {
    /* Product: '<S47>/delta fall limit' incorporates:
     *  Inport: '<Root>/Torque_Rate_Down'
     *  SampleTimeMath: '<S47>/sample time'
     *
     * About '<S47>/sample time':
     *  y = K where K = ( w * Ts )
     *   */
    HCU_V2_Simulink_Y.Steering_Angle = 0.01F *
      HCU_V2_Simulink_U.Torque_Rate_Down;

    /* Switch: '<S49>/Switch' incorporates:
     *  RelationalOperator: '<S49>/UpperRelop'
     */
    if (!(rtb_Regen_Shape < HCU_V2_Simulink_Y.Steering_Angle)) {
      /* Product: '<S47>/delta fall limit' */
      HCU_V2_Simulink_Y.Steering_Angle = rtb_Regen_Shape;
    }

    /* End of Switch: '<S49>/Switch' */
  }

  /* End of Switch: '<S49>/Switch2' */

  /* Sum: '<S47>/Difference Inputs2' incorporates:
   *  Delay: '<S47>/Delay'
   *
   * Block description for '<S47>/Difference Inputs2':
   *
   *  Add in CPU
   */
  HCU_V2_Simulink_DW.Delay_DSTATE += HCU_V2_Simulink_Y.Steering_Angle;

  /* Logic: '<S48>/OR' incorporates:
   *  Constant: '<S50>/Constant'
   *  Constant: '<S51>/Constant'
   *  Constant: '<S52>/Constant'
   *  Logic: '<S48>/AND'
   *  Logic: '<S48>/AND1'
   *  Logic: '<S48>/NOT1'
   *  Outport: '<Root>/Brake_Pressure'
   *  RelationalOperator: '<S50>/Compare'
   *  RelationalOperator: '<S51>/Compare'
   *  RelationalOperator: '<S52>/Compare'
   *  UnitDelay: '<S48>/Unit Delay'
   */
  HCU_V2_Simulink_DW.UnitDelay_DSTATE = (((HCU_V2_Simulink_Y.Brake_Pressure >=
    25.0F) && (HCU_V2_Simulink_Y.APPS_Clean >= 20.0F)) ||
    ((!(HCU_V2_Simulink_Y.APPS_Clean <= 5.0F)) &&
     HCU_V2_Simulink_DW.UnitDelay_DSTATE));

  /* Product: '<S38>/Product4' incorporates:
   *  Logic: '<S48>/NOT2'
   *  Outport: '<Root>/Inverter_Enable'
   *  Product: '<S38>/Product2'
   *  UnitDelay: '<S48>/Unit Delay'
   */
  HCU_V2_Simulink_Y.Base_Torque_Demand = HCU_V2_Simulink_DW.Delay_DSTATE *
    (real32_T)HCU_V2_Simulink_Y.Inverter_Enable * (real32_T)
    !HCU_V2_Simulink_DW.UnitDelay_DSTATE;

  /* Switch: '<S2>/Switch1' */
  if (rtb_Compare_ph) {
    /* Product: '<S47>/delta fall limit' incorporates:
     *  DataTypeConversion: '<S2>/Data Type Conversion5'
     *  DataTypeConversion: '<S2>/Data Type Conversion6'
     *  DataTypeConversion: '<S2>/Data Type Conversion7'
     *  Gain: '<S2>/Gain1'
     *  Inport: '<Root>/ECU_Misc'
     *  S-Function (sfix_bitop): '<S2>/Bitwise OR1'
     */
    HCU_V2_Simulink_Y.Steering_Angle = (real32_T)(int16_T)((uint32_T)rtb_y_hv |
      HCU_V2_Simulink_U.ECU_Misc[3]) * 0.03125F;
  } else {
    /* Product: '<S47>/delta fall limit' incorporates:
     *  Constant: '<S2>/Constant'
     */
    HCU_V2_Simulink_Y.Steering_Angle = 0.0F;
  }

  /* End of Switch: '<S2>/Switch1' */

  /* Sum: '<S42>/Subtract' incorporates:
   *  Inport: '<Root>/Steering_Centre'
   */
  rtb_Subtract = HCU_V2_Simulink_Y.Steering_Angle -
    HCU_V2_Simulink_U.Steering_Centre;

  /* Switch: '<S54>/Switch' incorporates:
   *  Inport: '<Root>/Steering_Deadzone'
   *  RelationalOperator: '<S54>/u_GTE_up'
   *  RelationalOperator: '<S54>/u_GT_lo'
   *  Switch: '<S54>/Switch1'
   *  UnaryMinus: '<S42>/Unary Minus'
   */
  if (rtb_Subtract >= HCU_V2_Simulink_U.Steering_Deadzone) {
    rtb_Regen_Shape = HCU_V2_Simulink_U.Steering_Deadzone;
  } else if (rtb_Subtract > -HCU_V2_Simulink_U.Steering_Deadzone) {
    /* Switch: '<S54>/Switch1' */
    rtb_Regen_Shape = rtb_Subtract;
  } else {
    rtb_Regen_Shape = -HCU_V2_Simulink_U.Steering_Deadzone;
  }

  /* Product: '<S42>/Product' incorporates:
   *  Inport: '<Root>/TV_Gain'
   *  Sum: '<S54>/Diff'
   *  Switch: '<S54>/Switch'
   */
  HCU_V2_Simulink_Y.Delta_Torque = (rtb_Subtract - rtb_Regen_Shape) *
    HCU_V2_Simulink_U.TV_Gain;

  /* Switch: '<S42>/Switch' incorporates:
   *  Constant: '<S42>/Constant'
   *  Constant: '<S53>/Constant'
   *  RelationalOperator: '<S53>/Compare'
   */
  if (HCU_V2_Simulink_Y.Base_Torque_Demand >= 0.0F) {
    /* Switch: '<S55>/Switch2' incorporates:
     *  Inport: '<Root>/Max_Torque_Split'
     *  RelationalOperator: '<S55>/LowerRelop1'
     *  RelationalOperator: '<S55>/UpperRelop'
     *  Switch: '<S55>/Switch'
     *  UnaryMinus: '<S42>/Unary Minus1'
     */
    if (HCU_V2_Simulink_Y.Delta_Torque > HCU_V2_Simulink_U.Max_Torque_Split) {
      rtb_Regen_Shape = HCU_V2_Simulink_U.Max_Torque_Split;
    } else if (HCU_V2_Simulink_Y.Delta_Torque <
               -HCU_V2_Simulink_U.Max_Torque_Split) {
      /* Switch: '<S55>/Switch' incorporates:
       *  UnaryMinus: '<S42>/Unary Minus1'
       */
      rtb_Regen_Shape = -HCU_V2_Simulink_U.Max_Torque_Split;
    } else {
      rtb_Regen_Shape = HCU_V2_Simulink_Y.Delta_Torque;
    }

    /* End of Switch: '<S55>/Switch2' */
  } else {
    rtb_Regen_Shape = 0.0F;
  }

  /* Gain: '<S42>/Gain' incorporates:
   *  Switch: '<S42>/Switch'
   */
  HCU_V2_Simulink_Y.Velocity_1 = 0.5F * rtb_Regen_Shape;

  /* Sum: '<S42>/Subtract1' */
  rtb_Regen_Shape = HCU_V2_Simulink_Y.Base_Torque_Demand -
    HCU_V2_Simulink_Y.Velocity_1;

  /* Sum: '<S42>/Add' */
  rtb_Subtract = HCU_V2_Simulink_Y.Velocity_1 +
    HCU_V2_Simulink_Y.Base_Torque_Demand;

  /* MATLAB Function: '<S4>/MATLAB Function' incorporates:
   *  Inport: '<Root>/ODrive_0_Encoder_Estimate'
   */
  HCU_V2_Simulink_MATLABFunction(HCU_V2_Simulink_U.ODrive_0_Encoder_Estimate,
    &HCU_V2_Simulink_Y.Velocity_0);

  /* Switch: '<S4>/Switch' incorporates:
   *  Constant: '<S32>/Constant'
   *  Constant: '<S4>/Constant'
   *  Inport: '<Root>/ODrive_0_Encoder_Estimate_age'
   *  RelationalOperator: '<S32>/Compare'
   */
  if (HCU_V2_Simulink_U.ODrive_0_Encoder_Estimate_age > 150U) {
    HCU_V2_Simulink_Y.Velocity_0 = 0.0F;
  }

  /* End of Switch: '<S4>/Switch' */

  /* MATLAB Function: '<S4>/MATLAB Function1' incorporates:
   *  Inport: '<Root>/ODrive_1_Encoder_Estimate'
   */
  HCU_V2_Simulink_MATLABFunction(HCU_V2_Simulink_U.ODrive_1_Encoder_Estimate,
    &HCU_V2_Simulink_Y.Velocity_1);

  /* Switch: '<S4>/Switch1' incorporates:
   *  Constant: '<S33>/Constant'
   *  Constant: '<S4>/Constant1'
   *  Inport: '<Root>/ODrive_1_Encoder_Estimate_age'
   *  RelationalOperator: '<S33>/Compare'
   */
  if (HCU_V2_Simulink_U.ODrive_1_Encoder_Estimate_age > 150U) {
    HCU_V2_Simulink_Y.Velocity_1 = 0.0F;
  }

  /* End of Switch: '<S4>/Switch1' */

  /* MATLAB Function: '<S5>/Torque_Power_Limiter' incorporates:
   *  Inport: '<Root>/BMS_Limits_age'
   *  Inport: '<Root>/BMS_Margin'
   *  Inport: '<Root>/Drive_Efficiency'
   *  Inport: '<Root>/Motor_Regen_Max'
   *  Inport: '<Root>/Motor_Torque_Max'
   *  Inport: '<Root>/ODrive_0_Encoder_Estimate_age'
   *  Inport: '<Root>/ODrive_1_Encoder_Estimate_age'
   *  Inport: '<Root>/Regen_Cutoff_Speed'
   *  Inport: '<Root>/Regen_Efficiency'
   *  Outport: '<Root>/Pack_Voltage'
   */
  cutoff = fmaxf(HCU_V2_Simulink_U.Regen_Cutoff_Speed, 0.001F);
  if (rtb_Regen_Shape < 0.0F) {
    rtb_Regen_Shape *= fminf(1.0F, fabsf(HCU_V2_Simulink_Y.Velocity_0) / cutoff);
  }

  if (rtb_Subtract < 0.0F) {
    rtb_Subtract *= fminf(1.0F, fabsf(HCU_V2_Simulink_Y.Velocity_1) / cutoff);
  }

  rtb_Regen_Shape = fminf(fmaxf(rtb_Regen_Shape,
    -HCU_V2_Simulink_U.Motor_Regen_Max), HCU_V2_Simulink_U.Motor_Torque_Max);
  rtb_Subtract = fminf(fmaxf(rtb_Subtract, -HCU_V2_Simulink_U.Motor_Regen_Max),
                       HCU_V2_Simulink_U.Motor_Torque_Max);
  HCU_V2_Simulink_Y.Power_Demand = 6.28318548F * fabsf
    (HCU_V2_Simulink_Y.Velocity_0) * rtb_Regen_Shape + 6.28318548F * fabsf
    (HCU_V2_Simulink_Y.Velocity_1) * rtb_Subtract;
  if (HCU_V2_Simulink_Y.Power_Demand >= 0.0F) {
    HCU_V2_Simulink_Y.Power_Budget = HCU_V2_Simulink_U.Drive_Efficiency *
      HCU_V2_Simulink_U.BMS_Margin * (real32_T)i *
      HCU_V2_Simulink_Y.Pack_Voltage;
    cutoff = HCU_V2_Simulink_Y.Power_Budget / fmaxf
      (HCU_V2_Simulink_Y.Power_Demand, 1.0F);
  } else {
    HCU_V2_Simulink_Y.Power_Budget = HCU_V2_Simulink_U.BMS_Margin * (real32_T)
      rtb_Switch1 * HCU_V2_Simulink_Y.Pack_Voltage / fmaxf
      (HCU_V2_Simulink_U.Regen_Efficiency, 0.001F);
    cutoff = HCU_V2_Simulink_Y.Power_Budget / fmaxf
      (-HCU_V2_Simulink_Y.Power_Demand, 1.0F);
  }

  HCU_V2_Simulink_Y.Torque_Scale_Factor = fminf(1.0F, fmaxf(0.0F, cutoff));
  if ((HCU_V2_Simulink_U.BMS_Limits_age >= 150U) ||
      (HCU_V2_Simulink_U.ODrive_0_Encoder_Estimate_age >= 150U) ||
      (HCU_V2_Simulink_U.ODrive_1_Encoder_Estimate_age >= 150U) ||
      (!(HCU_V2_Simulink_Y.Pack_Voltage > 0.0F))) {
    HCU_V2_Simulink_Y.Torque_Scale_Factor = 0.0F;
  }

  /* Outport: '<Root>/Torque_Request_Right' incorporates:
   *  Inport: '<Root>/Right_Direction'
   *  MATLAB Function: '<S5>/Torque_Power_Limiter'
   *  Product: '<S5>/Product1'
   */
  HCU_V2_Simulink_Y.Torque_Request_Right = HCU_V2_Simulink_Y.Torque_Scale_Factor
    * rtb_Subtract * (real32_T)HCU_V2_Simulink_U.Right_Direction;

  /* Outport: '<Root>/Torque_Request_Left' incorporates:
   *  Inport: '<Root>/Left_Direction'
   *  MATLAB Function: '<S5>/Torque_Power_Limiter'
   *  Product: '<S5>/Product'
   */
  HCU_V2_Simulink_Y.Torque_Request_Left = HCU_V2_Simulink_Y.Torque_Scale_Factor *
    rtb_Regen_Shape * (real32_T)HCU_V2_Simulink_U.Left_Direction;

  /* S-Function (any2byte): '<S9>/Byte Pack1' incorporates:
   *  Outport: '<Root>/Torque_Request_Left'
   */

  /* Pack: <S9>/Byte Pack1 */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate_i[0],
                &HCU_V2_Simulink_Y.Torque_Request_Left,
                4);

  /* S-Function (any2byte): '<S9>/Byte Pack' incorporates:
   *  Outport: '<Root>/Torque_Request_Right'
   */

  /* Pack: <S9>/Byte Pack */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate1_e[0],
                &HCU_V2_Simulink_Y.Torque_Request_Right,
                4);

  /* S-Function (any2byte): '<S6>/Byte Pack2' */

  /* Pack: <S6>/Byte Pack2 */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate2_m[0],
                &HCU_V2_Simulink_B.Switch_e,
                1);

  /* Product: '<S10>/Product' incorporates:
   *  Inport: '<Root>/Vel_Scale'
   *  Outport: '<Root>/Torque_Request_Left'
   */
  HCU_V2_Simulink_B.Product_l = HCU_V2_Simulink_Y.Torque_Request_Left *
    HCU_V2_Simulink_U.Vel_Scale;

  /* S-Function (any2byte): '<S10>/Byte Pack1' */

  /* Pack: <S10>/Byte Pack1 */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate[0],
                &HCU_V2_Simulink_B.Product_l,
                4);

  /* Product: '<S10>/Product1' incorporates:
   *  Inport: '<Root>/Vel_Scale'
   *  Outport: '<Root>/Torque_Request_Right'
   */
  HCU_V2_Simulink_B.Product1_n = HCU_V2_Simulink_U.Vel_Scale *
    HCU_V2_Simulink_Y.Torque_Request_Right;

  /* S-Function (any2byte): '<S10>/Byte Pack' */

  /* Pack: <S10>/Byte Pack */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate1[0],
                &HCU_V2_Simulink_B.Product1_n,
                4);

  /* Switch: '<S7>/Switch' incorporates:
   *  Inport: '<Root>/Bench_Velocity_Mode'
   */
  if (HCU_V2_Simulink_U.Bench_Velocity_Mode) {
    /* Switch: '<S7>/Switch' incorporates:
     *  Constant: '<S7>/Constant2'
     */
    HCU_V2_Simulink_B.Switch_el = 2U;
  } else {
    /* Switch: '<S7>/Switch' incorporates:
     *  Constant: '<S7>/Constant3'
     */
    HCU_V2_Simulink_B.Switch_el = 1U;
  }

  /* End of Switch: '<S7>/Switch' */

  /* S-Function (any2byte): '<S7>/Byte Pack1' */

  /* Pack: <S7>/Byte Pack1 */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate2[0],
                &HCU_V2_Simulink_B.Switch_el,
                1);

  /* S-Function (any2byte): '<S7>/Byte Pack2' */

  /* Pack: <S7>/Byte Pack2 */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate2[4],
                &HCU_V2_Simulink_B.Switch_el,
                1);

  /* Switch: '<S8>/Switch3' */
  if (!rtb_Compare_dm) {
    /* Outport: '<Root>/Bus_Current_1' incorporates:
     *  Constant: '<S8>/Constant1'
     */
    HCU_V2_Simulink_Y.Bus_Current_1 = 0.0F;
  }

  /* End of Switch: '<S8>/Switch3' */

  /* Switch: '<S8>/Switch1' */
  if (!rtb_Compare_h) {
    /* Outport: '<Root>/Bus_Current_0' incorporates:
     *  Constant: '<S8>/Constant'
     */
    HCU_V2_Simulink_Y.Bus_Current_0 = 0.0F;
  }

  /* End of Switch: '<S8>/Switch1' */

  /* Outport: '<Root>/Sync_State' */
  HCU_V2_Simulink_Y.Sync_State = (real32_T)rtb_Switch_k;

  /* Outport: '<Root>/CCL' */
  HCU_V2_Simulink_Y.CCL = (real32_T)rtb_Switch1;

  /* Outport: '<Root>/DCL' */
  HCU_V2_Simulink_Y.DCL = (real32_T)i;
  for (i = 0; i < 8; i++) {
    /* Outport: '<Root>/Set_Controller_Mode_0' */
    HCU_V2_Simulink_Y.Set_Controller_Mode_0[i] =
      HCU_V2_Simulink_B.VectorConcatenate2[i];

    /* Outport: '<Root>/Set_Controller_Mode_1' */
    HCU_V2_Simulink_Y.Set_Controller_Mode_1[i] =
      HCU_V2_Simulink_B.VectorConcatenate2[i];

    /* Outport: '<Root>/Velocity_Right' */
    HCU_V2_Simulink_Y.Velocity_Right[i] = HCU_V2_Simulink_B.VectorConcatenate1[i];

    /* Outport: '<Root>/Velocity_Left' */
    HCU_V2_Simulink_Y.Velocity_Left[i] = HCU_V2_Simulink_B.VectorConcatenate[i];

    /* Outport: '<Root>/Set_Axis_State_0' */
    HCU_V2_Simulink_Y.Set_Axis_State_0[i] =
      HCU_V2_Simulink_B.VectorConcatenate2_m[i];

    /* Outport: '<Root>/Set_Axis_State_1' */
    HCU_V2_Simulink_Y.Set_Axis_State_1[i] =
      HCU_V2_Simulink_B.VectorConcatenate2_m[i];

    /* Outport: '<Root>/Torque_Right' */
    HCU_V2_Simulink_Y.Torque_Right[i] = HCU_V2_Simulink_B.VectorConcatenate1_e[i];

    /* Outport: '<Root>/Torque_Left' */
    HCU_V2_Simulink_Y.Torque_Left[i] = HCU_V2_Simulink_B.VectorConcatenate_i[i];
  }

  /* Outport: '<Root>/Set_Controller_Mode_0_req' incorporates:
   *  Constant: '<S58>/Constant'
   *  Inport: '<Root>/ODrive_0_Heartbeat'
   *  RelationalOperator: '<S58>/Compare'
   */
  HCU_V2_Simulink_Y.Set_Controller_Mode_0_req =
    (HCU_V2_Simulink_U.ODrive_0_Heartbeat[4] == 1);

  /* Outport: '<Root>/Set_Controller_Mode_1_req' incorporates:
   *  Constant: '<S59>/Constant'
   *  Inport: '<Root>/ODrive_1_Heartbeat'
   *  RelationalOperator: '<S59>/Compare'
   */
  HCU_V2_Simulink_Y.Set_Controller_Mode_1_req =
    (HCU_V2_Simulink_U.ODrive_1_Heartbeat[4] == 1);

  /* Outport: '<Root>/User_LED_1' incorporates:
   *  Inport: '<Root>/bus1_ok'
   */
  HCU_V2_Simulink_Y.User_LED_1 = HCU_V2_Simulink_U.bus1_ok;

  /* Outport: '<Root>/User_LED_2' incorporates:
   *  Inport: '<Root>/bus2_ok'
   */
  HCU_V2_Simulink_Y.User_LED_2 = HCU_V2_Simulink_U.bus2_ok;

  /* Outport: '<Root>/Velocity_Mode_Active' incorporates:
   *  Inport: '<Root>/Bench_Velocity_Mode'
   */
  HCU_V2_Simulink_Y.Velocity_Mode_Active = HCU_V2_Simulink_U.Bench_Velocity_Mode;

  /* Update for Delay: '<S47>/Delay' */
  HCU_V2_Simulink_DW.icLoad = false;
}

/* Model initialize function */
void HCU_V2_Simulink_initialize(void)
{
  /* InitializeConditions for Delay: '<S47>/Delay' */
  HCU_V2_Simulink_DW.icLoad = true;
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
