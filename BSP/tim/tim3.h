#ifndef __BSP_TIM3_H
#define __BSP_TIM3_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

extern TIM_HandleTypeDef htim3;

HAL_StatusTypeDef BSP_TIM3_Init(uint32_t period);
HAL_StatusTypeDef BSP_TIM3_InitPWM(uint32_t period, HAL_TIM_PWMPolarTypeDef pol,
                                   uint8_t pinGroup);
HAL_StatusTypeDef BSP_TIM3_DeInit(void);
HAL_StatusTypeDef BSP_TIM3_Start(void);
HAL_StatusTypeDef BSP_TIM3_Start_IT(void);
HAL_StatusTypeDef BSP_TIM3_Stop(void);
HAL_StatusTypeDef BSP_TIM3_Stop_IT(void);

HAL_StatusTypeDef BSP_TIM3_InitCapture(HAL_TIM_CapEdgeTypeDef edge, uint32_t timeout,
                                       uint8_t pinGroup, HAL_TIM_PinModeTypeDef pinMode);
HAL_StatusTypeDef BSP_TIM3_InitCounter(HAL_TIM_CapEdgeTypeDef edge, uint32_t overflow,
                                       uint8_t pinGroup, HAL_TIM_PinModeTypeDef pinMode);
HAL_StatusTypeDef BSP_TIM3_CaptureStart_IT(void);
HAL_StatusTypeDef BSP_TIM3_CaptureStop_IT(void);

HAL_StatusTypeDef BSP_TIM3_PWMStart_DMA(const uint32_t *pulse, uint32_t len,
                                        HAL_TIM_DMALoopTypeDef loop);
HAL_StatusTypeDef BSP_TIM3_CaptureStart_DMA(uint32_t *buf, uint32_t len,
                                            HAL_TIM_DMALoopTypeDef loop);
HAL_StatusTypeDef BSP_TIM3_StopDMA(void);
uint32_t BSP_TIM3_GetDMACount(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TIM3_H */
