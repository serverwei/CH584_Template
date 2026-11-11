#ifndef __BSP_TIM1_H
#define __BSP_TIM1_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

extern TIM_HandleTypeDef htim1;

HAL_StatusTypeDef BSP_TIM1_Init(uint32_t period);
HAL_StatusTypeDef BSP_TIM1_InitPWM(uint32_t period, HAL_TIM_PWMPolarTypeDef pol,
                                   uint8_t pinGroup);
HAL_StatusTypeDef BSP_TIM1_DeInit(void);
HAL_StatusTypeDef BSP_TIM1_Start(void);
HAL_StatusTypeDef BSP_TIM1_Start_IT(void);
HAL_StatusTypeDef BSP_TIM1_Stop(void);
HAL_StatusTypeDef BSP_TIM1_Stop_IT(void);

HAL_StatusTypeDef BSP_TIM1_InitCapture(HAL_TIM_CapEdgeTypeDef edge, uint32_t timeout,
                                       uint8_t pinGroup, HAL_TIM_PinModeTypeDef pinMode);
HAL_StatusTypeDef BSP_TIM1_InitCounter(HAL_TIM_CapEdgeTypeDef edge, uint32_t overflow,
                                       uint8_t pinGroup, HAL_TIM_PinModeTypeDef pinMode);
HAL_StatusTypeDef BSP_TIM1_CaptureStart_IT(void);
HAL_StatusTypeDef BSP_TIM1_CaptureStop_IT(void);

HAL_StatusTypeDef BSP_TIM1_PWMStart_DMA(const uint32_t *pulse, uint32_t len,
                                        HAL_TIM_DMALoopTypeDef loop);
HAL_StatusTypeDef BSP_TIM1_CaptureStart_DMA(uint32_t *buf, uint32_t len,
                                            HAL_TIM_DMALoopTypeDef loop);
HAL_StatusTypeDef BSP_TIM1_StopDMA(void);
uint32_t BSP_TIM1_GetDMACount(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TIM1_H */
