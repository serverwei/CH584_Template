#ifndef __CH58X_HAL_SYS_H
#define __CH58X_HAL_SYS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

void HAL_SYS_DelayUs(uint32_t us);
void HAL_SYS_DelayMs(uint32_t ms);

/* 保留原符号名：startup_CH585.S 用 `jal highcode_init` 直接引用，
 * 它跑在 .highcode_init 段（RAM），早于任何 HAL 初始化。
 */
void highcode_init(void);

uint32_t HAL_SYS_GetCycleCount(void);
uint32_t HAL_SYS_DisableIrq(void);
void HAL_SYS_RestoreIrq(uint32_t status);
void HAL_SYS_SoftwareReset(void);

typedef void (*HAL_SYS_MachineModeCallbackTypeDef)(void);

/* ===================== 微处理器配置（CORECFGR）=====================
 * CSR 0xBC0，只能在机器模式下写，所以每次调用都要通过 SWI 切模式，
 * 期间全局中断被短暂关闭 —— 只在初始化时调，不要在中断里用。
 *
 * 注意：FETCH_MODE 的上电复位值本来就是「使能」，不存在「必须手动打开预取」。
 * 原先的 HAL_SYS_EnablePrefetch 名字与行为不符：它整体写 0x25，
 * 实际改的是 ROM 加速位和 MIE 映射位，还会顺手清掉 bit7 HF_NMI。已删除。
 */

/* CORECFGR[1:0] FETCH_MODE */
typedef enum {
    HAL_SYS_FETCH_OFF = 0, /* 预取关闭：功耗最低，性能下降 2~3 倍 */
    HAL_SYS_FETCH_ON  = 1  /* 预取使能：上电复位值，性能最强、功耗较高 */
} HAL_SYS_FetchModeTypeDef;

/* CORECFGR[3:2] ROM 区指令加速 */
typedef enum {
    HAL_SYS_ROM_ACCEL_NONE = 0x00, /* 都关闭 */
    HAL_SYS_ROM_ACCEL_JUMP = 0x04, /* bit2：跳转加速 */
    HAL_SYS_ROM_ACCEL_LOOP = 0x08, /* bit3：循环体≤128B 全加速、≤256B 部分加速 */
    HAL_SYS_ROM_ACCEL_ALL  = 0x0C  /* bit3 + bit2 */
} HAL_SYS_RomAccelTypeDef;

typedef struct
{
    HAL_SYS_FetchModeTypeDef FetchMode; /* [1:0] */
    HAL_SYS_RomAccelTypeDef RomAccel;   /* [3:2] */
    FunctionalState NestedIrq;          /* bit5：MIE 映射，分级中断的前提 */
} HAL_SYS_CoreCfgTypeDef;

/* 系统信息位。枚举值就是 R8_GLOB_CFG_INFO 的位序号（bit1 未定义），
 * 但 HAL_SYS_INFO_SAFE_ACCESS 是特例：它不取该寄存器，
 * 而是读 R8_SAFE_ACCESS_SIG 的解锁状态。
 */
typedef enum {
    HAL_SYS_INFO_ROM_READ    = 0, /* bit0：Flash ROM 代码/数据读保护状态 */
    HAL_SYS_INFO_RESET_EN    = 2, /* bit2：手动复位引脚是否使能 */
    HAL_SYS_INFO_BOOT_EN     = 3, /* bit3：boot-loader 是否使能 */
    HAL_SYS_INFO_DEBUG_EN    = 4, /* bit4：调试接口是否使能 */
    HAL_SYS_INFO_LOADER      = 5, /* bit5：1 = 当前运行在 boot-loader */
    HAL_SYS_INFO_SAFE_ACCESS = 6  /* 特例：读安全访问解锁状态，非位序号 */
} HAL_SYS_InfoTypeDef;

/* 上次复位原因（R8_RESET_STATUS & RB_RESET_FLAG） */
typedef enum {
    HAL_SYS_RESET_SOFTWARE = 0,
    HAL_SYS_RESET_POWER_ON,
    HAL_SYS_RESET_WATCHDOG,
    HAL_SYS_RESET_MANUAL,
    HAL_SYS_RESET_WAKEUP_SOFTWARE,
    HAL_SYS_RESET_WAKEUP_POWER_DOWN,
    HAL_SYS_RESET_WAKEUP_WATCHDOG,
    HAL_SYS_RESET_WAKEUP_MANUAL
} HAL_SYS_ResetStatusTypeDef;

uint8_t HAL_SYS_GetInfoStatus(HAL_SYS_InfoTypeDef info);
HAL_SYS_ResetStatusTypeDef HAL_SYS_GetLastResetStatus(void);
void HAL_SYS_SetResetKeepValue(uint8_t value);
uint8_t HAL_SYS_GetResetKeepValue(void);
void HAL_SYS_MachineModeCall(HAL_SYS_MachineModeCallbackTypeDef func);

HAL_StatusTypeDef HAL_SYS_SetCoreConfig(const HAL_SYS_CoreCfgTypeDef *cfg);
uint32_t HAL_SYS_GetCoreConfig(void);

#define HAL_SYS_GetChipID()   R8_CHIP_ID
#define HAL_SYS_GetAccessID() R8_SAFE_ACCESS_ID

/* 安全访问模式：必须成对使用，且位于同一作用域 */
#define HAL_SYS_SAFE_ACCESS_ENABLE()           \
    do {                                       \
        volatile uint32_t mpie_mie;            \
        mpie_mie = __risc_v_disable_irq();     \
        SAFEOPERATE;                           \
        R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1; \
        R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2; \
        SAFEOPERATE;

#define HAL_SYS_SAFE_ACCESS_DISABLE() \
    R8_SAFE_ACCESS_SIG = 0;           \
    __risc_v_enable_irq(mpie_mie);    \
    SAFEOPERATE;                      \
    }                                 \
    while (0)

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_SYS_H */
