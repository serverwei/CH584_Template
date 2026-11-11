#include "i2c.h"

I2C_HandleTypeDef hi2c1;

typedef struct
{
    GPIO_TypeDef *SclPort;
    uint32_t SclPin;
    GPIO_TypeDef *SdaPort;
    uint32_t SdaPin;
} BSP_I2C_PinTypeDef;

/* [引脚组] */
static const BSP_I2C_PinTypeDef s_i2cPin[2] =
    {
        {GPIOB, GPIO_PIN_13, GPIOB, GPIO_PIN_12}, /* 组 0：PB13 = SCL，PB12 = SDA */
        {GPIOB, GPIO_PIN_21, GPIOB, GPIO_PIN_20}  /* 组 1：PB21 = SCL，PB20 = SDA */
};

/**
 * @brief  I2C 引脚初始化，由 HAL_I2C_Init 调用
 * @param  hi2c 句柄，从其 PinGroup 选择引脚组
 * @note   CH58x 没有 GPIO 开漏模式选项，引脚配成上拉输入即可，
 *         开漏由 I2C 外设硬件接管。
 *         组 0 与 SPI0 组1 / UART1 组1 / USB2 冲突；
 *         组 1 与 UART3 组1 冲突。
 */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    const BSP_I2C_PinTypeDef *pin;
    uint8_t group;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return;
    }

    group = (hi2c->PinGroup != 0U) ? 1U : 0U;
    pin   = &s_i2cPin[group];

    if (group != 0U) {
        HAL_GPIO_SetPeriphRemap(ENABLE, RB_PIN_I2C);
    } else {
        HAL_GPIO_SetPeriphRemap(DISABLE, RB_PIN_I2C);
    }

    HAL_GPIO_SetMode(pin->SclPort, pin->SclPin, HAL_GPIO_MODE_IN_PULLUP);
    HAL_GPIO_SetMode(pin->SdaPort, pin->SdaPin, HAL_GPIO_MODE_IN_PULLUP);
    HAL_GPIO_SetDigitalInput(pin->SclPort, pin->SclPin, ENABLE);
    HAL_GPIO_SetDigitalInput(pin->SdaPort, pin->SdaPin, ENABLE);
}

/**
 * @brief  I2C 引脚回收，由 HAL_I2C_DeInit 调用
 * @param  hi2c 句柄
 * @note   两个引脚都设为浮空输入并禁用数字输入。
 */
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
    const BSP_I2C_PinTypeDef *pin;

    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return;
    }

    pin = &s_i2cPin[(hi2c->PinGroup != 0U) ? 1U : 0U];

    HAL_GPIO_SetMode(pin->SclPort, pin->SclPin, HAL_GPIO_MODE_IN_FLOATING);
    HAL_GPIO_SetMode(pin->SdaPort, pin->SdaPin, HAL_GPIO_MODE_IN_FLOATING);
    HAL_GPIO_SetDigitalInput(pin->SclPort, pin->SclPin, DISABLE);
    HAL_GPIO_SetDigitalInput(pin->SdaPort, pin->SdaPin, DISABLE);
}

/**
 * @brief  初始化 I2C1
 * @param  clockSpeed 时钟频率，100000 或 400000
 * @param  pinGroup   引脚组：0 = PB13(SCL)/PB12(SDA)，1 = PB21(SCL)/PB20(SDA)
 * @param  busMode    HAL_I2C_MODE_I2C 或 HAL_I2C_MODE_SMBUS_HOST
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   SMBus 主机模式带 35ms 硬件超时保护，可避免从机拉死 SDA 导致总线永久 BUSY。
 */
HAL_StatusTypeDef BSP_I2C_Init(uint32_t clockSpeed, uint8_t pinGroup, HAL_I2C_BusModeTypeDef busMode)
{
    hi2c1.Instance         = I2C1;
    hi2c1.Init.ClockSpeed  = clockSpeed;
    hi2c1.Init.DutyCycle   = HAL_I2C_DUTY_2;
    hi2c1.Init.AckAddr     = HAL_I2C_ACK_ADDR_7BIT;
    hi2c1.Init.OwnAddress1 = 0U;
    hi2c1.Init.BusMode     = busMode;
    hi2c1.PinGroup         = pinGroup;

    /* 优先级由 HAL_NVIC_InitPriorities 统一设置，这里只打通 NVIC */
    PFIC_EnableIRQ(I2C_IRQn);

    return HAL_I2C_Init(&hi2c1);
}

/**
 * @brief  关闭 I2C1
 * @note   关中断、关时钟、引脚恢复浮空，句柄的 Instance 置 NULL
 */
void BSP_I2C_DeInit(void)
{
    PFIC_DisableIRQ(I2C_IRQn);
    (void)HAL_I2C_DeInit(&hi2c1);
}

/* ===================== 中断钩子 =====================
 * 入口在 ch58x_hal_it.c；排除本文件时入口退回空实现
 */

/**
 * @brief  I2C1 中断钩子，由 ch58x_hal_it.c 的 I2C_IRQHandler 调用
 */
__HIGH_CODE
void BSP_I2C_IRQHandler(void)
{
    HAL_I2C_IRQHandler(&hi2c1);
}
