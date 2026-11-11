#include "ch58x_hal_pwr.h"

#include "ch58x_hal_sys.h"
#include "ISP585.h"

/* ROM 硬件参数区首地址（原 StdPeriph 的 ROM_CFG_ADR_HW） */
#define HAL_PWR_ROM_CFG_ADR_HW 0x7F00CU

static HAL_PWR_WakeGapCallbackTypeDef s_wakeGapCb;

/**
 * @brief  系统供电方式选择
 * @param  s ENABLE=内部 DC/DC，DISABLE=内部 LDO
 * @note   运行态与休眠态都按此设置供电；硬件参数区指示不支持 DC/DC 时，ENABLE 无操作。
 */
void HAL_PWR_ConfigDCDC(FunctionalState s)
{
    uint16_t adj  = R16_AUX_POWER_ADJ;
    uint16_t plan = R16_POWER_PLAN;

    if (s == DISABLE) {
        adj &= ~RB_DCDC_CHARGE;
        plan &= ~(RB_PWR_DCDC_EN | RB_PWR_DCDC_PRE); /* 关闭 DC/DC，改用内部 LDO */

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R16_AUX_POWER_ADJ = adj;
        R16_POWER_PLAN    = plan;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    } else {
        uint32_t hwData[2];

        FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, HAL_PWR_ROM_CFG_ADR_HW, hwData, 0);
        if ((hwData[0] & (1U << 13)) != 0U) {
            /* 硬件参数指示不支持 DC/DC */
            return;
        }

        adj |= RB_DCDC_CHARGE;
        plan |= RB_PWR_DCDC_PRE;

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R16_AUX_POWER_ADJ = adj;
        R16_POWER_PLAN    = plan;
        HAL_SYS_SAFE_ACCESS_DISABLE();

        HAL_SYS_DelayUs(10U);

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R16_POWER_PLAN |= RB_PWR_DCDC_EN;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    }
}

/**
 * @brief  可控供电单元开关，unit 取 HAL_PWR_UNIT_*
 * @note   LSE/LSI 在 R8_CK32K_CONFIG，HSE/PLL 在 R8_HFCK_PWR_CTRL。
 *         StdPeriph 版本只用 unit & 0x03，HSE/PLL 实际被静默丢弃，此处已修正。
 */
void HAL_PWR_ConfigUnitPower(FunctionalState s, uint32_t unit)
{
    uint32_t lowMask = unit & 0x03U; /* LSE | LSI */
    uint32_t hfcMask = unit & 0x14U; /* HSE | PLL */

    if (lowMask != 0U) {
        uint8_t cfg = R8_CK32K_CONFIG;

        cfg = (s == DISABLE) ? (uint8_t)(cfg & ~lowMask) : (uint8_t)(cfg | lowMask);

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_CK32K_CONFIG = cfg;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    }

    if (hfcMask != 0U) {
        uint8_t ctrl = R8_HFCK_PWR_CTRL;

        ctrl = (s == DISABLE) ? (uint8_t)(ctrl & ~hfcMask) : (uint8_t)(ctrl | hfcMask);

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_HFCK_PWR_CTRL = ctrl;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    }
}

/**
 * @brief  安全时钟控制位，periph 取 HAL_PWR_SLPCLK_*
 */
void HAL_PWR_ConfigSafeClock(FunctionalState s, uint32_t periph)
{
    uint32_t ctrl = R8_SAFE_CLK_CTRL;

    if (s == DISABLE) {
        ctrl |= periph; /* 置位表示关闭 */
    } else {
        ctrl &= ~periph;
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_SAFE_CLK_CTRL = (uint8_t)ctrl;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  外设时钟控制位
 * @note   关闭外设时钟前必须先关闭对应中断
 */
void HAL_PWR_ConfigPeriphClock(FunctionalState s, uint32_t periph)
{
    uint32_t ctrl = R32_SLEEP_CONTROL;

    if (s == DISABLE) {
        ctrl |= periph; /* 置位表示关闭 */
    } else {
        ctrl &= ~periph;
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R32_SLEEP_CONTROL = ctrl;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  睡眠唤醒源配置
 */
void HAL_PWR_ConfigPeriphWakeUp(FunctionalState s, uint32_t periph, HAL_PWR_WakeUpModeTypeDef mode)
{
    if (s == DISABLE) {
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_SLP_WAKE_CTRL &= (uint8_t)~periph;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    } else {
        uint8_t m;

        switch (mode) {
            case HAL_PWR_WAKE_LONG_DELAY:
                m = 0x00U;
                break;

            case HAL_PWR_WAKE_SHORT_DELAY:
            default:
                m = 0x01U;
                break;
        }

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_SLP_WAKE_CTRL |= (uint8_t)(RB_WAKE_EV_MODE | periph);
        HAL_SYS_SAFE_ACCESS_DISABLE();

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_SLP_POWER_CTRL &= ~RB_WAKE_DLY_MOD;
        HAL_SYS_SAFE_ACCESS_DISABLE();

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_SLP_POWER_CTRL |= m;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    }
}

/**
 * @brief  电源（电池）电压监控
 */
void HAL_PWR_ConfigVoltageMonitor(FunctionalState s, HAL_PWR_VoltageLevelTypeDef level)
{
    uint8_t ctrl = R8_BAT_DET_CTRL;
    uint8_t cfg  = R8_BAT_DET_CFG;
    uint8_t lv   = (uint8_t)level;

    if (s == DISABLE) {
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_BAT_DET_CTRL = 0;
        HAL_SYS_SAFE_ACCESS_DISABLE();
        return;
    }

    if ((lv & 0x80U) != 0U) {
        /* 低功耗监控 */
        cfg  = (uint8_t)(lv & 0x03U);
        ctrl = (uint8_t)(RB_BAT_MON_EN | ((lv >> 2) & 0x01U));
    } else {
        /* 高精度监控 */
        cfg  = (uint8_t)(lv & 0x03U);
        ctrl = RB_BAT_DET_EN;
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_BAT_DET_CTRL = ctrl;
    R8_BAT_DET_CFG  = cfg;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_DelayUs(1U);

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_BAT_DET_CTRL |= (RB_BAT_LOW_IE | RB_BAT_LOWER_IE);
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  进入 Idle 模式
 */
__HIGH_CODE
void HAL_PWR_EnterIdleMode(void)
{
    FLASH_ROM_SW_RESET();
    R8_FLASH_CTRL = 0x04; /* 关闭 flash */

    PFIC->SCTLR &= ~(1U << 2); /* sleep */
    __WFI();
    __nop();
    __nop();
}

/**
 * @brief  进入 Halt 模式
 * @note   切到 HSI/5 运行，唤醒后需自行重新选择系统时钟
 */
__HIGH_CODE
void HAL_PWR_EnterHaltMode(void)
{
    uint32_t i;
    uint8_t x32Mpw;
    uint16_t clk_sys_cfg;
    uint8_t flash_cfg;
    uint8_t flash_sck;

    clk_sys_cfg = R16_CLK_SYS_CFG;
    flash_cfg   = R8_FLASH_CFG;
    flash_sck   = R8_FLASH_SCK;

    FLASH_ROM_SW_RESET();
    R8_FLASH_CTRL = 0x04; /* 关闭 flash */

    x32Mpw = R8_XT32M_TUNE;
    if ((R8_HFCK_PWR_CTRL & RB_CLK_XT32M_KEEP) == 0U) {
        x32Mpw = (uint8_t)((x32Mpw & 0xFCU) | 0x03U); /* 150% 额定电流 */
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_BAT_DET_CTRL = 0; /* 关闭电压监控 */
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32M_TUNE = x32Mpw;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    if ((R16_CLK_SYS_CFG & RB_OSC32M_SEL) != 0U) {
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_SLP_POWER_CTRL &= ~(RB_WAKE_DLY_MOD);
        R8_SLP_POWER_CTRL |= 0x01;
        R8_FLASH_CFG    = 0x57;
        R8_FLASH_SCK    = R8_FLASH_SCK & (~(1U << 4));
        R16_CLK_SYS_CFG = HAL_RCC_CLK_HSE_4M;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    } else {
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_SLP_POWER_CTRL &= ~(RB_WAKE_DLY_MOD);
        R8_SLP_POWER_CTRL |= 0x01;
        R8_FLASH_CFG    = 0x57;
        R8_FLASH_SCK    = R8_FLASH_SCK & (~(1U << 4));
        R16_CLK_SYS_CFG = HAL_RCC_CLK_HSI_4M;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    }

    PFIC->SCTLR |= (1U << 2); /* deep sleep */
    __WFI();
    __nop();
    __nop();

    if (((clk_sys_cfg & RB_OSC32M_SEL) == 0U) && ((clk_sys_cfg & 0x100U) != 0U)) {
        i = 40;
        do {
            __nop();
        } while (--i);
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_FLASH_CFG    = flash_cfg;
    R8_FLASH_SCK    = flash_sck;
    R16_CLK_SYS_CFG = clk_sys_cfg;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  进入 Halt 模式，使用 WFE 唤醒
 */
__HIGH_CODE
void HAL_PWR_EnterHaltWFEMode(void)
{
    uint8_t x32Mpw;

    FLASH_ROM_SW_RESET();
    R8_FLASH_CTRL = 0x04; /* 关闭 flash */

    x32Mpw = R8_XT32M_TUNE;
    if ((R8_HFCK_PWR_CTRL & RB_CLK_XT32M_KEEP) == 0U) {
        x32Mpw = (uint8_t)((x32Mpw & 0xFCU) | 0x03U); /* 150% 额定电流 */
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_BAT_DET_CTRL = 0; /* 关闭电压监控 */
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32M_TUNE = x32Mpw;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_SLP_POWER_CTRL &= ~(RB_WAKE_DLY_MOD);
    R8_SLP_POWER_CTRL |= 0x01;
    R8_FLASH_CFG    = 0x57;
    R8_FLASH_SCK    = R8_FLASH_SCK & (~(1U << 4));
    R16_CLK_SYS_CFG = HAL_RCC_CLK_HSI_4M;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    PFIC->SCTLR |= (1U << 2); /* deep sleep */
    __WFE();
    __nop();
    __nop();
}

/**
 * @brief  进入 Sleep 模式
 * @param  rm 保持供电的区域，取 HAL_PWR_RAM_*
 * @note   唤醒后到 flash 稳定还需约 300us
 */
__HIGH_CODE
void HAL_PWR_EnterSleepMode(uint16_t rm)
{
    uint8_t x32Mpw;
    uint16_t power_plan;
    uint16_t clk_sys_cfg;
    uint16_t hfck_pwr_ctrl;
    uint8_t flash_cfg;
    uint8_t flash_sck;
    uint32_t i;

    clk_sys_cfg   = R16_CLK_SYS_CFG;
    hfck_pwr_ctrl = R8_HFCK_PWR_CTRL;
    x32Mpw        = R8_XT32M_TUNE;
    x32Mpw        = (uint8_t)((x32Mpw & 0xFCU) | 0x03U); /* 150% 额定电流 */
    flash_cfg     = R8_FLASH_CFG;
    flash_sck     = R8_FLASH_SCK;

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_BAT_DET_CTRL = 0; /* 关闭电压监控 */
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32M_TUNE = x32Mpw;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    PFIC->SCTLR |= (1U << 2); /* deep sleep */

    power_plan = R16_POWER_PLAN & (RB_PWR_DCDC_EN | RB_PWR_DCDC_PRE);
    power_plan |= (uint16_t)(RB_PWR_PLAN_EN | RB_PWR_CORE | rm | (2U << 11));
    if ((HAL_RCC_GetChipInfo() & 0x800000U) != 0U) {
        power_plan &= ~RB_XT_PRE_EN; /* 该芯片不支持 RB_XT_PRE_EN */
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R16_POWER_PLAN = power_plan;
    R8_HFCK_PWR_CTRL |= RB_CLK_RC16M_PON; /* 睡眠需要内部 HSI */
    HAL_SYS_SAFE_ACCESS_DISABLE();

    if ((R16_CLK_SYS_CFG & RB_OSC32M_SEL) != 0U) {
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_SLP_POWER_CTRL &= ~(RB_WAKE_DLY_MOD);
        R8_SLP_POWER_CTRL |= 0x01;
        R8_FLASH_CFG    = 0x57;
        R8_FLASH_SCK    = R8_FLASH_SCK & (~(1U << 4));
        R16_CLK_SYS_CFG = HAL_RCC_CLK_HSE_4M;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    } else {
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_SLP_POWER_CTRL &= ~(RB_WAKE_DLY_MOD);
        R8_SLP_POWER_CTRL |= 0x01;
        R8_FLASH_CFG    = 0x57;
        R8_FLASH_SCK    = R8_FLASH_SCK & (~(1U << 4));
        R16_CLK_SYS_CFG = HAL_RCC_CLK_HSI_4M;
        HAL_SYS_SAFE_ACCESS_DISABLE();
    }

    __WFI();
    __nop();
    __nop();

    if ((rm & RB_PWR_EXTEND) != 0U) {
        /* 使用高速 USB 时唤醒后需复位其寄存器 */
        R32_U2H_BC_CTRL           = 0;
        (*((PUINT32V)0x4000C254)) = 0;
    }

    if (((clk_sys_cfg & RB_OSC32M_SEL) == 0U) && ((clk_sys_cfg & 0x100U) != 0U)) {
        i = 40;
        do {
            __nop();
        } while (--i);
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_FLASH_CFG     = flash_cfg;
    R8_FLASH_SCK     = flash_sck;
    R16_CLK_SYS_CFG  = clk_sys_cfg;
    R8_HFCK_PWR_CTRL = (uint8_t)hfck_pwr_ctrl;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R16_POWER_PLAN &= ~RB_PWR_PLAN_EN;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  进入 Sleep 模式，使用 WFE 唤醒
 * @param  rm 保持供电的区域，取 HAL_PWR_RAM_*
 */
__HIGH_CODE
void HAL_PWR_EnterSleepWFEMode(uint16_t rm)
{
    uint16_t power_plan;

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_BAT_DET_CTRL = 0; /* 关闭电压监控 */
    HAL_SYS_SAFE_ACCESS_DISABLE();

    PFIC->SCTLR |= (1U << 2); /* deep sleep */

    power_plan = R16_POWER_PLAN & (RB_PWR_DCDC_EN | RB_PWR_DCDC_PRE);
    power_plan |= (uint16_t)(RB_PWR_PLAN_EN | RB_PWR_CORE | rm | (2U << 11));
    power_plan &= ~0x4000U; /* 该芯片不支持 RB_XT_PRE_EN */

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R16_POWER_PLAN = power_plan;
    R8_HFCK_PWR_CTRL |= RB_CLK_RC16M_PON; /* 睡眠需要内部 HSI */
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_SLP_POWER_CTRL &= ~(RB_WAKE_DLY_MOD);
    R8_SLP_POWER_CTRL |= 0x01;
    R8_FLASH_CFG    = 0x57;
    R8_FLASH_SCK    = R8_FLASH_SCK & (~(1U << 4));
    R16_CLK_SYS_CFG = HAL_RCC_CLK_HSI_4M;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    __WFE();
    __nop();
    __nop();

    if ((rm & RB_PWR_EXTEND) != 0U) {
        R32_U2H_BC_CTRL           = 0;
        (*((PUINT32V)0x4000C254)) = 0;
    }

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R16_POWER_PLAN &= ~RB_PWR_PLAN_EN;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  进入 Shutdown 模式
 * @param  rm 保持供电的区域，取 HAL_PWR_RAM_*
 * @note   进入前强制切回内部 LDO，唤醒后会走软件复位
 */
__HIGH_CODE
void HAL_PWR_EnterShutdownMode(uint16_t rm)
{
    uint8_t x32Kpw;

    FLASH_ROM_SW_RESET();

    x32Kpw = R8_XT32K_TUNE;
    x32Kpw = (uint8_t)((x32Kpw & 0xFCU) | 0x01U); /* LSE 驱动电流降到额定 */

    HAL_RCC_SetSysClock(HAL_RCC_CLK_HSI_PLL_13M);

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_BAT_DET_CTRL = 0; /* 关闭电压监控 */
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32K_TUNE = x32Kpw;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    PFIC->SCTLR |= (1U << 2); /* deep sleep */

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_SLP_POWER_CTRL |= 0x40;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R16_POWER_PLAN = (uint16_t)(RB_PWR_PLAN_EN | rm);
    HAL_SYS_SAFE_ACCESS_DISABLE();

    __WFI();
    __nop();
    __nop();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R16_INT32K_TUNE = 0xFFFF;
    R8_RST_WDOG_CTRL |= RB_SOFTWARE_RESET;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  使能 RTC 作为休眠唤醒源
 * @note   只置位，不清除其它唤醒源，也不改动唤醒延时模式；
 *         须在首次调用 HAL_PWR_EnterSleepUntil 之前执行，否则 WFE 无法被 RTC 唤醒。
 */
void HAL_PWR_EnableRtcWakeUp(void)
{
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_SLP_WAKE_CTRL |= RB_SLP_RTC_WAKE;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  进入 Sleep 直到 32K 计数到达 wakeTime（BLE idleCB 入口）
 * @param  wakeTime 目标 32K 计数值
 * @retval 0=唤醒成功，2=休眠时长越界，3=已到期未休眠
 */
__HIGH_CODE
uint32_t HAL_PWR_EnterSleepUntil(uint32_t wakeTime)
{
    uint32_t time_tign;
    uint32_t time_sleep;
    uint32_t time_curr;
    uint32_t irq_status;

    if (wakeTime <= HAL_PWR_WAKEUP_RTC_MAX_TIME) {
        time_tign = wakeTime + (HAL_RTC_MAX_COUNT - HAL_PWR_WAKEUP_RTC_MAX_TIME);
    } else {
        time_tign = wakeTime - HAL_PWR_WAKEUP_RTC_MAX_TIME;
    }

    irq_status = HAL_SYS_DisableIrq();
    time_curr  = HAL_RTC_GetCounter();

    if (time_tign < time_curr) {
        time_sleep = time_tign + (HAL_RTC_MAX_COUNT - time_curr);
    } else {
        time_sleep = time_tign - time_curr;
    }

    if ((time_sleep < HAL_PWR_SLEEP_RTC_MIN_TIME) || (time_sleep > HAL_PWR_SLEEP_RTC_MAX_TIME)) {
        HAL_SYS_RestoreIrq(irq_status);
        return 2U;
    }

    HAL_RTC_SetTrigger(time_tign);
    R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);

    if (!(R8_RTC_FLAG_CTRL & RB_RTC_TRIG_FLAG)) {
        uint8_t x32Mpw;

        HAL_PWR_EnterSleepWFEMode(HAL_PWR_RAM_32K | HAL_PWR_RAM_96K | HAL_PWR_RAM_EXTEND);

        x32Mpw = R8_XT32M_TUNE;
        x32Mpw = (x32Mpw & 0xFC) | 0x03;
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_XT32M_TUNE = x32Mpw;
        HAL_SYS_SAFE_ACCESS_DISABLE();

        if (!(R8_RTC_FLAG_CTRL & RB_RTC_TRIG_FLAG)) {
            HAL_SYS_DelayUs(1400U);
            HAL_RCC_SetSysClock(HAL_RCC_SYSCLK_SOURCE);
            HAL_SYS_RestoreIrq(irq_status);
            return 0U;
        }

        R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);
        HAL_RTC_SetTrigger(wakeTime);

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_HFCK_PWR_CTRL |= RB_CLK_XT32M_KEEP;
        HAL_SYS_SAFE_ACCESS_DISABLE();

        if (!(R8_RTC_FLAG_CTRL & RB_RTC_TRIG_FLAG)) {
            HAL_PWR_EnterHaltWFEMode();
        }

        HAL_RCC_SetSysClock(HAL_RCC_SYSCLK_SOURCE);
        R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);
        HAL_SYS_RestoreIrq(irq_status);
        HAL_RCC_SetHSECurrent(HAL_RCC_HSE_RCUR_100);
        return 0U;
    }

    HAL_SYS_RestoreIrq(irq_status);
    return 3U;
}

/**
 * @brief  注册唤醒间隙回调
 * @note   仅接受位于 RAM 的回调地址
 */
void HAL_PWR_RegisterWakeGapCallback(HAL_PWR_WakeGapCallbackTypeDef cb)
{
    if (((uint32_t)cb & 0x20000000U) != 0U) {
        s_wakeGapCb = cb;
    }

    UNUSED(s_wakeGapCb);
}
