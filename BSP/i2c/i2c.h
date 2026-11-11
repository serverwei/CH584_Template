#ifndef __BSP_I2C_H
#define __BSP_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

extern I2C_HandleTypeDef hi2c1;

HAL_StatusTypeDef BSP_I2C_Init(uint32_t clockSpeed, uint8_t pinGroup, HAL_I2C_BusModeTypeDef busMode);
void BSP_I2C_DeInit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_I2C_H */
