#include "pwm.h"

PWM_HandleTypeDef hpwm;

typedef struct
{
    GPIO_TypeDef *Port;
    uint32_t Pin;
    uint8_t Ch;
} BSP_PWM_PinTypeDef;

/* PWMX 各通道引脚。组 0 八个通道全有焊盘；组 1（RB_PIN_PWMX = 1）
 * 只有 PWM4/5/7/8/9 被重映射，PWM6/PWM10/PWM11 没有第二组引脚。
 * Ch = 0 的条目表示该组没有这个通道，ConfigPins 会跳过。
 * RB_PIN_PWMX 是全局位，整组一起切换，不能单通道选组。
 */
static const BSP_PWM_PinTypeDef s_pwmPin[2][8] =
    {
        {
            /* 组 0（RB_PIN_PWMX = 0） */
            {GPIOA, GPIO_PIN_12, HAL_PWM_CHANNEL_4},  /* 与 SPI0 组 0 的 SCS 冲突 */
            {GPIOA, GPIO_PIN_13, HAL_PWM_CHANNEL_5},  /* 与 SPI0 组 0 的 SCK0 冲突 */
            {GPIOB, GPIO_PIN_0, HAL_PWM_CHANNEL_6},   /* 与 UART0 的 CTS 冲突 */
            {GPIOB, GPIO_PIN_4, HAL_PWM_CHANNEL_7},   /* 与 UART0 组 0 的 RXD0 冲突 */
            {GPIOB, GPIO_PIN_6, HAL_PWM_CHANNEL_8},   /* 与 UART0 的 RTS 冲突 */
            {GPIOB, GPIO_PIN_7, HAL_PWM_CHANNEL_9},   /* 与 UART0 组 0 的 TXD0 冲突 */
            {GPIOB, GPIO_PIN_14, HAL_PWM_CHANNEL_10}, /* 与 SPI0 组 1 的 MOSI_ 冲突 */
            {GPIOB, GPIO_PIN_23, HAL_PWM_CHANNEL_11}  /* 默认是 RST#，需改选项字节 */
        },
        {                                        /* 组 1（RB_PIN_PWMX = 1），后三个通道没有第二组引脚 */
         {GPIOA, GPIO_PIN_6, HAL_PWM_CHANNEL_4}, /* 与 UART2 组 0 的 RXD2 冲突 */
         {GPIOA, GPIO_PIN_7, HAL_PWM_CHANNEL_5}, /* 与 UART2 组 0 的 TXD2 冲突 */
         {NULL, 0U, 0U},
         {GPIOB, GPIO_PIN_1, HAL_PWM_CHANNEL_7},
         {GPIOB, GPIO_PIN_2, HAL_PWM_CHANNEL_8},
         {GPIOB, GPIO_PIN_3, HAL_PWM_CHANNEL_9},
         {NULL, 0U, 0U},
         {NULL, 0U, 0U}}};

/* 各组实际引出焊盘的通道，与 HAL_PWM_GROUPn_PIN_MASK 一一对应 */
static const uint8_t s_pwmPinMask[2] =
    {
        HAL_PWM_GROUP0_PIN_MASK,
        HAL_PWM_GROUP1_PIN_MASK};

/**
 * @brief  按通道掩码占用或释放引脚
 * @param  hpwm 句柄，用 PinGroup 选引脚组
 * @param  ch   通道位组合
 * @param  s    ENABLE = 配成推挽输出，DISABLE = 还原为浮空输入
 * @note   先按该组的焊盘掩码过滤，组 1 的 PWM6/PWM10/PWM11 会被滤掉。
 */
static void BSP_PWM_ConfigPins(const PWM_HandleTypeDef *hpwm, uint8_t ch, FunctionalState s)
{
    const BSP_PWM_PinTypeDef *pin;
    uint8_t group;
    uint8_t i;

    group = (hpwm->PinGroup != 0U) ? 1U : 0U;
    ch &= s_pwmPinMask[group];

    if (ch == 0U) {
        return;
    }

    pin = s_pwmPin[group];

    for (i = 0U; i < 8U; i++) {
        if ((ch & pin[i].Ch) == 0U) {
            continue;
        }

        if (s != DISABLE) {
            HAL_GPIO_SetMode(pin[i].Port, pin[i].Pin, HAL_GPIO_MODE_OUT_PP_5MA);
            HAL_GPIO_SetDigitalInput(pin[i].Port, pin[i].Pin, ENABLE);
        } else {
            HAL_GPIO_SetMode(pin[i].Port, pin[i].Pin, HAL_GPIO_MODE_IN_FLOATING);
            HAL_GPIO_SetDigitalInput(pin[i].Port, pin[i].Pin, DISABLE);
        }
    }
}

/**
 * @brief  PWMX 引脚初始化，由 HAL_PWM_Init 调用
 * @param  hpwm 句柄，读取 Init.Channels 与 PinGroup
 * @note   只占用 Init.Channels 里选中的通道引脚，未选中的脚保持原功能。
 *         RB_PIN_PWMX 是全局位，一次切换影响全部 PWMX 通道。
 *         组 1 的 PWM6/PWM10/PWM11 没有引脚，写进 Channels 也不会占脚。
 */
void HAL_PWM_MspInit(PWM_HandleTypeDef *hpwm)
{
    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return;
    }

    if ((hpwm->Init.Channels & s_pwmPinMask[(hpwm->PinGroup != 0U) ? 1U : 0U]) == 0U) {
        return; /* 没有带引脚的通道，复用位也不用动 */
    }

    HAL_GPIO_SetPeriphRemap((hpwm->PinGroup != 0U) ? ENABLE : DISABLE, RB_PIN_PWMX);

    BSP_PWM_ConfigPins(hpwm, hpwm->Init.Channels, ENABLE);
}

/**
 * @brief  PWMX 引脚回收，由 HAL_PWM_DeInit 调用
 * @param  hpwm 句柄
 * @note   回收 Init.Channels 里全部带引脚的通道。
 */
void HAL_PWM_MspDeInit(PWM_HandleTypeDef *hpwm)
{
    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return;
    }

    BSP_PWM_ConfigPins(hpwm, hpwm->Init.Channels, DISABLE);
}

/**
 * @brief  单通道引脚回收，由 HAL_PWM_DeInitChannel 调用
 * @param  hpwm 句柄
 * @param  ch   要回收的通道位，只允许单个通道
 * @note   不动 RB_PIN_PWMX，同组其它通道继续正常输出。
 */
void HAL_PWM_MspChannelDeInit(PWM_HandleTypeDef *hpwm, uint8_t ch)
{
    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return;
    }

    BSP_PWM_ConfigPins(hpwm, ch, DISABLE);
}

/**
 * @brief  初始化 PWMX
 * @param  cycle    周期模式，HAL_PWM_CYCLE_256 / 255 / 128 / 127 / 64 / 63
 * @param  clockDiv 时钟分频，0 表示不修改
 * @param  channels 本次使用的通道位组合，如 HAL_PWM_CHANNEL_4 | HAL_PWM_CHANNEL_5
 * @param  pinGroup 0 = 默认引脚组，1 = 备用引脚组
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   组 0 八个通道全有引脚；组 1 只有 PWM4/5/7/8/9，
 *         其它通道写进 channels 也能输出，只是出不了引脚。
 */
HAL_StatusTypeDef BSP_PWM_Init(HAL_PWM_CycleTypeDef cycle, uint8_t clockDiv,
                               uint8_t channels, uint8_t pinGroup)
{
    hpwm.Instance      = PWMX;
    hpwm.Init.Cycle    = cycle;
    hpwm.Init.ClockDiv = clockDiv;
    hpwm.Init.Channels = channels;
    hpwm.PinGroup      = pinGroup;

    return HAL_PWM_Init(&hpwm);
}

HAL_StatusTypeDef BSP_PWM_DeInit(void)
{
    return HAL_PWM_DeInit(&hpwm);
}

/* ===================== 中断 =====================
 * PWMX 只有一个中断源：整组共用的周期结束。回调里能拿到的是
 * “一个周期过去了”，不是“某个通道到周期了”，改哪个通道由应用自己决定。
 *
 * 触发点不设默认值，由调用方通过 BSP_PWM_EnableCycleIT 的参数显式指定：
 *   HAL_PWM_CYC_PRE_EARLY —— 提前约 1/16 周期触发，回调里改 DATA 不会削掉本周期
 *   HAL_PWM_CYC_PRE_END   —— 周期末尾触发，与周期边界完全对齐
 * DATA 若是立即生效，必须用 EARLY；若是双缓冲，两者都可以。
 */

/**
 * @brief  打开 PWMX 周期结束中断
 * @param  pt 触发点，HAL_PWM_CYC_PRE_EARLY / HAL_PWM_CYC_PRE_END
 */
void BSP_PWM_EnableCycleIT(HAL_PWM_CycPointTypeDef pt)
{
    HAL_PWM_SetCycleIntPoint(&hpwm, pt);
    HAL_PWM_Enable_IT(&hpwm, HAL_PWM_IT_CYC_END);
}

/**
 * @brief  关闭 PWMX 周期结束中断
 */
void BSP_PWM_DisableCycleIT(void)
{
    HAL_PWM_Disable_IT(&hpwm, HAL_PWM_IT_CYC_END);
}

/**
 * @brief  周期结束回调的分发，由 HAL_PWM_CycleElapsedCallback 调用
 * @param  handle 触发回调的句柄
 * @note   覆盖 HAL 的 weak 实现，全工程只能有这一份。在中断上下文执行。
 */
void HAL_PWM_CycleElapsedCallback(PWM_HandleTypeDef *handle)
{
    UNUSED(handle);
    BSP_PWM_CycleElapsedCallback();
}

/**
 * @brief  PWMX 中断，由 ch58x_hal_it.c 的共享向量入口调用
 * @note   SPI1 也挂在 IRQ31 上，HAL_PWM_IRQHandler 内部会先查标志，
 *         不是 PWM 的中断立即返回，不会误处理 SPI1 的事件。
 */
__HIGH_CODE
void BSP_PWM_IRQHandler(void)
{
    HAL_PWM_IRQHandler(&hpwm);
}

__weak void BSP_PWM_CycleElapsedCallback(void)
{
}
