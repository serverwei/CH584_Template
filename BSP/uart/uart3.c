#include "uart3.h"
#include "uart.h"

UART_HandleTypeDef huart3;

/**
 * @brief  初始化 UART3
 * @param  baudrate 波特率，单位 bps，如 115200
 * @param  mode     收发方向，取 HAL_UART_MODE_TX / HAL_UART_MODE_RX / HAL_UART_MODE_TX_RX
 * @param  pinGroup 引脚组：0 = PA4(RXD)/PA5(TXD)，1 = PB20(RXD)/PB21(TXD)
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或波特率超出范围
 * @note   组 1 与 I2C 组 1 共用引脚，注意冲突。
 */
HAL_StatusTypeDef BSP_UART3_Init(uint32_t baudrate, uint32_t mode, uint8_t pinGroup)
{
    huart3.Instance         = UART3;
    huart3.Init.BaudRate    = baudrate;
    huart3.Init.WordLength  = HAL_UART_WORD_8B;
    huart3.Init.StopBits    = HAL_UART_STOP_1;
    huart3.Init.Parity      = HAL_UART_PAR_NONE;
    huart3.Init.FifoTrigger = HAL_UART_TRIG_4BYTE;
    huart3.Init.HwFlowCtl   = DISABLE;
    huart3.Init.Mode        = mode;
    huart3.Init.PinGroup    = pinGroup;

    return HAL_UART_Init(&huart3);
}

/**
 * @brief  关闭 UART3
 * @note   把 Init.Mode 声明过的引脚设为浮空输入并禁用数字输入，句柄的 Instance 置 NULL
 */
void BSP_UART3_DeInit(void)
{
    (void)HAL_UART_DeInit(&huart3);
}

/**
 * @brief  阻塞发送
 * @param  buf     待发送数据首地址
 * @param  len     发送字节数
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法或 Mode 不含 TX
 * @note   返回前会等最后一个字节真正移出移位寄存器
 */
HAL_StatusTypeDef BSP_UART3_Send(const uint8_t *buf, uint16_t len, uint32_t timeout)
{
    return HAL_UART_Transmit(&huart3, buf, len, timeout);
}

/**
 * @brief  启动中断接收
 * @param  buf 接收缓冲区，由调用方保证生命周期
 * @param  len 期望字节数，填满后关中断并触发 BSP_UART3_RxCpltCallback
 * @retval HAL_OK 成功；HAL_BUSY 上一次接收尚未完成；HAL_ERROR 参数非法或 Mode 不含 RX
 * @note   启动时会先清空 RX FIFO，避免旧字节混入新包
 */
HAL_StatusTypeDef BSP_UART3_Receive_IT(uint8_t *buf, uint16_t len)
{
    return HAL_UART_Receive_IT(&huart3, buf, len);
}

/* ===================== 中断钩子 =====================
 * 与 huart3 同文件；入口在 ch58x_hal_it.c，排除本文件时退回空实现
 */

/**
 * @brief  UART3 中断钩子，由 ch58x_hal_it.c 的 UART3_IRQHandler 调用
 */
__HIGH_CODE
void BSP_UART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart3);
}
