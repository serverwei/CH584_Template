#ifndef __BSP_UART_H
#define __BSP_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

void BSP_UART0_RxCpltCallback(void);
void BSP_UART1_RxCpltCallback(void);
void BSP_UART2_RxCpltCallback(void);
void BSP_UART3_RxCpltCallback(void);

void BSP_UART0_ErrorCallback(uint8_t lsr);
void BSP_UART1_ErrorCallback(uint8_t lsr);
void BSP_UART2_ErrorCallback(uint8_t lsr);
void BSP_UART3_ErrorCallback(uint8_t lsr);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_UART_H */
