#ifndef __BSP_SPI1_H
#define __BSP_SPI1_H

#ifdef __cplusplus
extern "C" {
#endif

/* 必须先 include 再判型号：CH58x_CHIP_MODEL 与 ID_CH584 都由 ch58x_hal.h 给出，
 * 若把 #if 写在 include 之前，两个宏都会被当成 0，(0 != 0) 为假，
 * 整段声明被静默跳过，调用方只会看到 "implicit declaration"。
 */
#include "ch58x_hal.h"
#include "spi.h"

#if (CH58x_CHIP_MODEL != ID_CH584)

extern SPI_HandleTypeDef hspi1;

HAL_StatusTypeDef BSP_SPI1_Init(const BSP_SPI_ConfigTypeDef *cfg);
void BSP_SPI1_DeInit(void);

void BSP_SPI1_CS_Select(void);
void BSP_SPI1_CS_Release(void);

HAL_StatusTypeDef BSP_SPI1_Transmit(const uint8_t *buf, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef BSP_SPI1_Receive(uint8_t *buf, uint16_t len, uint32_t timeout);

HAL_StatusTypeDef BSP_SPI1_Transmit_IT(const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef BSP_SPI1_Receive_IT(uint8_t *buf, uint16_t len);
HAL_StatusTypeDef BSP_SPI1_Abort_IT(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SPI1_H */
