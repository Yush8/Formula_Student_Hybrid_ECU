/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SDC_Monitor_Pin GPIO_PIN_2
#define SDC_Monitor_GPIO_Port GPIOE
#define AIR_LED_Pin GPIO_PIN_3
#define AIR_LED_GPIO_Port GPIOE
#define Precharge_LED_Pin GPIO_PIN_4
#define Precharge_LED_GPIO_Port GPIOE
#define SDC_Monitor_LED_Pin GPIO_PIN_5
#define SDC_Monitor_LED_GPIO_Port GPIOE
#define Thermistor_Pin GPIO_PIN_0
#define Thermistor_GPIO_Port GPIOC
#define Watchdog_Pin GPIO_PIN_3
#define Watchdog_GPIO_Port GPIOA
#define MCU_Active_Pin GPIO_PIN_7
#define MCU_Active_GPIO_Port GPIOE
#define External_Signal_1_Pin GPIO_PIN_10
#define External_Signal_1_GPIO_Port GPIOE
#define External_Signal_2_Pin GPIO_PIN_11
#define External_Signal_2_GPIO_Port GPIOE
#define External_Signal_3_Pin GPIO_PIN_12
#define External_Signal_3_GPIO_Port GPIOE
#define External_Signal_4_Pin GPIO_PIN_13
#define External_Signal_4_GPIO_Port GPIOE
#define User_LED_1_Pin GPIO_PIN_8
#define User_LED_1_GPIO_Port GPIOD
#define User_LED_2_Pin GPIO_PIN_9
#define User_LED_2_GPIO_Port GPIOD
#define User_LED_3_Pin GPIO_PIN_10
#define User_LED_3_GPIO_Port GPIOD
#define User_LED_4_Pin GPIO_PIN_11
#define User_LED_4_GPIO_Port GPIOD
#define User_Button_1_Pin GPIO_PIN_12
#define User_Button_1_GPIO_Port GPIOD
#define User_Button_2_Pin GPIO_PIN_13
#define User_Button_2_GPIO_Port GPIOD
#define USB_VBUS_DET_Pin GPIO_PIN_9
#define USB_VBUS_DET_GPIO_Port GPIOA
#define IMU_CLK_Pin GPIO_PIN_10
#define IMU_CLK_GPIO_Port GPIOC
#define IMU_MISO_Pin GPIO_PIN_11
#define IMU_MISO_GPIO_Port GPIOC
#define IMU_MOSI_Pin GPIO_PIN_12
#define IMU_MOSI_GPIO_Port GPIOC
#define IMU_Int_Pin GPIO_PIN_2
#define IMU_Int_GPIO_Port GPIOD
#define IMU_CS_Pin GPIO_PIN_3
#define IMU_CS_GPIO_Port GPIOD
#define AIR_Sink_Pin GPIO_PIN_0
#define AIR_Sink_GPIO_Port GPIOE
#define Precharge_Sink_Pin GPIO_PIN_1
#define Precharge_Sink_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
