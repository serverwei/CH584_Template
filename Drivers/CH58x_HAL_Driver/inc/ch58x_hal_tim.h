#ifndef __CH58X_HAL_TIM_H
#define __CH58X_HAL_TIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 寄存器块 =====================
 * TMR0~TMR3 四个实例等距 0x400
 */
typedef struct
{
    volatile uint8_t CTRL_MOD; /* +0x00 模式控制 */
    volatile uint8_t CTRL_DMA; /* +0x01 DMA 控制 */
    volatile uint8_t INTER_EN; /* +0x02 中断使能 */
    volatile uint8_t RESERVED0[3];
    volatile uint8_t INT_FLAG;   /* +0x06 中断标志（写 1 清除） */
    volatile uint8_t FIFO_COUNT; /* +0x07 FIFO 计数（只读） */
    volatile uint32_t COUNT;     /* +0x08 当前计数（只读） */
    volatile uint32_t CNT_END;   /* +0x0C 周期终值，低 26 位 */
    volatile uint32_t FIFO;      /* +0x10 捕获值 / PWM 占空比，低 26 位 */
    volatile uint32_t DMA_NOW;   /* +0x14 */
    volatile uint32_t DMA_BEG;   /* +0x18 */
    volatile uint32_t DMA_END;   /* +0x1C */
} TMR_TypeDef;

#define TMR0 ((TMR_TypeDef *)0x40002000U)
#define TMR1 ((TMR_TypeDef *)0x40002400U)
#define TMR2 ((TMR_TypeDef *)0x40002800U)
#define TMR3 ((TMR_TypeDef *)0x40002C00U)

/* ===================== 计数上限 ===================== */
#define HAL_TIM_MAX_PERIOD 0x03FFFFFFU

/* ===================== DMA 限制 =====================
 * 地址寄存器只有 bit16:2 有效（17 位地址域），因此：
 *   缓冲区首地址必须 4 字节对齐；
 *   长度上限 0x1FFFC / 4 = 0x7FFF 个 uint32_t。
 * 两个 DMA 方向：
 *   PWM 模式  —— 缓冲区逐个送进 FIFO（占空比序列）
 *   捕获/计数 —— FIFO 逐个搬进缓冲区
 * 捕获模式下前两个数据是无效值（厂商 EVT 例程已注明），取数时跳过。
 */
#define HAL_TIM_DMA_ADDR_MASK 0x0001FFFCU
#define HAL_TIM_DMA_LEN_MAX   0x7FFFU

/* ===================== 状态 ===================== */
typedef enum {
    HAL_TIM_STATE_RESET = 0,
    HAL_TIM_STATE_READY,
    HAL_TIM_STATE_BUSY
} HAL_TIM_StateTypeDef;

/* ===================== 配置枚举 ===================== */
typedef enum {
    HAL_TIM_MODE_TIMER = 0, /* 定时 */
    HAL_TIM_MODE_PWM,       /* PWM 输出 */
    HAL_TIM_MODE_CAPTURE,   /* 输入捕获 */
    HAL_TIM_MODE_COUNTER    /* 外部边沿计数 */
} HAL_TIM_ModeTypeDef;

typedef enum {
    HAL_TIM_CAP_EDGE_CHANGE = 1, /* 任意边沿 */
    HAL_TIM_CAP_EDGE_FALL,       /* 下降沿到下降沿 */
    HAL_TIM_CAP_EDGE_RISE        /* 上升沿到上升沿 */
} HAL_TIM_CapEdgeTypeDef;

typedef enum {
    HAL_TIM_PWM_POLAR_LOW  = 0, /* 默认低，有效电平高 */
    HAL_TIM_PWM_POLAR_HIGH = 1  /* 默认高，有效电平低 */
} HAL_TIM_PWMPolarTypeDef;

/* PWM 重复输出次数，对应 CTRL_MOD.PWM_REPEAT(bit7:6)。
 * 同一个 FIFO 值连续输出 N 个 PWM 周期后才取下一个值，
 * 用于降低 FIFO 供数频率（CPU 中断或 DMA 请求频率降为 1/N）。
 */
typedef enum {
    HAL_TIM_PWM_REPEAT_1 = 0, /* 每个 FIFO 值输出 1 个周期，普通 PWM */
    HAL_TIM_PWM_REPEAT_4,     /* 每个 FIFO 值连续输出 4 个周期 */
    HAL_TIM_PWM_REPEAT_8,     /* 每个 FIFO 值连续输出 8 个周期 */
    HAL_TIM_PWM_REPEAT_16     /* 每个 FIFO 值连续输出 16 个周期 */
} HAL_TIM_PWMRepeatTypeDef;

/* DMA 地址循环模式，对应 CTRL_DMA.RB_TMR_DMA_LOOP(bit2)。
 * 单次模式下搬运结束 DMA_NOW 停在 DMA_END，完成中断必须关掉才不会再报，
 * HAL_TIM_IRQHandler 已代为关闭，用户不必自己处理。
 */
typedef enum {
    HAL_TIM_DMA_ONCE = 0, /* 搬到 END 即停 */
    HAL_TIM_DMA_LOOP      /* 到 END 后自动回到 BEG 继续 */
} HAL_TIM_DMALoopTypeDef;

typedef struct
{
    HAL_TIM_ModeTypeDef Mode;
    uint32_t Period;                    /* 定时 / PWM 周期计数值 */
    HAL_TIM_PWMPolarTypeDef PWMPolar;   /* 仅 PWM 模式 */
    HAL_TIM_PWMRepeatTypeDef PWMRepeat; /* 仅 PWM 模式 */
    HAL_TIM_CapEdgeTypeDef CapEdge;     /* 仅捕获 / 计数模式 */
} TIM_InitTypeDef;

/* 捕获 / 计数输入引脚的上下拉，仅输入模式有效 */
typedef enum {
    HAL_TIM_PIN_FLOATING = 0, /* 浮空输入：外部已有驱动时用 */
    HAL_TIM_PIN_PULLUP,       /* 上拉输入：开漏输出 / 按键对地 / 悬空线 */
    HAL_TIM_PIN_PULLDOWN      /* 下拉输入：开漏输出 / 按键对电源 */
} HAL_TIM_PinModeTypeDef;

typedef struct
{
    TMR_TypeDef *Instance;
    TIM_InitTypeDef Init;
    uint8_t PinGroup;               /* 0 = 默认引脚组，1 = 备用组；由 BSP 的 MspInit 使用 */
    HAL_TIM_PinModeTypeDef PinMode; /* 输入模式下的上下拉；由 BSP 的 MspInit 使用 */
    volatile uint8_t State;
} TIM_HandleTypeDef;

/* ===================== 寄存器级宏 =====================
 * 均为单条寄存器操作，在调用点展开，因此跟随调用者所在的段：
 * 被 __HIGH_CODE 函数调用时就在 RAM 执行，不需要额外的 RAM 版本。
 */

#define __HAL_TIM_SET_PERIOD(__HANDLE__, __PERIOD__) \
    ((__HANDLE__)->Instance->CNT_END = (uint32_t)(__PERIOD__) & HAL_TIM_MAX_PERIOD)

#define __HAL_TIM_GET_PERIOD(__HANDLE__) \
    ((__HANDLE__)->Instance->CNT_END)

#define __HAL_TIM_GET_COUNTER(__HANDLE__) \
    ((__HANDLE__)->Instance->COUNT)

#define __HAL_TIM_RESET_COUNTER(__HANDLE__) \
    ((__HANDLE__)->Instance->CTRL_MOD |= RB_TMR_ALL_CLEAR)

#define __HAL_TIM_SET_PULSE(__HANDLE__, __PULSE__) \
    ((__HANDLE__)->Instance->FIFO = (uint32_t)(__PULSE__) & HAL_TIM_MAX_PERIOD)

#define __HAL_TIM_GET_PULSE(__HANDLE__) \
    ((__HANDLE__)->Instance->FIFO)

#define __HAL_TIM_GET_FIFO_COUNT(__HANDLE__) \
    ((__HANDLE__)->Instance->FIFO_COUNT)

#define __HAL_TIM_ENABLE(__HANDLE__) \
    ((__HANDLE__)->Instance->CTRL_MOD |= RB_TMR_COUNT_EN)

#define __HAL_TIM_DISABLE(__HANDLE__) \
    ((__HANDLE__)->Instance->CTRL_MOD &= (uint8_t)~RB_TMR_COUNT_EN)

#define __HAL_TIM_PWM_ENABLE(__HANDLE__) \
    ((__HANDLE__)->Instance->CTRL_MOD |= RB_TMR_OUT_EN)

#define __HAL_TIM_PWM_DISABLE(__HANDLE__) \
    ((__HANDLE__)->Instance->CTRL_MOD &= (uint8_t)~RB_TMR_OUT_EN)

#define __HAL_TIM_GET_FLAG(__HANDLE__, __FLAG__) \
    (((__HANDLE__)->Instance->INT_FLAG & (__FLAG__)) != 0U)

#define __HAL_TIM_CLEAR_FLAG(__HANDLE__, __FLAG__) \
    ((__HANDLE__)->Instance->INT_FLAG = (uint8_t)(__FLAG__))

#define __HAL_TIM_GET_IT_SOURCE(__HANDLE__, __IT__) \
    (((__HANDLE__)->Instance->INTER_EN & (__IT__)) != 0U)

/* 已搬运的元素个数，用当前地址减起始地址再右移 2 位 */
#define __HAL_TIM_GET_DMA_COUNT(__HANDLE__) \
    (((__HANDLE__)->Instance->DMA_NOW - (__HANDLE__)->Instance->DMA_BEG) >> 2)

/* ===================== 中断源 =====================
 * 数值与 TMRx_INTER_EN / INT_FLAG 的位一一对应，
 * 可直接传给 HAL_TIM_Enable_IT / HAL_TIM_Disable_IT 与 __HAL_TIM_GET_FLAG。
 */
#define HAL_TIM_IT_CYC_END  0x01U /* 定时-周期结束 / 捕获-超时 / PWM-周期结束 */
#define HAL_TIM_IT_DATA_ACT 0x02U /* 捕获-新数据 / PWM-有效电平结束 */
#define HAL_TIM_IT_FIFO_HF  0x04U /* 捕获-FIFO≥4 / PWM-FIFO≤3 */
#define HAL_TIM_IT_DMA_END  0x08U /* DMA 完成 */
#define HAL_TIM_IT_FIFO_OV  0x10U /* 捕获-FIFO满 / PWM-FIFO空 */
#define HAL_TIM_IT_ALL      0x1FU

/* ===================== 初始化 ===================== */

HAL_StatusTypeDef HAL_TIM_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_DeInit(TIM_HandleTypeDef *htim);
void HAL_TIM_MspInit(TIM_HandleTypeDef *htim);
void HAL_TIM_MspDeInit(TIM_HandleTypeDef *htim);

/* ===================== 定时（Base） ===================== */

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);

/* ===================== PWM ===================== */

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t pulse);
HAL_StatusTypeDef HAL_TIM_PWM_Start_IT(TIM_HandleTypeDef *htim, uint32_t pulse);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_PWM_Stop_IT(TIM_HandleTypeDef *htim);

/* ===================== 捕获 / 计数 ===================== */

HAL_StatusTypeDef HAL_TIM_Capture_Start(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Capture_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Capture_Stop(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Capture_Stop_IT(TIM_HandleTypeDef *htim);

HAL_StatusTypeDef HAL_TIM_PWM_Start_DMA(TIM_HandleTypeDef *htim, const uint32_t *pulse,
                                        uint32_t len, HAL_TIM_DMALoopTypeDef loop);
HAL_StatusTypeDef HAL_TIM_Capture_Start_DMA(TIM_HandleTypeDef *htim, uint32_t *buf,
                                            uint32_t len, HAL_TIM_DMALoopTypeDef loop);
HAL_StatusTypeDef HAL_TIM_Stop_DMA(TIM_HandleTypeDef *htim);
uint32_t HAL_TIM_GetDMACount(TIM_HandleTypeDef *htim);

/* ===================== 中断使能 =====================
 * 成对接口没覆盖的中断源（如捕获超时、FIFO 半满、FIFO 溢出）用这两个开关。
 */

void HAL_TIM_Enable_IT(TIM_HandleTypeDef *htim, uint8_t it);
void HAL_TIM_Disable_IT(TIM_HandleTypeDef *htim, uint8_t it);

/* ===================== 中断入口与回调 ===================== */

void HAL_TIM_IRQHandler(TIM_HandleTypeDef *htim);

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_TIM_CaptureCallback(TIM_HandleTypeDef *htim, uint32_t value);
void HAL_TIM_OverflowCallback(TIM_HandleTypeDef *htim);
void HAL_TIM_FifoHalfCallback(TIM_HandleTypeDef *htim);
void HAL_TIM_DMACpltCallback(TIM_HandleTypeDef *htim);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_TIM_H */
