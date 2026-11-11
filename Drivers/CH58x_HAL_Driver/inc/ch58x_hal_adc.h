#ifndef __CH58X_HAL_ADC_H
#define __CH58X_HAL_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 寄存器块 =====================
 * 基址 0x40001058，全片只有一个 ADC。
 * 注意 R32_ADC_DMA_CTRL 那个 32 位字被拆成 4 个字节用：
 *   +0x08 保留、+0x09 CTRL_DMA、+0x0A DMA_IF、+0x0B AUTO_CYCLE
 * 所以下面按字节建模，不按 32 位字建模。
 *
 * 触摸按键是另一组寄存器（R8_TKEY_* @0x40001054~0x57），不在本块内。
 */
typedef struct
{
    volatile uint8_t CHANNEL;    /* +0x00 通道索引 [4:0] */
    volatile uint8_t CFG;        /* +0x01 时钟/PGA/测试/差分/BUF/电源 */
    volatile uint8_t CONVERT;    /* +0x02 启动/采样时间/GAIN2/EOC */
    volatile uint8_t TEM_SENSOR; /* +0x03 内部温感电源 */
    volatile uint16_t DATA;      /* +0x04 12 位结果（只读） */
    volatile uint8_t INT_FLAG;   /* +0x06 EOC 标志（只读，写 CONVERT 清零） */
    volatile uint8_t RESERVED0;  /* +0x07 */
    volatile uint8_t RESERVED1;  /* +0x08 */
    volatile uint8_t CTRL_DMA;   /* +0x09 = 0x40001061 */
    volatile uint8_t DMA_IF;     /* +0x0A */
    volatile uint8_t AUTO_CYCLE; /* +0x0B */
    volatile uint32_t DMA_NOW;   /* +0x0C */
    volatile uint32_t DMA_BEG;   /* +0x10 */
    volatile uint32_t DMA_END;   /* +0x14 */
    volatile uint32_t SCAN_CFG1; /* +0x18 CH1~CH8 */
    volatile uint32_t SCAN_CFG2; /* +0x1C CH9~CH13 + 扫描控制 */
} ADC_TypeDef;

#define ADCX ((ADC_TypeDef *)0x40001058U)

/* ===================== 常量 ===================== */

/* 外部通道数量（AIN0~AIN13），全部落在 PA 口 */
#define HAL_ADC_EXT_CH_COUNT 14U
#define HAL_ADC_EXT_CH_MASK  0x3FFFU

/* 12 位满量程 */
#define HAL_ADC_DATA_MAX 0x0FFFU

/* ===================== DMA 限制 =====================
 * 地址寄存器只有 bit16:1 有效（比 TMR 的 bit16:2 多一位），
 * 因为 ADC 结果是 16 位、DMA 步进 2 字节，不是 TMR 的 4 字节：
 *   缓冲区首地址必须 2 字节对齐；
 *   长度上限 0x1FFFE / 2 = 0xFFFF 个 uint16_t。
 */
#define HAL_ADC_DMA_ADDR_MASK 0x0001FFFEU
#define HAL_ADC_DMA_LEN_MAX   0xFFFFU

/* 内部参考电压，手册标称 1.05V ± 0.015V */
#define HAL_ADC_VREF_MV 1050

/* 25℃ 校准点所在的 ROM 信息地址 */
#define HAL_ADC_ROM_TMP_25C 0x7F014U

/* 句柄状态 */
#define HAL_ADC_STATE_RESET 0x00U
#define HAL_ADC_STATE_READY 0x01U

/* ===================== 枚举 ===================== */

/* 通道索引，对应 CHANNEL.RB_ADC_CH_INX */
typedef enum {
    HAL_ADC_CH_0 = 0,
    HAL_ADC_CH_1,
    HAL_ADC_CH_2,
    HAL_ADC_CH_3,
    HAL_ADC_CH_4,
    HAL_ADC_CH_5,
    HAL_ADC_CH_6,
    HAL_ADC_CH_7,
    HAL_ADC_CH_8,
    HAL_ADC_CH_9,
    HAL_ADC_CH_10,
    HAL_ADC_CH_11,
    HAL_ADC_CH_12,
    HAL_ADC_CH_13,
    HAL_ADC_CH_VBAT = 14, /* 内部电池电压 */
    HAL_ADC_CH_TEMP = 15, /* 内部温度传感器 */
    HAL_ADC_CH_NFC  = 16  /* NFC 能量检测 */
} HAL_ADC_ChannelTypeDef;

/* ChannelMask 的位掩码，bit n 对应 AINn。
 * 直接拿上面的通道枚举移位，不另维护一套编号，枚举改了这里自动跟着变。
 *
 *   单个通道：HAL_ADC_CH_MASK(HAL_ADC_CH_3)
 *   组合    ：HAL_ADC_CH_MASK(HAL_ADC_CH_3) | HAL_ADC_CH_MASK(HAL_ADC_CH_5)
 *   全部 14 路：HAL_ADC_CH_MASK_ALL
 *
 * 内部通道（VBAT / TEMP / NFC）不占引脚，写进掩码没有意义，HAL_ADC_Init 会拒绝。
 */
#define HAL_ADC_CH_MASK(ch) ((uint32_t)(1UL << (uint32_t)(ch)))
#define HAL_ADC_CH_MASK_ALL HAL_ADC_EXT_CH_MASK
/* 采样时钟分档，对应 CFG.RB_ADC_CLK_DIV。
 * 实际频率随 R16_CLK_SYS_CFG[9] 变：
 *   [9]=1（PLL/XROM 624M 路径）：DIV0 保留，其余 8/5.33/4 MHz
 *   [9]=0（16M/32M 直出）：      8 / 4 / 2.67 / 2 MHz
 * 最高 8MHz。
 */
typedef enum {
    HAL_ADC_CLK_DIV0 = 0,
    HAL_ADC_CLK_DIV1,
    HAL_ADC_CLK_DIV2,
    HAL_ADC_CLK_DIV3
} HAL_ADC_ClockTypeDef;

/* PGA 增益，三位一起编码：低 2 位进 CFG[5:4]，bit4 表示 GAIN2 进 CONVERT[1]。
 * 用温度传感器时 GAIN2 必须为 0，即只能用 0x00~0x03 这四档。
 */
typedef enum {
    HAL_ADC_GAIN_N12DB = 0x00, /* 1/4 倍 */
    HAL_ADC_GAIN_N6DB  = 0x01, /* 1/2 倍 */
    HAL_ADC_GAIN_0DB   = 0x02, /* 1 倍 */
    HAL_ADC_GAIN_P6DB  = 0x03, /* 2 倍 */
    HAL_ADC_GAIN_P12DB = 0x10, /* 4 倍 */
    HAL_ADC_GAIN_P18DB = 0x11, /* 8 倍 */
    HAL_ADC_GAIN_P24DB = 0x12  /* 16 倍 */
} HAL_ADC_GainTypeDef;

/* 采样时间，Tadc = Tsys / clk_div，对应 CONVERT[5:4] */
typedef enum {
    HAL_ADC_SAMPLE_4T = 0,
    HAL_ADC_SAMPLE_5T,
    HAL_ADC_SAMPLE_6T,
    HAL_ADC_SAMPLE_7T
} HAL_ADC_SampleTimeTypeDef;

/* DMA 地址循环模式，对应 CTRL_DMA.RB_ADC_DMA_LOOP(bit2) */
typedef enum {
    HAL_ADC_DMA_ONCE = 0,
    HAL_ADC_DMA_LOOP
} HAL_ADC_DMALoopTypeDef;

/* ===================== 初始化与句柄 ===================== */
typedef struct
{
    uint32_t ChannelMask; /* bit n = 占用 AINn 引脚，用 HAL_ADC_CH_MASK() 拼 */
    HAL_ADC_ClockTypeDef Clock;
    HAL_ADC_GainTypeDef Gain;
    HAL_ADC_SampleTimeTypeDef SampleTime;
    FunctionalState DiffMode; /* 差分输入，默认 DISABLE */
    FunctionalState BufferEn; /* 输入缓冲，源内阻 <1K 才可关，默认 ENABLE */
} ADC_InitTypeDef;

typedef struct
{
    ADC_TypeDef *Instance;
    ADC_InitTypeDef Init;
    volatile uint8_t State;
} ADC_HandleTypeDef;

/* ===================== 初始化 ===================== */

HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_DeInit(ADC_HandleTypeDef *hadc);

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc);
void HAL_ADC_MspDeInit(ADC_HandleTypeDef *hadc);

/* ===================== 配置 ===================== */

HAL_StatusTypeDef HAL_ADC_SetClock(ADC_HandleTypeDef *hadc, HAL_ADC_ClockTypeDef clk);
HAL_StatusTypeDef HAL_ADC_SetGain(ADC_HandleTypeDef *hadc, HAL_ADC_GainTypeDef gain);
HAL_StatusTypeDef HAL_ADC_SetSampleTime(ADC_HandleTypeDef *hadc, HAL_ADC_SampleTimeTypeDef t);
HAL_StatusTypeDef HAL_ADC_SetChannel(ADC_HandleTypeDef *hadc, HAL_ADC_ChannelTypeDef ch);

void HAL_ADC_PowerOn(ADC_HandleTypeDef *hadc);
void HAL_ADC_PowerOff(ADC_HandleTypeDef *hadc);

/* ===================== 单次转换 ===================== */

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t timeout);
uint16_t HAL_ADC_GetValue(const ADC_HandleTypeDef *hadc);

HAL_StatusTypeDef HAL_ADC_Read(ADC_HandleTypeDef *hadc, HAL_ADC_ChannelTypeDef ch,
                               uint16_t *value, uint32_t timeout);
int16_t HAL_ADC_CalibrateRough(ADC_HandleTypeDef *hadc);

/* ===================== DMA ===================== */

HAL_StatusTypeDef HAL_ADC_SetDMAIntervalUs(ADC_HandleTypeDef *hadc, uint32_t us);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, uint16_t *buf, uint32_t len,
                                    HAL_ADC_DMALoopTypeDef loop);
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *hadc);
uint32_t HAL_ADC_GetDMACount(ADC_HandleTypeDef *hadc);

void HAL_ADC_IRQHandler(ADC_HandleTypeDef *hadc);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc);

/* ===================== 内部通道 =====================
 * 两者都会先保存、用完再恢复 CHANNEL/CFG/CONVERT/TEM_SENSOR/TKEY_CFG 五个寄存器，
 * 所以可以在 BLE 回调等任意上下文调用，不会破坏别人的 ADC 配置。
 */

uint16_t HAL_ADC_ReadTempRaw(void);
uint16_t HAL_ADC_ReadVbatRaw(void);

/* ===================== 换算（纯计算） ===================== */

int32_t HAL_ADC_TempToCelsius(uint16_t raw);
int32_t HAL_ADC_RawToMillivolt(uint16_t raw, HAL_ADC_GainTypeDef gain);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_ADC_H */
