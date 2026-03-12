#include "App/uart_sequence.h"

static uint16_t UartSequence_AppendText(
    uint8_t *buffer,
    uint16_t index,
    const char *text)
{
  while (*text != '\0')
  {
    buffer[index] = (uint8_t)*text;
    index++;
    text++;
  }

  return index;
}

static uint16_t UartSequence_AppendU32(
    uint8_t *buffer,
    uint16_t index,
    uint32_t value)
{
  uint32_t divisor = 1000000000U;
  uint8_t started = 0U;

  while (divisor != 0U)
  {
    uint8_t digit = (uint8_t)(value / divisor);

    if ((digit != 0U) || (started != 0U) || (divisor == 1U))
    {
      buffer[index] = (uint8_t)('0' + digit);
      index++;
      started = 1U;
    }

    value %= divisor;
    divisor /= 10U;
  }

  return index;
}

void UartSequence_Init(
    UartSequence_HandleTypeDef *handle,
    UART_HandleTypeDef *huart,
    uint32_t period_ms)
{
  if ((handle == (void *)0) || (huart == (void *)0))
  {
    return;
  }

  handle->huart = huart;
  handle->period_ms = period_ms;
  handle->last_tick_ms = HAL_GetTick();
  handle->serial_number = 0U;
}

void UartSequence_Task(UartSequence_HandleTypeDef *handle)
{
  uint8_t tx_buffer[24];
  uint16_t tx_length = 0U;
  uint32_t now_tick_ms;

  if ((handle == (void *)0) || (handle->huart == (void *)0))
  {
    return;
  }

  now_tick_ms = HAL_GetTick();
  if ((now_tick_ms - handle->last_tick_ms) < handle->period_ms)
  {
    return;
  }

  handle->last_tick_ms = now_tick_ms;

  tx_length = UartSequence_AppendText(tx_buffer, tx_length, "SEQ=");
  tx_length = UartSequence_AppendU32(tx_buffer, tx_length, handle->serial_number);
  tx_length = UartSequence_AppendText(tx_buffer, tx_length, "\r\n");

  if (HAL_UART_Transmit(handle->huart, tx_buffer, tx_length, 30U) == HAL_OK)
  {
    handle->serial_number++;
  }
}
