#include "App/stepper_tmc2209.h"

#define TMC2209_SYNC_BYTE 0x05U
#define TMC2209_WRITE_ACCESS 0x80U

#define TMC2209_REG_GCONF 0x00U
#define TMC2209_REG_IHOLD_IRUN 0x10U
#define TMC2209_REG_CHOPCONF 0x6CU
#define TMC2209_REG_PWMCONF 0x70U

static uint8_t StepperTmc2209_Crc8(const uint8_t *data, uint8_t data_len)
{
  uint8_t crc = 0U;
  uint8_t byte_index;
  uint8_t bit_index;

  for (byte_index = 0U; byte_index < data_len; byte_index++)
  {
    uint8_t current = data[byte_index];

    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
      if (((crc >> 7U) ^ (current & 0x01U)) != 0U)
      {
        crc = (uint8_t)((crc << 1U) ^ 0x07U);
      }
      else
      {
        crc <<= 1U;
      }

      current >>= 1U;
    }
  }

  return crc;
}

static uint8_t StepperTmc2209_IsApb2Timer(const TIM_TypeDef *timer_instance)
{
  if ((timer_instance == TIM1) || (timer_instance == TIM8) ||
      (timer_instance == TIM9) || (timer_instance == TIM10) ||
      (timer_instance == TIM11))
  {
    return 1U;
  }

  return 0U;
}

static uint32_t StepperTmc2209_GetTimerClock(const TIM_HandleTypeDef *htim)
{
  if (StepperTmc2209_IsApb2Timer(htim->Instance) != 0U)
  {
    if ((RCC->CFGR & RCC_CFGR_PPRE2) == RCC_CFGR_PPRE2_DIV1)
    {
      return HAL_RCC_GetPCLK2Freq();
    }

    return HAL_RCC_GetPCLK2Freq() * 2U;
  }

  if ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1)
  {
    return HAL_RCC_GetPCLK1Freq();
  }

  return HAL_RCC_GetPCLK1Freq() * 2U;
}

static HAL_StatusTypeDef StepperTmc2209_WriteRegister(
    const StepperTmc2209_HandleTypeDef *handle,
    uint8_t reg_addr,
    uint32_t reg_data)
{
  uint8_t tx_frame[8];

  tx_frame[0] = TMC2209_SYNC_BYTE;
  tx_frame[1] = handle->slave_address;
  tx_frame[2] = (uint8_t)(reg_addr | TMC2209_WRITE_ACCESS);
  tx_frame[3] = (uint8_t)((reg_data >> 24U) & 0xFFU);
  tx_frame[4] = (uint8_t)((reg_data >> 16U) & 0xFFU);
  tx_frame[5] = (uint8_t)((reg_data >> 8U) & 0xFFU);
  tx_frame[6] = (uint8_t)(reg_data & 0xFFU);
  tx_frame[7] = StepperTmc2209_Crc8(tx_frame, 7U);

  return HAL_UART_Transmit(handle->huart_tmc, tx_frame, 8U, 30U);
}

static HAL_StatusTypeDef StepperTmc2209_ConfigDefaultRegisters(
    const StepperTmc2209_HandleTypeDef *handle)
{
  HAL_StatusTypeDef status;

  status = StepperTmc2209_WriteRegister(handle, TMC2209_REG_GCONF, 0x00000040U);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_Delay(1U);

  status = StepperTmc2209_WriteRegister(handle, TMC2209_REG_IHOLD_IRUN, 0x00041F08U);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_Delay(1U);

  status = StepperTmc2209_WriteRegister(handle, TMC2209_REG_CHOPCONF, 0x10000053U);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_Delay(1U);

  return StepperTmc2209_WriteRegister(handle, TMC2209_REG_PWMCONF, 0xC10D0024U);
}

static HAL_StatusTypeDef StepperTmc2209_ApplyStepFrequency(
    StepperTmc2209_HandleTypeDef *handle,
    uint16_t step_hz)
{
  uint32_t timer_clock_hz;
  uint32_t counter_clock_hz;
  uint32_t arr_value;
  uint32_t ccr_value;

  if (step_hz == 0U)
  {
    return HAL_ERROR;
  }

  timer_clock_hz = StepperTmc2209_GetTimerClock(handle->htim_step);
  counter_clock_hz = timer_clock_hz / (handle->htim_step->Init.Prescaler + 1U);

  if (counter_clock_hz <= step_hz)
  {
    return HAL_ERROR;
  }

  arr_value = counter_clock_hz / step_hz;
  if (arr_value < 4U)
  {
    arr_value = 4U;
  }

  arr_value -= 1U;
  ccr_value = (arr_value + 1U) / 2U;

  __HAL_TIM_SET_AUTORELOAD(handle->htim_step, arr_value);
  __HAL_TIM_SET_COMPARE(handle->htim_step, handle->step_channel, ccr_value);
  __HAL_TIM_SET_COUNTER(handle->htim_step, 0U);
  if (HAL_TIM_GenerateEvent(handle->htim_step, TIM_EVENTSOURCE_UPDATE) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

HAL_StatusTypeDef StepperTmc2209_Init(
    StepperTmc2209_HandleTypeDef *handle,
    TIM_HandleTypeDef *htim_step,
    uint32_t step_channel,
    UART_HandleTypeDef *huart_tmc,
    GPIO_TypeDef *dir_gpio_port,
    uint16_t dir_pin,
    GPIO_TypeDef *en_gpio_port,
    uint16_t en_pin,
    uint8_t slave_address,
    const uint16_t speed_table_hz[STEPPER_TMC2209_SPEED_STAGE_COUNT])
{
  HAL_StatusTypeDef status;
  uint8_t i;
  static const uint16_t default_speed_table[STEPPER_TMC2209_SPEED_STAGE_COUNT] = {
      200U, 400U, 700U, 1000U, 1400U,
      200U, 400U, 700U, 1000U, 1400U};

  if ((handle == (void *)0) || (htim_step == (void *)0) || (huart_tmc == (void *)0) ||
      (dir_gpio_port == (void *)0) || (en_gpio_port == (void *)0))
  {
    return HAL_ERROR;
  }

  handle->htim_step = htim_step;
  handle->step_channel = step_channel;
  handle->huart_tmc = huart_tmc;
  handle->dir_gpio_port = dir_gpio_port;
  handle->dir_pin = dir_pin;
  handle->en_gpio_port = en_gpio_port;
  handle->en_pin = en_pin;
  handle->slave_address = slave_address;
  handle->speed_index = 0U;

  for (i = 0U; i < STEPPER_TMC2209_SPEED_STAGE_COUNT; i++)
  {
    if (speed_table_hz != (const uint16_t *)0)
    {
      handle->speed_hz[i] = speed_table_hz[i];
    }
    else
    {
      handle->speed_hz[i] = default_speed_table[i];
    }
  }

  HAL_GPIO_WritePin(handle->en_gpio_port, handle->en_pin, STEPPER_TMC2209_DISABLE);
  HAL_GPIO_WritePin(handle->dir_gpio_port, handle->dir_pin, STEPPER_TMC2209_DIR_FORWARD);
  HAL_Delay(2U);

  status = StepperTmc2209_ConfigDefaultRegisters(handle);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_GPIO_WritePin(handle->en_gpio_port, handle->en_pin, STEPPER_TMC2209_ENABLE);

  status = HAL_TIM_PWM_Start(handle->htim_step, handle->step_channel);
  if (status != HAL_OK)
  {
    return status;
  }

  return StepperTmc2209_SetSpeedStage(handle, 0U);
}

HAL_StatusTypeDef StepperTmc2209_SetSpeedStage(
    StepperTmc2209_HandleTypeDef *handle,
    uint8_t stage)
{
  HAL_StatusTypeDef status;

  if ((handle == (void *)0) || (stage >= STEPPER_TMC2209_SPEED_STAGE_COUNT))
  {
    return HAL_ERROR;
  }

  if (stage < STEPPER_TMC2209_DIRECTION_SPLIT_STAGE)
  {
    StepperTmc2209_SetDirection(handle, STEPPER_TMC2209_DIR_FORWARD);
  }
  else
  {
    StepperTmc2209_SetDirection(handle, STEPPER_TMC2209_DIR_REVERSE);
  }

  status = StepperTmc2209_ApplyStepFrequency(handle, handle->speed_hz[stage]);
  if (status == HAL_OK)
  {
    handle->speed_index = stage;
  }

  return status;
}

HAL_StatusTypeDef StepperTmc2209_NextSpeedStage(
    StepperTmc2209_HandleTypeDef *handle)
{
  uint8_t next_stage;

  if (handle == (void *)0)
  {
    return HAL_ERROR;
  }

  next_stage = (uint8_t)(handle->speed_index + 1U);
  if (next_stage >= STEPPER_TMC2209_SPEED_STAGE_COUNT)
  {
    next_stage = 0U;
  }

  return StepperTmc2209_SetSpeedStage(handle, next_stage);
}

void StepperTmc2209_SetDirection(
    StepperTmc2209_HandleTypeDef *handle,
    GPIO_PinState direction_state)
{
  if (handle == (void *)0)
  {
    return;
  }

  HAL_GPIO_WritePin(handle->dir_gpio_port, handle->dir_pin, direction_state);
}

void StepperTmc2209_SetEnable(
    StepperTmc2209_HandleTypeDef *handle,
    GPIO_PinState enable_state)
{
  if (handle == (void *)0)
  {
    return;
  }

  HAL_GPIO_WritePin(handle->en_gpio_port, handle->en_pin, enable_state);
}

uint8_t StepperTmc2209_GetSpeedStage(
    const StepperTmc2209_HandleTypeDef *handle)
{
  if (handle == (const StepperTmc2209_HandleTypeDef *)0)
  {
    return 0U;
  }

  return handle->speed_index;
}
