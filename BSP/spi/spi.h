#ifndef __BSP_SPI_H
#define __BSP_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal.h"

/* SPI0 引脚组（两组互斥，由 R16_PIN_ALTERNATE.RB_PIN_SPI0 切换）
 *   组 0：PA12=SCS   PA13=SCK0   PA14=MOSI   PA15=MISO
 *   组 1：PB12=SCS_  PB13=SCK0_  PB14=MOSI_  PB15=MISO_
 *
 * SPI1（仅 CH585，且只能作主机）：PA0=SCK1  PA1=MOSI1  PA2=MISO1，没有片选引脚
 *
 * 引脚复用冲突提醒：
 *   SPI0 组 0 的 PA12/PA13 同时是 PWM4/PWM5
 *   SPI0 组 1 的 PB12/PB13 同时是 UART1 备用组
 *   SPI1 的 PA2 同时是 TMR3 引脚
 * 启用前先确认这些外设没有被同时占用。
 */

/* ===================== 片选：只能软件控制 =====================
 * 手册 §10.4.1：「SPI 主机模式下，在 SCK 引脚产生串行时钟，片选引脚可以
 * 指定为任意 I/O 引脚。」主机模式下 SCS 完全由硬件忽略，PA12/PB12 上的
 * SCS 只在 SPI0 从机模式才有意义。所以主机片选必须用普通 GPIO 手动拉低/拉高。
 *
 * BSP 把片选脚当成配置项传进来：CsPort 为 NULL 表示不用软件片选（多从机
 * 或片选由应用自己管），业务层再调 BSP_SPIx_CS_Select / BSP_SPIx_CS_Release。
 */

typedef struct
{
    uint8_t ClockDiv;                 /* 主机分频 2~254，Fsck = Fsys / ClockDiv */
    HAL_SPI_ModeTypeDef Mode;         /* HAL_SPI_MODE_0 / HAL_SPI_MODE_3 */
    HAL_SPI_BitOrderTypeDef BitOrder; /* HAL_SPI_BITORDER_MSB / HAL_SPI_BITORDER_LSB */
    uint8_t PinGroup;                 /* 0 = 默认引脚组，1 = 备用引脚组；SPI1 忽略此字段 */
    GPIO_TypeDef *CsPort;             /* 片选端口，NULL 表示不管理片选 */
    uint32_t CsPin;                   /* 片选引脚位，CsPort 非 NULL 时有效 */
} BSP_SPI_ConfigTypeDef;

/* 六个回调的默认实现（weak）；
 * 实例级的 Init / DeInit / 收发接口见 spi0.h / spi1.h。
 */
void BSP_SPI0_TxCpltCallback(void);
void BSP_SPI0_RxCpltCallback(void);
void BSP_SPI0_ErrorCallback(void);

void BSP_SPI1_TxCpltCallback(void);
void BSP_SPI1_RxCpltCallback(void);
void BSP_SPI1_ErrorCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SPI_H */
