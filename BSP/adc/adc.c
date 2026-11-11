#include "adc.h"

ADC_HandleTypeDef hadc;

typedef struct
{
    GPIO_TypeDef *Port;
    uint32_t Pin;
} BSP_ADC_PinTypeDef;

/* 下标就是 ADC 通道号，全部在 PA 口 */
static const BSP_ADC_PinTypeDef s_adcPin[HAL_ADC_EXT_CH_COUNT] =
    {
        {GPIOA, GPIO_PIN_4},  /* AIN0   */
        {GPIOA, GPIO_PIN_5},  /* AIN1   */
        {GPIOA, GPIO_PIN_12}, /* AIN2   与 SPI0 组0 SCS / PWM4 冲突 */
        {GPIOA, GPIO_PIN_13}, /* AIN3   与 SPI0 组0 SCK0 / PWM5 / SEG27 冲突 */
        {GPIOA, GPIO_PIN_14}, /* AIN4   与 SPI0 组0 MOSI / UART0 TXD0_ 冲突 */
        {GPIOA, GPIO_PIN_15}, /* AIN5   与 SPI0 组0 MISO / UART0 RXD0_ 冲突 */
        {GPIOA, GPIO_PIN_3},  /* AIN6   */
        {GPIOA, GPIO_PIN_2},  /* AIN7   与 SPI1 MISO1 / TMR3 / SEG6 冲突 */
        {GPIOA, GPIO_PIN_1},  /* AIN8   与 SPI1 MOSI1 / SEG5 冲突 */
        {GPIOA, GPIO_PIN_0},  /* AIN9   与 SPI1 SCK1 / SEG4 冲突 */
        {GPIOA, GPIO_PIN_6},  /* AIN10  与 UART2 RXD2 / PWM4_ 冲突 */
        {GPIOA, GPIO_PIN_7},  /* AIN11  与 UART2 TXD2 / PWM5_ / SEG24 冲突 */
        {GPIOA, GPIO_PIN_8},  /* AIN12  与 UART1 TXD1 / SEG25 冲突 */
        {GPIOA, GPIO_PIN_9}   /* AIN13  与 TMR0 / UART1 RXD1 / SEG26 冲突 */
};

/**
 * @brief  ADC 引脚初始化，由 HAL_ADC_Init 调用
 * @param  hadc 句柄，读取 Init.ChannelMask
 * @note   按 ChannelMask 逐位占用，未选中的脚不动。
 *         ADC 引脚只要浮空输入，没有专门的模拟使能位。
 */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    uint8_t i;

    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return;
    }

    for (i = 0U; i < HAL_ADC_EXT_CH_COUNT; i++) {
        if ((hadc->Init.ChannelMask & (uint32_t)(1UL << i)) != 0U) {
            HAL_GPIO_SetMode(s_adcPin[i].Port, s_adcPin[i].Pin, HAL_GPIO_MODE_IN_FLOATING);
        }
    }
}

/**
 * @brief  ADC 引脚回收，由 HAL_ADC_DeInit 调用
 * @param  hadc 句柄
 * @note   仍是浮空输入，只是不再算作 ADC 占用，可以和别的外设手工协商。
 */
void HAL_ADC_MspDeInit(ADC_HandleTypeDef *hadc)
{
    uint8_t i;

    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return;
    }

    for (i = 0U; i < HAL_ADC_EXT_CH_COUNT; i++) {
        if ((hadc->Init.ChannelMask & (uint32_t)(1UL << i)) != 0U) {
            HAL_GPIO_SetMode(s_adcPin[i].Port, s_adcPin[i].Pin, HAL_GPIO_MODE_IN_FLOATING);
        }
    }
}

/**
 * @brief  初始化 ADC
 * @param  cfg 配置：通道掩码、采样时钟、增益、采样时间
 * @retval HAL_OK 成功；HAL_ERROR cfg 为空或参数非法
 * @note   如果只是要用内部温感给 BLE 做校准，可以不调本函数，
 *         直接调 BSP_ADC_ReadTempRaw —— 它自带现场保存，不依赖初始化。
 */
HAL_StatusTypeDef BSP_ADC_Init(const BSP_ADC_ConfigTypeDef *cfg)
{
    if (cfg == NULL) {
        return HAL_ERROR;
    }

    hadc.Instance         = ADCX;
    hadc.Init.ChannelMask = cfg->ChannelMask;
    hadc.Init.Clock       = cfg->Clock;
    hadc.Init.Gain        = cfg->Gain;
    hadc.Init.SampleTime  = cfg->SampleTime;
    hadc.Init.DiffMode    = DISABLE;
    hadc.Init.BufferEn    = ENABLE;

    if (HAL_ADC_Init(&hadc) != HAL_OK) {
        (void)HAL_ADC_DeInit(&hadc);
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  关闭 ADC 并归还引脚
 * @retval HAL_OK 成功
 */
HAL_StatusTypeDef BSP_ADC_DeInit(void)
{
    return HAL_ADC_DeInit(&hadc);
}

/**
 * @brief  单通道阻塞采样
 * @param  channel 通道号 0~13（外部），也可以传内部通道
 * @param  value   结果输出，12 位原始值
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法
 */
HAL_StatusTypeDef BSP_ADC_Read(uint8_t channel, uint16_t *value, uint32_t timeout)
{
    return HAL_ADC_Read(&hadc, (HAL_ADC_ChannelTypeDef)channel, value, timeout);
}

/**
 * @brief  改增益
 * @param  gain 七档之一
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   换算 mV 时会读这里的当前值，改完记得用新增益换算。
 */
HAL_StatusTypeDef BSP_ADC_SetGain(HAL_ADC_GainTypeDef gain)
{
    return HAL_ADC_SetGain(&hadc, gain);
}

/**
 * @brief  读内部温度传感器原始值
 * @retval 12 位原始值
 * @note   自带现场保存，可以直接挂给 BLE 的 tsCB。
 */
uint16_t BSP_ADC_ReadTempRaw(void)
{
    return HAL_ADC_ReadTempRaw();
}

/**
 * @brief  读内部电池电压原始值
 * @retval 12 位原始值
 */
uint16_t BSP_ADC_ReadVbatRaw(void)
{
    return HAL_ADC_ReadVbatRaw();
}

/**
 * @brief  温度原始值转摄氏度
 * @param  raw 12 位原始值
 * @retval 摄氏度
 */
int32_t BSP_ADC_TempToCelsius(uint16_t raw)
{
    return HAL_ADC_TempToCelsius(raw);
}

/**
 * @brief  采样值转毫伏，使用当前配置的增益
 * @param  raw 12 位原始值
 * @retval 毫伏
 */
int32_t BSP_ADC_RawToMillivolt(uint16_t raw)
{
    return HAL_ADC_RawToMillivolt(raw, hadc.Init.Gain);
}

/* ===================== DMA ===================== */

/**
 * @brief  设置自动连续采样的间隔
 * @param  us 采样间隔，单位 us
 * @retval HAL_OK 成功；HAL_ERROR 超出硬件可表示范围
 * @note   32MHz 下可设约 0.5us ~ 128us，计算细节见 HAL_ADC_SetDMAIntervalUs。
 */
HAL_StatusTypeDef BSP_ADC_SetDMAIntervalUs(uint32_t us)
{
    return HAL_ADC_SetDMAIntervalUs(&hadc, us);
}

/**
 * @brief  启动自动连续采样 + DMA
 * @param  buf  接收缓冲区，必须 2 字节对齐
 * @param  len  元素个数（uint16_t）
 * @param  loop HAL_ADC_DMA_LOOP 时写满后回到开头覆盖
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   缓冲区由调用者提供，BSP 不持有。
 *         调用前先用 BSP_ADC_Init 选好通道，并用 BSP_ADC_SetDMAIntervalUs 设间隔。
 *         启动前硬件会丢弃首次转换，需要有效值的话缓冲区后移一位取。
 */
HAL_StatusTypeDef BSP_ADC_StartDMA(uint16_t *buf, uint32_t len, HAL_ADC_DMALoopTypeDef loop)
{
    return HAL_ADC_Start_DMA(&hadc, buf, len, loop);
}

/**
 * @brief  停止自动采样与 DMA，不动通道、增益等采样配置
 */
HAL_StatusTypeDef BSP_ADC_StopDMA(void)
{
    return HAL_ADC_Stop_DMA(&hadc);
}

/**
 * @brief  已转换的元素个数
 */
uint32_t BSP_ADC_GetDMACount(void)
{
    return HAL_ADC_GetDMACount(&hadc);
}

void BSP_ADC_IRQHandler(void)
{
    HAL_ADC_IRQHandler(&hadc);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
    UNUSED(h);
    BSP_ADC_ConvCpltCallback();
}

/**
 * @brief  DMA 搬完一轮后的回调
 * @note   单次模式下 HAL 已把完成中断使能关掉，想再采一轮需要重新调 BSP_ADC_StartDMA。
 */
__weak void BSP_ADC_ConvCpltCallback(void)
{
}
