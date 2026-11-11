/* 未定义 CH58x_USE_BLE_LIB 时本文件整体不参与编译，见 src/ch58x_hal_conf.h */
#include "ch58x_hal_conf.h"

#ifdef CH58x_USE_BLE_LIB

#ifndef __CH58X_HAL_SNV_H
#define __CH58X_HAL_SNV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* SNV 存储区，位于 DataFlash 内，与 WCH 原 CONFIG.h 的默认布局一致 */
#ifndef HAL_SNV_ADDR
#define HAL_SNV_ADDR        0x7000U
#endif
#ifndef HAL_SNV_BLOCK
#define HAL_SNV_BLOCK       256U
#endif
#ifndef HAL_SNV_NUM
#define HAL_SNV_NUM         1U
#endif

#define HAL_SNV_SIZE    (HAL_SNV_BLOCK * HAL_SNV_NUM)

uint32_t HAL_SNV_Read(uint32_t addr, uint32_t num, uint32_t *buf);
uint32_t HAL_SNV_Write(uint32_t addr, uint32_t num, uint32_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_SNV_H */

#else /* CH58x_USE_BLE_LIB */

#error "CH58x_USE_BLE_LIB 未定义：Protocol/ble 不参与编译。要用 BLE 请到 src/ch58x_hal_conf.h 恢复定义。"

#endif /* CH58x_USE_BLE_LIB */
