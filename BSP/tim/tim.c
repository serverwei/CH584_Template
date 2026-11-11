#include "tim.h"

typedef struct
{
    GPIO_TypeDef *Port0;
    uint32_t Pin0;
    GPIO_TypeDef *Port1;
    uint32_t Pin1;
    uint16_t AltBit;
} BSP_TIM_PinTypeDef;

/* 组 0 / 组 1 引脚见 R16_PIN_ALTERNATE 的 RB_PIN_TMRx */
static const BSP_TIM_PinTypeDef s_timPin[4] =
    {
        {GPIOA, GPIO_PIN_9, GPIOB, GPIO_PIN_23, RB_PIN_TMR0},  /* TMR0: PA9 / PB23 */
        {GPIOA, GPIO_PIN_10, GPIOB, GPIO_PIN_10, RB_PIN_TMR1}, /* TMR1: PA10 / PB10 */
        {GPIOA, GPIO_PIN_11, GPIOB, GPIO_PIN_11, RB_PIN_TMR2}, /* TMR2: PA11 / PB11 */
        {GPIOA, GPIO_PIN_2, GPIOB, GPIO_PIN_22, RB_PIN_TMR3}   /* TMR3: PA2 / PB22 */
};

/**
 * @brief  实例索引，非法返回 4
 */
static uint8_t BSP_TIM_GetIndex(TMR_TypeDef *tim)
{
    if (tim == TMR0) {
        return 0U;
    }
    if (tim == TMR1) {
        return 1U;
    }
    if (tim == TMR2) {
        return 2U;
    }
    if (tim == TMR3) {
        return 3U;
    }

    return 4U;
}

/**
 * @brief  TMR 引脚初始化，由 HAL_TIM_Init 调用
 * @param  htim 句柄
 * @note   引脚组 1 的冲突：TMR0 组1 与 UART0 组1 无冲突，但与 GPIO 复用需注意；
 *         TMR2 组1 (PB11) 与 BOOT 脚冲突，TMR3 组1 (PB22) 与 BOOT 脚 / UART2 组1 冲突。
 */
void HAL_TIM_MspInit(TIM_HandleTypeDef *htim)
{
    const BSP_TIM_PinTypeDef *pin;
    GPIO_TypeDef *port;
    uint32_t pinMask;
    uint8_t idx;
    uint8_t group;

    if ((htim == NULL) || (htim->Instance == NULL)) {
        return;
    }

    idx = BSP_TIM_GetIndex(htim->Instance);

    if (idx >= 4U) {
        return;
    }

    /* 纯定时模式只做内部计数，信号不进出，不需要占用引脚 */
    if (htim->Init.Mode == HAL_TIM_MODE_TIMER) {
        return;
    }

    group = (htim->PinGroup != 0U) ? 1U : 0U;
    pin   = &s_timPin[idx];

    if (group != 0U) {
        HAL_GPIO_SetPeriphRemap(ENABLE, pin->AltBit);
        port    = pin->Port1;
        pinMask = pin->Pin1;
    } else {
        HAL_GPIO_SetPeriphRemap(DISABLE, pin->AltBit);
        port    = pin->Port0;
        pinMask = pin->Pin0;
    }

    if (htim->Init.Mode == HAL_TIM_MODE_PWM) {
        /* PWM 输出：推挽驱动 */
        HAL_GPIO_SetMode(port, pinMask, HAL_GPIO_MODE_OUT_PP_5MA);
    } else {
        /* 捕获 / 计数：信号是进来的，必须配成输入，
         * 上下拉由 BSP_TIMn_InitCapture / InitCounter 的 pinMode 指定 */
        switch (htim->PinMode) {
            case HAL_TIM_PIN_PULLUP:
                HAL_GPIO_SetMode(port, pinMask, HAL_GPIO_MODE_IN_PULLUP);
                break;

            case HAL_TIM_PIN_PULLDOWN:
                HAL_GPIO_SetMode(port, pinMask, HAL_GPIO_MODE_IN_PULLDOWN);
                break;

            case HAL_TIM_PIN_FLOATING:
            default:
                HAL_GPIO_SetMode(port, pinMask, HAL_GPIO_MODE_IN_FLOATING);
                break;
        }
    }

    HAL_GPIO_SetDigitalInput(port, pinMask, ENABLE);
}

/**
 * @brief  TMR 引脚回收
 */
void HAL_TIM_MspDeInit(TIM_HandleTypeDef *htim)
{
    const BSP_TIM_PinTypeDef *pin;
    GPIO_TypeDef *port;
    uint32_t pinMask;
    uint8_t idx;

    if ((htim == NULL) || (htim->Instance == NULL)) {
        return;
    }

    idx = BSP_TIM_GetIndex(htim->Instance);

    if (idx >= 4U) {
        return;
    }

    /* 纯定时模式在 MspInit 里没占过引脚，这里也无须回收 */
    if (htim->Init.Mode == HAL_TIM_MODE_TIMER) {
        return;
    }

    pin = &s_timPin[idx];

    if (htim->PinGroup != 0U) {
        port    = pin->Port1;
        pinMask = pin->Pin1;
    } else {
        port    = pin->Port0;
        pinMask = pin->Pin0;
    }

    HAL_GPIO_SetMode(port, pinMask, HAL_GPIO_MODE_IN_FLOATING);
    HAL_GPIO_SetDigitalInput(port, pinMask, DISABLE);
}

/* ===================== 回调分发 ===================== */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return;
    }

    if (htim->Instance == TMR0) {
        BSP_TIM0_PeriodElapsedCallback();
    } else if (htim->Instance == TMR1) {
        BSP_TIM1_PeriodElapsedCallback();
    } else if (htim->Instance == TMR2) {
        BSP_TIM2_PeriodElapsedCallback();
    } else if (htim->Instance == TMR3) {
        BSP_TIM3_PeriodElapsedCallback();
    } else {
        /* 未知实例，丢弃 */
    }
}

void HAL_TIM_CaptureCallback(TIM_HandleTypeDef *htim, uint32_t value)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return;
    }

    if (htim->Instance == TMR0) {
        BSP_TIM0_CaptureCallback(value);
    } else if (htim->Instance == TMR1) {
        BSP_TIM1_CaptureCallback(value);
    } else if (htim->Instance == TMR2) {
        BSP_TIM2_CaptureCallback(value);
    } else if (htim->Instance == TMR3) {
        BSP_TIM3_CaptureCallback(value);
    } else {
        /* 未知实例，丢弃 */
    }
}

void HAL_TIM_OverflowCallback(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return;
    }

    switch (BSP_TIM_GetIndex(htim->Instance)) {
        case 0U:
            BSP_TIM0_OverflowCallback();
            break;
        case 1U:
            BSP_TIM1_OverflowCallback();
            break;
        case 2U:
            BSP_TIM2_OverflowCallback();
            break;
        case 3U:
            BSP_TIM3_OverflowCallback();
            break;
        default:
            break; /* 未知实例，丢弃 */
    }
}

void HAL_TIM_FifoHalfCallback(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return;
    }

    switch (BSP_TIM_GetIndex(htim->Instance)) {
        case 0U:
            BSP_TIM0_FifoHalfCallback();
            break;
        case 1U:
            BSP_TIM1_FifoHalfCallback();
            break;
        case 2U:
            BSP_TIM2_FifoHalfCallback();
            break;
        case 3U:
            BSP_TIM3_FifoHalfCallback();
            break;
        default:
            break; /* 未知实例，丢弃 */
    }
}

void HAL_TIM_DMACpltCallback(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return;
    }

    switch (BSP_TIM_GetIndex(htim->Instance)) {
        case 0U:
            BSP_TIM0_DMACpltCallback();
            break;
        case 1U:
            BSP_TIM1_DMACpltCallback();
            break;
        case 2U:
            BSP_TIM2_DMACpltCallback();
            break;
        case 3U:
            BSP_TIM3_DMACpltCallback();
            break;
        default:
            break; /* 未知实例，丢弃 */
    }
}

__weak void BSP_TIM0_PeriodElapsedCallback(void)
{
}
__weak void BSP_TIM1_PeriodElapsedCallback(void)
{
}
__weak void BSP_TIM2_PeriodElapsedCallback(void)
{
}
__weak void BSP_TIM3_PeriodElapsedCallback(void)
{
}

__weak void BSP_TIM0_CaptureCallback(uint32_t value)
{
    UNUSED(value);
}
__weak void BSP_TIM1_CaptureCallback(uint32_t value)
{
    UNUSED(value);
}
__weak void BSP_TIM2_CaptureCallback(uint32_t value)
{
    UNUSED(value);
}
__weak void BSP_TIM3_CaptureCallback(uint32_t value)
{
    UNUSED(value);
}

/* 捕获 FIFO 溢出 / PWM FIFO 空。属于错误事件，默认空实现，
 * 应用里覆盖对应的 BSP_TIMn_OverflowCallback 才能感知丢数据。
 */
__weak void BSP_TIM0_OverflowCallback(void)
{
}
__weak void BSP_TIM1_OverflowCallback(void)
{
}
__weak void BSP_TIM2_OverflowCallback(void)
{
}
__weak void BSP_TIM3_OverflowCallback(void)
{
}

/* FIFO 过半：捕获已满 4 条 / PWM 剩 3 条。
 * 纯 CPU 供数时适合在这里补水；用 DMA 的场景一般不需要。
 */
__weak void BSP_TIM0_FifoHalfCallback(void)
{
}
__weak void BSP_TIM1_FifoHalfCallback(void)
{
}
__weak void BSP_TIM2_FifoHalfCallback(void)
{
}
__weak void BSP_TIM3_FifoHalfCallback(void)
{
}

/* DMA 搬完一轮。单次模式下 HAL_TIM_IRQHandler 已把完成中断使能关掉，
 * 想再传一轮需要重新调 BSP_TIMn_PWMStart_DMA / CaptureStart_DMA。
 */
__weak void BSP_TIM0_DMACpltCallback(void)
{
}
__weak void BSP_TIM1_DMACpltCallback(void)
{
}
__weak void BSP_TIM2_DMACpltCallback(void)
{
}
__weak void BSP_TIM3_DMACpltCallback(void)
{
}
