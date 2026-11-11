#include "ch58x_hal_conf.h"

#ifdef CH58x_USE_BLE_LIB

#include "ch58x_hal_rf_phy.h"

/* 单实例句柄（库回调无上下文参数，经此带入） */
static RF_HandleTypeDef *s_pHRF = NULL;

/**
 * @brief  事务闸门等待：轮询 flag，带超时强置位防死锁
 * @param  flag 事务完成标志指针（&hrf->TxEndFlag / &hrf->RxEndFlag）
 * @note   超时 2000µs；78/39/19.5MHz 档为纯延时后强置位；在 RAM 中执行
 */
__HIGH_CODE
__attribute__((noinline)) static void HAL_RF_WaitFlag(__IO uint8_t *flag)
{
    uint32_t i       = 0;
    const uint32_t t = 2000; // us
#if (HAL_RCC_SYSCLK_HZ == 78000000)
    i = t * 13;
#elif (HAL_RCC_SYSCLK_HZ == 62400000)
    i = ((uint32_t)t * 78) / 5;
#elif (HAL_RCC_SYSCLK_HZ == 52000000)
    i = t * 13;
#elif (HAL_RCC_SYSCLK_HZ == 39000000)
    i = (t * 13) >> 1;
#elif (HAL_RCC_SYSCLK_HZ == 26000000)
    i = (t * 13) >> 1;
#elif (HAL_RCC_SYSCLK_HZ == 24000000)
    i = t * 6;
#elif (HAL_RCC_SYSCLK_HZ == 19500000)
    i = (t * 13) >> 2;
#elif (HAL_RCC_SYSCLK_HZ == 32000000)
    i = t << 3;
#elif (HAL_RCC_SYSCLK_HZ == 16000000)
    i = t << 2;
#elif (HAL_RCC_SYSCLK_HZ == 13000000)
    i = (t * 13) >> 2;
#elif (HAL_RCC_SYSCLK_HZ == 8000000)
    i = t << 1;
#elif (HAL_RCC_SYSCLK_HZ == 4000000)
    i = t;
#elif (HAL_RCC_SYSCLK_HZ == 2000000)
    i = t >> 1;
#elif (HAL_RCC_SYSCLK_HZ == 1000000)
    i = t >> 2;
#else
    i = t;
#endif

#if ((HAL_RCC_SYSCLK_HZ == 78000000) || \
     (HAL_RCC_SYSCLK_HZ == 39000000) || \
     (HAL_RCC_SYSCLK_HZ == 19500000))
    while (--i) {
        __nop();
    };
#else
    while (!*flag && i > 0) {
        i--;
        __nop();
    }
#endif
    *flag = 1;
}

/**
 * @brief  等待发送事务终点（模块内部闸门）
 * @param  hrf RF 句柄
 * @note   basic 等 0x01；auto 等 0x02/0x12；超时强置位防死锁
 */
__HIGH_CODE
static void HAL_RF_WaitTxEnd(RF_HandleTypeDef *hrf)
{
    if (hrf == NULL) {
        return;
    }
    HAL_RF_WaitFlag(&hrf->TxEndFlag);
}

/**
 * @brief  等待 ACK 发送完成（模块内部闸门）
 * @param  hrf RF 句柄
 * @note   auto 接收 0x04/0x14 置位；不能用于等待收包
 */
__HIGH_CODE
static void HAL_RF_WaitRxEnd(RF_HandleTypeDef *hrf)
{
    if (hrf == NULL) {
        return;
    }
    HAL_RF_WaitFlag(&hrf->RxEndFlag);
}

/**
 * @brief  接收数据解析 + 限幅拷贝（0x02/0x03 的 rsr==0 共用）
 * @param  hrf    RF 句柄
 * @param  pDst   目标缓冲（可 NULL=不拷贝）
 * @param  MaxLen 目标缓冲容量（拷贝 MIN(实际, 容量)）
 * @param  rxBuf  库接收缓冲（BIT7=0：[RSSI][len][data]；BIT7=1：[len][data]）
 * @note   实际长度记录于 hrf->RxXferSize（供回调 Len）；ISR 内调用
 */
__HIGH_CODE
static void HAL_RF_DeliverRxData(RF_HandleTypeDef *hrf, uint8_t *pDst, uint16_t MaxLen, uint8_t *rxBuf)
{
    uint16_t len;
    uint8_t *pData;
    uint16_t n;

    if (__HAL_RF_IS_NON_RSSI(hrf)) {
        len   = rxBuf[0];
        pData = rxBuf + 1;
    } else {
        hrf->LastRssi = (int8_t)rxBuf[0];
        len           = rxBuf[1];
        pData         = rxBuf + 2;
    }
    hrf->RxXferSize = len;

    if (pDst != NULL && MaxLen > 0) {
        n = (len < MaxLen) ? len : MaxLen;
        if (n > 0) {
            __MCPY(pDst, pData, pData + n); /* 硬件 mcpy 整块搬运 */
        }
    }
}

/**
 * @brief  库状态回调入口（ISR 上下文：只记账/限幅拷贝/置闸门/投事件）
 * @param  hrf   RF 句柄
 * @param  sta   库状态码（TX_MODE_* / RX_MODE_*）
 * @param  rsr   接收状态（0=正常，bit0 CRC 错、bit1 类型不匹配）
 * @param  rxBuf 库接收缓冲（仅回调期间有效，必须当场拷贝）
 */
__HIGH_CODE
void HAL_RF_IRQHandler(RF_HandleTypeDef *hrf, uint8_t sta, uint8_t rsr, uint8_t *rxBuf)
{
    if (hrf == NULL) {
        return;
    }

    switch (sta) {
        case TX_MODE_TX_FINISH: { /* 0x01：basic=事务完成；auto=发射完成 */
            if (__HAL_RF_IS_AUTO_MODE(hrf)) {
                hrf->State = HAL_RF_STATE_BUSY_TX_RX; /* 阶段事件：不置 TxEndFlag */
            } else {
                hrf->TxEndFlag = 1;
                hrf->State     = HAL_RF_STATE_READY;
            }
            (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_TX_FINISH);
        } break;

        case TX_MODE_RX_DATA: { /* 0x02：auto 对端回复（事务终点，rsr!=0 亦结束） */
            hrf->LastRsr = rsr;
            if (rsr == 0) {
                HAL_RF_DeliverRxData(hrf, hrf->pAckBuffPtr, hrf->AckBuffMaxLen, rxBuf);
                hrf->TxEndFlag = 1;
                hrf->State     = HAL_RF_STATE_READY;
                (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_TX_RX_DATA);
            } else {
                if (rsr & HAL_RF_RSR_CRC_ERR) {
                    hrf->ErrorCode |= HAL_RF_ERROR_RX_CRC;
                }
                if (rsr & HAL_RF_RSR_TYPE_ERR) {
                    hrf->ErrorCode |= HAL_RF_ERROR_RX_TYPE;
                }
                hrf->TxEndFlag = 1;
                hrf->State     = HAL_RF_STATE_READY;
                (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_ERROR);
            }
        } break;

        case TX_MODE_TX_FAIL: { /* 0x11：发射失败（含 TIMEOUT 别名） */
            hrf->ErrorCode |= HAL_RF_ERROR_TX_FAIL;
            hrf->TxEndFlag = 1;
            hrf->State     = HAL_RF_STATE_READY;
            (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_ERROR);
        } break;

        case TX_MODE_RX_TIMEOUT: { /* 0x12：auto 无回复（正常结束） */
            hrf->TxEndFlag = 1;
            hrf->State     = HAL_RF_STATE_READY;
            (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_TX_RX_TIMEOUT);
        } break;

        case TX_MODE_HOP_SHUT: { /* 0x22：跳频停机 */
            hrf->TxEndFlag = 1;
            hrf->State     = HAL_RF_STATE_READY;
            (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_HOP_SHUT);
        } break;

        case RX_MODE_RX_DATA: { /* 0x03：basic=事务完成；auto=随后硬件自动回 ACK */
            hrf->LastRsr = rsr;
            if (rsr == 0) {
                HAL_RF_DeliverRxData(hrf, hrf->pAppRxBuffPtr, hrf->RxBuffMaxLen, rxBuf);
                if (__HAL_RF_IS_AUTO_MODE(hrf)) {
                    /* auto：等 ACK 闸门（0x04/0x14 嵌套置位）；返回后不覆盖 State，仅超时兜底 */
                    hrf->State = HAL_RF_STATE_BUSY_TX_RX;
                    HAL_RF_WaitRxEnd(hrf);
                    if (hrf->State == HAL_RF_STATE_BUSY_TX_RX) {
                        hrf->State = HAL_RF_STATE_READY;
                    }
                    (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_RX_DATA);
                } else {
                    hrf->State = HAL_RF_STATE_READY;
                    (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_RX_DATA);
                }
            } else {
                /* rsr!=0：CRC 错 / 类型不匹配（不交付）；auto 类型不匹配是否自动重收待实机确认 */
                if (rsr & HAL_RF_RSR_CRC_ERR) {
                    hrf->ErrorCode |= HAL_RF_ERROR_RX_CRC;
                }
                if (rsr & HAL_RF_RSR_TYPE_ERR) {
                    hrf->ErrorCode |= HAL_RF_ERROR_RX_TYPE;
                }
                hrf->State = HAL_RF_STATE_READY;
                (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_ERROR);
            }
        } break;

        case RX_MODE_TX_FINISH: { /* 0x04：auto 的 ACK 发送完成（事务终点） */
            hrf->RxEndFlag = 1;
            hrf->State     = HAL_RF_STATE_READY;
            (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_RX_TX_FINISH);
        } break;

        case RX_MODE_TX_FAIL: { /* 0x14：ACK 发送失败（含 TIMEOUT 别名） */
            hrf->ErrorCode |= HAL_RF_ERROR_RX_TX_FAIL;
            hrf->RxEndFlag = 1;
            hrf->State     = HAL_RF_STATE_READY;
            (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_ERROR);
        } break;

        case RX_MODE_HOP_SHUT: { /* 0x24：跳频停机 */
            hrf->RxEndFlag = 1;
            hrf->State     = HAL_RF_STATE_READY;
            (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_HOP_SHUT);
        } break;

        default:
            break;
    }
}

/**
 * @brief  库状态回调 shim（签名即 pfnRFStatusCB_t）
 * @param  sta   库状态码
 * @param  rsr   接收状态
 * @param  rxBuf 库接收缓冲
 * @note   经单实例 s_pHRF 带入句柄；DeInit 后迟到回调直接丢弃
 */
__HIGH_CODE
static void HAL_RF_StatusCallback(uint8_t sta, uint8_t rsr, uint8_t *rxBuf)
{
    if (s_pHRF != NULL) { /* DeInit 后迟到回调：丢弃 */
        HAL_RF_IRQHandler(s_pHRF, sta, rsr, rxBuf);
    }
}

/* 弱回调默认实现 */
/**
 * @brief  发射完成回调（弱定义，应用可覆写）
 * @param  hrf RF 句柄
 * @note   0x01：basic=事务完成；auto=发射完成（阶段事件）
 */
__weak void HAL_RF_TxFinishCallback(RF_HandleTypeDef *hrf)
{
    (void)hrf;
}
/**
 * @brief  auto 收到对端回复回调（弱定义，应用可覆写）
 * @param  hrf   RF 句柄
 * @param  pData 回复数据（hrf->pAckBuffPtr，可为 NULL）
 * @param  Len   实际回复长度
 */
__weak void HAL_RF_TxRxDataCallback(RF_HandleTypeDef *hrf, uint8_t *pData, uint16_t Len)
{
    (void)hrf;
    (void)pData;
    (void)Len;
}
/**
 * @brief  auto 无回复回调（弱定义，应用可覆写）
 * @param  hrf RF 句柄
 * @note   0x12：正常结束（非错误）
 */
__weak void HAL_RF_TxRxTimeoutCallback(RF_HandleTypeDef *hrf)
{
    (void)hrf;
}
/**
 * @brief  有效接收回调（弱定义，应用可覆写）
 * @param  hrf   RF 句柄
 * @param  pData 接收数据（hrf->pAppRxBuffPtr，可为 NULL）
 * @param  Len   实际接收长度
 */
__weak void HAL_RF_RxDataCallback(RF_HandleTypeDef *hrf, uint8_t *pData, uint16_t Len)
{
    (void)hrf;
    (void)pData;
    (void)Len;
}
/**
 * @brief  auto 的 ACK 发送完成回调（弱定义，应用可覆写）
 * @param  hrf RF 句柄
 * @note   0x04：事务终点
 */
__weak void HAL_RF_RxTxFinishCallback(RF_HandleTypeDef *hrf)
{
    (void)hrf;
}
/**
 * @brief  错误回调（弱定义，应用可覆写）
 * @param  hrf RF 句柄
 * @note   0x11/0x14/rsr 错/启动失败；种类见 __HAL_RF_GET_ERROR
 */
__weak void HAL_RF_ErrorCallback(RF_HandleTypeDef *hrf)
{
    (void)hrf;
}
/**
 * @brief  跳频停机回调（弱定义，应用可覆写）
 * @param  hrf RF 句柄
 * @note   0x22/0x24
 */
__weak void HAL_RF_HopShutCallback(RF_HandleTypeDef *hrf)
{
    (void)hrf;
}

/**
 * @brief  TX_REQ 处理：执行发送（任务上下文）
 * @param  hrf RF 句柄
 * @note   auto 模式等待事务终点；启动失败置 START 错误并回调
 */
static void HAL_RF_TxStart(RF_HandleTypeDef *hrf)
{
    if (hrf->State != HAL_RF_STATE_BUSY_TX) {
        return; /* 残留 REQ（已被 Shut 取消）：忽略 */
    }

    hrf->TxEndFlag = 0;
    (void)RF_Shut();
    if (RF_Tx((uint8_t *)hrf->pTxBuffPtr, (uint8_t)hrf->TxXferSize,
              hrf->Init.pktTxType, hrf->Init.pktRxType) == SUCCESS) {
        HAL_RF_WaitTxEnd(hrf); /* 事务闸门（basic 等 0x01；auto 等 0x02/0x12） */
    } else {
        /* 启动失败：闸门置位 + 报错 */
        hrf->ErrorCode |= HAL_RF_ERROR_START;
        hrf->TxEndFlag = 1;
        hrf->State     = HAL_RF_STATE_READY;
        HAL_RF_ErrorCallback(hrf);
    }
}

/**
 * @brief  RX_REQ 处理：执行接收（任务上下文）
 * @param  hrf RF 句柄
 * @note   不等待收包（ACK 闸门在 0x03 ISR）；启动失败置 START 错误并回调
 */
static void HAL_RF_RxStart(RF_HandleTypeDef *hrf)
{
    if (hrf->State != HAL_RF_STATE_BUSY_RX) {
        return;
    }

    hrf->RxEndFlag = 0;
    (void)RF_Shut();
    if (RF_Rx((uint8_t *)hrf->pReplyBuffPtr, (uint8_t)hrf->ReplyXferSize,
              hrf->Init.pktRxType, hrf->Init.pktTxType) != SUCCESS) {
        hrf->ErrorCode |= HAL_RF_ERROR_START;
        hrf->RxEndFlag = 1;
        hrf->State     = HAL_RF_STATE_READY;
        HAL_RF_ErrorCallback(hrf);
    }
}

/**
 * @brief  模块任务：完成事件派发（固定码序）-> REQ 启动
 * @param  taskID TMOS 任务 ID
 * @param  events 待处理事件位
 * @return 未处理事件位（TMOS 约定）
 */
static tmosEvents HAL_RF_ProcessEvent(tmosTaskID taskID, tmosEvents events)
{
    RF_HandleTypeDef *hrf = s_pHRF;

    if (events & SYS_EVENT_MSG) { /* 模块不使用 TMOS 消息：收到即释放 */
        uint8_t *msgPtr = tmos_msg_receive(taskID);
        if (msgPtr != NULL) {
            (void)tmos_msg_deallocate(msgPtr);
        }
        events &= (tmosEvents)(~SYS_EVENT_MSG);
    }

    if (events & HAL_RF_EVT_ALL) {
        if (hrf == NULL || hrf->State == HAL_RF_STATE_RESET) {
            /* 未绑定/已 DeInit：丢弃全部 RF 事件位 */
            events &= (tmosEvents)(~HAL_RF_EVT_ALL);
            return events;
        }

        /* 完成事件固定码序：RX_DATA -> RX_TX_FINISH -> TX_FINISH -> TX_RX_DATA
         * -> TX_RX_TIMEOUT -> ERROR -> HOP_SHUT（保证先收数据再知 ACK 结果） */
        if (events & HAL_RF_EVT_RX_DATA) {
            uint8_t *pData     = hrf->pAppRxBuffPtr; /* 先取值再清挂起 */
            uint16_t len       = hrf->RxXferSize;
            hrf->pAppRxBuffPtr = NULL;
            hrf->RxBuffMaxLen  = 0;
            HAL_RF_RxDataCallback(hrf, pData, len);
            events &= (tmosEvents)(~HAL_RF_EVT_RX_DATA);
        }
        if (events & HAL_RF_EVT_RX_TX_FINISH) {
            HAL_RF_RxTxFinishCallback(hrf);
            events &= (tmosEvents)(~HAL_RF_EVT_RX_TX_FINISH);
        }
        if (events & HAL_RF_EVT_TX_FINISH) {
            HAL_RF_TxFinishCallback(hrf);
            events &= (tmosEvents)(~HAL_RF_EVT_TX_FINISH);
        }
        if (events & HAL_RF_EVT_TX_RX_DATA) {
            uint8_t *pData     = hrf->pAckBuffPtr;
            uint16_t len       = hrf->RxXferSize;
            hrf->pAckBuffPtr   = NULL;
            hrf->AckBuffMaxLen = 0;
            HAL_RF_TxRxDataCallback(hrf, pData, len);
            events &= (tmosEvents)(~HAL_RF_EVT_TX_RX_DATA);
        }
        if (events & HAL_RF_EVT_TX_RX_TIMEOUT) {
            HAL_RF_TxRxTimeoutCallback(hrf);
            events &= (tmosEvents)(~HAL_RF_EVT_TX_RX_TIMEOUT);
        }
        if (events & HAL_RF_EVT_ERROR) {
            HAL_RF_ErrorCallback(hrf);
            events &= (tmosEvents)(~HAL_RF_EVT_ERROR);
        }
        if (events & HAL_RF_EVT_HOP_SHUT) {
            HAL_RF_HopShutCallback(hrf);
            events &= (tmosEvents)(~HAL_RF_EVT_HOP_SHUT);
        }

        /* 启动请求（完成事件之后） */
        if (events & HAL_RF_EVT_TX_REQ) {
            events &= (tmosEvents)(~HAL_RF_EVT_TX_REQ);
            HAL_RF_TxStart(hrf);
        }
        if (events & HAL_RF_EVT_RX_REQ) {
            events &= (tmosEvents)(~HAL_RF_EVT_RX_REQ);
            HAL_RF_RxStart(hrf);
        }
    }

    return events;
}

/**
 * @brief  HAL_RF_Init 用：由 hrf->Init 组装 hrf->Config（rfStatusCB/Resv 由模块维护）
 * @param  hrf RF 句柄
 */
static void HAL_RF_BuildConfig(RF_HandleTypeDef *hrf)
{
    hrf->Config.LLEMode       = hrf->Init.LLEMode;
    hrf->Config.Channel       = hrf->Init.Channel;
    hrf->Config.Frequency     = hrf->Init.Frequency;
    hrf->Config.accessAddress = hrf->Init.accessAddress;
    hrf->Config.CRCInit       = hrf->Init.CRCInit;
    hrf->Config.rfStatusCB    = HAL_RF_StatusCallback;
    hrf->Config.ChannelMap    = hrf->Init.ChannelMap;
    hrf->Config.Resv          = 0;
    hrf->Config.HeartPeriod   = hrf->Init.HeartPeriod;
    hrf->Config.HopPeriod     = hrf->Init.HopPeriod;
    hrf->Config.HopIndex      = hrf->Init.HopIndex;
    hrf->Config.RxMaxlen      = hrf->Init.RxMaxlen;
    hrf->Config.TxMaxlen      = hrf->Init.TxMaxlen;
}

/**
 * @brief  检查接入地址是否满足 PHY 层要求
 * @param  aa 32 位 PHY 接入地址
 * @retval 1 合法；0 非法
 * @note   厂商 EVT（BLE/RF_PHY/APP/RF_PHY.c）对 accessAddress 的注释：
 *           「禁止使用 0x55555555 以及 0xAAAAAAAA
 *             ( 建议不超过 24 次位反转，且不超过连续的 6 个 0 或 1 )」
 *         三条逐项对应：
 *           1. 0x55555555 / 0xAAAAAAAA 直流不平衡，接收端无法稳定定界；
 *           2. 相邻位跳变超过 24 次，同步头相关性反而下降；
 *           3. 连续 6 个以上相同位，时钟恢复容易失锁。
 *         这与 BLE Core Spec 对 Access Address 的要求一致。
 *         例程用的合法值：0x71764129、0x71762345。
 */
uint8_t HAL_RF_IsValidAccessAddress(uint32_t aa)
{
    uint32_t transitions = 0U;
    uint32_t run         = 1U;
    uint32_t i;

    if ((aa == 0x55555555U) || (aa == 0xAAAAAAAAU)) {
        return 0U;
    }

    for (i = 1U; i < 32U; i++) {
        if (((aa >> i) & 1U) != ((aa >> (i - 1U)) & 1U)) {
            transitions++;
            run = 1U;
        } else {
            run++;
            if (run > HAL_RF_AA_MAX_RUN) {
                return 0U;
            }
        }
    }

    return (transitions <= HAL_RF_AA_MAX_TRANSITIONS) ? 1U : 0U;
}

/* API */
/**
 * @brief  初始化 RF 模块（注册模块任务、组装 rfConfig_t 并调用 RF 库）
 * @param  hrf RF 句柄（Init 字段须已配置）
 * @retval HAL_OK    初始化成功
 * @retval HAL_ERROR 参数/状态错误，或 RF_RoleInit/RF_Config 失败（已回滚）
 * @note   须在 TMOS 任务上下文调用（TMOS 须已启动）；已初始化需先 DeInit
 */
HAL_StatusTypeDef HAL_RF_Init(RF_HandleTypeDef *hrf)
{
    if (hrf == NULL) {
        return HAL_ERROR;
    }
    if (hrf->State != HAL_RF_STATE_RESET) {
        return HAL_ERROR; /* 已初始化（需先 DeInit） */
    }

    /* 接入地址不合法就拒绝：此时还没注册任务，不必回滚 */
    if (HAL_RF_IsValidAccessAddress(hrf->Init.accessAddress) == 0U) {
        return HAL_ERROR;
    }

    /* 注册模块任务（TMOS 须已启动） */
    hrf->TmosTaskId = TMOS_ProcessEventRegister(HAL_RF_ProcessEvent);
    if (hrf->TmosTaskId == INVALID_TASK_ID) {
        return HAL_ERROR;
    }
    s_pHRF = hrf;

    /* 默认值（EndFlag=1：无事务挂起） */
    hrf->State         = HAL_RF_STATE_READY;
    hrf->ErrorCode     = HAL_RF_ERROR_NONE;
    hrf->LastRsr       = 0;
    hrf->LastRssi      = 0;
    hrf->TxEndFlag     = 1;
    hrf->RxEndFlag     = 1;
    hrf->RxXferSize    = 0;
    hrf->pTxBuffPtr    = NULL;
    hrf->TxXferSize    = 0;
    hrf->pAppRxBuffPtr = NULL;
    hrf->RxBuffMaxLen  = 0;
    hrf->pAckBuffPtr   = NULL;
    hrf->AckBuffMaxLen = 0;
    hrf->DummyReply    = 0;
    hrf->pReplyBuffPtr = &hrf->DummyReply;
    hrf->ReplyXferSize = 0;

    /* 组装配置并下发 */
    HAL_RF_BuildConfig(hrf);
    if ((RF_RoleInit() != SUCCESS) || (RF_Config(&hrf->Config) != SUCCESS)) {
        s_pHRF     = NULL; /* 回滚：丢弃全部 RF 事件 */
        hrf->State = HAL_RF_STATE_RESET;
        return HAL_ERROR;
    }
    return HAL_OK;
}

/**
 * @brief  反初始化：停机、清事件、解绑任务
 * @param  hrf RF 句柄
 * @retval HAL_OK    成功
 * @retval HAL_ERROR hrf 为 NULL 或未初始化
 * @note   调用后如需使用须重新 HAL_RF_Init
 */
HAL_StatusTypeDef HAL_RF_DeInit(RF_HandleTypeDef *hrf)
{
    if (hrf == NULL || hrf->State == HAL_RF_STATE_RESET) {
        return HAL_ERROR;
    }

    (void)RF_Shut();
    (void)tmos_clear_event(hrf->TmosTaskId, HAL_RF_EVT_ALL); /* 丢弃未派发事件 */
    s_pHRF     = NULL;
    hrf->State = HAL_RF_STATE_RESET;
    return HAL_OK;
}

/**
 * @brief  手动停机/中止空中收发并回 READY
 * @param  hrf RF 句柄
 * @retval HAL_OK    成功
 * @retval HAL_ERROR hrf 为 NULL 或未初始化
 * @note   不产生完成回调；READY 下幂等
 */
HAL_StatusTypeDef HAL_RF_Shut(RF_HandleTypeDef *hrf)
{
    if (hrf == NULL || hrf->State == HAL_RF_STATE_RESET) {
        return HAL_ERROR;
    }

    (void)RF_Shut(); /* 停空中收发（不产生完成回调） */

    hrf->TxEndFlag = 1; /* 无事务挂起 */
    hrf->RxEndFlag = 1;
    hrf->State     = HAL_RF_STATE_READY;
    return HAL_OK;
}

/**
 * @brief  下发配置（pConfig 全量覆盖 hrf->Config 后调 RF_Config）
 * @param  hrf     RF 句柄
 * @param  pConfig 新配置（建议先用 __HAL_RF_GET_CONFIG 复制副本、在副本上修改再传入）
 * @retval HAL_OK    下发成功
 * @retval HAL_ERROR hrf/pConfig 为 NULL、未初始化，或 RF_Config 失败（置 ErrorCode.START）
 * @retval HAL_BUSY  当前非 READY（请先等事务结束或 HAL_RF_Shut）
 * @note   须在 TMOS 任务上下文调用；RF_RoleInit 为一次性初始化（HAL_RF_Init 已调用）。
 *         rfStatusCB 可自定义（NULL=回退模块 shim）；自定义回调运行于 ISR 上下文，
 *         必须转调 HAL_RF_IRQHandler(hrf, sta, rsr, rxBuf) 以维持模块状态机/事件，
 *         且不得直接调用 RF 库 API，必须 __HIGH_CODE。Resv 由模块维护（强制 0）。
 */
HAL_StatusTypeDef HAL_RF_SetConfig(RF_HandleTypeDef *hrf, const rfConfig_t *pConfig)
{
    if (hrf == NULL || pConfig == NULL || hrf->State == HAL_RF_STATE_RESET) {
        return HAL_ERROR;
    }
    if (hrf->State != HAL_RF_STATE_READY) {
        return HAL_BUSY;
    }

    /* 接入地址同样要过 PHY 约束，否则 HAL_RF_Init 里的校验可以被这里绕过 */
    if (HAL_RF_IsValidAccessAddress(pConfig->accessAddress) == 0U) {
        return HAL_ERROR;
    }

    hrf->Config = *pConfig;
    if (hrf->Config.rfStatusCB == NULL) {
        hrf->Config.rfStatusCB = HAL_RF_StatusCallback; /* NULL：回退模块 shim */
    }
    hrf->Config.Resv = 0;

    if (RF_Config(&hrf->Config) != SUCCESS) {
        hrf->ErrorCode |= HAL_RF_ERROR_START;
        return HAL_ERROR;
    }
    return HAL_OK;
}

/**
 * @brief  启动一次发送事务（异步）
 * @param  hrf       RF 句柄
 * @param  pTxData   待发数据（不可 NULL；Len 可为 0）
 * @param  Len       发送长度（须 <= Init.TxMaxlen）
 * @param  pAckBuf   auto 对端回复缓冲（可 NULL=不拷贝）
 * @param  AckMaxLen 回复缓冲容量（拷贝限幅）
 * @retval HAL_OK    已挂起参数并投递 TX 请求
 * @retval HAL_ERROR 参数/状态错误（无副作用）
 * @retval HAL_BUSY  当前非 READY
 * @note   须在 TMOS 任务上下文调用；结果经回调通知
 */
HAL_StatusTypeDef HAL_RF_Transmit_IT(RF_HandleTypeDef *hrf, const uint8_t *pTxData, uint16_t Len,
                                     uint8_t *pAckBuf, uint16_t AckMaxLen)
{
    if (hrf == NULL) {
        return HAL_ERROR;
    }
    if (pTxData == NULL || Len > hrf->Config.TxMaxlen) {
        return HAL_ERROR; /* 校验失败无副作用 */
    }
    if (hrf->State == HAL_RF_STATE_RESET) {
        return HAL_ERROR;
    }
    if (hrf->State != HAL_RF_STATE_READY) {
        return HAL_BUSY;
    }

    hrf->ErrorCode     = HAL_RF_ERROR_NONE;
    hrf->pTxBuffPtr    = pTxData;
    hrf->TxXferSize    = Len;
    hrf->pAckBuffPtr   = pAckBuf; /* 可 NULL=不拷贝（仅 auto 用） */
    hrf->AckBuffMaxLen = AckMaxLen;
    hrf->State         = HAL_RF_STATE_BUSY_TX;
    (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_TX_REQ);
    return HAL_OK;
}

/**
 * @brief  启动接收窗口（异步）
 * @param  hrf    RF 句柄
 * @param  pRxBuf 接收缓冲（可 NULL=不拷贝）
 * @param  MaxLen 缓冲容量（拷贝限幅）
 * @retval HAL_OK / HAL_ERROR / HAL_BUSY
 * @note   须在 TMOS 任务上下文调用；auto 模式强制 dummy 回复
 */
HAL_StatusTypeDef HAL_RF_Receive_IT(RF_HandleTypeDef *hrf, uint8_t *pRxBuf, uint16_t MaxLen)
{
    if (hrf == NULL) {
        return HAL_ERROR;
    }
    if (hrf->State == HAL_RF_STATE_RESET) {
        return HAL_ERROR;
    }
    if (hrf->State != HAL_RF_STATE_READY) {
        return HAL_BUSY;
    }

    /* 普通接收：auto 模式强制 dummy 回复，防误发 */
    if (__HAL_RF_IS_AUTO_MODE(hrf)) {
        hrf->pReplyBuffPtr = &hrf->DummyReply;
        hrf->ReplyXferSize = 0;
    }

    hrf->ErrorCode     = HAL_RF_ERROR_NONE;
    hrf->pAppRxBuffPtr = pRxBuf; /* 可 NULL=不拷贝 */
    hrf->RxBuffMaxLen  = MaxLen;
    hrf->State         = HAL_RF_STATE_BUSY_RX;
    (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_RX_REQ);
    return HAL_OK;
}

/**
 * @brief  启动接收窗口并预登记自动应答数据（异步）
 * @param  hrf        RF 句柄
 * @param  pRxBuf     接收缓冲（可 NULL=不拷贝）
 * @param  MaxLen     缓冲容量（拷贝限幅）
 * @param  pReplyData 应答数据（可 NULL=回退 dummy/0）
 * @param  ReplyLen   应答长度（须 <= Init.TxMaxlen）
 * @retval HAL_OK / HAL_ERROR / HAL_BUSY
 * @note   须在 TMOS 任务上下文调用；basic 模式下应答数据不被库使用
 */
HAL_StatusTypeDef HAL_RF_ReceiveAutoReply_IT(RF_HandleTypeDef *hrf, uint8_t *pRxBuf, uint16_t MaxLen,
                                             const uint8_t *pReplyData, uint16_t ReplyLen)
{
    if (hrf == NULL) {
        return HAL_ERROR;
    }
    if (pReplyData != NULL && ReplyLen > hrf->Config.TxMaxlen) {
        return HAL_ERROR; /* 回复数据经 RF_Rx 发出，属“发送”上限 */
    }
    if (hrf->State == HAL_RF_STATE_RESET) {
        return HAL_ERROR;
    }
    if (hrf->State != HAL_RF_STATE_READY) {
        return HAL_BUSY;
    }

    if (pReplyData != NULL) {
        hrf->pReplyBuffPtr = pReplyData; /* basic 模式忽略回复（等价普通接收） */
        hrf->ReplyXferSize = ReplyLen;
    } else {
        hrf->pReplyBuffPtr = &hrf->DummyReply; /* 回退 dummy/0 */
        hrf->ReplyXferSize = 0;
    }

    hrf->ErrorCode     = HAL_RF_ERROR_NONE;
    hrf->pAppRxBuffPtr = pRxBuf;
    hrf->RxBuffMaxLen  = MaxLen;
    hrf->State         = HAL_RF_STATE_BUSY_RX;
    (void)tmos_set_event(hrf->TmosTaskId, HAL_RF_EVT_RX_REQ);
    return HAL_OK;
}

#else /* CH58x_USE_BLE_LIB */

/* 未启用闭源库：整个文件不参与编译，留一个类型避免空翻译单元告警 */
typedef int ch58x_hal_rf_phy_unused_translation_unit_t;

#endif /* CH58x_USE_BLE_LIB */