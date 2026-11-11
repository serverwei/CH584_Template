#ifndef __CH58X_HAL_CLK_DEF_H
#define __CH58X_HAL_CLK_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

/* 系统时钟源，数值直接写入 R16_CLK_SYS_CFG。编码方式：
 *   0x00C0 = RB_CLK_SYS_MOD（bit7:6），系统时钟来源
 *            00 = 16M/32M 分频，01 = PLL/2 分频，10 = 16M/32M 直出，11 = 32K 直出
 *   0x0100 = RB_XROM_SCLK_SEL，XROM 用 624MHz
 *   0x0200 = RB_OSC32M_SEL，PLL/系统时钟取外部 32M 晶振（否则用内部 16M RC）
 *   0x001F = RB_CLK_PLL_DIV，PLL 或 32M 的输出分频
 * PLL 固定 624MHz，先 /2 再除 RB_CLK_PLL_DIV 得到 Fsys。
 */
typedef enum {
    HAL_RCC_CLK_32KHZ = 0xC0, /* 32K 直出 */

    HAL_RCC_CLK_HSI_16M  = (0x100 | 0x80), /* 内部 RC 16M 直出 */
    HAL_RCC_CLK_HSI_8M   = 0x02,           /* 16M / 8 = 8M */
    HAL_RCC_CLK_HSI_5_3M = 0x03,           /* 16M / 3 = 5.33M */
    HAL_RCC_CLK_HSI_4M   = 0x04,           /* 16M / 4 = 4M */
    HAL_RCC_CLK_HSI_2M   = 0x08,           /* 16M / 8 = 2M（分频字段，非倍频） */
    HAL_RCC_CLK_HSI_1M   = 0x10,           /* 16M / 16 = 1M */

    HAL_RCC_CLK_HSE_32M  = (0x100 | 0x200 | 0x80), /* 外部 32M 晶振直出 */
    HAL_RCC_CLK_HSE_16M  = (0x200 | 0x02),         /* 32M / 2 = 16M */
    HAL_RCC_CLK_HSE_8M   = (0x200 | 0x04),         /* 32M / 4 = 8M */
    HAL_RCC_CLK_HSE_6_4M = (0x200 | 0x05),         /* 32M / 5 = 6.4M */
    HAL_RCC_CLK_HSE_4M   = (0x200 | 0x08),         /* 32M / 8 = 4M */
    HAL_RCC_CLK_HSE_2M   = (0x200 | 0x10),         /* 32M / 16 = 2M */

    HAL_RCC_CLK_HSI_PLL_78M   = (0x100 | 0x40 | 4),  /* 624 / 2 / 4  = 78M，PLL 源为内部 16M RC */
    HAL_RCC_CLK_HSI_PLL_62_4M = (0x100 | 0x40 | 5),  /* 624 / 2 / 5  = 62.4M */
    HAL_RCC_CLK_HSI_PLL_52M   = (0x100 | 0x40 | 6),  /* 624 / 2 / 6  = 52M */
    HAL_RCC_CLK_HSI_PLL_39M   = (0x100 | 0x40 | 8),  /* 624 / 2 / 8  = 39M */
    HAL_RCC_CLK_HSI_PLL_26M   = (0x100 | 0x40 | 12), /* 624 / 2 / 12 = 26M */
    HAL_RCC_CLK_HSI_PLL_24M   = (0x100 | 0x40 | 13), /* 624 / 2 / 13 = 24M */
    HAL_RCC_CLK_HSI_PLL_19_5M = (0x100 | 0x40 | 16), /* 624 / 2 / 16 = 19.5M */
    HAL_RCC_CLK_HSI_PLL_13M   = (0x100 | 0x40 | 24), /* 624 / 2 / 24 = 13M */

    HAL_RCC_CLK_HSE_PLL_78M   = (0x300 | 0x40 | 4),  /* 624 / 2 / 4  = 78M，PLL 源为外部 32M */
    HAL_RCC_CLK_HSE_PLL_62_4M = (0x300 | 0x40 | 5),  /* 624 / 2 / 5  = 62.4M */
    HAL_RCC_CLK_HSE_PLL_52M   = (0x300 | 0x40 | 6),  /* 624 / 2 / 6  = 52M */
    HAL_RCC_CLK_HSE_PLL_39M   = (0x300 | 0x40 | 8),  /* 624 / 2 / 8  = 39M */
    HAL_RCC_CLK_HSE_PLL_26M   = (0x300 | 0x40 | 12), /* 624 / 2 / 12 = 26M */
    HAL_RCC_CLK_HSE_PLL_24M   = (0x300 | 0x40 | 13), /* 624 / 2 / 13 = 24M */
    HAL_RCC_CLK_HSE_PLL_19_5M = (0x300 | 0x40 | 16), /* 624 / 2 / 16 = 19.5M */
    HAL_RCC_CLK_HSE_PLL_13M   = (0x300 | 0x40 | 24)  /* 624 / 2 / 24 = 13M */
} HAL_RCC_SysClkTypeDef;

/* HSE 偏置电流挡位 */
typedef enum {
    HAL_RCC_HSE_RCUR_75 = 0,
    HAL_RCC_HSE_RCUR_100,
    HAL_RCC_HSE_RCUR_125,
    HAL_RCC_HSE_RCUR_150
} HAL_RCC_HSECurrentTypeDef;

/* HSE 内部负载电容挡位 */
typedef enum {
    HAL_RCC_HSE_CAP_10P = 0,
    HAL_RCC_HSE_CAP_12P,
    HAL_RCC_HSE_CAP_14P,
    HAL_RCC_HSE_CAP_16P,
    HAL_RCC_HSE_CAP_18P,
    HAL_RCC_HSE_CAP_20P,
    HAL_RCC_HSE_CAP_22P,
    HAL_RCC_HSE_CAP_24P,
    HAL_RCC_HSE_CAP_2P,
    HAL_RCC_HSE_CAP_4P,
    HAL_RCC_HSE_CAP_6P,
    HAL_RCC_HSE_CAP_8P
} HAL_RCC_HSECapTypeDef;

/* LSE 偏置电流挡位 */
typedef enum {
    HAL_RCC_LSE_RCUR_70 = 0,
    HAL_RCC_LSE_RCUR_100,
    HAL_RCC_LSE_RCUR_140,
    HAL_RCC_LSE_RCUR_200
} HAL_RCC_LSECurrentTypeDef;

/* LSI 偏置电流挡位 */
typedef enum {
    HAL_RCC_LSI_RCUR_70 = 0,
    HAL_RCC_LSI_RCUR_100,
    HAL_RCC_LSI_RCUR_140,
    HAL_RCC_LSI_RCUR_200
} HAL_RCC_LSICurrentTypeDef;

/* LSE 内部负载电容挡位，实际电容 = 12pF + 档位 */
typedef enum {
    HAL_RCC_LSE_CAP_12P = 0,
    HAL_RCC_LSE_CAP_13P,
    HAL_RCC_LSE_CAP_14P,
    HAL_RCC_LSE_CAP_15P,
    HAL_RCC_LSE_CAP_16P,
    HAL_RCC_LSE_CAP_17P,
    HAL_RCC_LSE_CAP_18P,
    HAL_RCC_LSE_CAP_19P,
    HAL_RCC_LSE_CAP_20P,
    HAL_RCC_LSE_CAP_21P,
    HAL_RCC_LSE_CAP_22P,
    HAL_RCC_LSE_CAP_23P,
    HAL_RCC_LSE_CAP_24P,
    HAL_RCC_LSE_CAP_25P,
    HAL_RCC_LSE_CAP_26P,
    HAL_RCC_LSE_CAP_27P
} HAL_RCC_LSECapTypeDef;

/* LSI 校准精度等级，等级越高耗时越长 */
typedef enum {
    HAL_RCC_LSI_CALIB_32 = 3,
    HAL_RCC_LSI_CALIB_64,
    HAL_RCC_LSI_CALIB_128,
    HAL_RCC_LSI_CALIB_1024
} HAL_RCC_LSICalibLevelTypeDef;

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_CLK_DEF_H */
