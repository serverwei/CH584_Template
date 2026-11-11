#include "ch58x_hal.h"

#include <stddef.h>

/* ===================== 寄存器布局校验 =====================
 * 期望偏移取自 CH585DS1.PDF 表 13-1（LCD 寄存器，基址 0x40006000）。
 * CMD 是 8 位、后面 RAM0 对齐到 +0x04，中间三个字节是空隙，不能省。
 */
_Static_assert(offsetof(LCD_TypeDef, CMD) == 0x00, "LCD_CMD @ +0x00");
_Static_assert(offsetof(LCD_TypeDef, RAM0) == 0x04, "LCD_RAM0 @ +0x04");
_Static_assert(offsetof(LCD_TypeDef, RAM1) == 0x08, "LCD_RAM1 @ +0x08");
_Static_assert(offsetof(LCD_TypeDef, RAM2) == 0x0C, "LCD_RAM2 @ +0x0C");
_Static_assert(offsetof(LCD_TypeDef, RAM3) == 0x10, "LCD_RAM3 @ +0x10");
_Static_assert(offsetof(LCD_TypeDef, SEG_EN) == 0x14, "LCD_SEG_EN @ +0x14");
/* ===================== 内部工具 ===================== */

/**
 * @brief  把 Init 里的参数位拼成 CMD 寄存器值
 * @note   只拼参数位，ON / SYS_EN 由 HAL_LCD_Start / Stop 单独控制。
 */
static uint8_t HAL_LCD_CmdBits(const LCD_HandleTypeDef *hlcd)
{
    uint8_t cmd;

    cmd = (uint8_t)(hlcd->Init.Duty << 3);  /* RB_LCD_DUTY   = 0x18 */
    cmd |= (uint8_t)(hlcd->Init.Scan << 5); /* RB_LCD_SCAN_CLK = 0x60 */

    if (hlcd->Init.Bias != HAL_LCD_BIAS_1_2) {
        cmd |= RB_LCD_BIAS;
    }
    if (hlcd->Init.Drive != HAL_LCD_DRIVE_3V3) {
        cmd |= RB_LCD_VLCD_SEL;
    }

    return cmd;
}

/**
 * @brief  改完 Init 里的参数后刷新 CMD，保留 ON / SYS_EN
 */
static void HAL_LCD_UpdateCmd(LCD_HandleTypeDef *hlcd)
{
    uint8_t cmd = hlcd->Instance->CMD;

    cmd &= (uint8_t)~(RB_LCD_DUTY | RB_LCD_SCAN_CLK | RB_LCD_BIAS | RB_LCD_VLCD_SEL);
    cmd |= HAL_LCD_CmdBits(hlcd);

    hlcd->Instance->CMD = cmd;
}

/* ===================== 初始化 ===================== */

/**
 * @brief  初始化段式 LCD
 * @param  hlcd 句柄，调用前需填好 Instance 与 Init
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   只配置并清屏，不打开显示，开显示调 HAL_LCD_Start。
 *         调用前必须保证 32K 时钟已经打开（HAL_RCC_InitClock 已包含）。
 *         用 SEG2 时 PB2 会被占走，而 PB2 默认是外部复位 RST#，
 *         必须在烧录时关闭外部复位（用户选项字节），代码里改不了。
 */
HAL_StatusTypeDef HAL_LCD_Init(LCD_HandleTypeDef *hlcd)
{
    uint8_t i;

    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (hlcd->Instance != LCDX) {
        return HAL_ERROR;
    }
    if ((uint32_t)hlcd->Init.Drive > (uint32_t)HAL_LCD_DRIVE_2V5) {
        return HAL_ERROR;
    }
    if ((uint32_t)hlcd->Init.Duty > (uint32_t)HAL_LCD_DUTY_1_4) {
        return HAL_ERROR;
    }
    if ((uint32_t)hlcd->Init.Bias > (uint32_t)HAL_LCD_BIAS_1_3) {
        return HAL_ERROR;
    }
    if ((uint32_t)hlcd->Init.Scan > (uint32_t)HAL_LCD_SCAN_128HZ) {
        return HAL_ERROR;
    }
    if ((hlcd->Init.SegMask & (uint32_t)~HAL_LCD_SEG_MASK_ALL) != 0U) {
        return HAL_ERROR;
    }

    hlcd->State = HAL_LCD_STATE_RESET;

    HAL_LCD_MspInit(hlcd);

    /* 先写成关断状态：只落参数位，ON / SYS_EN 留给 HAL_LCD_Start */
    hlcd->Instance->CMD = HAL_LCD_CmdBits(hlcd);

    hlcd->Instance->SEG_EN = hlcd->Init.SegMask;

    /* 显存清零并推送，避免上电残留旧图案 */
    for (i = 0U; i < HAL_LCD_BUF_SIZE; i++) {
        hlcd->Buffer[i] = 0U;
    }
    HAL_LCD_Refresh(hlcd);

    hlcd->State = HAL_LCD_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  关闭段式 LCD 并归还引脚
 * @param  hlcd 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_LCD_DeInit(LCD_HandleTypeDef *hlcd)
{
    uint8_t i;

    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return HAL_ERROR;
    }

    HAL_LCD_Stop(hlcd);

    hlcd->Instance->SEG_EN = 0U;
    hlcd->Instance->RAM0   = 0U;
    hlcd->Instance->RAM1   = 0U;
    hlcd->Instance->RAM2   = 0U;
    hlcd->Instance->RAM3   = 0U;

    for (i = 0U; i < HAL_LCD_BUF_SIZE; i++) {
        hlcd->Buffer[i] = 0U;
    }

    HAL_LCD_MspDeInit(hlcd);

    hlcd->State = HAL_LCD_STATE_RESET;

    return HAL_OK;
}

/* ===================== 开关 ===================== */

/**
 * @brief  打开显示
 * @param  hlcd 句柄
 * @note   CMD 里两个使能位都要置：SYS_EN 是数字部分，ON 是模拟部分。
 */
void HAL_LCD_Start(LCD_HandleTypeDef *hlcd)
{
    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return;
    }

    hlcd->Instance->CMD |= (uint8_t)(RB_LCD_SYS_EN | RB_LCD_ON);
}

/**
 * @brief  关闭显示，显存与参数保持不变
 * @param  hlcd 句柄
 */
void HAL_LCD_Stop(LCD_HandleTypeDef *hlcd)
{
    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return;
    }

    hlcd->Instance->CMD &= (uint8_t)~(RB_LCD_SYS_EN | RB_LCD_ON);
}

/* ===================== 参数 ===================== */

/**
 * @brief  改驱动电压
 * @param  hlcd  句柄
 * @param  drive HAL_LCD_DRIVE_3V3 / HAL_LCD_DRIVE_2V5
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 */
HAL_StatusTypeDef HAL_LCD_SetDrive(LCD_HandleTypeDef *hlcd, HAL_LCD_DriveTypeDef drive)
{
    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)drive > (uint32_t)HAL_LCD_DRIVE_2V5) {
        return HAL_ERROR;
    }

    hlcd->Init.Drive = drive;
    HAL_LCD_UpdateCmd(hlcd);

    return HAL_OK;
}

/**
 * @brief  改占空比
 * @param  hlcd 句柄
 * @param  duty HAL_LCD_DUTY_1_2 / 1_3 / 1_4
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   COM 引脚数量随 duty 变，这里会重跑一遍 MspInit：
 *         多出来的 COM 释放成普通引脚，不够的补配成模拟。
 */
HAL_StatusTypeDef HAL_LCD_SetDuty(LCD_HandleTypeDef *hlcd, HAL_LCD_DutyTypeDef duty)
{
    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)duty > (uint32_t)HAL_LCD_DUTY_1_4) {
        return HAL_ERROR;
    }

    hlcd->Init.Duty = duty;
    HAL_LCD_UpdateCmd(hlcd);
    HAL_LCD_MspInit(hlcd);

    return HAL_OK;
}

/**
 * @brief  改偏压比
 * @param  hlcd 句柄
 * @param  bias HAL_LCD_BIAS_1_2 / HAL_LCD_BIAS_1_3
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 */
HAL_StatusTypeDef HAL_LCD_SetBias(LCD_HandleTypeDef *hlcd, HAL_LCD_BiasTypeDef bias)
{
    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)bias > (uint32_t)HAL_LCD_BIAS_1_3) {
        return HAL_ERROR;
    }

    hlcd->Init.Bias = bias;
    HAL_LCD_UpdateCmd(hlcd);

    return HAL_OK;
}

/**
 * @brief  改扫描频率
 * @param  hlcd 句柄
 * @param  scan HAL_LCD_SCAN_256HZ / 512HZ / 1KHZ / 128HZ
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 */
HAL_StatusTypeDef HAL_LCD_SetScan(LCD_HandleTypeDef *hlcd, HAL_LCD_ScanTypeDef scan)
{
    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((uint32_t)scan > (uint32_t)HAL_LCD_SCAN_128HZ) {
        return HAL_ERROR;
    }

    hlcd->Init.Scan = scan;
    HAL_LCD_UpdateCmd(hlcd);

    return HAL_OK;
}

/* ===================== 段使能 ===================== */

/**
 * @brief  整体设置段使能
 * @param  hlcd 句柄
 * @param  mask bit n = 使能 SEGn，超出 28 位的部分被忽略
 * @note   会使引脚占用跟着变，内部重跑 MspInit。
 */
void HAL_LCD_SetSegMask(LCD_HandleTypeDef *hlcd, uint32_t mask)
{
    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return;
    }

    hlcd->Init.SegMask = mask & (uint32_t)HAL_LCD_SEG_MASK_ALL;

    hlcd->Instance->SEG_EN = hlcd->Init.SegMask;

    HAL_LCD_MspInit(hlcd);
}

/**
 * @brief  单个段的使能/关闭
 * @param  hlcd 句柄
 * @param  seg  段号 0~27
 * @param  s    ENABLE = 使能并占用引脚，DISABLE = 关闭并释放引脚
 */
void HAL_LCD_SegEnable(LCD_HandleTypeDef *hlcd, uint8_t seg, FunctionalState s)
{
    if ((hlcd == NULL) || (hlcd->Instance == NULL) || (seg >= HAL_LCD_SEG_COUNT)) {
        return;
    }

    if (s != DISABLE) {
        hlcd->Init.SegMask |= (uint32_t)(1UL << seg);
    } else {
        hlcd->Init.SegMask &= (uint32_t)~(1UL << seg);
    }

    hlcd->Instance->SEG_EN = hlcd->Init.SegMask;

    HAL_LCD_MspInit(hlcd);
}

/* ===================== 影子显存 ===================== */

/**
 * @brief  写一个段的 4 个 COM
 * @param  hlcd    句柄
 * @param  seg     段号 0~27
 * @param  comMask bit0~3 对应 COM0~COM3，置 1 表示该点显示
 * @note   只改影子，要看到效果还得调 HAL_LCD_Refresh。
 */
void HAL_LCD_WriteSeg(LCD_HandleTypeDef *hlcd, uint8_t seg, uint8_t comMask)
{
    uint8_t idx;
    uint8_t sh;
    uint8_t v;

    if ((hlcd == NULL) || (seg >= HAL_LCD_SEG_COUNT)) {
        return;
    }

    idx = (uint8_t)(seg >> 1);
    sh  = (uint8_t)((uint8_t)(seg & 1U) << 2U);
    v   = (uint8_t)((comMask & HAL_LCD_COM_ALL) << sh);

    hlcd->Buffer[idx] = (uint8_t)((hlcd->Buffer[idx] & (uint8_t)~(0x0FU << sh)) | v);
}

/**
 * @brief  读一个段的 4 个 COM
 * @param  hlcd 句柄
 * @param  seg  段号 0~27
 * @retval bit0~3 对应 COM0~COM3；段号越界返回 0
 */
uint8_t HAL_LCD_ReadSeg(const LCD_HandleTypeDef *hlcd, uint8_t seg)
{
    if ((hlcd == NULL) || (seg >= HAL_LCD_SEG_COUNT)) {
        return 0U;
    }

    return (uint8_t)((hlcd->Buffer[seg >> 1] >> ((uint8_t)(seg & 1U) << 2U)) & HAL_LCD_COM_ALL);
}

/**
 * @brief  按字节索引写影子显存
 * @param  hlcd  句柄
 * @param  index 字节索引 0~13，低半字节是偶数段，高半字节是奇数段
 * @param  data  段码
 * @note   这个接口是为了跟厂商的 LCD_WriteData0~13 对齐，新代码建议用
 *         HAL_LCD_WriteSeg 按段号写，不容易错位。
 */
void HAL_LCD_WriteByte(LCD_HandleTypeDef *hlcd, uint8_t index, uint8_t data)
{
    if ((hlcd == NULL) || (index >= HAL_LCD_BUF_SIZE)) {
        return;
    }

    hlcd->Buffer[index] = data;
}

/**
 * @brief  按字节索引批量写影子显存
 * @param  hlcd  句柄
 * @param  index 起始字节索引 0~13
 * @param  buf   段码首地址
 * @param  count 字节数，超出 14 的部分被截断
 */
void HAL_LCD_WriteBuffer(LCD_HandleTypeDef *hlcd, uint8_t index,
                         const uint8_t *buf, uint8_t count)
{
    uint8_t i;

    if ((hlcd == NULL) || (buf == NULL) || (index >= HAL_LCD_BUF_SIZE)) {
        return;
    }

    for (i = 0U; (i < count) && ((uint8_t)(index + i) < HAL_LCD_BUF_SIZE); i++) {
        hlcd->Buffer[index + i] = buf[i];
    }
}

/**
 * @brief  清屏
 * @param  hlcd 句柄
 * @note   影子清零后立即推送硬件，调用完就能看到空屏。
 */
void HAL_LCD_Clear(LCD_HandleTypeDef *hlcd)
{
    uint8_t i;

    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return;
    }

    for (i = 0U; i < HAL_LCD_BUF_SIZE; i++) {
        hlcd->Buffer[i] = 0U;
    }

    HAL_LCD_Refresh(hlcd);
}

/**
 * @brief  把影子显存推到硬件
 * @param  hlcd 句柄
 * @note   RAM3 只有低 16 位有效，对应 SEG24~SEG27；影子多出来的
 *         Buffer[14]/[15] 本来就不存在，HAL_LCD_BUF_SIZE 已经卡在 14。
 */
void HAL_LCD_Refresh(LCD_HandleTypeDef *hlcd)
{
    const uint8_t *b;

    if ((hlcd == NULL) || (hlcd->Instance == NULL)) {
        return;
    }

    b = hlcd->Buffer;

    hlcd->Instance->RAM0 = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    hlcd->Instance->RAM1 = (uint32_t)b[4] | ((uint32_t)b[5] << 8) |
                           ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    hlcd->Instance->RAM2 = (uint32_t)b[8] | ((uint32_t)b[9] << 8) |
                           ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);
    hlcd->Instance->RAM3 = (uint32_t)b[12] | ((uint32_t)b[13] << 8);
}

/* ===================== 低功耗 ===================== */

/**
 * @brief  睡眠时是否关闭 LCD 时钟
 * @param  off ENABLE = 关闭 LCD 时钟（省电），DISABLE = 保持时钟
 * @note   R8_SLP_CLK_OFF1 在安全访问区（SAM），必须成对开关。
 *         注意语义是「关闭位」，不是「使能位」：写 1 才是省电。
 */
void HAL_LCD_SetClockOff(FunctionalState off)
{
    HAL_SYS_SAFE_ACCESS_ENABLE();

    if (off != DISABLE) {
        R8_SLP_CLK_OFF1 |= RB_SLP_CLK_LCD;
    } else {
        R8_SLP_CLK_OFF1 &= (uint8_t)~RB_SLP_CLK_LCD;
    }

    HAL_SYS_SAFE_ACCESS_DISABLE();
}

/* ===================== 默认 MSP ===================== */

__weak void HAL_LCD_MspInit(LCD_HandleTypeDef *hlcd)
{
    UNUSED(hlcd);
}

__weak void HAL_LCD_MspDeInit(LCD_HandleTypeDef *hlcd)
{
    UNUSED(hlcd);
}
