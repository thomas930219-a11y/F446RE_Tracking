# F446RE_TRACKING TMC2209 完全新手向 Code 導向 HackMD

> 更新日期：2026-03-21  
> 目前專案 microstep：`1/16`（由 `GCONF + CHOPCONF` 透過 UART 指定）  
> 主題：直接對著這份專案的 code 看懂 TMC2209，知道要去哪個檔案改什麼  
> 對象：第一次碰 STM32、第一次碰 TMC2209、想看完就能真的改 code 的人

[TOC]

---

## 0. 這份文件怎麼讀

這份不是「先講一堆抽象原理，再叫你自己回去對 code」的寫法。  
這份的規則很簡單，每一章都在回答這幾件事：

1. 你想做什麼
2. 去哪個檔案
3. 看哪段 code
4. 改成什麼
5. 改完會怎樣
6. 哪些東西要一起改

如果你現在最想知道的是：

- `ARR` 到底怎麼設：先看第 `4` 章
- 想改四檔速度：先看第 `5` 章
- 想改 microstep：先看第 `10` 章
- 想改電流：先看第 `11` 章
- 想知道 `2209 UART` 什麼時候會發：先看第 `1` 章和第 `3` 章

---

## 1. 先回答最重要的 4 個問題

### 1.1 馬達到底靠什麼轉

在這份專案裡：

- `UART4` / `UART5` 是拿來設定 TMC2209
- `TIM1_CH1` / `TIM3_CH1` 是拿來輸出 `STEP`
- `DIR` 決定方向
- `EN` 決定 driver 有沒有開

也就是：

> 馬達真正轉起來，靠的是 `STEP` 脈波，不是靠 UART 一直送命令。

### 1.2 `TMC2209 UART` 什麼時候會發

目前這份專案裡，TMC2209 的 UART 幾乎只在初始化時發。

真正送出 UART frame 的程式在：

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

```c
static HAL_StatusTypeDef StepperTmc2209_WriteRegister(
    const StepperTmc2209_HandleTypeDef *handle,
    uint8_t reg_addr,
    uint32_t reg_data)
{
  uint8_t tx_frame[TMC2209_FRAME_LEN];

  tx_frame[0] = TMC2209_SYNC_BYTE;
  tx_frame[1] = handle->slave_address;
  tx_frame[2] = (uint8_t)(reg_addr | TMC2209_WRITE_ACCESS);
  tx_frame[3] = (uint8_t)((reg_data >> 24U) & 0xFFU);
  tx_frame[4] = (uint8_t)((reg_data >> 16U) & 0xFFU);
  tx_frame[5] = (uint8_t)((reg_data >> 8U) & 0xFFU);
  tx_frame[6] = (uint8_t)(reg_data & 0xFFU);
  tx_frame[7] = StepperTmc2209_Crc8(tx_frame, TMC2209_FRAME_CRC_INPUT_LEN);

  return HAL_UART_Transmit(handle->huart_tmc, tx_frame, TMC2209_FRAME_LEN, TMC2209_UART_TIMEOUT_MS);
}
```

但這個函式只會在初始化流程裡被叫到：

```text
StepperTmc2209_Init()
  -> StepperTmc2209_ConfigDefaultRegisters()
      -> StepperTmc2209_WriteRegister()
```

所以現在的實際行為是：

- Motor1 初始化時，`UART4` 發 4 次寄存器寫入
- Motor2 初始化時，`UART5` 發 4 次寄存器寫入
- 之後按按鍵切速度、切方向、做 ramp，都不會再發 TMC2209 UART

### 1.3 `ARR` 到底在哪裡設

要分兩層看。

第一層是 `main.c` 的初值：

- [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c)

```c
htim1.Init.Prescaler = 84-1;
htim1.Init.Period = 1000;
```

```c
sConfigOC.Pulse = 500;
```

Motor2 一樣：

```c
htim3.Init.Prescaler = 84-1;
htim3.Init.Period = 1000;
```

```c
sConfigOC.Pulse = 500;
```

第二層是 driver 在運轉時動態重算的值：

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

```c
arr_value = counter_clock_hz / step_hz;
if (arr_value < 4U)
{
  arr_value = 4U;
}

arr_value -= 1U;
ccr_value = (arr_value + 1U) / 2U;

__HAL_TIM_SET_AUTORELOAD(handle->htim_step, arr_value);
__HAL_TIM_SET_COMPARE(handle->htim_step, handle->step_channel, ccr_value);
```

真正的結論是：

> `main.c` 設的是 Timer 初始 ARR，真正跑起來時的 ARR 是在 `StepperTmc2209_ApplyStepFrequency()` 裡用 `step_hz` 重算的。

### 1.4 改速度時為什麼看不到 UART 又送資料

因為改速度時，現在這份專案改的是：

- Timer 的 `ARR`
- Timer 的 `CCR`
- `DIR` pin

不是重新寫 TMC2209 寄存器。

所以：

- 改 `APP_MODE_*_STEP_HZ`
- 改 `speed_table[]`
- 改 `RAMP_STEP_HZ`

這些都不代表 UART 會再送一次。

---

## 2. 先認檔案：你要改什麼先看哪裡

| 你想改的事 | 最先看哪個檔案 |
| --- | --- |
| 四檔速度 | [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c) |
| 正反不同速 | [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c) |
| 按鍵切檔邏輯 | [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c) |
| microstep | [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c) |
| 電流 | [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c) |
| ramp | [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c) |
| `DIR` 正反邏輯 | [`Core/Inc/App/stepper_tmc2209.h`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Inc\App\stepper_tmc2209.h) |
| `EN` active-low / active-high | [`Core/Inc/App/stepper_tmc2209.h`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Inc\App\stepper_tmc2209.h) |
| UART baud rate | [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c) |
| STEP Timer 初值 | [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c) |
| STEP / UART 腳位實體映射 | [`Core/Src/stm32f4xx_hal_msp.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\stm32f4xx_hal_msp.c) |
| `DIR / EN` 腳位 define | [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c) |

---

## 3. 開機流程：code 真正怎麼跑

### 3.1 第一步：`main()` 先把外設建好

去哪個檔案：

- [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c)

看這段：

```c
MX_GPIO_Init();
MX_DMA_Init();
MX_USART2_UART_Init();
MX_ADC1_Init();
MX_ADC2_Init();
MX_USART1_UART_Init();
MX_UART4_Init();
MX_UART5_Init();
MX_TIM1_Init();
MX_TIM2_Init();
MX_TIM3_Init();
MX_TIM5_Init();

AppMain_Init(&hadc1, &hadc2, &huart2, &htim1, &htim3, &htim2, &htim5, &huart4, &huart5);
```

這段在做什麼：

- `TIM1` / `TIM3` 先建好
- `UART4` / `UART5` 先建好
- 然後把這些 handle 傳給 `AppMain_Init()`

### 3.2 第二步：`AppMain_Init()` 把資源餵進 driver

去哪個檔案：

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

看這段：

```c
motor_1_status = StepperTmc2209_Init(&g_stepper_1,
                                     htim_step_1,
                                     TIM_CHANNEL_1,
                                     huart_tmc_1,
                                     APP_MOTOR_1_DIR_GPIO_PORT,
                                     APP_MOTOR_1_DIR_PIN,
                                     APP_MOTOR_1_EN_GPIO_PORT,
                                     APP_MOTOR_1_EN_PIN,
                                     APP_TMC2209_MOTOR_1_SLAVE_ADDR,
                                     speed_table);
```

這一行裡面已經塞了 6 種東西：

- 用哪顆 driver handle
- 用哪顆 Timer 做 STEP
- 用哪個 PWM channel
- 用哪組 UART 跟 2209 說話
- `DIR / EN` 腳位在哪
- 這顆馬達的速度表是什麼

### 3.3 第三步：`StepperTmc2209_Init()` 做真正初始化

去哪個檔案：

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

看這段：

```c
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
```

這就是初始化真正順序：

1. 先 disable
2. 先設方向
3. 等 driver 喚醒
4. 寫 4 個 register
5. enable
6. 開 PWM
7. 設 stage 0

---

## 4. 程式怎麼設 `ARR`

這章只專心講你問的 `ARR`。

### 4.1 你想看「初始 ARR」去哪裡看

去哪個檔案：

- [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c)

看 Motor1：

```c
htim1.Instance = TIM1;
htim1.Init.Prescaler = 84-1;
htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
htim1.Init.Period = 1000;
```

看 Motor2：

```c
htim3.Instance = TIM3;
htim3.Init.Prescaler = 84-1;
htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
htim3.Init.Period = 1000;
```

這代表：

- 開機一開始，Timer 先有一個 `Period = 1000`
- 也就是先有一個初始 `ARR`

### 4.2 你想看「實際跑起來的 ARR」去哪裡看

去哪個檔案：

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

看 `StepperTmc2209_ApplyStepFrequency()`：

```c
timer_clock_hz = StepperTmc2209_GetTimerClock(handle->htim_step);
counter_clock_hz = timer_clock_hz / (handle->htim_step->Init.Prescaler + 1U);

arr_value = counter_clock_hz / step_hz;
if (arr_value < 4U)
{
  arr_value = 4U;
}

arr_value -= 1U;
ccr_value = (arr_value + 1U) / 2U;

__HAL_TIM_SET_AUTORELOAD(handle->htim_step, arr_value);
__HAL_TIM_SET_COMPARE(handle->htim_step, handle->step_channel, ccr_value);
```

### 4.3 這段 code 在做什麼

你可以這樣拆：

1. 先拿 Timer 時鐘
2. 再除以 prescaler，得到 counter clock
3. 再用目標 `step_hz` 算出週期要幾個 count
4. 用這個 count 設 `ARR`
5. 用一半設 `CCR`

### 4.4 這段 code 就是 `ARR` 的真正來源

一句話版本：

> `ARR` 是用 `counter_clock_hz / step_hz - 1` 算出來，然後用 `__HAL_TIM_SET_AUTORELOAD()` 寫進去。

### 4.5 這代表 `main.c` 的 `Period` 會怎樣

會被蓋掉。

所以如果你去改：

```c
htim1.Init.Period = 3000;
```

但後面 `StepperTmc2209_SetSpeedStage()` 又有跑過，那真正運轉的 `ARR` 還是會被重算。

### 4.6 你想做什麼：只是想讓馬達變慢

不要先去硬改 `ARR` 計算式。  
先改：

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

```c
#define APP_MODE_SLOW_STEP_HZ 100U
#define APP_MODE_FAST_STEP_HZ 500U
#define APP_MODE_ULTRA_STEP_HZ 1200U
#define APP_MODE_MAX_STEP_HZ 2400U
```

因為這樣改，driver 會自己把 `ARR` 算成對的值。

### 4.7 你想做什麼：固定寫死 ARR 來測試

去哪個檔案：

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

改這段：

```c
arr_value = counter_clock_hz / step_hz;
...
ccr_value = (arr_value + 1U) / 2U;
```

測試用改成：

```c
arr_value = 999U;
ccr_value = 500U;
```

改完會怎樣：

- 不管你 stage 是多少，速度都可能變成固定值
- 很適合只想先確認 `STEP` 腳是不是有乾淨方波
- 不適合正式邏輯

### 4.8 目前這份專案的 ARR 大概是多少

因為：

```c
Prescaler = 84 - 1
```

所以常用直覺是：

```text
counter_clock_hz ≈ 1MHz
```

這樣大約：

| step_hz | ARR | CCR |
| --- | --- | --- |
| 200 | 4999 | 2500 |
| 1400 | 713 | 357 |
| 5000 | 199 | 100 |
| 7500 | 132 | 66 |

---

## 5. 你想改四檔速度

### 5.1 去哪個檔案

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

### 5.2 看哪段 code

```c
#define APP_MODE_SLOW_STEP_HZ 200U
#define APP_MODE_FAST_STEP_HZ 1400U
#define APP_MODE_ULTRA_STEP_HZ 5000U
#define APP_MODE_MAX_STEP_HZ 7500U
```

### 5.3 你想做什麼：四檔全部降一半

改成：

```c
#define APP_MODE_SLOW_STEP_HZ 100U
#define APP_MODE_FAST_STEP_HZ 700U
#define APP_MODE_ULTRA_STEP_HZ 2500U
#define APP_MODE_MAX_STEP_HZ 3750U
```

改完會怎樣：

- stage `0~3` 會變慢
- stage `4~7` 也會跟著變慢
- `ARR / CCR` 會被 driver 自動重算

### 5.4 這題通常不用一起改什麼

通常先不用動：

- `stepper_tmc2209.c`
- UART
- GPIO
- CRC

### 5.5 什麼情況這題要搭配別的改

如果你是大幅提高速度，常要一起看：

- `IRUN`
- `RAMP_STEP_HZ`
- 機構負載

---

## 6. 你想讓正反不同速

### 6.1 去哪個檔案

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

### 6.2 看哪段 code

```c
static const uint16_t speed_table[STEPPER_TMC2209_SPEED_STAGE_COUNT] = {
    APP_MODE_SLOW_STEP_HZ, APP_MODE_FAST_STEP_HZ, APP_MODE_ULTRA_STEP_HZ, APP_MODE_MAX_STEP_HZ,
    APP_MODE_SLOW_STEP_HZ, APP_MODE_FAST_STEP_HZ, APP_MODE_ULTRA_STEP_HZ, APP_MODE_MAX_STEP_HZ};
```

這代表：

- 前 4 格是正轉
- 後 4 格是反轉

### 6.3 你想做什麼：反轉更保守

改成：

```c
static const uint16_t speed_table[STEPPER_TMC2209_SPEED_STAGE_COUNT] = {
    200U, 1400U, 5000U, 7500U,
    100U, 500U, 1200U, 2400U};
```

改完會怎樣：

- 正轉 `0~3` 不變
- 反轉 `4~7` 變慢

### 6.4 這題常要一起看什麼

如果反轉還是不穩，常一起看：

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

```c
#define TMC2209_RAMP_STEP_HZ 300U
#define TMC2209_DIR_SWITCH_SETTLE_MS 4U
```

---

## 7. 你想改按鍵切檔邏輯

### 7.1 去哪個檔案

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

### 7.2 看哪段 code

```c
if ((g_last_button_state == GPIO_PIN_SET) && (current_button_state == GPIO_PIN_RESET))
{
  if ((now_tick_ms - g_last_button_tick) >= APP_BUTTON_DEBOUNCE_MS)
  {
    g_last_button_tick = now_tick_ms;
    step_update_status = StepperTmc2209_NextSpeedStage(&g_stepper_1);
    if (step_update_status == HAL_OK)
    {
      next_stage = StepperTmc2209_GetSpeedStage(&g_stepper_1);
      (void)StepperTmc2209_SetSpeedStage(&g_stepper_2, next_stage);
    }
  }
}
```

### 7.3 你想做什麼：按一次只改 Motor1

把同步第二顆的部分拿掉：

```c
if (step_update_status == HAL_OK)
{
  /* 先不再同步 Motor2 */
}
```

### 7.4 你想做什麼：按一次跳兩檔

可以這樣改：

```c
step_update_status = StepperTmc2209_NextSpeedStage(&g_stepper_1);
if (step_update_status == HAL_OK)
{
  step_update_status = StepperTmc2209_NextSpeedStage(&g_stepper_1);
}
```

### 7.5 你想做什麼：debounce 更保守

改這個 define：

```c
#define APP_BUTTON_DEBOUNCE_MS 180U
```

例如：

```c
#define APP_BUTTON_DEBOUNCE_MS 300U
```

---

## 8. 你想改方向正反

### 8.1 去哪個檔案

- [`Core/Inc/App/stepper_tmc2209.h`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Inc\App\stepper_tmc2209.h)

### 8.2 看哪段 code

```c
#define STEPPER_TMC2209_DIR_FORWARD GPIO_PIN_RESET
#define STEPPER_TMC2209_DIR_REVERSE GPIO_PIN_SET
```

### 8.3 你想做什麼：整個方向相反

改成：

```c
#define STEPPER_TMC2209_DIR_FORWARD GPIO_PIN_SET
#define STEPPER_TMC2209_DIR_REVERSE GPIO_PIN_RESET
```

改完會怎樣：

- 所有 Forward / Reverse 定義都反過來

### 8.4 這題通常不用一起改什麼

通常不用先碰：

- `speed_table`
- UART
- `MRES`

---

## 9. 你想改 `EN` 邏輯

### 9.1 去哪個檔案

- [`Core/Inc/App/stepper_tmc2209.h`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Inc\App\stepper_tmc2209.h)

### 9.2 看哪段 code

```c
#define STEPPER_TMC2209_ENABLE GPIO_PIN_RESET
#define STEPPER_TMC2209_DISABLE GPIO_PIN_SET
```

### 9.3 你想做什麼：如果你板子是 active-high enable

改成：

```c
#define STEPPER_TMC2209_ENABLE GPIO_PIN_SET
#define STEPPER_TMC2209_DISABLE GPIO_PIN_RESET
```

### 9.4 什麼症狀最像這題

如果你看到：

- `STEP` 有波
- `DIR` 也會切
- 馬達卻完全沒力

這題一定先查。

---

## 10. 你想改 microstep

### 10.1 去哪個檔案

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

### 10.2 看哪段 code

目前專案在 [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c) 裡，是真的用下面這組常數決定 microstep：

```c
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
```

初始化時會真的把這兩個值寫進 driver：

```c
status = StepperTmc2209_WriteRegister(handle, TMC2209_REG_GCONF, TMC2209_GCONF_VALUE);
...
status = StepperTmc2209_WriteRegister(handle, TMC2209_REG_CHOPCONF, TMC2209_CHOPCONF_VALUE);
```

`MRES` 對照表如下，改 microstep 時可以直接對著這張表看：

| `MRES` 欄位值 | 細分 | `200 full-step/rev` 時每圈 STEP pulse |
| --- | --- | --- |
| `0x00` | `1/256` | `51200` |
| `0x01` | `1/128` | `25600` |
| `0x02` | `1/64` | `12800` |
| `0x03` | `1/32` | `6400` |
| `0x04` | `1/16` | `3200` |
| `0x05` | `1/8` | `1600` |
| `0x06` | `1/4` | `800` |
| `0x07` | `1/2` | `400` |
| `0x08` | `full-step` | `200` |

### 10.3 目前專案就是 `1/16`

目前實際寫進去的是：

```text
GCONF    = 0x000000C0
CHOPCONF = 0x14000053
```

白話就是：

- `pdn_disable = 1`
- `mstep_reg_select = 1`
- `MRES = 1/16`

如果馬達是常見的 `200 full-step/rev`，那現在就是：

```text
1 rev = 200 x 16 = 3200 STEP pulse
```

### 10.4 你想做什麼：改成 `1/8`

改成：

```c
#define TMC2209_CHOPCONF_MRES_1_8 0x05UL
#define TMC2209_CHOPCONF_VALUE \
  ((TMC2209_CHOPCONF_BASE_VALUE & ~TMC2209_CHOPCONF_MRES_MASK) | \
   (TMC2209_CHOPCONF_MRES_1_8 << TMC2209_CHOPCONF_MRES_SHIFT))
```

### 10.5 你想做什麼：改成 `1/32`

改成：

```c
#define TMC2209_CHOPCONF_MRES_1_32 0x03UL
#define TMC2209_CHOPCONF_VALUE \
  ((TMC2209_CHOPCONF_BASE_VALUE & ~TMC2209_CHOPCONF_MRES_MASK) | \
   (TMC2209_CHOPCONF_MRES_1_32 << TMC2209_CHOPCONF_MRES_SHIFT))
```

### 10.6 只改這裡會怎樣

只改 `MRES`，系統通常還能跑。  
但：

- 同樣 `step_hz` 下，實際 RPM 會變
- 你原本心裡對四檔速度的感覺會全部改掉

### 10.7 這題常要一起改什麼

如果你想維持差不多同樣 RPM，常一起改：

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

像這樣：

```c
#define APP_MODE_SLOW_STEP_HZ 100U
#define APP_MODE_FAST_STEP_HZ 700U
#define APP_MODE_ULTRA_STEP_HZ 2500U
#define APP_MODE_MAX_STEP_HZ 3750U
```

---

## 11. 你想改電流

### 11.1 去哪個檔案

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

### 11.2 看哪段 code

```c
#define TMC2209_IHOLD 6U
#define TMC2209_IRUN 16U
#define TMC2209_IHOLDDELAY 4U
```

這一章目前在這份專案裡能直接設定的，只有下面這 `3` 個值：

| 參數 | 作用 | 可設範圍 | 目前值 |
| --- | --- | --- | --- |
| `TMC2209_IHOLD` | 停住時的保持電流等級 | `0 ~ 31` | `6` |
| `TMC2209_IRUN` | 運轉時的電流等級 | `0 ~ 31` | `16` |
| `TMC2209_IHOLDDELAY` | 從 `IRUN` 慢慢降到 `IHOLD` 的過渡等級 | `0 ~ 15` | `4` |

注意兩件事：

- 這裡填的是 `TMC2209 IHOLD_IRUN` 寄存器欄位值，不是直接填「幾 mA」
- 這份 code 目前是直接把值組進寄存器，沒有先做 mask，所以不要填超出範圍的值

### 11.3 你想做什麼：運轉更有力

先改：

```c
#define TMC2209_IRUN 20U
```

### 11.4 你想做什麼：停住更有保持力

改：

```c
#define TMC2209_IHOLD 10U
```

### 11.5 這題通常單改就好的是哪個

- 沒力：先動 `IRUN`
- 停住太弱：先動 `IHOLD`

### 11.6 什麼情況不是只加電流就會好

如果你是：

- 高速掉步
- 反轉失敗
- 馬達很燙

那通常還要一起看：

- `APP_MODE_MAX_STEP_HZ`
- `RAMP_STEP_HZ`
- `DIR_SWITCH_SETTLE_MS`

---

## 12. 你想改 ramp 和反轉保護

### 12.1 去哪個檔案

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

### 12.2 看哪段 code

```c
#define TMC2209_RAMP_STEP_HZ 800U
#define TMC2209_RAMP_DELAY_MS 1U
#define TMC2209_DIR_SWITCH_SETTLE_MS 2U
```

這一章目前在這份專案裡能直接設定的，只有下面這 `3` 個值：

| 參數 | 作用 | 可設範圍 | 目前值 |
| --- | --- | --- | --- |
| `TMC2209_RAMP_STEP_HZ` | 每次 ramp 要增減多少 `step_hz` | `1 ~ 65535` | `800` |
| `TMC2209_RAMP_DELAY_MS` | 每一步 ramp 中間要等幾毫秒 | `0` 以上整數 `ms` | `1` |
| `TMC2209_DIR_SWITCH_SETTLE_MS` | 切換 `DIR` 後先等多久再重新加速 | `0` 以上整數 `ms` | `2` |

注意幾件事：

- `TMC2209_RAMP_STEP_HZ` 如果你設成 `0`，這份 code 進 runtime 之後會自動當成 `1`
- `TMC2209_RAMP_STEP_HZ` 的實際有效上限，還是會受 `target_hz` / `speed_table[]` 影響；設太大就會越來越像直接跳速
- `TMC2209_RAMP_DELAY_MS` 和 `TMC2209_DIR_SWITCH_SETTLE_MS` 目前都是直接丟給 `HAL_Delay()`
- 這份專案目前沒有分開的 `accel ramp` / `decel ramp` / `reverse brake` 三套獨立參數
- 反轉時中間會先降到 `speed_table[0]` 或 `speed_table[4]` 對應的速度，再切方向，所以反轉手感常常也要一起改 [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c) 裡的 `speed_table[]`

### 12.3 你想做什麼：切速太猛

改成：

```c
#define TMC2209_RAMP_STEP_HZ 300U
#define TMC2209_RAMP_DELAY_MS 2U
```

### 12.4 你想做什麼：反轉容易失敗

先改：

```c
#define TMC2209_DIR_SWITCH_SETTLE_MS 4U
```

如果還不穩，再一起搭：

```c
#define TMC2209_RAMP_STEP_HZ 300U
```

### 12.5 這題最像真的會怎麼一起改

常見保守版：

```c
/* app_main.c */
static const uint16_t speed_table[STEPPER_TMC2209_SPEED_STAGE_COUNT] = {
    200U, 1400U, 5000U, 7500U,
    100U, 500U, 1200U, 2400U};
```

```c
/* stepper_tmc2209.c */
#define TMC2209_RAMP_STEP_HZ 300U
#define TMC2209_DIR_SWITCH_SETTLE_MS 4U
```

---

## 13. 你想改 UART baud rate 或 UART 埠

### 13.1 去哪個檔案

- [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c)
- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)
- [`Core/Src/stm32f4xx_hal_msp.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\stm32f4xx_hal_msp.c)

### 13.2 你想做什麼：只改 baud rate

改 `main.c`：

```c
huart4.Init.BaudRate = 115200;
huart5.Init.BaudRate = 115200;
```

### 13.3 你想做什麼：換成別的 UART handle

除了 `main.c`，還要看：

```c
AppMain_Init(&hadc1, &hadc2, &huart2, &htim1, &htim3, &htim2, &htim5, &huart4, &huart5);
```

以及 MSP 腳位對應：

```text
PC10/PC11 -> UART4
PC12/PD2  -> UART5
```

也就是：

- 改 handle
- 改腳位初始化
- 改實體接線

這三個通常要一起對。

---

## 14. 你想改 STEP Timer 或 STEP 腳

### 14.1 去哪個檔案

- [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c)
- [`Core/Src/stm32f4xx_hal_msp.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\stm32f4xx_hal_msp.c)
- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

### 14.2 看哪段 code

`main.c`：

```c
htim1.Instance = TIM1;
HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
```

```c
htim3.Instance = TIM3;
HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);
```

MSP：

```text
PA8 -> TIM1_CH1
PA6 -> TIM3_CH1
```

### 14.3 你想做什麼：只改初始 Period / Pulse

改：

```c
htim1.Init.Period = 1000;
sConfigOC.Pulse = 500;
```

但要記得：

> 這只是初始值，後面會被 `StepperTmc2209_ApplyStepFrequency()` 蓋掉。

### 14.4 你想做什麼：真的換成別的 Timer

通常要一起改三層：

1. `main.c`  
   新 Timer 要被初始化成 PWM

2. `stm32f4xx_hal_msp.c`  
   新 Timer 的腳位 alternate function 要設對

3. `app_main.c`  
   `StepperTmc2209_Init()` 要傳新的 `htim`

---

## 15. 你想改 `DIR / EN` 腳位本身

### 15.1 去哪個檔案

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)
- [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c)

### 15.2 看哪段 code

`app_main.c`：

```c
#define APP_MOTOR_1_DIR_GPIO_PORT GPIOC
#define APP_MOTOR_1_DIR_PIN GPIO_PIN_6
#define APP_MOTOR_1_EN_GPIO_PORT GPIOB
#define APP_MOTOR_1_EN_PIN GPIO_PIN_8
```

`main.c`：

```c
GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_8|GPIO_PIN_9;
GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

GPIO_InitStruct.Pin = GPIO_PIN_8;
GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
```

### 15.3 這題最容易漏掉什麼

只改了 `app_main.c` 的 define，卻沒改 `MX_GPIO_Init()`。  
結果就是：

- 程式看起來像有換腳
- 但新腳位其實根本沒被設成 output

---

## 16. 你想只保留一顆馬達，先把問題縮小

### 16.1 去哪個檔案

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

### 16.2 可以怎麼改

先把第二顆初始化拿掉：

```c
/* motor_2_status = StepperTmc2209_Init(...); */
```

再把同步第二顆這段拿掉：

```c
if (step_update_status == HAL_OK)
{
  next_stage = StepperTmc2209_GetSpeedStage(&g_stepper_1);
  (void)StepperTmc2209_SetSpeedStage(&g_stepper_2, next_stage);
}
```

### 16.3 改完會怎樣

- 系統先只剩 Motor1
- 很適合把問題縮到最小

---

## 17. 你想讓運轉中也能發 UART 改參數

### 17.1 為什麼現在做不到

因為真正寫寄存器的函式是：

```c
static HAL_StatusTypeDef StepperTmc2209_WriteRegister(...)
```

它是 `static`，外面的 `app_main.c` 看不到。

### 17.2 你要先補哪個檔案

- [`Core/Inc/App/stepper_tmc2209.h`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Inc\App\stepper_tmc2209.h)
- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)

### 17.3 範例：做一個 runtime 改電流 API

`.h`：

```c
HAL_StatusTypeDef StepperTmc2209_SetRunCurrent(
    StepperTmc2209_HandleTypeDef *handle,
    uint8_t ihold,
    uint8_t irun,
    uint8_t iholddelay);
```

`.c`：

```c
HAL_StatusTypeDef StepperTmc2209_SetRunCurrent(
    StepperTmc2209_HandleTypeDef *handle,
    uint8_t ihold,
    uint8_t irun,
    uint8_t iholddelay)
{
  uint32_t value;

  if (handle == NULL)
  {
    return HAL_ERROR;
  }

  value = (((uint32_t)iholddelay) << 16U) |
          (((uint32_t)irun) << 8U) |
          ((uint32_t)ihold);

  return StepperTmc2209_WriteRegister(handle, TMC2209_REG_IHOLD_IRUN, value);
}
```

### 17.4 然後你才能在 `app_main.c` 呼叫

```c
(void)StepperTmc2209_SetRunCurrent(&g_stepper_1, 8U, 20U, 4U);
```

---

## 18. `m1 / m2` log 到底是什麼

### 18.1 去哪個檔案

- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

### 18.2 最重要的事

`m1` / `m2` 不是 TMC2209 回報值。  
它們只是：

```c
StepperTmc2209_GetSpeedStage(...)
```

拿到的 `speed_index`。

所以：

- `m1 = 3` 的意思是「軟體目前認為 Motor1 在 stage 3」
- 不是「TMC2209 透過 UART 回報 stage 3」

---

## 19. 常見除錯：症狀對哪個檔案

### 19.1 一開機就 `INIT ERROR`

先看：

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)
- [`Core/Src/main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c)

先查：

- UART4 / UART5 初始化
- `StepperTmc2209_ConfigDefaultRegisters()`
- 驅動器供電

### 19.2 `STEP` 沒波

先看：

- [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)
- [`Core/Src/stm32f4xx_hal_msp.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\stm32f4xx_hal_msp.c)

先查：

```c
HAL_TIM_PWM_Start(handle->htim_step, handle->step_channel);
```

以及：

```text
PA8 -> TIM1_CH1
PA6 -> TIM3_CH1
```

### 19.3 `STEP` 有波，但馬達完全不轉

先看：

- [`Core/Inc/App/stepper_tmc2209.h`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Inc\App\stepper_tmc2209.h)
- [`Core/Src/App/app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)

先查：

- `ENABLE / DISABLE` 宏
- `EN` 腳位 define
- 馬達供電和線圈接法

### 19.4 低速正常，高速掉步

先試：

1. 降 `APP_MODE_MAX_STEP_HZ`
2. 加 `IRUN`
3. 降 `RAMP_STEP_HZ`

### 19.5 反轉容易失敗

先試：

1. reverse 側 `speed_table[]` 更保守
2. `DIR_SWITCH_SETTLE_MS` 加大
3. `RAMP_STEP_HZ` 調小

---

## 20. 一張最實用的修改地圖

| 你想做的事 | 先改哪個檔案 | 先改哪段 code |
| --- | --- | --- |
| 四檔速度 | `app_main.c` | `APP_MODE_*_STEP_HZ` |
| 正反不同速 | `app_main.c` | `speed_table[]` |
| 按鍵切檔 | `app_main.c` | `AppMain_Task()` |
| 正反方向 | `stepper_tmc2209.h` | `DIR_FORWARD / DIR_REVERSE` |
| 使能邏輯 | `stepper_tmc2209.h` | `ENABLE / DISABLE` |
| microstep | `stepper_tmc2209.c` | `GCONF / CHOPCONF / MRES` |
| 電流 | `stepper_tmc2209.c` | `IHOLD / IRUN / IHOLDDELAY` |
| ramp | `stepper_tmc2209.c` | `RAMP_STEP_HZ / RAMP_DELAY_MS` |
| 反轉保護 | `stepper_tmc2209.c` | `DIR_SWITCH_SETTLE_MS` |
| UART baud | `main.c` | `huart4.Init.BaudRate / huart5.Init.BaudRate` |
| Timer 初始 ARR | `main.c` | `htim1.Init.Period / htim3.Init.Period` |
| 真正運轉 ARR | `stepper_tmc2209.c` | `StepperTmc2209_ApplyStepFrequency()` |
| runtime 用 UART 改寄存器 | `stepper_tmc2209.h/.c` | 先補 public API |

---

## 21. 最後總結

如果你要只記住最有用的版本，請記這 5 句：

1. `ARR` 初值在 [`main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\main.c)，但真正跑起來的 ARR 在 [`stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c) 裡重算。
2. 想改速度，先改 [`app_main.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\app_main.c)，不是先改 Timer 公式。
3. 想改 microstep、電流、ramp，先改 [`stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c)，其中 microstep 目前是靠 `GCONF + CHOPCONF` 指定。
4. 目前 TMC2209 UART 主要只在初始化時發。
5. 想在運轉中也透過 UART 改參數，現在要先補 driver API。

---

## 附錄 A：目前專案最重要的實際設定

### A.1 STEP

```text
Motor1 -> TIM1_CH1 -> PA8
Motor2 -> TIM3_CH1 -> PA6
```

### A.2 TMC UART

```text
Motor1 -> UART4 -> PC10/PC11 -> 115200
Motor2 -> UART5 -> PC12/PD2  -> 115200
```

### A.3 DIR / EN

```text
Motor1 DIR -> PC6
Motor1 EN  -> PB8
Motor2 DIR -> PC8
Motor2 EN  -> PC9
```

### A.4 速度

```c
200 / 1400 / 5000 / 7500 Hz
```

### A.5 microstep

```text
GCONF    = 0x000000C0
CHOPCONF = 0x14000053
MRES     = 1/16
```

### A.6 電流

```c
IHOLD = 6
IRUN = 16
IHOLDDELAY = 4
```

### A.7 ramp

```c
RAMP_STEP_HZ = 800
RAMP_DELAY_MS = 1
DIR_SWITCH_SETTLE_MS = 2
```
