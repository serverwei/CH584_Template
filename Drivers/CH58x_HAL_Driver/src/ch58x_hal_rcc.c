#include "ch58x_hal_rcc.h"

#include "ch58x_hal.h"
#include "ch58x_hal_sys.h"
#include "ISP585.h"

/* 芯片信息快照，来自 ROM_CFG_CHIP_ID */
static uint32_t s_chipInfo;

/**
 * @brief  按 ch58x_hal_conf.h 的配置完成时钟初始化
 * @note   HSE 电容/电流须在启动 HSE 之前写入。
 *         32K 源在这里一并配好，RTC / LCD / BLE 都靠它，
 *         不要留到各自模块里重复初始化。
 */
HAL_StatusTypeDef HAL_RCC_InitClock(void)
{
    s_chipInfo = *(volatile uint32_t *)ROM_CFG_CHIP_ID;

    HAL_RCC_SetHSECapacitance(HAL_RCC_HSE_CAPACITANCE);
    HAL_RCC_SetHSECurrent(HAL_RCC_HSE_CURRENT);

    HAL_RCC_Config32KSource();

    return HAL_RCC_SetSysClock(HAL_RCC_SYSCLK_SOURCE);
}

/**
 * @brief  读取芯片信息（ROM_CFG_CHIP_ID 快照）
 */
uint32_t HAL_RCC_GetChipInfo(void)
{
    return s_chipInfo;
}

/**
 * @brief  切换系统时钟源
 * @note   进入安全访问后只有一个出口，避免提前 return 破坏成对开关
 */
__HIGH_CODE
HAL_StatusTypeDef HAL_RCC_SetSysClock(HAL_RCC_SysClkTypeDef clk)
{
    uint16_t clk_sys_cfg;
    uint8_t i;
    uint8_t x32M_c;

    s_chipInfo = *(volatile uint32_t *)ROM_CFG_CHIP_ID;

    R32_SAFE_MODE_CTRL |= RB_XROM_312M_SEL;
    R8_SAFE_MODE_CTRL &= ~RB_SAFE_AUTO_EN;

    HAL_SYS_SAFE_ACCESS_ENABLE();

    if (clk == RB_CLK_SYS_MOD) {
        /* 32K 作为主频 */
        R16_CLK_SYS_CFG |= RB_CLK_SYS_MOD;
    } else {
        if ((clk & RB_OSC32M_SEL) != 0U) {
            if ((R8_HFCK_PWR_CTRL & RB_CLK_XT32M_PON) == 0U) {
                x32M_c = R8_XT32M_TUNE;
                R8_XT32M_TUNE |= 0x03;
                R8_HFCK_PWR_CTRL |= RB_CLK_XT32M_PON;
                clk_sys_cfg = R16_CLK_SYS_CFG;
                R16_CLK_SYS_CFG |= 0xC0;
                for (i = 0; i < 9U; i++) {
                    __nop();
                }
                R16_CLK_SYS_CFG = clk_sys_cfg;
                R8_XT32M_TUNE   = x32M_c;
            }
        } else {
            R8_HFCK_PWR_CTRL |= RB_CLK_RC16M_PON;
        }

        if ((clk & RB_XROM_SCLK_SEL) != 0U) {
            /* PLL 分频 */
            R8_HFCK_PWR_CTRL |= RB_CLK_PLL_PON;

            if ((clk & 0x1FU) == 0U) {
                R8_FLASH_SCK = R8_FLASH_SCK | (1U << 4);
                R8_FLASH_CFG = 0x07;
            } else if ((clk & 0x1FU) < 10U) {
                R8_FLASH_SCK = R8_FLASH_SCK & (~(1U << 4));
                R8_FLASH_CFG = 0x01;
            } else if ((clk & 0x1FU) < 16U) {
                R8_FLASH_SCK = R8_FLASH_SCK & (~(1U << 4));
                R8_FLASH_CFG = 0x02;
            } else {
                R8_FLASH_SCK = R8_FLASH_SCK & (~(1U << 4));
                R8_FLASH_CFG = 0x07;
            }
        } else {
            if ((clk & 0x1FU) < 8U) {
                R8_FLASH_SCK = R8_FLASH_SCK & (~(1U << 4));
                R8_FLASH_CFG = 0x51;
            } else {
                R8_FLASH_SCK = R8_FLASH_SCK & (~(1U << 4));
                R8_FLASH_CFG = 0x57;
            }
        }

        R16_CLK_SYS_CFG = clk | 0xC0;
        R16_CLK_SYS_CFG = clk;

        if ((clk & RB_OSC32M_SEL) != 0U) {
            if (((R8_GLOB_CFG_INFO & RB_CFG_DEBUG_EN) == 0U) &&
                ((R8_GLOB_CFG_INFO & RB_CFG_ROM_READ) == 0U) &&
                ((R8_SAFE_DEBUG_CTRL & RB_DEBUG_DIS) != 0U)) {
                R8_HFCK_PWR_CTRL &= ~RB_CLK_RC16M_PON;
            }
        } else {
            R8_HFCK_PWR_CTRL &= ~RB_CLK_XT32M_PON;
        }
    }

    R8_SAFE_MODE_CTRL |= RB_SAFE_AUTO_EN;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    /* 主频已改变，重算节拍时基的重装值 */
    (void)HAL_InitTick(HAL_TICK_PRIORITY);

    return HAL_OK;
}

/**
 * @brief  读取当前系统时钟频率（Hz）
 * @note   32K 主频时返回配置值 HAL_RCC_OSC32K_FREQ，不是实测值
 */
uint32_t HAL_RCC_GetSysClockFreq(void)
{
    uint32_t cfg = R16_CLK_SYS_CFG;

    if ((cfg & RB_CLK_SYS_MOD) == RB_CLK_SYS_MOD) {
        return HAL_RCC_OSC32K_FREQ;
    }

    if ((cfg & RB_XROM_SCLK_SEL) != 0U) {
        if ((cfg & 0x1FU) == 0U) {
            return ((cfg & RB_OSC32M_SEL) != 0U) ? 32000000U : 16000000U;
        }

        return 312000000U / (cfg & 0x1FU);
    }

    if ((cfg & RB_OSC32M_SEL) != 0U) {
        return 32000000U / (cfg & 0x1FU);
    }

    return 16000000U / (cfg & 0x1FU);
}

/**
 * @brief  HSE 晶体负载电容（R8_XT32M_TUNE bit7~4）
 */
void HAL_RCC_SetHSECapacitance(HAL_RCC_HSECapTypeDef cap)
{
    uint8_t tune = R8_XT32M_TUNE;

    tune = (uint8_t)((tune & 0x0FU) | ((uint8_t)cap << 4));

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32M_TUNE = tune;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  HSE 晶体偏置电流（R8_XT32M_TUNE bit1~0）
 */
void HAL_RCC_SetHSECurrent(HAL_RCC_HSECurrentTypeDef cur)
{
    uint8_t tune = R8_XT32M_TUNE;

    tune = (uint8_t)((tune & 0xFCU) | ((uint8_t)cur & 0x03U));

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32M_TUNE = tune;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  LSE 晶体负载电容（R8_XT32K_TUNE bit7~4）
 */
void HAL_RCC_SetLSECapacitance(HAL_RCC_LSECapTypeDef cap)
{
    uint8_t tune = R8_XT32K_TUNE;

    tune = (uint8_t)((tune & 0x0FU) | ((uint8_t)cap << 4));

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32K_TUNE = tune;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  LSE 晶体偏置电流（R8_XT32K_TUNE bit1~0）
 */
void HAL_RCC_SetLSECurrent(HAL_RCC_LSECurrentTypeDef cur)
{
    uint8_t tune = R8_XT32K_TUNE;

    tune = (uint8_t)((tune & 0xFCU) | ((uint8_t)cur & 0x03U));

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32K_TUNE = tune;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  LSI 偏置电流（R8_XT32K_TUNE bit3~2）
 */
void HAL_RCC_SetLSICurrent(HAL_RCC_LSICurrentTypeDef cur)
{
    uint8_t tune = R8_XT32K_TUNE;

    tune = (uint8_t)((tune & 0xF3U) | (((uint8_t)cur << 2) & 0x0CU));

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_XT32K_TUNE = tune;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  读取 RTC 32K 周期计数（双读校验）
 */
static uint32_t HAL_RCC_GetCycle32k(void)
{
    volatile uint32_t cnt;

    do {
        cnt = R32_RTC_CNT_32K;
    } while (cnt != R32_RTC_CNT_32K);

    return cnt;
}

/**
 * @brief  用主频校准内部 32K RC
 * @note   算法与 StdPeriph 的 Calibration_LSI 完全一致，仅替换安全访问/中断开关/常量名
 */
void HAL_RCC_CalibrateLSI(void)
{
    HAL_RCC_LSICalibLevelTypeDef cali_Lv = HAL_RCC_LSI_CALIB_64;
    uint64_t i;
    long long cnt_offset;
    uint8_t retry     = 0;
    uint8_t retry_all = 0;
    int32_t freq_sys;
    uint32_t cnt_32k = 0;
    uint32_t irqv    = 0;

    freq_sys = (int32_t)HAL_RCC_GetSysClockFreq();

    /* 打开 32K 滤波 */
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_CK32K_CONFIG &= ~RB_CLK_OSC32K_FILT;
    R8_CK32K_CONFIG |= RB_CLK_OSC32K_FILT;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    while (1) {
        /* 粗调：单周期计数 */
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_OSC_CAL_CTRL &= ~RB_OSC_CNT_TOTAL;
        R8_OSC_CAL_CTRL |= 1;
        HAL_SYS_SAFE_ACCESS_DISABLE();

        while (1) {
            HAL_SYS_SAFE_ACCESS_ENABLE();
            R8_OSC_CAL_CTRL |= RB_OSC_CNT_EN;
            R16_OSC_CAL_CNT |= RB_OSC_CAL_OV_CLR;
            R16_OSC_CAL_CNT |= RB_OSC_CAL_IF;
            HAL_SYS_SAFE_ACCESS_DISABLE();
            while ((R8_OSC_CAL_CTRL & RB_OSC_CNT_EN) == 0) {
                HAL_SYS_SAFE_ACCESS_ENABLE();
                R8_OSC_CAL_CTRL |= RB_OSC_CNT_EN;
                HAL_SYS_SAFE_ACCESS_DISABLE();
            }

            while (!(R8_OSC_CAL_CTRL & RB_OSC_CNT_HALT)) {
                /* 第一次捕获丢弃 */
            }

            irqv = HAL_SYS_DisableIrq();
            HAL_SYS_SAFE_ACCESS_ENABLE();
            R8_OSC_CAL_CTRL &= ~RB_OSC_CNT_EN;
            R8_OSC_CAL_CTRL |= RB_OSC_CNT_EN;
            R16_OSC_CAL_CNT |= RB_OSC_CAL_OV_CLR;
            R16_OSC_CAL_CNT |= RB_OSC_CAL_IF;
            HAL_SYS_SAFE_ACCESS_DISABLE();
            while ((R8_OSC_CAL_CTRL & RB_OSC_CNT_EN) == 0) {
                HAL_SYS_SAFE_ACCESS_ENABLE();
                R8_OSC_CAL_CTRL |= RB_OSC_CNT_EN;
                HAL_SYS_SAFE_ACCESS_DISABLE();
            }

            while ((R8_OSC_CAL_CTRL & RB_OSC_CNT_HALT) != 0) {
            }
            cnt_32k = HAL_RCC_GetCycle32k();
            while (HAL_RCC_GetCycle32k() == cnt_32k) {
            }
            R16_OSC_CAL_CNT |= RB_OSC_CAL_OV_CLR;
            HAL_SYS_RestoreIrq(irqv);
            while (!(R8_OSC_CAL_CTRL & RB_OSC_CNT_HALT)) {
            }

            i          = R16_OSC_CAL_CNT;
            cnt_offset = (long long)(i & 0x3FFFU) + (long long)R8_OSC_CAL_OV_CNT * 0x3FFFU - 2000 * (freq_sys / 1000) / HAL_RCC_OSC32KFQ;

            if (((cnt_offset > (-35 * (freq_sys / 1000) / 60000)) &&
                 (cnt_offset < (35 * (freq_sys / 1000) / 60000))) ||
                (retry > 2U)) {
                if (retry != 0U) {
                    break;
                }
            }

            retry++;
            cnt_offset = (cnt_offset > 0)
                             ? ((((cnt_offset * 2) / (70 * (freq_sys / 1000) / 60000)) + 1) / 2)
                             : ((((cnt_offset * 2) / (70 * (freq_sys / 1000) / 60000)) - 1) / 2);

            HAL_SYS_SAFE_ACCESS_ENABLE();
            R16_INT32K_TUNE = (uint16_t)(R16_INT32K_TUNE + cnt_offset);
            HAL_SYS_SAFE_ACCESS_DISABLE();
        }

        /* 细调：多周期计数，丢弃第一次捕获 */
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_OSC_CAL_CTRL &= ~RB_OSC_CNT_TOTAL;
        R8_OSC_CAL_CTRL |= cali_Lv;
        HAL_SYS_SAFE_ACCESS_DISABLE();
        while ((R8_OSC_CAL_CTRL & RB_OSC_CNT_TOTAL) != cali_Lv) {
            HAL_SYS_SAFE_ACCESS_ENABLE();
            R8_OSC_CAL_CTRL |= cali_Lv;
            HAL_SYS_SAFE_ACCESS_DISABLE();
        }

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_OSC_CAL_CTRL &= ~RB_OSC_CNT_EN;
        R8_OSC_CAL_CTRL |= RB_OSC_CNT_EN;
        R16_OSC_CAL_CNT |= RB_OSC_CAL_OV_CLR;
        R16_OSC_CAL_CNT |= RB_OSC_CAL_IF;
        HAL_SYS_SAFE_ACCESS_DISABLE();
        while ((R8_OSC_CAL_CTRL & RB_OSC_CNT_EN) == 0) {
            HAL_SYS_SAFE_ACCESS_ENABLE();
            R8_OSC_CAL_CTRL |= RB_OSC_CNT_EN;
            HAL_SYS_SAFE_ACCESS_DISABLE();
        }

        while (!(R8_OSC_CAL_CTRL & RB_OSC_CNT_HALT)) {
        }

        irqv = HAL_SYS_DisableIrq();
        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_OSC_CAL_CTRL &= ~RB_OSC_CNT_EN;
        R8_OSC_CAL_CTRL |= RB_OSC_CNT_EN;
        R16_OSC_CAL_CNT |= RB_OSC_CAL_OV_CLR;
        R16_OSC_CAL_CNT |= RB_OSC_CAL_IF;
        HAL_SYS_SAFE_ACCESS_DISABLE();
        while ((R8_OSC_CAL_CTRL & RB_OSC_CNT_EN) == 0) {
            HAL_SYS_SAFE_ACCESS_ENABLE();
            R8_OSC_CAL_CTRL |= RB_OSC_CNT_EN;
            HAL_SYS_SAFE_ACCESS_DISABLE();
        }

        while ((R8_OSC_CAL_CTRL & RB_OSC_CNT_HALT) != 0) {
        }
        cnt_32k = HAL_RCC_GetCycle32k();
        while (HAL_RCC_GetCycle32k() == cnt_32k) {
        }
        R16_OSC_CAL_CNT |= RB_OSC_CAL_OV_CLR;
        HAL_SYS_RestoreIrq(irqv);
        while (!(R8_OSC_CAL_CTRL & RB_OSC_CNT_HALT)) {
        }

        HAL_SYS_SAFE_ACCESS_ENABLE();
        R8_OSC_CAL_CTRL &= ~RB_OSC_CNT_EN;
        HAL_SYS_SAFE_ACCESS_DISABLE();

        i          = R16_OSC_CAL_CNT;
        cnt_offset = (long long)(i & 0x3FFFU) + (long long)R8_OSC_CAL_OV_CNT * 0x3FFFU - 4000 * (1 << ((cali_Lv <= HAL_RCC_LSI_CALIB_128) ? cali_Lv : (cali_Lv + 2))) * (freq_sys / 100000) / 256 * 100 / (HAL_RCC_OSC32KFQ / 256);
        cnt_offset = (cnt_offset > 0)
                         ? ((((cnt_offset * 2 * (100)) / (1141 * ((1 << ((cali_Lv <= HAL_RCC_LSI_CALIB_128) ? cali_Lv : (cali_Lv + 2))) / 8) * (freq_sys / 1000) / 60000)) + 1) / 2)
                         : ((((cnt_offset * 2 * (100)) / (1141 * ((1 << ((cali_Lv <= HAL_RCC_LSI_CALIB_128) ? cali_Lv : (cali_Lv + 2))) / 8) * (freq_sys / 1000) / 60000)) - 1) / 2);

        if ((cnt_offset > 0) && (((R16_INT32K_TUNE >> 5) + cnt_offset) > 0xFF)) {
            if (retry_all > 2U) {
                HAL_SYS_SAFE_ACCESS_ENABLE();
                R16_INT32K_TUNE |= (0xFFU << 5);
                HAL_SYS_SAFE_ACCESS_DISABLE();
                return;
            }

            HAL_SYS_SAFE_ACCESS_ENABLE();
            R16_INT32K_TUNE = (uint16_t)((R16_INT32K_TUNE & 0x1FU) | (0x7FU << 5));
            HAL_SYS_SAFE_ACCESS_DISABLE();
        } else if ((cnt_offset < 0) && ((R16_INT32K_TUNE >> 5) < (-cnt_offset))) {
            if (retry_all > 2U) {
                HAL_SYS_SAFE_ACCESS_ENABLE();
                R16_INT32K_TUNE &= 0x1FU;
                HAL_SYS_SAFE_ACCESS_DISABLE();
                return;
            }

            HAL_SYS_SAFE_ACCESS_ENABLE();
            R16_INT32K_TUNE = (uint16_t)((R16_INT32K_TUNE & 0x1FU) | (0x7FU << 5));
            HAL_SYS_SAFE_ACCESS_DISABLE();
        } else {
            HAL_SYS_SAFE_ACCESS_ENABLE();
            R16_INT32K_TUNE = (uint16_t)(R16_INT32K_TUNE + (cnt_offset << 5));
            HAL_SYS_SAFE_ACCESS_DISABLE();
            return;
        }

        retry_all++;
    }
}

/**
 * @brief  按 HAL_RCC_OSC32K_SOURCE 选择 32K 时钟源并配置其参数
 * @note   内部 RC 分支才需要校准；外部晶振分支不做校准
 */
void HAL_RCC_Config32KSource(void)
{
#if (HAL_RCC_OSC32K_SOURCE == 1)
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_CK32K_CONFIG &= ~(RB_CLK_OSC32K_XT | RB_CLK_XT32K_PON);
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_CK32K_CONFIG |= RB_CLK_INT32K_PON;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_RCC_SetLSICurrent(HAL_RCC_LSI_CURRENT);
    HAL_RCC_CalibrateLSI();
#else
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_CK32K_CONFIG &= ~RB_CLK_INT32K_PON;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_CK32K_CONFIG |= RB_CLK_OSC32K_XT | RB_CLK_XT32K_PON;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_RCC_SetLSECapacitance(HAL_RCC_LSE_CAPACITANCE);
    HAL_RCC_SetLSECurrent(HAL_RCC_LSE_CURRENT);
#endif
}
