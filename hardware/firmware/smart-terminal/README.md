# 智能终端固件（ESP32-S3 · 单板全功能）

一块 ESP32-S3 同时承担：

| 能力 | 说明 | 对应后端 |
|---|---|---|
| 体征采集 | MAX30102 心率/血氧 + DHT11 温湿度，每秒 JSON 上报 | `backend/health-platform` |
| 摄像头推流 | OV2640 JPEG 帧 HTTP POST，服务端转 HLS 给小程序 | `backend/camera-stream-server` |
| 云台随动 | SG90 舵机，轮询服务端目标角度并回报实际状态 | 同上（servo 接口） |
| 双向语音 | I²S 录音 → 语音服务 → 播放回复；默认整段 HTTP，可切 Opus WebSocket | `backend/voice-service` |
| 状态指示 | 板载 RGB LED 指示启动/就绪/思考/播放 | 无 |
| 云侧物模型 | 阿里云 IoT MQTT 属性上报（可关闭） | 阿里云 IoT |

## 1. 硬件

| 部件 | 型号 / 规格 | 接口 |
|---|---|---|
| 主控 | ESP32-S3，**8 MB Flash + 八线 PSRAM**（N8R8 / N16R8 类模组） | — |
| 摄像头 | OV2640（DVP 并口，带 24 pin FPC） | 见引脚表 |
| 心率血氧 | MAX30102 | I²C（400 kHz） |
| 温湿度 | DHT11 | 单总线 |
| 云台舵机 | SG90（5 V，500–2500 µs） | PWM 50 Hz |
| 麦克风 | INMP441 | I²S（24 bit，左声道） |
| 功放 | MAX98357A + 8 Ω 2 W 喇叭 | I²S + SD 使能脚 |
| 状态灯 | WS2812 类 RGB LED | RMT |

**PSRAM 是硬性要求**：VGA 双帧缓冲 + 语音环形缓冲都在 PSRAM 上，没有 PSRAM 时摄像头初始化会失败。

## 2. 引脚

完整总表见 [`../../PIN_MAP.md`](../../PIN_MAP.md)，这里只列本固件实际使用的：

| GPIO | 用途 | GPIO | 用途 |
|---|---|---|---|
| 2 | MAX30102 SDA | 17 | 摄像头 D7 |
| 4 | MAX30102 SCL | 18 | 摄像头 D6 |
| 5 | DHT11 DATA | 21 | 摄像头 D5 |
| 7 | MAX30102 INT（**预留，中断未启用**） | 47 | 摄像头 D4 |
| 6 | 摄像头 VSYNC | 39 | 摄像头 D3 |
| 8 | 摄像头 HREF | 40 | 摄像头 D2 |
| 9 | 摄像头 PCLK | 41 | 摄像头 D1 |
| 14 | 摄像头 XCLK（20 MHz，LEDC_TIMER_0/CH_0） | 42 | 摄像头 D0 |
| 15 | 摄像头 SIOD | 48 | SG90 PWM（LEDC_TIMER_1/CH_1） |
| 16 | 摄像头 SIOC | 38 | RGB LED（RMT CH0） |
| 10 | I²S BCLK | 1 | MAX98357A SD（使能/静音） |
| 11 | I²S WS/LRCK | | |
| 12 | I²S 麦克风数据输入 | | |
| 13 | I²S 功放数据输出 | | |

不可用：19/20（原生 USB）、43/44（UART0）、45/46（strapping）、26–32（Flash）、33–37（八线 PSRAM）。

> 舵机必须使用 `LEDC_TIMER_1 + LEDC_CHANNEL_1`。摄像头 XCLK 固定占 `LEDC_TIMER_0 + LEDC_CHANNEL_0`，
> 复用会互相覆盖时钟，表现为舵机乱抖或画面异常。

## 3. 软件环境

| 项目 | 版本 |
|---|---|
| ESP-IDF | **v5.2.2**（实测） |
| 目标 | `esp32s3` |
| Flash / 分区 | 8 MB / `single app`（app 分区 1.5 MB，当前镜像约 1.12 MB） |
| 托管组件 | `esp_audio_codec` 2.6.2、`esp_websocket_client` 1.8.0、`esp_jpeg` 1.3.1、`esp32-camera` 2.0.x |

编译、烧录、组件策略见 [`../README.md`](../README.md)。

## 4. 模块职责

| 文件 | 职责 |
|---|---|
| `main.c` | 启动流程：RGB → Wi-Fi → MQTT → 语音 → 摄像头 → 传感器/舵机 → 1 s 主循环上报 |
| `device_config.h` | 配置总入口，include `config.local.h`（缺失时回退 `config.example.h`），并拼装全部 URL |
| `camera.c` | OV2640 初始化 + JPEG 校验 + 每 `DEVICE_CAMERA_INTERVAL_MS` 上传一帧 |
| `servo.c` | SG90 LEDC 初始化、角度步进、轮询目标角度、回报状态 |
| `max30102.c` | I²C 初始化、FIFO 读取、心率/血氧算法（输出 `latest_heart_rate` / `latest_spo2`） |
| `dht11.c` | 单总线时序读数，输出温度/湿度整数 |
| `voice_audio.c` | I²S 总线与 RX/TX 通道初始化（16 kHz / 16 bit / 单声道） |
| `voice_input.c` | VAD 触发、前置缓冲、整段录音、WAV 上传、下载回复并播放 |
| `voice_output.c` | I²S 播放、流式 PCM 播放、WAV 解析、MAX98357A SD 静音控制 |
| `voice_stream.c` | Opus WebSocket 双向流（hello/listen/tts/playback.clear），默认关闭 |
| `mqtt_aliot.c` / `aliot_dm.c` | 阿里云 IoT MQTT 连接（hmacmd5 签名）与物模型属性上报 |
| `rgb_led.c` | RMT 驱动 RGB LED 的启动/就绪/思考/播放状态 |

## 5. 配置

```bash
copy main\config.example.h main\config.local.h
```

编辑 `config.local.h`（**该文件已被 `.gitignore` 忽略，不会入库**）：

| 字段 | 含义 | 踩坑提醒 |
|---|---|---|
| `DEVICE_WIFI_SSID` / `_PASSWORD` | Wi-Fi | 2.4 GHz；中文 SSID 需 UTF-8 |
| `DEVICE_SENSOR_ID` | 健康平台 `deviceId` | 与后端 `sensor_data.device_id` 对应 |
| `DEVICE_CAMERA_ID` | 摄像头服务 `cameraId` | 出现在 URL 路径，改名要和小程序一致 |
| `DEVICE_VOICE_ID` / `_SESSION_ID` | 语音 `deviceId` / `sessionId` | 会话号换新 = 清空上下文 |
| `DEVICE_HEALTH/CAMERA/VOICE_BASE_URL` | 三个服务基址 | 只写 `协议://主机:端口` |
| `DEVICE_VOICE_WS_BASE_URL` | WS 基址 | 形如 `ws://host:8011/api/voice/stream` |
| `DEVICE_STREAM_INGEST_TOKEN` | 摄像头 + 云台令牌 | 不要填观看密钥 |
| `DEVICE_VOICE_TOKEN` | 语音 `X-Device-Token` | 与 `DEVICE_API_TOKEN` 一致 |
| `DEVICE_HEALTH_TOKEN` | 健康平台鉴权 | 留空 = 后端未开鉴权 |
| `DEVICE_MQTT_*` / `DEVICE_MQTT_ENABLED` | 阿里云物模型 | 不接阿里云就置 0 |
| `DEVICE_VOICE_STREAM` | 0 = 仅 HTTP 语音，1 = 启用 Opus WS | 先跑通 HTTP 再开 |
| `DEVICE_CAMERA_INTERVAL_MS` / `_JPEG_QUALITY` / `_FB_COUNT` | 摄像头节奏 | 弱网先 QVGA/500 ms |

分辨率在 `camera.c` 里用 `FRAMESIZE_xxx` 改（枚举不能放配置文件，否则需要包含 `esp_camera.h`）。

## 6. 运行时任务

| 任务 | 优先级 | 栈 | 节奏 |
|---|---|---|---|
| `camera_upload` | 5 | 8192 | 每 `DEVICE_CAMERA_INTERVAL_MS` 一帧 |
| `servo_cmd` | 5 | 8192 | 5000 ms 轮询目标角度 |
| `voice_detect` | 6 | 4096 | 20 ms 一帧做 VAD |
| `voice_upload` | 4 | 8192 | 事件驱动（录音结束触发） |
| `app_main` 主循环 | 1 | 3584 | 1000 ms 上报一次体征 |

数据流：

```text
MAX30102/DHT11 ─┐
                ├─(1 s)─> aliot_post_property_*  → 阿里云物模型
                └────────> POST /sensor/save     → 健康平台 → MySQL → 小程序
OV2640 ─(500 ms)─> POST /api/cameras/{id}/frame → 摄像头服务 → FFmpeg → HLS → 小程序
SG90  <─(5 s 轮询)─ GET .../servo/command ──> 小程序设置的目标角度
      ─(动作后)──> POST .../servo/status
INMP441 ─VAD─> WAV → POST /api/voice/chat → 音频 → MAX98357A
```

## 7. 后端对接

| 用途 | 请求 | 鉴权 | 成功响应 |
|---|---|---|---|
| 体征上报 | `POST {health}/sensor/save` | `X-Device-Token`（可选） | `200` + 记录 JSON |
| 摄像头帧 | `POST {camera}/api/cameras/{id}/frame?token=` | query `token` | `204` |
| 云台指令 | `GET {camera}/api/cameras/{id}/servo/command?token=` | query `token` | `200 {"target_angle":45}` |
| 云台回报 | `POST {camera}/api/cameras/{id}/servo/status?token=` | query `token` | `204` |
| 语音对话 | `POST {voice}/api/voice/chat?deviceId=&sessionId=` | `X-Device-Token` | `200` + 音频 |
| 测试音 | `GET {voice}/api/voice/test-audio.wav` | `X-Device-Token` | `200` + WAV |
| 语音流 | `WS {voice-ws}?deviceId=` | `X-Device-Token` | `101` |

字段、错误码与重试策略详见 [`../../protocols/`](../../protocols/)。

## 8. 已知限制（不是 bug，是当前状态）

1. **云台软件限位 ±40°**，而后端允许 −90…+90°；超出时设备夹到 ±40° 并回报 `limit_reached`。要真正到 ±90°，需改 `servo.c` 的 `s_min_angle/s_max_angle`，并确认机械结构不撞限位。
2. **云台轮询 5000 ms**，操作延迟 0–5 s。后端 `SERVO_CONTROL.md` 建议 500–1000 ms；改 `servo.c` 里 `vTaskDelay(pdMS_TO_TICKS(5000))` 即可，注意与摄像头上传抢网络。
3. **Opus WebSocket 默认关闭**（`DEVICE_VOICE_STREAM=0`），当前走整段 HTTP 语音；WS 路径无自动重连，控制帧用字节搜索而非 JSON 解析。
4. **明文 HTTP**：当前配置用 `http://`/`ws://`。生产必须 HTTPS/WSS 并在设备端配置 CA 证书，否则任意人可窃听语音与图像。
5. **无 OTA、无配网**：Wi-Fi 写死在本地配置，换环境要重新烧录或改配置再编译。
6. **两套云端并行**：阿里云物模型与自建后端各自独立，数据不互通，也不互为备份。
7. **弱网无背压**：摄像头上传是阻塞式，弱网时帧率自动下降（`CAMERA_GRAB_LATEST` 保证拿到最新帧），不会堆积内存。
8. **MAX30102 中断未启用**：`MAX30102_GPIO_INT`（GPIO7）仅预留，中断初始化代码被注释，当前靠轮询 FIFO。

## 9. 验证清单

按顺序做，每一步都有明确判据：

- [ ] **编译**：`idf.py build` 通过；镜像 < 1.5 MB（`idf.py size`）
- [ ] **联网**：30 s 内出现 `esp32 get ip address`
- [ ] **体征**：`POST /sensor/save` 返回 `200`，body 含 `"id"`；健康平台 `GET /api/get-latest-data?deviceId=...` 能查到
- [ ] **摄像头**：`GET /api/cameras/{id}/status?token=<观看密钥>` 返回 `online:true` 且 `frameSequence` 递增
- [ ] **画面**：浏览器打开 `.../snapshot.jpg?token=` 能看到图；小程序 HLS 能播
- [ ] **云台**：小程序设 30° → 5 s 内转动，`.../servo/status` 回报 `moved`；设 80° → 回报 `limit_reached` 且停在 40°
- [ ] **语音**：对 INMP441 说话 → 串口出现录音结束 → 听到回复；`test-audio.wav` 可播放
- [ ] **长跑**：连续 30 min 无复位（查 `esp_reset_reason()` 或串口启动日志），`heap_caps_get_free_size` 不持续下降
- [ ] **断网恢复**：拔掉路由器 30 s 再插回，Wi-Fi 自动重连、上传自动恢复

## 10. 故障排查

| 现象 | 原因 | 处理 |
|---|---|---|
| `Camera init failed` | PSRAM 未启用或模式错（八线/四线） | 核对 `sdkconfig.defaults` 的 `CONFIG_SPIRAM_MODE_OCT`；`idf.py fullclean` 后重编 |
| 摄像头花屏 / 舵机乱抖 | LEDC 资源冲突 | 舵机必须 `LEDC_TIMER_1 + LEDC_CHANNEL_1` |
| 板子反复重启 | 舵机/功放从板载 5 V 取电，电流不足 | 舵机独立 5 V 供电，与主控共地 |
| `sensor upload status=401` | 健康平台开启了 `DEVICE_API_TOKEN` 但设备没带 | 填 `DEVICE_HEALTH_TOKEN` |
| 摄像头 `401` | `DEVICE_STREAM_INGEST_TOKEN` 与后端 `STREAM_INGEST_TOKEN` 不一致 | 改配置重新烧录 |
| 连不上 Wi-Fi | 5 GHz 热点 / 密码错 / 中文 SSID 编码问题 | 用 2.4 GHz；改用英文 SSID 测试 |
| 语音没声音 | MAX98357A SD 极性反了 | 调 `voice_output.c` 的 `MAX98357A_SD_ACTIVE_HIGH` |
| 串口日志乱码 | 波特率不对 | `idf.py monitor` 默认 115200 |
| 云台指令不生效 | 小程序没点"应用"或 token 用成了观看密钥 | 查 `.../servo/command` 手工 GET 的返回 |
