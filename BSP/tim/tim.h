#ifndef __BSP_TIM_H
#define __BSP_TIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

/* 四个实例共用的回调默认实现（weak）；
 * 实例级的 Init / DeInit / StartIT / Stop 见 tim0.h ~ tim3.h。
 */
void BSP_TIM0_PeriodElapsedCallback(void);
void BSP_TIM1_PeriodElapsedCallback(void);
void BSP_TIM2_PeriodElapsedCallback(void);
void BSP_TIM3_PeriodElapsedCallback(void);

void BSP_TIM0_CaptureCallback(uint32_t value);
void BSP_TIM1_CaptureCallback(uint32_t value);
void BSP_TIM2_CaptureCallback(uint32_t value);
void BSP_TIM3_CaptureCallback(uint32_t value);

void BSP_TIM0_OverflowCallback(void);
void BSP_TIM1_OverflowCallback(void);
void BSP_TIM2_OverflowCallback(void);
void BSP_TIM3_OverflowCallback(void);

void BSP_TIM0_FifoHalfCallback(void);
void BSP_TIM1_FifoHalfCallback(void);
void BSP_TIM2_FifoHalfCallback(void);
void BSP_TIM3_FifoHalfCallback(void);

void BSP_TIM0_DMACpltCallback(void);
void BSP_TIM1_DMACpltCallback(void);
void BSP_TIM2_DMACpltCallback(void);
void BSP_TIM3_DMACpltCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TIM_H */
