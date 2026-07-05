/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: HCU_V2_Simulink.h
 *
 * Code generated for Simulink model 'HCU_V2_Simulink'.
 *
 * Model version                  : 1.136
 * Simulink Coder version         : 25.2 (R2025b) 28-Jul-2025
 * C/C++ source code generated on : Sun Jul  5 10:07:11 2026
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
  real32_T Product_l;                  /* '<S9>/Product' */
  real32_T Product1_n;                 /* '<S9>/Product1' */
  uint8_T Switch_e;                    /* '<S6>/Switch' */
  uint8_T VectorConcatenate1[8];       /* '<S9>/Vector Concatenate1' */
  uint8_T VectorConcatenate[8];        /* '<S9>/Vector Concatenate' */
  uint8_T VectorConcatenate2[8];       /* '<S6>/Vector Concatenate2' */
  uint8_T VectorConcatenate1_e[8];     /* '<S8>/Vector Concatenate1' */
  uint8_T VectorConcatenate_i[8];      /* '<S8>/Vector Concatenate' */
} B_HCU_V2_Simulink_T;

/* Block states (default storage) for system '<Root>' */
typedef struct {
  real32_T Delay_DSTATE;               /* '<S46>/Delay' */
  uint16_T temporalCounter_i1;         /* '<S5>/Safety_Supervisor' */
  boolean_T UnitDelay_DSTATE;          /* '<S47>/Unit Delay' */
  uint8_T is_active_c3_HCU_V2_Simulink;/* '<S5>/Safety_Supervisor' */
  uint8_T is_c3_HCU_V2_Simulink;       /* '<S5>/Safety_Supervisor' */
  boolean_T Relay_Mode;                /* '<S37>/Relay' */
  boolean_T icLoad;                    /* '<S46>/Delay' */
} DW_HCU_V2_Simulink_T;

/* Constant parameters (default storage) */
typedef struct {
  /* Computed Parameter: Accel_Shape_tableData
   * Referenced by: '<S37>/Accel_Shape'
   */
  real32_T Accel_Shape_tableData[7];

  /* Computed Parameter: Accel_Shape_bp01Data
   * Referenced by: '<S37>/Accel_Shape'
   */
  real32_T Accel_Shape_bp01Data[7];

  /* Computed Parameter: Regen_Shape_tableData
   * Referenced by: '<S37>/Regen_Shape'
   */
  real32_T Regen_Shape_tableData[6];

  /* Computed Parameter: Regen_Shape_bp01Data
   * Referenced by: '<S37>/Regen_Shape'
   */
  real32_T Regen_Shape_bp01Data[6];
} ConstP_HCU_V2_Simulink_T;

/* External inputs (root inport signals with default storage) */
typedef struct {
  boolean_T bus1_ok;                   /* '<Root>/bus1_ok' */
  boolean_T bus2_ok;                   /* '<Root>/bus2_ok' */
  uint8_T APPS[8];                     /* '<Root>/APPS' */
  uint32_T APPS_age;                   /* '<Root>/APPS_age' */
  uint16_T BMS_Limits[8];              /* '<Root>/BMS_Limits' */
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
  uint8_T ODrive_0_Encoder_Estimate[8];/* '<Root>/ODrive_0_Encoder_Estimate' */
  uint32_T ODrive_0_Encoder_Estimate_age;
                                    /* '<Root>/ODrive_0_Encoder_Estimate_age' */
  uint8_T ODrive_1_Encoder_Estimate[8];/* '<Root>/ODrive_1_Encoder_Estimate' */
  uint32_T ODrive_1_Encoder_Estimate_age;
                                    /* '<Root>/ODrive_1_Encoder_Estimate_age' */
  real32_T Drive_Efficiency;           /* '<Root>/Drive_Efficiency' */
  real32_T BMS_Margin;                 /* '<Root>/BMS_Margin' */
  real32_T Motor_Torque_Max;           /* '<Root>/Motor_Torque_Max' */
  uint8_T Left_Direction;              /* '<Root>/Left_Direction' */
  uint8_T Right_Direction;             /* '<Root>/Right_Direction' */
  real32_T Regen_Efficiency;           /* '<Root>/Regen_Efficiency' */
  real32_T Motor_Regen_Max;            /* '<Root>/Motor_Regen_Max' */
  real32_T Regen_Cutoff_Speed;         /* '<Root>/Regen_Cutoff_Speed' */
  real32_T TV_Gain;                    /* '<Root>/TV_Gain' */
  real32_T Steering_Centre;            /* '<Root>/Steering_Centre' */
  real32_T Steering_Deadzone;          /* '<Root>/Steering_Deadzone' */
  real32_T Max_Torque_Split;           /* '<Root>/Max_Torque_Split' */
  uint8_T ECU_Misc[8];                 /* '<Root>/ECU_Misc' */
  uint32_T ECU_Misc_age;               /* '<Root>/ECU_Misc_age' */
  real32_T Vel_Scale;                  /* '<Root>/Vel_Scale' */
  real32_T Torque_Rate_Up;             /* '<Root>/Torque_Rate_Up' */
  real32_T Torque_Rate_Down;           /* '<Root>/Torque_Rate_Down' */
  real32_T Bench_Engine_Off;           /* '<Root>/Bench_Engine_Off' */
  boolean_T Start_Button_GUI;          /* '<Root>/Start_Button_GUI' */
  real_T Bench_Speed_Bypass;           /* '<Root>/Bench_Speed_Bypass' */
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
  real32_T Torque_Scale_Factor;        /* '<Root>/Torque_Scale_Factor' */
  real32_T Power_Demand;               /* '<Root>/Power_Demand' */
  real32_T Power_Budget;               /* '<Root>/Power_Budget' */
  real32_T Torque_Request_Left;        /* '<Root>/Torque_Request_Left' */
  real32_T Torque_Request_Right;       /* '<Root>/Torque_Request_Right' */
  boolean_T Inverter_Enable;           /* '<Root>/Inverter_Enable' */
  real32_T DCL;                        /* '<Root>/DCL' */
  real32_T CCL;                        /* '<Root>/CCL' */
  real32_T Bus_Voltage_0;              /* '<Root>/Bus_Voltage_0' */
  real32_T Bus_Voltage_1;              /* '<Root>/Bus_Voltage_1' */
  real32_T Bus_Current_0;              /* '<Root>/Bus_Current_0' */
  real32_T Bus_Current_1;              /* '<Root>/Bus_Current_1' */
  real32_T Pack_Voltage;               /* '<Root>/Pack_Voltage' */
  real32_T Velocity_1;                 /* '<Root>/Velocity_1' */
  real32_T Velocity_0;                 /* '<Root>/Velocity_0' */
  real32_T APPS_Clean;                 /* '<Root>/APPS_Clean' */
  real32_T Base_Torque_Demand;         /* '<Root>/Base_Torque_Demand' */
  real32_T Delta_Torque;               /* '<Root>/Delta_Torque ' */
  uint8_T State_Enum;                  /* '<Root>/State_Enum' */
  boolean_T APPS_Implausibility;       /* '<Root>/APPS_Implausibility' */
  boolean_T BMS_Fault;                 /* '<Root>/BMS_Fault' */
  boolean_T Engine_Synced;             /* '<Root>/Engine_Synced' */
  uint8_T Velocity_Left[8];            /* '<Root>/Velocity_Left' */
  uint8_T Velocity_Right[8];           /* '<Root>/Velocity_Right' */
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

/* Constant parameters (default storage) */
extern const ConstP_HCU_V2_Simulink_T HCU_V2_Simulink_ConstP;

/* Model entry point functions */
extern void HCU_V2_Simulink_initialize(void);
extern void HCU_V2_Simulink_step(void);
extern void HCU_V2_Simulink_terminate(void);

/* Real-time Model object */
extern RT_MODEL_HCU_V2_Simulink_T *const HCU_V2_Simulink_M;

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<S46>/FixPt Data Type Duplicate' : Unused code path elimination
 * Block '<S48>/Data Type Duplicate' : Unused code path elimination
 * Block '<S48>/Data Type Propagation' : Unused code path elimination
 * Block '<S54>/Data Type Duplicate' : Unused code path elimination
 * Block '<S54>/Data Type Propagation' : Unused code path elimination
 * Block '<S2>/Gain' : Eliminated nontunable gain of 1
 * Block '<S3>/Data Type Conversion' : Eliminate redundant data type conversion
 * Block '<S3>/Data Type Conversion1' : Eliminate redundant data type conversion
 * Block '<S3>/Data Type Conversion10' : Eliminate redundant data type conversion
 * Block '<S3>/Data Type Conversion11' : Eliminate redundant data type conversion
 * Block '<S3>/Data Type Conversion2' : Eliminate redundant data type conversion
 * Block '<S3>/Data Type Conversion3' : Eliminate redundant data type conversion
 * Block '<S3>/Data Type Conversion6' : Eliminate redundant data type conversion
 * Block '<S3>/Data Type Conversion7' : Eliminate redundant data type conversion
 * Block '<S3>/Data Type Conversion8' : Eliminate redundant data type conversion
 * Block '<S3>/Gain' : Eliminated nontunable gain of 1
 * Block '<S3>/Gain1' : Eliminated nontunable gain of 1
 * Block '<S46>/Zero-Order Hold' : Eliminated since input and output rates are identical
 * Block '<S55>/Data Type Conversion' : Eliminate redundant data type conversion
 * Block '<S56>/Data Type Conversion' : Eliminate redundant data type conversion
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
 * '<S3>'   : 'HCU_V2_Simulink/BMS_Limits_Decode'
 * '<S4>'   : 'HCU_V2_Simulink/Encoder_Velocity_Decode'
 * '<S5>'   : 'HCU_V2_Simulink/Logic'
 * '<S6>'   : 'HCU_V2_Simulink/ODrive_Axis_Control'
 * '<S7>'   : 'HCU_V2_Simulink/ODrive_VI_Decode'
 * '<S8>'   : 'HCU_V2_Simulink/Torque_Pack'
 * '<S9>'   : 'HCU_V2_Simulink/Velocity_Pack'
 * '<S10>'  : 'HCU_V2_Simulink/APPS_Decode/Bit Shift'
 * '<S11>'  : 'HCU_V2_Simulink/APPS_Decode/Bit Shift1'
 * '<S12>'  : 'HCU_V2_Simulink/APPS_Decode/Bit Shift2'
 * '<S13>'  : 'HCU_V2_Simulink/APPS_Decode/Compare To Constant'
 * '<S14>'  : 'HCU_V2_Simulink/APPS_Decode/Bit Shift/bit_shift'
 * '<S15>'  : 'HCU_V2_Simulink/APPS_Decode/Bit Shift1/bit_shift'
 * '<S16>'  : 'HCU_V2_Simulink/APPS_Decode/Bit Shift2/bit_shift'
 * '<S17>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift'
 * '<S18>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift1'
 * '<S19>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift2'
 * '<S20>'  : 'HCU_V2_Simulink/APPS_Decode1/Compare To Constant'
 * '<S21>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift/bit_shift'
 * '<S22>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift1/bit_shift'
 * '<S23>'  : 'HCU_V2_Simulink/APPS_Decode1/Bit Shift2/bit_shift'
 * '<S24>'  : 'HCU_V2_Simulink/BMS_Limits_Decode/Bit Shift'
 * '<S25>'  : 'HCU_V2_Simulink/BMS_Limits_Decode/Bit Shift1'
 * '<S26>'  : 'HCU_V2_Simulink/BMS_Limits_Decode/Bit Shift2'
 * '<S27>'  : 'HCU_V2_Simulink/BMS_Limits_Decode/Compare To Constant'
 * '<S28>'  : 'HCU_V2_Simulink/BMS_Limits_Decode/Bit Shift/bit_shift'
 * '<S29>'  : 'HCU_V2_Simulink/BMS_Limits_Decode/Bit Shift1/bit_shift'
 * '<S30>'  : 'HCU_V2_Simulink/BMS_Limits_Decode/Bit Shift2/bit_shift'
 * '<S31>'  : 'HCU_V2_Simulink/Encoder_Velocity_Decode/Compare To Constant'
 * '<S32>'  : 'HCU_V2_Simulink/Encoder_Velocity_Decode/Compare To Constant1'
 * '<S33>'  : 'HCU_V2_Simulink/Encoder_Velocity_Decode/MATLAB Function'
 * '<S34>'  : 'HCU_V2_Simulink/Encoder_Velocity_Decode/MATLAB Function1'
 * '<S35>'  : 'HCU_V2_Simulink/Logic/APPS_Processing'
 * '<S36>'  : 'HCU_V2_Simulink/Logic/BMS_Fault_Processing'
 * '<S37>'  : 'HCU_V2_Simulink/Logic/Base_Torque_Calculator'
 * '<S38>'  : 'HCU_V2_Simulink/Logic/Compare To Constant'
 * '<S39>'  : 'HCU_V2_Simulink/Logic/Safety_Supervisor'
 * '<S40>'  : 'HCU_V2_Simulink/Logic/Torque_Power_Limiter'
 * '<S41>'  : 'HCU_V2_Simulink/Logic/Torque_Vectoring'
 * '<S42>'  : 'HCU_V2_Simulink/Logic/APPS_Processing/Compare To Constant'
 * '<S43>'  : 'HCU_V2_Simulink/Logic/BMS_Fault_Processing/Compare To Constant'
 * '<S44>'  : 'HCU_V2_Simulink/Logic/BMS_Fault_Processing/Compare To Constant1'
 * '<S45>'  : 'HCU_V2_Simulink/Logic/Base_Torque_Calculator/Compare To Zero'
 * '<S46>'  : 'HCU_V2_Simulink/Logic/Base_Torque_Calculator/Rate Limiter Dynamic'
 * '<S47>'  : 'HCU_V2_Simulink/Logic/Base_Torque_Calculator/Virtual_BSPD'
 * '<S48>'  : 'HCU_V2_Simulink/Logic/Base_Torque_Calculator/Rate Limiter Dynamic/Saturation Dynamic'
 * '<S49>'  : 'HCU_V2_Simulink/Logic/Base_Torque_Calculator/Virtual_BSPD/Compare To Constant'
 * '<S50>'  : 'HCU_V2_Simulink/Logic/Base_Torque_Calculator/Virtual_BSPD/Compare To Constant1'
 * '<S51>'  : 'HCU_V2_Simulink/Logic/Base_Torque_Calculator/Virtual_BSPD/Compare To Constant2'
 * '<S52>'  : 'HCU_V2_Simulink/Logic/Torque_Vectoring/Compare To Zero'
 * '<S53>'  : 'HCU_V2_Simulink/Logic/Torque_Vectoring/Dead Zone Dynamic'
 * '<S54>'  : 'HCU_V2_Simulink/Logic/Torque_Vectoring/Saturation Dynamic'
 * '<S55>'  : 'HCU_V2_Simulink/ODrive_Axis_Control/Subsystem1'
 * '<S56>'  : 'HCU_V2_Simulink/ODrive_Axis_Control/Subsystem2'
 * '<S57>'  : 'HCU_V2_Simulink/ODrive_VI_Decode/Compare To Constant'
 * '<S58>'  : 'HCU_V2_Simulink/ODrive_VI_Decode/Compare To Constant1'
 * '<S59>'  : 'HCU_V2_Simulink/ODrive_VI_Decode/MATLAB Function'
 * '<S60>'  : 'HCU_V2_Simulink/ODrive_VI_Decode/MATLAB Function1'
 */
#endif                                 /* HCU_V2_Simulink_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
