# 智能养老 AI 语音服务（整段式）

该服务实现第一阶段语音闭环：

`设备整段录音 → ASR 语音识别 → DeepSeek 对话 → TTS 语音合成 → 返回完整音频`

服务独立运行，不修改传感器、摄像头和小程序后端。服务不会持久化原始录音，只在内存中保存最近几轮文字上下文，并按过期时间自动清理。

## 设备接口

### `POST /api/voice/chat?deviceId=设备编号&sessionId=会话编号`

请求头：

```http
X-Device-Token: 与服务器 DEVICE_API_TOKEN 相同的令牌
Content-Type: audio/wav
```

请求体直接放完整音频字节。支持：

- `audio/wav`
- `audio/mpeg`
- `audio/ogg`
- `audio/opus`
- `audio/webm`
- `audio/mp4`

也支持 `multipart/form-data`，字段为：

- `audio`：音频文件
- `deviceId`：设备编号
- `sessionId`：可选；不传时使用 `deviceId`

成功时直接返回可播放的完整音频，例如：

```http
HTTP/1.1 200 OK
Content-Type: audio/mpeg
X-Session-Id: esp32-room-1
X-Transcript: URL编码后的识别文字
X-AI-Reply: URL编码后的回答文字
```

硬件只需读取响应体并播放。`X-Transcript` 和 `X-AI-Reply` 用于调试，可以忽略。

失败时返回 JSON 和非 2xx 状态码。硬件必须先检查状态码，不能把错误 JSON 当成音频播放。

### `POST /api/voice/chat-stream?deviceId=设备编号&sessionId=会话编号`

第二阶段低延迟接口。请求格式、设备令牌和完整 WAV 接口一致；响应改为分块传输的原始 PCM：

```http
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Transfer-Encoding: chunked
X-Audio-Format: pcm_s16le
X-Audio-Sample-Rate: 16000
X-Audio-Bits-Per-Sample: 16
X-Audio-Channels: 1
X-ASR-Ms: 语音识别耗时
X-First-Audio-Ready-Ms: 第一段PCM准备完成耗时
```

服务端先完成 ASR，然后流式读取 AI 文字；每生成一句就立即进行 TTS，并将 WAV 文件头剥离，只下发连续的 PCM 帧。硬件不需要解析 WAV，也不能等待 `Content-Length`，因为响应使用 HTTP chunked 编码。

ESP32 接收要求：

- 确认 HTTP 200 和上述音频参数后再启动 I2S。
- HTTP 客户端必须负责移除 chunked framing，只把响应正文 PCM 交给播放器。
- 建议使用 16～32 KiB 环形缓冲，收到约 100～300 ms 音频后开始播放。
- 每次尽量读取 8～16 KiB，数据可用时不要固定延时。
- 以 HTTP 响应结束作为本轮 PCM 结束，不依赖文件长度。
- 语音上传、PCM 下载和播放期间暂停摄像头上传。

原 `/api/voice/chat` 会继续返回完整 WAV，作为兼容和回退接口。

### `DELETE /api/voice/sessions/{sessionId}`

清除一段对话的文字上下文。开始新的独立对话时调用。

### `GET /health`

检查必要配置是否齐全。未配置密钥时返回 `503`，配置完整时返回 `200`。

### `GET /api/voice/test-audio.wav`

公开下载固定的扬声器测试 WAV。该音频不包含老人数据或密钥，可供硬件联调直接下载播放。

## 本地启动

1. 创建虚拟环境并安装 `requirements.txt`。
2. 将 `voice-service.env.example` 中的配置写入环境变量。
3. 执行：

```bash
python app.py
```

默认监听 `8010` 端口。生产环境可使用附带的 systemd 服务或 Dockerfile。

## 推荐硬件参数

- PCM/WAV：16 kHz、16 bit、单声道。
- 每次静音结束后上传一整个 WAV。
- 单段建议不超过 30 秒。
- AI 音频播放期间暂停麦克风上传，避免设备录到自己播放的声音形成循环。
- 为录音保留约 200～300 毫秒前置缓冲，防止音量门限刚触发时丢掉第一个字。
- 静音结束阈值建议先从 800～1200 毫秒测试，老人说话停顿往往比年轻人长。

## 反向代理示例

将公网 HTTPS 路径转发到本机服务：

```nginx
location /api/voice/ {
    proxy_pass http://127.0.0.1:8010/api/voice/;
    client_max_body_size 10m;
    proxy_read_timeout 180s;
}
```

生产环境必须使用 HTTPS，配置非空且足够长的 `DEVICE_API_TOKEN`，并将环境文件权限设为 `600`。

## 配置说明

中国大陆 ECS 默认使用阿里云百炼语音接口，不依赖 VPN：

- ASR：`qwen3-asr-flash`
- TTS：`qwen-audio-3.0-tts-flash`
- 推荐音色：`longanhuan_v3.6`
- 鉴权：设置北京地域的 `DASHSCOPE_API_KEY`

文本模型配置与现有 `unified_backend` 一致，可复用 DeepSeek 的 `AI_BASE_URL`、`AI_MODEL` 和 `AI_API_KEY`。

如需切回兼容 OpenAI 音频接口的供应商，将 `SPEECH_PROVIDER` 设置为 `openai`，再配置 `SPEECH_BASE_URL` 和 `SPEECH_API_KEY`。
