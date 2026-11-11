#include "ch58x_hal_conf.h"

#ifdef CH58x_USE_BLE_LIB

#include "ch58x_hal_snv.h"
#include "ch58x_hal_flash.h"

/* CH584X 改一个字段要整块擦写，工作区按 4K 常驻；其余型号走原地擦写，不会用到 */
static uint32_t s_snvBlock[HAL_FLASH_EEPROM_BLOCK_SIZE / 4U];

/**
 * @brief  读 SNV 区，供 BLE 库 readFlashCB 使用
 * @param  addr DataFlash 偏移
 * @param  num  单位是 4 字节
 * @return 0 成功，非 0 失败
 */
uint32_t HAL_SNV_Read(uint32_t addr, uint32_t num, uint32_t *buf)
{
    return (uint32_t)HAL_FLASH_EEPROM_Read(addr, buf, num * 4U);
}

/**
 * @brief  写 SNV 区，供 BLE 库 writeFlashCB 使用
 * @param  addr DataFlash 偏移
 * @param  num  单位是 4 字节
 * @return 0 成功，非 0 失败
 * @note   CH584X 的 DataFlash 最小擦除单位是 4K，内部会整块读出、修改后整块写回。
 */
uint32_t HAL_SNV_Write(uint32_t addr, uint32_t num, uint32_t *buf)
{
    uint32_t len = num * 4U;
    uint32_t base;
    uint32_t offset;

    if ((buf == NULL) || (len == 0U))
    {
        return 1U;
    }

    if (HAL_FLASH_GetEraseGranularity() == HAL_FLASH_EEPROM_PAGE_SIZE)
    {
        if (HAL_FLASH_EEPROM_Erase(addr, len) != HAL_OK)
        {
            return 1U;
        }

        return (HAL_FLASH_EEPROM_Program(addr, buf, len) == HAL_OK) ? 0U : 1U;
    }

    base   = addr & ~(HAL_FLASH_EEPROM_BLOCK_SIZE - 1U);
    offset = addr - base;

    if ((offset + len) > HAL_FLASH_EEPROM_BLOCK_SIZE)
    {
        return 1U;
    }

    if (HAL_FLASH_EEPROM_Read(base, s_snvBlock, HAL_FLASH_EEPROM_BLOCK_SIZE) != HAL_OK)
    {
        return 1U;
    }

    memcpy((uint8_t *)s_snvBlock + offset, buf, len);

    if (HAL_FLASH_EEPROM_Erase(base, HAL_FLASH_EEPROM_BLOCK_SIZE) != HAL_OK)
    {
        return 1U;
    }

    return (HAL_FLASH_EEPROM_Program(base, s_snvBlock, HAL_FLASH_EEPROM_BLOCK_SIZE) == HAL_OK) ? 0U : 1U;
}

#else /* CH58x_USE_BLE_LIB */

/* 未启用闭源库：整个文件不参与编译，留一个类型避免空翻译单元告警 */
typedef int ch58x_hal_snv_unused_translation_unit_t;

#endif /* CH58x_USE_BLE_LIB */
