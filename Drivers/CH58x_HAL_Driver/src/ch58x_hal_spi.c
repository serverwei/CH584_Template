#include "ch58x_hal.h"

/* ===================== 内部工具 ===================== */

/**
 * @brief  由句柄的 Role / Mode 组合出 CTRL_MOD 的配置位
 * @note   不含 RB_SPI_ALL_CLEAR 与 RB_SPI_FIFO_DIR：
 *         前者是清零脉冲，后者由每次收发决定方向。
 *         从机模式下 bit3 是 SLV_CMD_MOD（首字节命令模式），
 *         不能按主机的时钟空闲模式去写，本 HAL 固定为 0（数据流模式）。
 */
static uint8_t HAL_SPI_ModeBits(const SPI_HandleTypeDef *hspi)
{
    uint8_t mod;

    if (hspi->Init.Role == HAL_SPI_ROLE_MASTER) {
        mod = RB_SPI_SCK_OE | RB_SPI_MOSI_OE;

        if (hspi->Init.Mode == HAL_SPI_MODE_3) {
            mod |= RB_SPI_MST_SCK_MOD;
        }
    } else {
        mod = RB_SPI_MISO_OE;
    }

    return mod;
}

/**
 * @brief  实例对应的中断号
 * @note   SPI0 独占 IRQ19；SPI1 与 PWMX 共用 IRQ31。
 */
static IRQn_Type HAL_SPI_GetIRQn(SPI_TypeDef *spi)
{
    return (spi == SPI0) ? SPI0_IRQn : PWMX_IRQn;
}

/**
 * @brief  超时判断
 * @param  start   起始时刻，单位 ms
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval 1 已超时；0 未超时
 */
static uint8_t HAL_SPI_IsTimeout(uint32_t start, uint32_t timeout)
{
    return (uint8_t)((timeout != HAL_MAX_DELAY) && ((HAL_GetTick() - start) >= timeout));
}

/**
 * @brief  发送是否真正结束
 * @param  spi    实例
 * @param  master 1 = 主机，0 = 从机
 * @note   主机模式：TOTAL_CNT 归零说明全部字节都已送出，再等 RB_SPI_FREE
 *         置位，确保最后一个字节已移出移位寄存器。
 *         从机模式没有 TOTAL_CNT，FIFO 被外部主机取空即算完成。
 */
static uint8_t HAL_SPI_TxDone(SPI_TypeDef *spi, uint8_t master)
{
    if (master == 0U) {
        return (uint8_t)(spi->FIFO_COUNT == 0U);
    }

    return (uint8_t)((spi->TOTAL_CNT == 0U) && ((spi->INT_FLAG & RB_SPI_FREE) != 0U));
}

/**
 * @brief  把待发数据尽量塞进 FIFO
 * @note   只负责填，不负责开关中断；队列填满后顺手关掉半满中断，
 *         由调用方决定是否重新打开。
 */
static void HAL_SPI_TxFill(SPI_HandleTypeDef *hspi)
{
    while ((hspi->TxCount < hspi->TxSize) &&
           (hspi->Instance->FIFO_COUNT < HAL_SPI_FIFO_SIZE)) {
        hspi->Instance->FIFO = hspi->pTxBuf[hspi->TxCount];
        hspi->TxCount++;
    }

    if (hspi->TxCount >= hspi->TxSize) {
        /* 数据已全部入队，不再需要半满中断，剩 CNT_END 收尾 */
        HAL_SPI_Disable_IT(hspi, HAL_SPI_IT_FIFO_HF);
    }
}

/**
 * @brief  把 FIFO 里已收到的数据搬进缓冲区
 */
static void HAL_SPI_RxDrain(SPI_HandleTypeDef *hspi)
{
    while ((hspi->RxCount < hspi->RxSize) && (hspi->Instance->FIFO_COUNT != 0U)) {
        hspi->pRxBuf[hspi->RxCount] = hspi->Instance->FIFO;
        hspi->RxCount++;
    }
}

/* ===================== 初始化 ===================== */

/**
 * @brief  初始化 SPI
 * @param  hspi 句柄，调用前需填好 Instance 与 Init
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   MspInit 先配引脚再配寄存器，与 UART / PWM 一致。
 *         CTRL_MOD 的清零位 RB_SPI_ALL_CLEAR 是脉冲，必须单独写一次，
 *         随后那次写 CTRL_MOD 才能把它带回 0，否则外设会被一直按住。
 */
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi)
{
    SPI_TypeDef *spi;

    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)hspi->Init.Role > (uint32_t)HAL_SPI_ROLE_SLAVE) {
        return HAL_ERROR;
    }
    if ((uint32_t)hspi->Init.Mode > (uint32_t)HAL_SPI_MODE_3) {
        return HAL_ERROR;
    }
    if ((uint32_t)hspi->Init.BitOrder > (uint32_t)HAL_SPI_BITORDER_LSB) {
        return HAL_ERROR;
    }
    if ((hspi->Instance == SPI1) && (hspi->Init.Role != HAL_SPI_ROLE_MASTER)) {
        return HAL_ERROR; /* CH585 的 SPI1 只有主机模式 */
    }
    if (hspi->Init.Role == HAL_SPI_ROLE_MASTER) {
        if ((hspi->Init.ClockDiv < HAL_SPI_CLOCK_DIV_MIN) ||
            (hspi->Init.ClockDiv > HAL_SPI_CLOCK_DIV_MAX)) {
            return HAL_ERROR;
        }
    }

    spi = hspi->Instance;

    hspi->State   = HAL_SPI_STATE_RESET;
    hspi->pTxBuf  = NULL;
    hspi->TxSize  = 0U;
    hspi->TxCount = 0U;
    hspi->pRxBuf  = NULL;
    hspi->RxSize  = 0U;
    hspi->RxCount = 0U;

    HAL_SPI_MspInit(hspi);

    /* 清 FIFO / 计数器 / 中断标志：ALL_CLEAR 单独写一次 */
    spi->CTRL_MOD = RB_SPI_ALL_CLEAR;
    spi->INTER_EN = 0x00U;
    spi->INT_FLAG = HAL_SPI_FLAG_MASK;

    spi->CTRL_MOD = HAL_SPI_ModeBits(hspi);

    /* AUTO_IF：访问 BUFFER/FIFO 时自动清 BYTE_END，省掉每字节一次写标志 */
    spi->CTRL_CFG = RB_SPI_AUTO_IF;

    if (hspi->Init.BitOrder == HAL_SPI_BITORDER_LSB) {
        spi->CTRL_CFG |= RB_SPI_BIT_ORDER;
    }

    hspi->State = HAL_SPI_STATE_READY;

    if (hspi->Init.Role == HAL_SPI_ROLE_MASTER) {
        return HAL_SPI_SetClockDiv(hspi, hspi->Init.ClockDiv);
    }

    return HAL_OK;
}

/**
 * @brief  关闭 SPI
 * @param  hspi 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 * @note   引脚回收由 HAL_SPI_MspDeInit 负责，句柄的 Instance 置 NULL
 */
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *hspi)
{
    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return HAL_ERROR;
    }

    hspi->Instance->INTER_EN = 0x00U;
    hspi->Instance->CTRL_MOD = RB_SPI_ALL_CLEAR;
    hspi->Instance->CTRL_CFG = 0x00U;
    hspi->Instance->INT_FLAG = HAL_SPI_FLAG_MASK;

    HAL_SPI_MspDeInit(hspi);

    hspi->pTxBuf   = NULL;
    hspi->TxSize   = 0U;
    hspi->TxCount  = 0U;
    hspi->pRxBuf   = NULL;
    hspi->RxSize   = 0U;
    hspi->RxCount  = 0U;
    hspi->State    = HAL_SPI_STATE_RESET;
    hspi->Instance = NULL;

    return HAL_OK;
}

/* ===================== 配置 ===================== */

/**
 * @brief  直接指定主机分频
 * @param  hspi 句柄
 * @param  div  分频，2~254，Fsck = Fsys / div
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空或分频越界
 * @note   分频取 2 时 SCK 逼近 Fsys/2，硬件要求打开主机输入延迟
 *         RB_SPI_MST_DLY_EN，这里自动处理。
 */
HAL_StatusTypeDef HAL_SPI_SetClockDiv(SPI_HandleTypeDef *hspi, uint8_t div)
{
    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((div < HAL_SPI_CLOCK_DIV_MIN) || (div > HAL_SPI_CLOCK_DIV_MAX)) {
        return HAL_ERROR;
    }

    if (div == HAL_SPI_CLOCK_DIV_MIN) {
        hspi->Instance->CTRL_CFG |= RB_SPI_MST_DLY_EN;
    } else {
        hspi->Instance->CTRL_CFG &= (uint8_t)~RB_SPI_MST_DLY_EN;
    }

    hspi->Instance->CLOCK_DIV = div;
    hspi->Init.ClockDiv       = div;

    return HAL_OK;
}

/**
 * @brief  按频率配置 SCK
 * @param  hspi     句柄
 * @param  baudrate 目标频率，单位 Hz
 * @retval HAL_OK 成功；HAL_ERROR 参数为空或系统时钟未知
 * @note   分频只能取 2~254 的整数，实际频率 = Fsys / ceil(Fsys / baudrate)，
 *         即永远不超过请求值；超出可达范围时钳位到边界。
 *         依赖 HAL_RCC_GetSysClockFreq()，需先完成时钟初始化。
 */
HAL_StatusTypeDef HAL_SPI_SetBaudRate(SPI_HandleTypeDef *hspi, uint32_t baudrate)
{
    uint32_t sysClk;
    uint32_t div;

    if ((hspi == NULL) || (hspi->Instance == NULL) || (baudrate == 0U)) {
        return HAL_ERROR;
    }

    sysClk = HAL_RCC_GetSysClockFreq();

    if (sysClk == 0U) {
        return HAL_ERROR;
    }

    if (baudrate >= sysClk) {
        div = HAL_SPI_CLOCK_DIV_MIN;
    } else {
        div = (sysClk + baudrate - 1U) / baudrate; /* 向上取整，保证 Fsck ≤ 请求值 */

        if (div < HAL_SPI_CLOCK_DIV_MIN) {
            div = HAL_SPI_CLOCK_DIV_MIN;
        }
        if (div > HAL_SPI_CLOCK_DIV_MAX) {
            div = HAL_SPI_CLOCK_DIV_MAX;
        }
    }

    return HAL_SPI_SetClockDiv(hspi, (uint8_t)div);
}

/**
 * @brief  切换时钟空闲模式与位序
 * @param  hspi     句柄
 * @param  mode     HAL_SPI_MODE_0 / HAL_SPI_MODE_3
 * @param  bitOrder HAL_SPI_BITORDER_MSB / LSB
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   只能在两次收发之间调用。会保留 FIFO 方向位，但整体重写 CTRL_MOD，
 *         所以正在传输时调用会打断本次传输。
 */
HAL_StatusTypeDef HAL_SPI_SetDataMode(SPI_HandleTypeDef *hspi,
                                      HAL_SPI_ModeTypeDef mode,
                                      HAL_SPI_BitOrderTypeDef bitOrder)
{
    uint8_t dir;

    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)mode > (uint32_t)HAL_SPI_MODE_3) {
        return HAL_ERROR;
    }
    if ((uint32_t)bitOrder > (uint32_t)HAL_SPI_BITORDER_LSB) {
        return HAL_ERROR;
    }

    hspi->Init.Mode     = mode;
    hspi->Init.BitOrder = bitOrder;

    dir = (uint8_t)(hspi->Instance->CTRL_MOD & RB_SPI_FIFO_DIR);

    hspi->Instance->CTRL_MOD = (uint8_t)(HAL_SPI_ModeBits(hspi) | dir);

    if (bitOrder == HAL_SPI_BITORDER_LSB) {
        hspi->Instance->CTRL_CFG |= RB_SPI_BIT_ORDER;
    } else {
        hspi->Instance->CTRL_CFG &= (uint8_t)~RB_SPI_BIT_ORDER;
    }

    return HAL_OK;
}

/**
 * @brief  当前 SCK 实际频率
 * @param  hspi 句柄
 * @retval 频率，单位 Hz；句柄为空或从机模式返回 0
 */
uint32_t HAL_SPI_GetClockFreq(const SPI_HandleTypeDef *hspi)
{
    uint32_t sysClk;

    if ((hspi == NULL) || (hspi->Instance == NULL) || (hspi->Init.ClockDiv == 0U)) {
        return 0U;
    }

    sysClk = HAL_RCC_GetSysClockFreq();

    return (sysClk == 0U) ? 0U : (sysClk / hspi->Init.ClockDiv);
}

/* ===================== 阻塞收发 ===================== */

/**
 * @brief  阻塞发送
 * @param  hspi    句柄
 * @param  buf     待发送数据首地址
 * @param  len     发送字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法；HAL_BUSY 上一笔未结束
 * @note   返回前会等最后一个字节真正移出移位寄存器。
 *         主机模式靠 TOTAL_CNT 自动产生时钟；从机模式由外部主机决定节奏，
 *         timeout 只是放弃等待，不保证数据已发完。
 */
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, const uint8_t *buf,
                                   uint16_t len, uint32_t timeout)
{
    SPI_TypeDef *spi;
    uint32_t start;
    uint16_t sent = 0U;
    uint8_t master;

    if ((hspi == NULL) || (hspi->Instance == NULL) || (buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((uint32_t)len > HAL_SPI_MAX_XFER) {
        return HAL_ERROR;
    }
    if (hspi->State != HAL_SPI_STATE_READY) {
        return HAL_BUSY;
    }

    spi    = hspi->Instance;
    master = (uint8_t)(hspi->Init.Role == HAL_SPI_ROLE_MASTER);

    hspi->State = HAL_SPI_STATE_BUSY_TX;

    spi->INT_FLAG = HAL_SPI_FLAG_MASK;
    spi->CTRL_MOD &= (uint8_t)~RB_SPI_FIFO_DIR; /* FIFO 方向：输出 */

    if (master != 0U) {
        spi->TOTAL_CNT = len;
    }

    start = HAL_GetTick();

    while (sent < len) {
        if (spi->FIFO_COUNT < HAL_SPI_FIFO_SIZE) {
            spi->FIFO = buf[sent];
            sent++;
        } else if (HAL_SPI_IsTimeout(start, timeout) != 0U) {
            hspi->State = HAL_SPI_STATE_READY;
            return HAL_TIMEOUT;
        } else {
            /* FIFO 仍满，继续等 */
        }
    }

    while (HAL_SPI_TxDone(spi, master) == 0U) {
        if (HAL_SPI_IsTimeout(start, timeout) != 0U) {
            hspi->State = HAL_SPI_STATE_READY;
            return HAL_TIMEOUT;
        }
    }

    hspi->State = HAL_SPI_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  阻塞接收
 * @param  hspi    句柄
 * @param  buf     接收缓冲区
 * @param  len     接收字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时（已收到的字节仍写进了 buf）；
 *         HAL_ERROR 参数非法；HAL_BUSY 上一笔未结束
 * @note   主机模式写 TOTAL_CNT 后硬件自动产生时钟。
 *         接收期间 MOSI 上的电平由硬件维持，手册未定义，仅在从机对 MOSI
 *         有要求时才需要留意。
 */
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *buf,
                                  uint16_t len, uint32_t timeout)
{
    SPI_TypeDef *spi;
    uint32_t start;
    uint16_t got = 0U;
    uint8_t master;

    if ((hspi == NULL) || (hspi->Instance == NULL) || (buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((uint32_t)len > HAL_SPI_MAX_XFER) {
        return HAL_ERROR;
    }
    if (hspi->State != HAL_SPI_STATE_READY) {
        return HAL_BUSY;
    }

    spi    = hspi->Instance;
    master = (uint8_t)(hspi->Init.Role == HAL_SPI_ROLE_MASTER);

    hspi->State = HAL_SPI_STATE_BUSY_RX;

    spi->INT_FLAG = HAL_SPI_FLAG_MASK;
    spi->CTRL_MOD |= RB_SPI_FIFO_DIR; /* FIFO 方向：输入 */

    if (master != 0U) {
        spi->TOTAL_CNT = len; /* 非 0 即自动产生时钟 */
    }

    start = HAL_GetTick();

    while (got < len) {
        if (spi->FIFO_COUNT != 0U) {
            buf[got] = spi->FIFO;
            got++;
        } else if (HAL_SPI_IsTimeout(start, timeout) != 0U) {
            hspi->State = HAL_SPI_STATE_READY;
            return HAL_TIMEOUT;
        } else {
            /* FIFO 还空，继续等 */
        }
    }

    hspi->State = HAL_SPI_STATE_READY;

    return HAL_OK;
}

/* ===================== DMA 收发 =====================
 * 只有 SPI0 有 DMA。硬件从 DMA_BEG 一直搬到 DMA_END（不含），
 * 每搬一次就顶一个字节进/出 FIFO，所以 CPU 完全不参与搬运。
 * 完成条件看 CNT_END，与 FIFO 模式一致。
 */

/**
 * @brief  公共的 DMA 前置检查与启动
 * @param  hspi 句柄
 * @param  buf  缓冲区地址
 * @param  len  字节数
 * @retval HAL_OK 参数合法；HAL_ERROR 参数非法或本实例没有 DMA
 */
static HAL_StatusTypeDef HAL_SPI_DmaStart(SPI_HandleTypeDef *hspi, const uint8_t *buf,
                                          uint16_t len, uint8_t receive)
{
    SPI_TypeDef *spi;

    if ((hspi == NULL) || (hspi->Instance == NULL) || (buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((uint32_t)len > HAL_SPI_MAX_XFER) {
        return HAL_ERROR;
    }
    if (hspi->Instance != SPI0) {
        return HAL_ERROR; /* SPI1 没有 DMA 寄存器 */
    }
    if (hspi->Init.Role != HAL_SPI_ROLE_MASTER) {
        return HAL_ERROR;
    }
    if (((uint32_t)buf & (HAL_SPI_DMA_ALIGN - 1U)) != 0U) {
        return HAL_ERROR; /* DMA 要求 4 字节对齐 */
    }
    if (hspi->State != HAL_SPI_STATE_READY) {
        return HAL_BUSY;
    }

    spi = hspi->Instance;

    /* 先关 DMA 再改地址，避免上一笔的残留设置生效 */
    spi->CTRL_CFG &= (uint8_t)~RB_SPI_DMA_ENABLE;
    spi->INT_FLAG = HAL_SPI_FLAG_MASK;

    if (receive != 0U) {
        spi->CTRL_MOD |= RB_SPI_FIFO_DIR;
    } else {
        spi->CTRL_MOD &= (uint8_t)~RB_SPI_FIFO_DIR;
    }

    spi->DMA_BEG   = (uint32_t)buf;
    spi->DMA_END   = (uint32_t)buf + len;
    spi->TOTAL_CNT = len;

    spi->CTRL_CFG |= RB_SPI_DMA_ENABLE;

    hspi->State = (receive != 0U) ? HAL_SPI_STATE_BUSY_RX : HAL_SPI_STATE_BUSY_TX;

    return HAL_OK;
}

/**
 * @brief  DMA 传输收尾：等 CNT_END、关 DMA、清标志
 * @param  hspi    句柄
 * @param  start   起始时刻，单位 ms
 * @param  timeout 超时，单位 ms
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时
 */
static HAL_StatusTypeDef HAL_SPI_DmaWait(SPI_HandleTypeDef *hspi, uint32_t start, uint32_t timeout)
{
    SPI_TypeDef *spi = hspi->Instance;

    while ((spi->INT_FLAG & RB_SPI_IF_CNT_END) == 0U) {
        if (HAL_SPI_IsTimeout(start, timeout) != 0U) {
            spi->CTRL_CFG &= (uint8_t)~RB_SPI_DMA_ENABLE;
            spi->INT_FLAG = HAL_SPI_FLAG_MASK;
            hspi->State   = HAL_SPI_STATE_READY;
            return HAL_TIMEOUT;
        }
    }

    spi->CTRL_CFG &= (uint8_t)~RB_SPI_DMA_ENABLE;
    spi->INT_FLAG = HAL_SPI_FLAG_MASK;

    /* CNT_END 只说明字节搬完了，再等总线空闲，确保最后一个字节已移出移位寄存器 */
    while ((spi->INT_FLAG & RB_SPI_FREE) == 0U) {
        if (HAL_SPI_IsTimeout(start, timeout) != 0U) {
            hspi->State = HAL_SPI_STATE_READY;
            return HAL_TIMEOUT;
        }
    }

    hspi->State = HAL_SPI_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  DMA 阻塞发送
 * @param  hspi    句柄，必须是 SPI0 主机
 * @param  buf     待发送数据首地址，须 4 字节对齐
 * @param  len     发送字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法或非 SPI0；HAL_BUSY 上一笔未结束
 * @note   适合 8 字节以上的长包；短包用 HAL_SPI_Transmit 开销更小。
 */
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *hspi, const uint8_t *buf,
                                       uint16_t len, uint32_t timeout)
{
    uint32_t start;
    HAL_StatusTypeDef st;

    st = HAL_SPI_DmaStart(hspi, buf, len, 0U);

    if (st != HAL_OK) {
        return st;
    }

    start = HAL_GetTick();

    return HAL_SPI_DmaWait(hspi, start, timeout);
}

/**
 * @brief  DMA 阻塞接收
 * @param  hspi    句柄，必须是 SPI0 主机
 * @param  buf     接收缓冲区，须 4 字节对齐
 * @param  len     接收字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法或非 SPI0；HAL_BUSY 上一笔未结束
 * @note   与 HAL_SPI_Receive 一样，接收期间 MOSI 电平由硬件维持，手册未定义。
 */
HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *hspi, uint8_t *buf,
                                      uint16_t len, uint32_t timeout)
{
    uint32_t start;
    HAL_StatusTypeDef st;

    st = HAL_SPI_DmaStart(hspi, buf, len, 1U);

    if (st != HAL_OK) {
        return st;
    }

    start = HAL_GetTick();

    return HAL_SPI_DmaWait(hspi, start, timeout);
}

/* ===================== 中断收发 ===================== */

/**
 * @brief  启动中断发送
 * @param  hspi 句柄
 * @param  buf  待发送数据首地址，由调用方保证生命周期
 * @param  len  发送字节数，最大 4095
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或从机模式；HAL_BUSY 上一笔未结束
 * @note   先把 FIFO 填满再开中断，保证首次中断不来也能起步；
 *         只有剩余数据超过 FIFO 深度时才打开半满中断。
 *         从机模式没有 TOTAL_CNT，收不到 CNT_END，只能用阻塞发送。
 */
HAL_StatusTypeDef HAL_SPI_Transmit_IT(SPI_HandleTypeDef *hspi, const uint8_t *buf, uint16_t len)
{
    if ((hspi == NULL) || (hspi->Instance == NULL) || (buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((uint32_t)len > HAL_SPI_MAX_XFER) {
        return HAL_ERROR;
    }
    if (hspi->Init.Role != HAL_SPI_ROLE_MASTER) {
        return HAL_ERROR;
    }
    if (hspi->State != HAL_SPI_STATE_READY) {
        return HAL_BUSY;
    }

    hspi->pTxBuf  = buf;
    hspi->TxSize  = len;
    hspi->TxCount = 0U;
    hspi->State   = HAL_SPI_STATE_BUSY_TX;

    hspi->Instance->INT_FLAG = HAL_SPI_FLAG_MASK;
    hspi->Instance->CTRL_MOD &= (uint8_t)~RB_SPI_FIFO_DIR;
    hspi->Instance->TOTAL_CNT = len;

    HAL_SPI_TxFill(hspi);

    HAL_SPI_Enable_IT(hspi, HAL_SPI_IT_CNT_END);

    if (hspi->TxCount < hspi->TxSize) {
        HAL_SPI_Enable_IT(hspi, HAL_SPI_IT_FIFO_HF);
    }

    return HAL_OK;
}

/**
 * @brief  启动中断接收
 * @param  hspi 句柄
 * @param  buf  接收缓冲区，由调用方保证生命周期
 * @param  len  接收字节数，最大 4095
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或从机模式；HAL_BUSY 上一笔未结束
 * @note   收满 len 字节后关中断并触发 HAL_SPI_RxCpltCallback。
 *         从机模式的接收长度由外部主机决定，不支持中断模式。
 */
HAL_StatusTypeDef HAL_SPI_Receive_IT(SPI_HandleTypeDef *hspi, uint8_t *buf, uint16_t len)
{
    if ((hspi == NULL) || (hspi->Instance == NULL) || (buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((uint32_t)len > HAL_SPI_MAX_XFER) {
        return HAL_ERROR;
    }
    if (hspi->Init.Role != HAL_SPI_ROLE_MASTER) {
        return HAL_ERROR;
    }
    if (hspi->State != HAL_SPI_STATE_READY) {
        return HAL_BUSY;
    }

    hspi->pRxBuf  = buf;
    hspi->RxSize  = len;
    hspi->RxCount = 0U;
    hspi->State   = HAL_SPI_STATE_BUSY_RX;

    hspi->Instance->INT_FLAG = HAL_SPI_FLAG_MASK;
    hspi->Instance->CTRL_MOD |= RB_SPI_FIFO_DIR;
    hspi->Instance->TOTAL_CNT = len;

    HAL_SPI_Enable_IT(hspi, (uint8_t)(HAL_SPI_IT_CNT_END | HAL_SPI_IT_FIFO_HF));

    return HAL_OK;
}

/**
 * @brief  中止正在进行的中断收发
 * @param  hspi 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 * @note   ALL_CLEAR 会把 CTRL_MOD 的配置位一起复位，清完必须补回来，
 *         否则 SCK/MOSI 输出使能丢失，下一次传输不再有时钟。
 */
HAL_StatusTypeDef HAL_SPI_Abort_IT(SPI_HandleTypeDef *hspi)
{
    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return HAL_ERROR;
    }

    HAL_SPI_Disable_IT(hspi, HAL_SPI_IT_ALL);

    hspi->Instance->CTRL_MOD = RB_SPI_ALL_CLEAR;
    hspi->Instance->CTRL_MOD = HAL_SPI_ModeBits(hspi);
    hspi->Instance->INT_FLAG = HAL_SPI_FLAG_MASK;

    hspi->pTxBuf  = NULL;
    hspi->TxSize  = 0U;
    hspi->TxCount = 0U;
    hspi->pRxBuf  = NULL;
    hspi->RxSize  = 0U;
    hspi->RxCount = 0U;
    hspi->State   = HAL_SPI_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  打开指定中断源并允许 NVIC 响应
 * @param  hspi 句柄
 * @param  it   中断源位组合，取 HAL_SPI_IT_xxx
 * @note   优先级由 HAL_NVIC_InitPriorities 统一设置，这里不再重复。
 *         SPI1 与 PWMX 共用 IRQ31，打开 SPI1 中断也会让 PWMX 的
 *         中断线一起使能，所以两个外设的处理函数都必须先查自己的标志。
 */
void HAL_SPI_Enable_IT(SPI_HandleTypeDef *hspi, uint8_t it)
{
    if ((hspi == NULL) || (hspi->Instance == NULL) || (it == 0U)) {
        return;
    }

    hspi->Instance->INTER_EN |= it;

    PFIC_EnableIRQ(HAL_SPI_GetIRQn(hspi->Instance));
}

/**
 * @brief  关闭指定中断源
 * @param  hspi 句柄
 * @param  it   中断源位组合，取 HAL_SPI_IT_xxx
 * @note   SPI0 独占 IRQ19，全部关完后可以安全地关 NVIC。
 *         SPI1 与 PWMX 共用 IRQ31，谁都不能单方面关这条线，否则会顺手
 *         把对方的中断也掐掉，所以 SPI1 只关自己的使能位，NVIC 一直保持。
 */
void HAL_SPI_Disable_IT(SPI_HandleTypeDef *hspi, uint8_t it)
{
    if ((hspi == NULL) || (hspi->Instance == NULL) || (it == 0U)) {
        return;
    }

    hspi->Instance->INTER_EN &= (uint8_t)~it;

    if ((hspi->Instance == SPI0) && (hspi->Instance->INTER_EN == 0U)) {
        PFIC_DisableIRQ(SPI0_IRQn);
    }
}

/* ===================== 状态 ===================== */

/**
 * @brief  读取句柄状态
 */
uint8_t HAL_SPI_GetState(const SPI_HandleTypeDef *hspi)
{
    return (hspi == NULL) ? HAL_SPI_STATE_RESET : hspi->State;
}

/**
 * @brief  读取 FIFO 中当前字节数
 */
uint8_t HAL_SPI_GetFifoCount(const SPI_HandleTypeDef *hspi)
{
    return (hspi == NULL) ? 0U : hspi->Instance->FIFO_COUNT;
}

/* ===================== 中断入口 ===================== */

/**
 * @brief  SPI 中断处理
 * @param  hspi 句柄
 * @note   必须先查标志再动手：SPI1 与 PWMX 共用 IRQ31，共享向量入口
 *         会无条件调用本函数，不是 SPI 的中断要立刻返回。
 *         INT_FLAG 的 bit6 是只读的空闲状态位，判断前用 HAL_SPI_FLAG_MASK 屏蔽。
 *         FIFO_HF 的含义随方向而变：发送时表示 FIFO 不足 4 字节（该补货），
 *         接收时表示 FIFO 已有 4 字节以上（该取走）。
 */
__HIGH_CODE
void HAL_SPI_IRQHandler(SPI_HandleTypeDef *hspi)
{
    uint8_t flags;

    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return;
    }

    flags = (uint8_t)(hspi->Instance->INT_FLAG & HAL_SPI_FLAG_MASK);

    if (flags == 0U) {
        return;
    }

    if ((flags & HAL_SPI_FLAG_FIFO_OV) != 0U) {
        hspi->Instance->INT_FLAG = HAL_SPI_FLAG_FIFO_OV;
        HAL_SPI_ErrorCallback(hspi);
    }

    /* DMA_END 由下面的阻塞式 DMA 接口自己轮询，这里只负责把标志清掉，
     * 否则它永远挂着，中断会一直重入。
     */
    if ((flags & HAL_SPI_FLAG_DMA_END) != 0U) {
        hspi->Instance->INT_FLAG = HAL_SPI_FLAG_DMA_END;
    }

    if ((flags & HAL_SPI_FLAG_FIFO_HF) != 0U) {
        hspi->Instance->INT_FLAG = HAL_SPI_FLAG_FIFO_HF;

        if (hspi->State == HAL_SPI_STATE_BUSY_TX) {
            HAL_SPI_TxFill(hspi);
        } else if (hspi->State == HAL_SPI_STATE_BUSY_RX) {
            HAL_SPI_RxDrain(hspi);
        } else {
            /* 空闲时来的半满中断，丢弃 */
        }
    }

    if ((flags & HAL_SPI_FLAG_CNT_END) != 0U) {
        hspi->Instance->INT_FLAG = HAL_SPI_FLAG_CNT_END;

        if (hspi->State == HAL_SPI_STATE_BUSY_TX) {
            HAL_SPI_Disable_IT(hspi, (uint8_t)(HAL_SPI_IT_CNT_END | HAL_SPI_IT_FIFO_HF));
            hspi->State = HAL_SPI_STATE_READY;
            HAL_SPI_TxCpltCallback(hspi);
        } else if (hspi->State == HAL_SPI_STATE_BUSY_RX) {
            HAL_SPI_RxDrain(hspi);
            HAL_SPI_Disable_IT(hspi, (uint8_t)(HAL_SPI_IT_CNT_END | HAL_SPI_IT_FIFO_HF));
            hspi->State = HAL_SPI_STATE_READY;
            HAL_SPI_RxCpltCallback(hspi);
        } else {
            /* 空闲时来的完成中断，丢弃 */
        }
    }
}

/* ===================== 默认回调与 MSP ===================== */

__weak void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    UNUSED(hspi);
}

__weak void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    UNUSED(hspi);
}

__weak void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    UNUSED(hspi);
}

__weak void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    UNUSED(hspi);
}

__weak void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
    UNUSED(hspi);
}
