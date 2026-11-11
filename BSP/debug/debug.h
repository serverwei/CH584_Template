#ifndef __BSP_DEBUG_H
#define __BSP_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

void BSP_DEBUG_Init(UART_HandleTypeDef *huart);
void BSP_DEBUG_DeInit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DEBUG_H */
