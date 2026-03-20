#include "App/app_adc.h"

#define APP_ADC_FILTER_WEIGHT_PREV 700U
#define APP_ADC_FILTER_WEIGHT_NEW 300U
#define APP_ADC_FILTER_WEIGHT_SCALE 1000U
#define APP_ADC_FILTER_ROUNDING (APP_ADC_FILTER_WEIGHT_SCALE / 2U)
#define APP_ADC1_INDEX 0U
#define APP_ADC2_INDEX 1U
#define APP_ADC3_INDEX 2U
#define APP_ADC4_INDEX 3U

static uint16_t AppAdc_ApplyLowPass(uint16_t previous, uint16_t sample)
{
  uint32_t weighted_previous;
  uint32_t weighted_sample;
  uint32_t blended_value;

  weighted_previous = (uint32_t)previous * APP_ADC_FILTER_WEIGHT_PREV;
  weighted_sample = (uint32_t)sample * APP_ADC_FILTER_WEIGHT_NEW;
  blended_value = weighted_previous + weighted_sample + APP_ADC_FILTER_ROUNDING;

  return (uint16_t)(blended_value / APP_ADC_FILTER_WEIGHT_SCALE);
}

static void AppAdc_UpdateFilter(
    uint16_t raw_value,
    uint16_t *filtered_value,
    uint8_t *is_seeded)
{
  if (*is_seeded == 0U)
  {
    *filtered_value = raw_value;
    *is_seeded = 1U;
  }
  else
  {
    *filtered_value = AppAdc_ApplyLowPass(*filtered_value, raw_value);
  }
}

static HAL_StatusTypeDef AppAdc_ValidateDmaConfig(
    const ADC_HandleTypeDef *hadc,
    uint32_t sample_count)
{
  if ((hadc == NULL) ||
      (sample_count == 0U) ||
      (hadc->Init.ScanConvMode == DISABLE) ||
      (hadc->Init.ContinuousConvMode == DISABLE) ||
      (hadc->Init.DMAContinuousRequests == DISABLE) ||
      (hadc->Init.NbrOfConversion != sample_count))
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

static HAL_StatusTypeDef AppAdc_StartDmaChannel(
    ADC_HandleTypeDef *hadc,
    volatile uint16_t *dma_target,
    uint32_t sample_count)
{
  HAL_StatusTypeDef status;

  if ((hadc == NULL) || (dma_target == NULL) || (sample_count == 0U))
  {
    return HAL_ERROR;
  }

  if (AppAdc_ValidateDmaConfig(hadc, sample_count) != HAL_OK)
  {
    return HAL_ERROR;
  }

  status = HAL_ADC_Start_DMA(hadc, (uint32_t *)dma_target, sample_count);
  if (status != HAL_OK)
  {
    return status;
  }

  if (hadc->DMA_Handle != NULL)
  {
    /* Keep circular DMA running without per-sample IRQ load. */
    __HAL_DMA_DISABLE_IT(hadc->DMA_Handle, DMA_IT_HT);
    __HAL_DMA_DISABLE_IT(hadc->DMA_Handle, DMA_IT_TC);
  }

  return HAL_OK;
}

static uint16_t AppAdc_GetFilteredValue(
    const AppAdc_HandleTypeDef *handle,
    uint8_t logical_index)
{
  if ((handle == NULL) || (logical_index >= APP_ADC_LOGICAL_CHANNEL_COUNT))
  {
    return 0U;
  }

  return handle->filtered_adc[logical_index];
}

void AppAdc_Init(
    AppAdc_HandleTypeDef *handle,
    ADC_HandleTypeDef *hadc1,
    ADC_HandleTypeDef *hadc2)
{
  uint32_t index;

  if ((handle == NULL) || (hadc1 == NULL) || (hadc2 == NULL))
  {
    return;
  }

  handle->hadc1 = hadc1;
  handle->hadc2 = hadc2;

  for (index = 0U; index < APP_ADC_DEVICE_CHANNEL_COUNT; index++)
  {
    handle->dma_adc1[index] = 0U;
    handle->dma_adc2[index] = 0U;
  }

  for (index = 0U; index < APP_ADC_LOGICAL_CHANNEL_COUNT; index++)
  {
    handle->raw_adc[index] = 0U;
    handle->filtered_adc[index] = 0U;
    handle->adc_seeded[index] = 0U;
  }

  if (AppAdc_StartDmaChannel(handle->hadc1, handle->dma_adc1, APP_ADC_DEVICE_CHANNEL_COUNT) != HAL_OK)
  {
    Error_Handler();
  }

  if (AppAdc_StartDmaChannel(handle->hadc2, handle->dma_adc2, APP_ADC_DEVICE_CHANNEL_COUNT) != HAL_OK)
  {
    Error_Handler();
  }
}

void AppAdc_Task(AppAdc_HandleTypeDef *handle)
{
  uint32_t index;

  if ((handle == NULL) || (handle->hadc1 == NULL) || (handle->hadc2 == NULL))
  {
    return;
  }

  /* Preserve the existing adc1/adc2 order, then append the two new inputs as adc3/adc4. */
  handle->raw_adc[APP_ADC1_INDEX] = handle->dma_adc1[0];
  handle->raw_adc[APP_ADC2_INDEX] = handle->dma_adc2[0];
  handle->raw_adc[APP_ADC3_INDEX] = handle->dma_adc1[1];
  handle->raw_adc[APP_ADC4_INDEX] = handle->dma_adc2[1];

  for (index = 0U; index < APP_ADC_LOGICAL_CHANNEL_COUNT; index++)
  {
    AppAdc_UpdateFilter(
        handle->raw_adc[index],
        &handle->filtered_adc[index],
        &handle->adc_seeded[index]);
  }
}

uint16_t AppAdc_GetFilteredAdc1(const AppAdc_HandleTypeDef *handle)
{
  return AppAdc_GetFilteredValue(handle, APP_ADC1_INDEX);
}

uint16_t AppAdc_GetFilteredAdc2(const AppAdc_HandleTypeDef *handle)
{
  return AppAdc_GetFilteredValue(handle, APP_ADC2_INDEX);
}

uint16_t AppAdc_GetFilteredAdc3(const AppAdc_HandleTypeDef *handle)
{
  return AppAdc_GetFilteredValue(handle, APP_ADC3_INDEX);
}

uint16_t AppAdc_GetFilteredAdc4(const AppAdc_HandleTypeDef *handle)
{
  return AppAdc_GetFilteredValue(handle, APP_ADC4_INDEX);
}
