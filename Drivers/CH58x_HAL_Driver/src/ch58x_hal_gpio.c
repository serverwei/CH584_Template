#include "ch58x_hal_gpio.h"

/* PB8/PB9 与 PB22/PB23 共用中断位，由 R32_PIN_CONFIG.RB_PIN_INTX 决定归属 */
#define HAL_GPIO_IT_SHARED_MASK  0x0300U /* 中断寄存器里 PB8/PB9 对应的位 */
#define HAL_GPIO_IT_SHARED_SHIFT 14U

/**
 * @brief  按端口宽度裁剪引脚掩码（PA 16 位，PB 24 位）
 */
uint32_t HAL_GPIO_Mask(GPIO_TypeDef *port, uint32_t pin)
{
    return (port == GPIOB) ? (pin & 0x00FFFFFFU) : (pin & 0x0000FFFFU);
}

/**
 * @brief  写引脚输出电平
 * @note   走 SET/CLR（WZ）单次写，天然原子
 */
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint32_t pin, HAL_GPIO_PinState state)
{
    if (state == HAL_GPIO_PIN_SET) {
        port->SET = pin;
    } else {
        port->CLR = pin;
    }
}

/**
 * @brief  输出电平反转
 * @note   读改写 OUT 寄存器，非原子；跨上下文翻转同端口建议自行加临界区
 */
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint32_t pin)
{
    port->OUT ^= HAL_GPIO_Mask(port, pin);
}

/**
 * @brief  读引脚输入电平（返回位掩码，非 0 即高）
 */
uint32_t HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint32_t pin)
{
    return (port->PIN & HAL_GPIO_Mask(port, pin));
}

/**
 * @brief  读整个端口的引脚输入电平
 */
uint32_t HAL_GPIO_ReadPort(GPIO_TypeDef *port)
{
    return port->PIN;
}

/**
 * @brief  读输出锁存
 */
uint32_t HAL_GPIO_ReadOutputPort(GPIO_TypeDef *port)
{
    return port->OUT;
}

/**
 * @brief  读指定引脚的输出锁存
 */
HAL_GPIO_PinState HAL_GPIO_ReadOutputPin(GPIO_TypeDef *port, uint32_t pin)
{
    return ((port->OUT & HAL_GPIO_Mask(port, pin)) != 0U) ? HAL_GPIO_PIN_SET : HAL_GPIO_PIN_RESET;
}

/**
 * @brief  把引脚掩码换算成中断寄存器里的位（PB22/23 需下移到 bit8/9）
 */
static uint32_t HAL_GPIO_ITMask(GPIO_TypeDef *port, uint32_t pin)
{
    uint32_t mask = HAL_GPIO_Mask(port, pin);

    if (port == GPIOB) {
        mask |= ((mask & (GPIO_PIN_22 | GPIO_PIN_23)) >> HAL_GPIO_IT_SHARED_SHIFT);
    }

    return mask;
}

/**
 * @brief  配置引脚模式
 * @param  port GPIOA / GPIOB
 * @param  pin  引脚位掩码，可用 GPIO_PIN_x 或 GPIO_PIN_All
 * @param  mode 模式
 */
void HAL_GPIO_SetMode(GPIO_TypeDef *port, uint32_t pin, HAL_GPIO_ModeTypeDef mode)
{
    uint32_t mask = HAL_GPIO_Mask(port, pin);

    if (mask == 0U) {
        return;
    }

    switch (mode) {
        case HAL_GPIO_MODE_IN_FLOATING:
            port->PD_DRV &= ~mask;
            port->PU &= ~mask;
            port->DIR &= ~mask;
            break;

        case HAL_GPIO_MODE_IN_PULLUP:
            port->PD_DRV &= ~mask;
            port->PU |= mask;
            port->DIR &= ~mask;
            break;

        case HAL_GPIO_MODE_IN_PULLDOWN:
            port->PD_DRV |= mask;
            port->PU &= ~mask;
            port->DIR &= ~mask;
            break;

        case HAL_GPIO_MODE_OUT_PP_5MA:
            port->PD_DRV &= ~mask;
            port->DIR |= mask;
            break;

        case HAL_GPIO_MODE_OUT_PP_20MA:
            port->PD_DRV |= mask;
            port->DIR |= mask;
            break;

        default:
            break;
    }
}

/**
 * @brief  配置引脚中断触发方式并使能
 * @note   触发极性借助输出锁存位实现，故该引脚必须是输入；
 *         PB8/PB9 与 PB22/PB23 共用中断位，二者只能选一组。
 */
void HAL_GPIO_ConfigIT(GPIO_TypeDef *port, uint32_t pin, HAL_GPIO_ITModeTypeDef mode)
{
    uint32_t idx  = (port == GPIOB) ? 1U : 0U;
    uint32_t mask = HAL_GPIO_ITMask(port, pin);

    if (mask == 0U) {
        return;
    }

    switch (mode) {
        case HAL_GPIO_IT_LOW_LEVEL: /* 电平模式 + 锁存位=0 */
            GPIO_IT->MODE[idx] &= (uint16_t)~mask;
            port->CLR = pin;
            break;

        case HAL_GPIO_IT_HIGH_LEVEL: /* 电平模式 + 锁存位=1 */
            GPIO_IT->MODE[idx] &= (uint16_t)~mask;
            port->SET = pin;
            break;

        case HAL_GPIO_IT_FALLING: /* 边沿模式 + 锁存位=0 */
            GPIO_IT->MODE[idx] |= (uint16_t)mask;
            port->CLR = pin;
            break;

        case HAL_GPIO_IT_RISING: /* 边沿模式 + 锁存位=1 */
            GPIO_IT->MODE[idx] |= (uint16_t)mask;
            port->SET = pin;
            break;

        default:
            break;
    }

    GPIO_IT->IF[idx] = (uint16_t)mask; /* 先清掉挂起标志 */
    GPIO_IT->EN[idx] |= (uint16_t)mask;
}

/**
 * @brief  读取引脚中断标志（位掩码）
 * @note   PB 的 bit8/9 归属随 RB_PIN_INTX 变化，此处已还原成真实引脚号
 */
uint32_t HAL_GPIO_GetITFlag(GPIO_TypeDef *port)
{
    uint32_t flags = GPIO_IT->IF[(port == GPIOB) ? 1U : 0U];

    if ((port == GPIOB) && ((R32_PIN_CONFIG & RB_PIN_INTX) != 0U)) {
        flags = (flags & ~HAL_GPIO_IT_SHARED_MASK) |
                ((flags & HAL_GPIO_IT_SHARED_MASK) << HAL_GPIO_IT_SHARED_SHIFT);
    }

    return flags;
}

/**
 * @brief  清除指定引脚的中断标志
 */
void HAL_GPIO_ClearITFlag(GPIO_TypeDef *port, uint32_t pin)
{
    uint32_t idx = (port == GPIOB) ? 1U : 0U;

    GPIO_IT->IF[idx] = (uint16_t)HAL_GPIO_ITMask(port, pin);
}

/**
 * @brief  数字输入使能/关闭
 * @note   改动的是全局位图 R32_PIN_IN_DIS（PB 部分还会动 R16_PIN_CONFIG），
 *         会影响同引脚上的 ADC / USB / 32K 等模拟复用，慎用。
 */
void HAL_GPIO_SetDigitalInput(GPIO_TypeDef *port, uint32_t pin, FunctionalState s)
{
    uint32_t disPin;
    uint16_t cfgPin;

    if (port == GPIOA) {
        disPin = pin & 0x0000FFFFU;
        cfgPin = 0U;
    } else {
        disPin = (HAL_GPIO_Mask(port, pin) << 16) & 0xFFFF0000U;
        cfgPin = (uint16_t)((pin >> 8) & 0xFF00U); /* PB8~PB15 对应 R16_PIN_CONFIG bit8~15 */
    }

    if (s != DISABLE) {
        if (disPin != 0U) {
            R32_PIN_IN_DIS &= ~disPin;
        }
        if (cfgPin != 0U) {
            R16_PIN_CONFIG &= (uint16_t)~cfgPin;
        }
    } else {
        if (disPin != 0U) {
            R32_PIN_IN_DIS |= disPin;
        }
        if (cfgPin != 0U) {
            R16_PIN_CONFIG |= cfgPin;
        }
    }
}

/**
 * @brief  外设功能引脚组切换
 * @param  s      是否启用备用引脚组
 * @param  periph  取 RB_PIN_TMR0 ~ RB_PIN_U0_INV
 */
void HAL_GPIO_SetPeriphRemap(FunctionalState s, uint32_t periph)
{
    uint16_t bits = (uint16_t)periph;

    if (s != DISABLE) {
        R16_PIN_ALTERNATE |= bits;
    } else {
        R16_PIN_ALTERNATE &= (uint16_t)~bits;
    }
}

/**
 * @brief  模拟/功能引脚使能
 * @param  s      使能则关闭数字功能、打开模拟功能
 * @param  periph 取 RB_PIN_ADCx_IE / RB_PIN_USB_EN / RB_DEBUG_PIN_SEL 等
 */
void HAL_GPIO_SetAnalog(FunctionalState s, uint16_t periph)
{
    if (s != DISABLE) {
        R16_PIN_CONFIG |= periph;
    } else {
        R16_PIN_CONFIG &= (uint16_t)~periph;
    }
}

/**
 * @brief  GPIO 中断处理，需在 GPIOA_IRQHandler / GPIOB_IRQHandler 中调用
 */
__HIGH_CODE
void HAL_GPIO_IRQHandler(GPIO_TypeDef *port)
{
    uint32_t idx  = (port == GPIOB) ? 1U : 0U;
    uint16_t raw  = GPIO_IT->IF[idx];
    uint32_t pins = raw;

    if (raw == 0U) {
        return;
    }

    GPIO_IT->IF[idx] = raw; /* RW1：写 1 清标志 */

    if ((port == GPIOB) && ((R32_PIN_CONFIG & RB_PIN_INTX) != 0U)) {
        pins = (pins & ~HAL_GPIO_IT_SHARED_MASK) |
               ((pins & HAL_GPIO_IT_SHARED_MASK) << HAL_GPIO_IT_SHARED_SHIFT);
    }

    HAL_GPIO_EXTI_Callback(port, (uint16_t)pins);
}

/**
 * @brief  引脚中断回调，默认空实现
 */
__weak void HAL_GPIO_EXTI_Callback(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port;
    (void)pin;
}
