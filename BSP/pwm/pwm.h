#ifndef __BSP_PWM_H
#define __BSP_PWM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

extern PWM_HandleTypeDef hpwm;

HAL_StatusTypeDef BSP_PWM_Init(HAL_PWM_CycleTypeDef cycle, uint8_t clockDiv,
                               uint8_t channels, uint8_t pinGroup);
HAL_StatusTypeDef BSP_PWM_DeInit(void);

void BSP_PWM_EnableCycleIT(HAL_PWM_CycPointTypeDef pt);
void BSP_PWM_DisableCycleIT(void);

void BSP_PWM_CycleElapsedCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_PWM_H */
