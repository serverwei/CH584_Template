#ifndef __CH58X_HAL_CONF_H
#define __CH58X_HAL_CONF_H

#include "ch58x_hal_clk_def.h"

/* ===================== 芯片型号 =====================
 *   ID_CH585 = 0x93   CH585，SPI0 + SPI1，RAM 128K
 *   ID_CH584 = 0x92   CH584，仅 SPI0，RAM 96K，CH584X DataFlash 以 4K 为单位擦除
 *   ID_CH585C = 0x90  CH585C
 */
#ifndef ID_CH585
#define ID_CH585 0x93
#endif
#ifndef ID_CH584
#define ID_CH584 0x92
#endif
#ifndef ID_CH585C
#define ID_CH585C 0x90
#endif

#ifndef CH58x_CHIP_MODEL
#define CH58x_CHIP_MODEL ID_CH584
#endif

#if (CH58x_CHIP_MODEL != ID_CH585) && (CH58x_CHIP_MODEL != ID_CH584) && (CH58x_CHIP_MODEL != ID_CH585C)
#error "CH58x_CHIP_MODEL 取值非法：只能是 ID_CH585 / ID_CH584 / ID_CH585C"
#endif

/* ===================== Protocol 层编译开关 =====================
 * 定义：编译 Protocol/ 下的 BLE / RF / SNV 代码。
 * 注释掉：这些文件整体被 #ifdef 关成空，等同排除出工程。
 * 不受本宏控制：ble_task_scheduler.S（.highcode 里几百字节死代码）、
 *              SysTick 模式（运行期由 HAL_UseFreeRunningTick() 决定）。
 */
#define CH58x_USE_BLE_LIB

/* SysTick 中断优先级：0 = 最高（不可抢占），非 0 = 低优先级（可被抢占） */
#ifndef HAL_TICK_PRIORITY
#define HAL_TICK_PRIORITY 1U
#endif

/* 32K 时钟源（R8_CK32K_CONFIG.RB_CLK_OSC32K_XT）
 * 0 = 外部晶振 32768Hz
 * 1 = 内部 RC 32000Hz
 */
#ifndef HAL_RCC_OSC32K_SOURCE
#define HAL_RCC_OSC32K_SOURCE 1
#endif

/* 系统时钟源（HAL_RCC_SysClkTypeDef） */
#ifndef HAL_RCC_SYSCLK_SOURCE
#define HAL_RCC_SYSCLK_SOURCE HAL_RCC_CLK_HSE_32M
#endif

/* 系统时钟频率（Hz），必须与 HAL_RCC_SYSCLK_SOURCE 一致 */
#ifndef HAL_RCC_SYSCLK_HZ
#define HAL_RCC_SYSCLK_HZ 32000000U
#endif

/* HSE（32MHz 晶振） */
#ifndef HAL_RCC_HSE_CAPACITANCE
#define HAL_RCC_HSE_CAPACITANCE HAL_RCC_HSE_CAP_12P
#endif
#ifndef HAL_RCC_HSE_CURRENT
#define HAL_RCC_HSE_CURRENT HAL_RCC_HSE_RCUR_100
#endif

/* LSE（32.768kHz 晶振） */
#ifndef HAL_RCC_LSE_CAPACITANCE
#define HAL_RCC_LSE_CAPACITANCE HAL_RCC_LSE_CAP_12P
#endif
#ifndef HAL_RCC_LSE_CURRENT
#define HAL_RCC_LSE_CURRENT HAL_RCC_LSE_RCUR_100
#endif

/* LSI（内部 32K RC） */
#ifndef HAL_RCC_LSI_CURRENT
#define HAL_RCC_LSI_CURRENT HAL_RCC_LSI_RCUR_100
#endif

/* #define USE_FULL_ASSERT */

#endif /* __CH58X_HAL_CONF_H */
