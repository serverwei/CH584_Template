#ifndef __CH58X_HAL_RTC_H
#define __CH58X_HAL_RTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"
#include "ch58x_hal_rcc.h"

/* 32K 周期计数上限，约 24 小时 */
#define HAL_RTC_MAX_COUNT 0xA8C00000U

/* 日历基准年，最大可用到 +44 年 */
#define HAL_RTC_BASE_YEAR 2020U

/* 时间换算：结果是 32K 周期数，按 HAL_RCC_OSC32K_SOURCE 选定的实际频率 */
#define HAL_RTC_US_TO_COUNT(us) ((uint32_t)(((uint64_t)(us) * HAL_RCC_OSC32K_FREQ + 500000U) / 1000000U))
#define HAL_RTC_MS_TO_COUNT(ms) ((uint32_t)(((uint64_t)(ms) * HAL_RCC_OSC32K_FREQ + 500U) / 1000U))
#define HAL_RTC_SEC_TO_COUNT(s) ((uint32_t)((uint64_t)(s) * HAL_RCC_OSC32K_FREQ))

typedef struct
{
    uint16_t year;  /* HAL_RTC_BASE_YEAR 起 */
    uint8_t month;  /* 1 ~ 12 */
    uint8_t day;    /* 1 ~ 31 */
    uint8_t hour;   /* 0 ~ 23 */
    uint8_t minute; /* 0 ~ 59 */
    uint8_t second; /* 0 ~ 59 */
} HAL_RTC_TimeTypeDef;

/* RTC 事件标志
 * TRIGGER：触发闹钟，对应 R8_RTC_FLAG_CTRL.RB_RTC_TRIG_FLAG，也是 RTC_IRQ 的来源；
 * PERIOD ：硬件周期模式标志 RB_RTC_TMR_FLAG。
 * 注意 HAL_RTC_SetPeriod 是用“触发重复装载”实现的，不走硬件 TMR 模式，
 * 所以用周期定时时请判 TRIGGER 标志，PERIOD 仅在手动开 RB_RTC_TMR_EN 时才有意义。
 */
typedef enum {
    HAL_RTC_EVENT_TRIGGER = 0, /* 触发事件 */
    HAL_RTC_EVENT_PERIOD       /* 硬件周期定时事件 */
} HAL_RTC_EventTypeDef;

/* 周期定时：由触发模式重复装载实现，周期按实际 32K 频率换算 */
typedef enum {
    HAL_RTC_PERIOD_125MS = 0,
    HAL_RTC_PERIOD_250MS,
    HAL_RTC_PERIOD_500MS,
    HAL_RTC_PERIOD_1S,
    HAL_RTC_PERIOD_2S,
    HAL_RTC_PERIOD_4S,
    HAL_RTC_PERIOD_8S,
    HAL_RTC_PERIOD_16S
} HAL_RTC_PeriodTypeDef;

HAL_StatusTypeDef HAL_RTC_Init(void);
HAL_StatusTypeDef HAL_RTC_DeInit(void);

HAL_StatusTypeDef HAL_RTC_SetTime(const HAL_RTC_TimeTypeDef *time);
HAL_StatusTypeDef HAL_RTC_GetTime(HAL_RTC_TimeTypeDef *time);

uint32_t HAL_RTC_GetCounter(void);
void HAL_RTC_SetCounter(uint32_t counter);

void HAL_RTC_SetTrigger(uint32_t trigger);
HAL_StatusTypeDef HAL_RTC_SetTriggerAfter(uint32_t cycles);
void HAL_RTC_SetPeriod(HAL_RTC_PeriodTypeDef period);
void HAL_RTC_DisableTrigger(void);
void HAL_RTC_DisablePeriod(void);

uint8_t HAL_RTC_GetEventFlag(HAL_RTC_EventTypeDef event);
void HAL_RTC_ClearEventFlag(HAL_RTC_EventTypeDef event);

/* 需在 RTC_IRQHandler 中调用 */

void HAL_RTC_IRQHandler(void);

/* 触发事件回调，默认空实现 */

void HAL_RTC_TriggerCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_RTC_H */
