#ifndef __CH58X_HAL_FLASH_H
#define __CH58X_HAL_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch58x_hal_def.h"

/* Flash ROM（代码区） */
#define HAL_FLASH_ROM_BLOCK_SIZE 4096U
#define HAL_FLASH_ROM_WRITE_UNIT 4U

/* DataFlash（EEPROM），地址为自 0 起的偏移 */
#define HAL_FLASH_EEPROM_PAGE_SIZE  256U
#define HAL_FLASH_EEPROM_BLOCK_SIZE 4096U
#define HAL_FLASH_EEPROM_MAX_SIZE   0x8000U

/* 用户配置字：绝对地址 0x14 存放「配置值低 24 位 + 低 8 位反码」 */
#define HAL_FLASH_OPTION_ADDR             0x00000014U
#define HAL_FLASH_OPTION_INFO_ADDR        0x0007EFFCU
#define HAL_FLASH_OPTION_MAGIC_FRESH      0xF5F9BDA9U
#define HAL_FLASH_OPTION_MAGIC_CONFIGURED 0xF3F9BDA9U

#define HAL_FLASH_OPTION_RESET_PIN        0x00000008U /* 外部复位引脚 */
#define HAL_FLASH_OPTION_UART_NO_KEY      0x00000100U /* 串口免按键下载 */
#define HAL_FLASH_OPTION_BOOT_PIN_PB22    0x00000200U /* 0=BOOT 脚 PB11，1=PB22 */
#define HAL_FLASH_OPTION_DEBUG            0x00000090U /* bit4 调试口，bit7 SPI Flash 读；0=使能 */
#define HAL_FLASH_OPTION_PROT_MASK        0x00FFFC00U /* bit23:10 写保护大小（单位 4K） */
#define HAL_FLASH_OPTION_PROT_UNIT_SHIFT  10U
#define HAL_FLASH_OPTION_FIXED_BITS       0x00500000U

/* Flash ROM（代码区，IAP 用） */

HAL_StatusTypeDef HAL_FLASH_Read(uint32_t addr, void *buf, uint32_t len);
HAL_StatusTypeDef HAL_FLASH_Erase(uint32_t addr, uint32_t len);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t addr, const void *buf, uint32_t len);
HAL_StatusTypeDef HAL_FLASH_Verify(uint32_t addr, const void *buf, uint32_t len);

/* DataFlash（EEPROM） */

HAL_StatusTypeDef HAL_FLASH_EEPROM_Read(uint32_t offset, void *buf, uint32_t len);
HAL_StatusTypeDef HAL_FLASH_EEPROM_Erase(uint32_t offset, uint32_t len);
HAL_StatusTypeDef HAL_FLASH_EEPROM_Program(uint32_t offset, const void *buf, uint32_t len);

/* 芯片信息 */

#define HAL_FLASH_UID_SIZE 8U
#define HAL_FLASH_MAC_SIZE 6U

HAL_StatusTypeDef HAL_FLASH_ReadRomInfo(uint32_t infoAddr, void *buf);
HAL_StatusTypeDef HAL_FLASH_GetUniqueID(uint8_t id[HAL_FLASH_UID_SIZE]);
HAL_StatusTypeDef HAL_FLASH_GetMACAddress(uint8_t mac[HAL_FLASH_MAC_SIZE]);
uint8_t HAL_FLASH_Is584x(void);
uint32_t HAL_FLASH_GetEraseGranularity(void);

/* Flash 控制器电源 */

HAL_StatusTypeDef HAL_FLASH_PowerDown(void);
HAL_StatusTypeDef HAL_FLASH_PowerUp(void);
HAL_StatusTypeDef HAL_FLASH_SoftwareReset(void);

/* 配置字 */

HAL_StatusTypeDef HAL_FLASH_SetOptionBytes(FunctionalState resetPin,
                                           FunctionalState bootPinPB22,
                                           FunctionalState uartNoKey,
                                           uint32_t protectSize4K);
HAL_StatusTypeDef HAL_FLASH_DisableDebugInterface(void);
HAL_StatusTypeDef HAL_FLASH_EnableDebugInterface(void);
void HAL_FLASH_ActivateOptionBytes(void);

#ifdef __cplusplus
}
#endif

#endif /* __CH58X_HAL_FLASH_H */
