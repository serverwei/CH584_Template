#include "ch58x_hal_sys.h"

#include "ISP585.h"

/**
 * @brief  把系统时钟切到内部 RC16M + PLL 62.4MHz
 * @note   由 startup_CH585.S 在搬运完 .highcode_init 段之后用 `jal` 直接调用，
 *         符号名不能改。此时时钟还没配置、外设也没起来，不能用任何 HAL API。
 *         整段必须夹在 SAFE_ACCESS 宏对里，中途不能 return。
 *         原先在 StdPeriphDriver/CH58x_sys.c，迁到 HAL 后不再依赖 CH58x_clk.h：
 *         CLK_SOURCE_HSI_PLL_62_4MHz 换成等值的 HAL_RCC_CLK_HSI_PLL_62_4M。
 */
__attribute__((section(".highcode_init"))) void highcode_init(void)
{
    R32_SAFE_MODE_CTRL |= RB_XROM_312M_SEL;
    R8_SAFE_MODE_CTRL &= ~RB_SAFE_AUTO_EN;

    HAL_SYS_SAFE_ACCESS_ENABLE();

    R32_MISC_CTRL |= 5 | (3 << 25);
    R8_PLL_CONFIG &= ~(1 << 5);
    R8_HFCK_PWR_CTRL |= RB_CLK_RC16M_PON | RB_CLK_PLL_PON;
    R16_CLK_SYS_CFG = HAL_RCC_CLK_HSI_PLL_62_4M; /* 624 / 2 / 5 = 62.4MHz */
    R8_FLASH_SCK    = R8_FLASH_SCK & (~(1 << 4));
    R8_FLASH_CFG    = 0x02;
    R8_XT32M_TUNE   = (R8_XT32M_TUNE & (~0x03)) | 0x01;
    R8_CK32K_CONFIG |= RB_CLK_INT32K_PON;

    R8_SAFE_MODE_CTRL |= RB_SAFE_AUTO_EN; /* 与厂商原实现一致：在安全区内恢复 */

    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/**
 * @brief  微秒级忙等
 * @param  us 延时微秒数，0 表示不延时
 * @note   分档系数与 StdPeriph 的 mDelayuS 一致，依据 HAL_RCC_SYSCLK_HZ
 */
__HIGH_CODE
void HAL_SYS_DelayUs(uint32_t us)
{
    uint32_t i;

    if (us == 0U) {
        return;
    }

#if (HAL_RCC_SYSCLK_HZ == 78000000)
    i = us * 13U;
#elif (HAL_RCC_SYSCLK_HZ == 62400000)
    i = (us * 78U) / 5U;
#elif (HAL_RCC_SYSCLK_HZ == 52000000)
    i = us * 13U;
#elif (HAL_RCC_SYSCLK_HZ == 39000000)
    i = (us * 13U) >> 1;
#elif (HAL_RCC_SYSCLK_HZ == 26000000)
    i = (us * 13U) >> 1;
#elif (HAL_RCC_SYSCLK_HZ == 24000000)
    i = us * 6U;
#elif (HAL_RCC_SYSCLK_HZ == 19500000)
    i = (us * 13U) >> 2;
#elif (HAL_RCC_SYSCLK_HZ == 32000000)
    i = us << 3;
#elif (HAL_RCC_SYSCLK_HZ == 16000000)
    i = us << 2;
#elif (HAL_RCC_SYSCLK_HZ == 13000000)
    i = (us * 13U) >> 2;
#elif (HAL_RCC_SYSCLK_HZ == 8000000)
    i = us << 1;
#elif (HAL_RCC_SYSCLK_HZ == 4000000)
    i = us;
#elif (HAL_RCC_SYSCLK_HZ == 2000000)
    i = us >> 1;
#elif (HAL_RCC_SYSCLK_HZ == 1000000)
    i = us >> 2;
#else
    i = us;
#endif

    if (i == 0U) {
        return;
    }

#if ((HAL_RCC_SYSCLK_HZ == 78000000) || \
     (HAL_RCC_SYSCLK_HZ == 39000000) || \
     (HAL_RCC_SYSCLK_HZ == 19500000))
    while (--i) {
        __nop();
    }
#else
    do {
        __nop();
    } while (--i);
#endif
}

/**
 * @brief  毫秒级忙等
 * @param  ms 延时毫秒数，0 表示不延时
 */
__HIGH_CODE
void HAL_SYS_DelayMs(uint32_t ms)
{
    if (ms == 0U) {
        return;
    }

    do {
        HAL_SYS_DelayUs(1000U);
    } while (--ms);
}

/**
 * @brief  读取 SysTick 当前计数值
 * @note   单位是 1/HAL_RCC_SYSCLK_HZ，不是毫秒
 */
uint32_t HAL_SYS_GetCycleCount(void)
{
    return SysTick->CNTL;
}

/**
 * @brief  关闭全部中断并返回原中断使能状态
 */
__HIGH_CODE
uint32_t HAL_SYS_DisableIrq(void)
{
    uint32_t status;

    status        = (PFIC->ISR[0] >> 8) | (PFIC->ISR[1] << 24);
    PFIC->IRER[0] = 0xFFFFFFFFU;
    PFIC->IRER[1] = 0xFFFFFFFFU;
    return status;
}

/**
 * @brief  按 HAL_SYS_DisableIrq 的返回值恢复中断
 */
__HIGH_CODE
void HAL_SYS_RestoreIrq(uint32_t status)
{
    PFIC->IENR[0] = (status << 8);
    PFIC->IENR[1] = (status >> 24);
}

/**
 * @brief  软件复位
 */
__HIGH_CODE
void HAL_SYS_SoftwareReset(void)
{
    FLASH_ROM_SW_RESET();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RST_WDOG_CTRL |= RB_SOFTWARE_RESET;
    HAL_SYS_SAFE_ACCESS_DISABLE();
}

static volatile HAL_SYS_MachineModeCallbackTypeDef s_machineModeFunc;

extern uint32_t _vector_base[];

/**
 * @brief  机器模式调用的 SWI 中断处理
 */
__HIGH_CODE
__INTERRUPT
static void HAL_SYS_MachineModeIrq(void)
{
    if (s_machineModeFunc != NULL) {
        s_machineModeFunc();
        s_machineModeFunc = NULL;
    }
}

/* CORECFGR（微处理器配置寄存器）的位定义，见手册 §3.4，CSR 地址 0xBC0。
 *
 * 注意：core_riscv.h 的 read_csr / write_csr 用 `#reg` 把参数直接字符串化，
 * 不做宏展开，所以调用处必须写裸字面量 `0xbc0` —— 传宏名会原样变成
 * "HAL_SYS_xxx_ADDR" 字符串，汇编器报 unknown CSR。
 */
#define HAL_SYS_CFG_FETCH     0x03U /* [1:0] FETCH_MODE */
#define HAL_SYS_CFG_ROM_ACCEL 0x0CU /* [3:2] ROM_JUMP_ACC + ROM_LOOP_ACC */
#define HAL_SYS_CFG_NEST_IRQ  0x20U /* bit5  IE_REMAP_EN */
/* bit7 HF_NMI 不在本接口范围内，读-改-写会原样保留 */

static uint32_t s_coreCfgMask;
static uint32_t s_coreCfgValue;
static uint32_t s_coreCfgRead;

/**
 * @brief  机器模式下按 mask 更新 CORECFGR
 * @note   读-改-写：mask 之外的位（如 bit7 HF_NMI）保持不变
 */
static void HAL_SYS_CoreCfgWriteFunc(void)
{
    uint32_t cfg = read_csr(0xbc0);

    cfg &= ~s_coreCfgMask;
    cfg |= (s_coreCfgValue & s_coreCfgMask);

    write_csr(0xbc0, cfg);
}

/**
 * @brief  机器模式下读 CORECFGR
 */
static void HAL_SYS_CoreCfgReadFunc(void)
{
    s_coreCfgRead = read_csr(0xbc0);
}

/**
 * @brief  关闭全部中断后通过 SWI 执行一次机器模式代码
 * @param  func 待执行函数，执行完即被清空
 */
__HIGH_CODE
void HAL_SYS_MachineModeCall(HAL_SYS_MachineModeCallbackTypeDef func)
{
    uint32_t irqStatus;
    uint32_t vector;

    irqStatus = HAL_SYS_DisableIrq();

    vector                 = _vector_base[SWI_IRQn];
    _vector_base[SWI_IRQn] = (uint32_t)HAL_SYS_MachineModeIrq;
    s_machineModeFunc      = func;

    PFIC_EnableIRQ(SWI_IRQn);
    PFIC_SetPendingIRQ(SWI_IRQn);

    while (s_machineModeFunc != NULL) {
    }

    PFIC_DisableIRQ(SWI_IRQn);
    _vector_base[SWI_IRQn] = vector;

    HAL_SYS_RestoreIrq(irqStatus);
}

/**
 * @brief  配置 CORECFGR（取指模式 / ROM 加速 / MIE 映射）
 * @param  cfg 配置内容
 * @retval HAL_OK 成功；HAL_ERROR cfg 为空或字段越界
 * @note   CORECFGR 只能在机器模式下写，内部通过 SWI 切换，期间全局中断被
 *         短暂关闭。只在初始化时调一次，不要在中断里调。
 *         只改 [5:0] 里的配置位，bit7 HF_NMI 等其它位原样保留。
 */
HAL_StatusTypeDef HAL_SYS_SetCoreConfig(const HAL_SYS_CoreCfgTypeDef *cfg)
{
    uint32_t value = 0U;

    if (cfg == NULL) {
        return HAL_ERROR;
    }
    if ((uint32_t)cfg->FetchMode > (uint32_t)HAL_SYS_FETCH_ON) {
        return HAL_ERROR;
    }
    if (((uint32_t)cfg->RomAccel & ~HAL_SYS_CFG_ROM_ACCEL) != 0U) {
        return HAL_ERROR;
    }

    value |= (uint32_t)cfg->FetchMode;
    value |= ((uint32_t)cfg->RomAccel & HAL_SYS_CFG_ROM_ACCEL);
    if (cfg->NestedIrq != DISABLE) {
        value |= HAL_SYS_CFG_NEST_IRQ;
    }

    s_coreCfgMask  = HAL_SYS_CFG_FETCH | HAL_SYS_CFG_ROM_ACCEL | HAL_SYS_CFG_NEST_IRQ;
    s_coreCfgValue = value;

    HAL_SYS_MachineModeCall(HAL_SYS_CoreCfgWriteFunc);

    return HAL_OK;
}

/**
 * @brief  读回 CORECFGR
 * @retval 寄存器原值，位定义见手册 §3.4 CORECFGR
 * @note   同样需要机器模式，会短暂关中断。
 */
uint32_t HAL_SYS_GetCoreConfig(void)
{
    HAL_SYS_MachineModeCall(HAL_SYS_CoreCfgReadFunc);

    return s_coreCfgRead;
}

/**
 * @brief  读取系统信息位
 */
uint8_t HAL_SYS_GetInfoStatus(HAL_SYS_InfoTypeDef info)
{
    if (info == HAL_SYS_INFO_SAFE_ACCESS) {
        return (uint8_t)(R8_SAFE_ACCESS_SIG & RB_SAFE_ACC_ACT);
    }

    return (uint8_t)(R8_GLOB_CFG_INFO & (1U << (uint32_t)info));
}

/**
 * @brief  读取上次复位原因
 */
HAL_SYS_ResetStatusTypeDef HAL_SYS_GetLastResetStatus(void)
{
    return (HAL_SYS_ResetStatusTypeDef)(R8_RESET_STATUS & RB_RESET_FLAG);
}

/**
 * @brief  写复位保持寄存器（不受软/看门狗/唤醒复位影响）
 */
void HAL_SYS_SetResetKeepValue(uint8_t value)
{
    R8_GLOB_RESET_KEEP = value;
}

/**
 * @brief  读复位保持寄存器
 */
uint8_t HAL_SYS_GetResetKeepValue(void)
{
    return R8_GLOB_RESET_KEEP;
}
