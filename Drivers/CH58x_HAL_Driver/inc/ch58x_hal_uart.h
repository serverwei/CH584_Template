#ifndef __CH58X_HAL_UART_H
#define __CH58X_HAL_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 寄存器块 =====================
 * 四个实例基址等距 0x400，字段布局一致
 * MSR / ADR 仅 UART0 有效
 */
typedef struct
{
    volatile uint8_t MCR; /* +0x00 调制解调器控制 */
    volatile uint8_t IER; /* +0x01 中断使能 */
    volatile uint8_t FCR; /* +0x02 FIFO 控制 */
    volatile uint8_t LCR; /* +0x03 线路控制 */
    volatile uint8_t IIR; /* +0x04 中断标志（只读） */
    volatile uint8_t LSR; /* +0x05 线路状态（只读，错误位 RZ 读清零） */
    volatile uint8_t MSR; /* +0x06 调制解调器状态（只读） */
    volatile uint8_t RESERVED0;
    volatile uint8_t DR; /* +0x08 读=RBR，写=THR */
    volatile uint8_t RESERVED1;
    volatile uint8_t RFC; /* +0x0A 接收 FIFO 计数（只读） */
    volatile uint8_t TFC; /* +0x0B 发送 FIFO 计数（只读） */
    volatile uint16_t DL; /* +0x0C 波特率除数 */
    volatile uint8_t DIV; /* +0x0E 预分频，1~128（写 0 表示 128） */
    volatile uint8_t ADR; /* +0x0F 从机地址 */
} UART_TypeDef;

#define UART0 ((UART_TypeDef *)0x40003000U)
#define UART1 ((UART_TypeDef *)0x40003400U)
#define UART2 ((UART_TypeDef *)0x40003800U)
#define UART3 ((UART_TypeDef *)0x40003C00U)

/* ===================== 模式 =====================
 * 只发模式下 RX 引脚无 PINMUX 断开位，仍连着 UART 输入，但可作 GPIO 使用
 */
#define HAL_UART_MODE_RX    0x01U
#define HAL_UART_MODE_TX    0x02U
#define HAL_UART_MODE_TX_RX (HAL_UART_MODE_TX | HAL_UART_MODE_RX)

/* ===================== 中断源 ===================== */
#define HAL_UART_IT_RECV_RDY  RB_IER_RECV_RDY  /* 0x01 接收数据可用 */
#define HAL_UART_IT_THR_EMPTY RB_IER_THR_EMPTY /* 0x02 发送保持寄存器空 */
#define HAL_UART_IT_LINE_STAT RB_IER_LINE_STAT /* 0x04 接收线路错误 */
#define HAL_UART_IT_MODEM_CHG RB_IER_MODEM_CHG /* 0x08 调制解调器状态变化，仅 UART0 */

/* ===================== 线路状态 ===================== */
#define HAL_UART_STA_DATA_RDY      RB_LSR_DATA_RDY
#define HAL_UART_STA_OVER_ERR      RB_LSR_OVER_ERR
#define HAL_UART_STA_PAR_ERR       RB_LSR_PAR_ERR
#define HAL_UART_STA_FRAME_ERR     RB_LSR_FRAME_ERR
#define HAL_UART_STA_BREAK_ERR     RB_LSR_BREAK_ERR
#define HAL_UART_STA_TX_FIFO_EMPTY RB_LSR_TX_FIFO_EMP
#define HAL_UART_STA_TX_ALL_EMPTY  RB_LSR_TX_ALL_EMP

/* ===================== 中断编码（IIR） =====================
 * 沿用 16550 标准编码，厂方代码未给出这些宏
 */
#define HAL_UART_IIR_MODEM     0x00U /* 调制解调器状态变化 */
#define HAL_UART_IIR_THR_EMPTY 0x02U /* 发送保持寄存器空 */
#define HAL_UART_IIR_RECV_RDY  0x04U /* 接收数据可用 */
#define HAL_UART_IIR_LINE_STAT 0x06U /* 接收线路错误 */
#define HAL_UART_IIR_TIMEOUT   0x0CU /* 接收 FIFO 超时 */

/* ===================== 枚举 ===================== */
/* 接收 FIFO 触发深度，对应 FCR.RB_FCR_FIFO_TRIG。
 * 收满该字节数才产生接收中断，用于降低中断频率。
 */
typedef enum {
    HAL_UART_TRIG_1BYTE = 0, /* 收到 1 字节即中断 */
    HAL_UART_TRIG_2BYTE,     /* 收到 2 字节中断 */
    HAL_UART_TRIG_4BYTE,     /* 收到 4 字节中断 */
    HAL_UART_TRIG_7BYTE      /* 收到 7 字节（FIFO 最深档）中断 */
} HAL_UART_TrigTypeDef;

/* 数据位宽，对应 LCR.RB_LCR_WORD_SZ */
typedef enum {
    HAL_UART_WORD_5B = 0, /* 5 位数据 */
    HAL_UART_WORD_6B,     /* 6 位数据 */
    HAL_UART_WORD_7B,     /* 7 位数据 */
    HAL_UART_WORD_8B      /* 8 位数据 */
} HAL_UART_WordLenTypeDef;

/* 停止位长度，对应 LCR.RB_LCR_STOP_BIT */
typedef enum {
    HAL_UART_STOP_1 = 0, /* 1 位停止位 */
    HAL_UART_STOP_2      /* 2 位停止位 */
} HAL_UART_StopBitsTypeDef;

/* 校验方式，对应 LCR.RB_LCR_PAR_EN 与 RB_LCR_PAR_MOD。
 * MARK / SPACE 是固定电平校验位，不做奇偶校验：
 *   MARK  校验位恒为 1
 *   SPACE 校验位恒为 0
 */
typedef enum {
    HAL_UART_PAR_NONE = 0, /* 无校验，PAR_EN=0 */
    HAL_UART_PAR_ODD,      /* 奇校验，PAR_MOD=00 */
    HAL_UART_PAR_EVEN,     /* 偶校验，PAR_MOD=01 */
    HAL_UART_PAR_MARK,     /* 校验位恒 1，PAR_MOD=10 */
    HAL_UART_PAR_SPACE     /* 校验位恒 0，PAR_MOD=11 */
} HAL_UART_ParityTypeDef;

/* ===================== 初始化与句柄 ===================== */
typedef struct
{
    uint32_t BaudRate;
    HAL_UART_WordLenTypeDef WordLength;
    HAL_UART_StopBitsTypeDef StopBits;
    HAL_UART_ParityTypeDef Parity;
    HAL_UART_TrigTypeDef FifoTrigger;
    FunctionalState HwFlowCtl; /* MCR.AU_FLOW_EN */
    uint32_t Mode;             /* HAL_UART_MODE_xxx */
    uint8_t PinGroup;          /* 0 = 默认引脚组，1 = 备用引脚组；由 BSP 的 MspInit 使用 */
} UART_InitTypeDef;

typedef struct
{
    UART_TypeDef *Instance;
    UART_InitTypeDef Init;
    uint8_t *pRxBuf; /* NULL 表示没有正在进行的接收 */
    uint16_t RxSize;
    volatile uint16_t RxCount;
    volatile uint8_t ErrorLsr; /* 中断里 LSR 快照，LSR 错误位是读清零 */
} UART_HandleTypeDef;

/* ===================== 初始化 ===================== */

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *huart);

void HAL_UART_MspInit(UART_HandleTypeDef *huart);
void HAL_UART_MspDeInit(UART_HandleTypeDef *huart);

/* ===================== 配置 ===================== */

HAL_StatusTypeDef HAL_UART_SetBaudRate(UART_HandleTypeDef *huart, uint32_t baudrate);
HAL_StatusTypeDef HAL_UART_SetFormat(UART_HandleTypeDef *huart,
                                     HAL_UART_WordLenTypeDef wordLen,
                                     HAL_UART_StopBitsTypeDef stopBits,
                                     HAL_UART_ParityTypeDef parity);
HAL_StatusTypeDef HAL_UART_SetMode(UART_HandleTypeDef *huart, uint32_t mode);
void HAL_UART_SetTrigger(UART_HandleTypeDef *huart, HAL_UART_TrigTypeDef trig);

/* ===================== 阻塞收发 ===================== */

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *buf,
                                    uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *buf,
                                   uint16_t len, uint32_t timeout);

/* ===================== 中断接收 ===================== */

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *buf, uint16_t len);
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *huart);

/* ===================== FIFO / 复位 / 状态 ===================== */

void HAL_UART_ClearRxFifo(UART_HandleTypeDef *huart);
void HAL_UART_ClearTxFifo(UART_HandleTypeDef *huart);
void HAL_UART_Reset(UART_HandleTypeDef *huart);
void HAL_UART_WaitTxComplete(UART_HandleTypeDef *huart, uint32_t timeout);
uint8_t HAL_UART_GetLineStatus(UART_HandleTypeDef *huart);
uint8_t HAL_UART_GetRxCount(UART_HandleTypeDef *huart);
uint8_t HAL_UART_GetTxCount(UART_HandleTypeDef *huart);

/* ===================== 中断入口与回调 ===================== */

void HAL_UART_IRQHandler(UART_HandleTypeDef *huart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_UART_H */
