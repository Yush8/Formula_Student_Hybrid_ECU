/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: HCU_V2_Simulink.c
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

#include "HCU_V2_Simulink.h"
#include "rtwtypes.h"
#include "HCU_V2_Simulink_private.h"
#include <string.h>
#include <stddef.h>
#include <math.h>

/* Named constants for Chart: '<S3>/Safety_Supervisor' */
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
static void HCU_V2_Simulink_PRE_CHARGE(const real32_T *DataTypeConversion9,
  const boolean_T *AND, const real32_T *Switch, const real32_T *Switch2);
static void HCU_V2_Simulink_RELAY_SWAP(const boolean_T *AND);
static void HCU_V2_Simulink_STANDBY(const boolean_T *Compare, const boolean_T
  *AND);

/*
 * Output and update for atomic system:
 *    '<S1>/Bit Shift'
 *    '<S1>/Bit Shift1'
 *    '<S2>/Bit Shift'
 *    '<S2>/Bit Shift1'
 */
uint16_T HCU_V2_Simulink_BitShift(uint16_T rtu_u)
{
  /* MATLAB Function: '<S7>/bit_shift' */
  return (uint16_T)(rtu_u << 8);
}

/*
 * Output and update for atomic system:
 *    '<S4>/MATLAB Function'
 *    '<S4>/MATLAB Function1'
 */
void HCU_V2_Simulink_MATLABFunction(const uint8_T rtu_CAN_Data[8], real32_T
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

/* Function for Chart: '<S3>/Safety_Supervisor' */
static void HCU_V2_Simulink_PRE_CHARGE(const real32_T *DataTypeConversion9,
  const boolean_T *AND, const real32_T *Switch, const real32_T *Switch2)
{
  real32_T tmp;

  /* Outport: '<Root>/Pre_Charge_Enable' */
  /* Inport: '<Root>/SDC_Monitor' */
  HCU_V2_Simulink_Y.Pre_Charge_Enable = true;
  tmp = 0.9F * *DataTypeConversion9;
  if ((*Switch > tmp) && (*Switch2 > tmp)) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_RELAY_SWAP;

    /* Outport: '<Root>/AIR_Enable' */
    HCU_V2_Simulink_Y.AIR_Enable = true;
  } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 1000) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_Error_State;

    /* Outport: '<Root>/AIR_Enable' */
    HCU_V2_Simulink_Y.AIR_Enable = false;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_B.Inverter_Enable = false;
  } else if (*AND) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V_IN_VALIDATING_ERROR_STATE;
  } else if (!HCU_V2_Simulink_U.SDC_Monitor) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

    /* Outport: '<Root>/AIR_Enable' */
    HCU_V2_Simulink_Y.AIR_Enable = false;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_B.Inverter_Enable = false;
  }

  /* End of Inport: '<Root>/SDC_Monitor' */
}

/* Function for Chart: '<S3>/Safety_Supervisor' */
static void HCU_V2_Simulink_RELAY_SWAP(const boolean_T *AND)
{
  /* Outport: '<Root>/Pre_Charge_Enable' */
  HCU_V2_Simulink_Y.Pre_Charge_Enable = true;

  /* Outport: '<Root>/AIR_Enable' */
  /* Inport: '<Root>/SDC_Monitor' */
  HCU_V2_Simulink_Y.AIR_Enable = true;
  if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_DRIVE;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_B.Inverter_Enable = true;
  } else if (*AND) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU__IN_VALIDATING_ERROR_STATE1;
  } else if (!HCU_V2_Simulink_U.SDC_Monitor) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

    /* Outport: '<Root>/AIR_Enable' */
    HCU_V2_Simulink_Y.AIR_Enable = false;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_B.Inverter_Enable = false;
  }

  /* End of Inport: '<Root>/SDC_Monitor' */
}

/* Function for Chart: '<S3>/Safety_Supervisor' */
static void HCU_V2_Simulink_STANDBY(const boolean_T *Compare, const boolean_T
  *AND)
{
  /* Inport: '<Root>/Start_Button' incorporates:
   *  Inport: '<Root>/SDC_Monitor'
   */
  if (HCU_V2_Simulink_U.Start_Button) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_PRE_CHARGE;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = true;
  } else if ((*Compare) || (*AND)) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = IN_VALIDATING_FAULT_FROM_STANDB;
  } else if (*AND) {
    HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU__IN_VALIDATING_ERROR_STATE3;
  } else if (!HCU_V2_Simulink_U.SDC_Monitor) {
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

    /* Outport: '<Root>/AIR_Enable' */
    HCU_V2_Simulink_Y.AIR_Enable = false;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_B.Inverter_Enable = false;
  }

  /* End of Inport: '<Root>/Start_Button' */
}

/* Model step function */
void HCU_V2_Simulink_step(void)
{
  int32_T i;
  real32_T DataTypeConversion9;
  real32_T rtb_Bus_Current;
  real32_T rtb_Bus_Current_m;
  real32_T rtb_Bus_Voltage;
  real32_T rtb_Switch;
  real32_T rtb_Switch1;
  uint16_T rtb_y;
  uint16_T rtb_y_n;
  boolean_T AND;
  boolean_T rtb_Equal1;

  /* SignalConversion generated from: '<S6>/Vector Concatenate2' incorporates:
   *  Concatenate: '<S6>/Vector Concatenate2'
   */
  for (i = 0; i < 7; i++) {
    HCU_V2_Simulink_B.VectorConcatenate2[i + 1] = 0U;
  }

  /* End of SignalConversion generated from: '<S6>/Vector Concatenate2' */

  /* SignalConversion generated from: '<S5>/Vector Concatenate' incorporates:
   *  Concatenate: '<S5>/Vector Concatenate'
   *  Constant: '<S5>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate[4] = 0U;

  /* SignalConversion generated from: '<S5>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S5>/Vector Concatenate1'
   *  Constant: '<S5>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1[4] = 0U;

  /* SignalConversion generated from: '<S5>/Vector Concatenate' incorporates:
   *  Concatenate: '<S5>/Vector Concatenate'
   *  Constant: '<S5>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate[5] = 0U;

  /* SignalConversion generated from: '<S5>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S5>/Vector Concatenate1'
   *  Constant: '<S5>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1[5] = 0U;

  /* SignalConversion generated from: '<S5>/Vector Concatenate' incorporates:
   *  Concatenate: '<S5>/Vector Concatenate'
   *  Constant: '<S5>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate[6] = 0U;

  /* SignalConversion generated from: '<S5>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S5>/Vector Concatenate1'
   *  Constant: '<S5>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1[6] = 0U;

  /* SignalConversion generated from: '<S5>/Vector Concatenate' incorporates:
   *  Concatenate: '<S5>/Vector Concatenate'
   *  Constant: '<S5>/Constant'
   */
  HCU_V2_Simulink_B.VectorConcatenate[7] = 0U;

  /* SignalConversion generated from: '<S5>/Vector Concatenate1' incorporates:
   *  Concatenate: '<S5>/Vector Concatenate1'
   *  Constant: '<S5>/Constant1'
   */
  HCU_V2_Simulink_B.VectorConcatenate1[7] = 0U;

  /* RelationalOperator: '<S9>/Compare' incorporates:
   *  Constant: '<S9>/Constant'
   *  Inport: '<Root>/APPS_age'
   */
  rtb_Equal1 = (HCU_V2_Simulink_U.APPS_age < 50U);

  /* Outputs for Atomic SubSystem: '<S1>/Bit Shift' */
  /* DataTypeConversion: '<S1>/Data Type Conversion' incorporates:
   *  Inport: '<Root>/APPS'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.APPS[0]);

  /* End of Outputs for SubSystem: '<S1>/Bit Shift' */

  /* Switch: '<S1>/Switch' incorporates:
   *  Constant: '<S1>/Constant'
   *  DataTypeConversion: '<S1>/Data Type Conversion1'
   *  DataTypeConversion: '<S1>/Data Type Conversion2'
   *  DataTypeConversion: '<S1>/Data Type Conversion4'
   *  Gain: '<S1>/Gain'
   *  Inport: '<Root>/APPS'
   *  S-Function (sfix_bitop): '<S1>/Bitwise OR'
   */
  if (rtb_Equal1) {
    rtb_Switch = (real32_T)(int16_T)((uint32_T)rtb_y_n | HCU_V2_Simulink_U.APPS
      [1]) * 0.0122070312F;
  } else {
    rtb_Switch = 0.0F;
  }

  /* End of Switch: '<S1>/Switch' */

  /* Outputs for Atomic SubSystem: '<S1>/Bit Shift1' */
  /* DataTypeConversion: '<S1>/Data Type Conversion3' incorporates:
   *  Inport: '<Root>/APPS'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.APPS[2]);

  /* End of Outputs for SubSystem: '<S1>/Bit Shift1' */

  /* Switch: '<S1>/Switch1' incorporates:
   *  Constant: '<S1>/Constant'
   *  DataTypeConversion: '<S1>/Data Type Conversion5'
   *  DataTypeConversion: '<S1>/Data Type Conversion6'
   *  DataTypeConversion: '<S1>/Data Type Conversion7'
   *  Gain: '<S1>/Gain1'
   *  Inport: '<Root>/APPS'
   *  S-Function (sfix_bitop): '<S1>/Bitwise OR1'
   */
  if (rtb_Equal1) {
    rtb_Switch1 = (real32_T)(int16_T)((uint32_T)rtb_y_n |
      HCU_V2_Simulink_U.APPS[3]) * 0.0122070312F;
  } else {
    rtb_Switch1 = 0.0F;
  }

  /* End of Switch: '<S1>/Switch1' */

  /* DataTypeConversion: '<S2>/Data Type Conversion9' incorporates:
   *  Inport: '<Root>/BMS_Limits'
   */
  DataTypeConversion9 = HCU_V2_Simulink_U.BMS_Limits[4];

  /* RelationalOperator: '<S20>/Compare' incorporates:
   *  Abs: '<S17>/Abs'
   *  Constant: '<S20>/Constant'
   *  Sum: '<S17>/Subtract'
   */
  rtb_Equal1 = (fabsf(rtb_Switch - rtb_Switch1) > 10.0F);

  /* Outputs for Atomic SubSystem: '<S2>/Bit Shift' */
  /* DataTypeConversion: '<S2>/Data Type Conversion1' incorporates:
   *  Inport: '<Root>/BMS_Limits'
   */
  rtb_y_n = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.BMS_Limits[1]);

  /* End of Outputs for SubSystem: '<S2>/Bit Shift' */

  /* Outputs for Atomic SubSystem: '<S2>/Bit Shift1' */
  /* DataTypeConversion: '<S2>/Data Type Conversion6' incorporates:
   *  Inport: '<Root>/BMS_Limits'
   */
  rtb_y = HCU_V2_Simulink_BitShift((uint16_T)HCU_V2_Simulink_U.BMS_Limits[3]);

  /* End of Outputs for SubSystem: '<S2>/Bit Shift1' */

  /* Switch: '<S2>/Switch1' incorporates:
   *  Constant: '<S14>/Constant'
   *  Constant: '<S2>/Constant1'
   *  DataTypeConversion: '<S2>/Data Type Conversion'
   *  DataTypeConversion: '<S2>/Data Type Conversion3'
   *  DataTypeConversion: '<S2>/Data Type Conversion5'
   *  Inport: '<Root>/BMS_Limits'
   *  Inport: '<Root>/BMS_Limits_age'
   *  RelationalOperator: '<S14>/Compare'
   *  S-Function (sfix_bitop): '<S2>/Bitwise OR'
   *  S-Function (sfix_bitop): '<S2>/Bitwise OR1'
   *  Switch: '<S2>/Switch'
   */
  if (HCU_V2_Simulink_U.BMS_Limits_age < 350U) {
    rtb_Bus_Voltage = (real32_T)((uint32_T)rtb_y | HCU_V2_Simulink_U.BMS_Limits
      [2]);
    i = (int32_T)((uint32_T)rtb_y_n | HCU_V2_Simulink_U.BMS_Limits[0]);
  } else {
    rtb_Bus_Voltage = 0.0F;
    i = 0;
  }

  /* End of Switch: '<S2>/Switch1' */

  /* Logic: '<S18>/AND' incorporates:
   *  Constant: '<S21>/Constant'
   *  Constant: '<S22>/Constant'
   *  RelationalOperator: '<S21>/Compare'
   *  RelationalOperator: '<S22>/Compare'
   *  Switch: '<S2>/Switch'
   */
  AND = ((i == 0) && (rtb_Bus_Voltage == 0.0F));

  /* MATLAB Function: '<S4>/MATLAB Function' incorporates:
   *  Inport: '<Root>/ODrive_0_VI'
   */
  HCU_V2_Simulink_MATLABFunction(HCU_V2_Simulink_U.ODrive_0_VI, &rtb_Bus_Voltage,
    &rtb_Bus_Current_m);

  /* Switch: '<S4>/Switch' incorporates:
   *  Constant: '<S23>/Constant'
   *  Inport: '<Root>/ODrive_0_VI_age'
   *  RelationalOperator: '<S23>/Compare'
   */
  if (HCU_V2_Simulink_U.ODrive_0_VI_age <= 120U) {
    /* Switch: '<S4>/Switch' */
    rtb_Bus_Current_m = rtb_Bus_Voltage;
  } else {
    /* Switch: '<S4>/Switch' incorporates:
     *  Constant: '<S4>/Constant'
     */
    rtb_Bus_Current_m = 0.0F;
  }

  /* End of Switch: '<S4>/Switch' */

  /* MATLAB Function: '<S4>/MATLAB Function1' incorporates:
   *  Inport: '<Root>/ODrive_1_VI'
   */
  HCU_V2_Simulink_MATLABFunction(HCU_V2_Simulink_U.ODrive_1_VI, &rtb_Bus_Voltage,
    &rtb_Bus_Current);

  /* Switch: '<S4>/Switch2' incorporates:
   *  Constant: '<S24>/Constant'
   *  Inport: '<Root>/ODrive_1_VI_age'
   *  RelationalOperator: '<S24>/Compare'
   */
  if (HCU_V2_Simulink_U.ODrive_1_VI_age > 120U) {
    /* Switch: '<S4>/Switch2' incorporates:
     *  Constant: '<S4>/Constant1'
     */
    rtb_Bus_Voltage = 0.0F;
  }

  /* End of Switch: '<S4>/Switch2' */

  /* Chart: '<S3>/Safety_Supervisor' incorporates:
   *  Inport: '<Root>/SDC_Monitor'
   */
  if (HCU_V2_Simulink_DW.temporalCounter_i1 < 1023) {
    HCU_V2_Simulink_DW.temporalCounter_i1++;
  }

  if (HCU_V2_Simulink_DW.is_active_c3_HCU_V2_Simulink == 0) {
    HCU_V2_Simulink_DW.is_active_c3_HCU_V2_Simulink = 1U;
    HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

    /* Outport: '<Root>/AIR_Enable' */
    HCU_V2_Simulink_Y.AIR_Enable = false;

    /* Outport: '<Root>/Pre_Charge_Enable' */
    HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
    HCU_V2_Simulink_B.Inverter_Enable = false;
  } else {
    switch (HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink) {
     case HCU_V2_Simulink_IN_DRIVE:
      /* Outport: '<Root>/AIR_Enable' */
      HCU_V2_Simulink_Y.AIR_Enable = true;

      /* Outport: '<Root>/Pre_Charge_Enable' */
      HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
      HCU_V2_Simulink_B.Inverter_Enable = true;
      if (AND) {
        HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU__IN_VALIDATING_ERROR_STATE2;
      } else if (rtb_Equal1) {
        HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          IN_VALIDATING_APPS_IMPLAUSIBILI;
      } else if (!HCU_V2_Simulink_U.SDC_Monitor) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = false;
      }
      break;

     case HCU_V2_Simulink_IN_Error_State:
      /* Outport: '<Root>/AIR_Enable' */
      HCU_V2_Simulink_Y.AIR_Enable = false;

      /* Outport: '<Root>/Pre_Charge_Enable' */
      HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
      HCU_V2_Simulink_B.Inverter_Enable = false;
      break;

     case HCU_V2_Simulink_IN_HV_OFF:
      /* Outport: '<Root>/AIR_Enable' */
      HCU_V2_Simulink_Y.AIR_Enable = false;

      /* Outport: '<Root>/Pre_Charge_Enable' */
      HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
      HCU_V2_Simulink_B.Inverter_Enable = false;
      if (!AND) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_STANDBY;
      } else {
        HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU__IN_VALIDATING_ERROR_STATE4;
      }
      break;

     case HCU_V2_Simulink_IN_PRE_CHARGE:
      HCU_V2_Simulink_PRE_CHARGE(&DataTypeConversion9, &AND, &rtb_Bus_Current_m,
        &rtb_Bus_Voltage);
      break;

     case HCU_V2_Simulink_IN_RELAY_SWAP:
      HCU_V2_Simulink_RELAY_SWAP(&AND);
      break;

     case HCU_V2_Simulink_IN_STANDBY:
      HCU_V2_Simulink_STANDBY(&rtb_Equal1, &AND);
      break;

     case IN_VALIDATING_APPS_IMPLAUSIBILI:
      if (!rtb_Equal1) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_DRIVE;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = true;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = true;
      } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = false;
      }
      break;

     case HCU_V_IN_VALIDATING_ERROR_STATE:
      if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = false;
      } else if (!AND) {
        HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_PRE_CHARGE;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = true;
      }
      break;

     case HCU__IN_VALIDATING_ERROR_STATE1:
      if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = false;
      } else if (!AND) {
        HCU_V2_Simulink_DW.temporalCounter_i1 = 0U;
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_RELAY_SWAP;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = true;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = true;
      }
      break;

     case HCU__IN_VALIDATING_ERROR_STATE2:
      if (!AND) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_DRIVE;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = true;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = true;
      } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = false;
      }
      break;

     case HCU__IN_VALIDATING_ERROR_STATE3:
      if (!AND) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_STANDBY;
      } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = false;
      }
      break;

     case HCU__IN_VALIDATING_ERROR_STATE4:
      if (!AND) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_HV_OFF;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = false;
      } else if (HCU_V2_Simulink_DW.temporalCounter_i1 >= 10) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink =
          HCU_V2_Simulink_IN_Error_State;

        /* Outport: '<Root>/AIR_Enable' */
        HCU_V2_Simulink_Y.AIR_Enable = false;

        /* Outport: '<Root>/Pre_Charge_Enable' */
        HCU_V2_Simulink_Y.Pre_Charge_Enable = false;
        HCU_V2_Simulink_B.Inverter_Enable = false;
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
        HCU_V2_Simulink_B.Inverter_Enable = false;
      } else if ((!rtb_Equal1) && (!AND)) {
        HCU_V2_Simulink_DW.is_c3_HCU_V2_Simulink = HCU_V2_Simulink_IN_STANDBY;
      }
      break;
    }
  }

  /* End of Chart: '<S3>/Safety_Supervisor' */

  /* Switch: '<S3>/Switch' */
  if (HCU_V2_Simulink_B.Inverter_Enable) {
    /* Switch: '<S3>/Switch' incorporates:
     *  Gain: '<S3>/Multiply'
     *  MinMax: '<S17>/Min'
     */
    HCU_V2_Simulink_B.Switch_j = 0.0442F * fminf(rtb_Switch, rtb_Switch1);
  } else {
    /* Switch: '<S3>/Switch' incorporates:
     *  Constant: '<S3>/Constant'
     */
    HCU_V2_Simulink_B.Switch_j = 0.0F;
  }

  /* End of Switch: '<S3>/Switch' */

  /* Gain: '<S3>/Gain' */
  HCU_V2_Simulink_B.Gain = -HCU_V2_Simulink_B.Switch_j;

  /* S-Function (any2byte): '<S5>/Byte Pack1' */

  /* Pack: <S5>/Byte Pack1 */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate[0], &HCU_V2_Simulink_B.Gain,
                4);

  /* S-Function (any2byte): '<S5>/Byte Pack' */

  /* Pack: <S5>/Byte Pack */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate1[0],
                &HCU_V2_Simulink_B.Switch_j,
                4);

  /* Switch: '<S6>/Switch' */
  if (HCU_V2_Simulink_B.Inverter_Enable) {
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

  /* S-Function (any2byte): '<S6>/Byte Pack2' */

  /* Pack: <S6>/Byte Pack2 */
  (void) memcpy(&HCU_V2_Simulink_B.VectorConcatenate2[0],
                &HCU_V2_Simulink_B.Switch_e,
                1);

  /* Outport: '<Root>/Set_Axis_State_0_req' incorporates:
   *  Inport: '<Root>/ODrive_0_Heartbeat'
   *  RelationalOperator: '<S6>/Equal'
   */
  HCU_V2_Simulink_Y.Set_Axis_State_0_req = (HCU_V2_Simulink_B.Switch_e !=
    HCU_V2_Simulink_U.ODrive_0_Heartbeat[4]);
  for (i = 0; i < 8; i++) {
    /* Outport: '<Root>/Set_Axis_State_0' */
    HCU_V2_Simulink_Y.Set_Axis_State_0[i] =
      HCU_V2_Simulink_B.VectorConcatenate2[i];

    /* Outport: '<Root>/Set_Axis_State_1' */
    HCU_V2_Simulink_Y.Set_Axis_State_1[i] =
      HCU_V2_Simulink_B.VectorConcatenate2[i];

    /* Outport: '<Root>/Torque_Right' */
    HCU_V2_Simulink_Y.Torque_Right[i] = HCU_V2_Simulink_B.VectorConcatenate1[i];

    /* Outport: '<Root>/Torque_Left' */
    HCU_V2_Simulink_Y.Torque_Left[i] = HCU_V2_Simulink_B.VectorConcatenate[i];
  }

  /* Outport: '<Root>/Set_Axis_State_1_req' incorporates:
   *  Inport: '<Root>/ODrive_1_Heartbeat'
   *  RelationalOperator: '<S6>/Equal1'
   */
  HCU_V2_Simulink_Y.Set_Axis_State_1_req =
    (HCU_V2_Simulink_U.ODrive_1_Heartbeat[4] != 0);

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
