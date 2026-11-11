#include "debug.h"

static UART_HandleTypeDef *s_debugHuart;

/**
 * @brief  绑定 printf 输出串口
 * @param  huart 已初始化的 UART 句柄，例如 &huart0
 * @note   需先调用对应的 BSP_UARTn_Init；未绑定时 printf 内容被丢弃。
 */
void BSP_DEBUG_Init(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    s_debugHuart = huart;
}

/**
 * @brief  解绑 printf 输出串口
 */
void BSP_DEBUG_DeInit(void)
{
    s_debugHuart = NULL;
}

/**
 * @brief  newlib 系统调用，printf 最终走到这里
 * @note   未绑定串口或参数非法时返回 size（假装写成功），
 *         避免 printf 内部反复重试。
 */
int _write(int fd, char *buf, int size)
{
    uint32_t remaining = (uint32_t)size;

    (void)fd;

    if ((s_debugHuart == NULL) || (buf == NULL) || (size <= 0)) {
        return size;
    }

    while (remaining != 0U) {
        uint16_t chunk = (remaining > 0xFFFFU) ? 0xFFFFU : (uint16_t)remaining;

        (void)HAL_UART_Transmit(s_debugHuart, (const uint8_t *)buf, chunk, HAL_MAX_DELAY);

        buf += chunk;
        remaining -= chunk;
    }

    return size;
}
