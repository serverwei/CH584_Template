#include "ch58x_hal.h"

#include <stddef.h>

/* ===================== 寄存器布局校验 =====================
 * 期望偏移取自 CH585DS1.PDF 表 8-1（TMR0 寄存器，基址 0x40002000）。
 * 四个定时器基址等距 0x400；这种分布很容易把中断标志写成 +0x03，实际在 +0x06。
 */
_Static_assert(offsetof(TMR_TypeDef, CTRL_MOD) == 0x00, "TMR_CTRL_MOD @ +0x00");
_Static_assert(offsetof(TMR_TypeDef, CTRL_DMA) == 0x01, "TMR_CTRL_DMA @ +0x01");
_Static_assert(offsetof(TMR_TypeDef, INTER_EN) == 0x02, "TMR_INTER_EN @ +0x02");
_Static_assert(offsetof(TMR_TypeDef, INT_FLAG) == 0x06, "TMR_INT_FLAG @ +0x06");
_Static_assert(offsetof(TMR_TypeDef, FIFO_COUNT) == 0x07, "TMR_FIFO_COUNT @ +0x07");
_Static_assert(offsetof(TMR_TypeDef, COUNT) == 0x08, "TMR_COUNT @ +0x08");
_Static_assert(offsetof(TMR_TypeDef, CNT_END) == 0x0C, "TMR_CNT_END @ +0x0C");
_Static_assert(offsetof(TMR_TypeDef, FIFO) == 0x10, "TMR_FIFO @ +0x10");
_Static_assert(offsetof(TMR_TypeDef, DMA_NOW) == 0x14, "TMR_DMA_NOW @ +0x14");
_Static_assert(offsetof(TMR_TypeDef, DMA_BEG) == 0x18, "TMR_DMA_BEG @ +0x18");
_Static_assert(offsetof(TMR_TypeDef, DMA_END) == 0x1C, "TMR_DMA_END @ +0x1C");
/* 四个实例基址等距 0x400 */
#define HAL_TIM_BASE_FIRST 0x40002000U
#define HAL_TIM_BASE_STEP  0x00000400U

static const uint32_t s_timClk[4] =
    {
        HAL_PWR_SLPCLK_TMR0,
        HAL_PWR_SLPCLK_TMR1,
        HAL_PWR_SLPCLK_TMR2,
        HAL_PWR_SLPCLK_TMR3};

/**
 * @brief  实例是否合法
 * @param  tim 待检查的实例指针
 * @retval 1 合法（TMR0~3），0 非法
 */
static uint8_t HAL_TIM_IsValid(TMR_TypeDef *tim)
{
    return ((tim == TMR0) || (tim == TMR1) || (tim == TMR2) || (tim == TMR3)) ? 1U : 0U;
}

/**
 * @brief  实例索引
 * @param  tim 实例指针
 * @retval 0~3 对应 TMR0~3
 */
static uint32_t HAL_TIM_GetIndex(TMR_TypeDef *tim)
{
    return ((uint32_t)tim - HAL_TIM_BASE_FIRST) / HAL_TIM_BASE_STEP;
}

/**
 * @brief  实例对应的中断号
 * @param  tim 实例指针，需先经 HAL_TIM_IsValid 校验
 */
static IRQn_Type HAL_TIM_GetIRQn(TMR_TypeDef *tim)
{
    switch (HAL_TIM_GetIndex(tim)) {
        case 0U:
            return TMR0_IRQn;

        case 1U:
            return TMR1_IRQn;

        case 2U:
            return TMR2_IRQn;

        default:
            return TMR3_IRQn;
    }
}

/**
 * @brief  初始化参数校验
 * @param  init 初始化结构
 * @retval HAL_OK 合法；HAL_ERROR 越界
 */
static HAL_StatusTypeDef HAL_TIM_CheckInit(const TIM_InitTypeDef *init)
{
    if ((uint32_t)init->Mode > (uint32_t)HAL_TIM_MODE_COUNTER) {
        return HAL_ERROR;
    }
    if ((uint32_t)init->PWMPolar > (uint32_t)HAL_TIM_PWM_POLAR_HIGH) {
        return HAL_ERROR;
    }
    if ((uint32_t)init->PWMRepeat > (uint32_t)HAL_TIM_PWM_REPEAT_16) {
        return HAL_ERROR;
    }
    if ((init->Mode == HAL_TIM_MODE_TIMER) || (init->Mode == HAL_TIM_MODE_PWM)) {
        if ((init->Period == 0U) || (init->Period > HAL_TIM_MAX_PERIOD)) {
            return HAL_ERROR;
        }
    }
    if ((init->Mode == HAL_TIM_MODE_CAPTURE) || (init->Mode == HAL_TIM_MODE_COUNTER)) {
        if ((uint32_t)init->CapEdge < (uint32_t)HAL_TIM_CAP_EDGE_CHANGE) {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

/**
 * @brief  配置 TMR 的 DMA 通道
 * @param  htim 句柄，Instance 必须指向 TMR0~3
 * @param  buf  缓冲区首地址，必须 4 字节对齐
 * @param  len  uint32_t 元素个数，1 ~ HAL_TIM_DMA_LEN_MAX
 * @param  loop 单次或循环
 * @retval HAL_OK 成功；HAL_ERROR 参数非法
 * @note   顺序与厂商 TMRx_DMACfg 一致：先关 DMA，再写 BEG/END，最后置控制位。
 *         手册 8.3.4 要求使能完成中断前先清一次 DMA_END 标志，这里一并做掉。
 *         地址寄存器只有 bit16:2 有效，超出会回绕成 0，所以在软件里先拦住。
 */
static HAL_StatusTypeDef HAL_TIM_DMA_Config(TIM_HandleTypeDef *htim, const uint32_t *buf,
                                            uint32_t len, HAL_TIM_DMALoopTypeDef loop)
{
    TMR_TypeDef *tim;
    uint32_t base;
    uint32_t bytes;

    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (HAL_TIM_IsValid(htim->Instance) == 0U) {
        return HAL_ERROR;
    }
    if ((buf == NULL) || (len == 0U) || (len > HAL_TIM_DMA_LEN_MAX)) {
        return HAL_ERROR;
    }
    if (((uint32_t)buf & 0x03U) != 0U) {
        return HAL_ERROR;
    }
    if ((uint32_t)loop > (uint32_t)HAL_TIM_DMA_LOOP) {
        return HAL_ERROR;
    }

    tim   = htim->Instance;
    base  = (uint32_t)buf & HAL_TIM_DMA_ADDR_MASK;
    bytes = len << 2;

    if ((base + bytes) > HAL_TIM_DMA_ADDR_MASK) {
        return HAL_ERROR;
    }

    tim->CTRL_DMA = 0U; /* 先关，避免改地址的过程中触发一次搬运 */

    tim->DMA_BEG = base;
    tim->DMA_END = base + bytes; /* 结束地址不含 */

    tim->INT_FLAG = (uint8_t)RB_TMR_IF_DMA_END;

    tim->CTRL_DMA = (uint8_t)(RB_TMR_DMA_ENABLE |
                              ((loop == HAL_TIM_DMA_LOOP) ? RB_TMR_DMA_LOOP : 0U));

    return HAL_OK;
}

/* ===================== 初始化 ===================== */

/**
 * @brief  初始化 TMR
 * @param  htim 句柄，调用前需填好 Instance 与 Init 全部字段
 * @retval HAL_OK 成功；HAL_ERROR 实例非法或参数越界
 * @note   只配置寄存器，不启动计数；启动用 HAL_TIM_Base_Start_IT / HAL_TIM_PWM_Start
 *         或 __HAL_TIM_ENABLE 宏。
 */
HAL_StatusTypeDef HAL_TIM_Init(TIM_HandleTypeDef *htim)
{
    TMR_TypeDef *tim;
    uint32_t idx;

    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (HAL_TIM_IsValid(htim->Instance) == 0U) {
        return HAL_ERROR;
    }
    if (HAL_TIM_CheckInit(&htim->Init) != HAL_OK) {
        return HAL_ERROR;
    }

    tim = htim->Instance;
    idx = HAL_TIM_GetIndex(tim);

    HAL_TIM_MspInit(htim);

    HAL_PWR_ConfigPeriphClock(ENABLE, s_timClk[idx]);

    /* 清计数与 FIFO；ALL_CLEAR 是自清位，不能与其它位一起写 */
    tim->CTRL_MOD = RB_TMR_ALL_CLEAR;

    switch (htim->Init.Mode) {
        case HAL_TIM_MODE_TIMER:
            tim->CNT_END = htim->Init.Period & HAL_TIM_MAX_PERIOD;
            break;

        case HAL_TIM_MODE_PWM:
            tim->CNT_END  = htim->Init.Period & HAL_TIM_MAX_PERIOD;
            tim->CTRL_MOD = (uint8_t)(((uint32_t)htim->Init.PWMPolar << 4) |
                                      ((uint32_t)htim->Init.PWMRepeat << 6));
            break;

        case HAL_TIM_MODE_CAPTURE:
            tim->CTRL_MOD = (uint8_t)(RB_TMR_COUNT_EN | RB_TMR_MODE_IN |
                                      ((uint32_t)htim->Init.CapEdge << 6));
            break;

        case HAL_TIM_MODE_COUNTER:
            tim->CTRL_MOD = (uint8_t)(RB_TMR_COUNT_EN | RB_TMR_MODE_IN |
                                      RB_TMR_CAP_COUNT |
                                      ((uint32_t)htim->Init.CapEdge << 6));
            break;

        default:
            return HAL_ERROR;
    }

    tim->INTER_EN = 0U;
    tim->INT_FLAG = 0x1FU; /* 清全部标志 */

    htim->State = HAL_TIM_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  关闭 TMR
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 * @note   停计数、关中断与 NVIC、关外设时钟、调 MspDeInit 回收引脚。
 */
HAL_StatusTypeDef HAL_TIM_DeInit(TIM_HandleTypeDef *htim)
{
    TMR_TypeDef *tim;
    uint32_t idx;

    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (HAL_TIM_IsValid(htim->Instance) == 0U) {
        return HAL_ERROR;
    }

    tim = htim->Instance;
    idx = HAL_TIM_GetIndex(tim);

    tim->CTRL_MOD = 0U;
    tim->INTER_EN = 0U;
    tim->INT_FLAG = 0x1FU;

    PFIC_DisableIRQ(HAL_TIM_GetIRQn(tim));

    HAL_PWR_ConfigPeriphClock(DISABLE, s_timClk[idx]);

    HAL_TIM_MspDeInit(htim);

    htim->State = HAL_TIM_STATE_RESET;

    return HAL_OK;
}

/* ===================== 中断使能 ===================== */

/**
 * @brief  打开指定中断源并允许 NVIC 响应
 * @param  htim 句柄
 * @param  it   中断源位组合，取 HAL_TIM_IT_xxx
 * @note   已有中断使能时重复调用无副作用；
 *         优先级由 HAL_NVIC_InitPriorities 统一设置，这里不再重复。
 */
void HAL_TIM_Enable_IT(TIM_HandleTypeDef *htim, uint8_t it)
{
    if ((htim == NULL) || (htim->Instance == NULL) || (it == 0U)) {
        return;
    }

    htim->Instance->INTER_EN |= it;

    PFIC_EnableIRQ(HAL_TIM_GetIRQn(htim->Instance));
}

/**
 * @brief  关闭指定中断源，全部关完后同时关 NVIC
 * @param  htim 句柄
 * @param  it   中断源位组合，取 HAL_TIM_IT_xxx
 */
void HAL_TIM_Disable_IT(TIM_HandleTypeDef *htim, uint8_t it)
{
    if ((htim == NULL) || (htim->Instance == NULL) || (it == 0U)) {
        return;
    }

    htim->Instance->INTER_EN &= (uint8_t)~it;

    if (htim->Instance->INTER_EN == 0U) {
        PFIC_DisableIRQ(HAL_TIM_GetIRQn(htim->Instance));
    }
}

/* ===================== 定时（Base） ===================== */

/**
 * @brief  启动定时计数，不开中断
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }

    __HAL_TIM_ENABLE(htim);

    htim->State = HAL_TIM_STATE_BUSY;

    return HAL_OK;
}

/**
 * @brief  启动定时计数并开周期结束中断
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空或当前为捕获模式
 * @note   周期结束触发 HAL_TIM_PeriodElapsedCallback。
 *         定时、PWM、计数三种模式都可用，捕获模式请用 Capture_Start_IT。
 */
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (htim->Init.Mode == HAL_TIM_MODE_CAPTURE) {
        return HAL_ERROR;
    }

    htim->Instance->INT_FLAG = HAL_TIM_IT_ALL;

    HAL_TIM_Enable_IT(htim, HAL_TIM_IT_CYC_END);

    __HAL_TIM_ENABLE(htim);

    htim->State = HAL_TIM_STATE_BUSY;

    return HAL_OK;
}

/**
 * @brief  停止定时计数，不动中断配置
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }

    __HAL_TIM_DISABLE(htim);

    htim->State = HAL_TIM_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  停止定时计数并关周期结束中断
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }

    __HAL_TIM_DISABLE(htim);

    HAL_TIM_Disable_IT(htim, HAL_TIM_IT_CYC_END);

    htim->State = HAL_TIM_STATE_READY;

    return HAL_OK;
}

/* ===================== PWM ===================== */

/**
 * @brief  启动 PWM 输出
 * @param  htim  句柄，Init.Mode 必须是 HAL_TIM_MODE_PWM
 * @param  pulse 初始有效电平计数值，需小于 Period
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或模式不对
 * @note   先写 FIFO 再开输出，避免第一个周期出现异常占空比。
 */
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t pulse)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (htim->Init.Mode != HAL_TIM_MODE_PWM) {
        return HAL_ERROR;
    }
    if (pulse > htim->Init.Period) {
        return HAL_ERROR;
    }

    __HAL_TIM_SET_PULSE(htim, pulse);

    __HAL_TIM_PWM_ENABLE(htim);
    __HAL_TIM_ENABLE(htim);

    htim->State = HAL_TIM_STATE_BUSY;

    return HAL_OK;
}

/**
 * @brief  停止 PWM 输出
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄为空
 * @note   先关输出再停计数，让引脚回到无效电平。
 */
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }

    __HAL_TIM_PWM_DISABLE(htim);
    __HAL_TIM_DISABLE(htim);

    htim->State = HAL_TIM_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  启动 PWM 输出并开周期结束中断
 * @param  htim  句柄，Init.Mode 必须是 HAL_TIM_MODE_PWM
 * @param  pulse 初始有效电平计数值，需小于 Period
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或模式不对
 * @note   每个 PWM 周期结束触发 HAL_TIM_PeriodElapsedCallback，
 *         适合在中断里用 __HAL_TIM_SET_PULSE 更新下一个周期的占空比。
 */
HAL_StatusTypeDef HAL_TIM_PWM_Start_IT(TIM_HandleTypeDef *htim, uint32_t pulse)
{
    if (HAL_TIM_PWM_Start(htim, pulse) != HAL_OK) {
        return HAL_ERROR;
    }

    htim->Instance->INT_FLAG = HAL_TIM_IT_ALL;

    HAL_TIM_Enable_IT(htim, HAL_TIM_IT_CYC_END);

    return HAL_OK;
}

/**
 * @brief  停止 PWM 输出并关周期结束中断
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_TIM_PWM_Stop_IT(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }

    __HAL_TIM_PWM_DISABLE(htim);
    __HAL_TIM_DISABLE(htim);

    HAL_TIM_Disable_IT(htim, HAL_TIM_IT_CYC_END);

    htim->State = HAL_TIM_STATE_READY;

    return HAL_OK;
}

/* ===================== 捕获 / 计数 ===================== */

/**
 * @brief  启动输入捕获或外部计数，不开中断
 * @param  htim 句柄，Init.Mode 必须是 HAL_TIM_MODE_CAPTURE 或 HAL_TIM_MODE_COUNTER
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或模式不对
 * @note   不开中断就只能轮询 __HAL_TIM_GET_FIFO_COUNT / __HAL_TIM_GET_COUNTER，
 *         常规捕获请用 HAL_TIM_Capture_Start_IT。
 */
HAL_StatusTypeDef HAL_TIM_Capture_Start(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((htim->Init.Mode != HAL_TIM_MODE_CAPTURE) &&
        (htim->Init.Mode != HAL_TIM_MODE_COUNTER)) {
        return HAL_ERROR;
    }

    __HAL_TIM_ENABLE(htim);

    htim->State = HAL_TIM_STATE_BUSY;

    return HAL_OK;
}

/**
 * @brief  启动输入捕获或外部计数并开数据有效中断
 * @param  htim 句柄，Init.Mode 必须是 HAL_TIM_MODE_CAPTURE 或 HAL_TIM_MODE_COUNTER
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或模式不对
 * @note   捕获到新数据触发 HAL_TIM_CaptureCallback；
 *         捕获超时（CYC_END）需另外调 HAL_TIM_Enable_IT(&htim, HAL_TIM_IT_CYC_END)。
 */
HAL_StatusTypeDef HAL_TIM_Capture_Start_IT(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((htim->Init.Mode != HAL_TIM_MODE_CAPTURE) &&
        (htim->Init.Mode != HAL_TIM_MODE_COUNTER)) {
        return HAL_ERROR;
    }

    htim->Instance->INT_FLAG = HAL_TIM_IT_ALL;

    HAL_TIM_Enable_IT(htim, HAL_TIM_IT_DATA_ACT);

    __HAL_TIM_ENABLE(htim);

    htim->State = HAL_TIM_STATE_BUSY;

    return HAL_OK;
}

/**
 * @brief  停止输入捕获或外部计数，不动中断配置
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_TIM_Capture_Stop(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }

    __HAL_TIM_DISABLE(htim);

    htim->State = HAL_TIM_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  停止输入捕获或外部计数并关捕获相关中断
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 */
HAL_StatusTypeDef HAL_TIM_Capture_Stop_IT(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }

    __HAL_TIM_DISABLE(htim);

    HAL_TIM_Disable_IT(htim, (uint8_t)(HAL_TIM_IT_DATA_ACT | HAL_TIM_IT_CYC_END));

    htim->State = HAL_TIM_STATE_READY;

    return HAL_OK;
}

/* ===================== DMA ===================== */

/**
 * @brief  启动 PWM + DMA，用缓冲区内容连续更新占空比
 * @param  htim  句柄，Init.Mode 必须是 HAL_TIM_MODE_PWM
 * @param  pulse 占空比数组，每个元素低 26 位有效
 * @param  len   元素个数，1 ~ HAL_TIM_DMA_LEN_MAX
 * @param  loop  HAL_TIM_DMA_LOOP 时到末尾自动回到开头循环播放
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或模式不对
 * @note   每个 PWM 周期（或 Init.PWMRepeat 指定的重复周期）取一个元素送进 FIFO。
 *         搬完一轮触发 HAL_TIM_DMACpltCallback，单次模式下中断使能会被自动关闭。
 *         换新缓冲区直接再调本函数即可，内部会先关 DMA 再重设地址。
 */
HAL_StatusTypeDef HAL_TIM_PWM_Start_DMA(TIM_HandleTypeDef *htim, const uint32_t *pulse,
                                        uint32_t len, HAL_TIM_DMALoopTypeDef loop)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if (htim->Init.Mode != HAL_TIM_MODE_PWM) {
        return HAL_ERROR;
    }
    if (HAL_TIM_DMA_Config(htim, pulse, len, loop) != HAL_OK) {
        return HAL_ERROR;
    }

    /* 输出与计数都打开后再开中断，避免第一轮还没跑就报完成 */
    __HAL_TIM_PWM_ENABLE(htim);
    __HAL_TIM_ENABLE(htim);

    HAL_TIM_Enable_IT(htim, HAL_TIM_IT_DMA_END);

    htim->State = HAL_TIM_STATE_BUSY;

    return HAL_OK;
}

/**
 * @brief  启动捕获 / 计数 + DMA，把捕获值自动写进缓冲区
 * @param  htim 句柄，Init.Mode 必须是 HAL_TIM_MODE_CAPTURE 或 HAL_TIM_MODE_COUNTER
 * @param  buf  接收缓冲区，必须 4 字节对齐
 * @param  len  元素个数，1 ~ HAL_TIM_DMA_LEN_MAX
 * @param  loop HAL_TIM_DMA_LOOP 时写满后回到开头覆盖
 * @retval HAL_OK 成功；HAL_ERROR 参数非法或模式不对
 * @note   厂商 EVT 例程注明：捕获模式下缓冲区前两个元素是无效数据，取数时应跳过。
 *         元素低 26 位有效，位 25 为 1 表示本次捕获超时，需与前一组累计。
 */
HAL_StatusTypeDef HAL_TIM_Capture_Start_DMA(TIM_HandleTypeDef *htim, uint32_t *buf,
                                            uint32_t len, HAL_TIM_DMALoopTypeDef loop)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }
    if ((htim->Init.Mode != HAL_TIM_MODE_CAPTURE) &&
        (htim->Init.Mode != HAL_TIM_MODE_COUNTER)) {
        return HAL_ERROR;
    }
    if (HAL_TIM_DMA_Config(htim, buf, len, loop) != HAL_OK) {
        return HAL_ERROR;
    }

    __HAL_TIM_ENABLE(htim);

    HAL_TIM_Enable_IT(htim, HAL_TIM_IT_DMA_END);

    htim->State = HAL_TIM_STATE_BUSY;

    return HAL_OK;
}

/**
 * @brief  停止 DMA 通道
 * @param  htim 句柄
 * @retval HAL_OK 成功；HAL_ERROR 句柄或实例为空
 * @note   只关 DMA 和它的完成中断，不影响 PWM 输出与计数，也不动引脚。
 *         需要连计数一起停请再调 HAL_TIM_PWM_Stop / HAL_TIM_Capture_Stop。
 */
HAL_StatusTypeDef HAL_TIM_Stop_DMA(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_ERROR;
    }

    HAL_TIM_Disable_IT(htim, HAL_TIM_IT_DMA_END);

    htim->Instance->CTRL_DMA = 0U;
    htim->Instance->INT_FLAG = (uint8_t)RB_TMR_IF_DMA_END;

    htim->State = HAL_TIM_STATE_READY;

    return HAL_OK;
}

/**
 * @brief  已搬运的元素个数
 * @param  htim 句柄
 * @retval 已搬运的 uint32_t 个数；句柄为空返回 0
 * @note   DMA 关闭后读数为最后一次的值；循环模式下会周期性回绕。
 */
uint32_t HAL_TIM_GetDMACount(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return 0U;
    }

    return __HAL_TIM_GET_DMA_COUNT(htim);
}

/* ===================== 中断入口与回调 ===================== */

/**
 * @brief  TMR 中断处理
 * @param  htim 句柄，由 _it.c 里的 TMRn_IRQHandler 传入
 * @note   INT_FLAG 是写 1 清除，与 I2C 的 rc_w0 相反。
 */
__HIGH_CODE
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *htim)
{
    TMR_TypeDef *tim;
    uint8_t flag;

    if ((htim == NULL) || (htim->Instance == NULL)) {
        return;
    }

    tim  = htim->Instance;
    flag = tim->INT_FLAG;

    if ((flag & RB_TMR_IF_CYC_END) != 0U) {
        tim->INT_FLAG = RB_TMR_IF_CYC_END;
        HAL_TIM_PeriodElapsedCallback(htim);
    }

    if ((flag & RB_TMR_IF_DATA_ACT) != 0U) {
        uint32_t value = tim->FIFO;

        tim->INT_FLAG = RB_TMR_IF_DATA_ACT;
        HAL_TIM_CaptureCallback(htim, value);
    }

    if ((flag & RB_TMR_IF_FIFO_OV) != 0U) {
        tim->INT_FLAG = RB_TMR_IF_FIFO_OV;
        HAL_TIM_OverflowCallback(htim);
    }

    if ((flag & RB_TMR_IF_FIFO_HF) != 0U) {
        tim->INT_FLAG = RB_TMR_IF_FIFO_HF;
        HAL_TIM_FifoHalfCallback(htim);
    }

    if ((flag & RB_TMR_IF_DMA_END) != 0U) {
        /* 单次模式搬完后 DMA_NOW 停在 DMA_END，只清标志会被硬件立刻重新置位，
         * 造成中断风暴。厂商 EVT 的做法是先关掉这条中断使能再清标志。 */
        if ((tim->CTRL_DMA & RB_TMR_DMA_LOOP) == 0U) {
            HAL_TIM_Disable_IT(htim, HAL_TIM_IT_DMA_END);
        }
        tim->INT_FLAG = RB_TMR_IF_DMA_END;
        HAL_TIM_DMACpltCallback(htim);
    }
}

__weak void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    UNUSED(htim);
}

__weak void HAL_TIM_CaptureCallback(TIM_HandleTypeDef *htim, uint32_t value)
{
    UNUSED(htim);
    UNUSED(value);
}

__weak void HAL_TIM_OverflowCallback(TIM_HandleTypeDef *htim)
{
    UNUSED(htim);
}

__weak void HAL_TIM_FifoHalfCallback(TIM_HandleTypeDef *htim)
{
    UNUSED(htim);
}

__weak void HAL_TIM_DMACpltCallback(TIM_HandleTypeDef *htim)
{
    UNUSED(htim);
}

__weak void HAL_TIM_MspInit(TIM_HandleTypeDef *htim)
{
    UNUSED(htim);
}

__weak void HAL_TIM_MspDeInit(TIM_HandleTypeDef *htim)
{
    UNUSED(htim);
}
