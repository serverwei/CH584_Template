#ifndef __CH58X_HAL_GPIO_H
#define __CH58X_HAL_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 端口寄存器块 =====================
 * PA 起始 0x400010A0，PB 起始 0x400010C0，两者字段布局完全一致
 */

typedef struct
{
    volatile uint32_t DIR;    /* +0x00 方向：0=输入 1=输出 */
    volatile uint32_t PIN;    /* +0x04 引脚输入电平（只读） */
    volatile uint32_t OUT;    /* +0x08 输出锁存 */
    volatile uint32_t CLR;    /* +0x0C 写 1 清零（WZ） */
    volatile uint32_t PU;     /* +0x10 上拉使能 */
    volatile uint32_t PD_DRV; /* +0x14 输入=下拉，输出=驱动能力 */
    volatile uint32_t SET;    /* +0x18 写 1 置位（WZ） */
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)0x400010A0U)
#define GPIOB ((GPIO_TypeDef *)0x400010C0U)

/* ===================== 中断寄存器块 =====================
 * 0x90:PA_EN 0x92:PB_EN 0x94:PA_MODE 0x96:PB_MODE 0x9C:PA_IF 0x9E:PB_IF
 */
typedef struct
{
    volatile uint16_t EN[2];   /* 中断使能 0=PA 1=PB */
    volatile uint16_t MODE[2]; /* 0=电平触发 1=边沿触发 */
    volatile uint16_t RESERVED[2];
    volatile uint16_t IF[2]; /* 中断标志，写 1 清除 */
} GPIO_ITTypeDef;

#define GPIO_IT ((GPIO_ITTypeDef *)0x40001090U)

/* ===================== 引脚 =====================
 * CH585SFR.h 把 GPIO_PIN_0/1/2 用作寄存器偏移量（0x04/0x05/0x06），
 * 与本层的位掩码语义冲突；那三个宏在器件头里没有使用者，直接覆盖。
 */
#undef GPIO_PIN_0
#undef GPIO_PIN_1
#undef GPIO_PIN_2

#define GPIO_PIN_0   ((uint32_t)0x00000001U)
#define GPIO_PIN_1   ((uint32_t)0x00000002U)
#define GPIO_PIN_2   ((uint32_t)0x00000004U)
#define GPIO_PIN_3   ((uint32_t)0x00000008U)
#define GPIO_PIN_4   ((uint32_t)0x00000010U)
#define GPIO_PIN_5   ((uint32_t)0x00000020U)
#define GPIO_PIN_6   ((uint32_t)0x00000040U)
#define GPIO_PIN_7   ((uint32_t)0x00000080U)
#define GPIO_PIN_8   ((uint32_t)0x00000100U)
#define GPIO_PIN_9   ((uint32_t)0x00000200U)
#define GPIO_PIN_10  ((uint32_t)0x00000400U)
#define GPIO_PIN_11  ((uint32_t)0x00000800U)
#define GPIO_PIN_12  ((uint32_t)0x00001000U)
#define GPIO_PIN_13  ((uint32_t)0x00002000U)
#define GPIO_PIN_14  ((uint32_t)0x00004000U)
#define GPIO_PIN_15  ((uint32_t)0x00008000U)
#define GPIO_PIN_16  ((uint32_t)0x00010000U) /* 仅 GPIOB */
#define GPIO_PIN_17  ((uint32_t)0x00020000U) /* 仅 GPIOB */
#define GPIO_PIN_18  ((uint32_t)0x00040000U) /* 仅 GPIOB */
#define GPIO_PIN_19  ((uint32_t)0x00080000U) /* 仅 GPIOB */
#define GPIO_PIN_20  ((uint32_t)0x00100000U) /* 仅 GPIOB */
#define GPIO_PIN_21  ((uint32_t)0x00200000U) /* 仅 GPIOB */
#define GPIO_PIN_22  ((uint32_t)0x00400000U) /* 仅 GPIOB */
#define GPIO_PIN_23  ((uint32_t)0x00800000U) /* 仅 GPIOB */
#define GPIO_PIN_All ((uint32_t)0xFFFFFFFFU)

/* 引脚模式：输入三种 + 输出两种。CH585 只有推挽输出，
 * 没有开漏模式，驱动能力仅 5mA / 20mA 两档。
 */
typedef enum {
    HAL_GPIO_MODE_IN_FLOATING = 0, /* 浮空输入：上拉、下拉、驱动均关闭 */
    HAL_GPIO_MODE_IN_PULLUP,       /* 上拉输入 */
    HAL_GPIO_MODE_IN_PULLDOWN,     /* 下拉输入 */
    HAL_GPIO_MODE_OUT_PP_5MA,      /* 推挽输出，驱动能力 5mA */
    HAL_GPIO_MODE_OUT_PP_20MA      /* 推挽输出，驱动能力 20mA */
} HAL_GPIO_ModeTypeDef;

/* 引脚中断触发方式。MODE 位选电平/边沿，输出锁存位选极性，
 * 因此配成中断的引脚必须是输入，否则锁存位会被输出数据覆盖。
 */
typedef enum {
    HAL_GPIO_IT_LOW_LEVEL = 0, /* 低电平触发 */
    HAL_GPIO_IT_HIGH_LEVEL,    /* 高电平触发 */
    HAL_GPIO_IT_FALLING,       /* 下降沿触发 */
    HAL_GPIO_IT_RISING         /* 上升沿触发 */
} HAL_GPIO_ITModeTypeDef;

/* 引脚电平状态，供 HAL_GPIO_WritePin / ReadOutputPin 使用 */
typedef enum {
    HAL_GPIO_PIN_RESET = 0,                  /* 低电平 */
    HAL_GPIO_PIN_SET   = !HAL_GPIO_PIN_RESET /* 高电平 */
} HAL_GPIO_PinState;

/* ===================== 引脚读写 ===================== */

/* 按端口宽度裁剪引脚掩码（PA 16 位，PB 24 位） */

uint32_t HAL_GPIO_Mask(GPIO_TypeDef *port, uint32_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint32_t pin, HAL_GPIO_PinState state);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint32_t pin);
uint32_t HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint32_t pin);
uint32_t HAL_GPIO_ReadPort(GPIO_TypeDef *port);
uint32_t HAL_GPIO_ReadOutputPort(GPIO_TypeDef *port);
HAL_GPIO_PinState HAL_GPIO_ReadOutputPin(GPIO_TypeDef *port, uint32_t pin);

/* ===================== 配置与中断 ===================== */

void HAL_GPIO_SetMode(GPIO_TypeDef *port, uint32_t pin, HAL_GPIO_ModeTypeDef mode);
void HAL_GPIO_ConfigIT(GPIO_TypeDef *port, uint32_t pin, HAL_GPIO_ITModeTypeDef mode);
uint32_t HAL_GPIO_GetITFlag(GPIO_TypeDef *port);
void HAL_GPIO_ClearITFlag(GPIO_TypeDef *port, uint32_t pin);
void HAL_GPIO_SetDigitalInput(GPIO_TypeDef *port, uint32_t pin, FunctionalState s);
void HAL_GPIO_SetPeriphRemap(FunctionalState s, uint32_t periph);
void HAL_GPIO_SetAnalog(FunctionalState s, uint16_t periph);

/* 在 GPIOA_IRQHandler / GPIOB_IRQHandler 中调用 */

void HAL_GPIO_IRQHandler(GPIO_TypeDef *port);

/* 引脚中断回调，pin 为触发引脚位掩码，默认空实现 */

void HAL_GPIO_EXTI_Callback(GPIO_TypeDef *port, uint16_t pin);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_GPIO_H */
