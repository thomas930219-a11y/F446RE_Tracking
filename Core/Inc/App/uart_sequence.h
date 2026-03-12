#ifndef APP_UART_SEQUENCE_H
#define APP_UART_SEQUENCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef struct
{
  UART_HandleTypeDef *huart;
  uint32_t period_ms;
  uint32_t last_tick_ms;
  uint32_t serial_number;
} UartSequence_HandleTypeDef;

void UartSequence_Init(
    UartSequence_HandleTypeDef *handle,
    UART_HandleTypeDef *huart,
    uint32_t period_ms);

void UartSequence_Task(UartSequence_HandleTypeDef *handle);

#ifdef __cplusplus
}
#endif

#endif /* APP_UART_SEQUENCE_H */
