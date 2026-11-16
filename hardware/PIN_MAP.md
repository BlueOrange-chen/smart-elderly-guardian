# 引脚总表

本表是**唯一权威的引脚来源**，内容逐项核对自 `firmware/smart-terminal/main/*.c`。
改动引脚时：先改本表 → 再改代码里的 `#define` → 同一次 PR 提交。

> 上一次整理的 `GPIO_PIN_USAGE.md` 有三处与代码不符，已在本表修正：
> GPIO21 不是 LCD CS（代码里是摄像头 D5）；GPIO47 不是空闲（是摄像头 D4）；GPIO7 不是"可能占用"（是 MAX30102 INT，中断未启用）。

## 1. 主表

| GPIO | 功能 | 方向 | 代码位置 | 备注 |
|---|---|---|---|---|
| 1 | MAX98357A SD（使能/静音） | 输出 | `voice_output.c` | 高低电平有效见 `MAX98357A_SD_ACTIVE_HIGH` |
| 2 | MAX30102 SDA | 双向 | `max30102.c` | I²C 400 kHz，4.7 kΩ 上拉 |
| 4 | MAX30102 SCL | 输出 | `max30102.c` | 4.7 kΩ 上拉 |
| 5 | DHT11 DATA | 双向 | `dht11.c` | 4.7–10 kΩ 上拉，线长 ≤ 1 m |
| 6 | 摄像头 VSYNC | 输入 | `camera.c` | |
| 7 | MAX30102 INT | 输入 | `max30102.c` | **预留**：中断初始化已注释，当前未使用 |
| 8 | 摄像头 HREF | 输入 | `camera.c` | |
| 9 | 摄像头 PCLK | 输入 | `camera.c` | |
| 10 | I²S BCLK | 输出 | `voice_audio.c` | INMP441 与 MAX98357A 共用 |
| 11 | I²S WS/LRCK | 输出 | `voice_audio.c` | 共用 |
| 12 | I²S 麦克风数据（SD） | 输入 | `voice_audio.c` | INMP441 → ESP32 |
| 13 | I²S 功放数据（DIN） | 输出 | `voice_audio.c` | ESP32 → MAX98357A |
| 14 | 摄像头 XCLK（20 MHz） | 输出 | `camera.c` | **占 LEDC_TIMER_0 + LEDC_CHANNEL_0** |
| 15 | 摄像头 SIOD（SCCB SDA） | 双向 | `camera.c` | |
| 16 | 摄像头 SIOC（SCCB SCL） | 输出 | `camera.c` | |
| 17 | 摄像头 D7 | 输入 | `camera.c` | |
| 18 | 摄像头 D6 | 输入 | `camera.c` | |
| 21 | 摄像头 D5 | 输入 | `camera.c` | 曾被误标为 LCD CS |
| 38 | RGB LED | 输出 | `rgb_led.c` | RMT_CHANNEL_0 |
| 39 | 摄像头 D3 | 输入 | `camera.c` | |
| 40 | 摄像头 D2 | 输入 | `camera.c` | |
| 41 | 摄像头 D1 | 输入 | `camera.c` | |
| 42 | 摄像头 D0 | 输入 | `camera.c` | |
| 47 | 摄像头 D4 | 输入 | `camera.c` | 曾被误标为"未使用" |
| 48 | SG90 PWM | 输出 | `servo.c` | **LEDC_TIMER_1 + LEDC_CHANNEL_1**，50 Hz，500–2500 µs，13 bit |

## 2. 不可占用引脚

| GPIO | 原因 |
|---|---|
| 19, 20 | 原生 USB D-/D+（`idf.py monitor`、USB 下载时占用） |
| 26–32 | SPI Flash |
| 33–37 | 八线 PSRAM（N8R8/N16R8 模组） |
| 43, 44 | UART0 TX/RX（日志与下载） |
| 45, 46 | Strapping 脚，影响启动模式 |

## 3. 两条硬约束（踩过坑）

**① 舵机不得使用 `LEDC_TIMER_0`**

摄像头 XCLK 固定用 `LEDC_TIMER_0 + LEDC_CHANNEL_0`（20 MHz），舵机若复用同组资源，
50 Hz 会覆盖摄像头的时钟源，现象是"画面花/黑，舵机也不动"。当前代码已分到 `LEDC_TIMER_1 + LEDC_CHANNEL_1`。

**② 舵机不得挂 GPIO19**

S3 的 GPIO19/20 是原生 USB 信号线。后端 `SERVO_CONTROL.md` 早期示例给的是 GPIO19，
接上去在插 USB 调试时会互相干扰。现用 GPIO48。

## 4. 电源

| 负载 | 供电 | 峰值电流 | 注意 |
|---|---|---|---|
| ESP32-S3 | 5 V → 板载 3V3 LDO | 500 mA | |
| OV2640 | 3V3 | 100–200 mA | LDO 余量不足会导致初始化失败 |
| SG90 | **独立 5 V** | 700 mA（堵转） | 与主控共地；从板载 5 V 取电会引发复位 |
| INMP441 | 3V3 | 1.4 mA | |
| MAX98357A + 喇叭 | 5 V | 500 mA（2 W） | 与舵机错峰，避免同时拉满 |

PCB 级建议：每路电源入口 100 µF 电解 + 100 nF 瓷片；舵机信号线串 100 Ω。

## 5. 接线核查顺序

上电前按这个顺序查一遍，能省掉大部分"玄学问题"：

1. 3V3 与 GND 是否短路（万用表二极管档）；
2. MAX30102 的 SDA/SCL 是否接反（SDA=2，SCL=4）；
3. DHT11 数据线是否有上拉（模块自带，裸件要外接）；
4. 摄像头 FPC 是否插到底、锁扣是否压紧（接触不良表现为 D0–D7 随机错位）；
5. 舵机是否独立供电且共地；
6. I²S 三条线是否交叉（BCLK=10、WS=11、麦克风 SD=12、功放 DIN=13）。

## 6. 引脚改动流程

1. 改本表并写明理由；
2. 改 `firmware/smart-terminal/main/` 下对应的 `#define`；
3. 运行 `python hardware/tools/check_pin_conflicts.py` 检查重复与保留脚占用；
4. 同一 PR 提交，PR 描述里注明"引脚变更"；
5. 影响协议字段的（如 `cameraId`）同步更新 `protocols/`。
