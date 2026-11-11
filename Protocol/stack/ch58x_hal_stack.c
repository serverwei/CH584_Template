#include "ch58x_hal_conf.h"

#ifdef CH58x_USE_BLE_LIB

#include "ch58x_hal_stack.h"

#include "ch58x_hal.h"      /* HAL_ADC_ReadTempRaw / HAL_RCC_CalibrateLSI / HAL_FLASH_GetMACAddress */
#include <string.h>         /* strlen */

/* 定义在 Drivers/CH58x_HAL_Driver/src/ch58x_hal.c。
 * ble_task_scheduler.S 里的 LLE_IRQHandler 汇编桩用 .extern 引用它，
 * 取值必须是库里 LLE_IRQLibHandler 的地址；留着 0 又开了 BLEL 中断就会跳到 0。
 * 定义放 HAL 驱动层是为了让「只留 Drivers/CH58x_HAL_Driver」的工程也能链接通过。
 */
extern uint32_t g_LLE_IRQLibHandlerLocation;

/* ===================== 内部常量 ===================== */

/* 本模块的 TMOS 事件位。SYS_EVENT_MSG(0x8000) 由库占用，不能自用。 */
#define HAL_STACK_EVT_CALIB     0x0001U

/* ===================== 内部状态 ===================== */

/* 协议栈堆。Init.MemAddr 为 0 时用这块；库按 uint32_t 访问，必须 4 字节对齐。 */
__attribute__((aligned(4))) static uint32_t s_memBuf[HAL_STACK_MEM_LEN_DEFAULT / 4U];

/* TMOS 回调拿不到句柄，用文件级指针回指；HAL_STACK_DeInit 会清空 */
static HAL_STACK_HandleTypeDef *s_hstack;

/* ===================== 内部函数 ===================== */

/**
 * @brief  TMOS 时基回调：把 RTC 中断挂起，给库一个软件触发点
 * @note   对应厂商 SYS_SetPendingIRQ()。必须放 RAM，会被 TMOS 从任意上下文调用。
 */
__HIGH_CODE
static void HAL_STACK_PendRtcIrq(void)
{
    PFIC_SetPendingIRQ(RTC_IRQn);
}

/**
 * @brief  本模块的 TMOS 任务
 * @param  taskID 任务号
 * @param  events 待处理事件位
 * @retval 未处理的事件位
 * @note   只负责周期校准；BLE 本身的调度由库内部的任务完成。
 */
static tmosEvents HAL_STACK_ProcessEvent(tmosTaskID taskID, tmosEvents events)
{
    if ((events & SYS_EVENT_MSG) != 0U)
    {
        /* 本模块不收 TMOS 消息：收到即释放，避免协议栈堆泄漏 */
        uint8_t *msg = tmos_msg_receive(taskID);

        if (msg != NULL)
        {
            (void)tmos_msg_deallocate(msg);
        }
        events = (tmosEvents)(events ^ SYS_EVENT_MSG);
    }

    if ((events & HAL_STACK_EVT_CALIB) != 0U)
    {
        /* 射频寄存器重校准。库注释：会关闭 RF 并改变 RF 相关寄存器，
         * 所以只有在 BLE 的连接事件间隙里做才安全；RF-PHY 场景不能开这个任务。 */
        BLE_RegInit();

#if (HAL_RCC_OSC32K_SOURCE == 1)
        /* 只有内部 32K RC 才需要校准；外部 32.768kHz 晶振不参与这条调谐链 */
        HAL_RCC_CalibrateLSI();
#endif

        if (s_hstack != NULL)
        {
            (void)tmos_start_task(taskID, HAL_STACK_EVT_CALIB,
                                  MS1_TO_SYSTEM_TIME(s_hstack->Init.CalibPeriodMs));
        }

        events = (tmosEvents)(events ^ HAL_STACK_EVT_CALIB);
    }

    return events;
}

/**
 * @brief  把 Init 里的 0 值替换成默认值，并算出派生字段
 * @param  init 待归一化的参数，就地修改
 * @retval HAL_OK 成功；HAL_ERROR 堆参数非法或角色数量超出 ConnectNumber 的两位字段
 * @note   归一化后 Init 里保存的就是实际生效值，便于运行期查询。
 */
static HAL_StatusTypeDef HAL_STACK_NormalizeInit(HAL_STACK_InitTypeDef *init)
{
    if (init->MemAddr == 0U)
    {
        init->MemAddr = (uint32_t)s_memBuf;
    }
    if (init->MemLen == 0U)
    {
        init->MemLen = (uint16_t)HAL_STACK_MEM_LEN_DEFAULT;
    }
    if (init->BufMaxLen == 0U)
    {
        init->BufMaxLen = (uint16_t)HAL_STACK_BUF_MAX_LEN;
    }
    if (init->BufNumber == 0U)
    {
        init->BufNumber = (uint8_t)HAL_STACK_BUF_NUMBER;
    }
    if (init->TxNumEvent == 0U)
    {
        init->TxNumEvent = (uint8_t)HAL_STACK_TX_NUM_EVENT;
    }
    if (init->TxPower == 0U)
    {
        init->TxPower = (uint8_t)LL_TX_PWR_0_DBM;
    }
    if (init->CalibPeriodMs == 0U)
    {
        init->CalibPeriodMs = HAL_STACK_CALIB_PERIOD_MS;
    }

    /* 角色数量：两个都为 0 才算「没指定」，走厂商默认 1 从 + 3 主。
     * 只填一个表示只做那一个角色（另一个保持 0）。 */
    if ((init->PeripheralNumber == 0U) && (init->CentralNumber == 0U))
    {
        init->PeripheralNumber = (uint8_t)HAL_STACK_PERIPHERAL_DEFAULT;
        init->CentralNumber    = (uint8_t)HAL_STACK_CENTRAL_DEFAULT;
    }

    /* ConnectNumber 的 bit0~1 是从机数、bit2 起是主机数，字段容量只有这么多 */
    if ((init->PeripheralNumber > 3U) || (init->CentralNumber > 63U))
    {
        return HAL_ERROR;
    }

    if (init->MemLen < HAL_STACK_MEM_LEN_MIN)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/* ===================== 对外 API ===================== */

/**
 * @brief  初始化 libCH58xBLE.a 底座
 * @param  hstack 句柄，调用前填好 Init（可全零走默认）
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空、参数非法、库版本不匹配或 BLE_LibInit 失败
 * @note   必须在 HAL_Init() 之后调用：它依赖 SysTick 已配成自由运行计数器。
 *         必须在任何 RF_* / TMOS_* / GAPRole_* 之前调用。
 */
HAL_StatusTypeDef HAL_STACK_Init(HAL_STACK_HandleTypeDef *hstack)
{
    bleConfig_t cfg;
    uint8_t     i;
    
    if (hstack == NULL)
    {
        return HAL_ERROR;
    }

    hstack->State = HAL_STACK_STATE_RESET;

    /* 头文件与 .a 版本不一致时会随机跑飞，必须挡在 LibInit 之前 */
    if (tmos_memcmp(VER_LIB, VER_FILE, (uint32_t)strlen(VER_FILE)) == FALSE)
    {
        return HAL_ERROR;
    }

    if (HAL_STACK_NormalizeInit(&hstack->Init) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 接管 SysTick：BLE/RF 要求它是自由运行的 32 位计数器且不产生中断，
     * 否则每 1ms 唤醒一次，休眠电流直接崩。HAL_Init() 已按 1ms 节拍配过，这里覆盖。
     * 主频变更后 HAL_InitTick 会保持自由运行模式，不会退回中断节拍。
     * 排除 Protocol/ 的工程没人调这个函数，自然保持 1ms 节拍。 */
    HAL_UseFreeRunningTick();

    /* LLE 中断桩靠它间接跳到库里的处理函数，必须在 BLE_LibInit 之前填好 */
    g_LLE_IRQLibHandlerLocation = (uint32_t)LLE_IRQLibHandler;

    tmos_memset(&cfg, 0, sizeof(cfg));

    cfg.MEMAddr         = hstack->Init.MemAddr;
    cfg.MEMLen          = hstack->Init.MemLen;
    cfg.BufMaxLen       = hstack->Init.BufMaxLen;
    cfg.BufNumber       = hstack->Init.BufNumber;
    cfg.RxNumEvent      = hstack->Init.BufNumber;    /* 厂商默认：接收缓存与发包缓存同数 */
    cfg.TxNumEvent      = hstack->Init.TxNumEvent;
    cfg.TxPower         = hstack->Init.TxPower;
    cfg.PeripheralNumber = hstack->Init.PeripheralNumber;
    cfg.CentralNumber    = hstack->Init.CentralNumber;
    cfg.ConnectNumber    = (uint8_t)((hstack->Init.PeripheralNumber & 0x03U) |
                                     (uint8_t)(hstack->Init.CentralNumber << 2));

    /* 温漂校准：不提供开关。RF 频偏全靠它拉回来，私有协议没有连接事件兜底。
     * HAL_ADC_ReadTempRaw 内部会保存/恢复 TKEY/ADC 五个寄存器，可在任意上下文调用。 */
    cfg.tsCB = HAL_ADC_ReadTempRaw;

#if (HAL_RCC_OSC32K_SOURCE == 1)
    /* 校准对象是内部 32K RC；外部 32.768kHz 晶振模式下它不参与定时，注册了也是白耗
     * 10ms 且全程关中断。判据与 HAL_RCC_Config32KSource() 的 #if 完全一致。 */
    cfg.rcCB = HAL_RCC_CalibrateLSI;
#endif

    cfg.staCB = HAL_STACK_StatusCallback;

    /* 绑定信息存储：三项都给了才启用，否则库自己就不会保存绑定信息 */
    if ((hstack->Init.SNVAddr != 0U) &&
        (hstack->Init.ReadFlashCB != NULL) &&
        (hstack->Init.WriteFlashCB != NULL))
    {
        cfg.SNVAddr      = hstack->Init.SNVAddr;
        cfg.SNVBlock     = (hstack->Init.SNVBlock != 0U) ? hstack->Init.SNVBlock
                                                         : (uint16_t)HAL_STACK_SNV_BLOCK_DEFAULT;
        cfg.SNVNum       = (hstack->Init.SNVNum != 0U) ? hstack->Init.SNVNum
                                                       : (uint8_t)HAL_STACK_SNV_NUM_DEFAULT;
        cfg.readFlashCB  = hstack->Init.ReadFlashCB;
        cfg.writeFlashCB = hstack->Init.WriteFlashCB;
    }

    if (hstack->Init.MacAddr != NULL)
    {
        for (i = 0U; i < 6U; i++)
        {
            cfg.MacAddr[i] = hstack->Init.MacAddr[i];
        }
    }
    else
    {
        /* 初始化为 0：HAL_FLASH_GetMACAddress 失败时不会写缓冲区，
         * 不初始化就会把栈上的随机值当成 MAC 用 */
        uint8_t mac[HAL_FLASH_MAC_SIZE] = {0U};

        (void)HAL_FLASH_GetMACAddress(mac);
        for (i = 0U; i < HAL_FLASH_MAC_SIZE; i++)
        {
            cfg.MacAddr[i] = mac[i];
        }
    }

    /* 睡眠：idleCB 必须在 BLE_LibInit 之前填好，库初始化后不再读这个字段。
     * 睡觉得有 RTC 计数器和 RTC 唤醒源，缺一不可。 */
    if (hstack->Init.SleepEnable != DISABLE)
    {
        if (HAL_RTC_Init() != HAL_OK)
        {
            return HAL_ERROR;
        }

        HAL_PWR_EnableRtcWakeUp();

        cfg.idleCB = HAL_PWR_EnterSleepUntil;
    }

    /* 库会在这里接管 VTF 中断 2/3 号，并初始化 TMOS */
    if (BLE_LibInit(&cfg) != SUCCESS)
    {
        return HAL_ERROR;
    }

    /* 告诉 TMOS 时基在哪、多快、多准。不调这个，tmos_start_task 的定时
     * 与连接事件锚点全是错的 —— 表现为「能连上但随机掉线」，极难定位。
     * ClockAccuracy 按 32K 源给：外部晶振典型 50ppm，内部 RC 按 1000ppm 保守估。 */
    {
        bleClockConfig_t clk;

        tmos_memset(&clk, 0, sizeof(clk));
        clk.ClockAccuracy  = (HAL_RCC_OSC32K_SOURCE == 0) ? 50U : 1000U;
        clk.ClockFrequency = (uint16_t)HAL_RCC_OSC32K_FREQ;
        clk.ClockMaxCount  = HAL_RTC_MAX_COUNT;
        clk.getClockValue  = HAL_RTC_GetCounter;   /* 已是 __HIGH_CODE + 双读去抖 */
        clk.SetPendingIRQ  = HAL_STACK_PendRtcIrq;

        if (TMOS_TimerInit(&clk) != SUCCESS)
        {
            return HAL_ERROR;
        }
    }

    s_hstack           = hstack;
    hstack->TmosTaskId = TMOS_ProcessEventRegister(HAL_STACK_ProcessEvent);

    if (hstack->TmosTaskId != INVALID_TASK_ID)
    {
        if (hstack->Init.CalibPeriodMs != HAL_STACK_CALIB_OFF)
        {
            (void)tmos_start_task(hstack->TmosTaskId, HAL_STACK_EVT_CALIB,
                                  MS1_TO_SYSTEM_TIME(HAL_STACK_CALIB_FIRST_MS));
        }
    }

    hstack->State = HAL_STACK_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  注销校准任务
 * @param  hstack 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空
 * @note   BLE_LibInit 一旦执行就无法撤销，所以这里只是停掉本模块的周期任务，
 *         协议栈堆、TMOS、射频中断都保持原状。真正的「关掉」靠排除文件。
 */
HAL_StatusTypeDef HAL_STACK_DeInit(HAL_STACK_HandleTypeDef *hstack)
{
    if (hstack == NULL)
    {
        return HAL_ERROR;
    }

    if (hstack->State == HAL_STACK_STATE_READY)
    {
        if ((hstack->TmosTaskId != INVALID_TASK_ID) &&
            (hstack->Init.CalibPeriodMs != HAL_STACK_CALIB_OFF))
        {
            (void)tmos_clear_event(hstack->TmosTaskId, HAL_STACK_EVT_CALIB);
        }
    }

    s_hstack      = NULL;
    hstack->State = HAL_STACK_STATE_RESET;

    return HAL_OK;
}

/**
 * @brief  跑一次协议栈调度
 * @note   TMOS_SystemProcess 会一直处理到没有待办事件才返回，所以应用自己的轮询
 *         代码放在它后面就相当于跑在空闲窗口里。放 RAM 执行，与厂商 Main_Circulation 一致。
 */
__HIGH_CODE
__attribute__((noinline))
void HAL_STACK_Process(void)
{
    TMOS_SystemProcess();
}

/**
 * @brief  库内部状态错误回调，默认什么都不做
 * @param  code   库定义的错误码
 * @param  status 附加状态
 * @note   可能在中断上下文被调用，内部不要做耗时操作或调用阻塞 API。
 */
__weak void HAL_STACK_StatusCallback(uint8_t code, uint32_t status)
{
    UNUSED(code);
    UNUSED(status);
}

#else /* CH58x_USE_BLE_LIB */

/* 未启用闭源库：整个文件不参与编译，留一个类型避免空翻译单元告警 */
typedef int ch58x_hal_stack_unused_translation_unit_t;

#endif /* CH58x_USE_BLE_LIB */
