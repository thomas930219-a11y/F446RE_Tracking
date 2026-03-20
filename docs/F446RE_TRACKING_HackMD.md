# F446RE_TRACKING HackMD（Pin / 功能 / 封包 / PWM / ADC）

> 更新日期: 2026-03-21  
> MCU: STM32F446RE  

## 1. 專案在做什麼（先講白話）

這個專案同時控制兩顆步進馬達（TMC2209），並且每 100ms 用 UART 輸出一次系統狀態。  
你可以把它想成「雙馬達測試台」：

- 按一下按鍵，兩顆馬達一起切到下一個模式（慢順 -> 快順 -> 超快順 -> 極速順 -> 慢逆 -> 快逆 -> 超快逆 -> 極速逆）。
- 四路 ADC 邏輯值由 ADC1/ADC2 的雙通道 scan DMA 持續收值，主迴圈只做濾波。
- 兩路 Encoder 持續累計位置，並換算成角度（0.0000 ~ 359.9999）。
- UART 封包把上述資訊整合成一行，方便看終端機或丟上位機。

---

## 2. Pin 使用總表

| 類別 | 功能 | MCU Pin | 外設 | I/O | 備註 |
|---|---|---|---|---|---|
| Motor1 | STEP | `PA8` | `TIM1_CH1` | Out | 脈波輸出到 TMC2209 #1 STEP |
| Motor1 | DIR | `PC6` | GPIO | Out | Motor1 方向控制 |
| Motor1 | EN | `PB8` | GPIO | Out | Motor1 enable，active-low |
| Motor1 | TMC UART TX | `PC10` | `UART4_TX` | Out | TMC2209 #1 設定封包 |
| Motor1 | TMC UART RX | `PC11` | `UART4_RX` | In | TMC2209 #1 RX，專案目前未使用回讀 |
| Motor1 | Encoder A/B | `PA15` / `PB9` | `TIM2_CH1/CH2` | In | Motor1 編碼器（Encoder mode） |
| Motor2 | STEP | `PA6` | `TIM3_CH1` | Out | 脈波輸出到 TMC2209 #2 STEP |
| Motor2 | DIR | `PC8` | GPIO | Out | Motor2 方向控制 |
| Motor2 | EN | `PC9` | GPIO | Out | Motor2 enable，active-low |
| Motor2 | TMC UART TX | `PC12` | `UART5_TX` | Out | TMC2209 #2 設定封包 |
| Motor2 | TMC UART RX | `PD2` | `UART5_RX` | In | TMC2209 #2 RX，專案目前未使用回讀 |
| Motor2 | Encoder A/B | `PA0` / `PA1` | `TIM5_CH1/CH2` | In | Motor2 編碼器（Encoder mode） |
| User I/F | 模式按鍵 | `PC13` | `B1 / EXTI13` | In | 切換 8 個 stage（4 段速度 x 正反轉） |
| ADC | ADC1 輸入 1 | `PC3` | `ADC1_IN13` | In | 類比通道 `adc1` |
| ADC | ADC2 輸入 1 | `PC4` | `ADC2_IN14` | In | 類比通道 `adc2` |
| ADC | ADC1 輸入 2 | `PC2` | `ADC1_IN12` | In | 類比通道 `adc3` |
| ADC | ADC2 輸入 2 | `PC1` | `ADC2_IN11` | In | 類比通道 `adc4` |
| Debug | UART TX | `PA2` | `USART2_TX` | Out | ST-LINK VCP 輸出 |
| Debug | UART RX | `PA3` | `USART2_RX` | In | 目前未使用 |

---

## 3. 模式（Step Stage）怎麼決定

### 3.1 8 個 Stage 定義

| stage | 名稱 | 方向 | STEP 頻率 |
|---|---|---|---|
| 0 | 慢順 | 正轉 | 200 Hz |
| 1 | 快順 | 正轉 | 1400 Hz |
| 2 | 超快順 | 正轉 | 5000 Hz |
| 3 | 極速順 | 正轉 | 7500 Hz |
| 4 | 慢逆 | 反轉 | 200 Hz |
| 5 | 快逆 | 反轉 | 1400 Hz |
| 6 | 超快逆 | 反轉 | 5000 Hz |
| 7 | 極速逆 | 反轉 | 7500 Hz |

切換順序固定：`0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 0`

### 3.2 觸發條件（按鍵）

- 只抓「按下瞬間」：上一拍是 `SET`、這拍是 `RESET`（falling edge）。
- 有去彈跳：`180ms` 內不重複觸發。
- Motor1 先 `NextSpeedStage()`，成功後 Motor2 跟著設定同一個 stage，確保兩顆同步。

### 3.3 程式上的方向切分規則

- `STEPPER_TMC2209_SPEED_STAGE_COUNT = 8`
- `STEPPER_TMC2209_DIRECTION_SPLIT_STAGE = 4`
- 所以：
  - `stage 0,1,2,3` 視為正轉
  - `stage 4,5,6,7` 視為反轉

### 3.4 7500 模式下避免「沒反應」的處理

- Stage 切換不再直接跳到目標頻率，而是用小步進頻率斜坡（ramp）上/下調整。
- 方向切換時會先減速到低速，再翻轉 DIR，最後再加速到目標速度。
- 目前 ramp 參數：
  - `TMC2209_RAMP_STEP_HZ = 800`
  - `TMC2209_RAMP_DELAY_MS = 1`
  - `TMC2209_DIR_SWITCH_SETTLE_MS = 2`

---

## 4. PWM 設計（STEP 脈波）完整解釋

> 這段最重要：目標是把「要的 step_hz」換成定時器的 `PSC / ARR / CCR`。

### 4.1 本專案時脈設定

- `SYSCLK = 84MHz`（PLL）
- `APB2 = 84MHz`（TIM1 在 APB2）
- `APB1 = 42MHz`（TIM3 在 APB1）
- STM32 定時器規則：
  - 若 APB 分頻器不是 1，對應 Timer clock 會是 `PCLK x 2`
  - 所以 TIM3 雖在 APB1=42MHz，實際 timer clock 仍是 `84MHz`

結論：TIM1 與 TIM3 的 `timer_clock_hz` 都是 84MHz。

### 4.2 Prescaler（預分頻器）是什麼

- 設定值：`PSC = 84-1 = 83`
- 公式：`counter_clock = timer_clock / (PSC + 1)`
- 帶入：`84MHz / 84 = 1MHz`

白話：定時器計數器每 1 微秒跳一次（1MHz）。

### 4.3 ARR（Auto-Reload）是什麼

- `ARR` 決定計數器從 0 數到哪裡就「回到 0」。
- 一次回圈時間 = `(ARR + 1) / counter_clock`
- 專案使用公式：
  - `arr_calc = counter_clock / step_hz`
  - `ARR = arr_calc - 1`
  - 最小保護：`arr_calc < 4` 時，先拉到 4（避免太窄脈波）

### 4.4 CCR（比較值）與占空比

- `CCR = (ARR + 1) / 2`
- 也就是約 50% duty
- 對 TMC2209 的 STEP 腳，重點是「上升緣計 step」，50% 方波最直觀且穩定。

### 4.5 200Hz / 1400Hz 實際算例

- 已知 `counter_clock = 1,000,000 Hz`

`200 Hz`

- `arr_calc = 1,000,000 / 200 = 5000`
- `ARR = 4999`
- `CCR = 2500`
- 實際頻率 = `1,000,000 / (4999+1) = 200 Hz`

`1400 Hz`

- `arr_calc = 1,000,000 / 1400 = 714`（整數除法）
- `ARR = 713`
- `CCR = 357`
- 實際頻率 = `1,000,000 / 714 = 1400.56 Hz`

白話：1400Hz 因為除不盡，會有非常小的量化誤差，這是正常現象。

### 4.6 目前 TMC2209 細分設定

這份專案現在不是把細分交給模組板硬體腳位，而是由 [`Core/Src/App/stepper_tmc2209.c`](c:\Users\a2105\STM32CubeIDE\workspace_1.14.1\F446RE_TRACKING\Core\Src\App\stepper_tmc2209.c) 的 UART 寄存器設定直接指定：

- `GCONF = 0x000000C0`
- `CHOPCONF = 0x14000053`

這代表：

- 開 `pdn_disable`
- 開 `mstep_reg_select`
- `MRES = 1/16 microstep`

因此若馬達是常見的 `200 full-step/rev`，目前實際就是：

```text
1 圈 = 200 x 16 = 3200 STEP pulse
```

也就是：

- `200 Hz` = `16 秒 / 圈`
- `3200 Hz` = `1 秒 / 圈`

如果你實測仍接近 `1600 pulse/rev`，那就要優先檢查：

- UART 寄存器設定有沒有真的寫進 TMC2209
- 模組板 `PDN_UART / MS1 / MS2` 的硬體接法有沒有影響細分來源

---

## 5. UART 監看封包格式（逐欄位說明）

### 5.1 一行封包格式

```text
<seq> m1:<mode> m2:<mode> adc1:<v> adc2:<v> adc3:<v> adc4:<v> enc1:<c> enc2:<c> ang1:<deg> ang2:<deg>\r\n
```

實例：

```text
0 m1:0 m2:0 adc1:1234 adc2:1201 adc3:1245 adc4:1190 enc1:0 enc2:0 ang1:0.0000 ang2:0.0000
1 m1:1 m2:1 adc1:1230 adc2:1198 adc3:1241 adc4:1186 enc1:12 enc2:-3 ang1:1.0800 ang2:359.7300
```

### 5.2 欄位意義

| 欄位 | 型態 | 範圍/格式 | 說明 |
|---|---|---|---|
| `seq` | `uint32` | `0,1,2...` | 成功送出後才 +1，用來看有沒有掉包/停住 |
| `m1` | `uint8` | `0~7` | Motor1 stage |
| `m2` | `uint8` | `0~7` | Motor2 stage |
| `adc1` | `uint16` | `0~4095` | `ADC1_IN13 / PC3` 濾波後值 |
| `adc2` | `uint16` | `0~4095` | `ADC2_IN14 / PC4` 濾波後值 |
| `adc3` | `uint16` | `0~4095` | `ADC1_IN12 / PC2` 濾波後值 |
| `adc4` | `uint16` | `0~4095` | `ADC2_IN11 / PC1` 濾波後值 |
| `enc1` | `int32` | 可正可負 | Encoder1 累積 count（不歸零） |
| `enc2` | `int32` | 可正可負 | Encoder2 累積 count（不歸零） |
| `ang1` | 文字小數 | `ddd.dddd` | Encoder1 角度（0~359.9999） |
| `ang2` | 文字小數 | `ddd.dddd` | Encoder2 角度（0~359.9999） |

### 5.3 輸出週期

- `APP_LOG_PERIOD_MS = 100`
- 所以預設每 `100ms` 一包（約 10Hz）

---

## 6. Encoder 與角度換算

- 編碼器規格：`1000 pulse/rev`
- 使用 TI12（四倍頻）後：`4000 count/rev`
- 換算：
  - 先把累積 count 正規化到一圈：`count % 4000`
  - 再換成角度（放大 10000 倍後輸出 4 位小數）

簡化公式：

```text
angle_deg = (normalized_count / 4000) * 360
```

---

## 7. ADC 優化（DMA + 濾波）

### 7.1 取樣架構：DMA circular

- ADC1、ADC2 各自啟動 `HAL_ADC_Start_DMA(..., len=2)`。
- DMA 會一直把最新結果覆寫到 `dma_adc1[2]` / `dma_adc2[2]`。
- 主迴圈的 `AppAdc_Task()` 會把兩組 DMA 值整理成 `adc1~adc4`，再做濾波。
- 為了降低 CPU 負載，在 App 層把 DMA 的 `HT/TC` 中斷關掉（不需要每次搬運都進 IRQ）。

白話：ADC 轉換和搬運交給硬體，CPU 不用一直 `PollForConversion()`。

### 7.2 用的是一階低通 IIR

程式常數：

- `APP_ADC_FILTER_WEIGHT_PREV = 700`
- `APP_ADC_FILTER_WEIGHT_NEW  = 300`
- `APP_ADC_FILTER_WEIGHT_SCALE = 1000`

公式：

```text
y[n] = (700*y[n-1] + 300*x[n] + 500) / 1000
```

- `x[n]`：這次原始 ADC
- `y[n-1]`：上次濾波結果
- `+500`：做四捨五入用

白話：

- 保留 70% 舊值 + 30% 新值
- 好處是畫面比較穩，不會每次 ADC 抖一下就跟著跳
- 壞處是反應會慢一點（但通常可接受）

### 7.3 啟動時的種子（seed）

第一次讀到資料時不做濾波，直接：

```text
filtered = raw
```

這樣可避免一開始從 0 慢慢爬升造成假延遲。

### 7.4 如果要換成電壓

目前封包送的是 ADC 數值（0~4095）。若要看電壓可在上位機換算：

```text
voltage = adc_raw * 3.3 / 4095
```

---

## 8. 系統流程圖（簡化版）

```text
開機初始化
  -> 初始化 TMC2209 x2（UART 寫寄存器、啟動 PWM）
  -> 啟動 ADC1/ADC2 DMA circular
  -> 啟動 TIM2/TIM5 Encoder mode
  -> UART 顯示 READY

主迴圈
  -> 讀按鍵 + debounce，必要時切下一個 stage
  -> 讀 DMA 最新 ADC + 濾波
  -> Encoder 更新累積 count
  -> 每 100ms 組一行文字封包送出
```

---

## 9. 參數總覽（目前版本）

| 類別 | 參數 | 值 |
|---|---|---|
| Button | debounce | `180 ms` |
| UART log | period | `100 ms` |
| UART (USART2/UART4/UART5) | baudrate | `115200` |
| PWM Timer | PSC | `83` (`84-1`) |
| PWM Timer | counter clock | `1 MHz` |
| Stage 0/4 | step | `200 Hz` |
| Stage 1/5 | step | `1400 Hz` |
| Stage 2/6 | step | `5000 Hz` |
| Stage 3/7 | step | `7500 Hz` |
| TMC2209 microstep | mode | `1/16` |
| ADC mode | acquisition | `HAL_ADC_Start_DMA(...,2)` x2 |
| DMA mode | stream | `CIRCULAR` |
| DMA IRQ | HT/TC | `Disabled in AppAdc` |
| ADC filter | old:new | `0.7 : 0.3` |
| Encoder | counts/rev | `4000` |

---

## 10. 快速排查建議

- `m1/m2` 會變但馬達不動：
  - 先看 EN 是否為低電位（active-low）
  - 再看 STEP 腳是否有方波（PA8/PA6）
- `enc` 會變但 `ang` 卡住：
  - 檢查是否只看整數角度，實際是小數在動
- ADC 很抖：
  - 硬體先補地線與 RC，再考慮把濾波改成 `0.8/0.2`
- `adc1/adc2/adc3/adc4` 長時間不變：
  - 檢查 ADC DMA 是否成功啟動（`HAL_ADC_Start_DMA`）
  - 檢查 DMA stream 與 channel 是否對應 `ADC1/ADC2`
