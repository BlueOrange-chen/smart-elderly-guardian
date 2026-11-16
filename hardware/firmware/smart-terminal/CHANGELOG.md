# 变更记录

## 2026-11-16 · 入库整理

- 按仓库 `CONTRIBUTING.md` 的 `smart-terminal/` 结构整理目录；
- 把 Wi-Fi、服务器地址、令牌、阿里云凭据从源码中抽出到 `config.local.h`（不入库），
  仓库只保留 `config.example.h`；
- 新增 `device_config.h` 作为配置总入口与 URL 拼装点；
- 新增 `sdkconfig.defaults`，记录 PSRAM / Flash / 缓存 / 摄像头 / MQTT 的实测配置；
- 第三方组件 `esp32-camera` 改为 Component Manager 托管（不再整目录拷贝）；
- 修正 `GPIO_PIN_USAGE.md` 中三处与代码不符的引脚记录，改为 `hardware/PIN_MAP.md`；
- 补齐设备协议文档（`hardware/protocols/`）。

## 2026-08-31 · 语音链路

### 稳定回退路径

- 保留原有 HTTP + WAV 语音请求与完整 WAV 播放流程；
- `VOICE_STREAM_ENABLED` 保持为 `0`，设备实际使用 HTTP 路径；
- 流式 WebSocket 不可用、`listen.started` 超时或 PCM 发送失败时，当前会话自动回退 HTTP。

### WebSocket 与 Opus 上行

- 连接后发送 `hello`，声明 Opus、16 kHz、单声道、20 ms 帧；
- 上行使用 Raw Opus binary 帧：320 采样点，输入 640 B PCM；
- 编码配置：VOIP、20 kbps、VBR 开、FEC/DTX 关、复杂度 0；
- VAD 的 START / DATA / STOP 映射为 `listen start`、连续 Opus binary、`listen stop`；
- 发送音频前等待 `listen.started`，最长 1 s。

### Opus 下行与首包播放

- WS binary 帧在事件回调里只入 32 帧接收队列，不在网络回调里解码或写 I2S；
- 独立 `voice_opus_decode` 任务解码后立即写 I2S，首个有效 PCM 帧即开始播放；
- 单个下行包上限 512 B，PCM 输出缓冲固定 640 B。

### 播放清理与半双工

- 收到 `playback.clear` / `response.done` / `tts.stop` 时清空接收队列并停止、静音 I2S；
- WS 断开时同步清除播放状态，避免 VAD 被永久暂停；
- 流式播放期间 VAD 采集进入忙状态，避免扬声器回采；
- 扬声器在 WS 启动前初始化，保证首包到达时输出层可用。

## 构建记录

| 项目 | 值 |
|---|---|
| ESP-IDF | v5.2.2 |
| 目标 | esp32s3 |
| 应用镜像 | 约 `0x111840`（1.12 MB） |
| 分区 | `single app`，1.5 MB，剩余约 36% |
| 组件 | esp_audio_codec 2.6.2、esp_websocket_client 1.8.0、esp_jpeg 1.3.1、esp32-camera 2.0.x |

## 已知限制

- WS 自动重连关闭，断网后需上层重启连接；
- 控制帧用字节搜索识别事件，未接完整 JSON 解析；
- 下行 Opus 按单包单帧处理，服务端不能把一个 Opus 包拆到多个 WS 帧；
- 代码构建通过，但 Opus WS 路径**尚未完成真实服务端与硬件的端到端验证**；
- 云台软件限位 ±40°，后端允许 ±90°；
- 云台轮询 5 s，操作延迟 0–5 s。
