#include "ch58x_hal.h"

#include <stddef.h>

/* ===================== 寄存器布局校验 =====================
 * 期望偏移取自 CH585DS1.PDF 表 9-1（UART0 寄存器，基址 0x40003000）。
 * CH585 的 RBR/THR 在 +0x08，与老 CH58x 的 +0x07 不同，别照搛旧代码。
 */
_Static_assert(offsetof(UART_TypeDef, MCR) == 0x00, "UART_MCR @ +0x00");
_Static_assert(offsetof(UART_TypeDef, IER) == 0x01, "UART_IER @ +0x01");
_Static_assert(offsetof(UART_TypeDef, FCR) == 0x02, "UART_FCR @ +0x02");
_Static_assert(offsetof(UART_TypeDef, LCR) == 0x03, "UART_LCR @ +0x03");
_Static_assert(offsetof(UART_TypeDef, IIR) == 0x04, "UART_IIR @ +0x04");
_Static_assert(offsetof(UART_TypeDef, LSR) == 0x05, "UART_LSR @ +0x05");
_Static_assert(offsetof(UART_TypeDef, MSR) == 0x06, "UART_MSR @ +0x06");
_Static_assert(offsetof(UART_TypeDef, DR) == 0x08, "UART_RBR/THR @ +0x08");
_Static_assert(offsetof(UART_TypeDef, RFC) == 0x0A, "UART_RFC @ +0x0A");
_Static_assert(offsetof(UART_TypeDef, TFC) == 0x0B, "UART_TFC @ +0x0B");
_Static_assert(offsetof(UART_TypeDef, DL) == 0x0C, "UART_DL @ +0x0C");
_Static_assert(offsetof(UART_TypeDef, DIV) == 0x0E, "UART_DIV @ +0x0E");
_Static_assert(offsetof(UART_TypeDef, ADR) == 0x0F, "UART_ADR @ +0x0F");
/* 四个实例基址等距 0x400 */
#define HAL_UART_BASE_FIRST 0x40003000U
#define HAL_UART_BASE_STEP  0x00000400U

static const uint32_t s_uartClk[4] =
    {
        HAL_PWR_SLPCLK_UART0,
        HAL_PWR_SLPCLK_UART1,
        HAL_PWR_SLPCLK_UART2,
        HAL_PWR_SLPCLK_UART3};

/**
 * @brief  实例是否合法
 * @param  uart 待检查的实例指针
 * @retval 1 合法（UART0~3），0 非法
 */
static uint8_t HAL_UART_IsValid(UART_TypeDef *uart)
{
    return ((uart == UART0) || (uart == UART1) || (uart == UART2) || (uart == UART3)) ? 1U : 0U;
}

/**
 * @brief  实例索引
 * @param  uart 实例指针
 * @retval 0~3 对应 UART0~3；非法实例返回越界值，调用前需先用 HAL_UART_IsValid 把关
 */
static uint32_t HAL_UART_GetIndex(UART_TypeDef *uart)
{
    return ((uint32_t)uart - HAL_UART_BASE_FIRST) / HAL_UART_BASE_STEP;
}

/**
 * @brief  实例对应的中断号
 * @param  uart 实例指针，需先经 HAL_UART_IsValid 校验
 */
static IRQn_Type HAL_UART_GetIRQn(UART_TypeDef *uart)
{
    switch (HAL_UART_GetIndex(uart)) {
        case 0U:
            return UART0_IRQn;

        case 1U:
            return UART1_IRQn;

        case 2U:
            return UART2_IRQn;

        default:
            return UART3_IRQn;
    }
}

/**
 * @brief  初始化参数范围校验
 * @param  init 待校验的初始化结构
 * @retval HAL_OK 全部合法；HAL_ERROR 有字段越界
 */
static HAL_StatusTypeDef HAL_UART_CheckInit(const UART_InitTypeDef *init)
{
    if (init->BaudRate == 0U) {
        return HAL_ERROR;
    }
    if ((uint32_t)init->WordLength > (uint32_t)HAL_UART_WORD_8B) {
        return HAL_ERROR;
    }
    if ((uint32_t)init->StopBits > (uint32_t)HAL_UART_STOP_2) {
        return HAL_ERROR;
    }
    if ((uint32_t)init->Parity > (uint32_t)HAL_UART_PAR_SPACE) {
        return HAL_ERROR;
    }
    if ((uint32_t)init->FifoTrigger > (uint32_t)HAL_UART_TRIG_7BYTE) {
        return HAL_ERROR;
    }
    if ((init->Mode == 0U) || ((init->Mode & ~(HAL_UART_MODE_RX | HAL_UART_MODE_TX)) != 0U)) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  中断使能/关闭
 * @param  huart 句柄
 * @param  it    中断源位掩码，取 HAL_UART_IT_*
 * @param  s     ENABLE 开中断，DISABLE 关中断
 * @note   厂方只在使能时置 MCR.INT_OE，关闭时不清；这里做成对称，
 *         但仅当 IER 归零时才收掉 INT_OE，避免误伤其它中断源。
 */
static void HAL_UART_ConfigIT(UART_HandleTypeDef *huart, uint8_t it, FunctionalState s)
{
    if (s != DISABLE) {
        huart->Instance->IER |= it;
        huart->Instance->MCR |= RB_MCR_INT_OE;
    } else {
        huart->Instance->IER &= (uint8_t)~it;

        if (huart->Instance->IER == 0U) {
            huart->Instance->MCR &= (uint8_t)~RB_MCR_INT_OE;
        }
    }
}

/* ===================== 初始化 ===================== */

/**
 * @brief  初始化 UART
 * @param  huart 句柄，调用前需填好 Instance 与 Init 全部字段
 * @retval HAL_OK 成功；HAL_ERROR 实例非法、参数越界或波特率超出范围
 * @note   流程：MspInit → 开时钟 → 外设复位 → 波特率 → FIFO → 帧格式
 *         → 流控 → 引脚使能 → 置空接收状态。
 */
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart)
{
    uint32_t idx;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (HAL_UART_IsValid(huart->Instance) == 0U) {
        return HAL_ERROR;
    }
    if (HAL_UART_CheckInit(&huart->Init) != HAL_OK) {
        return HAL_ERROR;
    }

    idx = HAL_UART_GetIndex(huart->Instance);

    HAL_UART_MspInit(huart);

    HAL_PWR_ConfigPeriphClock(ENABLE, s_uartClk[idx]);

    /* 外设中断源默认关闭，这里只把 NVIC 打通，真正的使能在 Receive_IT 里做。
     * 优先级由 HAL_NVIC_InitPriorities 统一设置，这里不再重复。
     */
    PFIC_EnableIRQ(HAL_UART_GetIRQn(huart->Instance));

    HAL_UART_Reset(huart);

    if (HAL_UART_SetBaudRate(huart, huart->Init.BaudRate) != HAL_OK) {
        return HAL_ERROR;
    }

    /* FIFO：触发点 + 清空 + 使能 */
    huart->Instance->FCR = (uint8_t)(((uint32_t)huart->Init.FifoTrigger << 6) |
                                     RB_FCR_TX_FIFO_CLR | RB_FCR_RX_FIFO_CLR | RB_FCR_FIFO_EN);

    if (HAL_UART_SetFormat(huart, huart->Init.WordLength,
                           huart->Init.StopBits, huart->Init.Parity) != HAL_OK) {
        return HAL_ERROR;
    }

    if (huart->Init.HwFlowCtl != DISABLE) {
        huart->Instance->MCR |= RB_MCR_AU_FLOW_EN;
    } else {
        huart->Instance->MCR &= (uint8_t)~RB_MCR_AU_FLOW_EN;
    }

    huart->Instance->IER = ((huart->Init.Mode & HAL_UART_MODE_TX) != 0U) ? RB_IER_TXD_EN : 0U;

    huart->pRxBuf   = NULL;
    huart->RxSize   = 0U;
    huart->RxCount  = 0U;
    huart->ErrorLsr = 0U;

    if ((huart->Init.Mode & HAL_UART_MODE_RX) == 0U) {
        HAL_UART_ClearRxFifo(huart);
    }

    return HAL_OK;
}

/**
 * @brief  关闭 UART
 * @param  huart 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空、实例非法
 * @note   关中断、外设复位、关时铟、调 MspDeInit 回收引脚，最后把 Instance 置 NULL。
 */
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *huart)
{
    uint32_t idx;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (HAL_UART_IsValid(huart->Instance) == 0U) {
        return HAL_ERROR;
    }

    idx = HAL_UART_GetIndex(huart->Instance);

    huart->Instance->IER = 0U;
    huart->Instance->MCR = 0U;
    HAL_UART_Reset(huart);

    PFIC_DisableIRQ(HAL_UART_GetIRQn(huart->Instance));

    HAL_PWR_ConfigPeriphClock(DISABLE, s_uartClk[idx]);

    HAL_UART_MspDeInit(huart);

    huart->pRxBuf   = NULL;
    huart->RxSize   = 0U;
    huart->RxCount  = 0U;
    huart->ErrorLsr = 0U;
    huart->Instance = NULL;

    return HAL_OK;
}

/* ===================== 配置 ===================== */

/**
 * @brief  波特率配置
 * @param  huart    句柄
 * @param  baudrate 波特率，单位 bps
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空、波特率为 0、过高或过低
 * @note   DL = round(SysClk / (8 * DIV * baudrate))，DIV 从 1 试到 128；
 *         DIV 寄存器写 0 表示 128 分频。
 *         依赖 HAL_Init 已启动 SysTick（HAL_RCC_GetSysClockFreq 返回非 0）。
 */
HAL_StatusTypeDef HAL_UART_SetBaudRate(UART_HandleTypeDef *huart, uint32_t baudrate)
{
    uint32_t sysClk;
    uint32_t div;
    uint32_t dl = 0U;
    uint32_t denom;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (baudrate == 0U) {
        return HAL_ERROR;
    }

    sysClk = HAL_RCC_GetSysClockFreq();

    if (sysClk == 0U) {
        return HAL_ERROR;
    }

    for (div = 1U; div <= 128U; div++) {
        if (baudrate > (0xFFFFFFFFU / (8U * div))) {
            break; /* 溢出保护 */
        }

        denom = 8U * div * baudrate;
        dl    = (sysClk + (denom / 2U)) / denom; /* sysClk + denom/2 不会超过 32 位 */

        if ((dl >= 1U) && (dl <= 0xFFFFU)) {
            break;
        }
    }

    if (div > 128U) {
        return HAL_ERROR; /* 波特率过低 */
    }

    huart->Instance->DIV = (div == 128U) ? 0U : (uint8_t)div;
    huart->Instance->DL  = (uint16_t)dl;
    huart->Init.BaudRate = baudrate;

    return HAL_OK;
}

/**
 * @brief  帧格式配置
 * @param  huart    句柄
 * @param  wordLen  数据位，HAL_UART_WORD_5B ~ HAL_UART_WORD_8B
 * @param  stopBits 停止位，HAL_UART_STOP_1 / HAL_UART_STOP_2
 * @param  parity   校验，HAL_UART_PAR_NONE / ODD / EVEN / MARK / SPACE
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空或枚举越界
 * @note   只改 LCR 里的数据位 / 停止位 / 校验位，不影响其它位。
 */
HAL_StatusTypeDef HAL_UART_SetFormat(UART_HandleTypeDef *huart,
                                     HAL_UART_WordLenTypeDef wordLen,
                                     HAL_UART_StopBitsTypeDef stopBits,
                                     HAL_UART_ParityTypeDef parity)
{
    uint8_t lcr;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)wordLen > (uint32_t)HAL_UART_WORD_8B) {
        return HAL_ERROR;
    }
    if ((uint32_t)stopBits > (uint32_t)HAL_UART_STOP_2) {
        return HAL_ERROR;
    }
    if ((uint32_t)parity > (uint32_t)HAL_UART_PAR_SPACE) {
        return HAL_ERROR;
    }

    lcr = huart->Instance->LCR;
    lcr &= (uint8_t)~(RB_LCR_WORD_SZ | RB_LCR_STOP_BIT | RB_LCR_PAR_EN | RB_LCR_PAR_MOD);
    lcr |= (uint8_t)wordLen;

    if (stopBits == HAL_UART_STOP_2) {
        lcr |= RB_LCR_STOP_BIT;
    }

    switch (parity) {
        case HAL_UART_PAR_ODD:
            lcr |= RB_LCR_PAR_EN;
            break;

        case HAL_UART_PAR_EVEN:
            lcr |= (uint8_t)(RB_LCR_PAR_EN | 0x10U);
            break;

        case HAL_UART_PAR_MARK:
            lcr |= (uint8_t)(RB_LCR_PAR_EN | 0x20U);
            break;

        case HAL_UART_PAR_SPACE:
            lcr |= (uint8_t)(RB_LCR_PAR_EN | 0x30U);
            break;

        default:
            break;
    }

    huart->Instance->LCR = lcr;

    huart->Init.WordLength = wordLen;
    huart->Init.StopBits   = stopBits;
    huart->Init.Parity     = parity;

    return HAL_OK;
}

/**
 * @brief  收发方向切换
 * @param  huart 句柄
 * @param  mode  HAL_UART_MODE_TX / HAL_UART_MODE_RX / HAL_UART_MODE_TX_RX
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空或 mode 非法
 * @note   只发模式下没有 PINMUX 断开位，RX 引脚仍连着 UART 输入，
 *         但可挪作 GPIO：作输入无影响，作输出会有回环垃圾数据，
 *         彻底断开可用 HAL_GPIO_SetDigitalInput(..., DISABLE) 禁用数字输入。
 */
HAL_StatusTypeDef HAL_UART_SetMode(UART_HandleTypeDef *huart, uint32_t mode)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((mode == 0U) || ((mode & ~(HAL_UART_MODE_RX | HAL_UART_MODE_TX)) != 0U)) {
        return HAL_ERROR;
    }

    if ((mode & HAL_UART_MODE_TX) != 0U) {
        huart->Instance->IER |= RB_IER_TXD_EN;
    } else {
        huart->Instance->IER &= (uint8_t)~RB_IER_TXD_EN;
    }

    if ((mode & HAL_UART_MODE_RX) == 0U) {
        HAL_UART_ConfigIT(huart, HAL_UART_IT_RECV_RDY, DISABLE);
        HAL_UART_ClearRxFifo(huart);

        huart->pRxBuf  = NULL;
        huart->RxSize  = 0U;
        huart->RxCount = 0U;
    }

    huart->Init.Mode = mode;

    return HAL_OK;
}

/**
 * @brief  FIFO 触发点配置
 * @param  huart 句柄
 * @param  trig  触发字节数，HAL_UART_TRIG_1BYTE / 2BYTE / 4BYTE / 7BYTE
 * @note   只改 FCR 的触发位，不影响 FIFO 使能与清空位；参数越界时直接返回。
 */
void HAL_UART_SetTrigger(UART_HandleTypeDef *huart, HAL_UART_TrigTypeDef trig)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }
    if ((uint32_t)trig > (uint32_t)HAL_UART_TRIG_7BYTE) {
        return;
    }

    huart->Instance->FCR = (uint8_t)((huart->Instance->FCR & (uint8_t)~RB_FCR_FIFO_TRIG) |
                                     ((uint32_t)trig << 6));

    huart->Init.FifoTrigger = trig;
}

/* ===================== 阻塞收发 ===================== */

/**
 * @brief  阻塞发送
 * @param  huart   句柄
 * @param  buf     待发送数据首地址
 * @param  len     发送字节数
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 等发送 FIFO 腾空超时；
 *         HAL_ERROR 句柄/缓冲为空、len 为 0 或 Mode 不含 TX
 * @note   返回前会等 LSR.TX_ALL_EMP，保证最后一个字节已移出移位寄存器；
 *         低功耗场景必须等，否则进休眠会丢数据。
 */
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *buf,
                                    uint16_t len, uint32_t timeout)
{
    UART_TypeDef *uart;
    uint32_t start;
    uint16_t i;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((huart->Init.Mode & HAL_UART_MODE_TX) == 0U) {
        return HAL_ERROR;
    }

    uart  = huart->Instance;
    start = HAL_GetTick();

    for (i = 0U; i < len; i++) {
        while (uart->TFC >= UART_FIFO_SIZE) {
            if ((timeout != HAL_MAX_DELAY) && ((HAL_GetTick() - start) >= timeout)) {
                return HAL_TIMEOUT;
            }
        }

        uart->DR = buf[i];
    }

    HAL_UART_WaitTxComplete(huart, timeout);

    return HAL_OK;
}

/**
 * @brief  阻塞接收
 * @param  huart   句柄
 * @param  buf     接收缓冲区
 * @param  len     接收字节数
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 等数据超时；
 *         HAL_ERROR 句柄/缓冲为空、len 为 0 或 Mode 不含 RX
 * @note   轮询 RFC 直到收满 len 字节；超时后已收的字节已写入 buf，但返回 HAL_TIMEOUT。
 */
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *buf,
                                   uint16_t len, uint32_t timeout)
{
    UART_TypeDef *uart;
    uint32_t start;
    uint16_t i;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((huart->Init.Mode & HAL_UART_MODE_RX) == 0U) {
        return HAL_ERROR;
    }

    uart  = huart->Instance;
    start = HAL_GetTick();

    for (i = 0U; i < len; i++) {
        while (uart->RFC == 0U) {
            if ((timeout != HAL_MAX_DELAY) && ((HAL_GetTick() - start) >= timeout)) {
                return HAL_TIMEOUT;
            }
        }

        buf[i] = uart->DR;
    }

    return HAL_OK;
}

/* ===================== 中断接收 ===================== */

/**
 * @brief  启动中断接收
 * @param  huart 句柄
 * @param  buf   接收缓冲区，由调用方保证生命周期
 * @param  len   期望字节数，填满后关中断并调 HAL_UART_RxCpltCallback
 * @retval HAL_OK 成功；HAL_BUSY 上一次接收尚未完成；
 *         HAL_ERROR 句柄/缓冲为空、len 为 0 或 Mode 不含 RX
 * @note   启动时先清空 RX FIFO，避免旧字节混入新包。
 */
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *buf, uint16_t len)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((huart->Init.Mode & HAL_UART_MODE_RX) == 0U) {
        return HAL_ERROR;
    }
    if (huart->pRxBuf != NULL) {
        return HAL_BUSY;
    }

    HAL_UART_ClearRxFifo(huart);

    huart->pRxBuf  = buf;
    huart->RxSize  = len;
    huart->RxCount = 0U;

    HAL_UART_ConfigIT(huart, HAL_UART_IT_RECV_RDY, ENABLE);

    return HAL_OK;
}

/**
 * @brief  中止中断接收
 * @param  huart 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 * @note   关 RECV_RDY 中断、清 RX FIFO，句柄的接收状态归位。
 */
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_ERROR;
    }

    HAL_UART_ConfigIT(huart, HAL_UART_IT_RECV_RDY, DISABLE);
    HAL_UART_ClearRxFifo(huart);

    huart->pRxBuf  = NULL;
    huart->RxSize  = 0U;
    huart->RxCount = 0U;

    return HAL_OK;
}

/* ===================== FIFO / 复位 / 状态 ===================== */

/**
 * @brief  清空接收 FIFO
 * @param  huart 句柄
 * @note   FCR 的清空位是 WZ，写 1 后硬件自动清零。
 */
void HAL_UART_ClearRxFifo(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    huart->Instance->FCR |= RB_FCR_RX_FIFO_CLR;
}

/**
 * @brief  清空发送 FIFO
 * @param  huart 句柄
 * @note   FCR 的清空位是 WZ，写 1 后硬件自动清零。
 */
void HAL_UART_ClearTxFifo(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    huart->Instance->FCR |= RB_FCR_TX_FIFO_CLR;
}

/**
 * @brief  外设软复位
 * @param  huart 句柄
 * @note   向 IER 写 RB_IER_RESET，硬件自动清零该位；其余配置寄存器不受影响。
 */
void HAL_UART_Reset(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    huart->Instance->IER = RB_IER_RESET;
}

/**
 * @brief  等待发送完成
 * @param  huart   句柄
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @note   轮询 LSR.TX_ALL_EMP。注意 LSR 的错误位是 RZ 读清零，
 *         轮询期间会把 OVER_ERR / PAR_ERR / FRAME_ERR / BREAK_ERR 一并读掉。
 */
void HAL_UART_WaitTxComplete(UART_HandleTypeDef *huart, uint32_t timeout)
{
    UART_TypeDef *uart;
    uint32_t start;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    uart  = huart->Instance;
    start = HAL_GetTick();

    while ((uart->LSR & RB_LSR_TX_ALL_EMP) == 0U) {
        if ((timeout != HAL_MAX_DELAY) && ((HAL_GetTick() - start) >= timeout)) {
            return;
        }
    }
}

/**
 * @brief  读线路状态
 * @param  huart 句柄
 * @return LSR 寄存器值，位定义见 HAL_UART_STA_*
 * @note   错误位是 RZ 读清零，调一次就读掉了，要多个判断请先存快照。
 */
uint8_t HAL_UART_GetLineStatus(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return 0U;
    }

    return huart->Instance->LSR;
}

/**
 * @brief  读接收 FIFO 里的字节数
 * @param  huart 句柄
 * @return 0~8
 */
uint8_t HAL_UART_GetRxCount(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return 0U;
    }

    return huart->Instance->RFC;
}

/**
 * @brief  读发送 FIFO 里的字节数
 * @param  huart 句柄
 * @return 0~8，为 0 时仅表示 FIFO 空，不代表移位寄存器已发完
 */
uint8_t HAL_UART_GetTxCount(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return 0U;
    }

    return huart->Instance->TFC;
}

/* ===================== 中断入口与回调 ===================== */

/**
 * @brief  UART 中断处理
 * @param  huart 句柄，由 _it.c 里的 UARTn_IRQHandler 传入
 * @note   IIR 是中断编码而非位图；LSR 的错误位是读清零，必须只读一次存快照。
 *         FIFO 必须读空，否则 RECV_RDY 中断不会撤销。
 */
__HIGH_CODE
void HAL_UART_IRQHandler(UART_HandleTypeDef *huart)
{
    UART_TypeDef *uart;
    uint8_t iir;
    uint16_t cnt;
    uint16_t size;

    if ((huart == NULL) || (huart->Instance == NULL)) {
        return;
    }

    uart = huart->Instance;
    iir  = (uint8_t)(uart->IIR & RB_IIR_INT_MASK);

    if ((iir & RB_IIR_NO_INT) != 0U) {
        return; /* bit0=1 表示无中断 */
    }

    if (iir == HAL_UART_IIR_LINE_STAT) {
        huart->ErrorLsr = uart->LSR; /* RZ 读清零，先存快照 */
        HAL_UART_ErrorCallback(huart);
        return;
    }

    if ((iir == HAL_UART_IIR_RECV_RDY) || (iir == HAL_UART_IIR_TIMEOUT)) {
        cnt  = huart->RxCount;
        size = huart->RxSize;

        while (uart->RFC != 0U) {
            uint8_t data = uart->DR;

            if ((huart->pRxBuf != NULL) && (cnt < size)) {
                huart->pRxBuf[cnt] = data;
                cnt++;
            }
        }

        huart->RxCount = cnt;

        if ((huart->pRxBuf != NULL) && (size != 0U) && (cnt >= size)) {
            HAL_UART_ConfigIT(huart, HAL_UART_IT_RECV_RDY, DISABLE);
            HAL_UART_RxCpltCallback(huart);
        }

        return;
    }

    /* THR_EMPTY / MODEM：本模块不开启，读 IIR 已清除 */
}

/**
 * @brief  接收缓冲填满回调，weak 默认空实现
 * @param  huart 触发回调的句柄
 * @note   中断上下文执行。填满后 RECV_RDY 已关，
 *         需要继续接收必须在覆盖的实现里重新调 HAL_UART_Receive_IT。
 */
__weak void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    UNUSED(huart);
}

/**
 * @brief  接收线路错误回调，weak 默认空实现
 * @param  huart 触发回调的句柄，错误码见 huart->ErrorLsr
 * @note   中断上下文执行。
 */
__weak void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    UNUSED(huart);
}

/**
 * @brief  引脚初始化，weak 默认空实现，在 BSP 中覆盖
 * @param  huart 句柄，此时 Instance 与 Init 已就绪
 * @note   由 HAL_UART_Init 在开外设时钟之前调用。
 */
__weak void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    UNUSED(huart);
}

/**
 * @brief  引脚回收，weak 默认空实现，在 BSP 中覆盖
 * @param  huart 句柄，Init.Mode 仍是 DeInit 前的值
 * @note   由 HAL_UART_DeInit 在关外设时钟之后调用。
 */
__weak void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    UNUSED(huart);
}
