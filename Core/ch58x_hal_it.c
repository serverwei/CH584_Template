#include "ch58x_hal_it.h"

#include "ch58x_hal.h"

/* 说明：
 *  - 函数名必须与 startup_CH585.S 向量表完全一致，否则中断会落进默认死循环。
 *  - 全部放在 .highcode（RAM）里执行，由启动代码从 flash 搬运。
 *  - BB_IRQHandler / LLE_IRQHandler / Ecall_M_Mode_Handler / Ecall_U_Mode_Handler
 *    由 libCH58xBLE.a 提供，此处不得定义。
 *  - SysTick：默认由 HAL_InitTick 配成 1ms 中断节拍，在此计数；
 *    协议栈启用后 HAL_UseFreeRunningTick() 会把它切成无中断的自由运行计数器
 *    （避免 1ms 唤醒影响休眠电流），此时本函数不再触发。
 *  - 只有不绑定外设实例的中断留在这里（SysTick / RTC / GPIO / WDG 与空桩）。
 *    绑定句柄的 TMR / UART / I2C 分别放在 BSP/tim、BSP/uart、BSP/i2c 中，
 *    这样不需要某个外设时，直接把它整个目录排除出工程不会断链。
 */

/**
 * @brief  1ms 系统节拍
 * @note   仅在 1ms 中断节拍模式下会进来；HAL_UseFreeRunningTick() 切换后
 *         SysTick 不再产生中断，本函数留在向量表里但不会被触发。
 */
__HIGH_CODE
__INTERRUPT
void SysTick_Handler(void)
{
    SysTick->SR = 0;
    HAL_IncTick();
}

/**
 * @brief  RTC 中断：触发事件与周期定时
 */
__HIGH_CODE
__INTERRUPT
void RTC_IRQHandler(void)
{
    HAL_RTC_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void GPIOA_IRQHandler(void)
{
    HAL_GPIO_IRQHandler(GPIOA);
}

__HIGH_CODE
__INTERRUPT
void GPIOB_IRQHandler(void)
{
    HAL_GPIO_IRQHandler(GPIOB);
}

/* ===================== TMR0~3 / UART0~3 =====================
 * 这些向量在本文件统一提供弱实现，各 BSP 模块只实现 BSP_xxx_IRQHandler 钩子。
 * 排除某个 BSP 模块时钩子退回空实现，中断进来什么都不做，
 * 不会断链，也不影响其它模块。
 */

__weak void BSP_TMR0_IRQHandler(void)
{
}
__weak void BSP_TMR1_IRQHandler(void)
{
}
__weak void BSP_TMR2_IRQHandler(void)
{
}
__weak void BSP_TMR3_IRQHandler(void)
{
}

__weak void BSP_UART0_IRQHandler(void)
{
}
__weak void BSP_UART1_IRQHandler(void)
{
}
__weak void BSP_UART2_IRQHandler(void)
{
}
__weak void BSP_UART3_IRQHandler(void)
{
}

__HIGH_CODE
__INTERRUPT
void TMR0_IRQHandler(void)
{
    BSP_TMR0_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void TMR1_IRQHandler(void)
{
    BSP_TMR1_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void TMR2_IRQHandler(void)
{
    BSP_TMR2_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void TMR3_IRQHandler(void)
{
    BSP_TMR3_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void UART0_IRQHandler(void)
{
    BSP_UART0_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void UART1_IRQHandler(void)
{
    BSP_UART1_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void UART2_IRQHandler(void)
{
    BSP_UART2_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void UART3_IRQHandler(void)
{
    BSP_UART3_IRQHandler();
}

/* ADC 中断由 BSP/adc/adc.c 接管；排除该文件时钩子为空，中断进来什么都不做。
 * ADC 是单实例外设，没有 adc0/adc1，所以句柄放在 BSP 里而不是这里。
 */
__weak void BSP_ADC_IRQHandler(void)
{
}

__HIGH_CODE
__INTERRUPT
void ADC_IRQHandler(void)
{
    BSP_ADC_IRQHandler();
}

/* ===================== I2C / SPI0 ===================== */

__weak void BSP_I2C_IRQHandler(void)
{
}
__weak void BSP_SPI0_IRQHandler(void)
{
}

__HIGH_CODE
__INTERRUPT
void I2C_IRQHandler(void)
{
    BSP_I2C_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void SPI0_IRQHandler(void)
{
    BSP_SPI0_IRQHandler();
}

/* ===================== 共享向量：PWMX / SPI1 =====================
 * 全库唯一一个被两个外设共用的中断向量（IRQ31）。
 * 下面两个弱钩子由 ch58x_hal_it.c 自己定义，不引用任何 BSP 符号，
 * 所以排除 BSP/pwm 或 BSP/spi 都不会断链，只是对应外设不再响应。
 * 各 BSP 模块在自己的 .c 里强定义同名函数即可接管：
 *   BSP/pwm/pwm.c  → void BSP_PWM_IRQHandler(void)  { HAL_PWM_IRQHandler(&hpwm); }
 *   BSP/spi/spi1.c → void BSP_SPI1_IRQHandler(void) { HAL_SPI_IRQHandler(&hspi1); }
 */

__weak void BSP_PWM_IRQHandler(void)
{
}

__weak void BSP_SPI1_IRQHandler(void)
{
}

/**
 * @brief  PWMX 与 SPI1 共享中断入口，由 startup_CH585.S 向量表调用
 * @note   两个被调函数都是弱函数，无强定义时为空，不产生任何副作用。
 */
__HIGH_CODE
__INTERRUPT
void PWMX_SPI1_IRQHandler(void)
{
    BSP_PWM_IRQHandler();
    BSP_SPI1_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void WDOG_BAT_IRQHandler(void)
{
    HAL_WDG_IRQHandler();
}

/* ===================== USB1（全速） =====================
 * IRQ22 由设备模式和主机模式共用，硬件不区分，所以只需要一个入口。
 * HAL 层不持有句柄（HAL_USBFS_IRQHandler 的参数由 BSP 传入），
 * 这里只把中断转给 BSP 的弱钩子；没有 BSP/usb 时钩子为空，不断链。
 */

__weak void BSP_USBFS_IRQHandler(void)
{
}

__HIGH_CODE
__INTERRUPT
void USB_IRQHandler(void)
{
    BSP_USBFS_IRQHandler();
}

/* ===================== USB2（高速） =====================
 * 与 FS 不同，WCH 给 HS 开了两个独立向量：
 *   IRQ37 USB2_DEVICE —— 设备模式
 *   IRQ38 USB2_HOST   —— 主机模式
 * CH584 没有这两个向量，所以整块按型号守卫。
 */
#if (CH58x_CHIP_MODEL != ID_CH584)

__weak void BSP_USBHS_DEV_IRQHandler(void)
{
}

__weak void BSP_USBHS_HST_IRQHandler(void)
{
}

__HIGH_CODE
__INTERRUPT
void USB2_DEVICE_IRQHandler(void)
{
    BSP_USBHS_DEV_IRQHandler();
}

__HIGH_CODE
__INTERRUPT
void USB2_HOST_IRQHandler(void)
{
    BSP_USBHS_HST_IRQHandler();
}

#endif
