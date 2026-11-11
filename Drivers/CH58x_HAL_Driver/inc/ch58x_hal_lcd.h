#ifndef __CH58X_HAL_LCD_H
#define __CH58X_HAL_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 寄存器块 =====================
 * 段式 LCD 控制器，基址 0x40006000，全片只有一个实例。
 */
typedef struct
{
    volatile uint8_t CMD; /* +0x00 命令寄存器 */
    volatile uint8_t RESERVED0[3];
    volatile uint32_t RAM0;   /* +0x04 SEG0~SEG7 */
    volatile uint32_t RAM1;   /* +0x08 SEG8~SEG15 */
    volatile uint32_t RAM2;   /* +0x0C SEG16~SEG23 */
    volatile uint32_t RAM3;   /* +0x10 仅 [15:0] 有效，SEG24~SEG27 */
    volatile uint32_t SEG_EN; /* +0x14 SEG0~SEG27 使能 */
} LCD_TypeDef;

#define LCDX ((LCD_TypeDef *)0x40006000U)

/* ===================== 容量 =====================
 * 28 段 × 4 COM = 112 点。
 * 每段只占 4 bit（bit0~3 对应 COM0~COM3），所以一个字节装两段：
 *   低半字节 = 偶数段（SEG0/2/4...），高半字节 = 奇数段（SEG1/3/5...）
 * 整个显存用 4 个 32 位寄存器，实际只有 14 字节有效。
 *
 * 注意厂商 StdPeriphDriver 的 LCD_WriteData0~13 是「字节索引」不是段号，
 * 一个宏写的是两段，别按段号去用。
 */
#define HAL_LCD_SEG_COUNT 28U
#define HAL_LCD_COM_COUNT 4U
#define HAL_LCD_BUF_SIZE  14U

/* 一段的 4 个 COM 全亮 */
#define HAL_LCD_COM_ALL 0x0FU

/* 28 段全使能 */
#define HAL_LCD_SEG_MASK_ALL 0x0FFFFFFFU

/* 句柄状态 */
#define HAL_LCD_STATE_RESET 0x00U
#define HAL_LCD_STATE_READY 0x01U

/* ===================== 枚举 ===================== */

/* 驱动电压，对应 CMD.RB_LCD_VLCD_SEL */
typedef enum {
    HAL_LCD_DRIVE_3V3 = 0, /* VIO33 × 100% */
    HAL_LCD_DRIVE_2V5      /* VIO33 × 76% */
} HAL_LCD_DriveTypeDef;

/* 占空比，对应 CMD.RB_LCD_DUTY。同时决定占用几个 COM 引脚：
 * 1/2 → COM0~1，1/3 → COM0~2，1/4 → COM0~3
 */
typedef enum {
    HAL_LCD_DUTY_1_2 = 0,
    HAL_LCD_DUTY_1_3,
    HAL_LCD_DUTY_1_4
} HAL_LCD_DutyTypeDef;

/* 偏压比，对应 CMD.RB_LCD_BIAS */
typedef enum {
    HAL_LCD_BIAS_1_2 = 0,
    HAL_LCD_BIAS_1_3
} HAL_LCD_BiasTypeDef;

/* 扫描频率，对应 CMD.RB_LCD_SCAN_CLK。LCD 由 32K 驱动，上电前
 * 必须先由 RCC 配好 32K 时钟源（HAL_RCC_InitClock 已包含）。
 */
typedef enum {
    HAL_LCD_SCAN_256HZ = 0,
    HAL_LCD_SCAN_512HZ,
    HAL_LCD_SCAN_1KHZ,
    HAL_LCD_SCAN_128HZ
} HAL_LCD_ScanTypeDef;

/* ===================== 初始化与句柄 ===================== */
typedef struct
{
    HAL_LCD_DriveTypeDef Drive;
    HAL_LCD_DutyTypeDef Duty;
    HAL_LCD_BiasTypeDef Bias;
    HAL_LCD_ScanTypeDef Scan;
    uint32_t SegMask; /* bit n = 使用 SEGn，一般填 HAL_LCD_SEG_MASK_ALL */
} LCD_InitTypeDef;

/* 影子显存：所有 Write 接口只改 Buffer，HAL_LCD_Refresh 才推到硬件。
 * 这样一次刷屏是 4 次整字写，不会出现「半新半旧」的中间画面。
 */
typedef struct
{
    LCD_TypeDef *Instance;
    LCD_InitTypeDef Init;
    uint8_t Buffer[HAL_LCD_BUF_SIZE];
    volatile uint8_t State;
} LCD_HandleTypeDef;

/* ===================== 初始化 ===================== */

HAL_StatusTypeDef HAL_LCD_Init(LCD_HandleTypeDef *hlcd);
HAL_StatusTypeDef HAL_LCD_DeInit(LCD_HandleTypeDef *hlcd);

void HAL_LCD_MspInit(LCD_HandleTypeDef *hlcd);
void HAL_LCD_MspDeInit(LCD_HandleTypeDef *hlcd);

/* ===================== 开关 ===================== */

void HAL_LCD_Start(LCD_HandleTypeDef *hlcd);
void HAL_LCD_Stop(LCD_HandleTypeDef *hlcd);

/* ===================== 参数 =====================
 * 改 Duty 或 SegMask 会重新调用 MspInit 调整引脚占用。
 */

HAL_StatusTypeDef HAL_LCD_SetDrive(LCD_HandleTypeDef *hlcd, HAL_LCD_DriveTypeDef drive);
HAL_StatusTypeDef HAL_LCD_SetDuty(LCD_HandleTypeDef *hlcd, HAL_LCD_DutyTypeDef duty);
HAL_StatusTypeDef HAL_LCD_SetBias(LCD_HandleTypeDef *hlcd, HAL_LCD_BiasTypeDef bias);
HAL_StatusTypeDef HAL_LCD_SetScan(LCD_HandleTypeDef *hlcd, HAL_LCD_ScanTypeDef scan);

/* ===================== 段使能 ===================== */

void HAL_LCD_SetSegMask(LCD_HandleTypeDef *hlcd, uint32_t mask);
void HAL_LCD_SegEnable(LCD_HandleTypeDef *hlcd, uint8_t seg, FunctionalState s);

/* ===================== 影子显存 ===================== */

void HAL_LCD_WriteSeg(LCD_HandleTypeDef *hlcd, uint8_t seg, uint8_t comMask);
uint8_t HAL_LCD_ReadSeg(const LCD_HandleTypeDef *hlcd, uint8_t seg);
void HAL_LCD_WriteByte(LCD_HandleTypeDef *hlcd, uint8_t index, uint8_t data);
void HAL_LCD_WriteBuffer(LCD_HandleTypeDef *hlcd, uint8_t index,
                         const uint8_t *buf, uint8_t count);

/* 影子清零并立即推送硬件 */
void HAL_LCD_Clear(LCD_HandleTypeDef *hlcd);

/* 影子 → 硬件 RAM0~RAM3 */
void HAL_LCD_Refresh(LCD_HandleTypeDef *hlcd);

/* ===================== 低功耗 ===================== */

/* ENABLE = 睡眠时关闭 LCD 时钟（省电），DISABLE = 保持时钟 */
void HAL_LCD_SetClockOff(FunctionalState off);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_LCD_H */
