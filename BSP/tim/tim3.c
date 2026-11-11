#include "tim3.h"

TIM_HandleTypeDef htim3;

/**
 * @brief  初始化 TMR3 为定时模式
 * @param  period 周期计数值，最大 0x03FFFFFF
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   纯定时只做内部计数，不占用任何引脚；
 *         TMR3 同时被旧 HAL/RTC.c 用作 RTC 触发补偿，两者不能同时启用。
 */
HAL_StatusTypeDef BSP_TIM3_Init(uint32_t period)
{
    htim3.Instance       = TMR3;
    htim3.Init.Mode      = HAL_TIM_MODE_TIMER;
    htim3.Init.Period    = period;
    htim3.Init.PWMPolar  = HAL_TIM_PWM_POLAR_LOW;
    htim3.Init.PWMRepeat = HAL_TIM_PWM_REPEAT_1;
    htim3.Init.CapEdge   = HAL_TIM_CAP_EDGE_CHANGE;
    htim3.PinGroup       = 0U;

    return HAL_TIM_Init(&htim3);
}

/**
 * @brief  初始化 TMR3 为 PWM 输出模式
 * @param  period   PWM 周期计数值，最大 0x03FFFFFF
 * @param  pol      输出极性
 * @param  pinGroup 0 = PA2，1 = PB22（PB22 与 BOOT 脚、UART2 组 1 冲突）
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   初始化后还要调 HAL_TIM_PWM_Start(&htim3, pulse) 才会出波形
 */
HAL_StatusTypeDef BSP_TIM3_InitPWM(uint32_t period, HAL_TIM_PWMPolarTypeDef pol,
                                   uint8_t pinGroup)
{
    htim3.Instance       = TMR3;
    htim3.Init.Mode      = HAL_TIM_MODE_PWM;
    htim3.Init.Period    = period;
    htim3.Init.PWMPolar  = pol;
    htim3.Init.PWMRepeat = HAL_TIM_PWM_REPEAT_1;
    htim3.Init.CapEdge   = HAL_TIM_CAP_EDGE_CHANGE;
    htim3.PinGroup       = pinGroup;

    return HAL_TIM_Init(&htim3);
}

/**
 * @brief  关闭 TMR3，引脚还原为浮空输入
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef BSP_TIM3_DeInit(void)
{
    return HAL_TIM_DeInit(&htim3);
}

/**
 * @brief  启动计数，不开中断
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef BSP_TIM3_Start(void)
{
    return HAL_TIM_Base_Start(&htim3);
}

/**
 * @brief  启动计数并使能周期中断
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef BSP_TIM3_Start_IT(void)
{
    return HAL_TIM_Base_Start_IT(&htim3);
}

/**
 * @brief  停止计数，不动中断配置
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef BSP_TIM3_Stop(void)
{
    return HAL_TIM_Base_Stop(&htim3);
}

/**
 * @brief  停止计数并关闭周期中断
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef BSP_TIM3_Stop_IT(void)
{
    return HAL_TIM_Base_Stop_IT(&htim3);
}

/**
 * @brief  初始化 TMR3 为输入捕获模式
 * @param  edge     捕获边沿，取 HAL_TIM_CAP_EDGE_CHANGE / FALL / RISE
 * @param  timeout  两次捕获之间的超时计数值，计到该值触发 PeriodElapsedCallback
 * @param  pinGroup 0 = PA2，1 = PB22（PB22 与 BOOT 脚、UART2 组 1 冲突）
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   初始化后用 BSP_TIM3_CaptureStart 启动，
 *         捕获值经 BSP_TIM3_CaptureCallback 送达。
 */
HAL_StatusTypeDef BSP_TIM3_InitCapture(HAL_TIM_CapEdgeTypeDef edge, uint32_t timeout,
                                       uint8_t pinGroup, HAL_TIM_PinModeTypeDef pinMode)
{
    htim3.Instance       = TMR3;
    htim3.Init.Mode      = HAL_TIM_MODE_CAPTURE;
    htim3.Init.Period    = timeout;
    htim3.Init.PWMPolar  = HAL_TIM_PWM_POLAR_LOW;
    htim3.Init.PWMRepeat = HAL_TIM_PWM_REPEAT_1;
    htim3.Init.CapEdge   = edge;
    htim3.PinGroup       = pinGroup;
    htim3.PinMode        = pinMode;

    return HAL_TIM_Init(&htim3);
}

/**
 * @brief  初始化 TMR3 为外部边沿计数模式
 * @param  edge     计数边沿，取 HAL_TIM_CAP_EDGE_CHANGE / FALL / RISE
 * @param  overflow 计数值达到该值时触发溢出（CYC_END）中断
 * @param  pinGroup 0 = PA2，1 = PB22（PB22 与 BOOT 脚、UART2 组 1 冲突）
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   内部按厂商公式设 CNT_END = overflow + 2；
 *         当前计数值用 __HAL_TIM_GET_COUNTER(&htim3) 读。
 */
HAL_StatusTypeDef BSP_TIM3_InitCounter(HAL_TIM_CapEdgeTypeDef edge, uint32_t overflow,
                                       uint8_t pinGroup, HAL_TIM_PinModeTypeDef pinMode)
{
    htim3.Instance       = TMR3;
    htim3.Init.Mode      = HAL_TIM_MODE_COUNTER;
    htim3.Init.Period    = overflow + 2U;
    htim3.Init.PWMPolar  = HAL_TIM_PWM_POLAR_LOW;
    htim3.Init.PWMRepeat = HAL_TIM_PWM_REPEAT_1;
    htim3.Init.CapEdge   = edge;
    htim3.PinGroup       = pinGroup;
    htim3.PinMode        = pinMode;

    return HAL_TIM_Init(&htim3);
}

/**
 * @brief  启动输入捕获 / 外部计数
 * @retval HAL_OK 成功；HAL_ERROR 未初始化或模式不对
 */
HAL_StatusTypeDef BSP_TIM3_CaptureStart_IT(void)
{
    return HAL_TIM_Capture_Start_IT(&htim3);
}

/**
 * @brief  停止输入捕获 / 外部计数，引脚保持原状
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef BSP_TIM3_CaptureStop_IT(void)
{
    return HAL_TIM_Capture_Stop_IT(&htim3);
}

/* ===================== DMA ===================== */

/**
 * @brief  启动 PWM + DMA，用缓冲区内容连续更新占空比
 * @param  pulse 占空比数组，必须 4 字节对齐，每个元素低 26 位有效
 * @param  len   元素个数，1 ~ HAL_TIM_DMA_LEN_MAX
 * @param  loop  HAL_TIM_DMA_LOOP 时循环播放
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或未初始化为 PWM 模式
 * @note   缓冲区由调用者提供，BSP 不持有。
 */
HAL_StatusTypeDef BSP_TIM3_PWMStart_DMA(const uint32_t *pulse, uint32_t len,
                                        HAL_TIM_DMALoopTypeDef loop)
{
    return HAL_TIM_PWM_Start_DMA(&htim3, pulse, len, loop);
}

/**
 * @brief  启动捕获 / 计数 + DMA
 * @param  buf  接收缓冲区，必须 4 字节对齐
 * @param  len  元素个数，1 ~ HAL_TIM_DMA_LEN_MAX
 * @param  loop HAL_TIM_DMA_LOOP 时写满后回到开头覆盖
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或模式不对
 * @note   缓冲区前两个元素是无效数据，取数请从下标 2 开始。
 */
HAL_StatusTypeDef BSP_TIM3_CaptureStart_DMA(uint32_t *buf, uint32_t len,
                                            HAL_TIM_DMALoopTypeDef loop)
{
    return HAL_TIM_Capture_Start_DMA(&htim3, buf, len, loop);
}

/**
 * @brief  停止 DMA 通道，不影响 PWM 输出与计数
 */
HAL_StatusTypeDef BSP_TIM3_StopDMA(void)
{
    return HAL_TIM_Stop_DMA(&htim3);
}

/**
 * @brief  已搬运的元素个数
 */
uint32_t BSP_TIM3_GetDMACount(void)
{
    return HAL_TIM_GetDMACount(&htim3);
}

/* ===================== 中断钩子 =====================
 * 与 htim3 同文件；入口在 ch58x_hal_it.c，排除本文件时退回空实现
 */

/**
 * @brief  TMR3 中断钩子，由 ch58x_hal_it.c 的 TMR3_IRQHandler 调用
 */
__HIGH_CODE
void BSP_TMR3_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim3);
}
