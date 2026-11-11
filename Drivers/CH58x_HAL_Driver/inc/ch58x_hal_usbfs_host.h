#ifndef __CH58X_HAL_USBFS_HOST_H
#define __CH58X_HAL_USBFS_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_usbfs.h"

/* ===================== 说明 =====================
 * USB1（全速）**主机模式**的硬件原语。
 *
 * 端点分配（硬件固定，不可改）：
 *   EP1  UH_SETUP    —— 辅助寄存器，放 SOF 使能 / PRE-PID 使能
 *   EP2  UH_RX_DMA   —— IN 方向（主机收），UH_EP_PID 借用 UEP2_T_LEN
 *   EP3  UH_TX_DMA   —— OUT/SETUP 方向（主机发）
 *
 * 与设备模式**互斥**：这些寄存器就是设备端点的同一批寄存器，
 * HAL_USBFS_Init(MODE_HOST) 之后不能再当设备用。
 *
 * 本层不含枚举：GET_DESCRIPTOR / SET_ADDRESS / SET_CONFIGURATION 的
 * 组包与状态机属于 Protocol/usb。
 */

/* ===================== 握手结果 ===================== */

#define HAL_USBFS_HST_PID_ACK   0x02U /* 与 USB_PID_ACK 同值 */
#define HAL_USBFS_HST_PID_NAK   0x0AU
#define HAL_USBFS_HST_PID_STALL 0x0EU

/**
 * @brief 一次事务的结果
 * @note  只有 HAL_USBFS_HST_OK 表示数据有效；NAK 应当重试，STALL 表示端点错误。
 */
typedef enum {
    HAL_USBFS_HST_OK = 0,  /**< 收到 ACK 且数据翻转正确 */
    HAL_USBFS_HST_NAK,     /**< 设备忙，可重试 */
    HAL_USBFS_HST_STALL,   /**< 端点挂起，需要 CONTROL_CLEAR_FEATURE */
    HAL_USBFS_HST_TIMEOUT, /**< 无应答 */
    HAL_USBFS_HST_ERROR    /**< 参数错误或总线错误 */
} HAL_USBFS_HST_ResultTypeDef;

/* ===================== API ===================== */

/** @brief 主机端点缓冲字节数（手册表 17-7 / 表 17-8）：双缓冲时由 TOG 选半区 */
#define HAL_USBFS_HST_BUF_LEN_SINGLE 64U
#define HAL_USBFS_HST_BUF_LEN_DOUBLE 128U

/**
 * @brief  安装主机收发缓冲并配置端点
 * @param  rxBuf IN 方向接收缓冲（4 字节对齐）
 * @param  txBuf OUT/SETUP 方向发送缓冲（4 字节对齐）
 * @param  mode  HAL_USBFS_BUF_SINGLE = 单 64 字节；
 *               HAL_USBFS_BUF_DOUBLE = 双 64 字节（缓冲需 128 字节，由 TOG 选半区）
 * @retval HAL_OK 成功；HAL_ERROR 指针为空/未对齐，或 mode 非法
 * @note   RB_UH_EP_TBUF_MOD / RB_UH_EP_RBUF_MOD 与设备侧 BUF_MOD 同在一个寄存器（UEP2_3_MOD），
 *         所以与方向使能位一起读-改-写。
 *         同时开启数据翻转自动切换（RB_UH_R_AUTO_TOG / RB_UH_T_AUTO_TOG）。
 */
HAL_StatusTypeDef HAL_USBFS_HST_EP_SetBuffer(uint8_t *rxBuf, uint8_t *txBuf,
                                             HAL_USBFS_BufModeTypeDef mode);

/**
 * @brief  低速前导包（PRE PID）使能（R8_UH_SETUP bit7）
 * @note   经外部全速 HUB 与低速设备通讯时必须置位。
 *         注意与 HAL_USBFS_HST_SetLowSpeed() 的区别：
 *         本函数是"向高速/全速总线插入 PRE 前导"，SetLowSpeed 是端口本身跑 1.5Mbps。
 */
HAL_StatusTypeDef HAL_USBFS_HST_SetPreamble(FunctionalState s);

/**
 * @brief  主机端口低速模式（R8_UHOST_CTRL bit2）：1 = 1.5Mbps，0 = 12Mbps
 * @note   用于直接连接低速设备（不经 HUB）；经 HUB 时用 HAL_USBFS_HST_SetPreamble()。
 */
HAL_StatusTypeDef HAL_USBFS_HST_SetLowSpeed(FunctionalState s);

/**
 * @brief  打开/关闭自动 SOF（1ms 令牌）
 */
HAL_StatusTypeDef HAL_USBFS_HST_SetSofEnable(FunctionalState s);

/**
 * @brief  使能/关闭端口（RB_UH_PORT_EN）
 * @note   设备拔掉后硬件会自动清零这一位。
 */
HAL_StatusTypeDef HAL_USBFS_HST_SetPortEnable(FunctionalState s);

/**
 * @brief  发出总线复位（SE0）
 * @retval HAL_OK 成功
 * @note   阻塞约 20ms，符合 USB 规范要求的下限 10ms。
 *         复位完成后设备地址回到 0。
 */
HAL_StatusTypeDef HAL_USBFS_HST_ResetBus(void);

/**
 * @brief  复位收发数据翻转位（控制传输进入 SETUP 阶段前必须调用）
 */
HAL_StatusTypeDef HAL_USBFS_HST_ResetToggle(void);

/**
 * @brief  执行一次 USB 事务（一次 token 往返），阻塞直到完成或超时
 * @param  pid       令牌：USB_PID_SETUP / USB_PID_IN / USB_PID_OUT
 * @param  ep        端点号 0~7
 * @param  len       发送长度；IN 事务忽略
 * @param  timeoutMs 超时（毫秒）
 * @retval HAL_USBFS_HST_OK / NAK / STALL / TIMEOUT / ERROR
 * @note   内部自动把 pid 和端点号拼成 UH_EP_PID 写进 UEP2_T_LEN。
 *         NAK 不自动重试，由上层按 USB 规范决定重试次数。
 *         不能放在中断里调用：依赖 HAL_GetTick() 做超时。
 */
HAL_USBFS_HST_ResultTypeDef HAL_USBFS_HST_Transact(uint8_t pid, uint8_t ep,
                                                   uint16_t len, uint16_t timeoutMs);

/** @brief 读最近一次 IN 事务收到的字节数 */
uint8_t HAL_USBFS_HST_GetRxLen(void);

/** @brief 当前是否有设备挂在总线上 */
uint8_t HAL_USBFS_HST_IsDetected(void);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_USBFS_HOST_H */
