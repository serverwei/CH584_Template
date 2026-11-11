#include "ch58x_hal.h"

#include <stddef.h>

/* ===================== 寄存器布局校验 =====================
 * 期望偏移取自 CH585DS1.PDF 表 15-1（ADC 相关寄存器，基址 0x40001058）。
 * ADC 是字节/半字/字混排的模块，编译器很容易插入隐式填充，逐项钉死。
 * 注意 INT_FLAG 后有两个保留字节，CTRL_DMA 在 +0x09 而不是 +0x07。
 */
_Static_assert(offsetof(ADC_TypeDef, CHANNEL) == 0x00, "ADC_CHANNEL @ +0x00");
_Static_assert(offsetof(ADC_TypeDef, CFG) == 0x01, "ADC_CFG @ +0x01");
_Static_assert(offsetof(ADC_TypeDef, CONVERT) == 0x02, "ADC_CONVERT @ +0x02");
_Static_assert(offsetof(ADC_TypeDef, TEM_SENSOR) == 0x03, "TEM_SENSOR @ +0x03");
_Static_assert(offsetof(ADC_TypeDef, DATA) == 0x04, "ADC_DATA @ +0x04");
_Static_assert(offsetof(ADC_TypeDef, INT_FLAG) == 0x06, "ADC_INT_FLAG @ +0x06");
_Static_assert(offsetof(ADC_TypeDef, CTRL_DMA) == 0x09, "ADC_CTRL_DMA @ +0x09");
_Static_assert(offsetof(ADC_TypeDef, DMA_IF) == 0x0A, "ADC_DMA_IF @ +0x0A");
_Static_assert(offsetof(ADC_TypeDef, AUTO_CYCLE) == 0x0B, "ADC_AUTO_CYCLE @ +0x0B");
_Static_assert(offsetof(ADC_TypeDef, DMA_NOW) == 0x0C, "ADC_DMA_NOW @ +0x0C");
_Static_assert(offsetof(ADC_TypeDef, DMA_BEG) == 0x10, "ADC_DMA_BEG @ +0x10");
_Static_assert(offsetof(ADC_TypeDef, DMA_END) == 0x14, "ADC_DMA_END @ +0x14");
_Static_assert(offsetof(ADC_TypeDef, SCAN_CFG1) == 0x18, "ADC_SCAN_CFG1 @ +0x18");
_Static_assert(offsetof(ADC_TypeDef, SCAN_CFG2) == 0x1C, "ADC_SCAN_CFG2 @ +0x1C");
/* 内部通道采样时的忙等上限，纯粹防死循环用，正常几微秒就结束 */
#define HAL_ADC_POLL_GUARD 0x100000U

/* ===================== 内部工具 ===================== */

/**
 * @brief  超时判断
 * @param  start   起始时刻，单位 ms
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval 1 已超时；0 未超时
 */
static uint8_t HAL_ADC_IsTimeout(uint32_t start, uint32_t timeout)
{
    return (uint8_t)((timeout != HAL_MAX_DELAY) && ((HAL_GetTick() - start) >= timeout));
}

/**
 * @brief  增益枚举是否合法
 * @note   七档之外的值（比如 0x04~0x0F）不能写进寄存器
 */
static uint8_t HAL_ADC_GainValid(HAL_ADC_GainTypeDef gain)
{
    switch (gain) {
        case HAL_ADC_GAIN_N12DB:
        case HAL_ADC_GAIN_N6DB:
        case HAL_ADC_GAIN_0DB:
        case HAL_ADC_GAIN_P6DB:
        case HAL_ADC_GAIN_P12DB:
        case HAL_ADC_GAIN_P18DB:
        case HAL_ADC_GAIN_P24DB:
            return 1U;

        default:
            return 0U;
    }
}

/**
 * @brief  把增益写进 CFG[5:4] 与 CONVERT[1]
 * @note   增益是 3 位，拆在两个寄存器里：低 2 位进 CFG，bit2（GAIN2）进 CONVERT。
 *         写 CONVERT 会顺带清掉 EOC 标志，所以只在配置阶段调。
 */
static void HAL_ADC_ApplyGain(ADC_HandleTypeDef *hadc)
{
    uint8_t cfg  = hadc->Instance->CFG;
    uint8_t conv = hadc->Instance->CONVERT;

    cfg = (uint8_t)((cfg & (uint8_t)~RB_ADC_PGA_GAIN) |
                    (uint8_t)(((uint8_t)hadc->Init.Gain & 0x03U) << 4U));

    if (((uint8_t)hadc->Init.Gain & 0x10U) != 0U) {
        conv |= RB_ADC_PGA_GAIN2;
    } else {
        conv &= (uint8_t)~RB_ADC_PGA_GAIN2;
    }

    hadc->Instance->CFG     = cfg;
    hadc->Instance->CONVERT = conv;
}

/**
 * @brief  内部通道单次采样，自带现场保存
 * @param  ch   通道索引
 * @param  cfg  CFG 寄存器的目标值
 * @param  ts   内部温感电源状态（0 或 RB_TEM_SEN_PWR_ON）
 * @retval 12 位原始值
 * @note   触摸模块和 ADC 共用同一套模拟前端，所以连 R8_TKEY_CFG 一起存。
 *         这段代码会被 BLE 的 tsCB 在任意时刻调用，必须保证退出时寄存器原样。
 */
static uint16_t HAL_ADC_ReadInternal(uint8_t ch, uint8_t cfg, uint8_t ts)
{
    ADC_TypeDef *adc = ADCX;
    uint8_t tkey;
    uint8_t sensor;
    uint8_t oldCh;
    uint8_t oldCfg;
    uint8_t oldConv;
    uint16_t raw;
    uint32_t guard;

    tkey    = R8_TKEY_CFG;
    sensor  = adc->TEM_SENSOR;
    oldCh   = adc->CHANNEL;
    oldCfg  = adc->CFG;
    oldConv = adc->CONVERT;

    R8_TKEY_CFG &= (uint8_t)~RB_TKEY_PWR_ON;
    adc->TEM_SENSOR = ts;
    adc->CHANNEL    = ch;
    adc->CFG        = cfg;
    adc->CONVERT &= (uint8_t)~RB_ADC_PGA_GAIN2;

    adc->CONVERT |= RB_ADC_START;

    guard = HAL_ADC_POLL_GUARD;
    while (((adc->CONVERT & RB_ADC_START) != 0U) && (guard != 0U)) {
        guard--;
    }

    raw = (uint16_t)(adc->DATA & RB_ADC_DATA);

    adc->TEM_SENSOR = sensor;
    adc->CHANNEL    = oldCh;
    adc->CFG        = oldCfg;
    R8_TKEY_CFG     = tkey;
    adc->CONVERT    = oldConv;

    return raw;
}

/* ===================== 初始化 ===================== */

/**
 * @brief  初始化 ADC
 * @param  hadc 句柄，调用前需填好 Instance 与 Init
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   只配置不启动，采样请调 HAL_ADC_Read / HAL_ADC_Start。
 *         引脚由 HAL_ADC_MspInit 按 Init.ChannelMask 逐位占用，内部通道不占脚。
 */
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *hadc)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (hadc->Instance != ADCX) {
        return HAL_ERROR;
    }
    if ((hadc->Init.ChannelMask & (uint32_t)~HAL_ADC_EXT_CH_MASK) != 0U) {
        return HAL_ERROR;
    }
    if ((uint32_t)hadc->Init.Clock > (uint32_t)HAL_ADC_CLK_DIV3) {
        return HAL_ERROR;
    }
    if ((uint32_t)hadc->Init.SampleTime > (uint32_t)HAL_ADC_SAMPLE_7T) {
        return HAL_ERROR;
    }
    if (HAL_ADC_GainValid(hadc->Init.Gain) == 0U) {
        return HAL_ERROR;
    }

    hadc->State = HAL_ADC_STATE_RESET;

    HAL_ADC_MspInit(hadc);

    hadc->Instance->TEM_SENSOR = 0U; /* 温感默认断电，用到再开 */

    hadc->Instance->CFG = (uint8_t)(RB_ADC_POWER_ON |
                                    ((hadc->Init.BufferEn != DISABLE) ? RB_ADC_BUF_EN : 0U) |
                                    ((hadc->Init.DiffMode != DISABLE) ? RB_ADC_DIFF_EN : 0U) |
                                    (uint8_t)((uint8_t)hadc->Init.Clock << 6U));

    hadc->Instance->CONVERT = (uint8_t)((uint8_t)hadc->Init.SampleTime << 4U);

    HAL_ADC_ApplyGain(hadc);

    hadc->State = HAL_ADC_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  关闭 ADC 并归还引脚
 * @param  hadc 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_ADC_DeInit(ADC_HandleTypeDef *hadc)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }

    hadc->Instance->CFG        = 0U;
    hadc->Instance->CONVERT    = 0U;
    hadc->Instance->TEM_SENSOR = 0U;

    HAL_ADC_MspDeInit(hadc);

    hadc->State = HAL_ADC_STATE_RESET;

    return HAL_OK;
}

/* ===================== 配置 ===================== */

/**
 * @brief  设置采样时钟分档
 * @param  hadc 句柄
 * @param  clk  HAL_ADC_CLK_DIV0 ~ DIV3
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   分档对应的实际频率随 R16_CLK_SYS_CFG[9] 变，见头文件注释。
 */
HAL_StatusTypeDef HAL_ADC_SetClock(ADC_HandleTypeDef *hadc, HAL_ADC_ClockTypeDef clk)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)clk > (uint32_t)HAL_ADC_CLK_DIV3) {
        return HAL_ERROR;
    }

    hadc->Init.Clock = clk;

    hadc->Instance->CFG = (uint8_t)((hadc->Instance->CFG & (uint8_t)~RB_ADC_CLK_DIV) |
                                    (uint8_t)((uint8_t)clk << 6U));

    return HAL_OK;
}

/**
 * @brief  设置 PGA 增益
 * @param  hadc 句柄
 * @param  gain 七档之一
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   写 CONVERT 会清 EOC 标志，所以别在采样流程中间调。
 */
HAL_StatusTypeDef HAL_ADC_SetGain(ADC_HandleTypeDef *hadc, HAL_ADC_GainTypeDef gain)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (HAL_ADC_GainValid(gain) == 0U) {
        return HAL_ERROR;
    }

    hadc->Init.Gain = gain;
    HAL_ADC_ApplyGain(hadc);

    return HAL_OK;
}

/**
 * @brief  设置采样时间
 * @param  hadc 句柄
 * @param  t    HAL_ADC_SAMPLE_4T ~ 7T，单位 Tadc = Tsys / clk_div
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 */
HAL_StatusTypeDef HAL_ADC_SetSampleTime(ADC_HandleTypeDef *hadc, HAL_ADC_SampleTimeTypeDef t)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)t > (uint32_t)HAL_ADC_SAMPLE_7T) {
        return HAL_ERROR;
    }

    hadc->Init.SampleTime = t;

    hadc->Instance->CONVERT = (uint8_t)((hadc->Instance->CONVERT & (uint8_t)~RB_ADC_SAMPLE_TIME) |
                                        (uint8_t)((uint8_t)t << 4U));

    return HAL_OK;
}

/**
 * @brief  选择采样通道
 * @param  hadc 句柄
 * @param  ch   0~16
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 */
HAL_StatusTypeDef HAL_ADC_SetChannel(ADC_HandleTypeDef *hadc, HAL_ADC_ChannelTypeDef ch)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)ch > (uint32_t)HAL_ADC_CH_NFC) {
        return HAL_ERROR;
    }

    hadc->Instance->CHANNEL = (uint8_t)ch;

    return HAL_OK;
}

/**
 * @brief  设置自动连续采样的周期间隔
 * @param  hadc 句柄
 * @param  us   采样间隔，单位 us
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或超出可表示范围
 * @note   硬件计数单位是 16 个系统时钟：
 *           interval = (256 - AUTO_CYCLE) * 16 * Tsys
 *         可设范围 16*Tsys ~ 4096*Tsys，32MHz 下约 0.5us ~ 128us。
 *         只在 HAL_ADC_Start_DMA 之后才有意义。
 */
HAL_StatusTypeDef HAL_ADC_SetDMAIntervalUs(ADC_HandleTypeDef *hadc, uint32_t us)
{
    uint32_t ticks;

    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (us == 0U) {
        return HAL_ERROR;
    }

    /* ticks = us * Fsys / 16e6。先折成 kHz 再乘，避免 us 较大时中间量溢出 */
    ticks = (us * (HAL_RCC_SYSCLK_HZ / 1000U)) / 16000U;

    if (ticks == 0U) {
        ticks = 1U;
    }
    if (ticks > 256U) {
        return HAL_ERROR;
    }

    hadc->Instance->AUTO_CYCLE = (uint8_t)(256U - ticks);

    return HAL_OK;
}

/**
 * @brief  打开 ADC 电源
 */
void HAL_ADC_PowerOn(ADC_HandleTypeDef *hadc)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return;
    }

    hadc->Instance->CFG |= RB_ADC_POWER_ON;
}

/**
 * @brief  关闭 ADC 电源
 * @note   关电源不影响寄存器里的其它配置位
 */
void HAL_ADC_PowerOff(ADC_HandleTypeDef *hadc)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return;
    }

    hadc->Instance->CFG &= (uint8_t)~RB_ADC_POWER_ON;
}

/* ===================== 单次转换 ===================== */

/**
 * @brief  启动一次转换
 * @param  hadc 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 * @note   START 位在转换结束时由硬件自动清零。
 */
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }

    hadc->Instance->CONVERT |= RB_ADC_START;

    return HAL_OK;
}

/**
 * @brief  等待转换结束
 * @param  hadc    句柄
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 句柄或实例为空
 * @note   两个等待与厂商实现保持一致：
 *         先等 START 自动清零，再等 EOC_X 落下。
 *         SST 手册把 EOC_X 写成"1=完成"，但厂商所有例程都是
 *         `while (CONVERT & EOC_X)` 等待，按实际行为以厂商为准。
 */
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t timeout)
{
    uint32_t start;

    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }

    start = HAL_GetTick();

    while ((hadc->Instance->CONVERT & RB_ADC_START) != 0U) {
        if (HAL_ADC_IsTimeout(start, timeout) != 0U) {
            return HAL_TIMEOUT;
        }
    }

    while ((hadc->Instance->CONVERT & RB_ADC_EOC_X) != 0U) {
        if (HAL_ADC_IsTimeout(start, timeout) != 0U) {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

/**
 * @brief  读取上一次转换结果
 * @param  hadc 句柄
 * @retval 12 位原始值
 */
uint16_t HAL_ADC_GetValue(const ADC_HandleTypeDef *hadc)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return 0U;
    }

    return (uint16_t)(hadc->Instance->DATA & RB_ADC_DATA);
}

/**
 * @brief  单通道阻塞采样
 * @param  hadc    句柄
 * @param  ch      通道
 * @param  value   结果输出，12 位原始值
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法
 */
HAL_StatusTypeDef HAL_ADC_Read(ADC_HandleTypeDef *hadc, HAL_ADC_ChannelTypeDef ch,
                               uint16_t *value, uint32_t timeout)
{
    if ((hadc == NULL) || (hadc->Instance == NULL) || (value == NULL)) {
        return HAL_ERROR;
    }
    if (HAL_ADC_SetChannel(hadc, ch) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_ADC_Start(hadc) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_ADC_PollForConversion(hadc, timeout) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    *value = HAL_ADC_GetValue(hadc);

    return HAL_OK;
}

/**
 * @brief  采样数据粗调，取 16 次平均后算偏差
 * @param  hadc 句柄，需先完成 HAL_ADC_Init 并选好通道
 * @retval 偏差值，调用方自行从后续采样里减掉
 * @note   进测试模式后数据寄存器按位取反，所以读取时再取反回来。
 *         结束后恢复 CFG 与 CHANNEL。
 */
int16_t HAL_ADC_CalibrateRough(ADC_HandleTypeDef *hadc)
{
    ADC_TypeDef *adc;
    uint8_t ch;
    uint8_t cfg;
    uint32_t sum = 0U;
    uint32_t i;

    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return 0;
    }

    adc = hadc->Instance;

    ch  = adc->CHANNEL;
    cfg = adc->CFG;

    adc->CFG |= RB_ADC_OFS_TEST;
    adc->CFG &= (uint8_t)~RB_ADC_DIFF_EN;

    adc->CONVERT |= RB_ADC_START;
    while ((adc->CONVERT & RB_ADC_START) != 0U) {
    }

    for (i = 0U; i < 16U; i++) {
        adc->CONVERT |= RB_ADC_START;
        while ((adc->CONVERT & RB_ADC_START) != 0U) {
        }

        sum += (uint32_t)((~adc->DATA) & RB_ADC_DATA);
    }

    sum = (sum + 8U) >> 4U;

    adc->CFG     = cfg;
    adc->CHANNEL = ch;

    return (int16_t)(2048 - (int32_t)sum);
}

/* ===================== DMA ===================== */

/**
 * @brief  启动自动连续采样 + DMA
 * @param  hadc 句柄
 * @param  buf  接收缓冲区，必须 2 字节对齐
 * @param  len  元素个数（uint16_t），1 ~ HAL_ADC_DMA_LEN_MAX
 * @param  loop HAL_ADC_DMA_LOOP 时写满后回到开头覆盖
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   与厂商 ADC_StartAutoDMA 一致：置 AUTO_EN 后硬件按 AUTO_CYCLE 的间隔
 *         不停采样，每个结果由 DMA 直接写进缓冲区，CPU 不参与。
 *         所以调用前先用 HAL_ADC_SetDMAIntervalUs 设好间隔。
 *         换缓冲区直接再调本函数即可，内部会先停一次再重设地址。
 */
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, uint16_t *buf, uint32_t len,
                                    HAL_ADC_DMALoopTypeDef loop)
{
    ADC_TypeDef *adc;
    uint32_t base;
    uint32_t bytes;

    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U) || (len > HAL_ADC_DMA_LEN_MAX)) {
        return HAL_ERROR;
    }
    if (((uint32_t)buf & 0x01U) != 0U) {
        return HAL_ERROR;
    }
    if ((uint32_t)loop > (uint32_t)HAL_ADC_DMA_LOOP) {
        return HAL_ERROR;
    }

    adc   = hadc->Instance;
    base  = (uint32_t)buf & HAL_ADC_DMA_ADDR_MASK;
    bytes = len << 1;

    if ((base + bytes) > HAL_ADC_DMA_ADDR_MASK) {
        return HAL_ERROR;
    }

    /* 先停自动采样与 DMA，避免改地址的过程中继续写旧缓冲区 */
    adc->CTRL_DMA &= (uint8_t)~(RB_ADC_AUTO_EN | RB_ADC_IE_DMA_END |
                                RB_ADC_DMA_ENABLE | RB_ADC_DMA_LOOP);

    adc->DMA_BEG = base;
    adc->DMA_END = base + bytes; /* 结束地址不含 */

    adc->DMA_IF = (uint8_t)(RB_ADC_IF_DMA_END | RB_ADC_IF_END_ADC);

    adc->CTRL_DMA |= (uint8_t)(RB_ADC_DMA_ENABLE | RB_ADC_IE_DMA_END |
                               ((loop == HAL_ADC_DMA_LOOP) ? RB_ADC_DMA_LOOP : 0U));

    adc->CTRL_DMA |= RB_ADC_AUTO_EN;

    /* 优先级统一由 HAL_NVIC_InitPriorities 设置，这里只管开关 */
    PFIC_EnableIRQ(ADC_IRQn);

    return HAL_OK;
}

/**
 * @brief  停止自动采样与 DMA
 * @param  hadc 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 * @note   清掉 AUTO_EN / IE_DMA_END / DMA_ENABLE / DMA_LOOP 并清完成标志。
 *         不动通道、增益等采样配置，重新 Start 不需要重新 Init。
 */
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *hadc)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_ERROR;
    }

    hadc->Instance->CTRL_DMA &= (uint8_t)~(RB_ADC_AUTO_EN | RB_ADC_IE_DMA_END |
                                           RB_ADC_DMA_ENABLE | RB_ADC_DMA_LOOP);
    hadc->Instance->DMA_IF = (uint8_t)RB_ADC_IF_DMA_END;

    PFIC_DisableIRQ(ADC_IRQn);

    return HAL_OK;
}

/**
 * @brief  已转换的元素个数
 * @param  hadc 句柄
 * @retval 已转换的 uint16_t 个数；句柄为空返回 0
 * @note   地址步进是 2 字节，所以右移 1 位；循环模式下会周期性回绕。
 */
uint32_t HAL_ADC_GetDMACount(ADC_HandleTypeDef *hadc)
{
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return 0U;
    }

    return (hadc->Instance->DMA_NOW - hadc->Instance->DMA_BEG) >> 1;
}

/**
 * @brief  ADC 中断处理
 * @param  hadc 句柄，由 BSP 的 ADC_IRQHandler 传入
 * @note   单次模式搬完后 DMA_NOW 停在 DMA_END，只清标志会被硬件立刻重新置位，
 *         造成中断风暴。厂商 EVT 是在中断里重设 R32_ADC_DMA_BEG 再清；
 *         这里改为先关自动采样与完成中断，下次 HAL_ADC_Start_DMA 会重设地址。
 */
__HIGH_CODE
void HAL_ADC_IRQHandler(ADC_HandleTypeDef *hadc)
{
    ADC_TypeDef *adc;
    uint8_t flag;

    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return;
    }

    adc  = hadc->Instance;
    flag = adc->DMA_IF;

    if ((flag & RB_ADC_IF_DMA_END) != 0U) {
        if ((adc->CTRL_DMA & RB_ADC_DMA_LOOP) == 0U) {
            adc->CTRL_DMA &= (uint8_t)~(RB_ADC_AUTO_EN | RB_ADC_IE_DMA_END);
        }
        adc->DMA_IF = (uint8_t)RB_ADC_IF_DMA_END;
        HAL_ADC_ConvCpltCallback(hadc);
    }
}

__weak void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    UNUSED(hadc);
}

/* ===================== 内部通道 ===================== */

/**
 * @brief  读内部温度传感器原始值
 * @retval 12 位原始值
 * @note   自带现场保存，可在 BLE 的 tsCB 里直接调用。
 *         手册要求用 TS 时 GAIN[2:0] = 011b 且 GAIN2 = 0，即 6dB。
 */
uint16_t HAL_ADC_ReadTempRaw(void)
{
    return HAL_ADC_ReadInternal((uint8_t)HAL_ADC_CH_TEMP,
                                (uint8_t)(RB_ADC_POWER_ON | RB_ADC_DIFF_EN |
                                          (uint8_t)((uint8_t)HAL_ADC_GAIN_P6DB << 4U)),
                                RB_TEM_SEN_PWR_ON);
}

/**
 * @brief  读内部电池电压原始值
 * @retval 12 位原始值
 * @note   自带现场保存。厂商实现用 -12dB 且开输入缓冲，这里保持一致。
 */
uint16_t HAL_ADC_ReadVbatRaw(void)
{
    return HAL_ADC_ReadInternal((uint8_t)HAL_ADC_CH_VBAT,
                                (uint8_t)(RB_ADC_POWER_ON | RB_ADC_BUF_EN |
                                          (uint8_t)((uint8_t)HAL_ADC_GAIN_N12DB << 4U)),
                                0U);
}

/* ===================== 换算 ===================== */

/**
 * @brief  温度原始值转摄氏度
 * @param  raw 12 位原始值
 * @retval 摄氏度
 * @note   用 ROM 信息区里的 25℃ 校准点做单点校正：
 *         ROM_CFG_TMP_25C 高 16 位是 25℃ 对应的 ADC 值，低 16 位是当时的温度
 *         （为 0 时按 25℃ 处理）。线性系数 100/283 取自厂商实现。
 */
int32_t HAL_ADC_TempToCelsius(uint16_t raw)
{
    uint32_t c25;
    int32_t t25;
    int32_t raw25;

    c25   = *(volatile const uint32_t *)HAL_ADC_ROM_TMP_25C;
    t25   = (int32_t)((c25 >> 16) & 0xFFFFU);
    raw25 = (int32_t)(c25 & 0xFFFFU);

    if (t25 == 0) {
        t25 = 25;
    }

    return t25 + ((((int32_t)raw - raw25) * 100) / 283);
}

/**
 * @brief  单端采样值转毫伏
 * @param  raw  12 位原始值
 * @param  gain 采样时用的增益
 * @retval 毫伏
 * @note   公式取自手册表 15-2，Vref 按标称 1050mV。
 *         差分输入请用表 15-3，本函数只覆盖单端。
 */
int32_t HAL_ADC_RawToMillivolt(uint16_t raw, HAL_ADC_GainTypeDef gain)
{
    int32_t v = (int32_t)(raw & HAL_ADC_DATA_MAX);

    switch (gain) {
        case HAL_ADC_GAIN_N12DB:
            return ((v * HAL_ADC_VREF_MV + 256) / 512) - (3 * HAL_ADC_VREF_MV);

        case HAL_ADC_GAIN_N6DB:
            return ((v * HAL_ADC_VREF_MV + 512) / 1024) - HAL_ADC_VREF_MV;

        case HAL_ADC_GAIN_0DB:
            return (v * HAL_ADC_VREF_MV + 1024) / 2048;

        case HAL_ADC_GAIN_P6DB:
            return ((v * HAL_ADC_VREF_MV + 2048) / 4096) + (HAL_ADC_VREF_MV / 2);

        case HAL_ADC_GAIN_P12DB:
            return ((v * HAL_ADC_VREF_MV + 4096) / 8192) + (HAL_ADC_VREF_MV * 3 / 4);

        case HAL_ADC_GAIN_P18DB:
            return ((v * HAL_ADC_VREF_MV + 8192) / 16384) + (HAL_ADC_VREF_MV * 7 / 8);

        case HAL_ADC_GAIN_P24DB:
            return ((v * HAL_ADC_VREF_MV + 16384) / 32768) + (HAL_ADC_VREF_MV * 15 / 16);

        default:
            return 0;
    }
}

/* ===================== 默认 MSP ===================== */

__weak void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    UNUSED(hadc);
}

__weak void HAL_ADC_MspDeInit(ADC_HandleTypeDef *hadc)
{
    UNUSED(hadc);
}
