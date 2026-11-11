/* 未定义 CH58x_USE_BLE_LIB 时本文件整体不参与编译，见 src/ch58x_hal_conf.h */
#include "ch58x_hal_conf.h"

#ifdef CH58x_USE_BLE_LIB

#ifndef __CH58X_HAL_BLE_H
#define __CH58X_HAL_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ch58x_hal_def.h 必须先于 CH58xBLE_LIB.h：前者通过 <string.h> 先把 NULL 定义成
 * ((void*)0)，后者只在未定义时才补 #define NULL 0，顺序反了会互相打架。
 */
#include "ch58x_hal_def.h"
#include "CH58xBLE_LIB.h"

/* ===================== 说明 =====================
 * 本模块只抽象「行为」（GAP 操作 + 事件分派 + 数据发送），
 * 不抽象「数据」：
 *   - GATT 服务表（gattAttribute_t 数组、各服务的读写回调）
 *   - 服务级参数 API（XXX_AddService / XXX_SetParameter）
 * 仍然直接用 WCH 原生接口，否则厂商例程没法直接抄。
 *
 * 应用代码里不应出现任何 gapRole_* / gattMsg_* 类型；需要高级 GAP 功能
 * （RSSI、PHY、扩展广播…）时直接 include CH58xBLE_LIB.h 调原生 API，
 * 本模块是便利层，不是门禁。
 *
 * 依赖：本模块假设 HAL_STACK_Init() 已成功，TMOS 时基已经由
 * TMOS_TimerInit() 配好；否则 GAPRole 的所有定时都是错的。
 *
 * 多连接：本模块最多支持 HAL_BLE_MAX_CONNECTIONS 条并发连接，用**索引**
 *   0 ~ HAL_BLE_MAX_CONNECTIONS-1 标识，而不是连接句柄 —— 索引在连接生命周期内
 *   稳定，且类型是 uint8_t，不会和 valueHandle(uint16_t) 传混。
 *   应用必须保证 HAL_STACK_InitTypeDef.PeripheralNumber <= HAL_BLE_MAX_CONNECTIONS，
 *   并且 BufNumber >= 连接数，否则超出槽位的连接会被拒绝接管并报
 *   HAL_BLE_ErrorCallback(code = 0x02)。
 */

/* ===================== 常量 =====================
 * 以下全部是蓝牙规范 / WCH 库规定的硬边界，不是可配置的默认值：
 *   - 广播与扫描响应长度由空口 PDU 结构决定（37 字节减 6 字节头）
 *   - 连接间隔、从机延迟、监督超时的取值范围由 Core Spec 规定
 *   - CONN_HANDLE_NONE 与库的 GAP_CONNHANDLE_INIT 保持一致
 * 可配置的默认值在 HAL_BLE_InitTypeDef 里；运行期能改的是
 * HAL_BLE_SetAdvData / SetScanRspData / UpdateConnParam。
 */

/** @brief 传统广播数据最大长度（PDU 37 字节 - 6 字节头） */
#define HAL_BLE_MAX_ADV_LEN 31U
/** @brief 传统扫描响应最大长度，同上 */
#define HAL_BLE_MAX_SCAN_RSP_LEN 31U

/** @brief 连接间隔单位 1.25ms，范围 0x0006(7.5ms) ~ 0x0C80(4s) */
#define HAL_BLE_INTERVAL_MIN 0x0006U
#define HAL_BLE_INTERVAL_MAX 0x0C80U

/** @brief 从机延迟上限，规范规定 0 ~ 499 个连接事件 */
#define HAL_BLE_LATENCY_MAX 499U

/** @brief 监督超时单位 10ms，范围 0x000A(100ms) ~ 0x0C80(32s) */
#define HAL_BLE_TIMEOUT_MIN 0x000AU
#define HAL_BLE_TIMEOUT_MAX 0x0C80U

/** @brief 未连接时的连接句柄占位值，与库的 GAP_CONNHANDLE_INIT 一致 */
#define HAL_BLE_CONN_HANDLE_NONE 0xFFFFU

/**
 * @brief 本模块支持的最大并发连接数
 * @note  与 HAL_STACK_InitTypeDef.PeripheralNumber 的上限（3）一致。
 *        调大只会多占 sizeof(HAL_BLE_ConnectionTypeDef) × N 的 RAM，
 *        真正能建几条连接由 PeripheralNumber / BufNumber 决定。
 */
#ifndef HAL_BLE_MAX_CONNECTIONS
#define HAL_BLE_MAX_CONNECTIONS 3U
#endif

/** @brief 「无此连接」的索引占位值，用于回调或查询失败 */
#define HAL_BLE_CONN_INDEX_NONE 0xFFU

/* ===================== 类型 ===================== */

/**
 * @brief  BLE 角色生命周期状态
 */
typedef enum {
    HAL_BLE_STATE_RESET = 0,   /**< 未初始化或已 DeInit */
    HAL_BLE_STATE_READY,       /**< 角色已启动，未广播 */
    HAL_BLE_STATE_ADVERTISING, /**< 广播中 */
    HAL_BLE_STATE_CONNECTED,   /**< 已连接 */
    HAL_BLE_STATE_ERROR        /**< 出错，见 HAL_BLE_ErrorCallback */
} HAL_BLE_StateTypeDef;

/**
 * @brief  连接参数
 * @note   三个值必须同时满足蓝牙规范的边界，HAL_BLE_Init / HAL_BLE_UpdateConnParam
 *         会逐项校验，不合法直接返 HAL_ERROR：
 *           - MIN_INTERVAL <= MaxInterval <= MAX_INTERVAL
 *           - Latency <= HAL_BLE_LATENCY_MAX
 *           - TIMEOUT_MIN <= Timeout <= TIMEOUT_MAX
 *           - Timeout * 4 > (1 + Latency) * MaxInterval   ← 最容易漏的一条
 */
typedef struct
{
    uint16_t MinInterval; /**< 最小连接间隔，单位 1.25ms */
    uint16_t MaxInterval; /**< 最大连接间隔，单位 1.25ms */
    uint16_t Latency;     /**< 从机延迟，单位是连接事件数 */
    uint16_t Timeout;     /**< 监督超时，单位 10ms */
} HAL_BLE_ConnParamTypeDef;

/**
 * @brief  一条连接的运行时状态
 * @note   由模块内部维护，应用只读（通过 HAL_BLE_GetConnection 拿指针）。
 *         索引 0 ~ HAL_BLE_MAX_CONNECTIONS-1 在连接存续期内固定不变。
 */
typedef struct
{
    uint16_t ConnHandle;     /**< 库给的连接句柄；Active=0 时无意义 */
    uint16_t Interval;       /**< 实际连接间隔，单位 1.25ms */
    uint16_t Latency;        /**< 实际从机延迟 */
    uint16_t Timeout;        /**< 实际监督超时，单位 10ms */
    volatile uint8_t Active; /**< 1 = 该槽已连接 */
} HAL_BLE_ConnectionTypeDef;

/**
 * @brief  初始化参数
 * @note   全部字段都可以填 0 / NULL，最简用法是定义一个全零实例（但 AdvData 必须有）。
 */
typedef struct
{
    const uint8_t *AdvData; /**< 广播数据，长度 1~31 */
    uint8_t AdvDataLen;
    const uint8_t *ScanRspData; /**< 扫描响应数据，可为 NULL */
    uint8_t ScanRspLen;
    HAL_BLE_ConnParamTypeDef ConnParam; /**< 期望连接参数，全 0 = 用库默认 */
    FunctionalState AutoAdvertise;      /**< ENABLE = 启动完成后自动开广播 */
} HAL_BLE_InitTypeDef;

/**
 * @brief  句柄，由应用定义实例并传入 API
 */
typedef struct
{
    HAL_BLE_InitTypeDef Init;                                /**< 初始化参数（Init 后会被归一化） */
    tmosTaskID TmosTaskId;                                   /**< 本模块的 TMOS 任务号 */
    HAL_BLE_ConnectionTypeDef Conn[HAL_BLE_MAX_CONNECTIONS]; /**< 连接槽 */
    volatile uint8_t ConnCount;                              /**< 当前已连接数 */
    volatile uint8_t State;                                  /**< HAL_BLE_STATE_xxx */
} HAL_BLE_HandleTypeDef;

/* ===================== API ===================== */

HAL_StatusTypeDef HAL_BLE_Init(HAL_BLE_HandleTypeDef *hble);

HAL_StatusTypeDef HAL_BLE_StartAdvertising(HAL_BLE_HandleTypeDef *hble);
HAL_StatusTypeDef HAL_BLE_StopAdvertising(HAL_BLE_HandleTypeDef *hble);

HAL_StatusTypeDef HAL_BLE_SetAdvData(HAL_BLE_HandleTypeDef *hble, const uint8_t *data, uint8_t len);
HAL_StatusTypeDef HAL_BLE_SetScanRspData(HAL_BLE_HandleTypeDef *hble, const uint8_t *data, uint8_t len);

/**
 * @brief  断开指定连接
 * @param  connIndex 连接索引 0 ~ HAL_BLE_MAX_CONNECTIONS-1
 * @retval HAL_OK 请求已下发；HAL_ERROR 索引非法/该槽未连接/库拒绝
 * @note   断开结果通过 HAL_BLE_DisconnectCallback 通知。
 */
HAL_StatusTypeDef HAL_BLE_Disconnect(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex);

/**
 * @brief  断开全部连接
 * @retval HAL_OK 全部请求已下发；HAL_ERROR 至少一个请求被库拒绝
 */
HAL_StatusTypeDef HAL_BLE_DisconnectAll(HAL_BLE_HandleTypeDef *hble);

/**
 * @brief  改连接参数
 * @param  connIndex 目标连接的索引；传 HAL_BLE_CONN_INDEX_NONE 表示
 *                   只修改希望值（不动已建立的连接，下次协商生效）
 * @param  param     目标参数，四项都必须满足规范边界（见 HAL_BLE_ConnParamValid）
 * @retval HAL_OK    已下发；结果由 HAL_BLE_ConnParamUpdateCallback 通知
 * @retval HAL_ERROR 参数越界或组合不成立、索引非法、或库拒绝请求
 */
HAL_StatusTypeDef HAL_BLE_UpdateConnParam(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                          const HAL_BLE_ConnParamTypeDef *param);

/**
 * @brief  向指定连接发通知（无需对端确认）
 * @param  connIndex   连接索引 0 ~ HAL_BLE_MAX_CONNECTIONS-1
 * @param  valueHandle 特征值句柄，由 GATT 服务表决定
 * @note   须在 TMOS 任务上下文调用（内部用 GATT_bm_alloc 申请协议栈缓冲）。
 */
HAL_StatusTypeDef HAL_BLE_Notify(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                 uint16_t valueHandle, const uint8_t *data, uint16_t len);

/**
 * @brief  向指定连接发指示（需对端确认）
 * @note   须在 TMOS 任务上下文调用；同一连接同一时刻只能有一个未确认的指示。
 */
HAL_StatusTypeDef HAL_BLE_Indicate(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                   uint16_t valueHandle, const uint8_t *data, uint16_t len);

uint8_t HAL_BLE_GetState(const HAL_BLE_HandleTypeDef *hble);

/** @brief 当前已连接数 */
uint8_t HAL_BLE_GetConnCount(const HAL_BLE_HandleTypeDef *hble);

/**
 * @brief  取一条连接的参数
 * @retval 连接信息指针（指向句柄内部，断开后失效）；索引非法或未连接返回 NULL
 */
const HAL_BLE_ConnectionTypeDef *HAL_BLE_GetConnection(const HAL_BLE_HandleTypeDef *hble,
                                                       uint8_t connIndex);

/* ===================== 弱回调 ===================== */

/**
 * @brief 连接建立
 * @param connIndex 分配到的连接索引，后续 Notify/Disconnect 都用它
 * @param conn      该连接的初始参数（指向句柄内部）
 */
void HAL_BLE_ConnectCallback(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                             const HAL_BLE_ConnectionTypeDef *conn);

/**
 * @brief 连接断开
 * @param connIndex 断开的连接索引；找不到对应槽位时为 HAL_BLE_CONN_INDEX_NONE
 * @param reason    HCI 错误码（0x08 超时、0x13 对端主动断开、0x16 本地终止…）
 * @note  回调触发时该槽已被清空，需要的连接信息请在 ConnectCallback 里存好。
 */
void HAL_BLE_DisconnectCallback(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex, uint8_t reason);

/** @brief 进入广播状态 */
void HAL_BLE_AdvertisingCallback(HAL_BLE_HandleTypeDef *hble);

/** @brief 某条连接的参数更新完成（对端同意后） */
void HAL_BLE_ConnParamUpdateCallback(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                     const HAL_BLE_ConnParamTypeDef *param);

/**
 * @brief 角色启动失败、进入错误态，或收到超出本模块能力的事件
 * @param  hble 句柄
 * @param  code 0x01 = 角色启动失败（HAL_BLE_ERROR_START）；
 *             0x02 = 连接槽耗尽（HAL_BLE_ERROR_NO_SLOT）；
 *             0x10 以上为库的 GAP opcode
 */
void HAL_BLE_ErrorCallback(HAL_BLE_HandleTypeDef *hble, uint8_t code);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_BLE_H */

#else /* CH58x_USE_BLE_LIB */

#error "CH58x_USE_BLE_LIB 未定义：Protocol/ble 不参与编译。要用 BLE 请到 src/ch58x_hal_conf.h 恢复定义。"

#endif /* CH58x_USE_BLE_LIB */
