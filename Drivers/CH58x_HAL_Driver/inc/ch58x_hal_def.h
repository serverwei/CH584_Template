#ifndef __CH58X_HAL_DEF_H
#define __CH58X_HAL_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_conf.h"

#include <stdint.h>
#include <string.h>

#include "CH585SFR.h"
#include "core_riscv.h"
#include "ch58x_hal_clk_def.h"

/* ===================== 中断优先级 =====================
 * CH58x 的 PFIC 只有两档（手册 §3.4.1 IPRIOR[7:5]：数值越小优先级越高，
 * 且只有 2 级嵌套，即只能抢占 1 次）。core_riscv.h 的 PFIC_SetPriority
 * 进一步把所有非 0 值折叠成 0x80，所以这里只区分两档。
 * 具体哪个中断用哪一档，见 ch58x_hal.c 的 s_irqPrio 表。
 */
#define HAL_IRQ_PRIO_HIGH 0U /* 射频：BB / LLE */
#define HAL_IRQ_PRIO_LOW  1U /* 其余全部，实际写入 0x80 */

#ifndef __HIGH_CODE
#define __HIGH_CODE __attribute__((section(".highcode")))
#endif

#ifndef __INTERRUPT
#ifdef INT_SOFT
#define __INTERRUPT __attribute__((interrupt()))
#else
#define __INTERRUPT __attribute__((interrupt("WCH-Interrupt-fast")))
#endif
#endif

#ifndef SAFEOPERATE
#define SAFEOPERATE asm volatile("fence.i")
#endif

typedef enum {
    HAL_OK      = 0x00U,
    HAL_ERROR   = 0x01U,
    HAL_BUSY    = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

#ifndef HAL_MAX_DELAY
#define HAL_MAX_DELAY 0xFFFFFFFFU
#endif

#ifndef __weak
#define __weak __attribute__((weak))
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifdef USE_FULL_ASSERT
void HAL_AssertFailed(const char *file, uint32_t line);
#define HAL_ASSERT(expr) ((expr) ? (void)0 : HAL_AssertFailed(__FILE__, __LINE__))
#else
#define HAL_ASSERT(expr) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_DEF_H */
