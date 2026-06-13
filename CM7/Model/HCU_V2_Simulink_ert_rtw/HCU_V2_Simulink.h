/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: HCU_V2_Simulink.h
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

#ifndef HCU_V2_Simulink_h_
#define HCU_V2_Simulink_h_
#ifndef HCU_V2_Simulink_COMMON_INCLUDES_
#define HCU_V2_Simulink_COMMON_INCLUDES_
#include "rtwtypes.h"
#include "math.h"
#endif                                 /* HCU_V2_Simulink_COMMON_INCLUDES_ */

#include "HCU_V2_Simulink_types.h"

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm)         ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val)    ((rtm)->errorStatus = (val))
#endif

/* External inputs (root inport signals with default storage) */
typedef struct {
  boolean_T bus1_ok;                   /* '<Root>/bus1_ok' */
  boolean_T bus2_ok;                   /* '<Root>/bus2_ok' */
  uint8_T test[8];                     /* '<Root>/test' */
  uint32_T test_age;                   /* '<Root>/test_age' */
} ExtU_HCU_V2_Simulink_T;

/* External outputs (root outports fed by signals with default storage) */
typedef struct {
  boolean_T User_LED_1;                /* '<Root>/User_LED_1' */
  boolean_T User_LED_2;                /* '<Root>/User_LED_2' */
  boolean_T User_LED_3;                /* '<Root>/User_LED_3' */
} ExtY_HCU_V2_Simulink_T;

/* Real-time Model Data Structure */
struct tag_RTM_HCU_V2_Simulink_T {
  const char_T * volatile errorStatus;
};

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
 * '<S1>'   : 'HCU_V2_Simulink/Compare To Constant'
 */
#endif                                 /* HCU_V2_Simulink_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
