#include "ch58x_hal.h"

#include <stddef.h>

/* ===================== 寄存器布局校验 =====================
 * 期望偏移取自 CH585DS1.PDF 表 12-1（I2C 寄存器，基址 0x40004800）。
 * I2C 的寄存器都是 16 位宽、但按 4 字节步进，靠 RESERVED 填位是必须的。
 */
_Static_assert(offsetof(I2C_TypeDef, CTRL1) == 0x00, "I2C_CTRL1 @ +0x00");
_Static_assert(offsetof(I2C_TypeDef, CTRL2) == 0x04, "I2C_CTRL2 @ +0x04");
_Static_assert(offsetof(I2C_TypeDef, OADDR1) == 0x08, "I2C_OADDR1 @ +0x08");
_Static_assert(offsetof(I2C_TypeDef, OADDR2) == 0x0C, "I2C_OADDR2 @ +0x0C");
_Static_assert(offsetof(I2C_TypeDef, DATAR) == 0x10, "I2C_DATAR @ +0x10");
_Static_assert(offsetof(I2C_TypeDef, STAR1) == 0x14, "I2C_STAR1 @ +0x14");
_Static_assert(offsetof(I2C_TypeDef, STAR2) == 0x18, "I2C_STAR2 @ +0x18");
_Static_assert(offsetof(I2C_TypeDef, CKCFGR) == 0x1C, "I2C_CKCFGR @ +0x1C");
_Static_assert(offsetof(I2C_TypeDef, RTR) == 0x20, "I2C_RTR @ +0x20");
/* ===================== 内部辅助 ===================== */

static void HAL_I2C_EnableIT(I2C_HandleTypeDef *hi2c)
{
    hi2c->Instance->CTRL2 |= (RB_I2C_ITEVTEN | RB_I2C_ITBUFEN | RB_I2C_ITERREN);
}

static void HAL_I2C_DisableIT(I2C_HandleTypeDef *hi2c)
{
    hi2c->Instance->CTRL2 &= (uint16_t)~(RB_I2C_ITEVTEN | RB_I2C_ITBUFEN | RB_I2C_ITERREN);
}

/**
 * @brief  记录并清除 STAR1 的错误位
 * @param  hi2c  句柄
 * @param  star1 STAR1 快照
 * @retval 1 有错误，0 无错误
 * @note   STAR1 的错误位是 rc_w0，写 0 清除；其余位写 1 无影响。
 */
static uint8_t HAL_I2C_CatchError(I2C_HandleTypeDef *hi2c, uint16_t star1)
{
    uint8_t err = HAL_I2C_ERROR_NONE;

    if ((star1 & RB_I2C_BERR) != 0U) {
        err |= HAL_I2C_ERROR_BERR;
    }
    if ((star1 & RB_I2C_ARLO) != 0U) {
        err |= HAL_I2C_ERROR_ARLO;
    }
    if ((star1 & RB_I2C_AF) != 0U) {
        err |= HAL_I2C_ERROR_AF;
    }
    if ((star1 & RB_I2C_OVR) != 0U) {
        err |= HAL_I2C_ERROR_OVR;
    }
    if ((star1 & RB_I2C_TIMEOUT) != 0U) {
        err |= HAL_I2C_ERROR_TIMEOUT;
    }

    if (err == HAL_I2C_ERROR_NONE) {
        return 0U;
    }

    hi2c->ErrorCode |= err;

    hi2c->Instance->STAR1 = (uint16_t)~(RB_I2C_BERR | RB_I2C_ARLO |
                                        RB_I2C_AF | RB_I2C_OVR | RB_I2C_TIMEOUT);

    return 1U;
}

/**
 * @brief  等 STAR1 的某个位置起
 * @param  hi2c    句柄
 * @param  flag    待等的位，SB / ADDR / BTF / TxE / RxNE
 * @param  timeout 超时，单位 ms，HAL_MAX_DELAY 不限
 * @retval HAL_OK 等到了；HAL_ERROR 途中出错；HAL_TIMEOUT 超时
 */
static HAL_StatusTypeDef HAL_I2C_WaitFlag(I2C_HandleTypeDef *hi2c, uint16_t flag, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();

    while ((hi2c->Instance->STAR1 & flag) == 0U) {
        if (HAL_I2C_CatchError(hi2c, hi2c->Instance->STAR1) != 0U) {
            return HAL_ERROR;
        }

        if ((timeout != HAL_MAX_DELAY) && ((HAL_GetTick() - start) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

/**
 * @brief  等总线空闲（STAR2.BUSY 清零）
 */
static HAL_StatusTypeDef HAL_I2C_WaitBusFree(I2C_HandleTypeDef *hi2c, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();

    while ((hi2c->Instance->STAR2 & RB_I2C_BUSY) != 0U) {
        if ((timeout != HAL_MAX_DELAY) && ((HAL_GetTick() - start) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

/**
 * @brief  等总线真正释放（STOP 发出后 BUSY 清零）
 */
static void HAL_I2C_WaitStopDone(I2C_HandleTypeDef *hi2c)
{
    uint32_t guard = 100000U;

    while (((hi2c->Instance->STAR2 & RB_I2C_BUSY) != 0U) && (guard != 0U)) {
        guard--;
    }
}

/**
 * @brief  清 SB 标志（rc_w0）
 */
static void HAL_I2C_ClearSB(I2C_HandleTypeDef *hi2c)
{
    hi2c->Instance->STAR1 = (uint16_t)~RB_I2C_SB;
}

/**
 * @brief  清 ADDR 标志（读 STAR1 再读 STAR2）
 */
static void HAL_I2C_ClearADDR(I2C_HandleTypeDef *hi2c)
{
    (void)hi2c->Instance->STAR1;
    (void)hi2c->Instance->STAR2;
}

/**
 * @brief  发一个字节的器件地址
 * @param  read 非 0 表示读方向
 */
static void HAL_I2C_SendAddr(I2C_HandleTypeDef *hi2c, uint8_t read)
{
    uint16_t addr = (uint16_t)(hi2c->DevAddr & 0xFEU);

    if (read != 0U) {
        addr |= 0x01U;
    }

    hi2c->Instance->DATAR = addr;
}

/**
 * @brief  发送寄存器地址的下一个字节
 * @retval 1 还有剩余地址字节，0 地址已发完
 */
static uint8_t HAL_I2C_SendMemAddrByte(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->MemAddrSize == 2U) {
        hi2c->Instance->DATAR = (uint16_t)((hi2c->MemAddr >> 8) & 0xFFU);
        hi2c->MemAddrSize     = 1U;
        return 1U;
    }

    if (hi2c->MemAddrSize == 1U) {
        hi2c->Instance->DATAR = (uint16_t)(hi2c->MemAddr & 0xFFU);
        hi2c->MemAddrSize     = 0U;
        return 1U;
    }

    return 0U;
}

/**
 * @brief  按剩余字节数设置接收末端的 ACK / POS / STOP
 * @param  remaining 还没读出的字节数（含当前这一个）
 */
static void HAL_I2C_SetRxEndMode(I2C_HandleTypeDef *hi2c, uint16_t remaining)
{
    if (remaining == 3U) {
        hi2c->Instance->CTRL1 &= (uint16_t)~RB_I2C_ACK;
    } else if (remaining == 2U) {
        hi2c->Instance->CTRL1 |= RB_I2C_POS;
    } else if (remaining == 1U) {
        hi2c->Instance->CTRL1 |= RB_I2C_STOP;
    } else {
        /* 更长的剩余，不做处理 */
    }
}

/**
 * @brief  传输收尾：关中断、状态归位
 */
static void HAL_I2C_EndTransfer(I2C_HandleTypeDef *hi2c)
{
    HAL_I2C_DisableIT(hi2c);
    hi2c->Instance->CTRL1 |= RB_I2C_ACK;
    hi2c->Instance->CTRL1 &= (uint16_t)~RB_I2C_POS;
    hi2c->Phase = HAL_I2C_PHASE_DATA;
    hi2c->State = HAL_I2C_STATE_READY;
}

/* ===================== 初始化 ===================== */

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c)
{
    I2C_TypeDef *i2c;
    uint32_t sysClk;
    uint16_t ccr;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (hi2c->Instance != I2C1) {
        return HAL_ERROR;
    }
    if ((hi2c->Init.ClockSpeed == 0U) || (hi2c->Init.ClockSpeed > 400000U)) {
        return HAL_ERROR;
    }

    i2c = hi2c->Instance;

    HAL_I2C_MspInit(hi2c);

    HAL_PWR_ConfigPeriphClock(ENABLE, HAL_PWR_SLPCLK_I2C);

    /* 软复位 */
    i2c->CTRL1 |= RB_I2C_SWRST;
    i2c->CTRL1 &= (uint16_t)~RB_I2C_SWRST;

    sysClk = HAL_RCC_GetSysClockFreq();
    if (sysClk == 0U) {
        return HAL_ERROR;
    }

    i2c->CTRL2 &= (uint16_t)~RB_I2C_FREQ;
    i2c->CTRL2 |= (uint16_t)(sysClk / 1000000U);

    i2c->CTRL1 &= (uint16_t)~RB_I2C_PE;

    if (hi2c->Init.ClockSpeed <= 100000U) {
        ccr = (uint16_t)((sysClk / (hi2c->Init.ClockSpeed << 1)) & RB_I2C_CCR);

        if (ccr < 0x04U) {
            ccr = 0x04U;
        }

        i2c->RTR = (uint16_t)((sysClk / 1000000U) + 1U);

        if (i2c->RTR > RB_I2C_TRISE) {
            i2c->RTR = RB_I2C_TRISE;
        }
    } else {
        if (hi2c->Init.DutyCycle == HAL_I2C_DUTY_2) {
            ccr = (uint16_t)((sysClk / (hi2c->Init.ClockSpeed * 3U)) & RB_I2C_CCR);
        } else {
            ccr = (uint16_t)((sysClk / (hi2c->Init.ClockSpeed * 25U)) & RB_I2C_CCR);
            ccr |= RB_I2C_DUTY;
        }

        if (ccr == 0U) {
            ccr = 0x01U;
        }

        ccr |= RB_I2C_F_S;

        i2c->RTR = (uint16_t)((((sysClk / 1000000U) * 300U) / 1000U) + 1U);
    }

    i2c->CKCFGR = ccr;

    i2c->CTRL1 &= (uint16_t)~(RB_I2C_SMBUS | RB_I2C_SMBTYPE | RB_I2C_ACK);

    if (hi2c->Init.BusMode == HAL_I2C_MODE_SMBUS_HOST) {
        i2c->CTRL1 |= (RB_I2C_SMBUS | RB_I2C_SMBTYPE);
    }

    i2c->CTRL1 |= RB_I2C_ACK;

    /* 自身地址：先清后置，不用 &= 0 */
    i2c->OADDR1 = (uint16_t)((uint16_t)hi2c->Init.AckAddr | (hi2c->Init.OwnAddress1 & 0x03FFU));

    i2c->CTRL1 |= RB_I2C_PE;

    hi2c->pBuf      = NULL;
    hi2c->XferSize  = 0U;
    hi2c->XferCount = 0U;
    hi2c->Phase     = HAL_I2C_PHASE_DATA;
    hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    hi2c->State     = HAL_I2C_STATE_READY;

    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *hi2c)
{
    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }

    HAL_I2C_DisableIT(hi2c);

    hi2c->Instance->CTRL1 = 0U;
    hi2c->Instance->CTRL2 = 0U;

    HAL_PWR_ConfigPeriphClock(DISABLE, HAL_PWR_SLPCLK_I2C);

    HAL_I2C_MspDeInit(hi2c);

    hi2c->pBuf      = NULL;
    hi2c->XferSize  = 0U;
    hi2c->XferCount = 0U;
    hi2c->Phase     = HAL_I2C_PHASE_DATA;
    hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    hi2c->State     = HAL_I2C_STATE_READY;
    hi2c->Instance  = NULL;

    return HAL_OK;
}

/* ===================== 主机阻塞传输 ===================== */

/**
 * @brief  主机发送
 * @param  hi2c    句柄
 * @param  devAddr 8 位左对齐器件地址
 * @param  buf     待发送数据
 * @param  len     字节数
 * @param  timeout 单元 ms，HAL_MAX_DELAY 不限
 * @retval HAL_OK 成功；HAL_BUSY 忙；HAL_ERROR 参数非法或总线错误；HAL_TIMEOUT 超时
 */
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                          const uint8_t *buf, uint16_t len, uint32_t timeout)
{
    I2C_TypeDef *i2c;
    HAL_StatusTypeDef st;
    uint16_t i;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if (hi2c->State != HAL_I2C_STATE_READY) {
        return HAL_BUSY;
    }

    i2c = hi2c->Instance;

    hi2c->State     = HAL_I2C_STATE_BUSY_TX;
    hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    hi2c->DevAddr   = devAddr;
    hi2c->Phase     = HAL_I2C_PHASE_DATA;

    st = HAL_I2C_WaitBusFree(hi2c, timeout);
    if (st != HAL_OK) {
        goto exit;
    }

    i2c->CTRL1 |= RB_I2C_START;

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_SB, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearSB(hi2c);

    HAL_I2C_SendAddr(hi2c, 0U);

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_ADDR, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearADDR(hi2c);

    for (i = 0U; i < len; i++) {
        st = HAL_I2C_WaitFlag(hi2c, RB_I2C_TxE, timeout);
        if (st != HAL_OK) {
            goto exit;
        }

        i2c->DATAR = buf[i];
    }

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_BTF, timeout);

exit:
    i2c->CTRL1 |= RB_I2C_STOP;
    HAL_I2C_WaitStopDone(hi2c);
    hi2c->State = HAL_I2C_STATE_READY;

    if ((st == HAL_OK) && (hi2c->ErrorCode != HAL_I2C_ERROR_NONE)) {
        st = HAL_ERROR;
    }

    return st;
}

/**
 * @brief  主机接收
 * @param  hi2c    句柄
 * @param  devAddr 8 位左对齐器件地址
 * @param  buf     接收缓冲
 * @param  len     字节数
 * @param  timeout 单位 ms，HAL_MAX_DELAY 不限
 * @retval HAL_OK 成功；HAL_BUSY 忙；HAL_ERROR 参数非法或总线错误；HAL_TIMEOUT 超时
 */
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                         uint8_t *buf, uint16_t len, uint32_t timeout)
{
    I2C_TypeDef *i2c;
    HAL_StatusTypeDef st;
    uint16_t i;
    uint16_t remaining;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if (hi2c->State != HAL_I2C_STATE_READY) {
        return HAL_BUSY;
    }

    i2c = hi2c->Instance;

    hi2c->State     = HAL_I2C_STATE_BUSY_RX;
    hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    hi2c->DevAddr   = devAddr;
    hi2c->Phase     = HAL_I2C_PHASE_DATA;

    st = HAL_I2C_WaitBusFree(hi2c, timeout);
    if (st != HAL_OK) {
        goto exit;
    }

    i2c->CTRL1 |= RB_I2C_START;

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_SB, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearSB(hi2c);

    HAL_I2C_SendAddr(hi2c, 1U);

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_ADDR, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearADDR(hi2c);

    if (len == 1U) {
        i2c->CTRL1 &= (uint16_t)~RB_I2C_ACK;
        i2c->CTRL1 |= RB_I2C_STOP;

        st = HAL_I2C_WaitFlag(hi2c, RB_I2C_RxNE, timeout);
        if (st != HAL_OK) {
            goto exit;
        }

        buf[0] = (uint8_t)i2c->DATAR;
    } else if (len == 2U) {
        i2c->CTRL1 &= (uint16_t)~RB_I2C_ACK;
        i2c->CTRL1 |= RB_I2C_POS;

        st = HAL_I2C_WaitFlag(hi2c, RB_I2C_BTF, timeout);
        if (st != HAL_OK) {
            goto exit;
        }

        i2c->CTRL1 |= RB_I2C_STOP;

        buf[0] = (uint8_t)i2c->DATAR;
        buf[1] = (uint8_t)i2c->DATAR;
    } else {
        for (i = 0U; i < len; i++) {
            remaining = (uint16_t)(len - i);
            HAL_I2C_SetRxEndMode(hi2c, remaining);

            st = HAL_I2C_WaitFlag(hi2c, RB_I2C_RxNE, timeout);
            if (st != HAL_OK) {
                goto exit;
            }

            buf[i] = (uint8_t)i2c->DATAR;
        }
    }

exit:
    i2c->CTRL1 |= RB_I2C_STOP;
    HAL_I2C_WaitStopDone(hi2c);
    i2c->CTRL1 |= RB_I2C_ACK;
    i2c->CTRL1 &= (uint16_t)~RB_I2C_POS;
    hi2c->State = HAL_I2C_STATE_READY;

    if ((st == HAL_OK) && (hi2c->ErrorCode != HAL_I2C_ERROR_NONE)) {
        st = HAL_ERROR;
    }

    return st;
}

/**
 * @brief  写器件寄存器
 * @param  hi2c        句柄
 * @param  devAddr     8 位左对齐器件地址
 * @param  memAddr     寄存器地址
 * @param  memAddrSize 寄存器地址字节数，1 或 2
 * @param  buf         待写数据
 * @param  len         字节数
 * @param  timeout     单位 ms
 * @retval HAL_OK 成功；HAL_BUSY 忙；HAL_ERROR 参数非法或总线错误；HAL_TIMEOUT 超时
 */
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                    uint16_t memAddr, uint8_t memAddrSize,
                                    const uint8_t *buf, uint16_t len, uint32_t timeout)
{
    I2C_TypeDef *i2c;
    HAL_StatusTypeDef st;
    uint16_t i;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U) || ((memAddrSize != 1U) && (memAddrSize != 2U))) {
        return HAL_ERROR;
    }
    if (hi2c->State != HAL_I2C_STATE_READY) {
        return HAL_BUSY;
    }

    i2c = hi2c->Instance;

    hi2c->State       = HAL_I2C_STATE_BUSY_MEM_TX;
    hi2c->ErrorCode   = HAL_I2C_ERROR_NONE;
    hi2c->DevAddr     = devAddr;
    hi2c->MemAddr     = memAddr;
    hi2c->MemAddrSize = memAddrSize;
    hi2c->Phase       = HAL_I2C_PHASE_MEM_ADDR;

    st = HAL_I2C_WaitBusFree(hi2c, timeout);
    if (st != HAL_OK) {
        goto exit;
    }

    i2c->CTRL1 |= RB_I2C_START;

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_SB, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearSB(hi2c);

    HAL_I2C_SendAddr(hi2c, 0U);

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_ADDR, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearADDR(hi2c);

    /* 寄存器地址 */
    while (hi2c->MemAddrSize != 0U) {
        st = HAL_I2C_WaitFlag(hi2c, RB_I2C_TxE, timeout);
        if (st != HAL_OK) {
            goto exit;
        }

        (void)HAL_I2C_SendMemAddrByte(hi2c);
    }

    hi2c->Phase = HAL_I2C_PHASE_DATA;

    /* 数据 */
    for (i = 0U; i < len; i++) {
        st = HAL_I2C_WaitFlag(hi2c, RB_I2C_TxE, timeout);
        if (st != HAL_OK) {
            goto exit;
        }

        i2c->DATAR = buf[i];
    }

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_BTF, timeout);

exit:
    i2c->CTRL1 |= RB_I2C_STOP;
    HAL_I2C_WaitStopDone(hi2c);
    hi2c->Phase = HAL_I2C_PHASE_DATA;
    hi2c->State = HAL_I2C_STATE_READY;

    if ((st == HAL_OK) && (hi2c->ErrorCode != HAL_I2C_ERROR_NONE)) {
        st = HAL_ERROR;
    }

    return st;
}

/**
 * @brief  读器件寄存器
 * @param  hi2c        句柄
 * @param  devAddr     8 位左对齐器件地址
 * @param  memAddr     寄存器地址
 * @param  memAddrSize 寄存器地址字节数，1 或 2
 * @param  buf         接收缓冲
 * @param  len         字节数
 * @param  timeout     单位 ms
 * @retval HAL_OK 成功；HAL_BUSY 忙；HAL_ERROR 参数非法或总线错误；HAL_TIMEOUT 超时
 */
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                   uint16_t memAddr, uint8_t memAddrSize,
                                   uint8_t *buf, uint16_t len, uint32_t timeout)
{
    I2C_TypeDef *i2c;
    HAL_StatusTypeDef st;
    uint16_t i;
    uint16_t remaining;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U) || ((memAddrSize != 1U) && (memAddrSize != 2U))) {
        return HAL_ERROR;
    }
    if (hi2c->State != HAL_I2C_STATE_READY) {
        return HAL_BUSY;
    }

    i2c = hi2c->Instance;

    hi2c->State       = HAL_I2C_STATE_BUSY_MEM_RX;
    hi2c->ErrorCode   = HAL_I2C_ERROR_NONE;
    hi2c->DevAddr     = devAddr;
    hi2c->MemAddr     = memAddr;
    hi2c->MemAddrSize = memAddrSize;
    hi2c->Phase       = HAL_I2C_PHASE_MEM_ADDR;

    st = HAL_I2C_WaitBusFree(hi2c, timeout);
    if (st != HAL_OK) {
        goto exit;
    }

    /* 第一段：写方向，发寄存器地址 */
    i2c->CTRL1 |= RB_I2C_START;

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_SB, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearSB(hi2c);

    HAL_I2C_SendAddr(hi2c, 0U);

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_ADDR, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearADDR(hi2c);

    while (hi2c->MemAddrSize != 0U) {
        st = HAL_I2C_WaitFlag(hi2c, RB_I2C_TxE, timeout);
        if (st != HAL_OK) {
            goto exit;
        }

        (void)HAL_I2C_SendMemAddrByte(hi2c);
    }

    /* 等最后一个地址字节真正发出，才能打 ReSTART */
    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_BTF, timeout);
    if (st != HAL_OK) {
        goto exit;
    }

    /* 第二段：重复起始，切读方向 */
    i2c->CTRL1 |= RB_I2C_START;

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_SB, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearSB(hi2c);

    HAL_I2C_SendAddr(hi2c, 1U);

    st = HAL_I2C_WaitFlag(hi2c, RB_I2C_ADDR, timeout);
    if (st != HAL_OK) {
        goto exit;
    }
    HAL_I2C_ClearADDR(hi2c);

    if (len == 1U) {
        i2c->CTRL1 &= (uint16_t)~RB_I2C_ACK;
        i2c->CTRL1 |= RB_I2C_STOP;

        st = HAL_I2C_WaitFlag(hi2c, RB_I2C_RxNE, timeout);
        if (st != HAL_OK) {
            goto exit;
        }

        buf[0] = (uint8_t)i2c->DATAR;
    } else if (len == 2U) {
        i2c->CTRL1 &= (uint16_t)~RB_I2C_ACK;
        i2c->CTRL1 |= RB_I2C_POS;

        st = HAL_I2C_WaitFlag(hi2c, RB_I2C_BTF, timeout);
        if (st != HAL_OK) {
            goto exit;
        }

        i2c->CTRL1 |= RB_I2C_STOP;

        buf[0] = (uint8_t)i2c->DATAR;
        buf[1] = (uint8_t)i2c->DATAR;
    } else {
        for (i = 0U; i < len; i++) {
            remaining = (uint16_t)(len - i);
            HAL_I2C_SetRxEndMode(hi2c, remaining);

            st = HAL_I2C_WaitFlag(hi2c, RB_I2C_RxNE, timeout);
            if (st != HAL_OK) {
                goto exit;
            }

            buf[i] = (uint8_t)i2c->DATAR;
        }
    }

exit:
    i2c->CTRL1 |= RB_I2C_STOP;
    HAL_I2C_WaitStopDone(hi2c);
    i2c->CTRL1 |= RB_I2C_ACK;
    i2c->CTRL1 &= (uint16_t)~RB_I2C_POS;
    hi2c->Phase = HAL_I2C_PHASE_DATA;
    hi2c->State = HAL_I2C_STATE_READY;

    if ((st == HAL_OK) && (hi2c->ErrorCode != HAL_I2C_ERROR_NONE)) {
        st = HAL_ERROR;
    }

    return st;
}

/**
 * @brief  探测器件是否在线（只发地址，看是否应答）
 * @param  hi2c    句柄
 * @param  devAddr 8 位左对齐器件地址
 * @param  trials  重试次数
 * @param  timeout 每次尝试的超时，单位 ms
 * @retval HAL_OK 器件应答；HAL_ERROR 无应答；HAL_BUSY 忙；HAL_TIMEOUT 超时
 */
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                        uint32_t trials, uint32_t timeout)
{
    I2C_TypeDef *i2c;
    HAL_StatusTypeDef st = HAL_ERROR;
    uint32_t n;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (trials == 0U) {
        return HAL_ERROR;
    }
    if (hi2c->State != HAL_I2C_STATE_READY) {
        return HAL_BUSY;
    }

    i2c = hi2c->Instance;

    hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    hi2c->DevAddr   = devAddr;

    for (n = 0U; n < trials; n++) {
        hi2c->State = HAL_I2C_STATE_BUSY_TX;

        if (HAL_I2C_WaitBusFree(hi2c, timeout) != HAL_OK) {
            st = HAL_TIMEOUT;
            break;
        }

        i2c->CTRL1 |= RB_I2C_START;

        if (HAL_I2C_WaitFlag(hi2c, RB_I2C_SB, timeout) != HAL_OK) {
            st = HAL_TIMEOUT;
            break;
        }
        HAL_I2C_ClearSB(hi2c);

        HAL_I2C_SendAddr(hi2c, 0U);

        /* 有应答或 AF 都算"有结果"，AF 代表器件不在线 */
        {
            uint32_t start = HAL_GetTick();
            uint16_t star1;

            for (;;) {
                star1 = i2c->STAR1;

                if ((star1 & (RB_I2C_ADDR | RB_I2C_AF)) != 0U) {
                    break;
                }

                if ((timeout != HAL_MAX_DELAY) && ((HAL_GetTick() - start) >= timeout)) {
                    st = HAL_TIMEOUT;
                    break;
                }
            }

            if (st == HAL_TIMEOUT) {
                break;
            }

            if ((star1 & RB_I2C_ADDR) != 0U) {
                HAL_I2C_ClearADDR(hi2c);
                st = HAL_OK;
            } else {
                i2c->STAR1 = (uint16_t)~RB_I2C_AF;
                hi2c->ErrorCode |= HAL_I2C_ERROR_AF;
                st = HAL_ERROR;
            }
        }

        i2c->CTRL1 |= RB_I2C_STOP;
        HAL_I2C_WaitStopDone(hi2c);
        hi2c->State = HAL_I2C_STATE_READY;

        if (st == HAL_OK) {
            break;
        }
    }

    hi2c->State = HAL_I2C_STATE_READY;

    return st;
}

/* ===================== 主机中断传输 ===================== */

/**
 * @brief  启动一次中断传输的公共部分
 */
static HAL_StatusTypeDef HAL_I2C_StartIT(I2C_HandleTypeDef *hi2c, uint8_t state,
                                         uint16_t devAddr, uint8_t *buf, uint16_t len)
{
    if (hi2c->State != HAL_I2C_STATE_READY) {
        return HAL_BUSY;
    }
    if ((buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }
    if ((hi2c->Instance->STAR2 & RB_I2C_BUSY) != 0U) {
        return HAL_BUSY;
    }

    hi2c->pBuf      = buf;
    hi2c->XferSize  = len;
    hi2c->XferCount = 0U;
    hi2c->DevAddr   = devAddr;
    hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    hi2c->Phase     = HAL_I2C_PHASE_DATA;
    hi2c->State     = state;

    HAL_I2C_EnableIT(hi2c);
    hi2c->Instance->CTRL1 |= RB_I2C_START;

    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                             uint8_t *buf, uint16_t len)
{
    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }

    return HAL_I2C_StartIT(hi2c, HAL_I2C_STATE_BUSY_TX, devAddr, buf, len);
}

HAL_StatusTypeDef HAL_I2C_Master_Receive_IT(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                            uint8_t *buf, uint16_t len)
{
    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }

    return HAL_I2C_StartIT(hi2c, HAL_I2C_STATE_BUSY_RX, devAddr, buf, len);
}

HAL_StatusTypeDef HAL_I2C_Mem_Write_IT(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                       uint16_t memAddr, uint8_t memAddrSize,
                                       uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef st;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((memAddrSize != 1U) && (memAddrSize != 2U)) {
        return HAL_ERROR;
    }

    st = HAL_I2C_StartIT(hi2c, HAL_I2C_STATE_BUSY_MEM_TX, devAddr, buf, len);

    if (st == HAL_OK) {
        hi2c->MemAddr     = memAddr;
        hi2c->MemAddrSize = memAddrSize;
        hi2c->Phase       = HAL_I2C_PHASE_MEM_ADDR;
    }

    return st;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *hi2c, uint16_t devAddr,
                                      uint16_t memAddr, uint8_t memAddrSize,
                                      uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef st;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((memAddrSize != 1U) && (memAddrSize != 2U)) {
        return HAL_ERROR;
    }

    st = HAL_I2C_StartIT(hi2c, HAL_I2C_STATE_BUSY_MEM_RX, devAddr, buf, len);

    if (st == HAL_OK) {
        hi2c->MemAddr     = memAddr;
        hi2c->MemAddrSize = memAddrSize;
        hi2c->Phase       = HAL_I2C_PHASE_MEM_ADDR;
    }

    return st;
}

/**
 * @brief  中止中断传输
 * @param  hi2c 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空
 * @note   打 STOP 并关中断，不触发完成回调。
 */
HAL_StatusTypeDef HAL_I2C_Abort_IT(I2C_HandleTypeDef *hi2c)
{
    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_ERROR;
    }

    hi2c->Instance->CTRL1 |= RB_I2C_STOP;
    HAL_I2C_WaitStopDone(hi2c);
    HAL_I2C_EndTransfer(hi2c);

    return HAL_OK;
}

/* ===================== 状态查询 ===================== */

HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL) {
        return HAL_I2C_STATE_READY;
    }

    return (HAL_I2C_StateTypeDef)hi2c->State;
}

uint8_t HAL_I2C_GetError(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL) {
        return HAL_I2C_ERROR_NONE;
    }

    return hi2c->ErrorCode;
}

uint32_t HAL_I2C_GetLastEvent(I2C_HandleTypeDef *hi2c)
{
    uint32_t flag1;
    uint32_t flag2;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return 0U;
    }

    flag1 = hi2c->Instance->STAR1;
    flag2 = (uint32_t)hi2c->Instance->STAR2 << 16;

    return ((flag1 | flag2) & 0x00FFFFFFU);
}

uint8_t HAL_I2C_CheckEvent(I2C_HandleTypeDef *hi2c, uint32_t event)
{
    uint32_t last = HAL_I2C_GetLastEvent(hi2c);

    return ((last & event) == event) ? 1U : 0U;
}

/**
 * @brief  软件复位 I2C 外设
 * @param  hi2c 句柄
 * @note   用于总线卡死后恢复；复位后需要重新调用 HAL_I2C_Init。
 */
void HAL_I2C_Reset(I2C_HandleTypeDef *hi2c)
{
    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return;
    }

    HAL_I2C_DisableIT(hi2c);

    hi2c->Instance->CTRL1 |= RB_I2C_SWRST;
    hi2c->Instance->CTRL1 &= (uint16_t)~RB_I2C_SWRST;

    hi2c->State     = HAL_I2C_STATE_READY;
    hi2c->Phase     = HAL_I2C_PHASE_DATA;
    hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
}

/* ===================== 中断入口与回调 ===================== */

/**
 * @brief  I2C 中断处理
 * @param  hi2c 句柄，由 _it.c 里的 I2C_IRQHandler 传入
 * @note   单条中断线，三个使能位（EVT/BUF/ERR）共用，靠 STAR1 判断具体事件。
 *         错误优先处理：出错时打 STOP、关中断、回调，避免反复进中断。
 *         标 __HIGH_CODE 是因为 flash 擦写期间 XROM 不响应取指，
 *         中断体必须整个待在 RAM 里。
 */
__HIGH_CODE
void HAL_I2C_IRQHandler(I2C_HandleTypeDef *hi2c)
{
    I2C_TypeDef *i2c;
    uint16_t star1;
    uint16_t remaining;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return;
    }

    i2c   = hi2c->Instance;
    star1 = i2c->STAR1;

    /* ---- 1. 错误优先 ---- */
    if (HAL_I2C_CatchError(hi2c, star1) != 0U) {
        i2c->CTRL1 |= RB_I2C_STOP;
        HAL_I2C_EndTransfer(hi2c);
        HAL_I2C_ErrorCallback(hi2c);
        return;
    }

    /* ---- 2. START 已发出，发地址 ---- */
    if ((star1 & RB_I2C_SB) != 0U) {
        HAL_I2C_ClearSB(hi2c);

        if (hi2c->Phase == HAL_I2C_PHASE_RESTART) {
            HAL_I2C_SendAddr(hi2c, 1U);
            hi2c->Phase = HAL_I2C_PHASE_DATA;
        } else if ((hi2c->State == HAL_I2C_STATE_BUSY_RX) ||
                   (hi2c->State == HAL_I2C_STATE_BUSY_MEM_RX)) {
            HAL_I2C_SendAddr(hi2c, 1U);
        } else {
            HAL_I2C_SendAddr(hi2c, 0U);
        }
        return;
    }

    /* ---- 3. 地址被应答 ---- */
    if ((star1 & RB_I2C_ADDR) != 0U) {
        HAL_I2C_ClearADDR(hi2c);

        if (hi2c->Phase == HAL_I2C_PHASE_MEM_ADDR) {
            (void)HAL_I2C_SendMemAddrByte(hi2c);
            return;
        }

        if ((hi2c->State == HAL_I2C_STATE_BUSY_RX) ||
            (hi2c->State == HAL_I2C_STATE_BUSY_MEM_RX)) {
            if (hi2c->XferSize == 1U) {
                i2c->CTRL1 &= (uint16_t)~RB_I2C_ACK;
                i2c->CTRL1 |= RB_I2C_STOP;
            } else if (hi2c->XferSize == 2U) {
                i2c->CTRL1 &= (uint16_t)~RB_I2C_ACK;
                i2c->CTRL1 |= RB_I2C_POS;
            } else {
                /* 更长的接收，按剩余字节数逐步处理 */
            }
        } else {
            /* 发送方向：填第一个字节，len 为 0 不会进到这里 */
            i2c->DATAR      = hi2c->pBuf[0];
            hi2c->XferCount = 1U;
        }
        return;
    }

    /* ---- 4. 发送缓冲空 ---- */
    if (((star1 & RB_I2C_TxE) != 0U) &&
        ((hi2c->State == HAL_I2C_STATE_BUSY_TX) || (hi2c->State == HAL_I2C_STATE_BUSY_MEM_TX))) {
        if (hi2c->Phase == HAL_I2C_PHASE_MEM_ADDR) {
            if (HAL_I2C_SendMemAddrByte(hi2c) == 0U) {
                hi2c->Phase = HAL_I2C_PHASE_DATA;
            }
        } else if (hi2c->XferCount < hi2c->XferSize) {
            i2c->DATAR = hi2c->pBuf[hi2c->XferCount];
            hi2c->XferCount++;
        } else {
            /* 数据已发完，等 BTF */
        }
        return;
    }

    /* ---- 5. 寄存器地址发完，打 ReSTART 切读 ---- */
    if (((star1 & RB_I2C_TxE) != 0U) && (hi2c->State == HAL_I2C_STATE_BUSY_MEM_RX) &&
        (hi2c->Phase == HAL_I2C_PHASE_MEM_ADDR) && (hi2c->MemAddrSize == 0U)) {
        hi2c->Phase = HAL_I2C_PHASE_RESTART;
        i2c->CTRL1 |= RB_I2C_START;
        return;
    }

    /* ---- 6. 字节传输完成 ---- */
    if ((star1 & RB_I2C_BTF) != 0U) {
        i2c->CTRL1 |= RB_I2C_STOP;
        HAL_I2C_EndTransfer(hi2c);

        switch (hi2c->State) {
            case HAL_I2C_STATE_BUSY_TX:
                HAL_I2C_MasterTxCpltCallback(hi2c);
                break;

            case HAL_I2C_STATE_BUSY_MEM_TX:
                HAL_I2C_MemTxCpltCallback(hi2c);
                break;

            default:
                break;
        }
        return;
    }

    /* ---- 7. 接收缓冲非空 ---- */
    if (((star1 & RB_I2C_RxNE) != 0U) &&
        ((hi2c->State == HAL_I2C_STATE_BUSY_RX) || (hi2c->State == HAL_I2C_STATE_BUSY_MEM_RX))) {
        remaining = (uint16_t)(hi2c->XferSize - hi2c->XferCount);
        HAL_I2C_SetRxEndMode(hi2c, remaining);

        hi2c->pBuf[hi2c->XferCount] = (uint8_t)i2c->DATAR;
        hi2c->XferCount++;

        if (hi2c->XferCount >= hi2c->XferSize) {
            i2c->CTRL1 |= RB_I2C_STOP;
            {
                uint8_t state = hi2c->State;
                HAL_I2C_EndTransfer(hi2c);

                if (state == HAL_I2C_STATE_BUSY_RX) {
                    HAL_I2C_MasterRxCpltCallback(hi2c);
                } else {
                    HAL_I2C_MemRxCpltCallback(hi2c);
                }
            }
        }
        return;
    }

    /* 其余位（ADD10 / STOPF）本轮不使用 */
}

__weak void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    UNUSED(hi2c);
}

__weak void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    UNUSED(hi2c);
}

__weak void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    UNUSED(hi2c);
}

__weak void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    UNUSED(hi2c);
}

__weak void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    UNUSED(hi2c);
}

__weak void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    UNUSED(hi2c);
}

__weak void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
    UNUSED(hi2c);
}
