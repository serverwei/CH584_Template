#ifndef __CH58X_HAL_IT_H
#define __CH58X_HAL_IT_H

#ifdef __cplusplus
extern "C" {
#endif

void RTC_IRQHandler(void);
void SysTick_Handler(void);

void GPIOA_IRQHandler(void);
void GPIOB_IRQHandler(void);
void TMR0_IRQHandler(void);
void TMR1_IRQHandler(void);
void TMR2_IRQHandler(void);
void TMR3_IRQHandler(void);
void UART0_IRQHandler(void);
void UART1_IRQHandler(void);
void UART2_IRQHandler(void);
void UART3_IRQHandler(void);
void ADC_IRQHandler(void);
void BSP_ADC_IRQHandler(void);
void I2C_IRQHandler(void);
void SPI0_IRQHandler(void);
void PWMX_SPI1_IRQHandler(void);
void WDOG_BAT_IRQHandler(void);

/* PWMX 与 SPI1 共享 IRQ31，ch58x_hal_it.c 提供弱实现。
 * 各 BSP 模块在自己的 .c 里强定义同名函数即可接管，无需修改中断文件。
 */
void BSP_PWM_IRQHandler(void);
void BSP_SPI1_IRQHandler(void);

/* 下面这些向量同样由 ch58x_hal_it.c 提供弱实现，各 BSP 模块在自己的 .c 里
 * 强定义同名钩子即可接管，无需修改中断文件。
 */
void BSP_TMR0_IRQHandler(void);
void BSP_TMR1_IRQHandler(void);
void BSP_TMR2_IRQHandler(void);
void BSP_TMR3_IRQHandler(void);
void BSP_UART0_IRQHandler(void);
void BSP_UART1_IRQHandler(void);
void BSP_UART2_IRQHandler(void);
void BSP_UART3_IRQHandler(void);
void BSP_I2C_IRQHandler(void);
void BSP_SPI0_IRQHandler(void);

/* USB1（全速）设备 / 主机共用同一个 IRQ22 */
void USB_IRQHandler(void);
void BSP_USBFS_IRQHandler(void);

/* USB2（高速）：IRQ37 / IRQ38 分开，CH584 没有这两个向量 */
#if (CH58x_CHIP_MODEL != ID_CH584)
void USB2_DEVICE_IRQHandler(void);
void USB2_HOST_IRQHandler(void);
void BSP_USBHS_DEV_IRQHandler(void);
void BSP_USBHS_HST_IRQHandler(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_IT_H */
