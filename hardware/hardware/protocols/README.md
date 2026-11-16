# 设备通信协议

本目录记录**设备端与后端之间的契约**。以 `main` 分支的后端实现为准，固件按此实现。

任何一条协议字段的改动，必须和对应后端的 PR 一起提交，否则设备与服务器会各说各话。

## 文档索引

| 文件 | 覆盖内容 |
|---|---|
| [`sensor-upload.md`](sensor-upload.md) | 体征数据上报（健康平台 `/sensor/save`、`/api/upload-data`） |
| [`camera-frame-servo.md`](camera-frame-servo.md) | JPEG 帧上传与云台指令/状态回报 |
| [`voice.md`](voice.md) | 整段 WAV 语音、流式 PCM、Opus WebSocket 双向语音 |
| [`mqtt-aliot.md`](mqtt-aliot.md) | 阿里云 IoT 物模型 MQTT（可选链路） |
| [`retry-and-errors.md`](retry-and-errors.md) | 统一的状态码、错误码、超时与重试退避 |

## 通用约定

### 服务与端口

| 服务 | 默认端口 | 用途 |
|---|---|---|
| 健康平台 | 8000 | 体征入库、AI 问答、周报 |
| 摄像头服务 | 8090 | JPEG 上传、HLS、云台 |
| 语音服务 HTTP | 8010 | 整段语音 / 流式 PCM |
| 语音服务 WebSocket | 8011 | Opus 双向流（与 realtime_gateway 不能同端口共存） |
| 阿里云 IoT | 8883 | MQTT over TLS |

### 设备身份

后端用**字符串**匹配设备，没有全局设备表：

- `deviceId`：健康平台写库字段 `sensor_data.device_id`，语音服务用于会话隔离
- `cameraId`：出现在 URL 路径 `/api/cameras/{cameraId}/...`，也要和小程序配置一致

约定：**同一个物理设备在两个服务里使用同一个标识**（例如 `room-1`），便于排障时一眼对上。
当前固件里 `DEVICE_SENSOR_ID`、`DEVICE_CAMERA_ID`、`DEVICE_VOICE_ID` 分开配置，允许不同值。

### 鉴权

三种凭证，用途不同，**不要复用同一个值**：

| 凭证 | 请求位置 | 后端环境变量 |
|---|---|---|
| 上传/设备令牌 | 摄像头与云台用 query `?token=`；健康与语音用头 `X-Device-Token` | `STREAM_INGEST_TOKEN` / `DEVICE_API_TOKEN` |
| 观看令牌 | 小程序拉 HLS、快照 | `STREAM_VIEWER_TOKEN` |
| 控制令牌 | 小程序下发云台角度 | `STREAM_CONTROL_TOKEN` |

固件只需要前两种中的"上传/设备令牌"。仓库里一律写 `CHANGE_ME`。

### 数据格式

- 请求体统一 `application/json`（二进制帧与音频除外）
- 数值字段用数字，不用字符串（后端 `float()` 转换失败会返回 400）
- 时间戳由**服务器生成**，设备不传，避免时钟不准
- 所有示例中的 `api.example.com` / `CHANGE_ME` 必须替换为真实值

### 传输安全

当前联调使用 `http://` 与 `ws://`。**生产必须 HTTPS/WSS**，且设备端要能校验服务器证书。
明文传播的语音与图像属于隐私数据，不能上公网。
