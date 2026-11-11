#ifndef __BSP_UART2_H
#define __BSP_UART2_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

extern UART_HandleTypeDef huart2;

HAL_StatusTypeDef BSP_UART2_Init(uint32_t baudrate, uint32_t mode, uint8_t pinGroup);
void BSP_UART2_DeInit(void);
HAL_StatusTypeDef BSP_UART2_Send(const uint8_t *buf, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef BSP_UART2_Receive_IT(uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_UART2_H */
