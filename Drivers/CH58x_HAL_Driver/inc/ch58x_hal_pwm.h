#ifndef __CH58X_HAL_PWM_H
#define __CH58X_HAL_PWM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 寄存器块 =====================
 * PWMX 是独立外设，与 TMR0~3 无关，共 8 通道 PWM4~PWM11
 */
typedef struct
{
    volatile uint8_t OUT_EN;    /* +0x00 输出使能，8 通道位图 */
    volatile uint8_t POLAR;     /* +0x01 输出极性，8 通道位图 */
    volatile uint8_t CONFIG;    /* +0x02 周期模式 */
    volatile uint8_t CLOCK_DIV; /* +0x03 时钟分频 */
    volatile uint8_t DATA[8];   /* +0x04~+0x0B PWM4~PWM11 占空比 */
    volatile uint8_t INT_CTRL;  /* +0x0C 中断控制 */
    volatile uint8_t RESERVED0[3];
    volatile uint32_t REG_DATA8; /* +0x10 16 位模式下的 PWM8/9 */
    volatile uint32_t REG_CYCLE; /* +0x14 16 位模式周期 */
} PWM_TypeDef;

#define PWMX ((PWM_TypeDef *)0x40005000U)

/* ===================== 通道位 =====================
 * 通道号沿用硅片命名：PWM0~PWM3 属于 TMR0~TMR3（见 ch58x_hal_tim.h），
 * PWMX 独占 PWM4~PWM11，所以这里从 4 开始，不要按 1~8 重排。
 *
 * 8 个通道在 CH585/CH584 上都有焊盘，但分两种：
 *   可重映射（R16_PIN_ALTERNATE.RB_PIN_PWMX 整组切换）
 *     组 0：PWM4=PA12  PWM5=PA13  PWM7=PB4  PWM8=PB6  PWM9=PB7
 *     组 1：PWM4=PA6   PWM5=PA7   PWM7=PB1  PWM8=PB2  PWM9=PB3
 *   固定引脚（不参与重映射，两组都一样）
 *     PWM6=PB0  PWM10=PB14  PWM11=PB23
 * 所以组 0 是 8 个通道全可用，组 1 只有 5 个。
 *
 * PWM11 落在 PB23 上，而 PB23 默认是外部复位输入 RST#。
 * 要用 PWM11 必须在烧录时关闭外部复位（用户选项字节，地址 0x14 的配置字，
 * 见厂商 UserOptionByteConfig），那不是运行期寄存器，代码里改不了。
 */
#define HAL_PWM_CHANNEL_4   0x01U
#define HAL_PWM_CHANNEL_5   0x02U
#define HAL_PWM_CHANNEL_6   0x04U
#define HAL_PWM_CHANNEL_7   0x08U
#define HAL_PWM_CHANNEL_8   0x10U
#define HAL_PWM_CHANNEL_9   0x20U
#define HAL_PWM_CHANNEL_10  0x40U
#define HAL_PWM_CHANNEL_11  0x80U
#define HAL_PWM_CHANNEL_ALL 0xFFU

/* 各引脚组实际引出焊盘的通道集合，MspInit 只按此掩码占引脚：
 *   组 0：8 个通道全引出
 *   组 1：只有 PWM4/5/7/8/9，PWM6/10/11 没有第二组引脚
 */
#define HAL_PWM_GROUP0_PIN_MASK HAL_PWM_CHANNEL_ALL
#define HAL_PWM_GROUP1_PIN_MASK                        \
    ((uint8_t)(HAL_PWM_CHANNEL_4 | HAL_PWM_CHANNEL_5 | \
               HAL_PWM_CHANNEL_7 | HAL_PWM_CHANNEL_8 | HAL_PWM_CHANNEL_9))

/* ===================== 周期模式 ===================== */
typedef enum {
    HAL_PWM_CYCLE_256 = 0, /* 8 位，周期 256 */
    HAL_PWM_CYCLE_255,     /* 8 位，周期 255 */
    HAL_PWM_CYCLE_128,     /* 7 位，周期 128 */
    HAL_PWM_CYCLE_127,     /* 7 位，周期 127 */
    HAL_PWM_CYCLE_64,      /* 6 位，周期 64 */
    HAL_PWM_CYCLE_63       /* 6 位，周期 63 */
} HAL_PWM_CycleTypeDef;

typedef enum {
    HAL_PWM_POLAR_LOW  = 0, /* 默认低，有效电平高 */
    HAL_PWM_POLAR_HIGH = 1  /* 默认高，有效电平低 */
} HAL_PWM_PolarTypeDef;

/* ===================== 中断 =====================
 * PWMX 只有一个中断源：整组共用的周期结束，不区分通道。
 * 所以回调只能告诉应用“一个周期过去了”，要改哪个通道的占空比自己决定。
 *
 * 注意：R8_PWM_INT_CTRL 里 IF_CYC(bit7, RW1) 与 IE_CYC(bit0)、CYC_PRE(bit1)
 * 同处一个寄存器，清标志必须读-改-写，直接写 0x80 会把使能位和触发点一起清掉。
 */
#define HAL_PWM_IT_CYC_END RB_PWM_IE_CYC /* 0x01 周期结束中断 */

/* 周期中断触发点，对应 INT_CTRL.RB_PWM_CYC_PRE。
 * 不提供“默认值”，使能中断前必须由调用方显式指定。
 */
typedef enum {
    HAL_PWM_CYC_PRE_END   = 0, /* 计到周期末尾才触发（8 位 0xFE，7 位 0x7E） */
    HAL_PWM_CYC_PRE_EARLY = 1  /* 提前约 1/16 周期触发（8 位 0xF0， 7 位 0x70） */
} HAL_PWM_CycPointTypeDef;

/* 句柄状态 */
#define HAL_PWM_STATE_RESET 0x00U
#define HAL_PWM_STATE_READY 0x01U

typedef struct
{
    HAL_PWM_CycleTypeDef Cycle;
    uint8_t ClockDiv; /* 时钟分频值，0 表示不修改 */
    uint8_t Channels; /* 当前占用的通道位组合；MspInit 按此占脚，DeInitChannel 会清掉对应位 */
} PWM_InitTypeDef;

typedef struct
{
    PWM_TypeDef *Instance;
    PWM_InitTypeDef Init;
    uint8_t PinGroup; /* 0 = 默认引脚组，1 = 备用组；由 BSP 的 MspInit 使用 */
    volatile uint8_t State;
} PWM_HandleTypeDef;

/* ===================== 寄存器级宏 =====================
 * 单通道操作，在调用点展开，跟随调用者所在的段。
 * ch 只能传单个通道位，多通道请用循环。
 */

#define __HAL_PWM_SET_DUTY(__HANDLE__, __CH__, __DUTY__) \
    ((__HANDLE__)->Instance->DATA[__builtin_ctz((uint32_t)(__CH__))] = (uint8_t)(__DUTY__))

#define __HAL_PWM_GET_DUTY(__HANDLE__, __CH__) \
    ((__HANDLE__)->Instance->DATA[__builtin_ctz((uint32_t)(__CH__))])

#define __HAL_PWM_ENABLE(__HANDLE__, __CH__) \
    ((__HANDLE__)->Instance->OUT_EN |= (uint8_t)(__CH__))

#define __HAL_PWM_DISABLE(__HANDLE__, __CH__) \
    ((__HANDLE__)->Instance->OUT_EN &= (uint8_t)~(uint8_t)(__CH__))

#define __HAL_PWM_SET_POLARITY(__HANDLE__, __CH__, __POLAR__)             \
    do {                                                                  \
        if ((__POLAR__) == HAL_PWM_POLAR_HIGH) {                          \
            (__HANDLE__)->Instance->POLAR |= (uint8_t)(__CH__);           \
        } else {                                                          \
            (__HANDLE__)->Instance->POLAR &= (uint8_t)~(uint8_t)(__CH__); \
        }                                                                 \
    } while (0)

/* ===================== 初始化 ===================== */

HAL_StatusTypeDef HAL_PWM_Init(PWM_HandleTypeDef *hpwm);
HAL_StatusTypeDef HAL_PWM_DeInit(PWM_HandleTypeDef *hpwm);
void HAL_PWM_MspInit(PWM_HandleTypeDef *hpwm);
void HAL_PWM_MspDeInit(PWM_HandleTypeDef *hpwm);
void HAL_PWM_MspChannelDeInit(PWM_HandleTypeDef *hpwm, uint8_t ch);

/* ===================== 通道 =====================
 * Start/Stop 只管寄存器；引脚的占用与归还由 MspInit / MspChannelDeInit 负责，
 * 与 STM32 的 HAL_TIM_PWM_Start/Stop 语义一致：Stop 不会释放引脚。
 */

HAL_StatusTypeDef HAL_PWM_Start(PWM_HandleTypeDef *hpwm, uint8_t ch,
                                uint8_t duty, HAL_PWM_PolarTypeDef pol);
HAL_StatusTypeDef HAL_PWM_StartAll(PWM_HandleTypeDef *hpwm, uint8_t chMask, uint8_t duty);
HAL_StatusTypeDef HAL_PWM_Stop(PWM_HandleTypeDef *hpwm, uint8_t ch);
HAL_StatusTypeDef HAL_PWM_DeInitChannel(PWM_HandleTypeDef *hpwm, uint8_t ch);

/* ===================== 中断 ===================== */

void HAL_PWM_Enable_IT(PWM_HandleTypeDef *hpwm, uint8_t it);
void HAL_PWM_Disable_IT(PWM_HandleTypeDef *hpwm, uint8_t it);
void HAL_PWM_SetCycleIntPoint(PWM_HandleTypeDef *hpwm, HAL_PWM_CycPointTypeDef pt);

void HAL_PWM_IRQHandler(PWM_HandleTypeDef *hpwm);
void HAL_PWM_CycleElapsedCallback(PWM_HandleTypeDef *hpwm);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_PWM_H */
