#ifndef __CH58X_HAL_H
#define __CH58X_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_conf.h"
#include "ch58x_hal_def.h"

HAL_StatusTypeDef HAL_Init(void);
HAL_StatusTypeDef HAL_DeInit(void);
void HAL_MspInit(void);
void HAL_MspDeInit(void);
void HAL_Delay(uint32_t Delay);
void HAL_DelayUs(uint32_t Delay);

/* 中断优先级：按 ch58x_hal.c 的 s_irqPrio 表统一刷新一次 */

void HAL_NVIC_InitPriorities(void);

/* 系统节拍 */

HAL_StatusTypeDef HAL_InitTick(uint32_t priority);
void HAL_UseFreeRunningTick(void);
void HAL_SuspendTick(void);
void HAL_ResumeTick(void);
uint32_t HAL_GetTick(void);
void HAL_DelayTick(uint32_t Delay);

/* 仅 1ms 中断节拍模式下由 SysTick_Handler 调用；自由运行模式下不会触发 */
void HAL_IncTick(void);

#include "ch58x_hal_sys.h"
#include "ch58x_hal_rcc.h"
#include "ch58x_hal_gpio.h"
#include "ch58x_hal_flash.h"
#include "ch58x_hal_uart.h"
#include "ch58x_hal_i2c.h"
#include "ch58x_hal_tim.h"
#include "ch58x_hal_pwm.h"
#include "ch58x_hal_spi.h"
#include "ch58x_hal_lcd.h"
#include "ch58x_hal_adc.h"
#include "ch58x_hal_pwr.h"
#include "ch58x_hal_rtc.h"
#include "ch58x_hal_wdg.h"
#include "ch58x_hal_usbfs.h"
#include "ch58x_hal_usbfs_host.h"

/* CH584 没有 USB2，头文件里有 #error 守卫，所以这里也要守着 */
#if (CH58x_CHIP_MODEL != ID_CH584)
#include "ch58x_hal_usbhs.h"
#include "ch58x_hal_usbhs_host.h"
#endif

#ifdef CH58x_USE_BLE_LIB
#include "ch58x_hal_stack.h"
#include "ch58x_hal_snv.h"
#include "ch58x_hal_rf_phy.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_H */
