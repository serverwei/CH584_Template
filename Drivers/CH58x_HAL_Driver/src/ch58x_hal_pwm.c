#include "ch58x_hal.h"

#include <stddef.h>

/* ===================== 寄存器布局校验 =====================
 * 期望偏移取自 CH585DS1.PDF 表 11-1（PWMx 寄存器，基址 0x40005000）。
 * DATA[0]~DATA[7] 对应 PWM4~PWM11，REG_DATA8/REG_CYCLE 在 +0x10/+0x14。
 */
_Static_assert(offsetof(PWM_TypeDef, OUT_EN) == 0x00, "PWM_OUT_EN @ +0x00");
_Static_assert(offsetof(PWM_TypeDef, POLAR) == 0x01, "PWM_POLAR @ +0x01");
_Static_assert(offsetof(PWM_TypeDef, CONFIG) == 0x02, "PWM_CONFIG @ +0x02");
_Static_assert(offsetof(PWM_TypeDef, CLOCK_DIV) == 0x03, "PWM_CLOCK_DIV @ +0x03");
_Static_assert(offsetof(PWM_TypeDef, DATA[0]) == 0x04, "PWM4_DATA @ +0x04");
_Static_assert(offsetof(PWM_TypeDef, DATA[7]) == 0x0B, "PWM11_DATA @ +0x0B");
_Static_assert(offsetof(PWM_TypeDef, INT_CTRL) == 0x0C, "PWM_INT_CTRL @ +0x0C");
_Static_assert(offsetof(PWM_TypeDef, REG_DATA8) == 0x10, "PWM_REG_DATA8 @ +0x10");
_Static_assert(offsetof(PWM_TypeDef, REG_CYCLE) == 0x14, "PWM_REG_CYCLE @ +0x14");
/**
 * @brief  周期模式对应的 CONFIG 低 4 位
 */
static uint8_t HAL_PWM_CycleBits(HAL_PWM_CycleTypeDef cycle)
{
    switch (cycle) {
        case HAL_PWM_CYCLE_256:
            return 0x00U;

        case HAL_PWM_CYCLE_255:
            return 0x01U;

        case HAL_PWM_CYCLE_128:
            return (uint8_t)(1U << 2);

        case HAL_PWM_CYCLE_127:
            return (uint8_t)((1U << 2) | 0x01U);

        case HAL_PWM_CYCLE_64:
            return (uint8_t)(2U << 2);

        case HAL_PWM_CYCLE_63:
            return (uint8_t)((2U << 2) | 0x01U);

        default:
            return 0x00U;
    }
}

/* ===================== 初始化 ===================== */

/**
 * @brief  初始化 PWMX
 * @param  hpwm 句柄，调用前需填好 Instance 与 Init
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   八通道共享一个周期，所以周期只能整体设。
 *         可重复调用（比如追加通道）：只复位 Init.Channels 涉及的位，
 *         已在跑的其它通道不受影响。
 */
HAL_StatusTypeDef HAL_PWM_Init(PWM_HandleTypeDef *hpwm)
{
    PWM_TypeDef *pwm;

    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (hpwm->Instance != PWMX) {
        return HAL_ERROR;
    }
    if ((uint32_t)hpwm->Init.Cycle > (uint32_t)HAL_PWM_CYCLE_63) {
        return HAL_ERROR;
    }

    pwm = hpwm->Instance;

    HAL_PWM_MspInit(hpwm);

    /* 只复位本次声明用到的通道，避免重复调用时误关其它通道 */
    pwm->OUT_EN &= (uint8_t)~hpwm->Init.Channels;
    pwm->POLAR &= (uint8_t)~hpwm->Init.Channels;

    pwm->CONFIG = (uint8_t)((pwm->CONFIG & 0xF0U) | HAL_PWM_CycleBits(hpwm->Init.Cycle));

    if (hpwm->Init.ClockDiv != 0U) {
        pwm->CLOCK_DIV = hpwm->Init.ClockDiv;
    }

    /* 只在首次初始化时复位中断控制：重复调用（追加通道）不能打断
     * 已经在跑的周期中断。IE_CYC=0 关中断，CYC_PRE=0（END），
     * IF_CYC 写 1 清掉上电残留的标志。
     * 触发点不在这里选，使能中断前用 HAL_PWM_SetCycleIntPoint 或
     * BSP_PWM_EnableCycleIT(pt) 显式指定。
     */
    if (hpwm->State != HAL_PWM_STATE_READY) {
        pwm->INT_CTRL = (uint8_t)RB_PWM_IF_CYC;
        hpwm->State   = HAL_PWM_STATE_READY;
    }

    return HAL_OK;
}

/**
 * @brief  关闭 PWMX
 * @param  hpwm 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_PWM_DeInit(PWM_HandleTypeDef *hpwm)
{
    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return HAL_ERROR;
    }

    hpwm->Instance->OUT_EN = 0x00U;
    hpwm->Instance->POLAR  = 0x00U;

    /* 关中断并使触发点回 END；IF_CYC 写 1 把标志清干净 */
    hpwm->Instance->INT_CTRL = (uint8_t)RB_PWM_IF_CYC;

    HAL_PWM_MspDeInit(hpwm);

    hpwm->Init.Channels = 0U;
    hpwm->State         = HAL_PWM_STATE_RESET;

    return HAL_OK;
}

/* ===================== 通道 ===================== */

/**
 * @brief  启动单个通道
 * @param  hpwm 句柄
 * @param  ch   通道位，取 HAL_PWM_CHANNEL_4 ~ HAL_PWM_CHANNEL_11，只允许单个通道
 * @param  duty 有效电平计数值，需小于当前周期
 * @param  pol  输出极性
 * @retval HAL_OK 成功；HAL_ERROR 参数非法，或该通道未在 Init.Channels 里占用
 * @note   先设极性和占空比，最后开输出，避免第一个周期异常。
 *         引脚由 HAL_PWM_MspInit 负责，所以 ch 必须先出现在 Init.Channels 中。
 */
HAL_StatusTypeDef HAL_PWM_Start(PWM_HandleTypeDef *hpwm, uint8_t ch,
                                uint8_t duty, HAL_PWM_PolarTypeDef pol)
{
    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((ch == 0U) || ((ch & (uint8_t)(ch - 1U)) != 0U)) {
        return HAL_ERROR; /* 必须是单个通道位 */
    }
    if ((hpwm->Init.Channels & ch) == 0U) {
        return HAL_ERROR; /* 该通道的引脚未在 MspInit 里占用 */
    }
    if ((uint32_t)pol > (uint32_t)HAL_PWM_POLAR_HIGH) {
        return HAL_ERROR;
    }

    __HAL_PWM_SET_POLARITY(hpwm, ch, pol);
    __HAL_PWM_SET_DUTY(hpwm, ch, duty);
    __HAL_PWM_ENABLE(hpwm, ch);

    return HAL_OK;
}

/**
 * @brief  按位组合批量启动，统一占空比，极性保持不变
 * @param  hpwm   句柄
 * @param  chMask 通道位组合，可或运算，如 HAL_PWM_CHANNEL_4 | HAL_PWM_CHANNEL_5
 * @param  duty   统一的有效电平计数值
 * @retval HAL_OK 成功；HAL_ERROR 参数非法，或 chMask 含未占用的通道
 */
HAL_StatusTypeDef HAL_PWM_StartAll(PWM_HandleTypeDef *hpwm, uint8_t chMask, uint8_t duty)
{
    uint8_t i;

    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (chMask == 0U) {
        return HAL_ERROR;
    }
    if ((chMask & (uint8_t)~hpwm->Init.Channels) != 0U) {
        return HAL_ERROR; /* 含未在 MspInit 里占用的通道 */
    }

    for (i = 0U; i < 8U; i++) {
        if ((chMask & (uint8_t)(1U << i)) != 0U) {
            hpwm->Instance->DATA[i] = duty;
        }
    }

    hpwm->Instance->OUT_EN |= chMask;

    return HAL_OK;
}

/**
 * @brief  停止单个通道输出
 * @param  hpwm 句柄
 * @param  ch   通道位，只允许单个通道
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   只清 OUT_EN，引脚仍保持推挽输出并停在无效电平。
 *         要把引脚让给其它外设，需再调 HAL_PWM_DeInitChannel。
 */
HAL_StatusTypeDef HAL_PWM_Stop(PWM_HandleTypeDef *hpwm, uint8_t ch)
{
    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((ch == 0U) || ((ch & (uint8_t)(ch - 1U)) != 0U)) {
        return HAL_ERROR; /* 必须是单个通道位 */
    }

    __HAL_PWM_DISABLE(hpwm, ch);

    return HAL_OK;
}

/**
 * @brief  注销单个通道：停止输出并归还引脚
 * @param  hpwm 句柄
 * @param  ch   通道位，只允许单个通道
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   会从 Init.Channels 中清掉该位，之后要重新使用必须重新调 HAL_PWM_Init。
 *         通道本来就没占用时直接返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_PWM_DeInitChannel(PWM_HandleTypeDef *hpwm, uint8_t ch)
{
    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((ch == 0U) || ((ch & (uint8_t)(ch - 1U)) != 0U)) {
        return HAL_ERROR; /* 必须是单个通道位 */
    }
    if ((hpwm->Init.Channels & ch) == 0U) {
        return HAL_OK; /* 本来就没占用 */
    }

    __HAL_PWM_DISABLE(hpwm, ch);

    HAL_PWM_MspChannelDeInit(hpwm, ch);

    hpwm->Init.Channels &= (uint8_t)~ch;

    return HAL_OK;
}

__weak void HAL_PWM_MspInit(PWM_HandleTypeDef *hpwm)
{
    UNUSED(hpwm);
}

/* ===================== 中断 ===================== */

/**
 * @brief  打开周期中断并允许 NVIC 响应
 * @param  hpwm 句柄
 * @param  it   中断源，目前只有 HAL_PWM_IT_CYC_END
 * @note   顺手把残留的 IF_CYC 清掉，避免一使能就立刻进一次中断。
 *         优先级由 HAL_NVIC_InitPriorities 统一设置，这里不重复。
 */
void HAL_PWM_Enable_IT(PWM_HandleTypeDef *hpwm, uint8_t it)
{
    uint8_t ctrl;

    if ((hpwm == NULL) || (hpwm->Instance == NULL) || (it == 0U)) {
        return;
    }

    ctrl = hpwm->Instance->INT_CTRL;

    /* 保留 CYC_PRE，写入 it 作使能位，同时写 1 给 IF_CYC 清标志 */
    hpwm->Instance->INT_CTRL = (uint8_t)((ctrl & RB_PWM_CYC_PRE) | it | RB_PWM_IF_CYC);

    PFIC_EnableIRQ(PWMX_IRQn);
}

/**
 * @brief  关闭周期中断
 * @param  hpwm 句柄
 * @param  it   中断源，目前只有 HAL_PWM_IT_CYC_END
 * @note   PWMX 与 SPI1 共用 IRQ31，谁都不能单方面关 NVIC，
 *         否则会顺手把对方的中断线也掐了。共用向量只关自己的使能位，
 *         NVIC 一旦打开就保持，多出来的空入口代价只有几个周期。
 */
void HAL_PWM_Disable_IT(PWM_HandleTypeDef *hpwm, uint8_t it)
{
    uint8_t ctrl;

    if ((hpwm == NULL) || (hpwm->Instance == NULL) || (it == 0U)) {
        return;
    }

    ctrl = hpwm->Instance->INT_CTRL;

    /* 保留 CYC_PRE，清使能位，顺手把 IF_CYC 也清掉 */
    ctrl &= (uint8_t)(RB_PWM_CYC_PRE & (uint8_t)~it);
    hpwm->Instance->INT_CTRL = (uint8_t)(ctrl | RB_PWM_IF_CYC);
}

/**
 * @brief  设置周期中断触发点
 * @param  hpwm 句柄
 * @param  pt   HAL_PWM_CYC_PRE_END / HAL_PWM_CYC_PRE_EARLY
 * @note   HAL_PWM_Init 只把触发点复位成 END，不替调用方选值。
 *         若 DATA 是立即生效，必须选 EARLY，否则在回调里改占空比
 *         会削掉本周期已经输出的那一段。
 */
void HAL_PWM_SetCycleIntPoint(PWM_HandleTypeDef *hpwm, HAL_PWM_CycPointTypeDef pt)
{
    uint8_t ctrl;

    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return;
    }

    /* 只改 bit1，IE_CYC 与 IF_CYC 的写法都保持“不清标志” */
    ctrl = (uint8_t)(hpwm->Instance->INT_CTRL & RB_PWM_IE_CYC);

    if (pt == HAL_PWM_CYC_PRE_EARLY) {
        ctrl |= RB_PWM_CYC_PRE;
    }

    hpwm->Instance->INT_CTRL = ctrl;
}

/**
 * @brief  PWMX 中断处理
 * @param  hpwm 句柄
 * @note   必须自检标志：本函数挂在 PWMX/SPI1 共享向量上，被无条件调用，
 *         不是 PWM 的中断要立即返回。
 */
__HIGH_CODE
void HAL_PWM_IRQHandler(PWM_HandleTypeDef *hpwm)
{
    uint8_t ctrl;

    if ((hpwm == NULL) || (hpwm->Instance == NULL)) {
        return;
    }

    ctrl = hpwm->Instance->INT_CTRL;

    if ((ctrl & RB_PWM_IF_CYC) == 0U) {
        return;
    }

    /* IF_CYC 是 RW1，但和 IE_CYC / CYC_PRE 同处一个寄存器，
     * 必须读-改-写：写 0x80 会把使能位和触发点一起清掉。
     */
    hpwm->Instance->INT_CTRL = (uint8_t)((ctrl & (RB_PWM_IE_CYC | RB_PWM_CYC_PRE)) |
                                         RB_PWM_IF_CYC);

    HAL_PWM_CycleElapsedCallback(hpwm);
}

__weak void HAL_PWM_CycleElapsedCallback(PWM_HandleTypeDef *hpwm)
{
    UNUSED(hpwm);
}

__weak void HAL_PWM_MspDeInit(PWM_HandleTypeDef *hpwm)
{
    UNUSED(hpwm);
}

__weak void HAL_PWM_MspChannelDeInit(PWM_HandleTypeDef *hpwm, uint8_t ch)
{
    UNUSED(hpwm);
    UNUSED(ch);
}
