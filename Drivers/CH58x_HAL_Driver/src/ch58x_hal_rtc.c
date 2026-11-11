#include "ch58x_hal_rtc.h"

#include "ch58x_hal_sys.h"

/* 一天的秒数 */
#define HAL_RTC_SECONDS_PER_DAY 86400U

/**
 * @brief  是否闰年
 */
static uint8_t HAL_RTC_IsLeapYear(uint16_t year)
{
    return (uint8_t)(((year % 400U) == 0U) ||
                     (((year % 100U) != 0U) && ((year % 4U) == 0U)));
}

/**
 * @brief  该年天数
 */
static uint16_t HAL_RTC_YearLength(uint16_t year)
{
    return (HAL_RTC_IsLeapYear(year) != 0U) ? 366U : 365U;
}

/**
 * @brief  该月天数
 * @param  month 0 ~ 11
 */
static uint8_t HAL_RTC_MonthLength(uint8_t leap, uint8_t month)
{
    static const uint8_t days[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                     31U, 31U, 30U, 31U, 30U, 31U};

    if (month == 1U) {
        return (uint8_t)(28U + leap);
    }

    return days[month];
}

/**
 * @brief  等到 32K 时钟引脚有效
 */
static void HAL_RTC_WaitClockPin(void)
{
    volatile uint8_t clkPin;

    do {
        clkPin = (R8_CK32K_CONFIG & RB_32K_CLK_PIN);
    } while (clkPin != (R8_CK32K_CONFIG & RB_32K_CLK_PIN));

    while (clkPin == 0U) {
        do {
            clkPin = (R8_CK32K_CONFIG & RB_32K_CLK_PIN);
        } while (clkPin != (R8_CK32K_CONFIG & RB_32K_CLK_PIN));
    }
}

/**
 * @brief  初始化 RTC：使能触发模式并打开中断
 * @note   唤醒源仍由 HAL_PWR_EnableRtcWakeUp 负责
 */
HAL_StatusTypeDef HAL_RTC_Init(void)
{
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RTC_MODE_CTRL |= RB_RTC_TRIG_EN;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    PFIC_EnableIRQ(RTC_IRQn);

    return HAL_OK;
}

/**
 * @brief  关闭 RTC 触发与周期定时
 */
HAL_StatusTypeDef HAL_RTC_DeInit(void)
{
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RTC_MODE_CTRL &= ~(RB_RTC_TRIG_EN | RB_RTC_TMR_EN);
    HAL_SYS_SAFE_ACCESS_DISABLE();

    PFIC_DisableIRQ(RTC_IRQn);

    return HAL_OK;
}

/* 周期定时的当前周期长度（32K 周期数），0 表示未启用 */
static uint32_t s_periodCycles;

/**
 * @brief  设置日历时间
 * @note   写入前会等待 32K 时钟稳定，耗时可能较长
 */
HAL_StatusTypeDef HAL_RTC_SetTime(const HAL_RTC_TimeTypeDef *time)
{
    uint64_t totalCycles;
    uint32_t days = 0U;
    uint32_t sod;
    uint32_t hwDay;
    uint32_t hwCnt;
    uint16_t year;
    uint16_t month;

    if (time == NULL) {
        return HAL_ERROR;
    }

    if ((time->year < HAL_RTC_BASE_YEAR) || (time->month < 1U) || (time->month > 12U) ||
        (time->day < 1U) || (time->day > 31U) || (time->hour > 23U) ||
        (time->minute > 59U) || (time->second > 59U)) {
        return HAL_ERROR;
    }

    for (year = HAL_RTC_BASE_YEAR; year < time->year; year++) {
        days += HAL_RTC_YearLength(year);
    }

    for (month = 1U; month < time->month; month++) {
        days += HAL_RTC_MonthLength(HAL_RTC_IsLeapYear(time->year), (uint8_t)(month - 1U));
    }

    days += (uint32_t)(time->day - 1U);
    sod = ((uint32_t)time->hour * 3600U) + ((uint32_t)time->minute * 60U) + (uint32_t)time->second;

    /* 先换算成 32K 周期总数，再拆成硬件日计数与当日计数，兼容 32000/32768 两种时钟 */
    totalCycles = ((((uint64_t)days * HAL_RTC_SECONDS_PER_DAY) + sod) * HAL_RCC_OSC32K_FREQ);

    hwDay = (uint32_t)(totalCycles / HAL_RTC_MAX_COUNT);
    hwCnt = (uint32_t)(totalCycles % HAL_RTC_MAX_COUNT);

    HAL_RTC_WaitClockPin();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R32_RTC_TRIG = hwDay;
    R8_RTC_MODE_CTRL |= RB_RTC_LOAD_HI;
    HAL_SYS_SAFE_ACCESS_DISABLE();
    while ((R32_RTC_TRIG & 0x3FFFU) != (R32_RTC_CNT_DAY & 0x3FFFU)) {
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R32_RTC_TRIG = hwCnt;
    R8_RTC_MODE_CTRL |= RB_RTC_LOAD_LO;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    return HAL_OK;
}

/**
 * @brief  读取日历时间
 */
HAL_StatusTypeDef HAL_RTC_GetTime(HAL_RTC_TimeTypeDef *time)
{
    uint64_t total;
    uint32_t days;
    uint32_t sod;
    uint32_t hwDay;
    uint32_t hwDayCheck;
    uint32_t cnt;
    uint16_t year;
    uint8_t month;

    if (time == NULL) {
        return HAL_ERROR;
    }

    /* day 与计数需一致快照：回绕可能发生在两次读之间 */
    do {
        hwDay      = R32_RTC_CNT_DAY & 0x3FFFU;
        cnt        = HAL_RTC_GetCounter();
        hwDayCheck = R32_RTC_CNT_DAY & 0x3FFFU;
    } while (hwDay != hwDayCheck);

    total = (((uint64_t)hwDay * HAL_RTC_MAX_COUNT) + cnt) / HAL_RCC_OSC32K_FREQ;
    days  = (uint32_t)(total / HAL_RTC_SECONDS_PER_DAY);
    sod   = (uint32_t)(total % HAL_RTC_SECONDS_PER_DAY);

    year = HAL_RTC_BASE_YEAR;
    while (days >= HAL_RTC_YearLength(year)) {
        days -= HAL_RTC_YearLength(year);
        year++;
    }

    month = 0U;
    while (days >= HAL_RTC_MonthLength(HAL_RTC_IsLeapYear(year), month)) {
        days -= HAL_RTC_MonthLength(HAL_RTC_IsLeapYear(year), month);
        month++;
    }

    time->year   = year;
    time->month  = (uint8_t)(month + 1U);
    time->day    = (uint8_t)(days + 1U);
    time->hour   = (uint8_t)(sod / 3600U);
    time->minute = (uint8_t)((sod % 3600U) / 60U);
    time->second = (uint8_t)(sod % 60U);

    return HAL_OK;
}

/**
 * @brief  读取当前 32K 周期计数
 */
__HIGH_CODE
uint32_t HAL_RTC_GetCounter(void)
{
    volatile uint32_t cnt;

    do {
        cnt = R32_RTC_CNT_32K;
    } while (cnt != R32_RTC_CNT_32K);

    return cnt;
}

/**
 * @brief  设置 32K 周期计数
 */
void HAL_RTC_SetCounter(uint32_t counter)
{
    HAL_RTC_WaitClockPin();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R32_RTC_TRIG = counter;
    R8_RTC_MODE_CTRL |= RB_RTC_LOAD_LO;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  设置触发计数值（绝对值）
 */
__HIGH_CODE
void HAL_RTC_SetTrigger(uint32_t trigger)
{
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R32_RTC_TRIG = trigger;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  从当前时刻起延时 cycles 个 32K 周期后触发
 */
HAL_StatusTypeDef HAL_RTC_SetTriggerAfter(uint32_t cycles)
{
    uint32_t t = HAL_RTC_GetCounter() + cycles;

    if (t > HAL_RTC_MAX_COUNT) {
        t -= HAL_RTC_MAX_COUNT;
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R32_RTC_TRIG = t;
    R8_RTC_MODE_CTRL |= RB_RTC_TRIG_EN;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    return HAL_OK;
}

/**
 * @brief  配置周期定时
 * @note   用触发模式重复装载实现，周期按实际 32K 频率换算，
 *         32000Hz 与 32768Hz 下都是准确的标称周期
 */
void HAL_RTC_SetPeriod(HAL_RTC_PeriodTypeDef period)
{
    /* 周期 = 2^period / 8 秒 */
    s_periodCycles = (uint32_t)(((uint64_t)HAL_RCC_OSC32K_FREQ << (uint32_t)period) >> 3);

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RTC_MODE_CTRL &= ~RB_RTC_TMR_EN; /* 不使用硬件周期模式 */
    HAL_SYS_SAFE_ACCESS_DISABLE();

    (void)HAL_RTC_SetTriggerAfter(s_periodCycles);
}

/**
 * @brief  关闭触发模式
 */
void HAL_RTC_DisableTrigger(void)
{
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RTC_MODE_CTRL &= ~RB_RTC_TRIG_EN;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  关闭周期定时
 */
void HAL_RTC_DisablePeriod(void)
{
    s_periodCycles = 0U;

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RTC_MODE_CTRL &= ~RB_RTC_TMR_EN;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  读取事件标志
 * @retval 0=未发生，1=已发生
 */
uint8_t HAL_RTC_GetEventFlag(HAL_RTC_EventTypeDef event)
{
    if (event == HAL_RTC_EVENT_TRIGGER) {
        return (uint8_t)((R8_RTC_FLAG_CTRL & RB_RTC_TRIG_FLAG) != 0U);
    }

    return (uint8_t)((R8_RTC_FLAG_CTRL & RB_RTC_TMR_FLAG) != 0U);
}

/**
 * @brief  清除事件标志
 */
void HAL_RTC_ClearEventFlag(HAL_RTC_EventTypeDef event)
{
    if (event == HAL_RTC_EVENT_TRIGGER) {
        R8_RTC_FLAG_CTRL = RB_RTC_TRIG_CLR;
    } else {
        R8_RTC_FLAG_CTRL = RB_RTC_TMR_CLR;
    }
}

/**
 * @brief  RTC 中断处理
 * @note   需由应用的 RTC_IRQHandler 调用
 */
__HIGH_CODE
void HAL_RTC_IRQHandler(void)
{
    uint8_t triggered = HAL_RTC_GetEventFlag(HAL_RTC_EVENT_TRIGGER);

    R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);

    if (triggered == 0U) {
        return;
    }

    /* 周期模式下重新装载下一次触发 */
    if (s_periodCycles != 0U) {
        (void)HAL_RTC_SetTriggerAfter(s_periodCycles);
    }

    HAL_RTC_TriggerCallback();
}

/**
 * @brief  触发事件回调，默认空实现
 */
__weak void HAL_RTC_TriggerCallback(void)
{
}
