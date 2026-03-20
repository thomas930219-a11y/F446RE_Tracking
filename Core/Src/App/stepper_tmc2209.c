#include "App/stepper_tmc2209.h"

#define TMC2209_SYNC_BYTE 0x05U
#define TMC2209_WRITE_ACCESS 0x80U

#define TMC2209_REG_GCONF 0x00U
#define TMC2209_REG_IHOLD_IRUN 0x10U
#define TMC2209_REG_CHOPCONF 0x6CU
#define TMC2209_REG_PWMCONF 0x70U

#define TMC2209_FRAME_LEN 8U
#define TMC2209_FRAME_CRC_INPUT_LEN 7U
#define TMC2209_UART_TIMEOUT_MS 30U
#define TMC2209_REG_WRITE_DELAY_MS 1U
#define TMC2209_DRIVER_WAKEUP_DELAY_MS 2U
#define TMC2209_FRAME_DATA_MSB_SHIFT 24U
#define TMC2209_FRAME_DATA_BYTE2_SHIFT 16U
#define TMC2209_FRAME_DATA_BYTE1_SHIFT 8U
#define TMC2209_IHOLD 6U
#define TMC2209_IRUN 16U
#define TMC2209_IHOLDDELAY 4U
#define TMC2209_RAMP_STEP_HZ 800U
#define TMC2209_RAMP_DELAY_MS 1U
#define TMC2209_DIR_SWITCH_SETTLE_MS 2U
#define TMC2209_GCONF_PDN_DISABLE (1UL << 6U)
#define TMC2209_GCONF_MSTEP_REG_SELECT (1UL << 7U)
#define TMC2209_GCONF_VALUE (TMC2209_GCONF_PDN_DISABLE | TMC2209_GCONF_MSTEP_REG_SELECT)
#define TMC2209_CHOPCONF_BASE_VALUE 0x10000053UL
#define TMC2209_CHOPCONF_MRES_SHIFT 24U
#define TMC2209_CHOPCONF_MRES_MASK (0x0FUL << TMC2209_CHOPCONF_MRES_SHIFT)
#define TMC2209_CHOPCONF_MRES_1_16 0x04UL
#define TMC2209_CHOPCONF_VALUE \
  ((TMC2209_CHOPCONF_BASE_VALUE & ~TMC2209_CHOPCONF_MRES_MASK) | \
   (TMC2209_CHOPCONF_MRES_1_16 << TMC2209_CHOPCONF_MRES_SHIFT))
#define TMC2209_PWMCONF_VALUE 0xC10D0024UL
#define TMC2209_IHOLD_IRUN_VALUE \
  ((((uint32_t)TMC2209_IHOLDDELAY) << 16U) | (((uint32_t)TMC2209_IRUN) << 8U) | ((uint32_t)TMC2209_IHOLD))

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
  uint8_t tx_frame[TMC2209_FRAME_LEN];

  tx_frame[0] = TMC2209_SYNC_BYTE;
  tx_frame[1] = handle->slave_address;
  tx_frame[2] = (uint8_t)(reg_addr | TMC2209_WRITE_ACCESS);
  tx_frame[3] = (uint8_t)((reg_data >> TMC2209_FRAME_DATA_MSB_SHIFT) & 0xFFU);
  tx_frame[4] = (uint8_t)((reg_data >> TMC2209_FRAME_DATA_BYTE2_SHIFT) & 0xFFU);
  tx_frame[5] = (uint8_t)((reg_data >> TMC2209_FRAME_DATA_BYTE1_SHIFT) & 0xFFU);
  tx_frame[6] = (uint8_t)(reg_data & 0xFFU);
  tx_frame[7] = StepperTmc2209_Crc8(tx_frame, TMC2209_FRAME_CRC_INPUT_LEN);

  return HAL_UART_Transmit(handle->huart_tmc, tx_frame, TMC2209_FRAME_LEN, TMC2209_UART_TIMEOUT_MS);
}

static HAL_StatusTypeDef StepperTmc2209_ConfigDefaultRegisters(
    const StepperTmc2209_HandleTypeDef *handle)
{
  HAL_StatusTypeDef status;

  status = StepperTmc2209_WriteRegister(handle, TMC2209_REG_GCONF, TMC2209_GCONF_VALUE);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_Delay(TMC2209_REG_WRITE_DELAY_MS);

  status = StepperTmc2209_WriteRegister(handle, TMC2209_REG_IHOLD_IRUN, TMC2209_IHOLD_IRUN_VALUE);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_Delay(TMC2209_REG_WRITE_DELAY_MS);

  status = StepperTmc2209_WriteRegister(handle, TMC2209_REG_CHOPCONF, TMC2209_CHOPCONF_VALUE);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_Delay(TMC2209_REG_WRITE_DELAY_MS);

  return StepperTmc2209_WriteRegister(handle, TMC2209_REG_PWMCONF, TMC2209_PWMCONF_VALUE);
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

static HAL_StatusTypeDef StepperTmc2209_RampStepFrequency(
    StepperTmc2209_HandleTypeDef *handle,
    uint16_t start_hz,
    uint16_t target_hz)
{
  uint32_t current_hz;
  uint32_t target_hz_u32;
  uint32_t step_hz;
  HAL_StatusTypeDef status;

  if ((handle == NULL) || (start_hz == 0U) || (target_hz == 0U))
  {
    return HAL_ERROR;
  }

  if (start_hz == target_hz)
  {
    return StepperTmc2209_ApplyStepFrequency(handle, target_hz);
  }

  current_hz = start_hz;
  target_hz_u32 = target_hz;
  step_hz = TMC2209_RAMP_STEP_HZ;
  if (step_hz == 0U)
  {
    step_hz = 1U;
  }

  while (current_hz != target_hz_u32)
  {
    if (current_hz < target_hz_u32)
    {
      uint32_t delta = target_hz_u32 - current_hz;
      current_hz += (delta > step_hz) ? step_hz : delta;
    }
    else
    {
      uint32_t delta = current_hz - target_hz_u32;
      current_hz -= (delta > step_hz) ? step_hz : delta;
    }

    status = StepperTmc2209_ApplyStepFrequency(handle, (uint16_t)current_hz);
    if (status != HAL_OK)
    {
      return status;
    }

    if (current_hz != target_hz_u32)
    {
      HAL_Delay(TMC2209_RAMP_DELAY_MS);
    }
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
  const uint16_t *source_speed_table;
  uint8_t i;
  static const uint16_t default_speed_table[STEPPER_TMC2209_SPEED_STAGE_COUNT] = {
      200U, 1400U, 5000U, 7500U,
      200U, 1400U, 5000U, 7500U};

  if ((handle == NULL) || (htim_step == NULL) || (huart_tmc == NULL) ||
      (dir_gpio_port == NULL) || (en_gpio_port == NULL))
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
  source_speed_table = (speed_table_hz != NULL) ? speed_table_hz : default_speed_table;

  for (i = 0U; i < STEPPER_TMC2209_SPEED_STAGE_COUNT; i++)
  {
    handle->speed_hz[i] = source_speed_table[i];
  }

  HAL_GPIO_WritePin(handle->en_gpio_port, handle->en_pin, STEPPER_TMC2209_DISABLE);
  HAL_GPIO_WritePin(handle->dir_gpio_port, handle->dir_pin, STEPPER_TMC2209_DIR_FORWARD);
  HAL_Delay(TMC2209_DRIVER_WAKEUP_DELAY_MS);

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
  GPIO_PinState target_direction;
  GPIO_PinState current_direction;
  uint16_t current_hz;
  uint16_t target_hz;
  uint16_t direction_switch_hz;
  uint8_t direction_switch_index;

  if ((handle == NULL) || (stage >= STEPPER_TMC2209_SPEED_STAGE_COUNT))
  {
    return HAL_ERROR;
  }

  target_direction = (stage < STEPPER_TMC2209_DIRECTION_SPLIT_STAGE) ? STEPPER_TMC2209_DIR_FORWARD : STEPPER_TMC2209_DIR_REVERSE;
  current_direction = (handle->speed_index < STEPPER_TMC2209_DIRECTION_SPLIT_STAGE) ? STEPPER_TMC2209_DIR_FORWARD : STEPPER_TMC2209_DIR_REVERSE;
  target_hz = handle->speed_hz[stage];
  current_hz = handle->speed_hz[handle->speed_index];
  if (current_hz == 0U)
  {
    current_hz = target_hz;
  }

  direction_switch_index = (target_direction == STEPPER_TMC2209_DIR_FORWARD) ? 0U : STEPPER_TMC2209_DIRECTION_SPLIT_STAGE;
  direction_switch_hz = handle->speed_hz[direction_switch_index];
  if (direction_switch_hz == 0U)
  {
    direction_switch_hz = target_hz;
  }

  if (current_direction != target_direction)
  {
    status = StepperTmc2209_RampStepFrequency(handle, current_hz, direction_switch_hz);
    if (status != HAL_OK)
    {
      return status;
    }

    StepperTmc2209_SetDirection(handle, target_direction);
    HAL_Delay(TMC2209_DIR_SWITCH_SETTLE_MS);
    status = StepperTmc2209_RampStepFrequency(handle, direction_switch_hz, target_hz);
  }
  else
  {
    StepperTmc2209_SetDirection(handle, target_direction);
    status = StepperTmc2209_RampStepFrequency(handle, current_hz, target_hz);
  }

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

  if (handle == NULL)
  {
    return HAL_ERROR;
  }

  next_stage = (uint8_t)((handle->speed_index + 1U) % STEPPER_TMC2209_SPEED_STAGE_COUNT);

  return StepperTmc2209_SetSpeedStage(handle, next_stage);
}

void StepperTmc2209_SetDirection(
    StepperTmc2209_HandleTypeDef *handle,
    GPIO_PinState direction_state)
{
  if (handle == NULL)
  {
    return;
  }

  HAL_GPIO_WritePin(handle->dir_gpio_port, handle->dir_pin, direction_state);
}

void StepperTmc2209_SetEnable(
    StepperTmc2209_HandleTypeDef *handle,
    GPIO_PinState enable_state)
{
  if (handle == NULL)
  {
    return;
  }

  HAL_GPIO_WritePin(handle->en_gpio_port, handle->en_pin, enable_state);
}

uint8_t StepperTmc2209_GetSpeedStage(
    const StepperTmc2209_HandleTypeDef *handle)
{
  if (handle == NULL)
  {
    return 0U;
  }

  return handle->speed_index;
}
