#ifndef __BSP_SPI0_H
#define __BSP_SPI0_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"
#include "spi.h"

extern SPI_HandleTypeDef hspi0;

HAL_StatusTypeDef BSP_SPI0_Init(const BSP_SPI_ConfigTypeDef *cfg);
void BSP_SPI0_DeInit(void);

void BSP_SPI0_CS_Select(void);
void BSP_SPI0_CS_Release(void);

HAL_StatusTypeDef BSP_SPI0_Transmit(const uint8_t *buf, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef BSP_SPI0_Receive(uint8_t *buf, uint16_t len, uint32_t timeout);

HAL_StatusTypeDef BSP_SPI0_Transmit_DMA(const uint8_t *buf, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef BSP_SPI0_Receive_DMA(uint8_t *buf, uint16_t len, uint32_t timeout);

HAL_StatusTypeDef BSP_SPI0_Transmit_IT(const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef BSP_SPI0_Receive_IT(uint8_t *buf, uint16_t len);
HAL_StatusTypeDef BSP_SPI0_Abort_IT(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SPI0_H */
