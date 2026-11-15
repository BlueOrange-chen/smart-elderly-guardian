# 智能养老统一后端

一个 Flask 服务整合三组能力：

- 传感数据接收、MySQL 持久化与可选 HTTP 转发；
- DeepSeek OpenAI 兼容接口的健康问答；
- 基于最近 7 天真实传感数据的专家规则周报与可选邮件发送。

## 主要接口

- `GET /health`
- `POST /sensor/save`（兼容旧 ESP32/Java 服务）
- `POST /api/upload-data`
- `GET /api/get-latest-data?deviceId=...`
- 异常数据查询已迁移至 Java `sensor-server`；本服务不再暴露异常查询接口。
- `POST /api/ai-chat`，请求 `{"message":"...","image_url":"..."}`
- `GET|POST /api/reports/weekly`
- `POST /send-report`，请求 `{"deviceId":"...","email":"..."}`

敏感配置只允许写入服务器 `/etc/health-platform.env`，权限应为 `600`。
