#ifndef __CH58X_HAL_I2C_H
#define __CH58X_HAL_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 寄存器块 =====================
 * 只有 I2C1 一个实例，寄存器 16 位宽，等距 4 字节
 */
typedef struct
{
    volatile uint16_t CTRL1; /* +0x00 控制 1 */
    volatile uint16_t RESERVED0;
    volatile uint16_t CTRL2; /* +0x04 控制 2 */
    volatile uint16_t RESERVED1;
    volatile uint16_t OADDR1; /* +0x08 自身地址 1 */
    volatile uint16_t RESERVED2;
    volatile uint16_t OADDR2; /* +0x0C 自身地址 2 */
    volatile uint16_t RESERVED3;
    volatile uint16_t DATAR; /* +0x10 数据 */
    volatile uint16_t RESERVED4;
    volatile uint16_t STAR1; /* +0x14 状态 1（只读，部分位 rc_w0） */
    volatile uint16_t RESERVED5;
    volatile uint16_t STAR2; /* +0x18 状态 2（只读） */
    volatile uint16_t RESERVED6;
    volatile uint16_t CKCFGR; /* +0x1C 时钟控制 */
    volatile uint16_t RESERVED7;
    volatile uint16_t RTR; /* +0x20 上升时间 */
} I2C_TypeDef;

#define I2C1 ((I2C_TypeDef *)0x40004800U)

/* ===================== 状态 ===================== */
typedef enum {
    HAL_I2C_STATE_READY = 0,   /* 空闲，可以发起新传输 */
    HAL_I2C_STATE_BUSY_TX,     /* 主机发送中（Master_Transmit） */
    HAL_I2C_STATE_BUSY_RX,     /* 主机接收中（Master_Receive） */
    HAL_I2C_STATE_BUSY_MEM_TX, /* 带寄存器地址的写（Mem_Write） */
    HAL_I2C_STATE_BUSY_MEM_RX  /* 带寄存器地址的读（Mem_Read） */
} HAL_I2C_StateTypeDef;

/* ===================== 错误码 ===================== */
#define HAL_I2C_ERROR_NONE    0x00U
#define HAL_I2C_ERROR_BERR    0x01U /* 总线错误 */
#define HAL_I2C_ERROR_ARLO    0x02U /* 仲裁丢失 */
#define HAL_I2C_ERROR_AF      0x04U /* 应答失败，常见于器件不在线 */
#define HAL_I2C_ERROR_OVR     0x08U /* 上溢/下溢 */
#define HAL_I2C_ERROR_TIMEOUT 0x10U /* SMBus 35ms 硬件超时 */

/* ===================== 配置枚举 ===================== */
typedef enum {
    HAL_I2C_DUTY_2 = 0, /* 快速模式 Tlow/Thigh = 2 */
    HAL_I2C_DUTY_16_9   /* 快速模式 Tlow/Thigh = 16/9 */
} HAL_I2C_DutyTypeDef;

/* 本机从地址格式，填充 OADDR1 的高 2 位。
 * bit14 是保留位，规范要求恒为 1；bit15 是 RB_I2C_ADDMODE。
 * 仅在作从机时有效，纯主机可以忽略。
 */
typedef enum {
    HAL_I2C_ACK_ADDR_7BIT  = 0x4000U, /* bit14=1，ADDMODE=0：7 位从地址 */
    HAL_I2C_ACK_ADDR_10BIT = 0xC000U  /* bit14=1，ADDMODE=1：10 位从地址 */
} HAL_I2C_AckAddrTypeDef;

typedef enum {
    HAL_I2C_MODE_I2C = 0,   /* 普通 I2C，无硬件超时，只能靠软超时兜底 */
    HAL_I2C_MODE_SMBUS_HOST /* SMBus 主机，带 35ms 硬件超时保护 */
} HAL_I2C_BusModeTypeDef;

/* ===================== 传输阶段 ===================== */
#define HAL_I2C_PHASE_DATA     0U /* 数据阶段 */
#define HAL_I2C_PHASE_MEM_ADDR 1U /* 正在发送寄存器地址 */
#define HAL_I2C_PHASE_RESTART  2U /* 已打 ReSTART，等待发送读地址 */

/* ===================== 初始化与句柄 ===================== */
typedef struct
{
    uint32_t ClockSpeed;           /* 100000 / 400000 */
    HAL_I2C_DutyTypeDef DutyCycle; /* 仅快速模式有效 */
    HAL_I2C_AckAddrTypeDef AckAddr;
    uint16_t OwnAddress1; /* 从机模式下的自身地址 */
    HAL_I2C_BusModeTypeDef BusMode;
} I2C_InitTypeDef;

typedef struct
{
    I2C_TypeDef *Instance;
    I2C_InitTypeDef Init;
    uint8_t PinGroup; /* 0 = PB13/PB12，1 = PB21/PB20；由 BSP 的 MspInit 使用 */

    uint8_t *pBuf; /* 中断传输上下文 */
    volatile uint16_t XferSize;
    volatile uint16_t XferCount;
    uint16_t DevAddr;
    uint16_t MemAddr;
    uint8_t MemAddrSize;    /* 1 或 2 */
    volatile uint8_t Phase; /* HAL_I2C_PHASE_xxx */
    volatile uint8_t State;
    volatile uint8_t ErrorCode;
} I2C_HandleTypeDef;

/* ===================== 初始化 ===================== */

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *hi2c);
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c);
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c);

/* ===================== 主机阻塞传输 ===================== */

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                          const uint8_t *buf, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                         uint8_t *buf, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                    uint16_t memAddr, uint8_t memAddrSize,
                                    const uint8_t *buf, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                   uint16_t memAddr, uint8_t memAddrSize,
                                   uint8_t *buf, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                        uint32_t trials, uint32_t timeout);

/* ===================== 主机中断传输 ===================== */

HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                             uint8_t *buf, uint16_t len);
HAL_StatusTypeDef HAL_I2C_Master_Receive_IT(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                            uint8_t *buf, uint16_t len);
HAL_StatusTypeDef HAL_I2C_Mem_Write_IT(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                       uint16_t memAddr, uint8_t memAddrSize,
                                       uint8_t *buf, uint16_t len);
HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                      uint16_t memAddr, uint8_t memAddrSize,
                                      uint8_t *buf, uint16_t len);
HAL_StatusTypeDef HAL_I2C_Abort_IT(I2C_HandleTypeDef *hi2c);

/* ===================== 状态查询 ===================== */

HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *hi2c);
uint8_t HAL_I2C_GetError(I2C_HandleTypeDef *hi2c);
uint32_t HAL_I2C_GetLastEvent(I2C_HandleTypeDef *hi2c);
uint8_t HAL_I2C_CheckEvent(I2C_HandleTypeDef *hi2c, uint32_t event);
void HAL_I2C_Reset(I2C_HandleTypeDef *hi2c);

/* ===================== 中断入口与回调 ===================== */

void HAL_I2C_IRQHandler(I2C_HandleTypeDef *hi2c);

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_I2C_H */
