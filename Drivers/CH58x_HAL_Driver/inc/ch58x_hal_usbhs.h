#ifndef __CH58X_HAL_USBHS_H
#define __CH58X_HAL_USBHS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_conf.h"

#if (CH58x_CHIP_MODEL == ID_CH584)

#error "CH584 没有 USB2（高速）模块。include 本文件的代码需要排除，或改用 ch58x_hal_usbfs.h。"

#else

#include "ch58x_hal_def.h"

/* ===================== 说明 =====================
 * USB2（高速，480Mbps）的**硬件抽象**：寄存器模型、端点原语、中断分发。
 *
 * 位定义来源：CH585.svd（MounRiver SDK 自带）。
 *   CH585SFR.h 只给了 R8_USB2_* / R8_U2EPn_* 的**地址**，位域定义全在 SVD 里，
 *   SVD 也是目前唯一能查到的官方字段说明（SDK 里没有 USBHS 例程）。
 *
 * 与 USB1（全速）的结构差异，本层必须区别对待：
 *   | 项目        | USBFS                        | USBHS                        |
 *   |-------------|------------------------------|------------------------------|
 *   | 主机寄存器  | 复用设备端点寄存器           | 独立，位于 +0x100~+0x13F     |
 *   | 设备/主机   | 互斥，运行期不能切换         | 地址不重叠，可同时具备能力   |
 *   | 包长寄存器  | 无                           | UEPn_MAX_LEN（每端点）       |
 *   | 接收长度    | 全局 R8_USB_RX_LEN           | 每端点 UEPn_RX_LEN           |
 *   | 长度位宽    | 8 位                         | 16 位                        |
 *   | 自动翻转    | 仅 EP1~EP3                   | 每端点 T_TOG_AUTO/R_TOG_AUTO |
 *   | TRANSFER 清 | 写 1 清                      | 只读，读 MIS_ST 清           |
 *   | 端点号位宽  | 3 位                         | TX_EN/RX_EN 16 位，但只有 0~7 可用 |
 *
 * 不做的事：枚举、描述符、class 协议、主机枚举栈 → 属于 Protocol/usb。
 *
 * ===================== 位定义证据来源 =====================
 * 全部寄存器位域均已经 CH585 数据手册逐位核对：
 *   设备侧 62 个 / 主机侧 20 组寄存器及其全部字段。
 * 仍存疑的只有两处语义推断（非位定义）：
 *   1) 主机 CFG.bit3 PHY_SUSPENDM 的极性：手册只写"PHY挂起"未给 1/0 含义，
 *      按设备侧同名寄存器（1 = 正常工作）推断；
 *   2) 主机 CFG.bit3 若推断反了，主机会完全不响应，届时优先查这里。
 * DMA 寄存器存的是**绝对地址**（含 0x2000xxxx），不是 RAM 偏移，
 * 依据厂商 CH58x_usbdev.c / CH58x_usbhostBase.c 的写法。
 *
 * CH585.svd 已被证实至少 8 处错误（MAX_LEN 位宽、BURST 字段重名、HOST_MODE 位置、
 * PORT_CFG/PORT_INT_EN 的 access、DOWN_LOWSPD 命名、RTX_ACT 清标志方式、
 * 主机 DMA 位宽、主中断 RESUME 标志），不可作为依据。
 */

/* ===================== 寄存器模型 ===================== */

/** @brief 端点长度与控制三元组，步长 4（与 +0x9C 起的布局一致） */
typedef struct
{
    volatile uint16_t TLEN;   /**< +0 发送长度 */
    volatile uint8_t TX_CTRL; /**< +2 发送控制：RES[1:0] / TOG[3:2] / NAK_ACT / DONE */
    volatile uint8_t RX_CTRL; /**< +3 接收控制：RES[1:0] / TOG[3:2] / SETUP_IS / ... */
} USBHS_EPCtrlTypeDef;

/** @brief 端点接收长度对，步长 4 */
typedef struct
{
    volatile uint16_t RX_LEN;  /**< +0 单次接收长度 */
    volatile uint16_t RX_SIZE; /**< +2 累计接收长度 */
} USBHS_EPRxLenTypeDef;

/**
 * @brief  USB2 设备侧寄存器块 @ 0x40009000，覆盖 +0x00 ~ +0xBF
 * @note   主机侧在 +0x100，见 USBHS_HST_TypeDef。
 */
typedef struct
{
    volatile uint8_t CTRL;                   /**< +0x00 复位 / DMA / 设备使能 / LPM */
    volatile uint8_t BASE_MODE;              /**< +0x01 EXP_SPD 期望速度 */
    volatile uint8_t INT_EN;                 /**< +0x02 */
    volatile uint8_t DEV_AD;                 /**< +0x03 设备地址 [6:0] */
    volatile uint8_t WAKE_CTRL;              /**< +0x04 远程唤醒，写 1 触发 */
    volatile uint8_t TEST_MODE;              /**< +0x05 */
    volatile uint16_t LPM_DATA;              /**< +0x06 */
    volatile uint8_t INT_FG;                 /**< +0x08 中断标志，写 1 清（TRANSFER 除外） */
    volatile uint8_t INT_ST;                 /**< +0x09 端点号 + 方向 */
    volatile uint8_t MIS_ST;                 /**< +0x0A 只读；读它同时清 TRANSFER 标志 */
    volatile uint8_t RESERVED0;              /**< +0x0B */
    volatile uint16_t FRAME_NO;              /**< +0x0C */
    volatile uint16_t BUS;                   /**< +0x0E */
    volatile uint16_t TX_EN;                 /**< +0x10 发送使能，bit n = EPn */
    volatile uint16_t RX_EN;                 /**< +0x12 接收使能，bit n = EPn */
    volatile uint16_t T_TOG_AUTO;            /**< +0x14 发送自动翻转使能 */
    volatile uint16_t R_TOG_AUTO;            /**< +0x16 接收自动翻转使能 */
    volatile uint8_t T_BURST;                /**< +0x18 */
    volatile uint8_t T_BURST_MODE;           /**< +0x19 */
    volatile uint8_t R_BURST;                /**< +0x1A */
    volatile uint8_t R_RES_MODE;             /**< +0x1B */
    volatile uint32_t AF_MODE;               /**< +0x1C 端点复用 */
    volatile uint32_t RX_DMA[8];             /**< +0x20 EP0~EP7 接收缓冲（EP0 收发共用） */
    volatile uint32_t TX_DMA[7];             /**< +0x40 EP1~EP7 发送缓冲，索引从 0 起等价 EP1 */
    volatile uint32_t MAX_LEN[8];            /**< +0x5C EP0~EP7 最大包长 */
    volatile USBHS_EPRxLenTypeDef RX_LEN[8]; /**< +0x7C EP0~EP7 接收长度 */
    volatile USBHS_EPCtrlTypeDef EP[8];      /**< +0x9C EP0~EP7 长度与控制 */
    volatile uint16_t T_ISO;                 /**< +0xBC 发送同步（等时）使能 */
    volatile uint16_t R_ISO;                 /**< +0xBE 接收同步（等时）使能 */
} USBHS_DEV_TypeDef;

/**
 * @brief  USB2 主机侧寄存器块 @ 0x40009100
 * @note   与设备侧地址完全分开，这是 HS 与 FS 最大的结构差别。
 */
typedef struct
{
    volatile uint8_t CFG;             /**< +0x00 链路/SIE 复位、DMA、SOF、主机模式 */
    volatile uint8_t RESERVED0;       /**< +0x01 */
    volatile uint8_t INT_EN;          /**< +0x02 */
    volatile uint8_t DEV_ADDR;        /**< +0x03 目标设备地址 */
    volatile uint32_t CONTROL;        /**< +0x04 PID / 端点号 / 翻转 / 触发位 */
    volatile uint8_t INT_FLAG;        /**< +0x08 写 1 清 */
    volatile uint8_t INT_ST;          /**< +0x09 收到的 PID */
    volatile uint8_t MIS_ST;          /**< +0x0A 总线状态 */
    volatile uint8_t RESERVED1;       /**< +0x0B */
    volatile uint32_t LPM;            /**< +0x0C */
    volatile uint32_t SPLIT;          /**< +0x10 */
    volatile uint32_t FRAME;          /**< +0x14 */
    volatile uint32_t TX_LEN;         /**< +0x18 */
    volatile uint32_t RX_LEN;         /**< +0x1C */
    volatile uint32_t RX_MAX_LEN;     /**< +0x20 */
    volatile uint32_t RX_DMA;         /**< +0x24 */
    volatile uint32_t TX_DMA;         /**< +0x28 */
    volatile uint32_t PORT_CTRL;      /**< +0x2C 端口复位/挂起/使能控制 */
    volatile uint8_t PORT_CFG;        /**< +0x30 HOST_MODE / PORT_RPD */
    volatile uint8_t RESERVED2;       /**< +0x31 */
    volatile uint8_t PORT_INT_EN;     /**< +0x32 */
    volatile uint8_t PORT_TEST;       /**< +0x33 */
    volatile uint16_t PORT_STATUS;    /**< +0x34 连接/使能/挂起/复位/速度 */
    volatile uint8_t PORT_STATUS_CHG; /**< +0x36 写 1 清 */
    volatile uint8_t RESERVED3;       /**< +0x37 */
    volatile uint32_t RESERVED4;      /**< +0x38 */
    volatile uint32_t ROOT_BC_CTRL;   /**< +0x3C BC1.2 充电协议 */
} USBHS_HST_TypeDef;

#define USBHS                           ((USBHS_DEV_TypeDef *)0x40009000U)
#define USBHS_HST                       ((USBHS_HST_TypeDef *)0x40009100U)
/** @brief HS PHY 的 480MHz PLL 控制（只用到低 3 位） */
#define USBHS_PLL                       (*(volatile uint8_t *)0x40009200U)

/* ===================== 常量 ===================== */

/** @brief 可用端点上限。TX_EN/RX_EN 位宽 16，但只有 EP0~EP7 有 DMA 寄存器 */
#define HAL_USBHS_EP_MAX                7U
/** @brief 单次传输最大字节数（高速端点缓冲上限） */
#define HAL_USBHS_EP_XFER_MAX           1024U

/** @brief EP0 最大包长上限（控制端点，USB 规定只能是 8/16/32/64） */
#define HAL_USBHS_EP0_MPS_MAX           64U

/* UEPn_MAX_LEN 的真实位宽（数据手册寄存器表复位值列：
 *   R32_U2EP0_MAX_LEN     0x000000XX  → 低 8 位可写，最大 255
 *   R32_U2EP1~7_MAX_LEN   0x00000XXX  → 低 12 位可写，最大 4095
 * 两者都远大于 USB 规范上限（控制 64 / 数据端点 1024），所以包长直接写原值，不做换算。
 *
 * 注意 CH585.svd 把这条写成了 [6:0]（7 位），是文档错误：
 * 7 位最大 127，装不下高速批量端点的 512 字节，也与同一份 SVD 里
 * 主机侧 UH_RX_MAX_LEN[10:0] 自相矛盾。此处以手册为准。 */

/* R8_USB2_CTRL(+0x00)。注意 bit3 的名字是反的，见下 */
#define HAL_USBHS_CTRL_LINK_RESET       0x01U /**< 1 = LINK 层复位，需软件清零 */
#define HAL_USBHS_CTRL_SIE_RESET        0x02U /**< 1 = 强制复位 SIE（含端点寄存器），需软件清零 */
#define HAL_USBHS_CTRL_ALL_CLR          0x04U /**< 1 = 清空中断标志和 FIFO，需软件清零 */
#define HAL_USBHS_CTRL_PHY_SUSPENDM     0x08U /**< ⚠ 手册原文：**1 = 正常工作；0 = 挂起** */
#define HAL_USBHS_CTRL_DMA_EN           0x10U /**< DMA 传输使能 */
#define HAL_USBHS_CTRL_DEV_EN           0x20U /**< USB 设备使能 */
#define HAL_USBHS_CTRL_LPM_EN           0x80U /**< LPM 使能 */

/* R8_USB2_BASE_MODE(+0x01) RB_UD_SPEED_TYPE[1:0]，已手册核对 */
#define HAL_USBHS_SPEED_MASK            0x03U
#define HAL_USBHS_SPEED_FULL            0x00U /**< 00 = 全速设备 */
#define HAL_USBHS_SPEED_HIGH            0x01U /**< 01 = 高速设备 */
#define HAL_USBHS_SPEED_LOW             0x02U /**< 10 = 低速设备（11 保留） */

/* R8_USB2_WAKE_CTRL(+0x04)：bit[7:1] 保留 RO；bit0 为 RW1Z —— 写 1 触发，硬件自动清零 */
#define HAL_USBHS_WAKE_REMOTE           0x01U

/** @brief 挂起后允许发出远程唤醒的最小延时（USB 2.0 规范要求 ≥ 5ms） */
#define HAL_USBHS_WAKE_DELAY_MS         5U

/* R8_USB2_INT_EN(+0x02) / R8_USB2_INT_FG(+0x08)，位位置相同 */
#define HAL_USBHS_INT_BUS_RST           0x01U
#define HAL_USBHS_INT_SUSPEND           0x02U
#define HAL_USBHS_INT_SLEEP             0x04U
#define HAL_USBHS_INT_LPM_ACT           0x08U
#define HAL_USBHS_INT_TRANSFER          0x10U /**< 只读，读 MIS_ST 清 */
#define HAL_USBHS_INT_RX_SOF            0x20U
#define HAL_USBHS_INT_LINK_RDY          0x40U
#define HAL_USBHS_INT_FIFO_OV           0x80U

/* R8_USB2_TEST_MODE(+0x05)，手册已核对 */
#define HAL_USBHS_TEST_EN               0x80U /**< 测试模式总使能 */
#define HAL_USBHS_TEST_SE0NAK           0x08U /**< 输出 SE0 */
#define HAL_USBHS_TEST_PKT              0x04U /**< 输出一个包（用 EP4 的地址与长度，TOG=DATA0） */
#define HAL_USBHS_TEST_K                0x02U /**< 输出 K */
#define HAL_USBHS_TEST_J                0x01U /**< 输出 J */
#define HAL_USBHS_TEST_MODES            (HAL_USBHS_TEST_SE0NAK | HAL_USBHS_TEST_PKT | HAL_USBHS_TEST_K | HAL_USBHS_TEST_J)

/* R16_USB2_FRAME_NO(+0x0C)：只读，bit[15:13] 微帧号，bit[10:0] 帧号 */
#define HAL_USBHS_FRAME_NO_MASK         0x07FFU
#define HAL_USBHS_MFRAME_NO_POS         13U
#define HAL_USBHS_MFRAME_NO_MASK        0xE000U

/* R8_USB2_INT_ST(+0x09) */
#define HAL_USBHS_INTST_EP_MASK         0x07U
#define HAL_USBHS_INTST_EP_DIR          0x10U /**< 1 = IN */

/* R8_USB2_MIS_ST(+0x0A) */
#define HAL_USBHS_MIS_READY             0x01U
#define HAL_USBHS_MIS_SUSPEND           0x02U
#define HAL_USBHS_MIS_SLEEP             0x04U
#define HAL_USBHS_MIS_SIE_FREE          0x08U
#define HAL_USBHS_MIS_SUSP_REQ          0x10U
#define HAL_USBHS_MIS_HS_MODE           0x80U /**< 手册原文：主机是否为高速 */

/* 端点应答码（手册 R8_UEPn_TX_CTRL / RX_CTRL 的 RES[1:0]）
 * ⚠ **与 FS 完全不同，不要照 FS 拄**：
 *   00 = NAK（忙）   01 = STALL（错误）   10 = ACK   11 = 保留
 * FS 是 0=ACK / 2=NAK，照搬会把整个端点应答全反过来。
 */
#define HAL_USBHS_RES_MASK              0x03U
#define HAL_USBHS_RES_NAK               0x00U
#define HAL_USBHS_RES_STALL             0x01U
#define HAL_USBHS_RES_ACK               0x02U

/* 端点控制位
 * 注意 DONE 与 NAK_ACT 是 **RW0**：硬件置 1，软件写 0 清。
 * 所以不能对 CTRL 整寄存器赋值（会把 TOG 一起写 0，EP0 的 DATA0/DATA1 就乱了）。
 */
#define HAL_USBHS_EP_T_DONE             0x80U /**< RW0 发送结束标志 */
#define HAL_USBHS_EP_T_NAK_ACT          0x40U /**< RW0 发送 NAK 结束标志 */
#define HAL_USBHS_EP_R_DONE             0x80U /**< RW0 接收结束标志 */
#define HAL_USBHS_EP_R_NAK_ACT          0x40U /**< RW0 接收 NAK 结束标志 */
#define HAL_USBHS_EP_R_NAK_TOG          0x20U /**< RO  返回 NAK 时的数据包类型 */
#define HAL_USBHS_EP_R_TOG_MATCH        0x10U /**< RO  收到的翻转位是否匹配 */
#define HAL_USBHS_EP_R_SETUP_IS         0x08U /**< RO  仅 EP0 RX_CTRL：本次是 SETUP 事务 */

/* TOG（期望的数据翻转）—— EP0 与 EPn 的位宽不同！
 *   EP0   : bit2 单 bit，0 = DATA0，1 = DATA1
 *   EP1~7 : bit[3:2] 两位，0 = DATA0，1 = DATA1，2 = DATA2，3 = MDATA
 * 开了 T_TOG_AUTO / R_TOG_AUTO 的端点，这两位不可写（硬件自动管）。
 */
#define HAL_USBHS_EP_TOG_POS_EP0        2U
#define HAL_USBHS_EP_TOG_MASK_EP0       0x04U
#define HAL_USBHS_EP_TOG_MASK           0x0CU /**< EP1~EP7 */
#define HAL_USBHS_EP_TOG_DATA0          0U
#define HAL_USBHS_EP_TOG_DATA1          1U

/* R8_USBHS_PLL_CTRL(+0x200)，已手册核对 */
#define HAL_USBHS_PLL_CKSEL             0x01U /**< 1 = 不分频保持 32M；0 = 2 分频产生 16M */
#define HAL_USBHS_PLL_LOWPOW            0x02U /**< 低功耗（仅固定全速应用可用，省约 0.3mA） */
#define HAL_USBHS_PLL_EN                0x04U /**< PLL 使能 */

/**
 * @brief PLL 使能后到可以释放 SIE 复位的等待时间（ms）
 * @note  手册表22-7「PLL 特性」：锁相时间 TPLLLK 最小 20µs / 典型 40µs，**未给最大值**；
 *        同表 FPLL = CK16M × 39 = 624MHz，即 PLL 参考时钟是 16MHz，不直接用 HSE 32M。
 *        同表还有 TSTHSE（HSE 稳定时间 500µs 典型 / 3ms 最大），但它只影响
 *        「PLL 参考若取自 HSE」的情况；本工程 HAL_Init 已把系统时钟切到 HSE，
 *        到这里 HSE 早已稳定，不重复等待。
 *
 *        为什么仍取 1ms 而不是几百 µs：
 *          1) 手册没给最大锁相时间，拿 40µs 典型值当上限不安全；
 *          2) 1ms 走 HAL_Delay（SysTick 按实际主频计时），不受 HAL_SYS_DelayUs
 *             那个按编译期 HAL_RCC_SYSCLK_HZ 标定的 NOP 循环误差影响；
 *          3) 代价只是开机多等 1ms。
 *        若后续能查实最大值，可下调。
 */
#define HAL_USBHS_PLL_SETTLE_MS         1U

/* R8_UH_CFG(+0x100) 主机链路 / 时钟配置 */
#define HAL_USBHS_HST_CFG_LINK_RESET    0x01U
#define HAL_USBHS_HST_CFG_SIE_RESET     0x02U
#define HAL_USBHS_HST_CFG_ALL_CLR       0x04U
#define HAL_USBHS_HST_CFG_PHY_SUSPENDM  0x08U
#define HAL_USBHS_HST_CFG_DMA_EN        0x10U
#define HAL_USBHS_HST_CFG_SOF_EN        0x20U
#define HAL_USBHS_HST_CFG_FORCE_FS      0x40U
#define HAL_USBHS_HST_CFG_LPM_EN        0x80U

/* R8_UH_INT_EN(+0x102) 与 R8_UH_INT_FLAG(+0x108) —— 两组**并非完全对称**！
 *   位7 FIFO_OVER / 位6 TX_HALT / 位5 SOF_ACT / 位4 TRANSFER / 位2 WKUP_ACT：两边都有
 *   位3：INT_EN 是 RB_UHIE_RESUME_ACT（总线恢复中断使能），
 *        但 INT_FLAG 的 bit3 是**保留位**，没有对应的恢复标志。
 *         恢复事件只能从 R8_U2H_INT_ST.PORT_RX_RESUME(bit4) 读。
 */
#define HAL_USBHS_HST_INT_WKUP          0x04U
#define HAL_USBHS_HST_INT_RESUME        0x08U /**< 仅 INT_EN：总线恢复中断使能 */
#define HAL_USBHS_HST_INT_TRANSFER      0x10U
#define HAL_USBHS_HST_INT_SOF           0x20U
#define HAL_USBHS_HST_INT_TX_HALT       0x40U
#define HAL_USBHS_HST_INT_FIFO_OV       0x80U

/* R32_UH_CONTROL(+0x104) —— 主机侧事务控制，与 FS 的 UH_EP_PID 写法完全不同 */
#define HAL_USBHS_HST_CTRL_PID_POS      0U          /**< [3:0]  令牌 PID */
#define HAL_USBHS_HST_CTRL_EP_POS       4U          /**< [7:4]  令牌端点号 */
#define HAL_USBHS_HST_CTRL_TOG_POS      8U          /**< [9:8]  数据 PID，见 HAL_USBHS_HST_TOG_* */
#define HAL_USBHS_HST_CTRL_BUF_MODE     (1UL << 10) /**< 1 = 收发 DMA 对调，默认 0 */
#define HAL_USBHS_HST_CTRL_ACTION       (1UL << 16) /**< 置 1 触发，事务完成后硬件自清 */
#define HAL_USBHS_HST_CTRL_LPM_VALID    (1UL << 17)
#define HAL_USBHS_HST_CTRL_SPLIT_VALID  (1UL << 18)
#define HAL_USBHS_HST_CTRL_PRE_PID_EN   (1UL << 19) /**< 低速前导包 PRE PID 使能（经 FS HUB 接 LS 设备） */
#define HAL_USBHS_HST_CTRL_TX_NO_DATA   (1UL << 20)
#define HAL_USBHS_HST_CTRL_RX_NO_DATA   (1UL << 21)
#define HAL_USBHS_HST_CTRL_TX_NO_RES    (1UL << 22)
#define HAL_USBHS_HST_CTRL_RX_NO_RES    (1UL << 23)

/* CTRL_TOG_POS 位置的数据 PID 取值（2 位，不是普通的 0/1） */
#define HAL_USBHS_HST_TOG_DATA0         0U
#define HAL_USBHS_HST_TOG_DATA1         1U
#define HAL_USBHS_HST_TOG_DATA2         2U /**< 高速高带宽等时传输 */
#define HAL_USBHS_HST_TOG_MDATA         3U /**< 高速高带宽等时传输 */

/* R32_UH_SPLIT_DATA(+0x110)：19 位 SPLIT 令牌，字段顺序与 USB 2.0 规范 8.4.2.1 一致 */
#define HAL_USBHS_HST_SPLIT_HUBADDR_POS 0U
#define HAL_USBHS_HST_SPLIT_SC_POS      7U
#define HAL_USBHS_HST_SPLIT_PORT_POS    8U
#define HAL_USBHS_HST_SPLIT_S_POS       15U
#define HAL_USBHS_HST_SPLIT_E_POS       16U
#define HAL_USBHS_HST_SPLIT_ET_POS      17U
#define HAL_USBHS_HST_SPLIT_MASK        0x0007FFFFUL /**< bit[18:0] */

#define HAL_USBHS_HST_SPLIT_ET_CONTROL  0U
#define HAL_USBHS_HST_SPLIT_ET_ISO      1U
#define HAL_USBHS_HST_SPLIT_ET_BULK     2U
#define HAL_USBHS_HST_SPLIT_ET_INT      3U

#define HAL_USBHS_HST_SPLIT_SC_START    0U
#define HAL_USBHS_HST_SPLIT_SC_COMPLETE 1U
#define HAL_USBHS_HST_SPLIT_S_FULL      0U
#define HAL_USBHS_HST_SPLIT_S_LOW       1U
#define HAL_USBHS_HST_SPLIT_E_MIDDLE    0U
#define HAL_USBHS_HST_SPLIT_E_END       1U

/**
 * @brief 拼一个 SPLIT 令牌字
 * @param  hubAddr 集线器地址 7 位
 * @param  sc      HAL_USBHS_HST_SPLIT_SC_START / _COMPLETE
 * @param  port    端口号 7 位
 * @param  s       HAL_USBHS_HST_SPLIT_S_FULL / _LOW
 * @param  e       HAL_USBHS_HST_SPLIT_E_MIDDLE / _END
 * @param  et      HAL_USBHS_HST_SPLIT_ET_CONTROL / _ISO / _BULK / _INT
 */
#define HAL_USBHS_HST_SPLIT_TOKEN(hubAddr, sc, port, s, e, et) \
    ((((uint32_t)(hubAddr) & 0x7FUL) << HAL_USBHS_HST_SPLIT_HUBADDR_POS) | (((uint32_t)(sc) & 0x01UL) << HAL_USBHS_HST_SPLIT_SC_POS) | (((uint32_t)(port) & 0x7FUL) << HAL_USBHS_HST_SPLIT_PORT_POS) | (((uint32_t)(s) & 0x01UL) << HAL_USBHS_HST_SPLIT_S_POS) | (((uint32_t)(e) & 0x01UL) << HAL_USBHS_HST_SPLIT_E_POS) | (((uint32_t)(et) & 0x03UL) << HAL_USBHS_HST_SPLIT_ET_POS))

/* R8_UH_INT_ST(+0x109) */
#define HAL_USBHS_HST_PID_MASK          0x0FU /**< 本次收到的 PID */
#define HAL_USBHS_HST_INTST_RESUME      0x10U /**< RW：端口收到唤醒信号，需软件清零 */

/* R8_UH_MIS_ST(+0x10A) */
#define HAL_USBHS_HST_MIS_SOF_FREE      0x01U
#define HAL_USBHS_HST_MIS_SOF_PRE       0x02U
#define HAL_USBHS_HST_MIS_SOF_ST        0x04U
#define HAL_USBHS_HST_MIS_WAKEUP        0x08U
#define HAL_USBHS_HST_MIS_LINESTATE     0x30U
#define HAL_USBHS_HST_MIS_BUS_J         0x40U
#define HAL_USBHS_HST_MIS_BUS_SE0       0x80U

/* R32_U2H_PORT_CTRL(+0x12C)
 * 这颗寄存器混装了两类位，访问权限不一样：
 *   [8]:0]、[5:0] 的 7 条命令是 **WO（只写）**，读完全 0；
 *   bit16 BUS_RST_LONG 与 [15:12] PORT_SLEEP_BESL 是 **RW 配置位**，可读可写。
 * 所以**不能用 `=` 整体赋值**，也不能只保 BESL —— 必须把 [16:12] 一起保住，
 * 否则每发一条命令就会把总线复位时长清回 16ms。
 */
#define HAL_USBHS_HST_PORT_SET_RESET    (1UL << 0)  /**< WO 端口发送复位 */
#define HAL_USBHS_HST_PORT_SET_SUSP     (1UL << 1)  /**< WO 进入挂起 */
#define HAL_USBHS_HST_PORT_CLR_SUSP     (1UL << 2)  /**< WO 退出挂起 */
#define HAL_USBHS_HST_PORT_SET_SLEEP    (1UL << 3)  /**< WO 进入 SLEEP（LPM） */
#define HAL_USBHS_HST_PORT_CLR_EN       (1UL << 4)  /**< WO 退出使能，进 DISABLED */
#define HAL_USBHS_HST_PORT_CLR_CONNECT  (1UL << 5)  /**< WO 使端口进入端口状态 */
#define HAL_USBHS_HST_PORT_CLR_SLEEP    (1UL << 8)  /**< WO 退出 SLEEP（LPM） */
#define HAL_USBHS_HST_PORT_BUS_RST_LONG (1UL << 16) /**< RW 0 = 16ms 复位，1 = 18ms */

#define HAL_USBHS_HST_PORT_BESL_POS     12U
#define HAL_USBHS_HST_PORT_BESL_MASK    0x0000F000UL /**< RW PORT_SLEEP_BESL[15:12] */
#define HAL_USBHS_HST_PORT_RSTLONG_MASK 0x00010000UL /**< RW BUS_RST_LONG[16] */
/** @brief 发命令时要原样保住的配置位（漏掉任何一个都会被清 0） */
#define HAL_USBHS_HST_PORT_KEEP_MASK    (HAL_USBHS_HST_PORT_BESL_MASK | HAL_USBHS_HST_PORT_RSTLONG_MASK)

/**
 * @brief LPM SLEEP 唤醒时间 PORT_SLEEP_BESL[3:0] 的取值
 * @note  手册编码表：0=125us / 1=150us / 2=200us / 3=300us / 4=400us / 5=500us /
 *        6=1ms  / 7=2ms   / 8=3ms   / 9=4ms   / A=5ms   / B=6ms   /
 *        C=7ms  / D=8ms   / E=9ms   / F=10ms
 */
typedef enum {
    HAL_USBHS_HST_BESL_125US = 0,
    HAL_USBHS_HST_BESL_150US,
    HAL_USBHS_HST_BESL_200US,
    HAL_USBHS_HST_BESL_300US,
    HAL_USBHS_HST_BESL_400US,
    HAL_USBHS_HST_BESL_500US,
    HAL_USBHS_HST_BESL_1MS,
    HAL_USBHS_HST_BESL_2MS,
    HAL_USBHS_HST_BESL_3MS,
    HAL_USBHS_HST_BESL_4MS,
    HAL_USBHS_HST_BESL_5MS,
    HAL_USBHS_HST_BESL_6MS,
    HAL_USBHS_HST_BESL_7MS,
    HAL_USBHS_HST_BESL_8MS,
    HAL_USBHS_HST_BESL_9MS,
    HAL_USBHS_HST_BESL_10MS
} HAL_USBHS_HST_BeslTypeDef;

/* R8_U2H_PORT_CFG(+0x130)
 * 注意：这不是"主机配置"，而是**端口方向总开关**。设备与主机共用这一位，
 * 所以每次切模式都必须显式重写，不能指望它自己回到默认值。
 */
#define HAL_USBHS_HST_PORT_CFG_HOST_EN  0x01U /**< 1 = 主机模式，0 = 设备模式 */
#define HAL_USBHS_HST_PORT_CFG_PD_EN    0x80U /**< 主机模式下 15K 下拉使能 */

/* R8_U2H_PORT_INT_EN(+0x132)，bit3 与 bit[7:6] 保留 */
#define HAL_USBHS_HST_PORT_IE_CONNECT   0x01U
#define HAL_USBHS_HST_PORT_IE_EN        0x02U
#define HAL_USBHS_HST_PORT_IE_SUSP      0x04U
#define HAL_USBHS_HST_PORT_IE_RESET     0x10U
#define HAL_USBHS_HST_PORT_IE_SLEEP     0x20U

/* R16_PORT_STATUS(+0x134) */
#define HAL_USBHS_HST_PORT_CONNECT      0x0001U
#define HAL_USBHS_HST_PORT_EN           0x0002U
#define HAL_USBHS_HST_PORT_SUSP         0x0004U
#define HAL_USBHS_HST_PORT_RESETTING    0x0010U
#define HAL_USBHS_HST_PORT_SLP          0x0020U
#define HAL_USBHS_HST_PORT_LOW_SPD      0x0200U
#define HAL_USBHS_HST_PORT_HIGH_SPD     0x0400U
#define HAL_USBHS_HST_PORT_TEST         0x0800U

/* R8_PORT_STATUS_CHG(+0x136)，写 1 清 */
#define HAL_USBHS_HST_PORT_CHG_CONNECT  0x01U
#define HAL_USBHS_HST_PORT_CHG_EN       0x02U
#define HAL_USBHS_HST_PORT_CHG_SUSP     0x04U
#define HAL_USBHS_HST_PORT_CHG_RESET    0x10U
#define HAL_USBHS_HST_PORT_CHG_SLEEP    0x20U

/* USB 标准 PID 码（UH_INT_ST.THIS_PID_MASK 与 UH_CONTROL.PID_MASK 共用） */
#define HAL_USBHS_PID_OUT               0x01U
#define HAL_USBHS_PID_ACK               0x02U
#define HAL_USBHS_PID_DATA0             0x03U
#define HAL_USBHS_PID_SOF               0x05U
#define HAL_USBHS_PID_NYET              0x06U
#define HAL_USBHS_PID_DATA1             0x0BU
#define HAL_USBHS_PID_IN                0x09U
#define HAL_USBHS_PID_SETUP             0x0DU
#define HAL_USBHS_PID_STALL             0x0EU
#define HAL_USBHS_PID_NAK               0x0AU

/* ===================== 类型 ===================== */

/** @brief 工作模式 */
typedef enum {
    HAL_USBHS_MODE_DEVICE = 0,
    HAL_USBHS_MODE_HOST
} HAL_USBHS_ModeTypeDef;

/** @brief 生命周期状态 */
typedef enum {
    HAL_USBHS_STATE_RESET = 0,
    HAL_USBHS_STATE_READY,
    HAL_USBHS_STATE_ERROR
} HAL_USBHS_StateTypeDef;

/** @brief 句柄 */
typedef struct
{
    USBHS_DEV_TypeDef *Instance; /**< 恒为 USBHS */
    HAL_USBHS_ModeTypeDef Mode;
    volatile uint8_t State;
} HAL_USBHS_HandleTypeDef;

/* ===================== 公共 API ===================== */

HAL_StatusTypeDef HAL_USBHS_Init(HAL_USBHS_HandleTypeDef *husb, HAL_USBHS_ModeTypeDef mode);
HAL_StatusTypeDef HAL_USBHS_DeInit(HAL_USBHS_HandleTypeDef *husb);
void HAL_USBHS_IRQHandler(HAL_USBHS_HandleTypeDef *husb);
uint8_t HAL_USBHS_GetState(const HAL_USBHS_HandleTypeDef *husb);

/**
 * @brief  设置 HS 设备测试模式（R8_USB2_TEST_MODE）
 * @param  mode 0 = 关闭；否则是 HAL_USBHS_TEST_* 子模式位的或（会自行补上 TEST_EN）
 * @retval HAL_OK 成功；HAL_ERROR mode 含非子模式的位
 * @note   手册：SE0NAK 输出 SE0；PKT 输出一个包（用端点4 的地址与长度、TOG=DATA0）；
 *         K / J 输出对应电平。属电气合规性测试，正常应用不应调用。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_SetTestMode(uint8_t mode);

/**
 * @brief  端点别名映射（R32_U2EP_AF_MODE）
 * @param  ep     端点号 1~7（EP0 无此功能）
 * @param  enable ENABLE = 该端点复用为 EP9~EP15；DISABLE = 复用为 EP1~EP7
 * @retval HAL_OK 成功；HAL_ERROR 端点号非法
 * @note   手册手册原文：「RB_UEP_T_AF[6:0]：1 = 复用 9-15；0 = 复用 1-7」。
 *         EP8~EP15 是 EP1~EP7 的**别名**，不是额外端点（它们没有自己的 DMA 寄存器）。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_SetEndpointAlias(uint8_t ep, FunctionalState enable);

/**
 * @brief  板级初始化：USB 时钟、PB12/PB13 引脚、480M PLL
 * @note   HAL 给有内容的默认实现。板上需要 VBUS 检测、供电开关时在 BSP 强定义覆盖。
 */
void HAL_USBHS_MspInit(HAL_USBHS_HandleTypeDef *husb);
void HAL_USBHS_MspDeInit(HAL_USBHS_HandleTypeDef *husb);

/* ===================== 设备模式 API ===================== */

HAL_StatusTypeDef HAL_USBHS_DEV_SetConnect(FunctionalState s);
HAL_StatusTypeDef HAL_USBHS_DEV_SetSpeed(uint8_t speed);
HAL_StatusTypeDef HAL_USBHS_DEV_SetAddress(uint8_t addr);

/**
 * @brief  打开端点
 * @param  ep     端点号 0~7
 * @param  maxLen 最大包长：EP0 取 8/16/32/64（高速下必为 64），
 *                EP1~EP7 取 64（中断）/ 512（高速批量）/ 1024（等时）
 * @param  rxBuf  OUT 缓冲，4 字节对齐
 * @param  txBuf  IN 缓冲，4 字节对齐；EP0 忽略（与 rxBuf 共用一组 DMA）
 * @retval HAL_OK 成功
 * @retval HAL_ERROR 端点号越界、指针未对齐、或 maxLen 超出寄存器位宽
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Open(uint8_t ep, uint16_t maxLen,
                                        uint8_t *rxBuf, uint8_t *txBuf);
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Close(uint8_t ep);

/**
 * @brief  配置端点突发（burst）传输
 * @param  ep          端点号 1~7（EP0 不支持）
 * @param  enable      ENABLE = 打开突发传输（同时置收发两个使能位）
 * @param  skipZeroLen ENABLE = 突发模式下不发送 0 长度包
 * @param  nyetReply   ENABLE = 接收数据包时回 NYET；DISABLE = 回 ACK
 * @retval HAL_OK 成功；HAL_ERROR 端点号越界或传了 EP0
 * @note   寄存器 T_BURST / T_BURST_MODE / R_BURST / R_RES_MODE 都是按端点的
 *         位图（bit n = EPn），与 T_TOG_AUTO 同一形式。
 *         高速批量端点需要它才能跑满带宽；HID 中断端点用不到。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_SetBurst(uint8_t ep, FunctionalState enable,
                                            FunctionalState skipZeroLen,
                                            FunctionalState nyetReply);

/**
 * @brief  把端点切换成等时（同步）传输
 * @param  ep 端点号 1~7（EP0 是控制端点，不能等时）
 * @param  s  ENABLE = 置 T_ISO/R_ISO 对应位；DISABLE = 清除
 * @retval HAL_OK 成功；HAL_ERROR 端点号非法
 * @note   手册 R16_U2EP_T_ISO / R_ISO 的 [7:1] 对应 EP1~EP7，**bit0 保留**。
 *         置位后收发按等时规则处理（无握手）；每帧发一包的节拍调度不属于本层。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_EP_SetIso(uint8_t ep, FunctionalState s);
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Transmit(uint8_t ep, uint16_t len);
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Receive(uint8_t ep);
HAL_StatusTypeDef HAL_USBHS_DEV_EP_Nak(uint8_t ep);
HAL_StatusTypeDef HAL_USBHS_DEV_EP_SetStall(uint8_t ep, FunctionalState s);

/** @brief 读某端点最近一次接收长度（HS 是每端点独立寄存器） */
uint16_t HAL_USBHS_DEV_EP_GetRxLen(uint8_t ep);
/** @brief 读某端点累计接收长度（RX_SIZE，多包累积；FPGA 单包用 RX_LEN） */
uint16_t HAL_USBHS_DEV_EP_GetRxSize(uint8_t ep);
/**
 * @brief  读帧号寄存器原始值（R16_USB2_FRAME_NO，只读）
 * @return bit[10:0] = FRAME_NO，bit[15:13] = MFRAME_NO（微帧号）
 * @note   帧号与 SOF 同步，等时传输用它算节拍。宏 HAL_USBHS_FRAME_NO_MASK /
 *         HAL_USBHS_MFRAME_NO_POS 可用于拆字段。
 */
uint16_t HAL_USBHS_DEV_GetFrameNo(void);
/** @brief USB 链路是否已建立（MIS_ST.RB_UDMS_READY） */
uint8_t HAL_USBHS_DEV_IsReady(void);
/** @brief 当前是否运行在高速模式（MIS_ST.RB_UDMS_HS_MOD，主机侧是否为高速） */
uint8_t HAL_USBHS_DEV_IsHighSpeed(void);
/** @brief USB 总线当前是否处于挂起状态（MIS_ST.RB_UDMS_SUSPEND） */
uint8_t HAL_USBHS_DEV_IsSuspended(void);

/**
 * @brief  触发一次远程唤醒
 * @retval HAL_OK    已触发，硬件产生 resume 波形后自动清零标志
 * @retval HAL_BUSY  挂起未满 5ms，请稍后再调
 * @retval HAL_ERROR 当前不在挂起状态，无需唤醒
 * @note   使用前提：配置描述符的 bmAttributes bit5 已声明支持远程唤醒。
 *         挂起后 ≥ 5ms 的延时由本模块保证（挂起时 IRQ 记录时间戳），
 *         应用层不需要自己计时；唤醒信号持续时间由硬件控制。
 *         典型用法：在 HAL_USBHS_SuspendCallback() 里把 USB 视为可睡眠，
 *         按键按下时直接调本函数，返回值 BUSY 就下一轮再试。
 */
HAL_StatusTypeDef HAL_USBHS_DEV_RemoteWakeUp(void);

/* ===================== 弱回调 ===================== */

void HAL_USBHS_ResetCallback(HAL_USBHS_HandleTypeDef *husb);
void HAL_USBHS_SuspendCallback(HAL_USBHS_HandleTypeDef *husb);
void HAL_USBHS_ResumeCallback(HAL_USBHS_HandleTypeDef *husb);

/**
 * @brief  SETUP 包到达
 * @param  setup 8 字节原始 SETUP
 * @note   本层不解析字段，解析属于 Protocol/usb。
 *         HS 用 EP0 RX_CTRL 的 RB_SETUP_IS 位判定，不需要另设标志。
 */
void HAL_USBHS_SetupCallback(HAL_USBHS_HandleTypeDef *husb, const uint8_t setup[8]);

void HAL_USBHS_EPInCallback(HAL_USBHS_HandleTypeDef *husb, uint8_t ep);
void HAL_USBHS_EPOutCallback(HAL_USBHS_HandleTypeDef *husb, uint8_t ep);
void HAL_USBHS_ErrorCallback(HAL_USBHS_HandleTypeDef *husb, uint8_t code);

#endif /* CH58x_CHIP_MODEL == ID_CH584 */

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_USBHS_H */
