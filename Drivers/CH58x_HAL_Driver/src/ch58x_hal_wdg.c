#include "ch58x_hal_wdg.h"

#include "ch58x_hal_sys.h"

/* R32_IWDG_KR 的键值 */
#define HAL_IWDG_KEY_UNPROTECT 0x5555U
#define HAL_IWDG_KEY_RELOAD    0xAAAAU
#define HAL_IWDG_KEY_START     0xCCCCU

/* R32_IWDG_CFG 位域 */
#define HAL_IWDG_CFG_RLD_MASK  0x0FFFU
#define HAL_IWDG_CFG_PR_MASK   0x7000U
#define HAL_IWDG_CFG_PR_POS    12U
#define HAL_IWDG_CFG_CORE_STOP (1U << 29)

/**
 * @brief  配置寄存器是否处于写保护
 */
static uint8_t HAL_IWDG_IsWriteProtected(void)
{
    return (uint8_t)((R32_IWDG_CFG >> 30) & 0x01U);
}

/* ============================ WWDG ============================ */

/**
 * @brief  装载 WWDG 计数值（喂狗）
 * @param  counter 计数值，计数到 0 溢出
 */
void HAL_WWDG_SetCounter(uint8_t counter)
{
    R8_WDOG_COUNT = counter;
}

/**
 * @brief  读取 WWDG 当前计数值
 */
uint8_t HAL_WWDG_GetCounter(void)
{
    return R8_WDOG_COUNT;
}

/**
 * @brief  溢出时是否复位
 */
void HAL_WWDG_EnableReset(FunctionalState s)
{
    uint8_t ctrl = R8_RST_WDOG_CTRL;

    ctrl = (s == DISABLE) ? (uint8_t)(ctrl & ~RB_WDOG_RST_EN)
                          : (uint8_t)(ctrl | RB_WDOG_RST_EN);

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RST_WDOG_CTRL = ctrl;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  溢出时是否产生中断
 */
void HAL_WWDG_EnableIT(FunctionalState s)
{
    uint8_t ctrl = R8_RST_WDOG_CTRL;

    ctrl = (s == DISABLE) ? (uint8_t)(ctrl & ~RB_WDOG_INT_EN)
                          : (uint8_t)(ctrl | RB_WDOG_INT_EN);

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RST_WDOG_CTRL = ctrl;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  读取溢出标志
 * @retval 0=未溢出，1=已溢出
 */
uint8_t HAL_WWDG_GetFlag(void)
{
    return (uint8_t)((R8_RST_WDOG_CTRL & RB_WDOG_INT_FLAG) != 0U);
}

/**
 * @brief  清除溢出标志
 */
void HAL_WWDG_ClearFlag(void)
{
    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RST_WDOG_CTRL |= RB_WDOG_INT_FLAG;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  WWDG 溢出回调，默认空实现
 */
__weak void HAL_WWDG_Callback(void)
{
}

/**
 * @brief  WDOG/BAT 共用中断处理，需在 WDOG_BAT_IRQHandler 中调用
 * @note   本函数只处理 WWDG；电池电压事件由后续电源/ADC 模块处理
 */
__HIGH_CODE
void HAL_WDG_IRQHandler(void)
{
    if (HAL_WWDG_GetFlag() != 0U) {
        HAL_WWDG_ClearFlag();
        HAL_WWDG_Callback();
    }
}

/* ============================ IWDG ============================ */

/**
 * @brief  启动独立看门狗并配置预分频与重装值
 * @param  prescaler 分频系数
 * @param  reload    重装值，最大 0xFFF
 * @note   复位类型被设为上电复位，避免全局复位时 LSI 被关掉而失效
 */
HAL_StatusTypeDef HAL_IWDG_Init(HAL_IWDG_PrescalerTypeDef prescaler, uint16_t reload)
{
    uint32_t cfg;

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_SAFE_LRST_CTRL |= RB_IWDG_RST_EN;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    R32_IWDG_KR = HAL_IWDG_KEY_START;
    R32_IWDG_KR = HAL_IWDG_KEY_UNPROTECT;

    if (HAL_IWDG_IsWriteProtected() != 0U) {
        return HAL_ERROR;
    }

    /* 分频与重装值一次写入，避免两次写 CFG 之间重新上锁 */
    cfg = R32_IWDG_CFG;
    cfg &= ~(HAL_IWDG_CFG_PR_MASK | HAL_IWDG_CFG_RLD_MASK);
    cfg |= (((uint32_t)prescaler << HAL_IWDG_CFG_PR_POS) | ((uint32_t)reload & HAL_IWDG_CFG_RLD_MASK));
    R32_IWDG_CFG = cfg;

    return HAL_OK;
}

/**
 * @brief  修改重装值
 */
HAL_StatusTypeDef HAL_IWDG_SetReload(uint16_t reload)
{
    uint32_t cfg;

    R32_IWDG_KR = HAL_IWDG_KEY_UNPROTECT;
    if (HAL_IWDG_IsWriteProtected() != 0U) {
        return HAL_ERROR;
    }

    cfg = R32_IWDG_CFG;
    cfg &= ~HAL_IWDG_CFG_RLD_MASK;
    cfg |= ((uint32_t)reload & HAL_IWDG_CFG_RLD_MASK);
    R32_IWDG_CFG = cfg;

    return HAL_OK;
}

/**
 * @brief  调试模式下内核暂停时是否同时暂停计数
 */
HAL_StatusTypeDef HAL_IWDG_EnableDebugStop(FunctionalState s)
{
    uint32_t cfg;

    R32_IWDG_KR = HAL_IWDG_KEY_UNPROTECT;
    if (HAL_IWDG_IsWriteProtected() != 0U) {
        return HAL_ERROR;
    }

    cfg          = R32_IWDG_CFG;
    cfg          = (s == DISABLE) ? (cfg & ~HAL_IWDG_CFG_CORE_STOP) : (cfg | HAL_IWDG_CFG_CORE_STOP);
    R32_IWDG_CFG = cfg;

    return HAL_OK;
}

/**
 * @brief  喂狗
 */
void HAL_IWDG_Feed(void)
{
    R32_IWDG_KR = HAL_IWDG_KEY_RELOAD;
}

/**
 * @brief  读取当前计数值
 */
uint32_t HAL_IWDG_GetCounter(void)
{
    return ((R32_IWDG_CFG >> 16) & HAL_IWDG_CFG_RLD_MASK);
}
