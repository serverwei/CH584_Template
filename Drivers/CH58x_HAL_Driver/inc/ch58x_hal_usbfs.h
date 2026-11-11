#ifndef __CH58X_HAL_USBFS_H
#define __CH58X_HAL_USBFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* ===================== 说明 =====================
 * 本模块只做 USB1（全速）的**硬件抽象**：寄存器、端点原语、中断分发。
 *
 * 不做的事（放在上层）：
 *   - 设备枚举（GET_DESCRIPTOR / SET_ADDRESS / SET_CONFIGURATION 的语义）
 *   - 描述符表
 *   - class 协议（HID 报告、CDC 波特率、MSC CBW/CSW）
 *   - 主机枚举栈
 *
 * 设备/主机的关键约束：
 *   CH58x 的 USB 主机模式**复用**设备端点寄存器（CH585SFR.h 里用 #define 别名
 *   把 R8_UH_* 指向 R8_UEPn_*）。所以同一时刻只能选一种模式，
 *   HAL_USBFS_Init() 时定死，运行期不支持切换。
 *
 * 端点缓冲：
 *   EP0~EP3、EP5~EP7 各有自己的 UEPn_DMA 寄存器；
 *   **EP4 没有独立 DMA 寄存器**，硬件直接用 UEP0_DMA + n×64 的偏移，
 *   所以 HAL_USBFS_DEV_EP_Open(4, buf) 的 buf 会被忽略（写成文档约束）。
 */

/* ===================== 寄存器模型 ===================== */

/**
 * @brief  USB1（全速）寄存器块 @ 0x40008000
 * @note   必须按字节建模：R32_USB_CONTROL 一个 32 位字里塞了
 *         CTRL(+0x00) / INT_EN(+0x02) / DEV_AD(+0x03)；
 *         R32_USB_BUF_MODE 是三个端点模式字节。
 *         按字访问会误改相邻字段。
 */
typedef struct
{
    volatile uint8_t CTRL;          /* +0x00 模式 / DMA / 设备上拉 */
    volatile uint8_t DEV_HOST_CTRL; /* +0x01 设备=UDEV_CTRL，主机=UHOST_CTRL */
    volatile uint8_t INT_EN;        /* +0x02 */
    volatile uint8_t DEV_AD;        /* +0x03 设备地址 [6:0] */
    volatile uint8_t RESERVED0;     /* +0x04 */
    volatile uint8_t MIS_ST;        /* +0x05 总线状态 */
    volatile uint8_t INT_FG;        /* +0x06 中断标志，写 1 清 */
    volatile uint8_t INT_ST;        /* +0x07 当前端点 / PID / 握手结果 */
    volatile uint8_t RX_LEN;        /* +0x08 最近一次接收长度 */
    volatile uint8_t RESERVED1[3];  /* +0x09~0x0B */

    volatile uint8_t UEP4_1_MOD; /* +0x0C EP1/EP4 方向使能 */
    volatile uint8_t UEP2_3_MOD; /* +0x0D EP2/EP3 方向使能 */
    volatile uint8_t UEP567_MOD; /* +0x0E EP5/EP6/EP7 方向使能 */
    volatile uint8_t RESERVED2;  /* +0x0F */

    volatile uint32_t UEP0_DMA; /* +0x10 */
    volatile uint32_t UEP1_DMA; /* +0x14 */
    volatile uint32_t UEP2_DMA; /* +0x18 */
    volatile uint32_t UEP3_DMA; /* +0x1C */

    volatile uint8_t UEP0_TLEN; /* +0x20 发送长度 */
    volatile uint8_t UEP0_RES0; /* +0x21 */
    volatile uint8_t UEP0_CTRL; /* +0x22 应答 / 翻转 */
    volatile uint8_t UEP0_RES1; /* +0x23 */
    volatile uint8_t UEP1_TLEN; /* +0x24 */
    volatile uint8_t UEP1_RES0; /* +0x25 */
    volatile uint8_t UEP1_CTRL; /* +0x26 */
    volatile uint8_t UEP1_RES1; /* +0x27 */
    volatile uint8_t UEP2_TLEN; /* +0x28 */
    volatile uint8_t UEP2_RES0; /* +0x29 */
    volatile uint8_t UEP2_CTRL; /* +0x2A */
    volatile uint8_t UEP2_RES1; /* +0x2B */
    volatile uint8_t UEP3_TLEN; /* +0x2C */
    volatile uint8_t UEP3_RES0; /* +0x2D */
    volatile uint8_t UEP3_CTRL; /* +0x2E */
    volatile uint8_t UEP3_RES1; /* +0x2F */
    volatile uint8_t UEP4_TLEN; /* +0x30 */
    volatile uint8_t UEP4_RES0; /* +0x31 */
    volatile uint8_t UEP4_CTRL; /* +0x32 */
    volatile uint8_t UEP4_RES1; /* +0x33 */

    volatile uint8_t RESERVED3[0x20]; /* +0x34~0x53 */

    volatile uint32_t UEP5_DMA;  /* +0x54 */
    volatile uint32_t UEP6_DMA;  /* +0x58 */
    volatile uint32_t UEP7_DMA;  /* +0x5C */
    volatile uint32_t RESERVED4; /* +0x60 */
    volatile uint8_t UEP5_TLEN;  /* +0x64 */
    volatile uint8_t UEP5_RES0;  /* +0x65 */
    volatile uint8_t UEP5_CTRL;  /* +0x66 */
    volatile uint8_t UEP5_RES1;  /* +0x67 */
    volatile uint8_t UEP6_TLEN;  /* +0x68 */
    volatile uint8_t UEP6_RES0;  /* +0x69 */
    volatile uint8_t UEP6_CTRL;  /* +0x6A */
    volatile uint8_t UEP6_RES1;  /* +0x6B */
    volatile uint8_t UEP7_TLEN;  /* +0x6C */
    volatile uint8_t UEP7_RES0;  /* +0x6D */
    volatile uint8_t UEP7_CTRL;  /* +0x6E */
    volatile uint8_t UEP7_RES1;  /* +0x6F */
} USBFS_TypeDef;

#define USBFS ((USBFS_TypeDef *)0x40008000U)

/* ===================== 常量 ===================== */

/** @brief 最大端点号（EP0~EP7） */
#define HAL_USBFS_EP_MAX 7U
/** @brief 单次传输最大字节数（全速单包上限） */
#define HAL_USBFS_EP_MPS_MAX 64U

/* 端点缓冲区模式位（手册表17-4）：只有 EP1/EP2/EP3 支持双 64 字节缓冲。
 * EP4 与 EP0 共用 UEP0_DMA（表17-3），EP5/6/7 只有单缓冲（表17-5）。 */
#define HAL_USBFS_BUF_MOD_EP1 0x10U /**< R8_UEP4_1_MOD bit4 */
#define HAL_USBFS_BUF_MOD_EP2 0x01U /**< R8_UEP2_3_MOD bit0 */
#define HAL_USBFS_BUF_MOD_EP3 0x10U /**< R8_UEP2_3_MOD bit4 */

/* 端点缓冲字节数（以 UEPn_DMA 为起始、由低向高排列）：
 *   单缓冲：(+0) OUT、(+64) IN
 *   双缓冲：(+0) OUT/TOG=0、(+64) OUT/TOG=1、(+128) IN/TOG=0、(+192) IN/TOG=1
 * 双缓冲下由 RB_UEP_R_TOG / RB_UEP_T_TOG 选择半区，配合 RB_UEP_AUTO_TOG 可自动切换。
 */
#define HAL_USBFS_BUF_LEN_SINGLE 128U
#define HAL_USBFS_BUF_LEN_DOUBLE 256U

/* 手册里有、但 CH585SFR.h 未定义（或命名不同）的两个位 */
#define HAL_USBFS_INTEN_1WIRE  0x20U /**< R8_USB_INT_EN bit5，手册 RB_MOD_1_WIRE */
#define HAL_USBFS_UDEV_HUB0RST 0x02U /**< R8_UDEV_CTRL bit1，手册 RB_UD_HUB0_RESET */

/** @brief RB_USB_CTRL 位（与 CH585SFR.h 同名，此处只做语义索引） */
#define HAL_USBFS_CTRL_HOST_MODE 0x80U
#define HAL_USBFS_CTRL_LOW_SPEED 0x40U
#define HAL_USBFS_CTRL_DEV_PU_EN 0x20U
#define HAL_USBFS_CTRL_INT_BUSY  0x08U
#define HAL_USBFS_CTRL_RESET_SIE 0x04U
#define HAL_USBFS_CTRL_CLR_ALL   0x02U
#define HAL_USBFS_CTRL_DMA_EN    0x01U

/* ===================== 类型 ===================== */

/**
 * @brief 工作模式
 * @note  设备模式与主机模式复用同一组端点寄存器，Init 时二选一。
 */
typedef enum {
    HAL_USBFS_MODE_DEVICE = 0,
    HAL_USBFS_MODE_HOST
} HAL_USBFS_ModeTypeDef;

/** @brief 生命周期状态 */
typedef enum {
    HAL_USBFS_STATE_RESET = 0,
    HAL_USBFS_STATE_READY,
    HAL_USBFS_STATE_BUSY,
    HAL_USBFS_STATE_ERROR
} HAL_USBFS_StateTypeDef;

/** @brief 端点缓冲区模式 */
typedef enum {
    HAL_USBFS_BUF_SINGLE = 0, /**< 单向单 64 字节缓冲 */
    HAL_USBFS_BUF_DOUBLE      /**< 单向双 64 字节缓冲，由 TOG 选半区；仅 EP1/2/3 支持 */
} HAL_USBFS_BufModeTypeDef;

/** @brief 句柄 */
typedef struct
{
    USBFS_TypeDef *Instance; /**< 恒为 USBFS */
    HAL_USBFS_ModeTypeDef Mode;
    volatile uint8_t State;
} HAL_USBFS_HandleTypeDef;

/* ===================== 公共 API ===================== */

HAL_StatusTypeDef HAL_USBFS_Init(HAL_USBFS_HandleTypeDef *husb, HAL_USBFS_ModeTypeDef mode);
HAL_StatusTypeDef HAL_USBFS_DeInit(HAL_USBFS_HandleTypeDef *husb);
void HAL_USBFS_IRQHandler(HAL_USBFS_HandleTypeDef *husb);
uint8_t HAL_USBFS_GetState(const HAL_USBFS_HandleTypeDef *husb);

/**
 * @brief  板级初始化：USB 时钟与引脚
 * @param  husb 句柄
 * @note   HAL 提供有内容的默认实现（CH58x 的 FS USB 引脚固定为 PB10/PB11，
 *         所有封装一致，没有引脚组可选）。板子需要外部 VBUS 检测、USB 供电
 *         开关、ESD 保护使能等处理时，在 BSP 里强定义同名函数覆盖即可。
 */
void HAL_USBFS_MspInit(HAL_USBFS_HandleTypeDef *husb);
void HAL_USBFS_MspDeInit(HAL_USBFS_HandleTypeDef *husb);

/* ===================== 设备模式 API ===================== */

/**
 * @brief  建立/断开设备连接（DP 上拉）
 * @retval HAL_OK 成功
 * @note   走 R8_USB_CTRL.RB_UC_DEV_PU_EN。睡眠期间上拉可能被关闭，
 *         需要"睡眠中保持 USB 连接"时改用 HAL_USBFS_DEV_SetConnectForce()。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetConnect(FunctionalState s);

/**
 * @brief  强制保持 DP 上拉（睡眠不掉）
 * @note   走 R16_PIN_CONFIG.RB_UDP_PU_EN。用于 USB 唤醒场景。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetConnectForce(FunctionalState s);

HAL_StatusTypeDef HAL_USBFS_DEV_SetAddress(uint8_t addr);

/**
 * @brief  打开端点：置方向使能位、写缓冲地址、配置缓冲模式、初始化应答
 * @param  ep   端点号 0~7
 * @param  buf  端点缓冲区基址，4 字节对齐；
 *              **传 NULL 表示不改 UEPn_DMA**（EP4 必须传 NULL）
 * @param  mode 缓冲区模式；只有 EP1/EP2/EP3 能传 HAL_USBFS_BUF_DOUBLE，
 *              其余端点传 DOUBLE 会返回 HAL_ERROR（不静默降级）
 * @retval HAL_OK 成功
 * @retval HAL_ERROR 端点号越界、指针未对齐、或缓冲模式与端点不匹配
 * @note   CH58x 的端点没有"包长"寄存器，包长完全由软件按描述符处理，故不传 maxPacket。
 *         缓冲尺寸见 HAL_USBFS_BUF_LEN_SINGLE / _DOUBLE：单缓冲 128 字节，双缓冲 256 字节。
 *         EP4 没有独立的 UEPn_DMA，硬件用 UEP0_DMA + 偏移（OUT=+64, IN=+128），
 *         所以 EP4 的 buf 必须传 NULL，缓冲由 EP0 那块区域切分。
 *         自动翻转（RB_UEP_AUTO_TOG）只对端点 1/2/3/5/6/7 开启，EP0 与 EP4 不支持。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Open(uint8_t ep, uint8_t *buf, HAL_USBFS_BufModeTypeDef mode);

HAL_StatusTypeDef HAL_USBFS_DEV_EP_Close(uint8_t ep);

/**
 * @brief  启动一次 IN 传输
 * @param  ep  端点号
 * @param  len 本次发送长度（数据须已在 EP_Open 传入的缓冲里）
 * @retval HAL_OK 已置 ACK，等待传输完成中断
 * @note   CH58x 的端点缓冲由硬件直取，不需要把数据再 memcpy 一次。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Transmit(uint8_t ep, uint16_t len);

/**
 * @brief  打开接收（置 R_RES = ACK）
 * @param  ep 端点号
 * @retval HAL_OK 成功
 * @note   数据会由硬件写进 EP_Open 传入的缓冲，长度用 HAL_USBFS_DEV_EP_GetRxLen() 取。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Receive(uint8_t ep);

/** @brief 停止接收（置 R_RES = NAK） */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_Nak(uint8_t ep);

HAL_StatusTypeDef HAL_USBFS_DEV_EP_SetStall(uint8_t ep, FunctionalState s);

/** @brief 读最近一次接收长度（R8_USB_RX_LEN，全局唯一，非每端点） */
uint8_t HAL_USBFS_DEV_EP_GetRxLen(void);

/**
 * @brief  把端点切换成等时（同步）传输的应答方式
 * @param  ep 端点号 1~7（EP0 不能是等时，传 0 返回 HAL_ERROR）
 * @param  s  ENABLE = 等时（收发均不应答）；DISABLE = 恢复 ACK/NAK
 * @retval HAL_OK 成功；HAL_ERROR 端点号非法
 * @note   手册 MASK_UEP_R_RES/MASK_UEP_T_RES 的 01 编码：
 *         接收侧是"超时/无响应"，发送侧是"发数据但期望无响应"，
 *         这就是等时传输的应答方式。每帧发一包的节拍调度不属于本层。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_EP_SetIso(uint8_t ep, FunctionalState s);

/**
 * @brief  USB 单线模式开关（R8_USB_INT_EN.RB_MOD_1_WIRE）
 * @note   手册只写"USB 单线模式使能"，属于电气层模式切换，无协议语义。
 */
HAL_StatusTypeDef HAL_USBFS_SetOneWire(FunctionalState s);

/**
 * @brief  USB HUB0 复位控制（R8_UDEV_CTRL bit1）
 * @note   ⚠ 手册对该位只有"USB HUB0 复位控制"一句话，未说明 HUB0 指什么、
 *         什么时机置位。本函数只提供寄存器位的原始读写，语义由调用方负责。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetHubReset(FunctionalState s);

/**
 * @brief  设备模式 NAK 中断使能（R8_USB_INT_EN bit6，RB_UIE_DEV_NAK）
 * @note   使能后主机对本设备回 NAK 时会产生中断，用 HAL_USBFS_DEV_IsNak() 查询。
 *         用途：在不支持 DMA 的场景下判断主机是否在轮询自己。
 */
HAL_StatusTypeDef HAL_USBFS_DEV_SetNakIrq(FunctionalState s);

/**
 * @brief  上一次传输是否回了 NAK（R8_USB_INT_FG bit7，RB_U_IS_NAK，只读）
 * @retval 1 = 回了 NAK；0 = 无 NAK 响应
 */
uint8_t HAL_USBFS_DEV_IsNak(void);

/* ===================== 弱回调（只报事件，不解释语义） ===================== */

void HAL_USBFS_ResetCallback(HAL_USBFS_HandleTypeDef *husb);
void HAL_USBFS_SuspendCallback(HAL_USBFS_HandleTypeDef *husb);
void HAL_USBFS_ResumeCallback(HAL_USBFS_HandleTypeDef *husb);

/**
 * @brief  SETUP 包到达
 * @param  setup 8 字节原始 SETUP（bmRequestType/bRequest/wValue/wIndex/wLength）
 * @note   本层不解析任何字段，解析属于 Protocol/usb。
 */
void HAL_USBFS_SetupCallback(HAL_USBFS_HandleTypeDef *husb, const uint8_t setup[8]);

void HAL_USBFS_EPInCallback(HAL_USBFS_HandleTypeDef *husb, uint8_t ep);
void HAL_USBFS_EPOutCallback(HAL_USBFS_HandleTypeDef *husb, uint8_t ep);

/**
 * @brief  出错（当前只报 FIFO 溢出）
 * @param  code 保留
 */
void HAL_USBFS_ErrorCallback(HAL_USBFS_HandleTypeDef *husb, uint8_t code);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_USBFS_H */
