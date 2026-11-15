# WebSocket Opus 语音服务

该目录为现有第二阶段 HTTP 服务增加一个独立的 WebSocket + Opus 双向流式传输服务。
原 `app.py`、端口 `8010` 和 HTTP/WAV/PCM 接口不做修改。

数据路径：

```text
ESP32 Opus 20 ms 帧
  → WebSocket 长连接
  → 服务端 libopus 解码为 PCM 16 kHz
  → AI 实时语音会话（流式识别、生成、合成）
  → PCM 24 kHz 连续重采样为 16 kHz
  → libopus 编码为 20 ms 帧
  → WebSocket binary frame
  → ESP32 解码并立即 I2S 播放
```

硬件协议见 [HARDWARE_PROTOCOL.md](HARDWARE_PROTOCOL.md)。

运行依赖：

- Python 3.6+
- `aiohttp==3.8.6`
- 系统动态库 `libopus`
- `DASHSCOPE_API_KEY`
- `DEVICE_API_TOKEN`

启动：

```bash
python app.py
```

默认监听 `8011`，健康检查为 `GET /health`，WebSocket 路径为
`GET /api/voice/stream?deviceId=...`。
