#include "ch58x_hal.h"

/* ===================== 中断优先级表 =====================
 * PFIC 只有两级：IPRIOR[7:5] 越小越高，而 PFIC_SetPriority 把它二值化成
 *   0x00 = 高，0x80 = 低。
 *
 * 本表只登记「需要降到低档」的中断，其余一律保留复位值 0x00（最高）。
 * 当前策略：外设最高，BLE 最低。
 *   - BLEL 是软件链路层调度器（LLE_IRQLibHandler），一次可能跑几十~几百微秒，
 *     但在事件窗口内还有富余时间，允许被打断，所以降级。
 *   - 外设中断（UART/SPI/I2C/TMR…）的容忍延迟是硬件 FIFO 填满时间，
 *     115200 下约 700µs，SPI 从机可能只有几微秒，不能被 BLE 阻塞，所以保持最高。
 *   - BLEB 不登记：libCH58xBLE.a 没提供 BB_IRQHandler（startup 里是弱死循环），
 *     该中断在 CH585 上不被库使用。
 *
 * 代价：LLE 被反复抢占会累积延迟。若实测出现掉连接，先检查各外设 ISR 是否
 * 太长（应在微秒级完成），而不是回头调这里的优先级。
 *
 * SysTick 不在此表：它随 HAL_InitTick / 主频切换重新配置。
 */
typedef struct
{
    IRQn_Type irqn;
    uint8_t prio;
} HAL_IRQ_PrioEntryTypeDef;

static const HAL_IRQ_PrioEntryTypeDef s_irqPrio[] =
    {
        /* 唯一需要降级的中断；外设靠复位默认值即保持最高档，不必逐条列举 */
        {BLEL_IRQn, HAL_IRQ_PRIO_LOW}};

/* SysTick 有两种工作模式，由 HAL_UseFreeRunningTick() 在运行期切换：
 *   1ms 中断节拍：每毫秒进一次中断，s_tick 累加，s_ticksPerMs 保持 0
 *   自由运行计数器：不开中断，s_ticksPerMs 非 0，HAL_GetTick 用 CNT 换算
 * 没有编译期开关 —— 用不用 BLE/RF 只由「Protocol/ 目录在不在工程里」决定。
 */
static volatile uint32_t s_tick; /* 1ms 中断节拍累加值 */
static uint32_t s_ticksPerMs;    /* 0 = 1ms 中断节拍；非 0 = 自由运行 */
static uint8_t s_tickFree;       /* 主频切换后仍保持自由运行模式 */

/* 自由运行模式下的毫秒扩展器：
 * SysTick->CNTL 是 32 位 tick 计数器，回绕点接近 2^32，与 uint32 的模一致，
 * 所以无符号差值跨回绕时仍是小正数。
 * 先把 tick 增量换算成毫秒累加到 s_tickExtended，并把不足 1ms 的余数
 * 留在 s_tickFrac 里，避免每次截断、高频调用时毫秒数长不起来。
 */
static uint32_t s_tickLastRaw;  /* 上次读到的 CNTL */
static uint32_t s_tickExtended; /* 累计毫秒，回绕在 2^32 */
static uint32_t s_tickFrac;     /* 不足 1ms 的 tick 余数，< s_ticksPerMs */

/* ===================== BLE 库的中断跳转槽 =====================
 * ble_task_scheduler.S 提供的 LLE_IRQHandler 汇编桩里有这么一句：
 *     la   a1, g_LLE_IRQLibHandlerLocation
 *     lw   a0, 0(a1)
 *     jalr x1, 0(a0)
 * 也就是「查一个全局指针，再间接跳过去」。那个 .S 属于 HAL 驱动层，
 * 只要 Drivers/CH58x_HAL_Driver 在工程里就一定会被编译，所以这个符号
 * 也必须由 HAL 驱动层提供，否则排除掉 Protocol/ 的纯外设工程会链接失败。
 *
 * 这里只定义不赋值，初值 0。真正填值的是 Protocol/stack 的 HAL_STACK_Init()，
 * 它把库里 LLE_IRQLibHandler 的地址写进来。没启用 BLE/RF 时这个槽一直是 0，
 * 只要不开 BLEL 中断就永远不会被读到。
 */
uint32_t g_LLE_IRQLibHandlerLocation;

__weak void HAL_MspInit(void)
{
}

__weak void HAL_MspDeInit(void)
{
}

/**
 * @brief  按 s_irqPrio 表下发中断优先级
 * @note   在 HAL_Init 里调一次即可，各外设开中断时不再重复设优先级。
 *         表里没登记的中断保持复位值 0x00，也就是最高档。
 */
void HAL_NVIC_InitPriorities(void)
{
    uint32_t i;

    for (i = 0U; i < (sizeof(s_irqPrio) / sizeof(s_irqPrio[0])); i++) {
        PFIC_SetPriority(s_irqPrio[i].irqn, s_irqPrio[i].prio);
    }
}

/**
 * @brief  HAL 初始化
 * @retval HAL_OK / HAL_ERROR（节拍时基配置失败）
 */
HAL_StatusTypeDef HAL_Init(void)
{
    HAL_MspInit();

    HAL_NVIC_InitPriorities();

    if (HAL_RCC_InitClock() != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_InitTick(HAL_TICK_PRIORITY);
}

/**
 * @brief  配置系统节拍时基
 * @param  priority SysTick 中断优先级，0=最高，非 0=可被抢占
 * @note   两种模式，由 HAL_UseFreeRunningTick() 在运行期选定：
 *         - 1ms 中断节拍（默认）：每毫秒进一次中断，HAL_IncTick 累加，
 *           供 HAL_GetTick / HAL_DelayTick 使用
 *         - 自由运行计数器：不开中断，避免每 1ms 唤醒把休眠电流拖垮；
 *           HAL_GetTick 仍返回回绕在 2^32 的单调毫秒计数（内部用 CNTL 增量累加）
 *         主频切换后由 HAL_RCC_SetSysClock 重新调用，模式保持不变。
 */
__weak HAL_StatusTypeDef HAL_InitTick(uint32_t priority)
{
    PFIC_SetPriority(SysTick_IRQn, (uint8_t)priority);

    if (s_tickFree != 0U) {
        s_ticksPerMs = HAL_RCC_GetSysClockFreq() / 1000U;
        if (s_ticksPerMs == 0U) {
            return HAL_ERROR;
        }

        /* 主频变了，tick 和毫秒的换算比例跟着变，重设基准；
         * 但不清 s_tickExtended，否则 HAL_GetTick 会倒退。
         * 余数按旧刻度存的，直接作废。 */
        s_tickLastRaw = SysTick->CNTL;
        s_tickFrac    = 0U;

        /* CH58x 的 SysTick 是 32 位单寄存器（CNT@0x08 / CMP@0x10，0x0C/0x14 保留），
         * 故可按 32 位满量程回绕；不写 CNTL 以免打断已有计数 */
        SysTick->CMP  = SysTick_LOAD_RELOAD_Msk - 1U;
        SysTick->CTLR = SysTick_CTLR_STRE | SysTick_CTLR_STCLK | SysTick_CTLR_STE;
        return HAL_OK;
    }

    s_ticksPerMs = 0U;

    if (SysTick_Config(HAL_RCC_GetSysClockFreq() / 1000U) != 0U) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  把系统节拍切成「自由运行计数器」模式
 * @note   供协议栈调用。切换后 SysTick 不再产生中断 —— 否则每 1ms 唤醒一次，
 *         休眠电流直接崩，射频的微秒级定时也无从谈起。
 *         HAL_GetTick 改从 SysTick->CNTL 增量推算，回绕周期仍是 2^32 ms（约 49.7 天），
 *         不会因为切模式而出现 134 秒级的回绕跳变。
 *         主频变更后模式保持，HAL_RCC_SetSysClock 重配时不会退回中断节拍。
 *
 *         要不要调用本函数，由「Protocol/ 目录在不在工程里」唯一决定：
 *         HAL_STACK_Init() 会调它，排除 Protocol/ 的工程没人调，保持 1ms 节拍。
 */
void HAL_UseFreeRunningTick(void)
{
    s_tickFree   = 1U;
    s_ticksPerMs = HAL_RCC_GetSysClockFreq() / 1000U;

    PFIC_DisableIRQ(SysTick_IRQn);

    /* 从 1ms 节拍切过来时，把已累计的 s_tick 作为起点继承下来，
     * 保证 HAL_GetTick 单调、不会因为切换而倒退 */
    s_tickExtended = s_tick;

    SysTick->CTLR = 0U;
    SysTick->SR   = 0U;
    SysTick->CNT  = 0U;
    SysTick->CMP  = SysTick_LOAD_RELOAD_Msk - 1U;
    SysTick->CTLR = SysTick_CTLR_STRE | SysTick_CTLR_STCLK | SysTick_CTLR_STE;

    s_tickLastRaw = 0U; /* CNT 刚被清零 */
    s_tickFrac    = 0U;
}

/**
 * @brief  暂停节拍中断（计数不停）
 * @note   进入低功耗前调用
 */
void HAL_SuspendTick(void)
{
    SysTick->CTLR &= ~SysTick_CTLR_STIE;
}

/**
 * @brief  恢复节拍中断
 * @note   自由运行模式本就不开中断，故不做任何事
 */
void HAL_ResumeTick(void)
{
    if (s_tickFree == 0U) {
        SysTick->CTLR |= SysTick_CTLR_STIE;
    }
}

/**
 * @brief  HAL 反初始化
 * @retval HAL_OK
 */
HAL_StatusTypeDef HAL_DeInit(void)
{
    HAL_MspDeInit();
    return HAL_OK;
}

/**
 * @brief  毫秒级忙等延时
 * @param  Delay 延时毫秒数，0 表示不延时
 */
void HAL_Delay(uint32_t Delay)
{
    HAL_SYS_DelayMs(Delay);
}

/**
 * @brief  微秒级忙等延时
 * @param  Delay 延时微秒数，0 表示不延时
 */
void HAL_DelayUs(uint32_t Delay)
{
    HAL_SYS_DelayUs(Delay);
}

/**
 * @brief  读取系统节拍（ms）
 * @retval 单调递增的毫秒计数，回绕周期 2^32 ms（约 49.7 天）
 * @note   两种时基模式返回同一语义：真正的毫秒计数，回绕点在 2^32，
 *         所以 (uint32_t)(now - start) 做超时差值是安全的。
 *
 *         1ms 中断节拍模式：直接返回 s_tick。
 *
 *         自由运行模式：SysTick->CNTL 是 32 位 tick 计数器，
 *         若先除成毫秒再取差值，回绕点就变成 2^32 / s_ticksPerMs
 *         （32MHz 下约 134218），差值会在回绕点跳变成巨值。
 *         所以这里**在 tick 域取差值** —— CNTL 的回绕点接近 2^32，
 *         与 uint32 的模一致，无符号差值跨回绕时仍是小正数。
 *         再把 tick 增量换算成毫秒累加，不足 1ms 的余数留在 s_tickFrac，
 *         否则高频调用时每次除法都截断成 0，毫秒数永远长不起来。
 *
 *         这样两种模式返回的都是回绕在 2^32 的单调毫秒计数，
 *         (uint32_t)(now - start) 做超时差值重新变得安全。
 *         取舍：CNTL 在 0xFFFFFFFE 重载（而非 0xFFFFFFFF），跨一次回绕
 *         会多算 1 tick（≈ 32ns），每 134 秒一次，可忽略。
 *
 *         前提：两次调用间隔必须远小于 134 秒，否则会漏圈。
 *         超时轮询是微秒级调用，周期任务也是秒级，都满足。
 *
 *         本函数有副作用、不是纯读，**不可重入**，不能在中断里调用
 *         （与现有约定一致：HAL_GetTick 只允许在主上下文使用）。
 */
uint32_t HAL_GetTick(void)
{
    /* 只读一次：若读两次，中间被 HAL_UseFreeRunningTick 改成 0 会除零 */
    uint32_t ticksPerMs = s_ticksPerMs;
    uint32_t raw;
    uint32_t delta;
    uint32_t inc;

    if (ticksPerMs == 0U) {
        return s_tick;
    }

    raw           = SysTick->CNTL;
    delta         = raw - s_tickLastRaw; /* tick 域差值，跨回绕仍是小正数 */
    s_tickLastRaw = raw;

    if (delta == 0U) {
        return s_tickExtended;
    }

    /* 接上上次剩下的不足 1ms 的 tick；两者正常都远小于 2^32，
     * 只有两次调用隔 134 秒以上才会碰到钳位 */
    if (delta > (0xFFFFFFFFU - s_tickFrac)) {
        delta = 0xFFFFFFFFU - s_tickFrac;
    }
    delta += s_tickFrac;

    inc        = delta / ticksPerMs;
    s_tickFrac = delta - (inc * ticksPerMs);

    s_tickExtended += inc;

    return s_tickExtended;
}

/**
 * @brief  基于节拍的毫秒等待
 * @param  Delay 等待毫秒数
 * @note   仅轮询时基，不依赖 SysTick 中断，但休眠期间无法走时。
 *         HAL_GetTick 已经是回绕在 2^32 的单调毫秒计数，
 *         所以直接做无符号差值即可，两种时基模式通用。
 */
void HAL_DelayTick(uint32_t Delay)
{
    uint32_t start = HAL_GetTick();
    uint32_t wait  = Delay;

    if (wait < HAL_MAX_DELAY) {
        wait += 1U; /* 补偿节拍边沿抖动，保证至少等满 Delay 毫秒 */
    }

    while ((HAL_GetTick() - start) < wait) {
    }
}

/**
 * @brief  1ms 节拍递增，由 SysTick_Handler 调用
 * @note   自由运行模式下 SysTick 不产生中断，本函数不会被调用
 */
void HAL_IncTick(void)
{
    s_tick++;
}
