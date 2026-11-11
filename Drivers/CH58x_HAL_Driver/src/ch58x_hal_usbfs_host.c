#include "ch58x_hal.h"
#include "ch58x_hal_usbfs_host.h"

/* ===================== 主机模式 =====================
 * 寄存器复用关系（CH58x 硬件固定，不是本模块的取舍）：
 *   UEP1_CTRL   == UH_SETUP      SOF 使能 / PRE-PID 使能
 *   UEP2_DMA    == UH_RX_DMA     IN 方向缓冲
 *   UEP2_T_LEN  == UH_EP_PID     (PID << 4) | 端点号
 *   UEP2_CTRL   == UH_RX_CTRL
 *   UEP3_DMA    == UH_TX_DMA     OUT/SETUP 方向缓冲
 *   UEP3_T_LEN  == UH_TX_LEN
 *   UEP3_CTRL   == UH_TX_CTRL
 *   UEP2_3_MOD  == UH_EP_MOD     RB_UH_EP_TX_EN / RB_UH_EP_RX_EN
 * 因此主机模式与设备模式互斥，运行期不可切换。
 */

#define HAL_USBFS_HST_RESET_MS 20U

/**
 * @brief  安装主机收发缓冲并配置端点
 * @param  rxBuf IN 方向接收缓冲
 * @param  txBuf OUT/SETUP 方向发送缓冲
 * @param  mode  缓冲模式：单 64 字节 / 双 64 字节（双缓冲需 128 字节，由 TOG 选半区）
 * @retval HAL_OK 成功；HAL_ERROR 指针为空、未 4 字节对齐，或 mode 非法
 */
HAL_StatusTypeDef HAL_USBFS_HST_EP_SetBuffer(uint8_t *rxBuf, uint8_t *txBuf,
                                             HAL_USBFS_BufModeTypeDef mode)
{
    if ((rxBuf == NULL) || (txBuf == NULL)) {
        return HAL_ERROR;
    }
    if ((((uint32_t)rxBuf | (uint32_t)txBuf) & 0x03U) != 0U) {
        return HAL_ERROR; /* 硬件按 4 字节访问 */
    }
    if ((mode != HAL_USBFS_BUF_SINGLE) && (mode != HAL_USBFS_BUF_DOUBLE)) {
        return HAL_ERROR;
    }

    USBFS->UEP2_DMA = (uint32_t)rxBuf; /* UH_RX_DMA */
    USBFS->UEP3_DMA = (uint32_t)txBuf; /* UH_TX_DMA */

    /* 打开主机收发通道 */
    USBFS->UEP2_3_MOD |= (uint8_t)(RB_UH_EP_TX_EN | RB_UH_EP_RX_EN);

    /* 双缓冲位与设备侧的 BUF_MOD 同在一个寄存器（手册表17-7/表17-8）：
     * 双缓冲时收发各占 128 字节，由 RB_UH_T_TOG / RB_UH_R_TOG 选半区。 */
    if (mode == HAL_USBFS_BUF_DOUBLE) {
        USBFS->UEP2_3_MOD |= (uint8_t)(RB_UH_EP_TBUF_MOD | RB_UH_EP_RBUF_MOD);
    } else {
        USBFS->UEP2_3_MOD &= (uint8_t)~(RB_UH_EP_TBUF_MOD | RB_UH_EP_RBUF_MOD);
    }

    /* 收发都开自动翻转；应答置 ACK（RB_UH_R_RES / RB_UH_T_RES 均为 0 = ACK） */
    USBFS->UEP2_CTRL = RB_UH_R_AUTO_TOG;
    USBFS->UEP3_CTRL = RB_UH_T_AUTO_TOG;

    return HAL_OK;
}

/**
 * @brief  低速前导包（PRE PID）使能
 * @note   R8_UH_SETUP bit7。经外部全速 HUB 与低速设备通讯时必须置位。
 */
HAL_StatusTypeDef HAL_USBFS_HST_SetPreamble(FunctionalState s)
{
    if (s != DISABLE) {
        USBFS->UEP1_CTRL |= RB_UH_PRE_PID_EN;
    } else {
        USBFS->UEP1_CTRL &= (uint8_t)~RB_UH_PRE_PID_EN;
    }

    return HAL_OK;
}

/**
 * @brief  主机端口低速模式（R8_UHOST_CTRL bit2）
 * @note   1 = 1.5Mbps；0 = 12Mbps。用于直接连低速设备（不经 HUB）。
 */
HAL_StatusTypeDef HAL_USBFS_HST_SetLowSpeed(FunctionalState s)
{
    if (s != DISABLE) {
        USBFS->DEV_HOST_CTRL |= RB_UH_LOW_SPEED;
    } else {
        USBFS->DEV_HOST_CTRL &= (uint8_t)~RB_UH_LOW_SPEED;
    }

    return HAL_OK;
}

/**
 * @brief  打开/关闭自动 SOF
 * @note   关掉 SOF 后设备侧会进挂起，只在省电场景用。
 */
HAL_StatusTypeDef HAL_USBFS_HST_SetSofEnable(FunctionalState s)
{
    if (s != DISABLE) {
        USBFS->UEP1_CTRL |= RB_UH_SOF_EN;
    } else {
        USBFS->UEP1_CTRL &= (uint8_t)~RB_UH_SOF_EN;
    }

    return HAL_OK;
}

/**
 * @brief  使能/关闭端口
 */
HAL_StatusTypeDef HAL_USBFS_HST_SetPortEnable(FunctionalState s)
{
    if (s != DISABLE) {
        USBFS->DEV_HOST_CTRL |= RB_UH_PORT_EN;
    } else {
        USBFS->DEV_HOST_CTRL &= (uint8_t)~RB_UH_PORT_EN;
    }

    return HAL_OK;
}

/**
 * @brief  发出总线复位
 * @note   阻塞 20ms，满足 USB 规范「复位至少 10ms」的要求。
 *         CH58x 没有硬件计时，只能靠软件延时。
 */
HAL_StatusTypeDef HAL_USBFS_HST_ResetBus(void)
{
    USBFS->DEV_HOST_CTRL |= RB_UH_BUS_RESET;
    HAL_Delay(HAL_USBFS_HST_RESET_MS);
    USBFS->DEV_HOST_CTRL &= (uint8_t)~RB_UH_BUS_RESET;

    /* 复位后设备地址归零，主机侧也要跟着清 */
    USBFS->DEV_AD = 0U;

    return HAL_OK;
}

/**
 * @brief  复位收发数据翻转位
 * @note   控制传输每次进入 SETUP 阶段前必须调用，否则 DATA0/DATA1 会错位。
 */
HAL_StatusTypeDef HAL_USBFS_HST_ResetToggle(void)
{
    USBFS->UEP2_CTRL = RB_UH_R_AUTO_TOG; /* 清 RB_UH_R_TOG */
    USBFS->UEP3_CTRL = RB_UH_T_AUTO_TOG; /* 清 RB_UH_T_TOG */

    return HAL_OK;
}

/**
 * @brief  执行一次 USB 事务，阻塞直到完成或超时
 * @param  pid       令牌：USB_PID_SETUP / USB_PID_IN / USB_PID_OUT
 * @param  ep        端点号 0~7
 * @param  len       发送长度；IN 事务忽略
 * @param  timeoutMs 超时（毫秒）
 * @retval 握手结果
 * @note   不能放在中断里调用：超时依赖 HAL_GetTick()。
 */
HAL_USBFS_HST_ResultTypeDef HAL_USBFS_HST_Transact(uint8_t pid, uint8_t ep,
                                                   uint16_t len, uint16_t timeoutMs)
{
    uint32_t start;
    uint8_t res;

    if ((ep > HAL_USBFS_EP_MAX) || (len > HAL_USBFS_EP_MPS_MAX)) {
        return HAL_USBFS_HST_ERROR;
    }

    if ((pid == USB_PID_OUT) || (pid == USB_PID_SETUP)) {
        USBFS->UEP3_TLEN = (uint8_t)len; /* UH_TX_LEN */
    }

    start = HAL_GetTick();

    for (;;) {
        USBFS->INT_FG    = RB_UIF_TRANSFER;                      /* 先清标志 */
        USBFS->UEP2_TLEN = (uint8_t)((pid << 4) | (ep & 0x0FU)); /* 发令牌 */

        while ((USBFS->INT_FG & RB_UIF_TRANSFER) == 0U) {
            if ((HAL_GetTick() - start) > timeoutMs) {
                USBFS->UEP2_TLEN = 0U; /* 停令牌，免得硬件继续发 */
                return HAL_USBFS_HST_TIMEOUT;
            }
        }

        USBFS->UEP2_TLEN = 0U; /* 停令牌 */

        if ((USBFS->INT_ST & RB_UIS_TOG_OK) != 0U) {
            return HAL_USBFS_HST_OK;
        }

        res = (uint8_t)(USBFS->INT_ST & MASK_UIS_H_RES);

        if (res == HAL_USBFS_HST_PID_STALL) {
            return HAL_USBFS_HST_STALL;
        }
        if (res == HAL_USBFS_HST_PID_NAK) {
            return HAL_USBFS_HST_NAK;
        }

        /* 其它（CRC 错 / 位填充错 / 设备无响应）算总线错误，重试到超时 */
        if ((HAL_GetTick() - start) > timeoutMs) {
            return HAL_USBFS_HST_TIMEOUT;
        }
    }
}

/**
 * @brief  读最近一次 IN 事务收到的字节数
 */
uint8_t HAL_USBFS_HST_GetRxLen(void)
{
    return USBFS->RX_LEN;
}

/**
 * @brief  当前是否有设备挂在总线上
 */
uint8_t HAL_USBFS_HST_IsDetected(void)
{
    return ((USBFS->MIS_ST & RB_UMS_DEV_ATTACH) != 0U) ? 1U : 0U;
}
