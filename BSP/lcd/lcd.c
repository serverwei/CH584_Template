#include "lcd.h"

LCD_HandleTypeDef hlcd;

typedef struct
{
    GPIO_TypeDef *Port;
    uint32_t Pin;
} BSP_LCD_PinTypeDef;

/* SEG0~SEG27 的引脚，下标就是段号 */
static const BSP_LCD_PinTypeDef s_lcdSeg[HAL_LCD_SEG_COUNT] =
    {
        {GPIOB, GPIO_PIN_0},  /* SEG0  与 UART0 CTS 冲突 */
        {GPIOB, GPIO_PIN_1},  /* SEG1  与 PWM7_ / DSR 冲突 */
        {GPIOB, GPIO_PIN_2},  /* SEG2  与 PWM8_ 冲突；默认是 RST# */
        {GPIOB, GPIO_PIN_3},  /* SEG3  与 PWM9_ / DCD 冲突 */
        {GPIOA, GPIO_PIN_0},  /* SEG4  与 SPI1 SCK1 冲突 */
        {GPIOA, GPIO_PIN_1},  /* SEG5  与 SPI1 MOSI1 冲突 */
        {GPIOA, GPIO_PIN_2},  /* SEG6  与 SPI1 MISO1 / TMR3 冲突 */
        {GPIOA, GPIO_PIN_3},  /* SEG7  */
        {GPIOB, GPIO_PIN_8},  /* SEG8  */
        {GPIOB, GPIO_PIN_9},  /* SEG9  */
        {GPIOB, GPIO_PIN_10}, /* SEG10 与 USB UD- 冲突 */
        {GPIOB, GPIO_PIN_11}, /* SEG11 与 USB UD+ 冲突 */
        {GPIOB, GPIO_PIN_12}, /* SEG12 与 SPI0 SCS_ / I2C SDA / UART1 RXD1_ 冲突 */
        {GPIOB, GPIO_PIN_13}, /* SEG13 与 SPI0 SCK0_ / I2C SCL / UART1 TXD1_ 冲突 */
        {GPIOB, GPIO_PIN_14}, /* SEG14 与 SPI0 MOSI_ / 调试口 TIO 冲突 */
        {GPIOB, GPIO_PIN_15}, /* SEG15 与 SPI0 MISO_ / 调试口 TCK 冲突 */
        {GPIOB, GPIO_PIN_16}, /* SEG16 与 NFC- 冲突 */
        {GPIOB, GPIO_PIN_17}, /* SEG17 与 NFC+ 冲突 */
        {GPIOB, GPIO_PIN_18}, /* SEG18 */
        {GPIOB, GPIO_PIN_19}, /* SEG19 */
        {GPIOB, GPIO_PIN_20}, /* SEG20 与 UART3_ / I2C 冲突 */
        {GPIOB, GPIO_PIN_21}, /* SEG21 与 UART3_ / I2C 冲突 */
        {GPIOB, GPIO_PIN_22}, /* SEG22 与 TMR3 / UART2 RXD2_ 冲突 */
        {GPIOB, GPIO_PIN_23}, /* SEG23 与 TMR0_ / PWM11 / RST# 冲突 */
        {GPIOA, GPIO_PIN_7},  /* SEG24 与 UART2 TXD2 / PWM5_ 冲突 */
        {GPIOA, GPIO_PIN_8},  /* SEG25 与 UART1 TXD1 冲突 */
        {GPIOA, GPIO_PIN_9},  /* SEG26 与 TMR0 / UART1 RXD1 冲突 */
        {GPIOA, GPIO_PIN_13}  /* SEG27 与 SPI0 SCK0 / PWM5 冲突 */
};

/* COM0~COM3，同时是 UART0 的默认引脚组 */
static const BSP_LCD_PinTypeDef s_lcdCom[HAL_LCD_COM_COUNT] =
    {
        {GPIOB, GPIO_PIN_4}, /* COM0  与 UART0 RXD0 / PWM7 冲突 */
        {GPIOB, GPIO_PIN_5}, /* COM1  与 UART0 DTR 冲突 */
        {GPIOB, GPIO_PIN_6}, /* COM2  与 UART0 RTS / PWM8 冲突 */
        {GPIOB, GPIO_PIN_7}  /* COM3  与 UART0 TXD0 / PWM9 冲突 */
};

/**
 * @brief  占用或释放一个 LCD 引脚
 * @param  pin 引脚
 * @param  s   ENABLE = 配成 LCD 模拟脚，DISABLE = 还原为普通浮空输入
 * @note   LCD 引脚必须是「浮空 + 关闭数字输入」的纯模拟脚，
 *         HAL_GPIO_SetDigitalInput(DISABLE) 会去置 R32_PIN_IN_DIS / R16_PIN_CONFIG
 *         里对应的位，正是手册要求的做法。
 */
static void BSP_LCD_ConfigPin(const BSP_LCD_PinTypeDef *pin, FunctionalState s)
{
    HAL_GPIO_SetMode(pin->Port, pin->Pin, HAL_GPIO_MODE_IN_FLOATING);

    if (s != DISABLE) {
        HAL_GPIO_SetDigitalInput(pin->Port, pin->Pin, DISABLE);
    } else {
        HAL_GPIO_SetDigitalInput(pin->Port, pin->Pin, ENABLE);
    }
}

/**
 * @brief  LCD 引脚初始化，由 HAL_LCD_Init / SetDuty / SetSegMask 调用
 * @param  hlcd 句柄，读取 Init.SegMask 与 Init.Duty
 * @note   可重复调用：SegMask 里没选的段、duty 用不到的 COM 会被释放，
 *         所以缩小配置时不会留下占着脚的僵尸段。
 *         COM 数量由 duty 决定：1/2→2 个，1/3→3 个，1/4→4 个。
 */
void HAL_LCD_MspInit(LCD_HandleTypeDef *hlcd)
{
    uint8_t comCount;
    uint8_t i;

    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return;
    }

    comCount = (uint8_t)((uint32_t)hlcd->Init.Duty + 2U);

    for (i = 0U; i < HAL_LCD_SEG_COUNT; i++) {
        if ((hlcd->Init.SegMask & (uint32_t)(1UL << i)) != 0U) {
            BSP_LCD_ConfigPin(&s_lcdSeg[i], ENABLE);
        } else {
            BSP_LCD_ConfigPin(&s_lcdSeg[i], DISABLE);
        }
    }

    for (i = 0U; i < HAL_LCD_COM_COUNT; i++) {
        BSP_LCD_ConfigPin(&s_lcdCom[i], (i < comCount) ? ENABLE : DISABLE);
    }
}

/**
 * @brief  LCD 引脚回收，由 HAL_LCD_DeInit 调用
 * @param  hlcd 句柄
 */
void HAL_LCD_MspDeInit(LCD_HandleTypeDef *hlcd)
{
    uint8_t i;

    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return;
    }

    for (i = 0U; i < HAL_LCD_SEG_COUNT; i++) {
        BSP_LCD_ConfigPin(&s_lcdSeg[i], DISABLE);
    }

    for (i = 0U; i < HAL_LCD_COM_COUNT; i++) {
        BSP_LCD_ConfigPin(&s_lcdCom[i], DISABLE);
    }
}

/**
 * @brief  初始化段式 LCD
 * @param  cfg 配置：电压、duty、bias、扫描频率、段掩码
 * @retval HAL_OK 成功；HAL_ERROR cfg 为空或参数非法
 * @note   与 PWM 不同，LCD 没有「配好但关着」的用法，所以这里
 *         Init 完就直接打开显示，并确保睡眠时钟门没被关掉。
 *         调用前必须保证 32K 时钟已开（HAL_RCC_InitClock 已包含）。
 */
HAL_StatusTypeDef BSP_LCD_Init(const BSP_LCD_ConfigTypeDef *cfg)
{
    if (cfg == NULL) {
        return HAL_ERROR;
    }

    hlcd.Instance     = LCDX;
    hlcd.Init.Drive   = cfg->Drive;
    hlcd.Init.Duty    = cfg->Duty;
    hlcd.Init.Bias    = cfg->Bias;
    hlcd.Init.Scan    = cfg->Scan;
    hlcd.Init.SegMask = (cfg->SegMask != 0U) ? cfg->SegMask : HAL_LCD_SEG_MASK_ALL;

    if (HAL_LCD_Init(&hlcd) != HAL_OK) {
        (void)HAL_LCD_DeInit(&hlcd);
        return HAL_ERROR;
    }

    HAL_LCD_SetClockOff(DISABLE); /* 打开 LCD 时钟 */
    HAL_LCD_Start(&hlcd);

    return HAL_OK;
}

/**
 * @brief  关闭段式 LCD 并释放全部引脚
 * @retval HAL_OK 成功
 */
HAL_StatusTypeDef BSP_LCD_DeInit(void)
{
    return HAL_LCD_DeInit(&hlcd);
}

/**
 * @brief  写一个段的 4 个 COM（只改影子）
 * @param  seg     段号 0~27
 * @param  comMask bit0~3 对应 COM0~COM3
 * @note   改完调 BSP_LCD_Refresh 才会上屏。
 */
void BSP_LCD_WriteSeg(uint8_t seg, uint8_t comMask)
{
    HAL_LCD_WriteSeg(&hlcd, seg, comMask);
}

/**
 * @brief  按字节索引批量写影子显存
 * @param  index 起始字节索引 0~13
 * @param  buf   段码首地址，低半字节是偶数段，高半字节是奇数段
 * @param  count 字节数
 */
void BSP_LCD_WriteBuffer(uint8_t index, const uint8_t *buf, uint8_t count)
{
    HAL_LCD_WriteBuffer(&hlcd, index, buf, count);
}

/**
 * @brief  清屏并立即上屏
 */
void BSP_LCD_Clear(void)
{
    HAL_LCD_Clear(&hlcd);
}

/**
 * @brief  把影子显存推到硬件
 */
void BSP_LCD_Refresh(void)
{
    HAL_LCD_Refresh(&hlcd);
}

/**
 * @brief  睡眠时是否关闭 LCD 时钟
 * @param  off ENABLE = 关闭（省电），DISABLE = 保持
 */
void BSP_LCD_SetClockOff(FunctionalState off)
{
    HAL_LCD_SetClockOff(off);
}
