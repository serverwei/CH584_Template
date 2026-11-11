#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

extern ADC_HandleTypeDef hadc;

/* ADC 是单实例外设，没有 adc0/adc1。
 *
 * 14 个外部通道全部落在 PA 口，而且和 SPI/I2C/UART/PWM/LCD 大面积重叠：
 *   AIN0=PA4  AIN1=PA5  AIN2=PA12 AIN3=PA13 AIN4=PA14 AIN5=PA15 AIN6=PA3
 *   AIN7=PA2  AIN8=PA1  AIN9=PA0  AIN10=PA6 AIN11=PA7 AIN12=PA8 AIN13=PA9
 * 所以只按 ChannelMask 逐位占用，没用到的脚保持原功能。
 *
 * ADC 引脚不需要专门的模拟使能位，配成浮空输入即可（与 LCD 不同）。
 */

typedef struct
{
    uint32_t ChannelMask; /* HAL_ADC_CH_MASK(HAL_ADC_CH_n) 拼起来 */
    HAL_ADC_ClockTypeDef Clock;
    HAL_ADC_GainTypeDef Gain;
    HAL_ADC_SampleTimeTypeDef SampleTime;
} BSP_ADC_ConfigTypeDef;

/* ChannelMask 写法示例：
 *   单通道    HAL_ADC_CH_MASK(HAL_ADC_CH_3)
 *   多通道    HAL_ADC_CH_MASK(HAL_ADC_CH_3) | HAL_ADC_CH_MASK(HAL_ADC_CH_5)
 *   全部 14 路 HAL_ADC_CH_MASK_ALL
 *   不占引脚  0（只用内部通道，例如只调 BSP_ADC_ReadTempRaw）
 */

HAL_StatusTypeDef BSP_ADC_Init(const BSP_ADC_ConfigTypeDef *cfg);
HAL_StatusTypeDef BSP_ADC_DeInit(void);

HAL_StatusTypeDef BSP_ADC_Read(uint8_t channel, uint16_t *value, uint32_t timeout);
HAL_StatusTypeDef BSP_ADC_SetGain(HAL_ADC_GainTypeDef gain);

/* DMA 连续采样：缓冲区由调用者提供，BSP 不持有。 */
HAL_StatusTypeDef BSP_ADC_SetDMAIntervalUs(uint32_t us);
HAL_StatusTypeDef BSP_ADC_StartDMA(uint16_t *buf, uint32_t len, HAL_ADC_DMALoopTypeDef loop);
HAL_StatusTypeDef BSP_ADC_StopDMA(void);
uint32_t BSP_ADC_GetDMACount(void);

void BSP_ADC_ConvCpltCallback(void);

/* 内部通道，自带现场保存，可在任意上下文调用 */
uint16_t BSP_ADC_ReadTempRaw(void);
uint16_t BSP_ADC_ReadVbatRaw(void);

/* 换算 */
int32_t BSP_ADC_TempToCelsius(uint16_t raw);
int32_t BSP_ADC_RawToMillivolt(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ADC_H */
