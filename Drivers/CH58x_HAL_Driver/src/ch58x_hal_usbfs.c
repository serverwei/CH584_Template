#include "ch58x_hal_pwr.h"
#include "ch58x_hal_usbfs.h"

#include <stddef.h>

/* ===================== 寄存器布局校验 =====================
 * 期望值直接取自 CH585SFR.h 里 R8_UEPx_* 的绝对地址减去 0x40008000。
 * 任何一处写错都会在编译期报错，不依赖运行期测试。
 */
_Static_assert(offsetof(USBFS_TypeDef, CTRL) == 0x00, "USB CTRL @ +0x00");
_Static_assert(offsetof(USBFS_TypeDef, DEV_HOST_CTRL) == 0x01, "UDEV_CTRL @ +0x01");
_Static_assert(offsetof(USBFS_TypeDef, INT_EN) == 0x02, "INT_EN @ +0x02");
_Static_assert(offsetof(USBFS_TypeDef, DEV_AD) == 0x03, "DEV_AD @ +0x03");
_Static_assert(offsetof(USBFS_TypeDef, MIS_ST) == 0x05, "MIS_ST @ +0x05");
_Static_assert(offsetof(USBFS_TypeDef, INT_FG) == 0x06, "INT_FG @ +0x06");
_Static_assert(offsetof(USBFS_TypeDef, INT_ST) == 0x07, "INT_ST @ +0x07");
_Static_assert(offsetof(USBFS_TypeDef, RX_LEN) == 0x08, "RX_LEN @ +0x08");
_Static_assert(offsetof(USBFS_TypeDef, UEP4_1_MOD) == 0x0C, "UEP4_1_MOD @ +0x0C");
_Static_assert(offsetof(USBFS_TypeDef, UEP2_3_MOD) == 0x0D, "UEP2_3_MOD @ +0x0D");
_Static_assert(offsetof(USBFS_TypeDef, UEP567_MOD) == 0x0E, "UEP567_MOD @ +0x0E");
_Static_assert(offsetof(USBFS_TypeDef, UEP0_DMA) == 0x10, "UEP0_DMA @ +0x10");
_Static_assert(offsetof(USBFS_TypeDef, UEP1_DMA) == 0x14, "UEP1_DMA @ +0x14");
_Static_assert(offsetof(USBFS_TypeDef, UEP2_DMA) == 0x18, "UEP2_DMA @ +0x18");
_Static_assert(offsetof(USBFS_TypeDef, UEP3_DMA) == 0x1C, "UEP3_DMA @ +0x1C");
_Static_assert(offsetof(USBFS_TypeDef, UEP0_TLEN) == 0x20, "UEP0_T_LEN @ +0x20");
_Static_assert(offsetof(USBFS_TypeDef, UEP0_CTRL) == 0x22, "UEP0_CTRL @ +0x22");
_Static_assert(offsetof(USBFS_TypeDef, UEP1_TLEN) == 0x24, "UEP1_T_LEN @ +0x24");
_Static_assert(offsetof(USBFS_TypeDef, UEP1_CTRL) == 0x26, "UEP1_CTRL @ +0x26");
_Static_assert(offsetof(USBFS_TypeDef, UEP2_TLEN) == 0x28, "UEP2_T_LEN @ +0x28");
_Static_assert(offsetof(USBFS_TypeDef, UEP2_CTRL) == 0x2A, "UEP2_CTRL @ +0x2A");
_Static_assert(offsetof(USBFS_TypeDef, UEP3_TLEN) == 0x2C, "UEP3_T_LEN @ +0x2C");
_Static_assert(offsetof(USBFS_TypeDef, UEP3_CTRL) == 0x2E, "UEP3_CTRL @ +0x2E");
_Static_assert(offsetof(USBFS_TypeDef, UEP4_TLEN) == 0x30, "UEP4_T_LEN @ +0x30");
_Static_assert(offsetof(USBFS_TypeDef, UEP4_CTRL) == 0x32, "UEP4_CTRL @ +0x32");
_Static_assert(offsetof(USBFS_TypeDef, UEP5_DMA) == 0x54, "UEP5_DMA @ +0x54");
_Static_assert(offsetof(USBFS_TypeDef, UEP6_DMA) == 0x58, "UEP6_DMA @ +0x58");
_Static_assert(offsetof(USBFS_TypeDef, UEP7_DMA) == 0x5C, "UEP7_DMA @ +0x5C");
_Static_assert(offsetof(USBFS_TypeDef, UEP5_TLEN) == 0x64, "UEP5_T_LEN @ +0x64");
_Static_assert(offsetof(USBFS_TypeDef, UEP5_CTRL) == 0x66, "UEP5_CTRL @ +0x66");
_Static_assert(offsetof(USBFS_TypeDef, UEP6_TLEN) == 0x68, "UEP6_T_LEN @ +0x68");
_Static_assert(offsetof(USBFS_TypeDef, UEP6_CTRL) == 0x6A, "UEP6_CTRL @ +0x6A");
_Static_assert(offsetof(USBFS_TypeDef, UEP7_TLEN) == 0x6C, "UEP7_T_LEN @ +0x6C");
_Static_assert(offsetof(USBFS_TypeDef, UEP7_CTRL) == 0x6E, "UEP7_CTRL @ +0x6E");

/* ===================== 内部工具 ===================== */

/**
 * @brief  端点号 → UEPn_DMA 寄存器
 * @param  ep 端点号
 * @retval 寄存器指针；EP4 没有独立 DMA 寄存器，返回 NULL
 * @note   EP4 的缓冲由硬件按 UEP0_DMA + 偏移（OUT +64 / IN +128）取，
 *         这是 CH58x 的固定设计，不是本模块的取舍。
 */
static volatile uint32_t *HAL_USBFS_EP_DmaReg(uint8_t ep)
{
    switch (ep) {
        case 0U:
            return &USBFS->UEP0_DMA;
        case 1U:
            return &USBFS->UEP1_DMA;
        case 2U:
            return &USBFS->UEP2_DMA;
        case 3U:
            return &USBFS->UEP3_DMA;
        case 5U:
            return &USBFS->UEP5_DMA;
        case 6U:
            return &USBFS->UEP6_DMA;
        case 7U:
            return &USBFS->UEP7_DMA;
        default:
            return NULL; /* ep == 4 */
    }
}

/**
 * @brief  端点号 → UEPn_T_LEN 寄存器
 */
static volatile uint8_t *HAL_USBFS_EP_LenReg(uint8_t ep)
{
    switch (ep) {
        case 0U:
            return &USBFS->UEP0_TLEN;
        case 1U:
            return &USBFS->UEP1_TLEN;
        case 2U:
            return &USBFS->UEP2_TLEN;
        case 3U:
            return &USBFS->UEP3_TLEN;
        case 4U:
            return &USBFS->UEP4_TLEN;
        case 5U:
            return &USBFS->UEP5_TLEN;
        case 6U:
            return &USBFS->UEP6_TLEN;
        case 7U:
            return &USBFS->UEP7_TLEN;
        default:
            return NULL;
    }
}

/**
 * @brief  端点号 → UEPn_CTRL 寄存器
 */
static volatile uint8_t *HAL_USBFS_EP_CtrlReg(uint8_t ep)
{
    switch (ep) {
        case 0U:
            return &USBFS->UEP0_CTRL;
        case 1U:
            return &USBFS->UEP1_CTRL;
        case 2U:
            return &USBFS->UEP2_CTRL;
        case 3U:
            return &USBFS->UEP3_CTRL;
        case 4U:
            return &USBFS->UEP4_CTRL;
        case 5U:
            return &USBFS->UEP5_CTRL;
        case 6U:
            return &USBFS->UEP6_CTRL;
        case 7U:
            return &USBFS->UEP7_CTRL;
        default:
            return NULL;
    }
}

/**
 * @brief  设置端点方向使能位
 * @param  ep 端点号；EP0 方向固定，无 MOD 位
 * @param  tx ENABLE = 打开 IN 方向
 * @param  rx ENABLE = 打开 OUT 方向
 * @note   UEP4_1_MOD / UEP2_3_MOD / UEP567_MOD 每个字节管两个端点，
 *         改动必须读-改-写，不能直接赋值。
 */
static void HAL_USBFS_EP_SetDir(uint8_t ep, FunctionalState tx, FunctionalState rx)
{
    volatile uint8_t *mod;
    uint8_t bitTx;
    uint8_t bitRx;

    switch (ep) {
        case 1U:
            mod   = &USBFS->UEP4_1_MOD;
            bitTx = RB_UEP1_TX_EN;
            bitRx = RB_UEP1_RX_EN;
            break;
        case 4U:
            mod   = &USBFS->UEP4_1_MOD;
            bitTx = RB_UEP4_TX_EN;
            bitRx = RB_UEP4_RX_EN;
            break;
        case 2U:
            mod   = &USBFS->UEP2_3_MOD;
            bitTx = RB_UEP2_TX_EN;
            bitRx = RB_UEP2_RX_EN;
            break;
        case 3U:
            mod   = &USBFS->UEP2_3_MOD;
            bitTx = RB_UEP3_TX_EN;
            bitRx = RB_UEP3_RX_EN;
            break;
        case 5U:
            mod   = &USBFS->UEP567_MOD;
            bitTx = RB_UEP5_TX_EN;
            bitRx = RB_UEP5_RX_EN;
            break;
        case 6U:
            mod   = &USBFS->UEP567_MOD;
            bitTx = RB_UEP6_TX_EN;
            bitRx = RB_UEP6_RX_EN;
            break;
        case 7U:
            mod   = &USBFS->UEP567_MOD;
            bitTx = RB_UEP7_TX_EN;
            bitRx = RB_UEP7_RX_EN;
            break;
        default:
            return; /* EP0 */
    }

    if (tx != DISABLE) {
        *mod |= bitTx;
    } else {
        *mod &= (uint8_t)~bitTx;
    }

    if (rx != DISABLE) {
        *mod |= bitRx;
    } else {
        *mod &= (uint8_t)~bitRx;
    }
}

/* ===================== 板级钩子（默认实现） ===================== */

/**
 * @brief  板级初始化：USB 时钟与引脚
 * @note   CH58x 的 FS USB 引脚固定为 PB10(UD-)/PB11(UD+)，所有封装一致，
 *         没有引脚组可选，所以默认实现直接把时钟和引脚都打开。
 *         需要外部 VBUS 检测、USB 供电开关、ESD 保护使能时，BSP 强定义覆盖本函数。
 */
__weak void HAL_USBFS_MspInit(HAL_USBFS_HandleTypeDef *husb)
{
    UNUSED(husb);

    HAL_PWR_ConfigPeriphClock(ENABLE, HAL_PWR_SLPCLK_USB);

    /* 注意是 |=：R16_PIN_CONFIG 还装着 GPIO 配置和其它 USB 位，不能整体赋值 */
    R16_PIN_CONFIG |= RB_PIN_USB_EN;
}

/**
 * @brief  板级反初始化：关时钟、还引脚
 */
__weak void HAL_USBFS_MspDeInit(HAL_USBFS_HandleTypeDef *husb)
{
    UNUSED(husb);

    R16_PIN_CONFIG &= (uint16_t)~RB_PIN_USB_EN;
    HAL_PWR_ConfigPeriphClock(DISABLE, HAL_PWR_SLPCLK_USB);
}

/* ===================== 公共 API ===================== */

/**
 * @brief  初始化 USB1 控制器
 * @param  husb 句柄
 * @param  mode 设备或主机；两者复用同一组端点寄存器，运行期不可切换
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空或模式非法
 * @note   不建立连接：设备模式还要调 HAL_USBFS_DEV_SetConnect(ENABLE)。
 */
HAL_StatusTypeDef HAL_USBFS_Init(HAL_USBFS_HandleTypeDef *husb, HAL_USBFS_ModeTypeDef mode)
{
    if (husb == NULL) {
        return HAL_ERROR;
    }
    if ((mode != HAL_USBFS_MODE_DEVICE) && (mode != HAL_USBFS_MODE_HOST)) {
        return HAL_ERROR;
    }

    husb->Instance = USBFS;
    husb->Mode     = mode;
    husb->State    = HAL_USBFS_STATE_RESET;

    HAL_USBFS_MspInit(husb);

    /* 复位 SIE 并清 FIFO/计数，厂商顺序：先复位再配 */
    USBFS->CTRL = HAL_USBFS_CTRL_RESET_SIE | HAL_USBFS_CTRL_CLR_ALL;
    USBFS->CTRL = 0U;

    /* 端点方向全部关掉，交给 EP_Open 逐个打开 */
    USBFS->UEP4_1_MOD = 0U;
    USBFS->UEP2_3_MOD = 0U;
    USBFS->UEP567_MOD = 0U;

    USBFS->DEV_AD = 0U;
    USBFS->INT_FG = 0x1FU; /* 写 1 清；bit7/6/5 是 RO 状态位，不能写 */

    if (mode == HAL_USBFS_MODE_DEVICE) {
        /* DMA 使能 + 中断标志未清期间自动回 NAK */
        USBFS->CTRL          = HAL_USBFS_CTRL_DMA_EN | HAL_USBFS_CTRL_INT_BUSY;
        USBFS->DEV_HOST_CTRL = RB_UD_PD_DIS | RB_UD_PORT_EN;
        USBFS->INT_EN        = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER | RB_UIE_FIFO_OV;
    } else {
        /* 主机模式：RB_UC_HOST_MODE=1 且 SYS_CTRL=00（normal status） */
        USBFS->CTRL          = HAL_USBFS_CTRL_HOST_MODE | HAL_USBFS_CTRL_DMA_EN;
        USBFS->DEV_HOST_CTRL = RB_UH_PD_DIS | RB_UH_PORT_EN;
        /* 故意不打开 RB_UIE_TRANSFER：主机事务由 HAL_USBFS_HST_Transact()
         * 直接轮询 INT_FG，若同时开中断，ISR 会先把标志清掉，轮询永远等不到。 */
        USBFS->INT_EN = RB_UIE_SUSPEND | RB_UIE_DETECT | RB_UIE_FIFO_OV;
    }

    husb->State = HAL_USBFS_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  关闭 USB1：关中断、清模式、还时钟和引脚
 * @param  husb 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空
 */
HAL_StatusTypeDef HAL_USBFS_DeInit(HAL_USBFS_HandleTypeDef *husb)
{
    if (husb == NULL) {
        return HAL_ERROR;
    }

    USBFS->INT_EN     = 0U;
    USBFS->CTRL       = 0U;
    USBFS->UEP4_1_MOD = 0U;
    USBFS->UEP2_3_MOD = 0U;
    USBFS->UEP567_MOD = 0U;

    HAL_USBFS_MspDeInit(husb);

    husb->State = HAL_USBFS_STATE_RESET;

    return HAL_OK;
}

/**
 * @brief  读当前状态
 */
uint8_t HAL_USBFS_GetState(const HAL_USBFS_HandleTypeDef *husb)
{
    return (husb != NULL) ? husb->State : (uint8_t)HAL_USBFS_STATE_RESET;
}

/* ===================== 设备模式 ===================== */

/**
 * @brief  建立/断开设备连接（DP 上拉）
 * @param  s ENABLE = 挂上拉，主机可枚举；DISABLE = 摘上拉
 * @retval HAL_OK 成功；HAL_ERROR 当前不是设备模式
 * @note   走 RB_UC_DEV_PU_EN，睡眠期间可能被硬件关闭。
 *         需要"睡眠中保持连接"请用 HAL_USBFS_DEV_SetConnectForce()。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetConnect(FunctionalState s)
{
    uint8_t ctrl = USBFS->CTRL;

    if (s != DISABLE) {
        ctrl |= HAL_USBFS_CTRL_DEV_PU_EN;
    } else {
        ctrl &= (uint8_t)~HAL_USBFS_CTRL_DEV_PU_EN;
    }

    USBFS->CTRL = ctrl;

    return HAL_OK;
}

/**
 * @brief  强制保持 DP 上拉（不受睡眠影响）
 * @param  s ENABLE = 强制上拉；DISABLE = 交回 RB_UC_DEV_PU_EN 控制
 * @retval HAL_OK 成功
 * @note   USB 唤醒场景必须用这个，RB_UC_DEV_PU_EN 在睡眠时会掉。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetConnectForce(FunctionalState s)
{
    if (s != DISABLE) {
        R16_PIN_CONFIG |= RB_UDP_PU_EN;
    } else {
        R16_PIN_CONFIG &= (uint16_t)~RB_UDP_PU_EN;
    }

    return HAL_OK;
}

/**
 * @brief  设置设备地址
 * @param  addr 0~127
 * @retval HAL_OK 成功；HAL_ERROR 地址越界
 * @note   本函数只写寄存器，SET_ADDRESS 请求的应答时序由上层负责。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetAddress(uint8_t addr)
{
    if (addr > 127U) {
        return HAL_ERROR;
    }

    USBFS->DEV_AD = addr;

    return HAL_OK;
}

/**
 * @brief  设置端点缓冲区模式位（BUF_MOD）
 * @param  ep   端点号
 * @param  mode 缓冲区模式
 * @note   手册表 17-4：只有 EP1/EP2/EP3 有这一位。
 *         EP4 的缓冲模式由 RB_UEP4_RX_EN/RB_UEP4_TX_EN 组合决定（表 17-3），
 *         EP5/6/7 只有单缓冲（表 17-5），都不在此处理。
 */
static void HAL_USBFS_EP_SetBufMode(uint8_t ep, HAL_USBFS_BufModeTypeDef mode)
{
    volatile uint8_t *mod;
    uint8_t bit;

    switch (ep) {
        case 1U:
            mod = &USBFS->UEP4_1_MOD;
            bit = HAL_USBFS_BUF_MOD_EP1;
            break;
        case 2U:
            mod = &USBFS->UEP2_3_MOD;
            bit = HAL_USBFS_BUF_MOD_EP2;
            break;
        case 3U:
            mod = &USBFS->UEP2_3_MOD;
            bit = HAL_USBFS_BUF_MOD_EP3;
            break;
        default:
            return;
    }

    if (mode == HAL_USBFS_BUF_DOUBLE) {
        *mod |= bit;
    } else {
        *mod &= (uint8_t)~bit;
    }
}

/**
 * @brief  打开端点
 * @param  ep   端点号 0~7
 * @param  buf  缓冲地址；传 NULL 表示不改 UEPn_DMA。**EP4 必须传 NULL**
 * @param  mode 缓冲区模式；只有 EP1/EP2/EP3 支持 DOUBLE
 * @retval HAL_OK 成功
 * @retval HAL_ERROR 端点号越界、EP4 传了 buf、buf 未 4 字节对齐、或模式与端点不匹配
 * @note   CH58x 的端点没有包长寄存器，包长完全由软件按描述符处理，故不传 maxPacket。
 *         EP4 没有独立 DMA 寄存器，硬件按 UEP0_DMA + 偏移取缓冲
 *         （OUT = +64，IN = +128），所以缓冲归 EP0 那块区域统一管理。
 *         缓冲尺寸：单缓冲 128 字节（+0 OUT / +64 IN）；
 *         双缓冲 256 字节（+0、+64 为 OUT 两个半区，+128、+192 为 IN 两个半区），
 *         由 RB_UEP_R_TOG / RB_UEP_T_TOG 选半区。
 *         自动翻转只对 1/2/3/5/6/7 开启；EP0 和 EP4 需软件管翻转。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Open(uint8_t ep, uint8_t *buf, HAL_USBFS_BufModeTypeDef mode)
{
    volatile uint32_t *dma;
    volatile uint8_t *ctrl;

    if (ep > HAL_USBFS_EP_MAX) {
        return HAL_ERROR;
    }
    if ((ep == 4U) && (buf != NULL)) {
        return HAL_ERROR; /* EP4 复用 EP0 的 DMA 区域 */
    }
    if ((buf != NULL) && (((uint32_t)buf & 0x03U) != 0U)) {
        return HAL_ERROR; /* 硬件按 4 字节访问 */
    }
    if ((mode != HAL_USBFS_BUF_SINGLE) && (mode != HAL_USBFS_BUF_DOUBLE)) {
        return HAL_ERROR;
    }
    /* 双缓冲只有 EP1/EP2/EP3 有（表 17-4）。不支持时直接报错而不静默降级：
     * 上层若按 256 字节排布了数据，降级成单缓冲会让半区错位。 */
    if ((mode == HAL_USBFS_BUF_DOUBLE) && (ep >= 4U)) {
        return HAL_ERROR;
    }

    if (ep != 0U) {
        HAL_USBFS_EP_SetDir(ep, ENABLE, ENABLE);
    }

    if (buf != NULL) {
        dma = HAL_USBFS_EP_DmaReg(ep);
        if (dma == NULL) {
            return HAL_ERROR;
        }
        *dma = (uint32_t)buf;
    }

    HAL_USBFS_EP_SetBufMode(ep, mode);

    ctrl = HAL_USBFS_EP_CtrlReg(ep);
    if (ctrl == NULL) {
        return HAL_ERROR;
    }

    /* 初始应答：接收 ACK、发送 NAK（还没数据可发） */
    *ctrl = (uint8_t)(UEP_R_RES_ACK | UEP_T_RES_NAK);

    /* 自动翻转：手册 17.2.2 明确只支持端点 1/2/3/5/6/7，
     * EP0（控制传输需软件管翻转）与 EP4（与 EP0 共用缓冲）不支持。 */
    if ((ep != 0U) && (ep != 4U)) {
        *ctrl |= RB_UEP_AUTO_TOG;
    }

    return HAL_OK;
}

/**
 * @brief  关闭端点
 * @param  ep 端点号
 * @retval HAL_OK 成功；HAL_ERROR 端点号越界
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Close(uint8_t ep)
{
    volatile uint8_t *ctrl;

    if (ep > HAL_USBFS_EP_MAX) {
        return HAL_ERROR;
    }

    if (ep != 0U) {
        HAL_USBFS_EP_SetDir(ep, DISABLE, DISABLE);
    }

    ctrl = HAL_USBFS_EP_CtrlReg(ep);
    if (ctrl == NULL) {
        return HAL_ERROR;
    }
    *ctrl = (uint8_t)(UEP_R_RES_NAK | UEP_T_RES_NAK);

    return HAL_OK;
}

/**
 * @brief  启动一次 IN 传输
 * @param  ep  端点号
 * @param  len 发送长度（数据须已在 EP_Open 传入的缓冲里）
 * @retval HAL_OK 已置 ACK，等待 HAL_USBFS_EPInCallback
 * @retval HAL_ERROR 端点号越界或 len 超出全速单包上限
 * @note   CH58x 的端点缓冲由硬件直取，不需要把数据再 memcpy 一次。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Transmit(uint8_t ep, uint16_t len)
{
    volatile uint8_t *ctrl;
    volatile uint8_t *lenReg;

    if ((ep > HAL_USBFS_EP_MAX) || (len > HAL_USBFS_EP_MPS_MAX)) {
        return HAL_ERROR;
    }

    lenReg = HAL_USBFS_EP_LenReg(ep);
    ctrl   = HAL_USBFS_EP_CtrlReg(ep);
    if ((lenReg == NULL) || (ctrl == NULL)) {
        return HAL_ERROR;
    }

    *lenReg = (uint8_t)len;
    *ctrl   = (uint8_t)((*ctrl & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_ACK);

    return HAL_OK;
}

/**
 * @brief  打开接收（R_RES = ACK）
 * @param  ep 端点号
 * @retval HAL_OK 成功；HAL_ERROR 端点号越界
 * @note   数据由硬件写进 EP_Open 传入的缓冲，长度用 HAL_USBFS_DEV_EP_GetRxLen() 取。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Receive(uint8_t ep)
{
    volatile uint8_t *ctrl;

    if (ep > HAL_USBFS_EP_MAX) {
        return HAL_ERROR;
    }

    ctrl = HAL_USBFS_EP_CtrlReg(ep);
    if (ctrl == NULL) {
        return HAL_ERROR;
    }
    *ctrl = (uint8_t)((*ctrl & (uint8_t)~MASK_UEP_R_RES) | UEP_R_RES_ACK);

    return HAL_OK;
}

/**
 * @brief  停止接收（R_RES = NAK），主机重试但不占用缓冲
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Nak(uint8_t ep)
{
    volatile uint8_t *ctrl;

    if (ep > HAL_USBFS_EP_MAX) {
        return HAL_ERROR;
    }

    ctrl = HAL_USBFS_EP_CtrlReg(ep);
    if (ctrl == NULL) {
        return HAL_ERROR;
    }
    *ctrl = (uint8_t)((*ctrl & (uint8_t)~MASK_UEP_R_RES) | UEP_R_RES_NAK);

    return HAL_OK;
}

/**
 * @brief  置/清 STALL
 * @param  ep 端点号
 * @param  s  ENABLE = 收发都回 STALL；DISABLE = 恢复 ACK/NAK
 * @retval HAL_OK 成功；HAL_ERROR 端点号越界
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_SetStall(uint8_t ep, FunctionalState s)
{
    volatile uint8_t *ctrl;
    uint8_t v;

    if (ep > HAL_USBFS_EP_MAX) {
        return HAL_ERROR;
    }

    ctrl = HAL_USBFS_EP_CtrlReg(ep);
    if (ctrl == NULL) {
        return HAL_ERROR;
    }

    v = (uint8_t)(*ctrl & (uint8_t)~(MASK_UEP_R_RES | MASK_UEP_T_RES));

    if (s != DISABLE) {
        v |= (uint8_t)(UEP_R_RES_STALL | UEP_T_RES_STALL);
    } else {
        v |= (uint8_t)(UEP_R_RES_ACK | UEP_T_RES_NAK);
    }

    *ctrl = v;

    return HAL_OK;
}

/**
 * @brief  读最近一次接收长度
 */
uint8_t HAL_USBFS_DEV_EP_GetRxLen(void)
{
    return USBFS->RX_LEN;
}

/**
 * @brief  把端点切换成等时（同步）传输的应答方式
 * @param  ep 端点号 1~7（EP0 是控制端点，不能等时）
 * @param  s  ENABLE = 等时（收发均不应答）；DISABLE = 恢复 ACK / NAK
 * @retval HAL_OK 成功；HAL_ERROR 端点号非法
 * @note   手册 MASK_UEP_R_RES / MASK_UEP_T_RES 的 01 编码：
 *         接收侧是"超时/无响应"，发送侧是"发数据但期望无响应"，
 *         这正是等时传输的应答方式。每帧发一包的节拍调度不属于本层。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_SetIso(uint8_t ep, FunctionalState s)
{
    volatile uint8_t *ctrl;

    if ((ep == 0U) || (ep > HAL_USBFS_EP_MAX)) {
        return HAL_ERROR;
    }

    ctrl = HAL_USBFS_EP_CtrlReg(ep);
    if (ctrl == NULL) {
        return HAL_ERROR;
    }

    if (s != DISABLE) {
        *ctrl = (uint8_t)((*ctrl & (uint8_t)~(MASK_UEP_R_RES | MASK_UEP_T_RES)) | (uint8_t)(UEP_R_RES_TOUT | UEP_T_RES_TOUT));
    } else {
        *ctrl = (uint8_t)((*ctrl & (uint8_t)~(MASK_UEP_R_RES | MASK_UEP_T_RES)) | (uint8_t)(UEP_R_RES_ACK | UEP_T_RES_NAK));
    }

    return HAL_OK;
}

/**
 * @brief  USB 单线模式开关（R8_USB_INT_EN bit5，手册 RB_MOD_1_WIRE）
 * @note   手册只写"USB 单线模式使能"，属电气层模式切换，无协议语义。
 *         CH585SFR.h 未定义这一位，宏在头文件里。
 */
HAL_StatusTypeDef HAL_USBFS_SetOneWire(FunctionalState s)
{
    if (s != DISABLE) {
        USBFS->INT_EN |= HAL_USBFS_INTEN_1WIRE;
    } else {
        USBFS->INT_EN &= (uint8_t)~HAL_USBFS_INTEN_1WIRE;
    }

    return HAL_OK;
}

/**
 * @brief  USB HUB0 复位控制（R8_UDEV_CTRL bit1，手册 RB_UD_HUB0_RESET）
 * @note   ⚠ 手册对该位只有"USB HUB0 复位控制"一句，未说明 HUB0 指什么、
 *         什么时机置位。本函数只做寄存器位的原始读写，语义由调用方负责。
 *         CH585SFR.h 把这一位叫 RB_UD_GP_BIT（通用标志位），两处命名不一致。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetHubReset(FunctionalState s)
{
    if (s != DISABLE) {
        USBFS->DEV_HOST_CTRL |= HAL_USBFS_UDEV_HUB0RST;
    } else {
        USBFS->DEV_HOST_CTRL &= (uint8_t)~HAL_USBFS_UDEV_HUB0RST;
    }

    return HAL_OK;
}

/**
 * @brief  设备模式 NAK 中断使能（R8_USB_INT_EN bit6）
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetNakIrq(FunctionalState s)
{
    if (s != DISABLE) {
        USBFS->INT_EN |= RB_UIE_DEV_NAK;
    } else {
        USBFS->INT_EN &= (uint8_t)~RB_UIE_DEV_NAK;
    }

    return HAL_OK;
}

/**
 * @brief  上一次传输是否回了 NAK（R8_USB_INT_FG bit7，只读）
 */
uint8_t HAL_USBFS_DEV_IsNak(void)
{
    return ((USBFS->INT_FG & RB_U_IS_NAK) != 0U) ? 1U : 0U;
}

/* ===================== 中断分发 ===================== */

/**
 * @brief  USB1 中断入口，由 USB_IRQHandler 调用
 * @param  husb 句柄
 * @note   只把硬件事件翻译成弱回调，不做任何 USB 协议解析。
 */
void HAL_USBFS_IRQHandler(HAL_USBFS_HandleTypeDef *husb)
{
    uint8_t flags;

    if (husb == NULL) {
        return;
    }

    flags = USBFS->INT_FG;

    if (husb->State != HAL_USBFS_STATE_READY) {
        USBFS->INT_FG = 0x1FU; /* 本函数只支持 0x1F；bit7/6/5 是 RO */
        return;
    }

    if ((flags & RB_UIF_FIFO_OV) != 0U) {
        USBFS->INT_FG = RB_UIF_FIFO_OV;
        HAL_USBFS_ErrorCallback(husb, 0U);
    }

    if ((flags & RB_UIF_SUSPEND) != 0U) {
        USBFS->INT_FG = RB_UIF_SUSPEND;

        if ((USBFS->MIS_ST & RB_UMS_SUSPEND) != 0U) {
            HAL_USBFS_SuspendCallback(husb);
        } else {
            HAL_USBFS_ResumeCallback(husb);
        }
    }

    if (husb->Mode == HAL_USBFS_MODE_DEVICE) {
        if ((flags & RB_UIF_BUS_RST) != 0U) {
            USBFS->INT_FG = RB_UIF_BUS_RST;
            USBFS->DEV_AD = 0U; /* 复位后地址归零，必须由上层重新 GET_DESCRIPTOR */
            HAL_USBFS_ResetCallback(husb);
        }

        if ((flags & RB_UIF_TRANSFER) != 0U) {
            uint8_t ep      = (uint8_t)(USBFS->INT_ST & MASK_UIS_ENDP);
            uint8_t tok     = (uint8_t)(USBFS->INT_ST & MASK_UIS_TOKEN);
            uint8_t isSetup = (uint8_t)(USBFS->INT_ST & RB_UIS_SETUP_ACT);

            USBFS->INT_FG = RB_UIF_TRANSFER;

            /* SETUP 不能用 MASK_UIS_TOKEN 判。手册 17.2.1：
             *   MASK_UIS_TOKEN 只有 00=OUT / 10=IN / 11=空闲 / 01 未定义；
             *   SETUP 由 RB_UIS_SETUP_ACT(bit7) 单独表示，而且该位置位时
             *   MASK_UIS_TOKEN / MASK_UIS_ENDP / RX_LEN 都可能是上一次事务的残留值。
             * 手册还要求：两者同时置位时先处理 TOKEN 事务，再处理 SETUP。
             * 这里在同一趟 ISR 内按这个顺序分发。 */
            if ((tok == UIS_TOKEN_IN) || (tok == UIS_TOKEN_OUT)) {
                if (tok == UIS_TOKEN_IN) {
                    HAL_USBFS_EPInCallback(husb, ep);
                } else {
                    HAL_USBFS_EPOutCallback(husb, ep);
                }
            }

            if (isSetup != 0U) {
                /* SETUP 之后 EP0 收发翻转位都归 DATA1，是硬件层必须做的初始化，
                 * 不涉及请求内容解析。 */
                USBFS->UEP0_CTRL = (uint8_t)(RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK);

                HAL_USBFS_SetupCallback(husb, (const uint8_t *)(uintptr_t)USBFS->UEP0_DMA);
            }
        }
    } else {
        /* 主机模式的分发由 ch58x_hal_usbfs_host.c 的 HAL_USBFS_HST_IRQHandler 接管；
         * 这里只处理共用的挂起/溢出。 */
        if ((flags & RB_UIF_TRANSFER) != 0U) {
            USBFS->INT_FG = RB_UIF_TRANSFER;
        }
        if ((flags & RB_UIF_DETECT) != 0U) {
            USBFS->INT_FG = RB_UIF_DETECT;
        }
        if ((flags & RB_UIF_HST_SOF) != 0U) {
            USBFS->INT_FG = RB_UIF_HST_SOF;
        }
    }
}

/* ===================== 弱回调默认实现 ===================== */

__weak void HAL_USBFS_ResetCallback(HAL_USBFS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

__weak void HAL_USBFS_SuspendCallback(HAL_USBFS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

__weak void HAL_USBFS_ResumeCallback(HAL_USBFS_HandleTypeDef *husb)
{
    UNUSED(husb);
}

__weak void HAL_USBFS_SetupCallback(HAL_USBFS_HandleTypeDef *husb, const uint8_t setup[8])
{
    UNUSED(husb);
    UNUSED(setup);
}

__weak void HAL_USBFS_EPInCallback(HAL_USBFS_HandleTypeDef *husb, uint8_t ep)
{
    UNUSED(husb);
    UNUSED(ep);
}

__weak void HAL_USBFS_EPOutCallback(HAL_USBFS_HandleTypeDef *husb, uint8_t ep)
{
    UNUSED(husb);
    UNUSED(ep);
}

__weak void HAL_USBFS_ErrorCallback(HAL_USBFS_HandleTypeDef *husb, uint8_t code)
{
    UNUSED(husb);
    UNUSED(code);
}
