#include "spi.h"

typedef struct
{
    GPIO_TypeDef *SckPort;
    uint32_t SckPin;
    GPIO_TypeDef *MosiPort;
    uint32_t MosiPin;
    GPIO_TypeDef *MisoPort;
    uint32_t MisoPin;
} BSP_SPI_PinTypeDef;

/* [实例][引脚组]：SPI0 两组，SPI1 只有一组，第 1 组实际不用 */
static const BSP_SPI_PinTypeDef s_spiPin[2][2] =
    {
        {/* SPI0：组 0 = PA13/PA14/PA15，组 1 = PB13/PB14/PB15 */
         {GPIOA, GPIO_PIN_13, GPIOA, GPIO_PIN_14, GPIOA, GPIO_PIN_15},
         {GPIOB, GPIO_PIN_13, GPIOB, GPIO_PIN_14, GPIOB, GPIO_PIN_15}},
        {/* SPI1（仅 CH585）：PA0/PA1/PA2，无重映射 */
         {GPIOA, GPIO_PIN_0, GPIOA, GPIO_PIN_1, GPIOA, GPIO_PIN_2},
         {GPIOA, GPIO_PIN_0, GPIOA, GPIO_PIN_1, GPIOA, GPIO_PIN_2}}};

/**
 * @brief  实例索引 0~1，非法实例返回 2
 */
static uint32_t BSP_SPI_GetIndex(SPI_TypeDef *spi)
{
    if (spi == SPI0) {
        return 0U;
    }
    if (spi == SPI1) {
        return 1U;
    }

    return 2U;
}

/**
 * @brief  SPI 引脚初始化，由 HAL_SPI_Init 调用
 * @param  hspi 句柄，从其 Init 读 Role 与 PinGroup
 * @note   主模式：SCK/MOSI 推挽输出，MISO 上拉输入。
 *         从模式：SCK/MOSI 上拉输入，MISO 上拉输入，片选有效期间硬件自动切为输出。
 *         只有 SPI0 有重映射位 RB_PIN_SPI0，SPI1 是固定引脚，不碰这个位。
 */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    const BSP_SPI_PinTypeDef *pin;
    uint32_t idx;
    uint8_t group;

    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return;
    }

    idx = BSP_SPI_GetIndex(hspi->Instance);

    if (idx >= 2U) {
        return;
    }

    group = (hspi->Init.PinGroup != 0U) ? 1U : 0U;
    pin   = &s_spiPin[idx][group];

    if (hspi->Instance == SPI0) {
        if (group != 0U) {
            HAL_GPIO_SetPeriphRemap(ENABLE, RB_PIN_SPI0);
        } else {
            HAL_GPIO_SetPeriphRemap(DISABLE, RB_PIN_SPI0);
        }
    }

    if (hspi->Init.Role == HAL_SPI_ROLE_MASTER) {
        HAL_GPIO_SetMode(pin->SckPort, pin->SckPin, HAL_GPIO_MODE_OUT_PP_5MA);
        HAL_GPIO_SetDigitalInput(pin->SckPort, pin->SckPin, ENABLE);

        HAL_GPIO_SetMode(pin->MosiPort, pin->MosiPin, HAL_GPIO_MODE_OUT_PP_5MA);
        HAL_GPIO_SetDigitalInput(pin->MosiPort, pin->MosiPin, ENABLE);

        HAL_GPIO_SetMode(pin->MisoPort, pin->MisoPin, HAL_GPIO_MODE_IN_PULLUP);
        HAL_GPIO_SetDigitalInput(pin->MisoPort, pin->MisoPin, ENABLE);
    } else {
        HAL_GPIO_SetMode(pin->SckPort, pin->SckPin, HAL_GPIO_MODE_IN_PULLUP);
        HAL_GPIO_SetDigitalInput(pin->SckPort, pin->SckPin, ENABLE);

        HAL_GPIO_SetMode(pin->MosiPort, pin->MosiPin, HAL_GPIO_MODE_IN_PULLUP);
        HAL_GPIO_SetDigitalInput(pin->MosiPort, pin->MosiPin, ENABLE);

        HAL_GPIO_SetMode(pin->MisoPort, pin->MisoPin, HAL_GPIO_MODE_IN_PULLUP);
        HAL_GPIO_SetDigitalInput(pin->MisoPort, pin->MisoPin, ENABLE);
    }
}

/**
 * @brief  SPI 引脚回收，由 HAL_SPI_DeInit 调用
 * @param  hspi 句柄
 * @note   三个引脚全部设为浮空输入并禁用数字输入，不区分主从。
 *         片选是应用自己管的 GPIO，这里不碰。
 */
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
    const BSP_SPI_PinTypeDef *pin;
    uint32_t idx;

    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return;
    }

    idx = BSP_SPI_GetIndex(hspi->Instance);

    if (idx >= 2U) {
        return;
    }

    pin = &s_spiPin[idx][(hspi->Init.PinGroup != 0U) ? 1U : 0U];

    HAL_GPIO_SetMode(pin->SckPort, pin->SckPin, HAL_GPIO_MODE_IN_FLOATING);
    HAL_GPIO_SetDigitalInput(pin->SckPort, pin->SckPin, DISABLE);

    HAL_GPIO_SetMode(pin->MosiPort, pin->MosiPin, HAL_GPIO_MODE_IN_FLOATING);
    HAL_GPIO_SetDigitalInput(pin->MosiPort, pin->MosiPin, DISABLE);

    HAL_GPIO_SetMode(pin->MisoPort, pin->MisoPin, HAL_GPIO_MODE_IN_FLOATING);
    HAL_GPIO_SetDigitalInput(pin->MisoPort, pin->MisoPin, DISABLE);
}

/**
 * @brief  发送完成回调，按实例分发到 BSP_SPIx_TxCpltCallback
 * @param  hspi 触发回调的句柄，从其 Instance 判别是哪个实例
 * @note   覆盖 HAL 的 weak 实现，全工程只能有这一份。在中断上下文执行。
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return;
    }

    if (hspi->Instance == SPI0) {
        BSP_SPI0_TxCpltCallback();
    } else if (hspi->Instance == SPI1) {
        BSP_SPI1_TxCpltCallback();
    } else {
        /* 未知实例，丢弃 */
    }
}

/**
 * @brief  接收完成回调，按实例分发到 BSP_SPIx_RxCpltCallback
 * @param  hspi 触发回调的句柄
 */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return;
    }

    if (hspi->Instance == SPI0) {
        BSP_SPI0_RxCpltCallback();
    } else if (hspi->Instance == SPI1) {
        BSP_SPI1_RxCpltCallback();
    } else {
        /* 未知实例，丢弃 */
    }
}

/**
 * @brief  错误回调，按实例分发到 BSP_SPIx_ErrorCallback
 * @param  hspi 触发回调的句柄
 * @note   目前只有从机 FIFO 溢出会走到这里。
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return;
    }

    if (hspi->Instance == SPI0) {
        BSP_SPI0_ErrorCallback();
    } else if (hspi->Instance == SPI1) {
        BSP_SPI1_ErrorCallback();
    } else {
        /* 未知实例，丢弃 */
    }
}

/* ===================== 默认回调 =====================
 * 6 个弱定义集中放在这里，不放各自的 spix.c：
 * 上面的分发函数会无条件调用它们，若散落在 spix.c 里，
 * 排除某个实例文件就会让 spi.c 出现 undefined reference。
 *
 * 分发在中断上下文执行。TxCplt/RxCplt 触发时 HAL 已关闭 CNT_END 与
 * FIFO_HF 中断，需要继续收发必须重新调用 BSP_SPIx_Transmit_IT / Receive_IT。
 */

__weak void BSP_SPI0_TxCpltCallback(void)
{
}
__weak void BSP_SPI0_RxCpltCallback(void)
{
}
__weak void BSP_SPI0_ErrorCallback(void)
{
}

__weak void BSP_SPI1_TxCpltCallback(void)
{
}
__weak void BSP_SPI1_RxCpltCallback(void)
{
}
__weak void BSP_SPI1_ErrorCallback(void)
{
}
