#ifndef __CH58X_HAL_WDG_H
#define __CH58X_HAL_WDG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"
#include "ch58x_hal_rcc.h"

/* ===================== WWDG：8 位看门狗 =====================
 * 计数时钟 = Fsys / 131072；32MHz 下约 4.1ms 一个计数，满量程约 1.04s
 */
#define HAL_WWDG_TICKS_PER_SEC (HAL_RCC_SYSCLK_HZ / 131072U)

void HAL_WWDG_SetCounter(uint8_t counter);
uint8_t HAL_WWDG_GetCounter(void);
void HAL_WWDG_EnableReset(FunctionalState s);
void HAL_WWDG_EnableIT(FunctionalState s);
uint8_t HAL_WWDG_GetFlag(void);
void HAL_WWDG_ClearFlag(void);

/* ===================== IWDG：12 位独立看门狗 =====================
 * 计数时钟 = 32K / 分频；超时 = (reload + 1) * 分频 / 32K
 */
typedef enum {
    HAL_IWDG_PR_4 = 0,
    HAL_IWDG_PR_8,
    HAL_IWDG_PR_16,
    HAL_IWDG_PR_32,
    HAL_IWDG_PR_64,
    HAL_IWDG_PR_128,
    HAL_IWDG_PR_256,
    HAL_IWDG_PR_512
} HAL_IWDG_PrescalerTypeDef;

#define HAL_IWDG_TIMEOUT_MS(pr, reload) \
    ((uint32_t)(((uint64_t)((reload) + 1U) * (1UL << ((uint32_t)(pr) + 2U)) * 1000U) / HAL_RCC_OSC32K_FREQ))

HAL_StatusTypeDef HAL_IWDG_Init(HAL_IWDG_PrescalerTypeDef prescaler, uint16_t reload);
HAL_StatusTypeDef HAL_IWDG_SetReload(uint16_t reload);
HAL_StatusTypeDef HAL_IWDG_EnableDebugStop(FunctionalState s);
void HAL_IWDG_Feed(void);
uint32_t HAL_IWDG_GetCounter(void);

/* ===================== 中断 ===================== */

/* 在 WDOG_BAT_IRQHandler 中调用 */

void HAL_WDG_IRQHandler(void);

/* WWDG 溢出中断回调，默认空实现 */

void HAL_WWDG_Callback(void);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_WDG_H */
