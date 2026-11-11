/* 未定义 CH58x_USE_BLE_LIB 时本文件整体不参与编译，见 src/ch58x_hal_conf.h */
#include "ch58x_hal_conf.h"

#ifdef CH58x_USE_BLE_LIB

/**
 * @file    ch58x_hal_stack.h
 * @note    使用约定：
 *          1) 本模块是 libCH58xBLE.a 的底座。BLE 与 RF-PHY 用的是同一个库、同一套 TMOS、
 *             同一个 LLE 中断入口，所以放 Protocol/stack 而不是 Drivers/CH58x_HAL_Driver。
 *          2) 调用顺序固定：HAL_Init() → HAL_STACK_Init() → 各外设 Init → 主循环。
 *          3) 主循环里必须反复调用 HAL_STACK_Process()，否则 TMOS 任务和射频回调都不会派发。
 *          4) 不用 BLE 也不用 RF 的工程，把整个 Protocol/ 目录排除即可，HAL 层不受影响。
 *
 *          5) RF-only 与 BLE 只差两处：
 *             - CalibPeriodMs 必须设 HAL_STACK_CALIB_OFF。周期校准任务里的 BLE_RegInit()
 *               会关闭 RF 并改写射频寄存器，而 RF-PHY 绕过 LLE 调度器、随时可能正在收发，
 *               校准插进来就会丢包。
 *             - SNV 五项全填 0 / NULL，并把 Protocol/ble 整个目录排除（RF 没有绑定信息）。
 *
 *          6) 温度漂移校准（tsCB）是强制的，不提供开关：RF 频偏全靠它拉回来，私有协议
 *             没有连接事件兜底，更需要。内部 32K RC 校准（rcCB）按 HAL_RCC_OSC32K_SOURCE
 *             编译期决定 —— 用外部 32.768kHz 晶振时内部 RC 不参与任何定时，校准纯属浪费。
 */
#ifndef __CH58X_HAL_STACK_H
#define __CH58X_HAL_STACK_H

#ifdef __cplusplus
extern "C" {
#endif

/* ch58x_hal_def.h 必须先于 CH58xBLE_LIB.h：前者通过 <string.h> 先把 NULL 定义成
 * ((void*)0)，后者只在未定义时才补 #define NULL 0，顺序反了会互相打架。
 */
#include "ch58x_hal_def.h"
#include "CH58xBLE_LIB.h"

/* ===================== 默认值 =====================
 * 结构体里填 0 一律表示「用这里的默认值」，唯一例外是 PeripheralNumber /
 * CentralNumber —— 那一对全填 0 才走默认，只填一个表示只做那一个角色。
 */

/** @brief 协议栈堆下限，库内部按此校验 */
#define HAL_STACK_MEM_LEN_MIN 4096U
/** @brief 协议栈堆默认值（厂商默认 6K） */
#define HAL_STACK_MEM_LEN_DEFAULT 6144U
/** @brief 单包最大长度；ATT_MTU = 本值 - 4，开 SC 时必须 > 69 */
#define HAL_STACK_BUF_MAX_LEN 27U
/** @brief 控制器缓存的收发包数量，必须大于连接数 */
#define HAL_STACK_BUF_NUMBER 5U
/** @brief 单个连接事件最多发几个包 */
#define HAL_STACK_TX_NUM_EVENT 1U
/** @brief 未指定角色时的从机数量 */
#define HAL_STACK_PERIPHERAL_DEFAULT 1U
/** @brief 未指定角色时的主机数量 */
#define HAL_STACK_CENTRAL_DEFAULT 3U
/** @brief SNV 块大小默认值，单位字节 */
#define HAL_STACK_SNV_BLOCK_DEFAULT 256U
/** @brief SNV 块数量默认值 */
#define HAL_STACK_SNV_NUM_DEFAULT 1U

/** @brief 周期校准默认周期，单位 ms（与厂商 BLE_CALIBRATION_PERIOD 一致） */
#define HAL_STACK_CALIB_PERIOD_MS 120000U

/** @brief CalibPeriodMs 填本值表示完全不做周期校准，RF-only 必须用 */
#define HAL_STACK_CALIB_OFF 0xFFFFFFFFU

/** @brief 周期校准首次触发延时，单位 ms（与厂商一致，留出启动余量） */
#define HAL_STACK_CALIB_FIRST_MS 800U

/* ===================== 类型 ===================== */

/**
 * @brief  底座生命周期状态
 */
typedef enum {
    HAL_STACK_STATE_RESET = 0, /**< 未初始化或已 DeInit */
    HAL_STACK_STATE_READY      /**< BLE_LibInit 成功，TMOS 可派发 */
} HAL_STACK_StateTypeDef;

/**
 * @brief  初始化参数
 * @note   全部字段都可以填 0 / NULL 走默认值，最简用法就是定义一个全零的实例。
 */
typedef struct
{
    uint32_t MemAddr;   /**< 协议栈堆起始地址，0 = 用模块内部的 6K 静态堆 */
    uint16_t MemLen;    /**< 协议栈堆大小，0 = HAL_STACK_MEM_LEN_DEFAULT */
    uint16_t BufMaxLen; /**< 单包最大长度，0 = HAL_STACK_BUF_MAX_LEN */
    uint8_t BufNumber;  /**< 收发缓存包数，0 = HAL_STACK_BUF_NUMBER */
    uint8_t TxNumEvent; /**< 单个连接事件发包上限，0 = HAL_STACK_TX_NUM_EVENT */

    /** 发射功率，取 LL_TX_PWR_xxx_DBM；0 = LL_TX_PWR_0_DBM， ..., 4 = LL_TX_PWR_4_DBM，。
     *  RF-PHY 也用它 —— rfConfig_t 里没有功率字段，私有协议功率只能从这儿给。 */
    uint8_t TxPower;

    uint8_t PeripheralNumber; /**< 从机数量；与 CentralNumber 同时为 0 时走默认 */
    uint8_t CentralNumber;    /**< 主机数量；与 PeripheralNumber 同时为 0 时走默认 */

    /** 周期校准周期，单位 ms。0 = HAL_STACK_CALIB_PERIOD_MS，
     *  HAL_STACK_CALIB_OFF = 不做周期校准（RF-only 必须填这个）。 */
    uint32_t CalibPeriodMs;

    /** 睡眠开关。ENABLE = 把 idleCB 接到 HAL_PWR_EnterSleepUntil。
     *  睡觉得有 RTC 计数器和 RTC 唤醒源，HAL_STACK_Init 会按需调 HAL_RTC_Init()。
     *  USB 供电模式、调试模式要关掉它（否则主循环空转时也进睡眠）。 */
    FunctionalState SleepEnable;

    /* ---- 绑定信息存储（DataFlash 偏移）----
     * SNVAddr 为 0 或任一回调为 NULL 都表示不做 SNV，库不会保存绑定信息。
     * 要 SNV 就传 HAL_SNV_ADDR / HAL_SNV_BLOCK / HAL_SNV_NUM /
     * HAL_SNV_Read / HAL_SNV_Write（来自 Protocol/ble）；
     * 不要 SNV 就全填 0 / NULL，并把 Protocol/ble 整个目录排除，
     * 那样 ch58x_hal_snv.c 里那块 4K 整块擦写缓冲才真省得下来。
     */
    uint32_t SNVAddr;
    uint16_t SNVBlock;
    uint8_t SNVNum;
    uint32_t (*ReadFlashCB)(uint32_t addr, uint32_t num, uint32_t *pBuf);
    uint32_t (*WriteFlashCB)(uint32_t addr, uint32_t num, uint32_t *pBuf);

    /** 自定义 MAC（6 字节，库要求小端序）；NULL = 读芯片出厂 MAC */
    const uint8_t *MacAddr;
} HAL_STACK_InitTypeDef;

/**
 * @brief  句柄，由应用定义实例并传入 API
 */
typedef struct
{
    HAL_STACK_InitTypeDef Init; /**< 初始化参数；Init 成功后会被归一化成实际生效值 */
    tmosTaskID TmosTaskId;      /**< 校准任务的 TMOS 任务号 */
    volatile uint8_t State;     /**< HAL_STACK_STATE_xxx */
} HAL_STACK_HandleTypeDef;

/* ===================== API ===================== */

HAL_StatusTypeDef HAL_STACK_Init(HAL_STACK_HandleTypeDef *hstack);
HAL_StatusTypeDef HAL_STACK_DeInit(HAL_STACK_HandleTypeDef *hstack);

/**
 * @brief  跑一次协议栈调度，需要在应用主循环里反复调用
 * @note   内部就是 TMOS_SystemProcess()，会一直处理到没有待办事件为止，
 *         所以应用自己的轮询代码放在它后面，跑在空闲窗口里。
 */
void HAL_STACK_Process(void);

/* ===================== 弱回调 ===================== */

/**
 * @brief  库内部状态错误回调
 * @param  code   库定义的错误码
 * @param  status 附加状态
 * @note   可能在中断上下文被调用，内部不要做耗时操作或调用阻塞 API。
 */
void HAL_STACK_StatusCallback(uint8_t code, uint32_t status);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_STACK_H */

#else /* CH58x_USE_BLE_LIB */

#error "CH58x_USE_BLE_LIB 未定义：Protocol/stack 不参与编译。要用 BLE/RF 请到 src/ch58x_hal_conf.h 恢复定义。"

#endif /* CH58x_USE_BLE_LIB */
