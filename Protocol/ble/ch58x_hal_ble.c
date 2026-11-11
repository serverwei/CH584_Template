#include "ch58x_hal_conf.h"

#ifdef CH58x_USE_BLE_LIB

#include "ch58x_hal_ble.h"

/* ===================== 内部常量 ===================== */

/* 本模块的 TMOS 事件位。SYS_EVENT_MSG(0x8000) 由库占用，不能自用。 */
#define HAL_BLE_EVT_START 0x0001U

/* HAL_BLE_ErrorCallback 的 code：0x01 起是模块自定义，0x10 以上才是 GAP opcode */
#define HAL_BLE_ERROR_START   0x01U /* GAPRole_PeripheralStartDevice 失败 */
#define HAL_BLE_ERROR_NO_SLOT 0x02U /* 连接数超过 HAL_BLE_MAX_CONNECTIONS */

/* ===================== 内部状态 ===================== */

/* GAP 回调没有上下文参数，用文件级指针回指 */
static HAL_BLE_HandleTypeDef *s_pHble;

/* 不接管配对回调（静态零初始化 = 全空实现）。
 * 需要 Passkey / 配对状态通知时，在这里填 passcodeCB / pairStateCB。 */
static gapBondCBs_t s_bondCBs;

/* ===================== 内部函数 ===================== */

/**
 * @brief  按连接句柄查找槽位
 * @param  hble       句柄
 * @param  connHandle 库给的连接句柄
 * @retval 0 ~ HAL_BLE_MAX_CONNECTIONS-1；找不到返回 HAL_BLE_CONN_INDEX_NONE
 */
static uint8_t HAL_BLE_FindConn(const HAL_BLE_HandleTypeDef *hble, uint16_t connHandle)
{
    uint8_t i;

    for (i = 0U; i < HAL_BLE_MAX_CONNECTIONS; i++) {
        if ((hble->Conn[i].Active != 0U) && (hble->Conn[i].ConnHandle == connHandle)) {
            return i;
        }
    }

    return (uint8_t)HAL_BLE_CONN_INDEX_NONE;
}

/**
 * @brief  清空一个连接槽
 */
static void HAL_BLE_ClearConn(HAL_BLE_HandleTypeDef *hble, uint8_t idx)
{
    hble->Conn[idx].ConnHandle = HAL_BLE_CONN_HANDLE_NONE;
    hble->Conn[idx].Interval   = 0U;
    hble->Conn[idx].Latency    = 0U;
    hble->Conn[idx].Timeout    = 0U;
    hble->Conn[idx].Active     = 0U;
}

/**
 * @brief  取一个已连接的槽位
 * @retval 槽位指针；索引非法或该槽未连接则返回 NULL
 */
static HAL_BLE_ConnectionTypeDef *HAL_BLE_GetActiveConn(HAL_BLE_HandleTypeDef *hble, uint8_t idx)
{
    if ((hble == NULL) || (idx >= HAL_BLE_MAX_CONNECTIONS)) {
        return NULL;
    }
    if (hble->Conn[idx].Active == 0U) {
        return NULL;
    }

    return &hble->Conn[idx];
}

/**
 * @brief  GAP 状态变化回调，把库事件翻译成本模块的弱回调
 * @param  newState 库状态字的低 4 位表示广播状态机
 * @param  pEvent   事件联合体，opcode 区分具体事件
 * @note   顺序很重要：带 opcode 的两类事件（建立/断开）先处理，
 *         因为它们携带连接句柄等参数，而 switch 分支只是一个粗粒度状态。
 */
static void HAL_BLE_StateChangeCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    HAL_BLE_HandleTypeDef *p = s_pHble;
    uint8_t               i;
    uint8_t               idx;

    if ((p == NULL) || (pEvent == NULL)) {
        return;
    }

    if (pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT) {
        if (pEvent->gap.hdr.status == SUCCESS) {
            /* 找第一个空槽。槽位本身就是连接在模块里的身份，
             * 在连接存续期内不变，应用可以直接拿它做 connIndex。 */
            for (i = 0U; i < HAL_BLE_MAX_CONNECTIONS; i++) {
                if (p->Conn[i].Active == 0U) {
                    break;
                }
            }

            /* 槽位耗尽：不接管这条连接（也不覆盖已有记录），只报错。
             * 不在回调里调 GAPRole_TerminateLink：那是同一个状态机上下文，可能重入。 */
            if (i >= HAL_BLE_MAX_CONNECTIONS) {
                HAL_BLE_ErrorCallback(p, HAL_BLE_ERROR_NO_SLOT);
                return;
            }

            p->Conn[i].ConnHandle = pEvent->linkCmpl.connectionHandle;
            p->Conn[i].Interval   = pEvent->linkCmpl.connInterval;
            p->Conn[i].Latency    = pEvent->linkCmpl.connLatency;
            p->Conn[i].Timeout    = pEvent->linkCmpl.connTimeout;
            p->Conn[i].Active     = 1U;

            p->ConnCount++;
            p->State = HAL_BLE_STATE_CONNECTED;

            HAL_BLE_ConnectCallback(p, i, &p->Conn[i]);
        }
        return;
    }

    if (pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
        uint8_t reason = pEvent->linkTerminate.reason;

        idx = HAL_BLE_FindConn(p, pEvent->linkTerminate.connectionHandle);
        if (idx < HAL_BLE_MAX_CONNECTIONS) {
            HAL_BLE_ClearConn(p, idx);
            if (p->ConnCount > 0U) {
                p->ConnCount--;
            }
        }

        /* 还有连接就别改成非连接态 */
        if (p->ConnCount == 0U) {
            p->State = ((newState & GAPROLE_STATE_ADV_MASK) == GAPROLE_ADVERTISING)
                           ? HAL_BLE_STATE_ADVERTISING
                           : HAL_BLE_STATE_READY;
        }

        HAL_BLE_DisconnectCallback(p, idx, reason);
        return;
    }

    switch (newState & GAPROLE_STATE_ADV_MASK) {
        case GAPROLE_STARTED:
            p->State = HAL_BLE_STATE_READY;
            break;

        case GAPROLE_ADVERTISING:
            /* 建立连接后库可能回到 CONNECTED_ADV，不能把状态改回 ADVERTISING */
            p->State = (p->ConnCount > 0U) ? HAL_BLE_STATE_CONNECTED
                                           : HAL_BLE_STATE_ADVERTISING;
            HAL_BLE_AdvertisingCallback(p);
            break;

        case GAPROLE_CONNECTED:
        case GAPROLE_CONNECTED_ADV:
            /* 连接建立已由 GAP_LINK_ESTABLISHED_EVENT 分支处理，这里只同步状态 */
            p->State = HAL_BLE_STATE_CONNECTED;
            break;

        case GAPROLE_WAITING:
            /* 广播结束等待下一次，或连接失败回退，都算可再广播的中间态 */
            p->State = (p->ConnCount > 0U) ? HAL_BLE_STATE_CONNECTED
                                           : HAL_BLE_STATE_READY;
            break;

        case GAPROLE_ERROR:
        default:
            p->State = HAL_BLE_STATE_ERROR;
            HAL_BLE_ErrorCallback(p, (uint8_t)pEvent->gap.opcode);
            break;
    }
}

/**
 * @brief  连接参数更新完成回调
 */
static void HAL_BLE_ParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                  uint16_t connSlaveLatency, uint16_t connTimeout)
{
    HAL_BLE_ConnParamTypeDef prm;
    HAL_BLE_HandleTypeDef   *p = s_pHble;
    uint8_t                  idx;

    if (p == NULL) {
        return;
    }

    idx = HAL_BLE_FindConn(p, connHandle);

    if (idx < HAL_BLE_MAX_CONNECTIONS) {
        p->Conn[idx].Interval = connInterval;
        p->Conn[idx].Latency  = connSlaveLatency;
        p->Conn[idx].Timeout  = connTimeout;
    }

    prm.MinInterval = connInterval; /* 更新完成后区间收敛成实际值 */
    prm.MaxInterval = connInterval;
    prm.Latency     = connSlaveLatency;
    prm.Timeout     = connTimeout;

    HAL_BLE_ConnParamUpdateCallback(p, idx, &prm);
}

/**
 * @brief  RSSI 读取回调
 * @note   本模块不主动读 RSSI；这里必须有实体，避免库在未判空时调用到 NULL。
 *         应用需要 RSSI 时直接调原生 GAPRole_ReadRssiCmd()。
 */
static void HAL_BLE_RssiReadCB(uint16_t connHandle, int8_t newRSSI)
{
    UNUSED(connHandle);
    UNUSED(newRSSI);
}

/* 传给 GAPRole_PeripheralStartDevice 的回调表 */
static gapRolesCBs_t s_rolesCBs =
    {
        HAL_BLE_StateChangeCB,
        HAL_BLE_RssiReadCB,
        HAL_BLE_ParamUpdateCB};

/**
 * @brief  本模块的 TMOS 任务
 * @param  taskID 任务号
 * @param  events 待处理事件位
 * @retval 未处理的事件位
 * @note   角色启动必须在这个上下文里做，应用从 main() 调 HAL_BLE_Init 只是投递事件。
 */
static tmosEvents HAL_BLE_ProcessEvent(tmosTaskID taskID, tmosEvents events)
{
    HAL_BLE_HandleTypeDef *p = s_pHble;

    if ((events & SYS_EVENT_MSG) != 0U) {
        /* 本模块不收 TMOS 消息：收到即释放，避免协议栈堆泄漏 */
        uint8_t *msg = tmos_msg_receive(taskID);

        if (msg != NULL) {
            (void)tmos_msg_deallocate(msg);
        }
        events = (tmosEvents)(events ^ SYS_EVENT_MSG);
    }

    if ((events & HAL_BLE_EVT_START) != 0U) {
        events = (tmosEvents)(events ^ HAL_BLE_EVT_START);

        if (p != NULL) {
            if (GAPRole_PeripheralStartDevice(taskID, &s_bondCBs, &s_rolesCBs) == SUCCESS) {
                p->State = HAL_BLE_STATE_READY;

                if (p->Init.AutoAdvertise != DISABLE) {
                    uint8_t en = TRUE;

                    (void)GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,
                                               (uint16_t)sizeof(uint8_t), &en);
                }
            } else {
                p->State = HAL_BLE_STATE_ERROR;
                HAL_BLE_ErrorCallback(p, HAL_BLE_ERROR_START);
            }
        }
    }

    return events;
}

/**
 * @brief  校验连接参数是否满足蓝牙规范
 * @param  p 待校验参数
 * @retval HAL_OK 合法；HAL_ERROR 越界或组合不成立
 * @note   这些约束不满足时，对端会直接拒绝参数更新请求（或干脆超时），
 *         现象是「参数改了怎么都不生效」，所以必须在本地拦下来。
 */
static HAL_StatusTypeDef HAL_BLE_ConnParamValid(const HAL_BLE_ConnParamTypeDef *p)
{
    if (p->MaxInterval < p->MinInterval) {
        return HAL_ERROR;
    }
    if (p->MinInterval < HAL_BLE_INTERVAL_MIN) {
        return HAL_ERROR;
    }
    if (p->MaxInterval > HAL_BLE_INTERVAL_MAX) {
        return HAL_ERROR;
    }
    if (p->Latency > HAL_BLE_LATENCY_MAX) {
        return HAL_ERROR;
    }
    if (p->Timeout < HAL_BLE_TIMEOUT_MIN) {
        return HAL_ERROR;
    }
    if (p->Timeout > HAL_BLE_TIMEOUT_MAX) {
        return HAL_ERROR;
    }

    /* 监督超时下限：(1 + Latency) * MaxInterval * 1.25ms * 2。
     * Timeout 单位 10ms，两边同除以 2500us 后得
     *   Timeout * 4 > (1 + Latency) * MaxInterval
     * 例：Interval=24(30ms)、Latency=0 → Timeout > 6，即至少 70ms。 */
    if (((uint32_t)p->Timeout * 4U) <=
        ((uint32_t)(p->Latency + 1U) * (uint32_t)p->MaxInterval)) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  下发广播数据 / 扫描响应 / 连接参数区间
 * @note   照厂商 Peripheral 例程的做法，这些 SetParameter 在
 *         GAPRole_PeripheralStartDevice 之前调用是允许的。
 */
static HAL_StatusTypeDef HAL_BLE_ApplyInit(HAL_BLE_HandleTypeDef *hble)
{
    const HAL_BLE_InitTypeDef *init = &hble->Init;

    if (GAPRole_SetParameter(GAPROLE_ADVERT_DATA, (uint16_t)init->AdvDataLen,
                             (void *)init->AdvData) != SUCCESS) {
        return HAL_ERROR;
    }

    if ((init->ScanRspData != NULL) && (init->ScanRspLen != 0U)) {
        if (GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, (uint16_t)init->ScanRspLen,
                                 (void *)init->ScanRspData) != SUCCESS) {
            return HAL_ERROR;
        }
    }

    /* 连接参数：全 0 表示沿用库默认；只要填了就必须合法 */
    if ((init->ConnParam.MinInterval != 0U) || (init->ConnParam.MaxInterval != 0U) ||
        (init->ConnParam.Latency != 0U) || (init->ConnParam.Timeout != 0U)) {
        if (HAL_BLE_ConnParamValid(&init->ConnParam) != HAL_OK) {
            return HAL_ERROR;
        }
        if (GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, (uint16_t)sizeof(uint16_t),
                                 (void *)&init->ConnParam.MinInterval) != SUCCESS) {
            return HAL_ERROR;
        }
        if (GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, (uint16_t)sizeof(uint16_t),
                                 (void *)&init->ConnParam.MaxInterval) != SUCCESS) {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

/* ===================== 对外 API ===================== */

/**
 * @brief  初始化 BLE 从机角色
 * @param  hble 句柄，调用前填好 Init（AdvData 必须有）
 * @retval HAL_OK 成功（角色在 TMOS 任务里异步启动）
 * @retval HAL_ERROR 句柄/参数非法，或数据下发失败
 * @note   必须在 HAL_STACK_Init() 成功之后调用。
 */
HAL_StatusTypeDef HAL_BLE_Init(HAL_BLE_HandleTypeDef *hble)
{
    uint8_t i;

    if (hble == NULL) {
        return HAL_ERROR;
    }
    if (hble->State != HAL_BLE_STATE_RESET) {
        return HAL_ERROR; /* 已初始化 */
    }

    if ((hble->Init.AdvData == NULL) || (hble->Init.AdvDataLen == 0U) ||
        (hble->Init.AdvDataLen > HAL_BLE_MAX_ADV_LEN)) {
        return HAL_ERROR;
    }
    if ((hble->Init.ScanRspData != NULL) && (hble->Init.ScanRspLen > HAL_BLE_MAX_SCAN_RSP_LEN)) {
        return HAL_ERROR;
    }

    s_pHble = hble;

    for (i = 0U; i < HAL_BLE_MAX_CONNECTIONS; i++) {
        HAL_BLE_ClearConn(hble, i);
    }
    hble->ConnCount = 0U;

    if (HAL_BLE_ApplyInit(hble) != HAL_OK) {
        s_pHble = NULL;
        return HAL_ERROR;
    }

    hble->TmosTaskId = TMOS_ProcessEventRegister(HAL_BLE_ProcessEvent);
    if (hble->TmosTaskId == INVALID_TASK_ID) {
        s_pHble = NULL;
        return HAL_ERROR;
    }

    /* 角色启动要在 TMOS 上下文做，这里只投递事件 */
    (void)tmos_set_event(hble->TmosTaskId, HAL_BLE_EVT_START);

    return HAL_OK;
}

/**
 * @brief  开始广播
 * @param  hble 句柄
 * @retval HAL_OK 请求已下发；状态由 HAL_BLE_AdvertisingCallback 确认
 */
HAL_StatusTypeDef HAL_BLE_StartAdvertising(HAL_BLE_HandleTypeDef *hble)
{
    uint8_t en = TRUE;

    if ((hble == NULL) || (hble->State == HAL_BLE_STATE_RESET)) {
        return HAL_ERROR;
    }

    if (GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, (uint16_t)sizeof(uint8_t), &en) != SUCCESS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  停止广播
 * @param  hble 句柄
 * @retval HAL_OK 请求已下发
 * @note   已连接时停广播不影响连接（对应库的 CONNECTED_ADV 状态回 CONNECTED）。
 */
HAL_StatusTypeDef HAL_BLE_StopAdvertising(HAL_BLE_HandleTypeDef *hble)
{
    uint8_t en = FALSE;

    if ((hble == NULL) || (hble->State == HAL_BLE_STATE_RESET)) {
        return HAL_ERROR;
    }

    if (GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, (uint16_t)sizeof(uint8_t), &en) != SUCCESS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  更新广播数据
 * @param  data 新数据
 * @param  len  长度 1~31
 * @retval HAL_OK 成功
 * @note   广播进行中也可以调用，下一个广播周期生效。
 */
HAL_StatusTypeDef HAL_BLE_SetAdvData(HAL_BLE_HandleTypeDef *hble, const uint8_t *data, uint8_t len)
{
    if ((hble == NULL) || (data == NULL) || (len == 0U) || (len > HAL_BLE_MAX_ADV_LEN)) {
        return HAL_ERROR;
    }

    if (GAPRole_SetParameter(GAPROLE_ADVERT_DATA, (uint16_t)len, (void *)data) != SUCCESS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  更新扫描响应数据
 * @param  data 新数据，传 NULL 表示清空
 * @param  len  长度 0~31
 * @retval HAL_OK 成功
 */
HAL_StatusTypeDef HAL_BLE_SetScanRspData(HAL_BLE_HandleTypeDef *hble, const uint8_t *data, uint8_t len)
{
    if ((hble == NULL) || (len > HAL_BLE_MAX_SCAN_RSP_LEN)) {
        return HAL_ERROR;
    }
    if ((data == NULL) && (len != 0U)) {
        return HAL_ERROR;
    }

    if (GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, (uint16_t)len, (void *)data) != SUCCESS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  断开指定连接
 * @param  hble      句柄
 * @param  connIndex 连接索引
 * @retval HAL_OK 请求已下发；HAL_ERROR 索引非法、该槽未连接或库拒绝
 * @note   断开结果通过 HAL_BLE_DisconnectCallback 通知。
 */
HAL_StatusTypeDef HAL_BLE_Disconnect(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex)
{
    HAL_BLE_ConnectionTypeDef *conn = HAL_BLE_GetActiveConn(hble, connIndex);

    if (conn == NULL) {
        return HAL_ERROR;
    }

    if (GAPRole_TerminateLink(conn->ConnHandle) != SUCCESS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  断开全部连接
 * @param  hble 句柄
 * @retval HAL_OK 全部请求已下发；HAL_ERROR 至少一个请求被库拒绝
 * @note   每个连接各自触发一次 HAL_BLE_DisconnectCallback。
 */
HAL_StatusTypeDef HAL_BLE_DisconnectAll(HAL_BLE_HandleTypeDef *hble)
{
    HAL_StatusTypeDef ret = HAL_OK;
    uint8_t           i;

    if (hble == NULL) {
        return HAL_ERROR;
    }

    for (i = 0U; i < HAL_BLE_MAX_CONNECTIONS; i++) {
        if (hble->Conn[i].Active != 0U) {
            if (GAPRole_TerminateLink(hble->Conn[i].ConnHandle) != SUCCESS) {
                ret = HAL_ERROR;
            }
        }
    }

    return ret;
}

/**
 * @brief  修改连接参数
 * @param  hble      句柄
 * @param  connIndex 目标连接索引；HAL_BLE_CONN_INDEX_NONE = 只改希望值、不起请求
 * @param  param     目标参数，四项都必须满足规范边界（见 HAL_BLE_ConnParamValid）
 * @retval HAL_OK    已下发；参数更新结果由 HAL_BLE_ConnParamUpdateCallback 通知
 * @retval HAL_ERROR 参数越界或组合不成立、索引非法、或库拒绝请求
 * @note   对端同意后由 HAL_BLE_ConnParamUpdateCallback 通知实际生效值。
 */
HAL_StatusTypeDef HAL_BLE_UpdateConnParam(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                          const HAL_BLE_ConnParamTypeDef *param)
{
    HAL_BLE_ConnectionTypeDef *conn = NULL;

    if ((hble == NULL) || (param == NULL)) {
        return HAL_ERROR;
    }
    if (HAL_BLE_ConnParamValid(param) != HAL_OK) {
        return HAL_ERROR;
    }

    if (GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, (uint16_t)sizeof(uint16_t),
                             (void *)&param->MinInterval) != SUCCESS) {
        return HAL_ERROR;
    }
    if (GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, (uint16_t)sizeof(uint16_t),
                             (void *)&param->MaxInterval) != SUCCESS) {
        return HAL_ERROR;
    }

    hble->Init.ConnParam = *param; /* 记住新希望值 */

    if (connIndex == HAL_BLE_CONN_INDEX_NONE) {
        return HAL_OK; /* 只改希望值，不动已建立的连接 */
    }

    conn = HAL_BLE_GetActiveConn(hble, connIndex);
    if (conn == NULL) {
        return HAL_ERROR;
    }

    if (GAPRole_PeripheralConnParamUpdateReq(conn->ConnHandle,
                                             param->MinInterval,
                                             param->MaxInterval,
                                             param->Latency,
                                             param->Timeout,
                                             hble->TmosTaskId) != SUCCESS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  向指定连接发送通知
 * @param  hble        句柄
 * @param  connIndex   连接索引
 * @param  valueHandle 特征值句柄（由 GATT 服务表决定）
 * @param  data        数据
 * @param  len         长度
 * @retval HAL_OK 已交给协议栈；HAL_ERROR 索引非法/该槽未连接/参数非法/缓冲申请失败
 * @note   须在 TMOS 任务上下文调用。GATT_bm_alloc 申请的内存，
 *         成功发出时由协议栈释放，失败时才需要自己 GATT_bm_free。
 */
HAL_StatusTypeDef HAL_BLE_Notify(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                 uint16_t valueHandle, const uint8_t *data, uint16_t len)
{
    HAL_BLE_ConnectionTypeDef *conn = HAL_BLE_GetActiveConn(hble, connIndex);
    attHandleValueNoti_t       noti;

    if ((conn == NULL) || (data == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }

    noti.handle = valueHandle;
    noti.len    = len;
    noti.pValue = (uint8_t *)GATT_bm_alloc(conn->ConnHandle, ATT_HANDLE_VALUE_NOTI,
                                           len, NULL, 0U);
    if (noti.pValue == NULL) {
        return HAL_ERROR;
    }

    (void)tmos_memcpy(noti.pValue, data, len);

    if (GATT_Notification(conn->ConnHandle, &noti, FALSE) != SUCCESS) {
        GATT_bm_free((gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI);
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  向指定连接发送指示（需对端确认）
 * @param  hble        句柄
 * @param  connIndex   连接索引
 * @param  valueHandle 特征值句柄
 * @param  data        数据
 * @param  len         长度
 * @retval HAL_OK 已交给协议栈
 * @note   须在 TMOS 任务上下文调用。同一连接同一时刻只允许一个未确认的指示，
 *         重复调用会被协议栈拒绝并返回 HAL_ERROR（已释放缓冲）。
 */
HAL_StatusTypeDef HAL_BLE_Indicate(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                   uint16_t valueHandle, const uint8_t *data, uint16_t len)
{
    HAL_BLE_ConnectionTypeDef *conn = HAL_BLE_GetActiveConn(hble, connIndex);
    attHandleValueInd_t        ind;

    if ((conn == NULL) || (data == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }

    ind.handle = valueHandle;
    ind.len    = len;
    ind.pValue = (uint8_t *)GATT_bm_alloc(conn->ConnHandle, ATT_HANDLE_VALUE_IND,
                                         len, NULL, 0U);
    if (ind.pValue == NULL) {
        return HAL_ERROR;
    }

    (void)tmos_memcpy(ind.pValue, data, len);

    if (GATT_Indication(conn->ConnHandle, &ind, FALSE, hble->TmosTaskId) != SUCCESS) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  读当前状态
 */
uint8_t HAL_BLE_GetState(const HAL_BLE_HandleTypeDef *hble)
{
    return (hble != NULL) ? hble->State : (uint8_t)HAL_BLE_STATE_RESET;
}

/**
 * @brief  读当前已连接数
 */
uint8_t HAL_BLE_GetConnCount(const HAL_BLE_HandleTypeDef *hble)
{
    return (hble != NULL) ? hble->ConnCount : 0U;
}

/**
 * @brief  取一条连接的参数
 * @retval 连接信息指针（指向句柄内部，断开后失效）；索引非法或未连接返回 NULL
 */
const HAL_BLE_ConnectionTypeDef *HAL_BLE_GetConnection(const HAL_BLE_HandleTypeDef *hble,
                                                       uint8_t connIndex)
{
    if ((hble == NULL) || (connIndex >= HAL_BLE_MAX_CONNECTIONS)) {
        return NULL;
    }
    if (hble->Conn[connIndex].Active == 0U) {
        return NULL;
    }

    return &hble->Conn[connIndex];
}

/* ===================== 弱回调默认实现 ===================== */

__weak void HAL_BLE_ConnectCallback(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                   const HAL_BLE_ConnectionTypeDef *conn)
{
    UNUSED(hble);
    UNUSED(connIndex);
    UNUSED(conn);
}

__weak void HAL_BLE_DisconnectCallback(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex, uint8_t reason)
{
    UNUSED(hble);
    UNUSED(connIndex);
    UNUSED(reason);
}

__weak void HAL_BLE_AdvertisingCallback(HAL_BLE_HandleTypeDef *hble)
{
    UNUSED(hble);
}

__weak void HAL_BLE_ConnParamUpdateCallback(HAL_BLE_HandleTypeDef *hble, uint8_t connIndex,
                                            const HAL_BLE_ConnParamTypeDef *param)
{
    UNUSED(hble);
    UNUSED(connIndex);
    UNUSED(param);
}

__weak void HAL_BLE_ErrorCallback(HAL_BLE_HandleTypeDef *hble, uint8_t code)
{
    UNUSED(hble);
    UNUSED(code);
}

#else /* CH58x_USE_BLE_LIB */

/* 未启用闭源库：整个文件不参与编译，留一个类型避免空翻译单元告警 */
typedef int ch58x_hal_ble_unused_translation_unit_t;

#endif /* CH58x_USE_BLE_LIB */
