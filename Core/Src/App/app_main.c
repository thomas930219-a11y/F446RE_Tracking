#include "App/app_main.h"
#include "App/stepper_tmc2209.h"
#include "App/uart_sequence.h"

#define APP_BUTTON_DEBOUNCE_MS 180U
#define APP_LOG_PERIOD_MS 100U
#define APP_TMC2209_SLAVE_ADDR 0x00U

static StepperTmc2209_HandleTypeDef g_stepper_1;
static UartSequence_HandleTypeDef g_uart_seq;
static GPIO_PinState g_last_button_state = GPIO_PIN_SET;
static uint32_t g_last_button_tick = 0U;

void AppMain_Init(ADC_HandleTypeDef *hadc1,
                  ADC_HandleTypeDef *hadc2,
                  UART_HandleTypeDef *huart_log,
                  TIM_HandleTypeDef *htim_step_1,
                  TIM_HandleTypeDef *htim_step_2,
                  TIM_HandleTypeDef *htim_enc_1,
                  TIM_HandleTypeDef *htim_enc_2,
                  UART_HandleTypeDef *huart_tmc_1,
                  UART_HandleTypeDef *huart_tmc_2)
{
  static const uint16_t speed_table[STEPPER_TMC2209_SPEED_STAGE_COUNT] = {
      200U, 400U, 700U, 1000U, 1400U,
      200U, 400U, 700U, 1000U, 1400U};
  HAL_StatusTypeDef status;
  const uint8_t ready_text[] = "APP READY\r\n";
  const uint8_t error_text[] = "TMC2209 INIT ERROR\r\n";

  (void)hadc1;
  (void)hadc2;
  (void)htim_step_2;
  (void)htim_enc_1;
  (void)htim_enc_2;
  (void)huart_tmc_2;

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);

  status = StepperTmc2209_Init(&g_stepper_1,
                               htim_step_1,
                               TIM_CHANNEL_1,
                               huart_tmc_1,
                               GPIOC,
                               GPIO_PIN_6,
                               GPIOB,
                               GPIO_PIN_8,
                               APP_TMC2209_SLAVE_ADDR,
                               speed_table);

  g_last_button_state = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);
  g_last_button_tick = HAL_GetTick();

  UartSequence_Init(&g_uart_seq, huart_log, APP_LOG_PERIOD_MS);

  if (status == HAL_OK)
  {
    (void)HAL_UART_Transmit(huart_log, (uint8_t *)ready_text, (uint16_t)(sizeof(ready_text) - 1U), 50U);
  }
  else
  {
    (void)HAL_UART_Transmit(huart_log, (uint8_t *)error_text, (uint16_t)(sizeof(error_text) - 1U), 50U);
  }
}

void AppMain_Task(void)
{
  GPIO_PinState current_button_state;
  uint32_t now_tick;

  now_tick = HAL_GetTick();
  current_button_state = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);

  if ((g_last_button_state == GPIO_PIN_SET) && (current_button_state == GPIO_PIN_RESET))
  {
    if ((now_tick - g_last_button_tick) >= APP_BUTTON_DEBOUNCE_MS)
    {
      g_last_button_tick = now_tick;
      (void)StepperTmc2209_NextSpeedStage(&g_stepper_1);
    }
  }

  g_last_button_state = current_button_state;
  UartSequence_Task(&g_uart_seq);
}
