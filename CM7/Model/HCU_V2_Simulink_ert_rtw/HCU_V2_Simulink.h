/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: HCU_V2_Simulink.h
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

#ifndef HCU_V2_Simulink_h_
#define HCU_V2_Simulink_h_
#ifndef HCU_V2_Simulink_COMMON_INCLUDES_
#define HCU_V2_Simulink_COMMON_INCLUDES_
#include "rtwtypes.h"
#include "math.h"
#endif                                 /* HCU_V2_Simulink_COMMON_INCLUDES_ */

#include "HCU_V2_Simulink_types.h"
#include <string.h>

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm)         ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val)    ((rtm)->errorStatus = (val))
#endif

/* Block signals (default storage) */
typedef struct {
  real32_T Switch_j;                   /* '<S3>/Switch' */
  real32_T Gain;                       /* '<S3>/Gain' */
  uint8_T Switch_e;                    /* '<S6>/Switch' */
  uint8_T VectorConcatenate2[8];       /* '<S6>/Vector Concatenate2' */
  uint8_T VectorConcatenate1[8];       /* '<S5>/Vector Concatenate1' */
  uint8_T VectorConcatenate[8];        /* '<S5>/Vector Concatenate' */
  boolean_T Inverter_Enable;           /* '<S3>/Safety_Supervisor' */
} B_HCU_V2_Simulink_T;

/* Block states (default storage) for system '<Root>' */
typedef struct {
  uint16_T temporalCounter_i1;         /* '<S3>/Safety_Supervisor' */
  uint8_T is_active_c3_HCU_V2_Simulink;/* '<S3>/Safety_Supervisor' */
  uint8_T is_c3_HCU_V2_Simulink;       /* '<S3>/Safety_Supervisor' */
} DW_HCU_V2_Simulink_T;

/* External inputs (root inport signals with default storage) */
typedef struct {
  boolean_T bus1_ok;                   /* '<Root>/bus1_ok' */
  boolean_T bus2_ok;                   /* '<Root>/bus2_ok' */
  uint8_T APPS[8];                     /* '<Root>/APPS' */
  uint32_T APPS_age;                   /* '<Root>/APPS_age' */
  uint8_T BMS_Limits[8];               /* '<Root>/BMS_Limits' */
  uint32_T BMS_Limits_age;             /* '<Root>/BMS_Limits_age' */
  uint8_T ODrive_0_VI[8];              /* '<Root>/ODrive_0_VI' */
  uint8_T ODrive_1_VI[8];              /* '<Root>/ODrive_1_VI' */
  uint32_T ODrive_0_VI_age;            /* '<Root>/ODrive_0_VI_age' */
  uint32_T ODrive_1_VI_age;            /* '<Root>/ODrive_1_VI_age' */
  boolean_T SDC_Monitor;               /* '<Root>/SDC_Monitor' */
  boolean_T Start_Button;              /* '<Root>/Start_Button' */
  uint8_T ODrive_0_Heartbeat[8];       /* '<Root>/ODrive_0_Heartbeat' */
  uint8_T ODrive_1_Heartbeat[8];       /* '<Root>/ODrive_1_Heartbeat' */
  uint32_T ODrive_0_Heartbeat_age;     /* '<Root>/ODrive_0_Heartbeat_age' */
  uint32_T ODrive_1_Heartbeat_age;     /* '<Root>/ODrive_1_Heartbeat_age' */
} ExtU_HCU_V2_Simulink_T;

/* External outputs (root outports fed by signals with default storage) */
typedef struct {
  boolean_T User_LED_1;                /* '<Root>/User_LED_1' */
  boolean_T User_LED_2;                /* '<Root>/User_LED_2' */
  uint8_T Torque_Left[8];              /* '<Root>/Torque_Left' */
  uint8_T Torque_Right[8];             /* '<Root>/Torque_Right' */
  boolean_T Set_Axis_State_0_req;      /* '<Root>/Set_Axis_State_0_req' */
  boolean_T Set_Axis_State_1_req;      /* '<Root>/Set_Axis_State_1_req' */
  uint8_T Set_Axis_State_0[8];         /* '<Root>/Set_Axis_State_0' */
  uint8_T Set_Axis_State_1[8];         /* '<Root>/Set_Axis_State_1' */
  boolean_T AIR_Enable;                /* '<Root>/AIR_Enable' */
  boolean_T Pre_Charge_Enable;         /* '<Root>/Pre_Charge_Enable' */
} ExtY_HCU_V2_Simulink_T;

/* Real-time Model Data Structure */
struct tag_RTM_HCU_V2_Simulink_T {
  const char_T * volatile errorStatus;
};

/* Block signals (default storage) */
extern B_HCU_V2_Simulink_T HCU_V2_Simulink_B;

/* Block states (default storage) */
extern DW_HCU_V2_Simulink_T HCU_V2_Simulink_DW;

/* External inputs (root inport signals with default storage) */
extern ExtU_HCU_V2_Simulink_T HCU_V2_Simulink_U;

/* External outputs (root outports fed by signals with default storage) */
extern ExtY_HCU_V2_Simulink_T HCU_V2_Simulink_Y;

/* Model entry point functions */
extern void HCU_V2_Simulink_initialize(void);
extern void HCU_V2_Simulink_step(void);
extern void HCU_V2_Simulink_terminate(void);

/* Real-time Model object */
extern RT_MODEL_HCU_V2_Simulink_T *const HCU_V2_Simulink_M;

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<S4>/Switch1' : Unused code path elimination
 * Block '<S4>/Switch3' : Unused code path elimination
 * Block '<S2>/Data Type Conversion11' : Eliminate redundant data type conversion
 * Block '<S2>/Data Type Conversion2' : Eliminate redundant data type conversion
 * Block '<S2>/Data Type Conversion7' : Eliminate redundant data type conversion
 * Block '<S2>/Data Type Conversion8' : Eliminate redundant data type conversion
 * Block '<S2>/Gain' : Eliminated nontunable gain of 1
 * Block '<S2>/Gain1' : Eliminated nontunable gain of 1
 * Block '<S2>/Gain2' : Eliminated nontunable gain of 1
 * Block '<S3>/Gain1' : Eliminated nontunable gain of 1
 * Block '<S27>/Data Type Conversion' : Eliminate redundant data type conversion
 * Block '<S28>/Data Type Conversion' : Eliminate redundant data type conversion
 */

/*-
 * The generated code includes comments that allow you to trace directly
 * back to the appropriate location in the model.  The basic format
 * is <system>/block_name, where system is the system number (uniquely
 * assigned by Simulink) and block_name is the name of the block.
 *
 * Use the MATLAB hilite_system command to trace the generated code back
 * to the model.  For example,
 *
 * hilite_system('<S3>')    - opens system 3
 * hilite_system('<S3>/Kp') - opens and selects block Kp which resides in S3
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'HCU_V2_Simulink'
 * '<S1>'   : 'HCU_V2_Simulink/APPS_Decode'
 * '<S2>'   : 'HCU_V2_Simulink/APPS_Decode1'
 * '<S3>'   : 'HCU_V2_Simulink/Logic'
 * '<S4>'   : 'HCU_V2_Simulink/Subsystem'
 * '<S5>'   : 'HCU_V2_Simulink/Subsystem3'
 * '<S6>'   : 'HCU_V2_Simulink/Subsystem4'
 * '<S7>'   : 'HCU_V2_Simulink/APPS_Decode/Bit Shift'
 * '<S8>'   : 'HCU_V2_Simulink/APPS_Decode/Bit Shift1'
 * '<S9>'   : 'HCU_V2_Simulink/APPS_Decode/Compare To Constant'
 * '<S10>'  : 'HCU_V2_Simulink/APPS_Decode/Bit Shift/bit_shift'
 * '<S11>'  : 'HCU_V2_Simulink/APPS_Decode/Bit Shift1/bit_shift'
 * '<S12>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift'
 * '<S13>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift1'
 * '<S14>'  : 'HCU_V2_Simulink/APPS_Decode1/Compare To Constant'
 * '<S15>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift/bit_shift'
 * '<S16>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift1/bit_shift'
 * '<S17>'  : 'HCU_V2_Simulink/Logic/APPS_Processing'
 * '<S18>'  : 'HCU_V2_Simulink/Logic/BMS_Fault_Processing'
 * '<S19>'  : 'HCU_V2_Simulink/Logic/Safety_Supervisor'
 * '<S20>'  : 'HCU_V2_Simulink/Logic/APPS_Processing/Compare To Constant'
 * '<S21>'  : 'HCU_V2_Simulink/Logic/BMS_Fault_Processing/Compare To Constant'
 * '<S22>'  : 'HCU_V2_Simulink/Logic/BMS_Fault_Processing/Compare To Constant1'
 * '<S23>'  : 'HCU_V2_Simulink/Subsystem/Compare To Constant'
 * '<S24>'  : 'HCU_V2_Simulink/Subsystem/Compare To Constant1'
 * '<S25>'  : 'HCU_V2_Simulink/Subsystem/MATLAB Function'
 * '<S26>'  : 'HCU_V2_Simulink/Subsystem/MATLAB Function1'
 * '<S27>'  : 'HCU_V2_Simulink/Subsystem4/Subsystem1'
 * '<S28>'  : 'HCU_V2_Simulink/Subsystem4/Subsystem2'
 */
#endif                                 /* HCU_V2_Simulink_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
