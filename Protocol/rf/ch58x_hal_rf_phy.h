/* 未定义 CH58x_USE_BLE_LIB 时本文件整体不参与编译，见 src/ch58x_hal_conf.h */
#include "ch58x_hal_conf.h"

#ifdef CH58x_USE_BLE_LIB

/**
 * @file    ch58x_hal_rf_phy.h
 * @note    使用约定：
 *          1) Init/DeInit/Shut 及 *_IT API 须在 TMOS 任务上下文调用（RF 库要求）。
 *          2) 用户回调（__weak）统一在模块任务上下文分发；回调内可安全发起下一次收发。
 *          3) MaxLen/AckMaxLen 仅作 ISR 拷贝限幅，不改变空口长度；实际长度经回调 Len 传递。
 *
 *          4) 本模块属 Protocol 层，不是 HAL 层：它占用 TMOS 任务并驱动 BLE 库里的
 *             RF-PHY profile，所以放在 Protocol/rf 而不是 Drivers/CH58x_HAL_Driver。
 *          5) 标准 BLE 项目（GATT 透传 / HID / 心率…）用不到本模块，直接从工程排除；
 *             私有 2.4G 与 Mesh 才需要。BLE 与 RF-PHY 同属 libCH58xBLE.a，可以共存，
 *             但 rfConfig_t 是单实例全局参数，两者同时启用时需自行确认不互相覆盖。
 */
#ifndef __CH58X_HAL_RF_PHY_H
#define __CH58X_HAL_RF_PHY_H

#ifdef __cplusplus
extern "C" {
#endif

/* ch58x_hal_def.h 必须先于 CH58xBLE_LIB.h：前者通过 <string.h> 先把 NULL 定义成
 * ((void*)0)，后者只在未定义时才补 #define NULL 0，顺序反了会互相打架。
 */
#include "ch58x_hal_def.h"
#include "CH58xBLE_LIB.h"

/**
 * @brief  RF 模块生命周期状态
 */
typedef enum {
    HAL_RF_STATE_RESET = 0,  /**< 未初始化 / 已 DeInit */
    HAL_RF_STATE_READY,      /**< 空闲，可发起收发 */
    HAL_RF_STATE_BUSY_TX,    /**< 发送事务进行中 */
    HAL_RF_STATE_BUSY_RX,    /**< 接收窗口打开中 */
    HAL_RF_STATE_BUSY_TX_RX, /**< 自动模式过渡态（发后等回复 / 收后发 ACK） */
    HAL_RF_STATE_ERROR       /**< 预留：当前不使用 */
} HAL_RF_StateTypeDef;

/**
 * @brief  ErrorCode 位（__HAL_RF_GET_ERROR 读取；无错 = 0）
 */
#define HAL_RF_ERROR_NONE       0x00000000U /**< 无错误 */
#define HAL_RF_ERROR_TX_FAIL    0x00000001U /**< 0x11：发射失败（含 TIMEOUT 别名） */
#define HAL_RF_ERROR_RX_TX_FAIL 0x00000002U /**< 0x14：ACK 发送失败（含 TIMEOUT 别名） */
#define HAL_RF_ERROR_RX_CRC     0x00000004U /**< 收包 CRC 错（rsr bit0） */
#define HAL_RF_ERROR_RX_TYPE    0x00000008U /**< 收包类型不匹配（rsr bit1） */
#define HAL_RF_ERROR_START      0x00000010U /**< RF_Shut/RF_Tx/RF_Rx 启动失败 */

/**
 * @brief  TMOS 任务事件位（模块内部；每个完成事件唯一映射一个用户回调）
 */
#define HAL_RF_EVT_TX_FINISH     0x0001U /**< 0x01 -> HAL_RF_TxFinishCallback */
#define HAL_RF_EVT_TX_RX_DATA    0x0002U /**< 0x02(rsr==0) -> HAL_RF_TxRxDataCallback */
#define HAL_RF_EVT_TX_RX_TIMEOUT 0x0004U /**< 0x12 -> HAL_RF_TxRxTimeoutCallback */
#define HAL_RF_EVT_RX_DATA       0x0008U /**< 0x03(rsr==0) -> HAL_RF_RxDataCallback */
#define HAL_RF_EVT_RX_TX_FINISH  0x0010U /**< 0x04 -> HAL_RF_RxTxFinishCallback */
#define HAL_RF_EVT_ERROR         0x0020U /**< 0x11/0x14/0x02·0x03(rsr!=0) -> HAL_RF_ErrorCallback */
#define HAL_RF_EVT_HOP_SHUT      0x0040U /**< 0x22/0x24 -> HAL_RF_HopShutCallback */
#define HAL_RF_EVT_TX_REQ        0x0100U /**< TX 启动请求 */
#define HAL_RF_EVT_RX_REQ        0x0200U /**< RX 启动请求 */
#define HAL_RF_EVT_ALL           0x037FU /**< 全部 RF 事件位（未绑定/DeInit 丢弃用） */

/**
 * @brief  rsr 位掩码（0=正常；例程中的 crc 即此 rsr）
 */
#define HAL_RF_RSR_CRC_ERR  (1U << 0) /**< bit0：CRC 检查错误 */
#define HAL_RF_RSR_TYPE_ERR (1U << 1) /**< bit1：包类型不匹配 */

/* 接入地址（accessAddress）的 PHY 层约束，见厂商 EVT
 * BLE/RF_PHY/APP/RF_PHY.c 对 rf_Config.accessAddress 的注释。 */
#define HAL_RF_AA_MAX_TRANSITIONS 24U /**< 相邻位反转最多 24 次 */
#define HAL_RF_AA_MAX_RUN         6U  /**< 连续 0 或 1 最多 6 个 */

/**
 * @brief  __HAL_RF_* 读取宏
 */
#define __HAL_RF_GET_STATE(__HANDLE__) ((__HANDLE__)->State)     /**< 模块状态 */
#define __HAL_RF_GET_ERROR(__HANDLE__) ((__HANDLE__)->ErrorCode) /**< 错误位组合 */
#define __HAL_RF_GET_RSSI(__HANDLE__)  ((__HANDLE__)->LastRssi)  /**< 最近有效接收 RSSI */
/** @brief 原始接收状态（0=正常，bit0=CRC 错、bit1=类型不匹配；仅调试用） */
#define __HAL_RF_GET_RSR(__HANDLE__) ((__HANDLE__)->LastRsr)
/** @brief 最近一次有效接收长度（仅 rsr==0 时更新） */
#define __HAL_RF_GET_RX_LEN(__HANDLE__)   ((__HANDLE__)->RxXferSize)
#define __HAL_RF_IS_AUTO_MODE(__HANDLE__) (((__HANDLE__)->Config.LLEMode & LLE_MODE_AUTO) != 0)     /**< 是否 auto 模式 */
#define __HAL_RF_IS_NON_RSSI(__HANDLE__)  (((__HANDLE__)->Config.LLEMode & LLE_MODE_NON_RSSI) != 0) /**< 是否 NON_RSSI（BIT7=1） */
/** @brief 接收头部长度：BIT7=0 -> 2（[RSSI][len]），BIT7=1 -> 1（[len]） */
#define __HAL_RF_GET_RX_HEAD_LEN(__HANDLE__) (__HAL_RF_IS_NON_RSSI(__HANDLE__) ? 1U : 2U)
/** @brief 当前库配置（只读；修改请先复制副本，再经 HAL_RF_SetConfig 下发） */
#define __HAL_RF_GET_CONFIG(__HANDLE__) (&(__HANDLE__)->Config)

/**
 * @brief  RF 手动初始化参数：HAL_RF_Init 将其组装为 rfConfig_t 后调用 RF_Config
 * @note   字段语义/默认值以库 rfConfig_t 为准；rfStatusCB、Resv 由模块固定维护，不在此暴露
 */
typedef struct
{
    uint8_t LLEMode;        /**< LLE 模式位域（LLE_MODE_* 可按位或组合）：
                                 BIT0   0=basic（收发完成即回 idle）；1=auto（发后自动转收、收后自动回 ACK）
                                 BIT1   0=白化开（LLE_WHITENING_ON）；1=白化关（LLE_WHITENING_OFF）
                                 BIT4-5 00=1M PHY（LLE_MODE_PHY_1M）；01=2M PHY（LLE_MODE_PHY_2M）；10/11=保留
                                 BIT6   0=按 Channel 选数据通道；1=按 Frequency 直设频点（LLE_MODE_EX_CHANNEL）
                                 BIT7   0=接收缓冲首字节为 RSSI；1=首字节为包类型（LLE_MODE_NON_RSSI） */
    uint8_t Channel;        /**< 数据通道 0-39，f = 2402 + 2*ch MHz（BIT6=0 时有效） */
    uint32_t Frequency;     /**< 直设频点 2400000~2483500 kHz（BIT6=1 时有效） */
    uint32_t accessAddress; /**< 32bit PHY 接入地址（收发双方须一致）。
                                 必须满足 PHY 层约束，否则 HAL_RF_Init / HAL_RF_SetConfig
                                 会直接返回 HAL_ERROR：
                                   - 禁止 0x55555555 / 0xAAAAAAAA（直流不平衡）
                                   - 相邻位反转（跳变）不超过 24 次
                                   - 连续 0 或 1 不超过 6 个
                                 填之前可用 HAL_RF_IsValidAccessAddress() 自检；
                                 厂商例程的合法值：0x71764129、0x71762345。 */
    uint32_t CRCInit;       /**< CRC 初值（收发双方须一致） */
    uint32_t ChannelMap;    /**< 跳频信道图：bit n 对应数据通道 n，1=使用（RF_FrequencyHoppingTx/Rx 用；当前未启用，填 0） */
    uint8_t HeartPeriod;    /**< 跳频心跳包周期，单位 100ms：实际间隔 = 本值 × 100ms（0~255 → 最大 25.5s；
                                 0=库内部默认；仅跳频模式（RF_FrequencyHoppingTx/Rx）有效，固定频点模式无心跳包） */
    uint8_t HopPeriod;      /**< 跳频周期 T=32n*RTC 时钟，库默认 8 */
    uint8_t HopIndex;       /**< 跳频算法 hopIncrement，库默认 17 */
    uint8_t RxMaxlen;       /**< 单包最大接收长度，最大 251 */
    uint8_t TxMaxlen;       /**< 单包最大发送长度，最大 251（HAL 发送长度前置校验用） */
    uint8_t pktRxType;      /**< 收包类型过滤：0xFF=接收所有类型（含广播）；其他=仅本类型或广播 */
    uint8_t pktTxType;      /**< 发包类型：0xFF=广播（所有匹配类型可收）；其他=仅同类型可收 */
} RF_InitTypeDef;

/**
 * @brief  RF 模块句柄（应用定义实例并传入各 API；模块内单实例绑定）
 */
typedef struct
{
    RF_InitTypeDef Init;   /**< 手动初始化参数（HAL_RF_Init 首次下发；后续改参数用 Config） */
    rfConfig_t Config;     /**< 当前库配置（__HAL_RF_GET_CONFIG 获取；副本修改后经 HAL_RF_SetConfig 下发） */
    tmosTaskID TmosTaskId; /**< 模块任务 ID（Init 注册） */

    __IO HAL_RF_StateTypeDef State; /**< 生命周期状态 */
    __IO uint32_t ErrorCode;        /**< HAL_RF_ERROR_xxx 位组合 */
    __IO uint8_t LastRsr;           /**< 最近接收状态 rsr（0=正常，bit0 CRC 错、bit1 类型不匹配） */
    __IO int8_t LastRssi;           /**< 最近有效接收 RSSI（BIT7=0 时更新） */
    __IO uint8_t TxEndFlag;         /**< 发送事务闸门：0x01(basic)/0x02/0x12/0x11/0x22 置位 */
    __IO uint8_t RxEndFlag;         /**< ACK 发送完成闸门：0x04/0x14/0x24 置位 */

    const uint8_t *pTxBuffPtr; /**< TX 挂起参数：待发数据 */
    uint16_t TxXferSize;       /**< TX 挂起参数：待发长度 */

    const uint8_t *pReplyBuffPtr; /**< 自动回复数据（NULL 时回退 DummyReply/0） */
    uint16_t ReplyXferSize;       /**< 自动回复长度 */

    uint8_t *pAppRxBuffPtr; /**< RX 挂起参数：应用接收缓冲（可 NULL=不拷贝） */
    uint16_t RxBuffMaxLen;  /**< 接收缓冲容量（拷贝限幅） */

    uint8_t *pAckBuffPtr;   /**< auto-TX 对端回复缓冲（可 NULL=不拷贝） */
    uint16_t AckBuffMaxLen; /**< 回复缓冲容量（拷贝限幅） */

    __IO uint16_t RxXferSize; /**< 最近实际接收长度（回调 Len 用） */
    uint8_t DummyReply;       /**< 兜底回复缓冲 */
} RF_HandleTypeDef;

void HAL_RF_TxFinishCallback(RF_HandleTypeDef *hrf);
void HAL_RF_TxRxDataCallback(RF_HandleTypeDef *hrf, uint8_t *pData, uint16_t Len);
void HAL_RF_TxRxTimeoutCallback(RF_HandleTypeDef *hrf);
void HAL_RF_RxDataCallback(RF_HandleTypeDef *hrf, uint8_t *pData, uint16_t Len);
void HAL_RF_RxTxFinishCallback(RF_HandleTypeDef *hrf);
void HAL_RF_ErrorCallback(RF_HandleTypeDef *hrf);
void HAL_RF_HopShutCallback(RF_HandleTypeDef *hrf);

uint8_t HAL_RF_IsValidAccessAddress(uint32_t aa);

HAL_StatusTypeDef HAL_RF_Init(RF_HandleTypeDef *hrf);
HAL_StatusTypeDef HAL_RF_DeInit(RF_HandleTypeDef *hrf);
HAL_StatusTypeDef HAL_RF_Shut(RF_HandleTypeDef *hrf);
HAL_StatusTypeDef HAL_RF_SetConfig(RF_HandleTypeDef *hrf, const rfConfig_t *pConfig);
HAL_StatusTypeDef HAL_RF_Transmit_IT(RF_HandleTypeDef *hrf, const uint8_t *pTxData, uint16_t Len,
                                     uint8_t *pAckBuf, uint16_t AckMaxLen);
HAL_StatusTypeDef HAL_RF_Receive_IT(RF_HandleTypeDef *hrf, uint8_t *pRxBuf, uint16_t MaxLen);
HAL_StatusTypeDef HAL_RF_ReceiveAutoReply_IT(RF_HandleTypeDef *hrf, uint8_t *pRxBuf, uint16_t MaxLen,
                                             const uint8_t *pReplyData, uint16_t ReplyLen);
void HAL_RF_IRQHandler(RF_HandleTypeDef *hrf, uint8_t sta, uint8_t rsr, uint8_t *rxBuf);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_RF_PHY_H */

#else /* CH58x_USE_BLE_LIB */

#error "CH58x_USE_BLE_LIB 未定义：Protocol/rf 不参与编译。要用 BLE/RF 请到 src/ch58x_hal_conf.h 恢复定义。"

#endif /* CH58x_USE_BLE_LIB */