#ifndef __CH58X_HAL_PWR_H
#define __CH58X_HAL_PWR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"
#include "ch58x_hal_rtc.h"

/* 可控供电单元 */
#define HAL_PWR_UNIT_LSE RB_CLK_XT32K_PON
#define HAL_PWR_UNIT_LSI RB_CLK_INT32K_PON
#define HAL_PWR_UNIT_HSE RB_CLK_XT32M_PON
#define HAL_PWR_UNIT_PLL RB_CLK_PLL_PON

/* 外设时钟 / 唤醒 / 安全时钟位 */
#define HAL_PWR_SLPCLK_TMR0  0x00000001U
#define HAL_PWR_SLPCLK_TMR1  0x00000002U
#define HAL_PWR_SLPCLK_TMR2  0x00000004U
#define HAL_PWR_SLPCLK_TMR3  0x00000008U
#define HAL_PWR_SLPCLK_UART0 0x00000010U
#define HAL_PWR_SLPCLK_UART1 0x00000020U
#define HAL_PWR_SLPCLK_UART2 0x00000040U
#define HAL_PWR_SLPCLK_UART3 0x00000080U
#define HAL_PWR_SLPCLK_SPI0  0x00000100U
#define HAL_PWR_SLPCLK_SPI1  0x00000200U
#define HAL_PWR_SLPCLK_PWMX  0x00000400U
#define HAL_PWR_SLPCLK_I2C   0x00000800U
#define HAL_PWR_SLPCLK_USB   0x00001000U
#define HAL_PWR_SLPCLK_USB2  0x00002000U
#define HAL_PWR_SLPCLK_LCD   0x00004000U
#define HAL_PWR_SLPCLK_BLE   0x00008000U
#define HAL_PWR_SLPCLK_UTMI  0x04000000U
#define HAL_PWR_SLPCLK_RAMX  0x10000000U
#define HAL_PWR_SLPCLK_RAM2K 0x20000000U
#define HAL_PWR_SLPCLK_ALL   0x3000FFFFU

/* 休眠时保持供电的 RAM 区域 */
#define HAL_PWR_RAM_32K    RB_PWR_RAM32K
#define HAL_PWR_RAM_96K    RB_PWR_RAM96K
#define HAL_PWR_RAM_EXTEND RB_PWR_EXTEND

/* 睡眠唤醒源（R8_SLP_WAKE_CTRL） */
#define HAL_PWR_WAKE_USB       RB_SLP_USB_WAKE
#define HAL_PWR_WAKE_USB2      RB_SLP_USB2_WAKE
#define HAL_PWR_WAKE_GPIO_EDGE RB_GPIO_EDGE_WAKE
#define HAL_PWR_WAKE_RTC       RB_SLP_RTC_WAKE
#define HAL_PWR_WAKE_GPIO      RB_SLP_GPIO_WAKE
#define HAL_PWR_WAKE_BAT       RB_SLP_BAT_WAKE

/* 唤醒延时，对应 R8_SLP_POWER_CTRL.RB_WAKE_DLY_MOD。
 * 延时越长功耗越低，但唤醒后要等更久才能访问外设；
 * 芯片还支持 70 周期与 8 周期两档，HAL 暂未开放。
 */
typedef enum {
    HAL_PWR_WAKE_SHORT_DELAY = 0, /* 520 个时钟周期 + TSUHSE */
    HAL_PWR_WAKE_LONG_DELAY       /* 3590 个时钟周期 + TSUHSE */
} HAL_PWR_WakeUpModeTypeDef;

/* 电池电压监控等级。bit7 是模式标志，不是数值高位：
 *   0（H_ 段）：高精度检测，置 RB_BAT_DET_EN，仅在唤醒状态有效；
 *   1（LP_ 段）：低功耗监控，置 RB_BAT_MON_EN，可跨睡眠持续监控。
 * bit2 选 RB_BAT_LOW_VTHX，bit1:0 写入 R8_BAT_DET_CFG.RB_BAT_LOW_VTH，
 * 两段各自从 0 开始编号，所以数值看起来重复。
 */
typedef enum {
    HAL_PWR_VOL_H_1V9 = 0, /* 高精度，1.9V */
    HAL_PWR_VOL_H_2V1,     /* 高精度，2.1V */
    HAL_PWR_VOL_H_2V3,     /* 高精度，2.3V */
    HAL_PWR_VOL_H_2V5,     /* 高精度，2.5V */

    HAL_PWR_VOL_LP_1V7 = 0x80, /* 低功耗，1.7V */
    HAL_PWR_VOL_LP_1V8,        /* 低功耗，1.8V */
    HAL_PWR_VOL_LP_1V9,        /* 低功耗，1.9V */
    HAL_PWR_VOL_LP_2V0,        /* 低功耗，2.0V */
    HAL_PWR_VOL_LP_2V1,        /* 低功耗，2.1V */
    HAL_PWR_VOL_LP_2V2,        /* 低功耗，2.2V */
    HAL_PWR_VOL_LP_2V3,        /* 低功耗，2.3V */
    HAL_PWR_VOL_LP_2V4         /* 低功耗，2.4V */
} HAL_PWR_VoltageLevelTypeDef;

typedef void (*HAL_PWR_WakeGapCallbackTypeDef)(void);

/* 供电方式：ENABLE=内部 DC/DC，DISABLE=内部 LDO */

void HAL_PWR_ConfigDCDC(FunctionalState s);
void HAL_PWR_ConfigUnitPower(FunctionalState s, uint32_t unit);
void HAL_PWR_ConfigSafeClock(FunctionalState s, uint32_t periph);
void HAL_PWR_ConfigPeriphClock(FunctionalState s, uint32_t periph);
void HAL_PWR_ConfigPeriphWakeUp(FunctionalState s, uint32_t periph, HAL_PWR_WakeUpModeTypeDef mode);
void HAL_PWR_ConfigVoltageMonitor(FunctionalState s, HAL_PWR_VoltageLevelTypeDef level);

void HAL_PWR_EnterIdleMode(void);
void HAL_PWR_EnterHaltMode(void);
void HAL_PWR_EnterHaltWFEMode(void);
void HAL_PWR_EnterSleepMode(uint16_t rm);
void HAL_PWR_EnterSleepWFEMode(uint16_t rm);
void HAL_PWR_EnterShutdownMode(uint16_t rm);

void HAL_PWR_EnableRtcWakeUp(void);
uint32_t HAL_PWR_EnterSleepUntil(uint32_t wakeTime);
void HAL_PWR_RegisterWakeGapCallback(HAL_PWR_WakeGapCallbackTypeDef cb);

/* 微秒转 32K 计数，四舍五入 */
#define HAL_PWR_US_TO_RTC(us) HAL_RTC_US_TO_COUNT(us)

#ifndef HAL_PWR_SLEEP_RTC_MIN_TIME
#define HAL_PWR_SLEEP_RTC_MIN_TIME HAL_PWR_US_TO_RTC(1000)
#endif
#ifndef HAL_PWR_SLEEP_RTC_MAX_TIME
#define HAL_PWR_SLEEP_RTC_MAX_TIME (HAL_RTC_MAX_COUNT - HAL_PWR_US_TO_RTC(1000U * 1000U * 30U))
#endif
#ifndef HAL_PWR_WAKEUP_RTC_MAX_TIME
#define HAL_PWR_WAKEUP_RTC_MAX_TIME HAL_PWR_US_TO_RTC(1600)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_PWR_H */
