#include "spi0.h"
#include "spi.h"

SPI_HandleTypeDef hspi0;

/* 软件片选引脚，由 BSP_SPI0_Init 填入；s_csPort 为 NULL 表示不管理片选 */
static GPIO_TypeDef *s_csPort;
static uint32_t s_csPin;

/**
 * @brief  初始化 SPI0
 * @param  cfg 配置：分频、模式、位序、引脚组、片选
 * @retval HAL_OK 成功；HAL_ERROR cfg 为空或分频越界
 * @note   固定为主机。芯片只支持模式 0/3，其余组合会被 HAL 拒绕。
 *         片选是软件控制的普通 GPIO（主机模式下硬件不驱动 SCS）：
 *         本函数把它配成推挽输出并拉到高电平（未选中），
 *         收发前后请调 BSP_SPI0_CS_Select / BSP_SPI0_CS_Release。
 */
HAL_StatusTypeDef BSP_SPI0_Init(const BSP_SPI_ConfigTypeDef *cfg)
{
    HAL_StatusTypeDef st;

    if (cfg == NULL) {
        return HAL_ERROR;
    }

    /* 先把片选拉到无效电平，再去配 SCK/MOSI，避免上电瞬间从设备误动作 */
    s_csPort = cfg->CsPort;
    s_csPin  = cfg->CsPin;

    if ((s_csPort != NULL) && (s_csPin != 0U)) {
        HAL_GPIO_WritePin(s_csPort, s_csPin, HAL_GPIO_PIN_SET);
        HAL_GPIO_SetMode(s_csPort, s_csPin, HAL_GPIO_MODE_OUT_PP_5MA);
        HAL_GPIO_SetDigitalInput(s_csPort, s_csPin, ENABLE);
    }

    hspi0.Instance      = SPI0;
    hspi0.Init.Role     = HAL_SPI_ROLE_MASTER;
    hspi0.Init.Mode     = cfg->Mode;
    hspi0.Init.BitOrder = cfg->BitOrder;
    hspi0.Init.ClockDiv = cfg->ClockDiv;
    hspi0.Init.PinGroup = cfg->PinGroup;

    st = HAL_SPI_Init(&hspi0);

    if (st != HAL_OK) {
        BSP_SPI0_DeInit();
    }

    return st;
}

/**
 * @brief  关闭 SPI0
 * @note   SCK/MOSI/MISO 设为浮空输入并禁用数字输入，句柄的 Instance 置 NULL；
 *         配置过的片选脚一并释放
 */
void BSP_SPI0_DeInit(void)
{
    if ((s_csPort != NULL) && (s_csPin != 0U)) {
        HAL_GPIO_WritePin(s_csPort, s_csPin, HAL_GPIO_PIN_SET);
        HAL_GPIO_SetMode(s_csPort, s_csPin, HAL_GPIO_MODE_IN_FLOATING);
        HAL_GPIO_SetDigitalInput(s_csPort, s_csPin, DISABLE);
        s_csPort = NULL;
        s_csPin  = 0U;
    }

    (void)HAL_SPI_DeInit(&hspi0);
}

/**
 * @brief  拉低片选，选中从设备
 * @note   配置里没给片选时什么都不做，同一份业务代码可以直接搬到多从机场景。
 */
void BSP_SPI0_CS_Select(void)
{
    if ((s_csPort != NULL) && (s_csPin != 0U)) {
        HAL_GPIO_WritePin(s_csPort, s_csPin, HAL_GPIO_PIN_RESET);
    }
}

/**
 * @brief  拉高片选，释放从设备
 */
void BSP_SPI0_CS_Release(void)
{
    if ((s_csPort != NULL) && (s_csPin != 0U)) {
        HAL_GPIO_WritePin(s_csPort, s_csPin, HAL_GPIO_PIN_SET);
    }
}

/**
 * @brief  阻塞发送
 * @param  buf     待发送数据首地址
 * @param  len     发送字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法；HAL_BUSY 上一笔未结束
 */
HAL_StatusTypeDef BSP_SPI0_Transmit(const uint8_t *buf, uint16_t len, uint32_t timeout)
{
    return HAL_SPI_Transmit(&hspi0, buf, len, timeout);
}

/**
 * @brief  阻塞接收
 * @param  buf     接收缓冲区
 * @param  len     接收字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时（已收到的字节仍写进了 buf）
 */
HAL_StatusTypeDef BSP_SPI0_Receive(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    return HAL_SPI_Receive(&hspi0, buf, len, timeout);
}

/**
 * @brief  DMA 阻塞发送
 * @param  buf     待发送数据首地址，必须 4 字节对齐
 * @param  len     发送字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法或缓冲区未对齐
 * @note   适合 8 字节以上的长包，CPU 不参与搬运；短包用 BSP_SPI0_Transmit 开销更小。
 */
HAL_StatusTypeDef BSP_SPI0_Transmit_DMA(const uint8_t *buf, uint16_t len, uint32_t timeout)
{
    return HAL_SPI_Transmit_DMA(&hspi0, buf, len, timeout);
}

/**
 * @brief  DMA 阻塞接收
 * @param  buf     接收缓冲区，必须 4 字节对齐
 * @param  len     接收字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法或缓冲区未对齐
 */
HAL_StatusTypeDef BSP_SPI0_Receive_DMA(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    return HAL_SPI_Receive_DMA(&hspi0, buf, len, timeout);
}

/**
 * @brief  启动中断发送
 * @param  buf 待发送数据首地址，由调用方保证生命周期
 * @param  len 发送字节数，最大 4095
 * @retval HAL_OK 成功；HAL_BUSY 上一笔未结束
 * @note   全部发完后触发 BSP_SPI0_TxCpltCallback
 */
HAL_StatusTypeDef BSP_SPI0_Transmit_IT(const uint8_t *buf, uint16_t len)
{
    return HAL_SPI_Transmit_IT(&hspi0, buf, len);
}

/**
 * @brief  启动中断接收
 * @param  buf 接收缓冲区，由调用方保证生命周期
 * @param  len 接收字节数，最大 4095
 * @retval HAL_OK 成功；HAL_BUSY 上一笔未结束
 * @note   收满 len 字节后触发 BSP_SPI0_RxCpltCallback
 */
HAL_StatusTypeDef BSP_SPI0_Receive_IT(uint8_t *buf, uint16_t len)
{
    return HAL_SPI_Receive_IT(&hspi0, buf, len);
}

/**
 * @brief  中止正在进行的中断收发
 * @retval HAL_OK 成功
 */
HAL_StatusTypeDef BSP_SPI0_Abort_IT(void)
{
    return HAL_SPI_Abort_IT(&hspi0);
}

/* ===================== 中断钩子 =====================
 * 与 hspi0 同文件；入口在 ch58x_hal_it.c，排除本文件时退回空实现
 */

/**
 * @brief  SPI0 中断钩子，由 ch58x_hal_it.c 的 SPI0_IRQHandler 调用
 */
__HIGH_CODE
void BSP_SPI0_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&hspi0);
}
