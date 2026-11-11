#include "ch58x_hal_conf.h"

#if (CH58x_CHIP_MODEL == ID_CH584)

typedef int ch58x_hal_usbhs_host_unused_translation_unit_t;

#else

#include "ch58x_hal.h"
#include "ch58x_hal_usbhs.h"
#include "ch58x_hal_usbhs_host.h"

/* ===================== 主机模式 =====================
 * HS 主机是独立寄存器组（+0x100 起），与设备端点寄存器不重叠，
 * 这是它和 FS 主机（复用设备端点）最根本的差别。
 *
 * 事务流程：
 *   1. UH_TX_LEN / UH_RX_MAX_LEN 配长度
 *   2. UH_CONTROL = PID | (ep<<4) | (tog<<8) | HOST_ACTION(bit16)
 *   3. 轮询 UH_INT_FLAG.USB_ACT_IF(bit4)
 *   4. 读 UH_INT_ST.THIS_PID_MASK[3:0] 判定握手
 */

/**
 * @brief  向 PORT_CTRL 下发一条命令
 * @param  cmd 命令位（可或组合）
 * @note   PORT_CTRL 的命令位是**只写**的，读回全 0；
 *         但 bit16 BUS_RST_LONG 和 [15:12] PORT_SLEEP_BESL 是 RW 配置位，
 *         写命令时必须原样保住，否则每发一条命令就重置一次配置。
 */
static void HAL_USBHS_HST_PortCmd(uint32_t cmd)
{
    uint32_t v           = USBHS_HST->PORT_CTRL & HAL_USBHS_HST_PORT_KEEP_MASK;
    USBHS_HST->PORT_CTRL = v | cmd;
}

/**
 * @brief  安装主机收发缓冲
 * @param  rxBuf    IN 方向缓冲
 * @param  txBuf    OUT/SETUP 方向缓冲
 * @param  rxMaxLen IN 事务允许接收的最大字节数
 * @retval HAL_OK 成功；HAL_ERROR 指针为空或未 4 字节对齐
 */
HAL_StatusTypeDef HAL_USBHS_HST_EP_SetBuffer(uint8_t *rxBuf, uint8_t *txBuf, uint16_t rxMaxLen)
{
    if ((rxBuf == NULL) || (txBuf == NULL)) {
        return HAL_ERROR;
    }
    if ((((uint32_t)rxBuf | (uint32_t)txBuf) & 0x03U) != 0U) {
        return HAL_ERROR; /* 硬件按 4 字节访问 */
    }
    if ((rxMaxLen == 0U) || (rxMaxLen > HAL_USBHS_EP_XFER_MAX)) {
        return HAL_ERROR;
    }

    USBHS_HST->RX_DMA     = (uint32_t)rxBuf;
    USBHS_HST->TX_DMA     = (uint32_t)txBuf;
    USBHS_HST->RX_MAX_LEN = rxMaxLen;

    return HAL_OK;
}

/**
 * @brief  打开/关闭自动 SOF 令牌
 * @note   只动 SOF_EN，不打开 SOF 中断。
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetSofEnable(FunctionalState s)
{
    if (s != DISABLE) {
        USBHS_HST->CFG |= HAL_USBHS_HST_CFG_SOF_EN;
    } else {
        USBHS_HST->CFG &= (uint8_t)~HAL_USBHS_HST_CFG_SOF_EN;
    }

    return HAL_OK;
}

/**
 * @brief  设置目标设备地址
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetDeviceAddress(uint8_t addr)
{
    USBHS_HST->DEV_ADDR = addr;
    return HAL_OK;
}

/**
 * @brief  写入 SPLIT 令牌
 * @param  token 用 HAL_USBHS_HST_SPLIT_TOKEN() 拼出来的 19 位值
 * @retval HAL_OK 成功；HAL_ERROR token 超出 19 位
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetSplitToken(uint32_t token)
{
    if ((token & ~HAL_USBHS_HST_SPLIT_MASK) != 0U) {
        return HAL_ERROR;
    }

    USBHS_HST->SPLIT = token;
    return HAL_OK;
}

/**
 * @brief  执行一次 USB 事务，可附加 UH_CONTROL 的额外控制位
 * @param  pid       令牌：HAL_USBHS_PID_SETUP / _IN / _OUT
 * @param  ep        端点号 0~15
 * @param  tog       数据包 PID，取值见 HAL_USBHS_HST_TOG_*
 * @param  len        发送长度；IN 事务忽略
 * @param  ctrlFlags 或进 UH_CONTROL 的额外位
 * @param  timeoutMs 超时（毫秒）
 * @retval 握手结果
 * @note   不能放在中断里调用：超时依赖 HAL_GetTick()。
 */
HAL_USBHS_HST_ResultTypeDef HAL_USBHS_HST_TransactEx(uint8_t pid, uint8_t ep, uint8_t tog,
                                                     uint16_t len, uint32_t ctrlFlags,
                                                     uint16_t timeoutMs)
{
    uint32_t start;
    uint32_t ctrl;
    uint8_t res;

    if ((ep > 15U) || (len > HAL_USBHS_EP_XFER_MAX) || (tog > HAL_USBHS_HST_TOG_MDATA)) {
        return HAL_USBHS_HST_ERROR;
    }

    if ((pid == HAL_USBHS_PID_OUT) || (pid == HAL_USBHS_PID_SETUP)) {
        USBHS_HST->TX_LEN = len;
    }

    ctrl = ((uint32_t)(pid & 0x0FU) << HAL_USBHS_HST_CTRL_PID_POS);
    ctrl |= ((uint32_t)(ep & 0x0FU) << HAL_USBHS_HST_CTRL_EP_POS);
    ctrl |= ((uint32_t)(tog & 0x03U) << HAL_USBHS_HST_CTRL_TOG_POS);
    ctrl |= ctrlFlags;
    ctrl |= HAL_USBHS_HST_CTRL_ACTION; /* 最后置触发位，避免被 ctrlFlags 覆盖前就启动 */

    USBHS_HST->INT_FLAG = HAL_USBHS_HST_INT_TRANSFER; /* 先清 */
    USBHS_HST->CONTROL  = ctrl;

    start = HAL_GetTick();
    while ((USBHS_HST->INT_FLAG & HAL_USBHS_HST_INT_TRANSFER) == 0U) {
        if ((HAL_GetTick() - start) > timeoutMs) {
            USBHS_HST->CONTROL = 0U; /* 停掉事务，免得硬件继续发 */
            return HAL_USBHS_HST_TIMEOUT;
        }
    }

    USBHS_HST->INT_FLAG = HAL_USBHS_HST_INT_TRANSFER;
    USBHS_HST->CONTROL  = 0U;

    res = (uint8_t)(USBHS_HST->INT_ST & HAL_USBHS_HST_PID_MASK);

    switch (res) {
        case HAL_USBHS_PID_ACK:
        case HAL_USBHS_PID_DATA0:
        case HAL_USBHS_PID_DATA1:
            return HAL_USBHS_HST_OK;

        case HAL_USBHS_PID_NAK:
            return HAL_USBHS_HST_NAK;

        case HAL_USBHS_PID_NYET:
            return HAL_USBHS_HST_NYET;

        case HAL_USBHS_PID_STALL:
            return HAL_USBHS_HST_STALL;

        default:
            return HAL_USBHS_HST_ERROR;
    }
}

/**
 * @brief  执行一次普通事务（不带 SPLIT / PRE / 同步传输标志）
 */
HAL_USBHS_HST_ResultTypeDef HAL_USBHS_HST_Transact(uint8_t pid, uint8_t ep, uint8_t tog,
                                                   uint16_t len, uint16_t timeoutMs)
{
    return HAL_USBHS_HST_TransactEx(pid, ep, tog, len, 0U, timeoutMs);
}

/**
 * @brief  复位下游设备，阻塞到 PORT_RESET_IF 置位
 * @param  timeoutMs 超时（毫秒）
 * @retval HAL_OK 复位完成；HAL_TIMEOUT 超时
 * @note   复位完成后设备地址回到 0，上层必须重新 SET_ADDRESS。
 *         时序（复位时长）由硬件按 USB 规范自动控制，软件不用自己延时。
 */
HAL_StatusTypeDef HAL_USBHS_HST_ResetPort(uint16_t timeoutMs)
{
    uint32_t start;

    USBHS_HST->PORT_STATUS_CHG = HAL_USBHS_HST_PORT_CHG_RESET; /* 先清旧标志 */
    HAL_USBHS_HST_PortCmd(HAL_USBHS_HST_PORT_SET_RESET);

    start = HAL_GetTick();
    while ((USBHS_HST->PORT_STATUS_CHG & HAL_USBHS_HST_PORT_CHG_RESET) == 0U) {
        if ((HAL_GetTick() - start) > timeoutMs) {
            return HAL_TIMEOUT;
        }
    }
    USBHS_HST->PORT_STATUS_CHG = HAL_USBHS_HST_PORT_CHG_RESET;

    return HAL_OK;
}

/**
 * @brief  关闭端口
 * @note   PORT_CTRL 是写 1 触发的命令寄存器，没有可读的使能位。
 */
HAL_StatusTypeDef HAL_USBHS_HST_DisablePort(void)
{
    HAL_USBHS_HST_PortCmd(HAL_USBHS_HST_PORT_CLR_EN);
    return HAL_OK;
}

/**
 * @brief  选择总线复位时长
 * @param  longReset ENABLE = 18ms，DISABLE = 16ms
 * @note   复位时长由硬件计时，HAL_USBHS_HST_ResetPort() 只负责等 PORT_RESET_IF。
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetResetDuration(FunctionalState longReset)
{
    uint32_t v = USBHS_HST->PORT_CTRL & HAL_USBHS_HST_PORT_BESL_MASK;

    if (longReset != DISABLE) {
        v |= HAL_USBHS_HST_PORT_BUS_RST_LONG;
    }

    USBHS_HST->PORT_CTRL = v;
    return HAL_OK;
}

/**
 * @brief  让端口进入挂起
 */
HAL_StatusTypeDef HAL_USBHS_HST_SuspendPort(void)
{
    HAL_USBHS_HST_PortCmd(HAL_USBHS_HST_PORT_SET_SUSP);
    return HAL_OK;
}

/**
 * @brief  让端口退出挂起
 */
HAL_StatusTypeDef HAL_USBHS_HST_ResumePort(void)
{
    HAL_USBHS_HST_PortCmd(HAL_USBHS_HST_PORT_CLR_SUSP);
    return HAL_OK;
}

/**
 * @brief  让端口进入 SLEEP（LPM）
 * @param  besl 唤醒时间
 * @retval HAL_OK 成功；HAL_ERROR besl 超出 0~15
 * @note   BESL 与 SLEEP 命令在同一次写入里完成：中间不要插第二次写，
 *         否则会多出一个 BESL 已改、SLEEP 未发的中间态。
 */
HAL_StatusTypeDef HAL_USBHS_HST_EnterSleep(HAL_USBHS_HST_BeslTypeDef besl)
{
    uint32_t v;

    if ((uint32_t)besl > 15U) {
        return HAL_ERROR;
    }

    v = USBHS_HST->PORT_CTRL & HAL_USBHS_HST_PORT_RSTLONG_MASK; /* 保住复位时长 */
    v |= ((uint32_t)besl << HAL_USBHS_HST_PORT_BESL_POS);
    v |= HAL_USBHS_HST_PORT_SET_SLEEP;

    USBHS_HST->PORT_CTRL = v;
    return HAL_OK;
}

/**
 * @brief  让端口退出 SLEEP（LPM）
 */
HAL_StatusTypeDef HAL_USBHS_HST_ExitSleep(void)
{
    HAL_USBHS_HST_PortCmd(HAL_USBHS_HST_PORT_CLR_SLEEP);
    return HAL_OK;
}

/**
 * @brief  设置 HS 主机端口测试模式（R8_U2H_PORT_TEST_CT）
 * @param  mode HAL_USBHS_HST_PORT_TEST_* 的位组合，0 = 关闭
 * @retval HAL_OK 成功；HAL_ERROR 含未定义位
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetPortTestMode(uint8_t mode)
{
    if ((mode & (uint8_t)~HAL_USBHS_HST_PORT_TEST_MASK) != 0U) {
        return HAL_ERROR;
    }

    USBHS_HST->PORT_TEST = mode;
    return HAL_OK;
}

/**
 * @brief  读端口状态原始值
 */
uint16_t HAL_USBHS_HST_GetPortStatus(void)
{
    return USBHS_HST->PORT_STATUS;
}

/**
 * @brief  当前是否有设备连接
 */
uint8_t HAL_USBHS_HST_IsConnected(void)
{
    return ((USBHS_HST->PORT_STATUS & HAL_USBHS_HST_PORT_CONNECT) != 0U) ? 1U : 0U;
}

/**
 * @brief  已连接的设备是否运行在高速模式
 * @note   只有 PORT_HIGH_SPD 置位才算高速；低全速设备通过 PRE/SPLIT 处理。
 */
uint8_t HAL_USBHS_HST_IsHighSpeed(void)
{
    return ((USBHS_HST->PORT_STATUS & HAL_USBHS_HST_PORT_HIGH_SPD) != 0U) ? 1U : 0U;
}

/**
 * @brief  读最近一次 IN 事务收到的字节数
 */
uint16_t HAL_USBHS_HST_GetRxLen(void)
{
    return (uint16_t)(USBHS_HST->RX_LEN & 0x07FFU);
}

/**
 * @brief  USB2 主机中断入口，由 USB2_HOST_IRQHandler 调用
 * @param  husb 句柄
 * @note   只处理端口状态变化、恢复和 FIFO 溢出。
 *         事务完成中断默认不开（HAL_USBHS_HST_Transact 是轮询的，
 *         若同时开中断，ISR 会先清掉标志导致轮询永远超时）。
 */
void HAL_USBHS_HST_IRQHandler(HAL_USBHS_HandleTypeDef *husb)
{
    uint8_t flag;
    uint8_t chg;

    if (husb == NULL) {
        return;
    }

    flag = USBHS_HST->INT_FLAG;
    chg  = USBHS_HST->PORT_STATUS_CHG;

    if ((flag & HAL_USBHS_HST_INT_FIFO_OV) != 0U) {
        USBHS_HST->INT_FLAG = HAL_USBHS_HST_INT_FIFO_OV;
        HAL_USBHS_ErrorCallback(husb, 0U);
    }

    /* TX_HALT：发送被中止（下传中读数据溢出，或 EOF2 到点未发完），只清标志 */
    if ((flag & HAL_USBHS_HST_INT_TX_HALT) != 0U) {
        USBHS_HST->INT_FLAG = HAL_USBHS_HST_INT_TX_HALT;
    }

    /* WKUP：唤醒事件，只清标志 */
    if ((flag & HAL_USBHS_HST_INT_WKUP) != 0U) {
        USBHS_HST->INT_FLAG = HAL_USBHS_HST_INT_WKUP;
    }

    /* 总线恢复**没有**独立的中断标志 —— INT_FLAG 的 bit3 是保留位，
     * 虽然 INT_EN 的 bit3 叫 RESUME_ACT，但标志位不存在。
     * 恢复事件只能从 INT_ST.PORT_RX_RESUME(bit4) 读，且该位是 RW，要软件清。 */
    if ((USBHS_HST->INT_ST & HAL_USBHS_HST_INTST_RESUME) != 0U) {
        USBHS_HST->INT_ST &= (uint8_t)~HAL_USBHS_HST_INTST_RESUME;
        HAL_USBHS_ResumeCallback(husb);
    }

    if ((chg & HAL_USBHS_HST_PORT_CHG_CONNECT) != 0U) {
        USBHS_HST->PORT_STATUS_CHG = HAL_USBHS_HST_PORT_CHG_CONNECT;

        if ((USBHS_HST->PORT_STATUS & HAL_USBHS_HST_PORT_CONNECT) != 0U) {
            HAL_USBHS_HST_ConnectCallback(husb);
        } else {
            HAL_USBHS_HST_DisconnectCallback(husb);
        }
    }

    if ((chg & HAL_USBHS_HST_PORT_CHG_RESET) != 0U) {
        USBHS_HST->PORT_STATUS_CHG = HAL_USBHS_HST_PORT_CHG_RESET;
        HAL_USBHS_HST_PortResetCallback(husb);
    }

    /* 其余状态变化位读掉，避免同一事件反复进中断 */
    USBHS_HST->PORT_STATUS_CHG = (uint8_t)(chg & (HAL_USBHS_HST_PORT_CHG_EN | HAL_USBHS_HST_PORT_CHG_SUSP | HAL_USBHS_HST_PORT_CHG_SLEEP));
}

/* ===================== 弱回调默认实现 ===================== */

__weak void HAL_USBHS_HST_ConnectCallback(HAL_USBHS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

__weak void HAL_USBHS_HST_DisconnectCallback(HAL_USBHS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

__weak void HAL_USBHS_HST_PortResetCallback(HAL_USBHS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

#endif /* CH58x_CHIP_MODEL == ID_CH584 */
