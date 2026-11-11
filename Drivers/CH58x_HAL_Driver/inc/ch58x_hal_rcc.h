#ifndef __CH58X_HAL_RCC_H
#define __CH58X_HAL_RCC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

HAL_StatusTypeDef HAL_RCC_InitClock(void);
HAL_StatusTypeDef HAL_RCC_SetSysClock(HAL_RCC_SysClkTypeDef clk);
uint32_t HAL_RCC_GetSysClockFreq(void);
uint32_t HAL_RCC_GetChipInfo(void);

void HAL_RCC_SetHSECapacitance(HAL_RCC_HSECapTypeDef cap);
void HAL_RCC_SetHSECurrent(HAL_RCC_HSECurrentTypeDef cur);
void HAL_RCC_SetLSECapacitance(HAL_RCC_LSECapTypeDef cap);
void HAL_RCC_SetLSECurrent(HAL_RCC_LSECurrentTypeDef cur);
void HAL_RCC_SetLSICurrent(HAL_RCC_LSICurrentTypeDef cur);

void HAL_RCC_CalibrateLSI(void);
void HAL_RCC_Config32KSource(void);

/* 32K 时钟的实际频率（Hz） */
#define HAL_RCC_OSC32K_FREQ (((HAL_RCC_OSC32K_SOURCE) == 1) ? 32000U : 32768U)

/* 校准算法内部使用的有符号形式 */
#define HAL_RCC_OSC32KFQ ((int32_t)HAL_RCC_OSC32K_FREQ)

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_RCC_H */
