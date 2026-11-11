#ifndef __BSP_LCD_H
#define __BSP_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

extern LCD_HandleTypeDef hlcd;

/* 段式 LCD 是单实例外设，没有 lcd0/lcd1。
 *
 * 引脚占用（手册表 1-3）：
 *   COM0~COM3  = PB4~PB7
 *   SEG0~SEG3  = PB0~PB3      SEG4~SEG7   = PA0~PA3
 *   SEG8~SEG15 = PB8~PB15     SEG16~SEG23 = PB16~PB23
 *   SEG24~SEG26= PA7~PA9      SEG27       = PA13
 * 这些脚与 UART/SPI/I2C/PWM/USB 大面积重叠，所以只按 SegMask 逐位占用，
 * 没用到的脚保持原功能。
 *
 * 两条硬件限制代码解决不了：
 *   SEG2 = PB2 默认是外部复位 RST#，要用必须烧录时关闭外部复位（选项字节）
 *   调试口占着 PB14/PB15，开了 SWD 就不能当 SEG14/SEG15
 */

typedef struct
{
    HAL_LCD_DriveTypeDef Drive;
    HAL_LCD_DutyTypeDef Duty;
    HAL_LCD_BiasTypeDef Bias;
    HAL_LCD_ScanTypeDef Scan;
    uint32_t SegMask; /* bit n = 使用 SEGn，填 0 表示用满 28 段 */
} BSP_LCD_ConfigTypeDef;

HAL_StatusTypeDef BSP_LCD_Init(const BSP_LCD_ConfigTypeDef *cfg);
HAL_StatusTypeDef BSP_LCD_DeInit(void);

void BSP_LCD_WriteSeg(uint8_t seg, uint8_t comMask);
void BSP_LCD_WriteBuffer(uint8_t index, const uint8_t *buf, uint8_t count);
void BSP_LCD_Clear(void);
void BSP_LCD_Refresh(void);

void BSP_LCD_SetClockOff(FunctionalState off);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LCD_H */
