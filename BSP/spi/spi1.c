#include "spi1.h"
#include "spi.h"

#if (CH58x_CHIP_MODEL != ID_CH584)

SPI_HandleTypeDef hspi1;

/* 软件片选引脚，由 BSP_SPI1_Init 填入；s_csPort 为 NULL 表示不管理片选 */
static GPIO_TypeDef *s_csPort;
static uint32_t s_csPin;

/**
 * @brief  初始化 SPI1
 * @param  cfg 配置：分频、模式、位序、片选；PinGroup 对 SPI1 无效
 * @retval HAL_OK 成功；HAL_ERROR cfg 为空或分频越界
 * @note   固定为 PA0=SCK1 / PA1=MOSI1 / PA2=MISO1，没有重映射，
 *         SPI1 也没有片选引脚，片选必须挂在外面的普通 GPIO 上。
 *         芯片只支持主机模式与模式 0/3，其余组合会被 HAL 拒绝。
 */
HAL_StatusTypeDef BSP_SPI1_Init(const BSP_SPI_ConfigTypeDef *cfg)
{
    HAL_StatusTypeDef st;

    if (cfg == NULL) {
        return HAL_ERROR;
    }

    /* 先把片选拉到无效电平，再去配 SCK/MOSI，避免上电瞬间从设备误动作 */
    s_csPort = cfg->CsPort;
    s_csPin  = cfg->CsPin;

    if ((s_csPort != NULL) && (s_csPin != 0U)) {
        HAL_GPIO_WritePin(s_csPort, s_csPin, HAL_GPIO_PIN_SET);
        HAL_GPIO_SetMode(s_csPort, s_csPin, HAL_GPIO_MODE_OUT_PP_5MA);
        HAL_GPIO_SetDigitalInput(s_csPort, s_csPin, ENABLE);
    }

    hspi1.Instance      = SPI1;
    hspi1.Init.Role     = HAL_SPI_ROLE_MASTER;
    hspi1.Init.Mode     = cfg->Mode;
    hspi1.Init.BitOrder = cfg->BitOrder;
    hspi1.Init.ClockDiv = cfg->ClockDiv;
    hspi1.Init.PinGroup = 0U; /* SPI1 无重映射 */

    st = HAL_SPI_Init(&hspi1);

    if (st != HAL_OK) {
        BSP_SPI1_DeInit();
    }

    return st;
}

/**
 * @brief  关闭 SPI1
 * @note   SCK/MOSI/MISO 设为浮空输入并禁用数字输入，句柄的 Instance 置 NULL；
 *         配置过的片选脚一并释放
 */
void BSP_SPI1_DeInit(void)
{
    if ((s_csPort != NULL) && (s_csPin != 0U)) {
        HAL_GPIO_WritePin(s_csPort, s_csPin, HAL_GPIO_PIN_SET);
        HAL_GPIO_SetMode(s_csPort, s_csPin, HAL_GPIO_MODE_IN_FLOATING);
        HAL_GPIO_SetDigitalInput(s_csPort, s_csPin, DISABLE);
        s_csPort = NULL;
        s_csPin  = 0U;
    }

    (void)HAL_SPI_DeInit(&hspi1);
}

/**
 * @brief  拉低片选，选中从设备
 * @note   配置里没给片选时什么都不做，同一份业务代码可以直接搬到多从机场景。
 */
void BSP_SPI1_CS_Select(void)
{
    if ((s_csPort != NULL) && (s_csPin != 0U)) {
        HAL_GPIO_WritePin(s_csPort, s_csPin, HAL_GPIO_PIN_RESET);
    }
}

/**
 * @brief  拉高片选，释放从设备
 */
void BSP_SPI1_CS_Release(void)
{
    if ((s_csPort != NULL) && (s_csPin != 0U)) {
        HAL_GPIO_WritePin(s_csPort, s_csPin, HAL_GPIO_PIN_SET);
    }
}

/**
 * @brief  阻塞发送
 * @param  buf     待发送数据首地址
 * @param  len     发送字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时；HAL_ERROR 参数非法；HAL_BUSY 上一笔未结束
 */
HAL_StatusTypeDef BSP_SPI1_Transmit(const uint8_t *buf, uint16_t len, uint32_t timeout)
{
    return HAL_SPI_Transmit(&hspi1, buf, len, timeout);
}

/**
 * @brief  阻塞接收
 * @param  buf     接收缓冲区
 * @param  len     接收字节数，最大 4095
 * @param  timeout 超时，单位 ms；HAL_MAX_DELAY 表示不限时
 * @retval HAL_OK 成功；HAL_TIMEOUT 超时（已收到的字节仍写进了 buf）
 */
HAL_StatusTypeDef BSP_SPI1_Receive(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    return HAL_SPI_Receive(&hspi1, buf, len, timeout);
}

/**
 * @brief  启动中断发送
 * @param  buf 待发送数据首地址，由调用方保证生命周期
 * @param  len 发送字节数，最大 4095
 * @retval HAL_OK 成功；HAL_BUSY 上一笔未结束
 * @note   全部发完后触发 BSP_SPI1_TxCpltCallback
 */
HAL_StatusTypeDef BSP_SPI1_Transmit_IT(const uint8_t *buf, uint16_t len)
{
    return HAL_SPI_Transmit_IT(&hspi1, buf, len);
}

/**
 * @brief  启动中断接收
 * @param  buf 接收缓冲区，由调用方保证生命周期
 * @param  len 接收字节数，最大 4095
 * @retval HAL_OK 成功；HAL_BUSY 上一笔未结束
 * @note   收满 len 字节后触发 BSP_SPI1_RxCpltCallback
 */
HAL_StatusTypeDef BSP_SPI1_Receive_IT(uint8_t *buf, uint16_t len)
{
    return HAL_SPI_Receive_IT(&hspi1, buf, len);
}

/**
 * @brief  中止正在进行的中断收发
 * @retval HAL_OK 成功
 */
HAL_StatusTypeDef BSP_SPI1_Abort_IT(void)
{
    return HAL_SPI_Abort_IT(&hspi1);
}

/* ===================== 共享向量接管 =====================
 * SPI1 与 PWMX 共用 IRQ31。ch58x_hal_it.c 里的 PWMX_SPI1_IRQHandler
 * 会先后调用 BSP_PWM_IRQHandler 与 BSP_SPI1_IRQHandler，两者在那边
 * 都是弱空函数；这里给 BSP_SPI1_IRQHandler 一个强定义即可接管。
 * HAL_SPI_IRQHandler 内部会先查标志，不是 SPI 的中断立即返回，
 * 所以 PWM 的中断不会被误处理。
 */

/**
 * @brief  SPI1 中断处理，由 ch58x_hal_it.c 的共享向量入口调用
 * @note   与 BSP_PWM_IRQHandler 对称，同样需要待存 RAM：
 *         flash 擦写期间 XROM 不响应取指。
 */
__HIGH_CODE
void BSP_SPI1_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&hspi1);
}

#endif
