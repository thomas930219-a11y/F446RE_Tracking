# F446RE_TRACKING HackMD（Pin 清楚版）

> 更新日期: 2026-03-12  
> MCU: STM32F446RE  
> 版本範圍: 目前 `Core/Src/App/*` 程式（HAL）

## 1. 目前有實作的功能

- 一組步進馬達 + TMC2209（UART4 設定、TIM1 輸出 STEP）
- USER 按鍵（`PC13`）切換 10 段
- `USART2` 持續送序號：`SEQ=0`, `SEQ=1`, ...

## 2. 本版實際使用 Pin（請照這張接）

| 功能 | MCU Pin | 外設 | I/O | 接線到哪裡 | 備註 |
|---|---|---|---|---|---|
| STEP | `PA8` | `TIM1_CH1` | Out | TMC2209 `STEP` | 脈波頻率 = 速度段 |
| DIR | `PC6` | GPIO | Out | TMC2209 `DIR` | `0~4` 正轉，`5~9` 反轉 |
| EN | `PB8` | GPIO | Out | TMC2209 `EN` | active-low（`0` 啟用） |
| TMC UART TX | `PC10` | `UART4_TX` | Out | TMC2209 `PDN_UART`/RX | 設定封包輸出 |
| TMC UART RX | `PC11` | `UART4_RX` | In | TMC2209 TX | 回讀線（目前主程式以寫入為主） |
| 速度按鍵 | `PC13` | `B1 / EXTI13` | In | Nucleo USER Button | 每按一次切下一段 |
| 序號輸出 TX | `PA2` | `USART2_TX` | Out | ST-LINK VCP | 終端機看 `SEQ=...` |
| 序號輸出 RX | `PA3` | `USART2_RX` | In | ST-LINK VCP | 目前未用 |

## 3. 10 段速度與方向對照

| 段位 | 方向 | STEP 頻率 |
|---|---|---|
| 0 | 正轉 | 200 Hz |
| 1 | 正轉 | 400 Hz |
| 2 | 正轉 | 700 Hz |
| 3 | 正轉 | 1000 Hz |
| 4 | 正轉 | 1400 Hz |
| 5 | 反轉 | 200 Hz |
| 6 | 反轉 | 400 Hz |
| 7 | 反轉 | 700 Hz |
| 8 | 反轉 | 1000 Hz |
| 9 | 反轉 | 1400 Hz |

切換順序: `0 -> 1 -> 2 -> ... -> 9 -> 0`（按 `PC13`）

## 4. 目前程式沒用到但 .ioc 已配置的 Pin（避免誤接）

| MCU Pin | 外設 | 狀態 |
|---|---|---|
| `PA6` | `TIM3_CH1` | 已配置，這版 App 未使用 |
| `PC12` / `PD2` | `UART5_TX/RX` | 已配置，這版 App 未使用 |
| `PC8` / `PC9` | GPIO Output | 已配置，這版 App 未使用 |
| `PC3` / `PC4` | `ADC1_IN13` / `ADC2_IN14` | 已配置，這版 App 未使用 |
| `PA15` / `PB9` | `TIM2 Encoder` | 已配置，這版 App 未使用 |
| `PA0` / `PA1` | `TIM5 Encoder` | 已配置，這版 App 未使用 |
| `PA9` / `PA10` | `USART1_TX/RX` | 已配置，這版 App 未使用 |

## 5. 接線注意事項

- TMC2209 的 `EN` 是 active-low，拉低才會啟用驅動。
- 若你的 TMC2209 模組是單線 `PDN_UART`，`UART4_TX/UART4_RX` 需要依模組建議做單線接法（常見為電阻合路）。
- 序號監看請選 `USART2`（Nucleo ST-LINK 虛擬 COM）。
