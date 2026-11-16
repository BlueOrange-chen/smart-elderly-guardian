# 语音协议

设备 ↔ 语音服务（`backend/voice-service`）。

三个层次，按稳定性从高到低：

| 层次 | 接口 | 当前状态 | 延迟 |
|---|---|---|---|
| A. 整段 WAV | `POST /api/voice/chat` | **固件默认使用** | 3–8 s（等整段 TTS） |
| B. 流式 PCM | `POST /api/voice/chat-stream` | 可用，固件未接 | 1–3 s（首包即可播） |
| C. Opus WebSocket | `WS /api/voice/stream` | 后端已实现；固件默认关闭（`DEVICE_VOICE_STREAM=0`） | < 1 s |

固件实现：`voice_input.c`（A）、`voice_stream.c`（C，Opus 编解码走 `esp_audio_codec`）、
`voice_output.c`（播放）。

## 1. 音频参数（三层通用）

| 参数 | 值 |
|---|---|
| 采样率 | 16000 Hz |
| 位深 | 16 bit（signed little-endian） |
| 声道 | 1（mono） |
| Opus 帧长 | 20 ms = 320 samples = 640 B PCM |
| Opus 码率 | 20 kbps，VOIP 模式，VBR 开，FEC/DTX 关 |
| 单段时长 | ≤ 30 s（后端代理超时 180 s） |

## 2. A. 整段语音（当前固件路径）

```http
POST {VOICE_BASE_URL}/api/voice/chat?deviceId=room-1&sessionId=room-1-001
X-Device-Token: {DEVICE_VOICE_TOKEN}
Content-Type: audio/wav

<完整 WAV 字节>
```

成功 `200`，直接返回可播放音频：

```http
HTTP/1.1 200 OK
Content-Type: audio/mpeg
X-Session-Id: room-1-001
X-Transcript: %E4%BD%A0%E5%A5%BD          ← URL 编码，调试用
X-AI-Reply: %E6%82%A8%E5%A5%BD...          ← URL 编码，调试用
```

设备只读响应体播放，`X-Transcript` / `X-AI-Reply` 可忽略（解码后打日志便于排障）。

失败返回 JSON + 非 2xx：

| 状态码 | 含义 |
|---|---|
| `400` | 空音频 / 格式不支持 |
| `401` | `X-Device-Token` 错误 |
| `503` | ASR / TTS 上游不可用 |

**必须先把状态码判断完再决定播放**，否则会把错误 JSON 当音频播出一段噪声。

`Content-Type` 也接受 `audio/mpeg`、`audio/ogg`、`audio/opus`、`audio/webm`、`audio/mp4`，
以及 `multipart/form-data`（字段 `audio`）。固件当前只发 WAV。

## 3. B. 流式 PCM

```http
POST {VOICE_BASE_URL}/api/voice/chat-stream?deviceId=room-1&sessionId=room-1-001
X-Device-Token: {DEVICE_VOICE_TOKEN}
Content-Type: audio/wav
```

成功 `200`，`Transfer-Encoding: chunked`，正文是连续 PCM：

```http
Content-Type: application/octet-stream
X-Audio-Format: pcm_s16le
X-Audio-Sample-Rate: 16000
X-Audio-Bits-Per-Sample: 16
X-Audio-Channels: 1
X-ASR-Ms: 860
X-First-Audio-Ready-Ms: 1200
```

设备侧要求：

1. 先确认 `200` 与上述音频参数，再启动 I2S；
2. HTTP 客户端负责剥掉 chunked framing，只把正文 PCM 交给播放器（ESP-IDF `esp_http_client` 自动处理）；
3. 环形缓冲 16–32 KiB，收到约 100–300 ms 音频后开始播放；
4. 每次尽量读 8–16 KiB，**数据可用时不要固定延时**；
5. 以响应结束作为本轮终点，**不要依赖 `Content-Length`**。

相比 A，这条路不需要等 MP3 整段下载完，也不必在设备上做 MP3 解码。

## 4. C. Opus WebSocket

### 连接

```text
ws(s)://{host}:8011/api/voice/stream?deviceId={deviceId}
X-Device-Token: {DEVICE_VOICE_TOKEN}
```

握手必须 `101 Switching Protocols`。

### 时序

```text
设备                                   服务端
 │ ── 文本 {"type":"hello",...} ──────> │
 │ <─ 文本 {"type":"hello","session_id"}│
 │ ── 文本 {"type":"listen","state":"start"} ─>
 │ <─ 文本 {"type":"listen","state":"started"} │
 │ ── 二进制 Opus 帧 × N ─────────────> │     (每 20 ms 一个完整 Opus packet)
 │ ── 文本 {"type":"listen","state":"stop"} ──>
 │ <─ 文本 {"type":"listen","state":"stopped"} │
 │ <─ 文本 {"type":"tts","state":"start"}      │
 │ <─ 二进制 Opus 帧 × N ──────────────│
 │ <─ 文本 {"type":"tts","state":"stop"}       │
 │ <─ 文本 {"type":"response.done"}            │
```

`hello` 请求体：

```json
{"type":"hello","version":1,"transport":"websocket",
 "audio_params":{"format":"opus","sample_rate":16000,"channels":1,"frame_duration_ms":20}}
```

`hello` 响应体：

```json
{"type":"hello","version":1,"transport":"websocket","session_id":"...","device_id":"room-1",
 "input_audio":{...},"output_audio":{"format":"opus","sample_rate":16000,"channels":1,
 "frame_duration_ms":20,"bitrate":20000}}
```

### 二进制帧规则

- 每个 binary frame **必须是一个完整、独立的 Opus packet**；
- 不加长度头、序号、时间戳、自定义容器；
- 服务端下发也是一包一帧，设备不能假设一个包能跨 WS 帧分片；
- 收到 `error` 后不得把 JSON 文本当音频播放。

### 其它控制消息

| 消息 | 方向 | 设备动作 |
|---|---|---|
| `{"type":"ping"}` / `{"type":"pong","timestamp":...}` | 设备 → 服务端 → 设备 | 保活，间隔 20–30 s |
| `{"type":"playback.clear"}` | 服务端 → 设备 | **立即停止播放，清空 Opus 队列与 I2S 环形缓冲** |
| `{"type":"stt","text":"...","final":true}` | 服务端 → 设备 | 调试可打日志 |
| `{"type":"tts.text.delta","text":"..."}` | 服务端 → 设备 | 调试可打日志 |
| `{"type":"error","code":"...","message":"..."}` | 服务端 → 设备 | 见 [`retry-and-errors.md`](retry-and-errors.md) |

### 设备队列要求

- Opus 接收队列 20–40 帧（400–800 ms）；
- 收到首个下行包即启动播放，**不要等 `tts.stop`**；
- 播放、录音、WS 收发三个任务互相独立；
- 语音会话期间降低摄像头上传优先级（当前固件靠任务优先级 + 半双工暂停实现）；
- 断线重连按 **1、2、4、8、15 秒**退避，重连后必须重新发 `hello`；
- 当前不要求回声消除，播放期间暂停麦克风上行（半双工）。

## 5. 会话管理

```http
DELETE {VOICE_BASE_URL}/api/voice/sessions/{sessionId}
```

清除该会话的文字上下文。开新的独立对话时调用；不调用则 AI 会记住之前几轮。
设备侧也可以换 `sessionId`（例如 `room-1-002`）达到同样效果。

## 6. 半双工规则

录像期间**不要**同时播放，否则麦克风会录到自己的扬声器输出，形成回环。

当前实现：VAD 在流式播放期间进入 busy 状态，暂停采集。整段路径同理——播放时不录音。

## 7. 测试音

```http
GET {VOICE_BASE_URL}/api/voice/test-audio.wav
X-Device-Token: {DEVICE_VOICE_TOKEN}
```

固定的扬声器测试 WAV，不含用户数据。用于先排除功放/接线问题，再去调语音链路。

## 8. 联调命令

```bash
# 服务健康（缺密钥返回 503）
curl "http://127.0.0.1:8010/health"

# 下载测试音
curl -o test.wav "http://127.0.0.1:8010/api/voice/test-audio.wav" -H "X-Device-Token: CHANGE_ME"

# 整段对话
curl -i -X POST "http://127.0.0.1:8010/api/voice/chat?deviceId=room-1&sessionId=room-1-001" \
  -H "Content-Type: audio/wav" -H "X-Device-Token: CHANGE_ME" --data-binary @ask.wav -o reply.mp3

# 流式 PCM
curl -X POST "http://127.0.0.1:8010/api/voice/chat-stream?deviceId=room-1" \
  -H "Content-Type: audio/wav" -H "X-Device-Token: CHANGE_ME" --data-binary @ask.wav -o reply.pcm
```
