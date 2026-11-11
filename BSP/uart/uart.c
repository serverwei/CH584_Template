#include "uart.h"
#include "uart0.h"
#include "uart1.h"
#include "uart2.h"
#include "uart3.h"

typedef struct
{
    GPIO_TypeDef *RxPort;
    uint32_t RxPin;
    GPIO_TypeDef *TxPort;
    uint32_t TxPin;
} BSP_UART_PinTypeDef;

/* [实例][引脚组] */
static const BSP_UART_PinTypeDef s_uartPin[4][2] =
    {
        {/* UART0：组 0 = PB4/PB7，组 1 = PA15/PA14 */
         {GPIOB, GPIO_PIN_4, GPIOB, GPIO_PIN_7},
         {GPIOA, GPIO_PIN_15, GPIOA, GPIO_PIN_14}},
        {/* UART1：组 0 = PA8/PA9，组 1 = PB12/PB13 */
         {GPIOA, GPIO_PIN_8, GPIOA, GPIO_PIN_9},
         {GPIOB, GPIO_PIN_12, GPIOB, GPIO_PIN_13}},
        {/* UART2：组 0 = PA6/PA7，组 1 = PB22/PB23 */
         {GPIOA, GPIO_PIN_6, GPIOA, GPIO_PIN_7},
         {GPIOB, GPIO_PIN_22, GPIOB, GPIO_PIN_23}},
        {/* UART3：组 0 = PA4/PA5，组 1 = PB20/PB21 */
         {GPIOA, GPIO_PIN_4, GPIOA, GPIO_PIN_5},
         {GPIOB, GPIO_PIN_20, GPIOB, GPIO_PIN_21}}};

static const uint16_t s_uartAltBit[4] =
    {
        RB_PIN_UART0,
        RB_PIN_UART1,
        RB_PIN_UART2,
        RB_PIN_UART3};

/**
 * @brief  实例索引 0~3，非法实例返回 4
 */
static uint32_t BSP_UART_GetIndex(UART_TypeDef *uart)
{
    if (uart == UART0) {
        return 0U;
    }
    if (uart == UART1) {
        return 1U;
    }
    if (uart == UART2) {
        return 2U;
    }
    if (uart == UART3) {
        return 3U;
    }

    return 4U;
}

/**
 * @brief  UART 引脚初始化，由 HAL_UART_Init 调用
 * @param  huart 句柄，从其 Init 读 Mode 与 PinGroup
 * @note   只发模式不碰 RX 引脚、只收模式不碰 TX 引脚，未配置的引脚保持原状。
 *         PinGroup 非 0 时写 R16_PIN_ALTERNATE 切到备用引脚组。
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    const BSP_UART_PinTypeDef *pin;
    uint32_t idx;
    uint8_t group;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    idx = BSP_UART_GetIndex(huart->Instance);

    if (idx >= 4U) {
        return;
    }

    group = (huart->Init.PinGroup != 0U) ? 1U : 0U;
    pin   = &s_uartPin[idx][group];

    if (group != 0U) {
        HAL_GPIO_SetPeriphRemap(ENABLE, s_uartAltBit[idx]);
    } else {
        HAL_GPIO_SetPeriphRemap(DISABLE, s_uartAltBit[idx]);
    }

    if ((huart->Init.Mode & HAL_UART_MODE_RX) != 0U) {
        HAL_GPIO_SetMode(pin->RxPort, pin->RxPin, HAL_GPIO_MODE_IN_PULLUP);
        HAL_GPIO_SetDigitalInput(pin->RxPort, pin->RxPin, ENABLE);
    }

    if ((huart->Init.Mode & HAL_UART_MODE_TX) != 0U) {
        HAL_GPIO_SetMode(pin->TxPort, pin->TxPin, HAL_GPIO_MODE_OUT_PP_5MA);
        HAL_GPIO_SetDigitalInput(pin->TxPort, pin->TxPin, ENABLE);
    }
}

/**
 * @brief  UART 引脚回收，由 HAL_UART_DeInit 调用
 * @param  huart 句柄，从其 Init 读 Mode 与 PinGroup
 * @note   只回收 Init.Mode 声明过的引脚，设为浮空输入并禁用数字输入；
 *         未声明过的引脚不动，避免破坏被挪作它用的引脚。
 */
void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    const BSP_UART_PinTypeDef *pin;
    uint32_t idx;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    idx = BSP_UART_GetIndex(huart->Instance);

    if (idx >= 4U) {
        return;
    }

    pin = &s_uartPin[idx][(huart->Init.PinGroup != 0U) ? 1U : 0U];

    /* 只回收本实例在 Init.Mode 里声明过的引脚，其余保持原状 */
    if ((huart->Init.Mode & HAL_UART_MODE_RX) != 0U) {
        HAL_GPIO_SetMode(pin->RxPort, pin->RxPin, HAL_GPIO_MODE_IN_FLOATING);
        HAL_GPIO_SetDigitalInput(pin->RxPort, pin->RxPin, DISABLE);
    }

    if ((huart->Init.Mode & HAL_UART_MODE_TX) != 0U) {
        HAL_GPIO_SetMode(pin->TxPort, pin->TxPin, HAL_GPIO_MODE_IN_FLOATING);
        HAL_GPIO_SetDigitalInput(pin->TxPort, pin->TxPin, DISABLE);
    }
}

/**
 * @brief  接收完成回调，按实例分发到 BSP_UARTn_RxCpltCallback
 * @param  huart 触发回调的句柄，从其 Instance 判别是哪个实例
 * @note   覆盖 HAL 的 weak 实现，全工程只能有这一份
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    if (huart->Instance == UART0) {
        BSP_UART0_RxCpltCallback();
    } else if (huart->Instance == UART1) {
        BSP_UART1_RxCpltCallback();
    } else if (huart->Instance == UART2) {
        BSP_UART2_RxCpltCallback();
    } else if (huart->Instance == UART3) {
        BSP_UART3_RxCpltCallback();
    } else {
        /* 未知实例，丢弃 */
    }
}

/**
 * @brief  接收线路错误回调，按实例分发到 BSP_UARTn_ErrorCallback
 * @param  huart 触发回调的句柄，错误码取自 huart->ErrorLsr
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    if (huart->Instance == UART0) {
        BSP_UART0_ErrorCallback(huart->ErrorLsr);
    } else if (huart->Instance == UART1) {
        BSP_UART1_ErrorCallback(huart->ErrorLsr);
    } else if (huart->Instance == UART2) {
        BSP_UART2_ErrorCallback(huart->ErrorLsr);
    } else if (huart->Instance == UART3) {
        BSP_UART3_ErrorCallback(huart->ErrorLsr);
    } else {
        /* 未知实例，丢弃 */
    }
}

/* ===================== 默认回调 =====================
 * 8 个弱定义集中放在这里，不放各自的 uartn.c：
 * 上面的分发函数会无条件调用它们，若散落在 uartn.c 里，
 * 排除某个实例文件就会让 uart.c 出现 undefined reference。
 *
 * 分发在中断上下文执行。RxCplt 触发时 HAL 已关闭 RECV_RDY 中断，
 * 需要继续接收必须在覆盖的实现里重新调用 BSP_UARTn_Receive_IT。
 * ErrorCallback 收到的是 LSR 快照，回调里再读寄存器已读不到。
 */

__weak void BSP_UART0_RxCpltCallback(void)
{
}
__weak void BSP_UART1_RxCpltCallback(void)
{
}
__weak void BSP_UART2_RxCpltCallback(void)
{
}
__weak void BSP_UART3_RxCpltCallback(void)
{
}

__weak void BSP_UART0_ErrorCallback(uint8_t lsr)
{
    UNUSED(lsr);
}
__weak void BSP_UART1_ErrorCallback(uint8_t lsr)
{
    UNUSED(lsr);
}
__weak void BSP_UART2_ErrorCallback(uint8_t lsr)
{
    UNUSED(lsr);
}
__weak void BSP_UART3_ErrorCallback(uint8_t lsr)
{
    UNUSED(lsr);
}
