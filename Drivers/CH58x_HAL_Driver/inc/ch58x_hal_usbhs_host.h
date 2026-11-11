#ifndef __CH58X_HAL_USBHS_HOST_H
#define __CH58X_HAL_USBHS_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_conf.h"

#if (CH58x_CHIP_MODEL == ID_CH584)

#error "CH584 没有 USB2（高速）模块。include 本文件的代码需要排除，或改用 ch58x_hal_usbfs_host.h。"

#else

#include "ch58x_hal_usbhs.h"

/* ===================== 说明 =====================
 * USB2（高速）**主机模式**的硬件原语。
 *
 * 与 FS 主机最大的结构差别：
 *   FS 主机复用设备端点寄存器（UH_EP_PID 借 UEP2_T_LEN、UH_RX_DMA 借 UEP2_DMA），
 *   所以设备/主机互斥。
 *   HS 主机有**独立寄存器组**（+0x100~+0x13F），与设备侧的端点寄存器完全不重叠：
 *     UH_CONTROL 一次装好 PID + 端点号 + 翻转 + 触发位，置 HOST_ACTION 开始事务；
 *     UH_RX_DMA / UH_TX_DMA 是主机专用的单一收发缓冲，不像设备端按端点分。
 *
 * 本层不含枚举：GET_DESCRIPTOR / SET_ADDRESS / SET_CONFIGURATION 的组包与
 * 状态机属于 Protocol/usb。
 */

/** @brief 一次事务的结果 */
typedef enum {
    HAL_USBHS_HST_OK = 0,  /**< 收到 ACK 或 DATA0/DATA1 */
    HAL_USBHS_HST_NAK,     /**< 设备忙，可重试 */
    HAL_USBHS_HST_NYET,    /**< 高速中继器未就绪（有 SPLIT 事务时出现） */
    HAL_USBHS_HST_STALL,   /**< 端点挂起，需要 CONTROL_CLEAR_FEATURE */
    HAL_USBHS_HST_TIMEOUT, /**< 无应答 */
    HAL_USBHS_HST_ERROR    /**< 参数错误或总线错误 */
} HAL_USBHS_HST_ResultTypeDef;

/* ===================== API ===================== */

/**
 * @brief  安装主机收发缓冲
 * @param  rxBuf    IN 方向缓冲，4 字节对齐
 * @param  txBuf    OUT/SETUP 方向缓冲，4 字节对齐
 * @param  rxMaxLen IN 事务允许接收的最大字节数（写入 UH_RX_MAX_LEN）
 * @retval HAL_OK 成功；HAL_ERROR 指针为空或未对齐
 * @note   HS 主机的收发缓冲是**全局唯一**的，不像设备端按端点配置。
 *         收发同一时刻只能有一个事务在跑，多设备切换只是换 DEV_ADDR。
 */
HAL_StatusTypeDef HAL_USBHS_HST_EP_SetBuffer(uint8_t *rxBuf, uint8_t *txBuf, uint16_t rxMaxLen);

/**
 * @brief  打开/关闭自动 SOF 令牌（总线保活）
 * @note   只动 R8_UH_CFG.SOF_EN，**不**打开 SOF 中断。
 *         关掉 SOF 后下游设备会进挂起，只在省电场景用。
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetSofEnable(FunctionalState s);

/**
 * @brief  设置目标设备地址（对应 SET_ADDRESS 之后要写的那一步）
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetDeviceAddress(uint8_t addr);

/**
 * @brief  执行一次 USB 事务，阻塞直到完成或超时
 * @param  pid       令牌：HAL_USBHS_PID_SETUP / _IN / _OUT
 * @param  ep        端点号 0~15
 * @param  tog       数据包 PID：HAL_USBHS_HST_TOG_DATA0 / _DATA1 / _DATA2 / _MDATA
 * @param  len       发送长度；IN 事务忽略
 * @param  timeoutMs 超时（毫秒）
 * @retval 握手结果
 * @note   等价于 TransactEx(..., 0U, ...)。
 *         NAK 不自动重试，由上层按 USB 规范决定重试次数。
 *         不能放在中断里调用：超时依赖 HAL_GetTick()。
 */
HAL_USBHS_HST_ResultTypeDef HAL_USBHS_HST_Transact(uint8_t pid, uint8_t ep, uint8_t tog,
                                                   uint16_t len, uint16_t timeoutMs);

/**
 * @brief  执行一次 USB 事务，可附加 UH_CONTROL 的额外控制位
 * @param  ctrlFlags 或进 UH_CONTROL 的位，可用值：
 *                     HAL_USBHS_HST_CTRL_PRE_PID_EN（经 FS HUB 接低速设备）
 *                     HAL_USBHS_HST_CTRL_SPLIT_VALID（启用 SPLIT 事务）
 *                     HAL_USBHS_HST_CTRL_LPM_VALID
 *                     HAL_USBHS_HST_CTRL_TX_NO_DATA / RX_NO_DATA
 *                     HAL_USBHS_HST_CTRL_TX_NO_RES  / RX_NO_RES
 *                     HAL_USBHS_HST_CTRL_BUF_MODE
 *                   传 0 即普通事务。
 * @note   HOST_ACTION 由本函数自己置位，不需要调用者传；
 *         SPLIT 事务要先调 HAL_USBHS_HST_SetSplitToken() 把令牌写好。
 */
HAL_USBHS_HST_ResultTypeDef HAL_USBHS_HST_TransactEx(uint8_t pid, uint8_t ep, uint8_t tog,
                                                     uint16_t len, uint32_t ctrlFlags,
                                                     uint16_t timeoutMs);

/**
 * @brief  写入 SPLIT 令牌
 * @param  token 用 HAL_USBHS_HST_SPLIT_TOKEN() 拼出来的 19 位值
 * @retval HAL_OK 成功；HAL_ERROR token 超出 19 位
 * @note   与 HAL_USBHS_HST_TransactEx(..., HAL_USBHS_HST_CTRL_SPLIT_VALID, ...) 配合使用。
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetSplitToken(uint32_t token);

/**
 * @brief  复位下游设备（发 SE0），阻塞到 PORT_RESET_IF 置位
 * @param  timeoutMs 超时（毫秒），常规给 100
 * @retval HAL_OK 复位完成；HAL_TIMEOUT 超时
 * @note   复位完成后设备地址回到 0，上层必须重新 SET_ADDRESS。
 */
HAL_StatusTypeDef HAL_USBHS_HST_ResetPort(uint16_t timeoutMs);

/**
 * @brief  关闭端口（CLR_PORT_EN）
 * @note   HS 的端口控制命令位是**只写**的，没有可读的使能位，
 *         所以本函数只发命令，不维护状态；状态一律从 PORT_STATUS 读。
 */
HAL_StatusTypeDef HAL_USBHS_HST_DisablePort(void);

/**
 * @brief  选择总线复位时长
 * @param  longReset ENABLE = 18ms，DISABLE = 16ms（硬件自动计时，软件不参与）
 * @note   这是 PORT_CTRL 里唯一的非命令配置位，会被命令写入保留。
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetResetDuration(FunctionalState longReset);

/** @brief 让端口进入挂起（SET_PORT_SUSP，WO 命令） */
HAL_StatusTypeDef HAL_USBHS_HST_SuspendPort(void);

/** @brief 让端口退出挂起（CLR_PORT_SUSP，WO 命令） */
HAL_StatusTypeDef HAL_USBHS_HST_ResumePort(void);

/**
 * @brief  让端口进入 SLEEP（LPM，SET_PORT_SLEEP）
 * @param  besl 唤醒时间：本函数会先把 BESL 写好，再发 SLEEP 命令
 * @retval HAL_OK 成功；HAL_ERROR besl 超出 0~15
 * @note   BESL 是 [15:12] 的 RW 配置位，与命令位同寄存器，
 *         所以两者必须同一次写入，不能先写 BESL 再发命令（否则中间状态会丢）。
 */
HAL_StatusTypeDef HAL_USBHS_HST_EnterSleep(HAL_USBHS_HST_BeslTypeDef besl);

/** @brief 让端口退出 SLEEP（CLR_PORT_SLEEP，WO 命令） */
HAL_StatusTypeDef HAL_USBHS_HST_ExitSleep(void);

/** @brief 端口测试模式位（R8_U2H_PORT_TEST_CT +0x133，手册已核对） */
#define HAL_USBHS_HST_PORT_TEST_J        0x01U
#define HAL_USBHS_HST_PORT_TEST_K        0x02U
#define HAL_USBHS_HST_PORT_TEST_FORCE_EN 0x04U
#define HAL_USBHS_HST_PORT_TEST_MASK     0x07U

/**
 * @brief  设置 HS 主机端口测试模式（R8_U2H_PORT_TEST_CT）
 * @param  mode HAL_USBHS_HST_PORT_TEST_* 的位组合，0 = 关闭
 * @retval HAL_OK 成功；HAL_ERROR mode 含未定义位
 * @note   属电气合规性测试，正常应用不应调用。
 */
HAL_StatusTypeDef HAL_USBHS_HST_SetPortTestMode(uint8_t mode);

/** @brief 读端口状态原始值（复位值 0x0010，即上电时 bit4 复位中为 1） */
uint16_t HAL_USBHS_HST_GetPortStatus(void);

/** @brief 当前是否有设备连接 */
uint8_t HAL_USBHS_HST_IsConnected(void);

/** @brief 已连接的设备是否运行在高速模式 */
uint8_t HAL_USBHS_HST_IsHighSpeed(void);

/** @brief 读最近一次 IN 事务收到的字节数 */
uint16_t HAL_USBHS_HST_GetRxLen(void);

/**
 * @brief  USB2 主机中断入口，由 USB2_HOST_IRQHandler 调用
 * @note   只处理端口状态变化、恢复和 FIFO 溢出。
 *         事务完成中断默认**不**打开（见 HAL_USBHS_Init），
 *         因为 HAL_USBHS_HST_Transact 是轮询 INT_FLAG 的，
 *         若同时开中断，ISR 会先把标志清掉，轮询永远等不到。
 */
void HAL_USBHS_HST_IRQHandler(HAL_USBHS_HandleTypeDef *husb);

/* ===================== 弱回调 ===================== */

void HAL_USBHS_HST_ConnectCallback(HAL_USBHS_HandleTypeDef *husb);
void HAL_USBHS_HST_DisconnectCallback(HAL_USBHS_HandleTypeDef *husb);
void HAL_USBHS_HST_PortResetCallback(HAL_USBHS_HandleTypeDef *husb);

#endif /* CH58x_CHIP_MODEL == ID_CH584 */

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_USBHS_HOST_H */
