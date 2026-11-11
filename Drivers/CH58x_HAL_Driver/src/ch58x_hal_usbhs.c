#include "ch58x_hal_conf.h"

#if (CH58x_CHIP_MODEL == ID_CH584)

/* CH584 没有 USB2（高速）模块：整个翻译单元编成空，
 * 这样工程里保留本文件也不会断链，属于「裁剪」而不是功能开关。
 * 见 CH584 数据手册：「CH584 基于 CH585 简化，去掉了一组 USBHS 和 SPI1 模块」。 */
typedef int ch58x_hal_usbhs_unused_translation_unit_t;

#else

#include "ch58x_hal.h"
#include "ch58x_hal_pwr.h"
#include "ch58x_hal_usbhs.h"

#include <stddef.h>

/* ===================== 寄存器布局校验 =====================
 * 期望值取自 CH585.svd（MounRiver SDK）中 USBHS 外设的 addressOffset。
 * SVD 是唯一给出 USB2 位域定义的官方文件，CH585SFR.h 只给了寄存器地址。
 */
_Static_assert(offsetof(USBHS_DEV_TypeDef, CTRL) == 0x00, "USB2_CTRL @ +0x00");
_Static_assert(offsetof(USBHS_DEV_TypeDef, BASE_MODE) == 0x01, "USB2_BASE_MODE @ +0x01");
_Static_assert(offsetof(USBHS_DEV_TypeDef, INT_EN) == 0x02, "USB2_INT_EN @ +0x02");
_Static_assert(offsetof(USBHS_DEV_TypeDef, DEV_AD) == 0x03, "USB2_DEV_AD @ +0x03");
_Static_assert(offsetof(USBHS_DEV_TypeDef, INT_FG) == 0x08, "USB2_INT_FG @ +0x08");
_Static_assert(offsetof(USBHS_DEV_TypeDef, INT_ST) == 0x09, "USB2_INT_ST @ +0x09");
_Static_assert(offsetof(USBHS_DEV_TypeDef, MIS_ST) == 0x0A, "USB2_MIS_ST @ +0x0A");
_Static_assert(offsetof(USBHS_DEV_TypeDef, TX_EN) == 0x10, "U2EP_TX_EN @ +0x10");
_Static_assert(offsetof(USBHS_DEV_TypeDef, RX_EN) == 0x12, "U2EP_RX_EN @ +0x12");
_Static_assert(offsetof(USBHS_DEV_TypeDef, T_TOG_AUTO) == 0x14, "U2EP_T_TOG_AUTO @ +0x14");
_Static_assert(offsetof(USBHS_DEV_TypeDef, R_TOG_AUTO) == 0x16, "U2EP_R_TOG_AUTO @ +0x16");
_Static_assert(offsetof(USBHS_DEV_TypeDef, AF_MODE) == 0x1C, "U2EP_AF_MODE @ +0x1C");
_Static_assert(offsetof(USBHS_DEV_TypeDef, RX_DMA) == 0x20, "U2EP0_/U2EPn_RX_DMA @ +0x20");
_Static_assert(offsetof(USBHS_DEV_TypeDef, TX_DMA) == 0x40, "U2EPn_TX_DMA @ +0x40");
_Static_assert(offsetof(USBHS_DEV_TypeDef, MAX_LEN) == 0x5C, "U2EPn_MAX_LEN @ +0x5C");
_Static_assert(offsetof(USBHS_DEV_TypeDef, RX_LEN) == 0x7C, "U2EP0_/U2EPn_RX_LEN @ +0x7C");
_Static_assert(offsetof(USBHS_DEV_TypeDef, EP) == 0x9C, "U2EP0_T_LEN @ +0x9C");
_Static_assert(offsetof(USBHS_DEV_TypeDef, T_ISO) == 0xBC, "U2EP_T_ISO @ +0xBC");

/* ===================== 内部工具 ===================== */

/** @brief 最近一次进入挂起的时间戳，用于远程唤醒的 5ms 下限判断 */
static volatile uint32_t s_suspendTick;

/**
 * @brief  端点号合法性检查
 * @note   HS 的 TX_EN / RX_EN 是 16 位（RB_EP0_EN ~ RB_EP15_EN），
 *         但 SFR 与 SVD 都只为 EP0~EP7 提供了 DMA / MAX_LEN / RX_LEN 寄存器，
 *         EP8~EP15 无法承载数据，所以本层只支持 0~7。
 */
static uint8_t HAL_USBHS_EP_Valid(uint8_t ep)
{
    return (ep <= HAL_USBHS_EP_MAX) ? 1U : 0U;
}

/**
 * @brief  端点方向使能
 * @param  ep 端点号 0~7
 * @note   R16_U2EP_TX_EN / RX_EN 是「0~15 端点发送/接收使能」的平坦位图，
 *         bit n 对应 EPn，**包括 EP0**（手册明确有 RB_EP0_EN）。
 *         所以 EP0 也要置位，不能因为它是控制端点就当成"恒开"跳过。
 */
static void HAL_USBHS_EP_SetDir(uint8_t ep, FunctionalState tx, FunctionalState rx)
{
    uint16_t bit = (uint16_t)(1U << ep);

    if (tx != DISABLE) {
        USBHS->TX_EN |= bit;
    } else {
        USBHS->TX_EN &= (uint16_t)~bit;
    }

    if (rx != DISABLE) {
        USBHS->RX_EN |= bit;
    } else {
        USBHS->RX_EN &= (uint16_t)~bit;
    }
}

/* ===================== 板级钩子（默认实现） ===================== */

/**
 * @brief  板级初始化：USB 时钟与引脚
 * @note   CH58x 只有 R16_PIN_CONFIG.RB_PIN_USB2_EN 一个使能位，引脚固定为
 *         PB12(U2D-)/PB13(U2D+)，所有封装一致。
 *         注意 PB12 同时是 SPI0 组1 的 SCS_、I2C 的 SDA、UART1 的 RXD1_，
 *         启用 USB HS 后这三个功能在 PB12 上不可用。
 *         板上需要 VBUS 检测或供电开关时，在 BSP 里强定义覆盖本函数。
 *         480M PLL 和 SIE 复位**不在**这里做（它们是芯片级资源，
 *         放在 MspInit 里会被 BSP 的覆盖版本弄丢），见 HAL_USBHS_Init。
 */
__weak void HAL_USBHS_MspInit(HAL_USBHS_HandleTypeDef *husb)
{
    UNUSED(husb);

    HAL_PWR_ConfigPeriphClock(ENABLE, HAL_PWR_SLPCLK_USB2);
    R16_PIN_CONFIG |= RB_PIN_USB2_EN;
}

/**
 * @brief  板级反初始化
 */
__weak void HAL_USBHS_MspDeInit(HAL_USBHS_HandleTypeDef *husb)
{
    UNUSED(husb);

    R16_PIN_CONFIG &= (uint16_t)~RB_PIN_USB2_EN;
    HAL_PWR_ConfigPeriphClock(DISABLE, HAL_PWR_SLPCLK_USB2);
}

/* ===================== 公共 API ===================== */

/**
 * @brief  初始化 USB2（高速）控制器
 * @param  husb 句柄
 * @param  mode 设备或主机；设备寄存器在 +0x00，主机寄存器在 +0x100，互不重叠
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空或模式非法
 * @note   不建立连接：设备模式还要调 HAL_USBHS_DEV_SetConnect(ENABLE)。
 */
HAL_StatusTypeDef HAL_USBHS_Init(HAL_USBHS_HandleTypeDef *husb, HAL_USBHS_ModeTypeDef mode)
{
    if (husb == NULL) {
        return HAL_ERROR;
    }
    if ((mode != HAL_USBHS_MODE_DEVICE) && (mode != HAL_USBHS_MODE_HOST)) {
        return HAL_ERROR;
    }

    husb->Instance = USBHS;
    husb->Mode     = mode;
    husb->State    = HAL_USBHS_STATE_RESET;

    HAL_USBHS_MspInit(husb);

    /* 480MHz PHY 的 PLL 是芯片级资源，必须在这里开而不是 MspInit：
     * BSP 为了 VBUS 检测覆盖 MspInit 时不会把它弄丢。
     * 手册/ SVD 都没有 PLL 锁定状态位（MIS_ST 的 READY 是 USB 连接状态，不是 PLL 就绪），
     * 只能等固定时间；数据依据与取值理由见 HAL_USBHS_PLL_SETTLE_MS 的说明。 */
    USBHS_PLL |= HAL_USBHS_PLL_EN;
    HAL_Delay(HAL_USBHS_PLL_SETTLE_MS);

    /* 打一次复位把 FIFO 和标志清干净。
     * R8_USB2_CTRL 复位值 0x07 说明这三位是电平型（1 = 保持复位），写 0 才释放。 */
    USBHS->CTRL = HAL_USBHS_CTRL_LINK_RESET | HAL_USBHS_CTRL_SIE_RESET | HAL_USBHS_CTRL_ALL_CLR;
    USBHS->CTRL = 0U;

    USBHS->INT_EN = 0U;
    USBHS->TX_EN  = 0U;
    USBHS->RX_EN  = 0U;
    USBHS->INT_FG = 0xFFU; /* 写 1 清 */
    USBHS->DEV_AD = 0U;
    if (mode == HAL_USBHS_MODE_DEVICE) {
        /* 端口方向必须显式写：R8_U2H_PORT_CFG.HOST_EN 是设备/主机共用的总开关，
         * 从主机模式切回设备模式时如果不清，端口会停在主机方向，
         * DEV_EN 打开了也枚举不上且不报错。 */
        USBHS_HST->PORT_CFG = 0U; /* HOST_EN = 0（设备），PD_EN = 0（设备侧不需要下拉） */

        USBHS->BASE_MODE = HAL_USBHS_SPEED_HIGH;
        /* ⚠ PHY_SUSPENDM 名字是反的：手册写「1 = 正常工作；0 = 挂起」。
         * 复位值 0x07 说明上电时 bit3 = 0，PHY 默认就是挂起的 —— 不置 1 则 USB 完全不动。 */
        USBHS->CTRL   = HAL_USBHS_CTRL_DMA_EN | HAL_USBHS_CTRL_DEV_EN | HAL_USBHS_CTRL_PHY_SUSPENDM;
        USBHS->INT_EN = HAL_USBHS_INT_BUS_RST | HAL_USBHS_INT_SUSPEND | HAL_USBHS_INT_TRANSFER | HAL_USBHS_INT_LINK_RDY | HAL_USBHS_INT_FIFO_OV;
    } else {
        /* 先把主机侧链路/SIE 复位一下，再打开主机模式。
         * R8_U2H_CFG 复位值也是 0x07，与设备侧 CTRL 一样是电平型（1 = 保持复位）。
         * bit3 ROOT_PHY_SUSPENDM 按设备侧同位置的语义（1 = 正常工作）置 1；
         * 这一位只有 SVD 依据，若主机模式完全不响应先查这里。 */
        USBHS_HST->CFG = HAL_USBHS_HST_CFG_LINK_RESET | HAL_USBHS_HST_CFG_SIE_RESET | HAL_USBHS_HST_CFG_ALL_CLR;
        USBHS_HST->CFG = HAL_USBHS_HST_CFG_PHY_SUSPENDM;

        USBHS_HST->PORT_CFG        = HAL_USBHS_HST_PORT_CFG_HOST_EN | HAL_USBHS_HST_PORT_CFG_PD_EN;
        USBHS_HST->PORT_INT_EN     = HAL_USBHS_HST_PORT_IE_CONNECT | HAL_USBHS_HST_PORT_IE_EN | HAL_USBHS_HST_PORT_IE_SUSP | HAL_USBHS_HST_PORT_IE_RESET;
        USBHS_HST->INT_EN          = HAL_USBHS_HST_INT_RESUME | HAL_USBHS_HST_INT_FIFO_OV;
        USBHS_HST->INT_FLAG        = 0xFFU; /* 写 1 清 */
        USBHS_HST->PORT_STATUS_CHG = 0xFFU;
    }

    husb->State = HAL_USBHS_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  关闭 USB2
 */
HAL_StatusTypeDef HAL_USBHS_DeInit(HAL_USBHS_HandleTypeDef *husb)
{
    if (husb == NULL) {
        return HAL_ERROR;
    }

    USBHS->INT_EN = 0U;
    USBHS->CTRL   = HAL_USBHS_CTRL_SIE_RESET;
    USBHS->TX_EN  = 0U;
    USBHS->RX_EN  = 0U;

    /* 把端口方向拉回设备侧：否则下次以设备模式 Init 前，
     * HOST_EN 还留在 1，端口停在主机方向。 */
    USBHS_HST->PORT_CFG = 0U;

    HAL_USBHS_MspDeInit(husb);

    USBHS_PLL &= (uint8_t)~HAL_USBHS_PLL_EN; /* 与 Init 里的使能对称 */

    husb->State = HAL_USBHS_STATE_RESET;

    return HAL_OK;
}

uint8_t HAL_USBHS_GetState(const HAL_USBHS_HandleTypeDef *husb)
{
    return (husb != NULL) ? husb->State : (uint8_t)HAL_USBHS_STATE_RESET;
}

/* ===================== 设备模式 ===================== */

/**
 * @brief  建立/断开设备连接
 * @param  s ENABLE = 打开 D+ 上拉（DEV_EN 已置位后由硬件接管）
 * @note   USB2 没有 FS 那样的"强制上拉"独立位，睡眠期间由 PHY 控制。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_SetConnect(FunctionalState s)
{
    if (s != DISABLE) {
        USBHS->CTRL |= HAL_USBHS_CTRL_DEV_EN;
    } else {
        USBHS->CTRL &= (uint8_t)~HAL_USBHS_CTRL_DEV_EN;
    }

    return HAL_OK;
}

/**
 * @brief  设置期望速度（EXP_SPD）
 * @param  speed HAL_USBHS_SPEED_HIGH / _FULL / _LOW
 * @retval HAL_OK 成功
 * @note   写 R8_USB2_BASE_MODE 低两位。正常产品只应使用 HIGH，
 *         FULL/LOW 仅用于调试和兼容性测试。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_SetSpeed(uint8_t speed)
{
    USBHS->BASE_MODE = (uint8_t)(speed & HAL_USBHS_SPEED_MASK);
    return HAL_OK;
}

/**
 * @brief  设置设备地址
 */
HAL_StatusTypeDef HAL_USBHS_DEV_SetAddress(uint8_t addr)
{
    if (addr > 127U) {
        return HAL_ERROR;
    }

    USBHS->DEV_AD = addr;
    return HAL_OK;
}

/**
 * @brief  打开端点
 * @param  ep     端点号 0~7
 * @param  maxLen 最大包长；EP0 必须 64，高速批量 512，中断 64
 * @param  rxBuf  OUT 方向缓冲，4 字节对齐；传 NULL 表示不改 DMA
 * @param  txBuf  IN 方向缓冲，4 字节对齐；传 NULL 表示不改 DMA
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   EP0 只有一组 DMA（RX_DMA[0]），收发共用，传 txBuf 会被忽略并按 rxBuf 写入。
 *         HS 与 FS 的差别：HS 有 MAX_LEN 和 T_TOG_AUTO / R_TOG_AUTO 寄存器，
 *         所以两端点都能开硬件自动翻转，不需要像 FS 那样只限 EP1~EP3。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Open(uint8_t ep, uint16_t maxLen,
                                        uint8_t *rxBuf, uint8_t *txBuf)
{
    if (HAL_USBHS_EP_Valid(ep) == 0U) {
        return HAL_ERROR;
    }
    if ((((uint32_t)rxBuf | (uint32_t)txBuf) & 0x03U) != 0U) {
        return HAL_ERROR;
    }
    /* 按 USB 规范上限拦住：控制端点最大 64，数据端点最大 1024。
     * 寄存器的位宽（EP0 8 位 / EPn 12 位，见 ch58x_hal_usbhs.h 里的说明）
     * 都比这两个上限宽，所以只要能过这里，写进去就不会被硬件截断。 */
    if (maxLen == 0U) {
        return HAL_ERROR;
    }
    if ((ep == 0U) && (maxLen > HAL_USBHS_EP0_MPS_MAX)) {
        return HAL_ERROR;
    }
    if ((ep != 0U) && (maxLen > HAL_USBHS_EP_XFER_MAX)) {
        return HAL_ERROR;
    }

    if (rxBuf != NULL) {
        USBHS->RX_DMA[ep] = (uint32_t)rxBuf;
    }
    if ((ep != 0U) && (txBuf != NULL)) {
        USBHS->TX_DMA[ep - 1U] = (uint32_t)txBuf;
    }

    USBHS->MAX_LEN[ep] = maxLen;

    /* 方向使能对 EP0 同样有效（TX_EN/RX_EN 的 bit0 = RB_EP0_EN） */
    HAL_USBHS_EP_SetDir(ep, ENABLE, ENABLE);

    if (ep != 0U) {
        /* 自动翻转只给数据端点：控制传输必须软件管 DATA1/DATA0 的切换，
         * EP0 开自动翻转会打乱 SETUP/DATA/STATUS 三阶段的翻转序列。 */
        USBHS->T_TOG_AUTO |= (uint16_t)(1U << ep);
        USBHS->R_TOG_AUTO |= (uint16_t)(1U << ep);
    }

    /* 初始：接收 ACK、发送 NAK */
    USBHS->EP[ep].TX_CTRL = (uint8_t)HAL_USBHS_RES_NAK;
    USBHS->EP[ep].RX_CTRL = (uint8_t)HAL_USBHS_RES_ACK;

    return HAL_OK;
}

/**
 * @brief  关闭端点
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Close(uint8_t ep)
{
    if (HAL_USBHS_EP_Valid(ep) == 0U) {
        return HAL_ERROR;
    }

    HAL_USBHS_EP_SetDir(ep, DISABLE, DISABLE);

    if (ep != 0U) {
        USBHS->T_TOG_AUTO &= (uint16_t)~(1U << ep);
        USBHS->R_TOG_AUTO &= (uint16_t)~(1U << ep);
    }

    USBHS->EP[ep].TX_CTRL = (uint8_t)HAL_USBHS_RES_NAK;
    USBHS->EP[ep].RX_CTRL = (uint8_t)HAL_USBHS_RES_NAK;

    return HAL_OK;
}

/**
 * @brief  配置端点突发（burst）传输
 * @param  ep          端点号 1~7（EP0 是控制端点，硬件没有 burst 概念）
 * @param  enable      ENABLE = 打开该端点的突发（收发两侧同时置位，
 *                     端点通常单向使用，另一个方向置位无副作用）
 * @param  skipZeroLen ENABLE = 突发模式下不发送 0 长度包；DISABLE = 发送
 * @param  nyetReply   ENABLE = 接收数据包时回 NYET；DISABLE = 回 ACK
 * @retval HAL_OK 成功；HAL_ERROR 端点号越界或传了 EP0
 * @note   高速的高带宽靠 burst（一个微帧内连发多包），不开就只能每微帧 1 包，
 *         高速等于白搭。HID 中断端点（64 字节）用不上，批量端点才需要。
 *         四个寄存器都是**按端点的位图**（bit n = EPn），必须读-改-写。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_SetBurst(uint8_t ep, FunctionalState enable,
                                            FunctionalState skipZeroLen,
                                            FunctionalState nyetReply)
{
    uint8_t bit;

    if ((HAL_USBHS_EP_Valid(ep) == 0U) || (ep == 0U)) {
        return HAL_ERROR;
    }

    bit = (uint8_t)(1U << ep);

    if (enable != DISABLE) {
        USBHS->T_BURST |= bit;
        USBHS->R_BURST |= bit;
    } else {
        USBHS->T_BURST &= (uint8_t)~bit;
        USBHS->R_BURST &= (uint8_t)~bit;
    }

    if (skipZeroLen != DISABLE) {
        USBHS->T_BURST_MODE |= bit;
    } else {
        USBHS->T_BURST_MODE &= (uint8_t)~bit;
    }

    if (nyetReply != DISABLE) {
        USBHS->R_RES_MODE |= bit;
    } else {
        USBHS->R_RES_MODE &= (uint8_t)~bit;
    }

    return HAL_OK;
}

/**
 * @brief  启动一次 IN 传输
 * @param  ep  端点号
 * @param  len 发送长度（数据须已在 EP_Open 的 txBuf 里）
 * @retval HAL_OK 已置 ACK，等待 HAL_USBHS_EPInCallback
 * @retval HAL_ERROR 端点号越界，或 len 超出该端点长度寄存器位宽
 * @note   长度寄存器位宽与 FS 不同：
 *         EP0 仍是 **8 位**（手册 R16_U2EP0_T_LEN 复位值 0x00XX），
 *         EP1~EP7 才是 16 位（复位值 0x0000，包长上限 1024 由 USB 规范定）。
 *         所以 EP0 必须单独拦一刀，否则传超过 255 会被硬件默默截断。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Transmit(uint8_t ep, uint16_t len)
{
    if (HAL_USBHS_EP_Valid(ep) == 0U) {
        return HAL_ERROR;
    }
    if ((ep == 0U) && (len > HAL_USBHS_EP0_MPS_MAX)) {
        return HAL_ERROR; /* EP0 T_LEN 只有 8 位，且控制端点本身最大 64 */
    }
    if (len > HAL_USBHS_EP_XFER_MAX) {
        return HAL_ERROR;
    }

    USBHS->EP[ep].TLEN = len;

    /* 只改 RES，TOG 必须原样保留；同时把上一次的 DONE 写 0 清掉（RW0）。
     * 直接 `= RES_ACK` 会把 TOG 一起清零 —— EP0 没有自动翻转，
     * 清零后每次都发 DATA0，控制传输必挂。 */
    {
        volatile uint8_t *ctrl = &USBHS->EP[ep].TX_CTRL;
        *ctrl                  = (uint8_t)((*ctrl & HAL_USBHS_EP_TOG_MASK) | HAL_USBHS_RES_ACK);
    }

    return HAL_OK;
}

/**
 * @brief  打开接收（RES = ACK）
 * @note   读-改-写保留 TOG，否则 EP0 的 DATA0/DATA1 序列会被打乱。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Receive(uint8_t ep)
{
    volatile uint8_t *ctrl;

    if (HAL_USBHS_EP_Valid(ep) == 0U) {
        return HAL_ERROR;
    }

    ctrl  = &USBHS->EP[ep].RX_CTRL;
    *ctrl = (uint8_t)((*ctrl & HAL_USBHS_EP_TOG_MASK) | HAL_USBHS_RES_ACK);
    return HAL_OK;
}

/**
 * @brief  停止接收（RES = NAK）
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Nak(uint8_t ep)
{
    volatile uint8_t *ctrl;

    if (HAL_USBHS_EP_Valid(ep) == 0U) {
        return HAL_ERROR;
    }

    ctrl  = &USBHS->EP[ep].RX_CTRL;
    *ctrl = (uint8_t)((*ctrl & HAL_USBHS_EP_TOG_MASK) | HAL_USBHS_RES_NAK);
    return HAL_OK;
}

/**
 * @brief  置/清 STALL
 * @note   读-改-写保留 TOG，不破坏当前数据翻转阶段。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_SetStall(uint8_t ep, FunctionalState s)
{
    volatile uint8_t *tx;
    volatile uint8_t *rx;

    if (HAL_USBHS_EP_Valid(ep) == 0U) {
        return HAL_ERROR;
    }

    tx = &USBHS->EP[ep].TX_CTRL;
    rx = &USBHS->EP[ep].RX_CTRL;

    if (s != DISABLE) {
        *tx = (uint8_t)((*tx & HAL_USBHS_EP_TOG_MASK) | HAL_USBHS_RES_STALL);
        *rx = (uint8_t)((*rx & HAL_USBHS_EP_TOG_MASK) | HAL_USBHS_RES_STALL);
    } else {
        *tx = (uint8_t)((*tx & HAL_USBHS_EP_TOG_MASK) | HAL_USBHS_RES_NAK);
        *rx = (uint8_t)((*rx & HAL_USBHS_EP_TOG_MASK) | HAL_USBHS_RES_ACK);
    }

    return HAL_OK;
}

/**
 * @brief  读某端点最近一次接收长度
 * @param  ep 端点号
 * @retval 字节数；ep 越界返回 0
 * @note   与 FS 不同：FS 只有全局的 R8_USB_RX_LEN，HS 是每端点独立寄存器。
 */
uint16_t HAL_USBHS_DEV_EP_GetRxLen(uint8_t ep)
{
    if (HAL_USBHS_EP_Valid(ep) == 0U) {
        return 0U;
    }

    return USBHS->RX_LEN[ep].RX_LEN;
}

/**
 * @brief  读某端点累计接收长度（RX_SIZE）
 * @note   多包累积长度；单包长度用 HAL_USBHS_DEV_EP_GetRxLen()。
 */
uint16_t HAL_USBHS_DEV_EP_GetRxSize(uint8_t ep)
{
    if (HAL_USBHS_EP_Valid(ep) == 0U) {
        return 0U;
    }

    return USBHS->RX_LEN[ep].RX_SIZE;
}

/**
 * @brief  读帧号寄存器原始值
 * @return bit[10:0] = FRAME_NO，bit[15:13] = MFRAME_NO
 */
uint16_t HAL_USBHS_DEV_GetFrameNo(void)
{
    return USBHS->FRAME_NO;
}

/**
 * @brief  把端点切换成等时（同步）传输
 * @param  ep 端点号 1~7
 * @param  s  ENABLE = 置位；DISABLE = 清除
 * @retval HAL_OK 成功；HAL_ERROR 端点号非法
 * @note   T_ISO / R_ISO 的 [7:1] 对应 EP1~EP7，bit0 保留（EP0 不能做等时）。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_SetIso(uint8_t ep, FunctionalState s)
{
    uint16_t bit;

    if ((HAL_USBHS_EP_Valid(ep) == 0U) || (ep == 0U)) {
        return HAL_ERROR;
    }

    bit = (uint16_t)(1U << ep);

    if (s != DISABLE) {
        USBHS->T_ISO |= bit;
        USBHS->R_ISO |= bit;
    } else {
        USBHS->T_ISO &= (uint16_t)~bit;
        USBHS->R_ISO &= (uint16_t)~bit;
    }

    return HAL_OK;
}

/** @brief USB 链路是否已建立（MIS_ST.RB_UDMS_READY） */
uint8_t HAL_USBHS_DEV_IsReady(void)
{
    return ((USBHS->MIS_ST & HAL_USBHS_MIS_READY) != 0U) ? 1U : 0U;
}

/** @brief 当前是否运行在高速模式（MIS_ST.RB_UDMS_HS_MOD，语义是「主机侧是否为高速」） */
uint8_t HAL_USBHS_DEV_IsHighSpeed(void)
{
    return ((USBHS->MIS_ST & HAL_USBHS_MIS_HS_MODE) != 0U) ? 1U : 0U;
}

/** @brief USB 总线当前是否处于挂起状态（MIS_ST.RB_UDMS_SUSPEND） */
uint8_t HAL_USBHS_DEV_IsSuspended(void)
{
    return ((USBHS->MIS_ST & HAL_USBHS_MIS_SUSPEND) != 0U) ? 1U : 0U;
}

/**
 * @brief  设置 HS 设备测试模式
 * @param  mode 0 = 关闭；否则为 HAL_USBHS_TEST_* 子模式位的组合
 * @retval HAL_OK 成功；HAL_ERROR 含未定义位
 */
HAL_StatusTypeDef HAL_USBHS_DEV_SetTestMode(uint8_t mode)
{
    if ((mode & (uint8_t)~HAL_USBHS_TEST_MODES) != 0U) {
        return HAL_ERROR; /* TEST_EN 由本函数自己补，不接受从外部传 */
    }

    if (mode == 0U) {
        USBHS->TEST_MODE = 0U; /* 连同 TEST_EN 一起清 */
    } else {
        USBHS->TEST_MODE = (uint8_t)(mode | HAL_USBHS_TEST_EN);
    }

    return HAL_OK;
}

/**
 * @brief  端点别名映射（R32_U2EP_AF_MODE）
 * @param  ep     端点号 1~7
 * @param  enable ENABLE = 复用为 EP9~EP15；DISABLE = 复用为 EP1~EP7
 * @retval HAL_OK 成功；HAL_ERROR 端点号非法
 */
HAL_StatusTypeDef HAL_USBHS_DEV_SetEndpointAlias(uint8_t ep, FunctionalState enable)
{
    uint32_t bit;

    if ((HAL_USBHS_EP_Valid(ep) == 0U) || (ep == 0U)) {
        return HAL_ERROR; /* EP0 无复用位 */
    }

    bit = (1UL << ep);

    if (enable != DISABLE) {
        USBHS->AF_MODE |= bit;
    } else {
        USBHS->AF_MODE &= ~bit;
    }

    return HAL_OK;
}

/**
 * @brief  触发一次远程唤醒
 * @retval HAL_OK    已触发
 * @retval HAL_BUSY  挂起未满 5ms
 * @retval HAL_ERROR 当前不在挂起状态
 * @note   R8_USB2_WAKE_CTRL 的 bit0 是 RW1Z：写 1 触发，硬件产生 resume 波形
 *         （1~15ms，USB 规范要求）后自动清零，软件不用管持续时间。
 *         先查挂起状态再触发：总线活着的时候发唤醒信号没有意义，
 *         只会让主机看到莫名其妙的 K 状态。
 *         5ms 下限也在这里拦住：规范要求设备进入挂起后至少等 5ms 才能唤醒，
 *         否则部分主机会直接判为非法并重新枚举。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_RemoteWakeUp(void)
{
    if (HAL_USBHS_DEV_IsSuspended() == 0U) {
        return HAL_ERROR;
    }

    if ((HAL_GetTick() - s_suspendTick) < HAL_USBHS_WAKE_DELAY_MS) {
        return HAL_BUSY;
    }

    USBHS->WAKE_CTRL = HAL_USBHS_WAKE_REMOTE;

    return HAL_OK;
}

/* ===================== 中断分发 ===================== */

/**
 * @brief  USB2 设备中断入口，由 USB2_DEVICE_IRQHandler 调用
 * @param  husb 句柄
 * @note   RB_UDIF_RTX_ACT(bit4) 是**只读**标志，写 1 无效、读 MIS_ST 也无效，
 *         只能把对应端点的 DONE 位（RW0）写 0 才能清除。
 */
void HAL_USBHS_IRQHandler(HAL_USBHS_HandleTypeDef *husb)
{
    uint8_t flags;

    if (husb == NULL) {
        return;
    }

    flags = USBHS->INT_FG;

    if (husb->State != HAL_USBHS_STATE_READY) {
        USBHS->INT_FG = 0xFFU; /* RTX_ACT 是 RO，这里清不掉，但其余位能清 */
        return;
    }

    if ((flags & HAL_USBHS_INT_FIFO_OV) != 0U) {
        USBHS->INT_FG = HAL_USBHS_INT_FIFO_OV;
        HAL_USBHS_ErrorCallback(husb, 0U);
    }

    if ((flags & HAL_USBHS_INT_SUSPEND) != 0U) {
        USBHS->INT_FG = HAL_USBHS_INT_SUSPEND;

        if ((USBHS->MIS_ST & HAL_USBHS_MIS_SUSPEND) != 0U) {
            s_suspendTick = HAL_GetTick(); /* 供远程唤醒的 5ms 下限判断 */
            HAL_USBHS_SuspendCallback(husb);
        } else {
            HAL_USBHS_ResumeCallback(husb);
        }
    }

    if ((flags & HAL_USBHS_INT_BUS_RST) != 0U) {
        USBHS->INT_FG = HAL_USBHS_INT_BUS_RST;
        USBHS->DEV_AD = 0U;
        HAL_USBHS_ResetCallback(husb);
    }

    if ((flags & HAL_USBHS_INT_LINK_RDY) != 0U) {
        USBHS->INT_FG = HAL_USBHS_INT_LINK_RDY;
        HAL_USBHS_ResetCallback(husb); /* 链路就绪，等同复位后可用 */
    }

    if ((flags & HAL_USBHS_INT_TRANSFER) != 0U) {
        uint8_t ep  = (uint8_t)(USBHS->INT_ST & HAL_USBHS_INTST_EP_MASK);
        uint8_t dir = (uint8_t)(USBHS->INT_ST & HAL_USBHS_INTST_EP_DIR);
        uint8_t isSetup;

        isSetup = ((ep == 0U) &&
                   ((USBHS->EP[0].RX_CTRL & HAL_USBHS_EP_R_SETUP_IS) != 0U))
                      ? 1U
                      : 0U;

        /* 先清 RTX_ACT（把对应 DONE 写 0），再分发。
         * 顺序不能反：回调里重新装载端点时，旧标志会立刻再触发一次中断。 */
        if (dir != 0U) {
            USBHS->EP[ep].TX_CTRL &= (uint8_t)~HAL_USBHS_EP_T_DONE;
        } else {
            USBHS->EP[ep].RX_CTRL &= (uint8_t)~HAL_USBHS_EP_R_DONE;
        }

        if (isSetup != 0U) {
            /* SETUP 之后 EP0 收发翻转位都归 DATA1，这是硬件层必须做的初始化，
             * 不涉及请求内容解析。EP0 的 TOG 只有 bit2 一位。 */
            uint8_t tog1 = (uint8_t)(HAL_USBHS_EP_TOG_DATA1 << HAL_USBHS_EP_TOG_POS_EP0);

            USBHS->EP[0].RX_CTRL = (uint8_t)(tog1 | HAL_USBHS_RES_ACK);
            USBHS->EP[0].TX_CTRL = (uint8_t)(tog1 | HAL_USBHS_RES_NAK);

            HAL_USBHS_SetupCallback(husb, (const uint8_t *)(uintptr_t)USBHS->RX_DMA[0]);
        } else if (dir != 0U) {
            HAL_USBHS_EPInCallback(husb, ep);
        } else {
            HAL_USBHS_EPOutCallback(husb, ep);
        }
    }
}

/* ===================== 弱回调默认实现 ===================== */

__weak void HAL_USBHS_ResetCallback(HAL_USBHS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

__weak void HAL_USBHS_SuspendCallback(HAL_USBHS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

__weak void HAL_USBHS_ResumeCallback(HAL_USBHS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

__weak void HAL_USBHS_SetupCallback(HAL_USBHS_HandleTypeDef *husb, const uint8_t setup[8])
{
    UNUSED(husb);
    UNUSED(setup);
}

__weak void HAL_USBHS_EPInCallback(HAL_USBHS_HandleTypeDef *husb, uint8_t ep)
{
    UNUSED(husb);
    UNUSED(ep);
}

__weak void HAL_USBHS_EPOutCallback(HAL_USBHS_HandleTypeDef *husb, uint8_t ep)
{
    UNUSED(husb);
    UNUSED(ep);
}

__weak void HAL_USBHS_ErrorCallback(HAL_USBHS_HandleTypeDef *husb, uint8_t code)
{
    UNUSED(husb);
    UNUSED(code);
}

#endif /* CH58x_CHIP_MODEL == ID_CH584 */
