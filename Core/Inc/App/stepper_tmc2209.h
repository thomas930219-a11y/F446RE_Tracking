#ifndef APP_STEPPER_TMC2209_H
#define APP_STEPPER_TMC2209_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define STEPPER_TMC2209_SPEED_STAGE_COUNT 10U
#define STEPPER_TMC2209_DIRECTION_SPLIT_STAGE (STEPPER_TMC2209_SPEED_STAGE_COUNT / 2U)
#define STEPPER_TMC2209_DIR_FORWARD GPIO_PIN_RESET
#define STEPPER_TMC2209_DIR_REVERSE GPIO_PIN_SET
#define STEPPER_TMC2209_ENABLE GPIO_PIN_RESET
#define STEPPER_TMC2209_DISABLE GPIO_PIN_SET

typedef struct
{
  TIM_HandleTypeDef *htim_step;
  uint32_t step_channel;
  UART_HandleTypeDef *huart_tmc;
  GPIO_TypeDef *dir_gpio_port;
  uint16_t dir_pin;
  GPIO_TypeDef *en_gpio_port;
  uint16_t en_pin;
  uint8_t slave_address;
  uint16_t speed_hz[STEPPER_TMC2209_SPEED_STAGE_COUNT];
  uint8_t speed_index;
} StepperTmc2209_HandleTypeDef;

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
    const uint16_t speed_table_hz[STEPPER_TMC2209_SPEED_STAGE_COUNT]);

HAL_StatusTypeDef StepperTmc2209_SetSpeedStage(
    StepperTmc2209_HandleTypeDef *handle,
    uint8_t stage);

HAL_StatusTypeDef StepperTmc2209_NextSpeedStage(
    StepperTmc2209_HandleTypeDef *handle);

void StepperTmc2209_SetDirection(
    StepperTmc2209_HandleTypeDef *handle,
    GPIO_PinState direction_state);

void StepperTmc2209_SetEnable(
    StepperTmc2209_HandleTypeDef *handle,
    GPIO_PinState enable_state);

uint8_t StepperTmc2209_GetSpeedStage(
    const StepperTmc2209_HandleTypeDef *handle);

#ifdef __cplusplus
}
#endif

#endif /* APP_STEPPER_TMC2209_H */
