#ifndef __CH58X_HAL_SPI_H
#define __CH58X_HAL_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 寄存器块 =====================
 * SPI0 @0x40004000，SPI1 @0x40004400，两者字段布局一致。
 * 差异：SPI1 只有主机模式，没有 DMA、没有从机控制位。
 */
typedef struct
{
    volatile uint8_t CTRL_MOD;     /* +0x00 模式控制 */
    volatile uint8_t CTRL_CFG;     /* +0x01 配置 */
    volatile uint8_t INTER_EN;     /* +0x02 中断使能 */
    volatile uint8_t CLOCK_DIV;    /* +0x03 主机分频；从机模式下同址为 SLAVE_PRE 首字节预置值 */
    volatile uint8_t BUFFER;       /* +0x04 数据缓冲：读=最近一次交换到的字节，写=主机模式立即发 1 字节 */
    volatile uint8_t RUN_FLAG;     /* +0x05 工作状态（只读） */
    volatile uint8_t INT_FLAG;     /* +0x06 中断标志（写 1 清零） */
    volatile uint8_t FIFO_COUNT;   /* +0x07 FIFO 字节数（只读） */
    volatile uint32_t RESERVED0;   /* +0x08 ~ +0x0B */
    volatile uint16_t TOTAL_CNT;   /* +0x0C 收发总字节数，仅低 12 位有效 */
    volatile uint16_t RESERVED1;   /* +0x0E ~ +0x0F */
    volatile uint8_t FIFO;         /* +0x10 数据 FIFO：写=入队，读=出队 */
    volatile uint8_t RESERVED2[2]; /* +0x11 ~ +0x12 */
    volatile uint8_t FIFO_COUNT1;  /* +0x13 与 FIFO_COUNT 同值 */
    volatile uint32_t DMA_NOW;     /* +0x14 仅 SPI0 */
    volatile uint32_t DMA_BEG;     /* +0x18 仅 SPI0 */
    volatile uint32_t DMA_END;     /* +0x1C 仅 SPI0 */
} SPI_TypeDef;

#define SPI0 ((SPI_TypeDef *)0x40004000U)
#define SPI1 ((SPI_TypeDef *)0x40004400U)

/* ===================== 中断源 =====================
 * 直接沿用 SFR 的 RB_SPI_IE_xxx，写进 INTER_EN
 */
#define HAL_SPI_IT_CNT_END  RB_SPI_IE_CNT_END  /* 0x01 全部字节收发完成 */
#define HAL_SPI_IT_BYTE_END RB_SPI_IE_BYTE_END /* 0x02 单字节交换完成 */
#define HAL_SPI_IT_FIFO_HF  RB_SPI_IE_FIFO_HF  /* 0x04 FIFO 过半 */
#define HAL_SPI_IT_DMA_END  RB_SPI_IE_DMA_END  /* 0x08 DMA 完成，仅 SPI0 */
#define HAL_SPI_IT_FIFO_OV  RB_SPI_IE_FIFO_OV  /* 0x10 FIFO 溢出，仅从机 */
#define HAL_SPI_IT_FST_BYTE RB_SPI_IE_FST_BYTE /* 0x80 从机首字节，仅 SPI0 */
#define HAL_SPI_IT_ALL      0x9FU

/* ===================== 中断标志 =====================
 * INT_FLAG 里 bit5 保留、bit6 是只读的空闲状态 RB_SPI_FREE。
 * 判断"是不是本外设的中断"必须先与 HAL_SPI_FLAG_MASK 相与，
 * 否则总线空闲时 FREE=1 会被误当成有中断。
 */
#define HAL_SPI_FLAG_CNT_END  RB_SPI_IF_CNT_END  /* 0x01 写 1 清零 */
#define HAL_SPI_FLAG_BYTE_END RB_SPI_IF_BYTE_END /* 0x02 写 1 清零 */
#define HAL_SPI_FLAG_FIFO_HF  RB_SPI_IF_FIFO_HF  /* 0x04 写 1 清零 */
#define HAL_SPI_FLAG_DMA_END  RB_SPI_IF_DMA_END  /* 0x08 写 1 清零 */
#define HAL_SPI_FLAG_FIFO_OV  RB_SPI_IF_FIFO_OV  /* 0x10 写 1 清零 */
#define HAL_SPI_FLAG_FST_BYTE RB_SPI_IF_FST_BYTE /* 0x80 写 1 清零 */
#define HAL_SPI_FLAG_MASK     0x9FU

/* 只读状态位，不是中断标志，写 1 清不掉 */
#define HAL_SPI_STA_FREE       RB_SPI_FREE       /* 0x40 总线空闲 */
#define HAL_SPI_STA_FIFO_READY RB_SPI_FIFO_READY /* 0x20 FIFO 就绪 */

/* ===================== 容量与分频 ===================== */
#define HAL_SPI_FIFO_SIZE     SPI_FIFO_SIZE /* 8 字节 */
#define HAL_SPI_MAX_XFER      4095U         /* TOTAL_CNT 只有低 12 位 */
#define HAL_SPI_CLOCK_DIV_MIN 2U            /* 此时 Fsck = Fsys/2，须打开 MST_DLY_EN */
#define HAL_SPI_CLOCK_DIV_MAX 254U

/* DMA 缓冲区必须是 4 字节对齐（方案手册要求） */
#define HAL_SPI_DMA_ALIGN 4U

/* ===================== 枚举 ===================== */

/* 时钟空闲电平。硅片固定"上升沿采样、下降沿输出"，
 * 所以只有模式 0 与模式 3 两种组合可选。
 */
typedef enum {
    HAL_SPI_MODE_0 = 0, /* 空闲时 SCK 为低 */
    HAL_SPI_MODE_3      /* 空闲时 SCK 为高 */
} HAL_SPI_ModeTypeDef;

/* 位序，对应 CTRL_CFG.RB_SPI_BIT_ORDER */
typedef enum {
    HAL_SPI_BITORDER_MSB = 0, /* 高位在前 */
    HAL_SPI_BITORDER_LSB      /* 低位在前 */
} HAL_SPI_BitOrderTypeDef;

/* 主从角色。SPI1 只支持主机。 */
typedef enum {
    HAL_SPI_ROLE_MASTER = 0, /* 主机 */
    HAL_SPI_ROLE_SLAVE       /* 从机，仅 SPI0 */
} HAL_SPI_RoleTypeDef;

/* ===================== 句柄状态 ===================== */
#define HAL_SPI_STATE_RESET   0x00U
#define HAL_SPI_STATE_READY   0x01U
#define HAL_SPI_STATE_BUSY_TX 0x02U
#define HAL_SPI_STATE_BUSY_RX 0x03U

/* ===================== 初始化与句柄 ===================== */
typedef struct
{
    HAL_SPI_RoleTypeDef Role;
    HAL_SPI_ModeTypeDef Mode;
    HAL_SPI_BitOrderTypeDef BitOrder;
    uint8_t ClockDiv; /* 主机分频 2~254，Fsck = Fsys / ClockDiv；从机模式忽略 */
    uint8_t PinGroup; /* 0 = 默认引脚组，1 = 备用引脚组（仅 SPI0 有）；由 BSP 的 MspInit 使用 */
} SPI_InitTypeDef;

typedef struct
{
    SPI_TypeDef *Instance;
    SPI_InitTypeDef Init;
    const uint8_t *pTxBuf; /* NULL 表示没有正在进行的发送 */
    uint16_t TxSize;
    volatile uint16_t TxCount;
    uint8_t *pRxBuf; /* NULL 表示没有正在进行的接收 */
    uint16_t RxSize;
    volatile uint16_t RxCount;
    volatile uint8_t State;
} SPI_HandleTypeDef;

/* ===================== 初始化 ===================== */

HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *hspi);

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi);
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi);

/* ===================== 配置 ===================== */

HAL_StatusTypeDef HAL_SPI_SetClockDiv(SPI_HandleTypeDef *hspi, uint8_t div);
HAL_StatusTypeDef HAL_SPI_SetBaudRate(SPI_HandleTypeDef *hspi, uint32_t baudrate);
HAL_StatusTypeDef HAL_SPI_SetDataMode(SPI_HandleTypeDef *hspi,
                                      HAL_SPI_ModeTypeDef mode,
                                      HAL_SPI_BitOrderTypeDef bitOrder);
uint32_t HAL_SPI_GetClockFreq(const SPI_HandleTypeDef *hspi);

/* ===================== 阻塞收发 ===================== */

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, const uint8_t *buf,
                                   uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *buf,
                                  uint16_t len, uint32_t timeout);

/* ===================== 中断收发 ===================== */

HAL_StatusTypeDef HAL_SPI_Transmit_IT(SPI_HandleTypeDef *hspi, const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef HAL_SPI_Receive_IT(SPI_HandleTypeDef *hspi, uint8_t *buf, uint16_t len);
HAL_StatusTypeDef HAL_SPI_Abort_IT(SPI_HandleTypeDef *hspi);

void HAL_SPI_Enable_IT(SPI_HandleTypeDef *hspi, uint8_t it);
void HAL_SPI_Disable_IT(SPI_HandleTypeDef *hspi, uint8_t it);

/* ===================== DMA 收发（仅 SPI0） =====================
 * 缓冲区必须 4 字节对齐。是阻塞式接口：内部轮询 CNT_END 直到传完。
 */

HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *hspi, const uint8_t *buf,
                                       uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *hspi, uint8_t *buf,
                                      uint16_t len, uint32_t timeout);

/* ===================== 状态 ===================== */

uint8_t HAL_SPI_GetState(const SPI_HandleTypeDef *hspi);
uint8_t HAL_SPI_GetFifoCount(const SPI_HandleTypeDef *hspi);

/* ===================== 中断入口与回调 ===================== */

void HAL_SPI_IRQHandler(SPI_HandleTypeDef *hspi);
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi);
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi);
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_SPI_H */
