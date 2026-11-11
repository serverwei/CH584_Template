#include "ch58x_hal_flash.h"

#include "ch58x_hal_sys.h"
#include "ISP585.h"

/**
 * @brief  读绝对地址上的 32 位字
 * @param  addr 绝对地址
 * @retval 该地址的原始内容
 * @note   用户配置字固定在 0x14 这类低绝对地址，不在任何 C 对象内，
 *         GCC 会对这种解引用报 -Warray-bounds 误报，故用内联汇编读取。
 */
static uint32_t HAL_FLASH_ReadAbsWord(uint32_t addr)
{
    uint32_t value;

    __asm__ volatile("lw %0, 0(%1)" : "=r"(value) : "r"(addr) : "memory");

    return value;
}

/**
 * @brief  地址 + 长度是否越出 DataFlash
 */
static uint8_t HAL_FLASH_EEPROM_InRange(uint32_t offset, uint32_t len)
{
    if ((offset >= HAL_FLASH_EEPROM_MAX_SIZE) ||
        (len > (HAL_FLASH_EEPROM_MAX_SIZE - offset))) {
        return 0U;
    }

    return 1U;
}

/* ===================== Flash ROM（代码区） ===================== */

/**
 * @brief  读 Flash ROM
 * @note   Flash 直接映射到地址空间，此处就是 memcpy
 */
HAL_StatusTypeDef HAL_FLASH_Read(uint32_t addr, void *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return HAL_ERROR;
    }

    memcpy(buf, (const void *)addr, len);

    return HAL_OK;
}

/**
 * @brief  擦除 Flash ROM
 * @param  addr 必须是 4K 对齐
 * @param  len  必须是 4K 的整数倍
 */
HAL_StatusTypeDef HAL_FLASH_Erase(uint32_t addr, uint32_t len)
{
    if ((len == 0U) ||
        ((len % HAL_FLASH_ROM_BLOCK_SIZE) != 0U) ||
        ((addr % HAL_FLASH_ROM_BLOCK_SIZE) != 0U)) {
        return HAL_ERROR;
    }

    return (FLASH_ROM_ERASE(addr, len) == 0U) ? HAL_OK : HAL_ERROR;
}

/**
 * @brief  写 Flash ROM
 * @param  addr 必须 4 字节对齐
 * @param  buf  必须在 RAM 中且 4 字节对齐
 * @param  len  必须是 4 的整数倍
 */
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t addr, const void *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U) ||
        ((len % HAL_FLASH_ROM_WRITE_UNIT) != 0U) ||
        (((addr | (uint32_t)buf) & 0x03U) != 0U)) {
        return HAL_ERROR;
    }

    return (FLASH_ROM_WRITE(addr, (void *)buf, len) == 0U) ? HAL_OK : HAL_ERROR;
}

/**
 * @brief  校验 Flash ROM 内容
 */
HAL_StatusTypeDef HAL_FLASH_Verify(uint32_t addr, const void *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U) ||
        ((len % HAL_FLASH_ROM_WRITE_UNIT) != 0U) ||
        (((addr | (uint32_t)buf) & 0x03U) != 0U)) {
        return HAL_ERROR;
    }

    return (FLASH_ROM_VERIFY(addr, (void *)buf, len) == 0U) ? HAL_OK : HAL_ERROR;
}

/* ===================== DataFlash（EEPROM） ===================== */

/**
 * @brief  读 DataFlash
 * @param  offset 自 DataFlash 起始的偏移
 */
HAL_StatusTypeDef HAL_FLASH_EEPROM_Read(uint32_t offset, void *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U) || !HAL_FLASH_EEPROM_InRange(offset, len)) {
        return HAL_ERROR;
    }

    return (EEPROM_READ(offset, buf, len) == 0U) ? HAL_OK : HAL_ERROR;
}

/**
 * @brief  擦除 DataFlash
 * @note   CH584X 的最小擦除单位是 4K，其余型号是 256B；不满足直接返回错误
 */
HAL_StatusTypeDef HAL_FLASH_EEPROM_Erase(uint32_t offset, uint32_t len)
{
    uint32_t gran = HAL_FLASH_GetEraseGranularity();

    if ((len == 0U) ||
        ((len % gran) != 0U) ||
        ((offset % gran) != 0U) ||
        !HAL_FLASH_EEPROM_InRange(offset, len)) {
        return HAL_ERROR;
    }

    return (EEPROM_ERASE(offset, len) == 0U) ? HAL_OK : HAL_ERROR;
}

/**
 * @brief  写 DataFlash
 * @param  buf 必须在 RAM 中且 4 字节对齐
 */
HAL_StatusTypeDef HAL_FLASH_EEPROM_Program(uint32_t offset, const void *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U) ||
        (((uint32_t)buf & 0x03U) != 0U) ||
        !HAL_FLASH_EEPROM_InRange(offset, len)) {
        return HAL_ERROR;
    }

    return (EEPROM_WRITE(offset, (void *)buf, len) == 0U) ? HAL_OK : HAL_ERROR;
}

/* ===================== 芯片信息 ===================== */

/**
 * @brief  从 ROM 信息区读一段数据
 * @param  infoAddr 取 ROM_CFG_* 常量
 * @param  buf      必须 4 字节对齐
 */
HAL_StatusTypeDef HAL_FLASH_ReadRomInfo(uint32_t infoAddr, void *buf)
{
    if ((buf == NULL) || (((uint32_t)buf & 0x03U) != 0U)) {
        return HAL_ERROR;
    }

    return (FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, infoAddr, buf, 0U) == 0U) ? HAL_OK : HAL_ERROR;
}

/**
 * @brief  读 64 位芯片唯一 ID
 * @param  id 输出缓冲区，长度 HAL_FLASH_UID_SIZE（8 字节）
 * @retval HAL_OK 成功；HAL_ERROR id 为空或 ROM 命令失败
 * @note   UID 是 64 位，不是 STM32 那种 96 位三字结构。
 *         小端存放：id[0] 是最低字节。
 */
HAL_StatusTypeDef HAL_FLASH_GetUniqueID(uint8_t id[HAL_FLASH_UID_SIZE])
{
    uint32_t tmp[2];

    if (id == NULL) {
        return HAL_ERROR;
    }

    tmp[0] = 0xFFFFFFFFU;
    tmp[1] = 0xFFFFFFFFU;

    if (FLASH_EEPROM_CMD(CMD_GET_UNIQUE_ID, 0U, tmp, 0U) != 0U) {
        return HAL_ERROR;
    }

    memcpy(id, tmp, HAL_FLASH_UID_SIZE);

    return HAL_OK;
}

/**
 * @brief  读 6 字节 MAC 地址
 * @param  mac 输出缓冲区，长度 HAL_FLASH_MAC_SIZE（6 字节）
 * @retval HAL_OK 成功；HAL_ERROR mac 为空或 ROM 命令失败
 * @note   读失败时缓冲区保持调用前的值，不再缄默地返回 6 个 0xFF。
 */
HAL_StatusTypeDef HAL_FLASH_GetMACAddress(uint8_t mac[HAL_FLASH_MAC_SIZE])
{
    uint32_t tmp[2];

    if (mac == NULL) {
        return HAL_ERROR;
    }

    tmp[0] = 0xFFFFFFFFU;
    tmp[1] = 0xFFFFFFFFU;

    if (FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, ROM_CFG_MAC_ADDR, tmp, 0U) != 0U) {
        return HAL_ERROR;
    }

    memcpy(mac, tmp, HAL_FLASH_MAC_SIZE);

    return HAL_OK;
}

/**
 * @brief  是否为 CH584X 系列
 * @note   直接读 ROM 配置区，不依赖 HAL_RCC 是否已初始化
 */
uint8_t HAL_FLASH_Is584x(void)
{
    return (uint8_t)((*(volatile const uint32_t *)ROM_CFG_CHIP_ID & 0x0FU) == DEF_CHIP_ID_CH584X);
}

/**
 * @brief  DataFlash 最小擦除单位
 */
uint32_t HAL_FLASH_GetEraseGranularity(void)
{
    return (HAL_FLASH_Is584x() != 0U) ? HAL_FLASH_EEPROM_BLOCK_SIZE : HAL_FLASH_EEPROM_PAGE_SIZE;
}

/* ===================== 控制器电源 ===================== */

HAL_StatusTypeDef HAL_FLASH_PowerDown(void)
{
    return (FLASH_ROM_PWR_DOWN() == 0U) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_FLASH_PowerUp(void)
{
    return (FLASH_ROM_PWR_UP() == 0U) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_FLASH_SoftwareReset(void)
{
    return (FLASH_ROM_SW_RESET() == 0U) ? HAL_OK : HAL_ERROR;
}

/* ===================== 用户配置字 ===================== */

/**
 * @brief  读出用户配置字的原始值
 * @param  value 低 24 位为配置值
 * @note   绝对地址 0x14 处的魔术字为 F5F9BDA9 / F3F9BDA9 时才认为芯片支持配置字
 */
static HAL_StatusTypeDef HAL_FLASH_ReadOption(uint32_t *value)
{
    uint32_t magic = HAL_FLASH_ReadAbsWord(HAL_FLASH_OPTION_ADDR);
    uint32_t raw   = 0U;

    if ((magic != HAL_FLASH_OPTION_MAGIC_FRESH) &&
        (magic != HAL_FLASH_OPTION_MAGIC_CONFIGURED)) {
        return HAL_ERROR;
    }

    if (FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, HAL_FLASH_OPTION_INFO_ADDR, &raw, 4U) != 0U) {
        return HAL_ERROR;
    }

    *value = raw;

    return HAL_OK;
}

/**
 * @brief  写回用户配置字并回读校验
 */
static HAL_StatusTypeDef HAL_FLASH_WriteOption(uint32_t value)
{
    uint32_t s;
    uint32_t t;

    s = value & 0x00FFFFFFU;
    s |= ((~(s << 24)) & 0xFF000000U);

    if (FLASH_ROM_WRITE(HAL_FLASH_OPTION_ADDR, &s, 4U) != 0U) {
        return HAL_ERROR;
    }

    t = HAL_FLASH_ReadAbsWord(HAL_FLASH_OPTION_ADDR);

    return (s == t) ? HAL_OK : HAL_ERROR;
}

/**
 * @brief  写用户配置字
 * @param  resetPin      外部复位引脚使能
 * @param  bootPinPB22   ENABLE=BOOT 脚用 PB22，DISABLE=用 PB11
 * @param  uartNoKey     串口免按键下载使能
 * @param  protectSize4K 写保护大小，单位 4K，取值 0~63
 * @note   每次烧录后只能写一次；生效需调用 HAL_FLASH_ActivateOptionBytes()。
 */
HAL_StatusTypeDef HAL_FLASH_SetOptionBytes(FunctionalState resetPin,
                                           FunctionalState bootPinPB22,
                                           FunctionalState uartNoKey,
                                           uint32_t protectSize4K)
{
    uint32_t s;
    HAL_StatusTypeDef status = HAL_FLASH_ReadOption(&s);

    if (status != HAL_OK) {
        return status;
    }

    if (resetPin != DISABLE) {
        s |= HAL_FLASH_OPTION_RESET_PIN;
    } else {
        s &= ~HAL_FLASH_OPTION_RESET_PIN;
    }

    if (bootPinPB22 != DISABLE) {
        s |= HAL_FLASH_OPTION_BOOT_PIN_PB22;
    } else {
        s &= ~HAL_FLASH_OPTION_BOOT_PIN_PB22;
    }

    if (uartNoKey != DISABLE) {
        s |= HAL_FLASH_OPTION_UART_NO_KEY;
    } else {
        s &= ~HAL_FLASH_OPTION_UART_NO_KEY;
    }

    s &= ~HAL_FLASH_OPTION_PROT_MASK;
    s |= (((protectSize4K << HAL_FLASH_OPTION_PROT_UNIT_SHIFT) | HAL_FLASH_OPTION_FIXED_BITS) &
          HAL_FLASH_OPTION_PROT_MASK);

    return HAL_FLASH_WriteOption(s);
}

/**
 * @brief  关闭两线调试接口与 SPI Flash 读
 * @note   关闭后两次上电即失效，只能通过 HAL_FLASH_EnableDebugInterface() 恢复。
 */
HAL_StatusTypeDef HAL_FLASH_DisableDebugInterface(void)
{
    uint32_t s;
    HAL_StatusTypeDef status = HAL_FLASH_ReadOption(&s);

    if (status != HAL_OK) {
        return status;
    }

    s &= ~HAL_FLASH_OPTION_DEBUG;

    return HAL_FLASH_WriteOption(s);
}

/**
 * @brief  重新使能两线调试接口与 SPI Flash 读
 */
HAL_StatusTypeDef HAL_FLASH_EnableDebugInterface(void)
{
    uint32_t s;
    HAL_StatusTypeDef status = HAL_FLASH_ReadOption(&s);

    if (status != HAL_OK) {
        return status;
    }

    s |= HAL_FLASH_OPTION_DEBUG;

    return HAL_FLASH_WriteOption(s);
}

/**
 * @brief  用户配置字生效，执行后立即软件复位，不返回
 */
void HAL_FLASH_ActivateOptionBytes(void)
{
    (void)FLASH_ROM_SW_RESET();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R16_INT32K_TUNE = 0xFFFFU;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    HAL_SYS_SAFE_ACCESS_ENABLE();
    R8_RST_WDOG_CTRL |= RB_SOFTWARE_RESET;
    HAL_SYS_SAFE_ACCESS_DISABLE();

    while (1) {
    }
}
