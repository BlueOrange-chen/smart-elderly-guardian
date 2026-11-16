# 体征数据上报协议

设备 → 健康平台（`backend/health-platform/app.py`）。

固件实现：`firmware/smart-terminal/main/main.c` 的 `upload_data_to_api()`。

## 1. 接口选择

| 接口 | 用途 | 成功状态码 | 响应体 |
|---|---|---|---|
| `POST /sensor/save` | **旧接口，当前固件使用**（兼容旧 ESP32/Java 服务） | `200` | 单条记录 JSON |
| `POST /api/upload-data` | 新接口，建议新代码使用 | `201` | `{"status":"success",...}` |
| `GET /api/get-latest-data?deviceId=` | 小程序/调试查询最新一条 | `200` | `{"status":"success","data":{...}}` |

## 2. 请求

```http
POST {HEALTH_BASE_URL}/sensor/save HTTP/1.1
Content-Type: application/json
X-Device-Token: {DEVICE_HEALTH_TOKEN}   ← 后端启用 DEVICE_API_TOKEN 时必填，否则可省略

{"deviceId":"room-1","temperature":26,"humidity":58,"bloodOxygen":97,"heartRate":72}
```

环境变量 `HEALTH_BASE_URL` 只写协议+主机+端口，例如 `http://192.168.1.10:8000`。

## 3. 字段

| 字段 | 类型 | 必填 | 取值范围 | 说明 |
|---|---|---|---|---|
| `deviceId` | string | 否 | ≤ 255 字符 | 缺省 `default-device`；**实际必须显式传**，否则多设备数据混在一起 |
| `heartRate` | number | **是** | 20–250 | bpm |
| `bloodOxygen` | number | **是** | 0–100 | 血氧饱和度 % |
| `temperature` | number | **是** | −40–100 | ℃ |
| `humidity` | number | **是** | 0–100 | %RH |

后端同时兼容下划线写法：`heart_rate`、`blood_oxygen`、`spo2`、`device_id`。

**范围校验失败会直接返回 400**，所以固件必须先做本地裁剪：传感器读数异常（例如 MAX30102 未接触到手指时
`latest_heart_rate` 可能是 0）时，不要上报，等下一次有效读数。

## 4. 后端派生字段

| 字段 | 规则 |
|---|---|
| `isAbnormal` | `heartRate < 60` 或 `heartRate > 100` 或 `bloodOxygen < 95` |
| `timestamp` | 服务器当前时间（`datetime.now()`） |
| `id` | 自增主键 |

设备无法也不应该覆盖这三个字段。

## 5. 响应

成功（`/sensor/save`，200）：

```json
{"id":1024,"deviceId":"room-1","heartRate":72,"bloodOxygen":97,
 "temperature":26.0,"humidity":58.0,"isAbnormal":false,"timestamp":"2026-11-16T19:30:00"}
```

成功（`/api/upload-data`，201）：

```json
{"status":"success","message":"数据接收成功","data":{"id":1024,...}}
```

失败：

| 状态码 | 返回 | 触发条件 |
|---|---|---|
| `400` | `{"status":"error","message":"缺少字段：heartRate"}` 或 `字段 heart_rate 超出合理范围` | 缺字段 / 类型错 / 超范围 |
| `401` | `{"status":"error","message":"设备凭据无效"}` | 后端设了 `DEVICE_API_TOKEN` 而设备没带或带错 |
| `500` | `{"status":"error","message":"数据保存失败"}` | MySQL 不可用 |
| `503` | `{"status":"error","database":"unavailable"}` | `GET /health` 数据库探活失败 |

## 6. 上报节奏

当前固件：主循环每 **1000 ms** 上报一次。

建议：

- 正常状态 1 s 一条足够，一周约 60 万条——如果演示时间跨度长，改成 5–10 s 一条；
- 异常状态（`isAbnormal` 为真）不必提高频率，后端周报按平均值与异常计数统计；
- 一次请求失败不要立刻重试，按 [`retry-and-errors.md`](retry-and-errors.md) 退避。

## 7. 联调命令

```bash
# 上报一条
curl -i -X POST "http://127.0.0.1:8000/sensor/save" \
  -H "Content-Type: application/json" \
  -H "X-Device-Token: CHANGE_ME" \
  -d '{"deviceId":"room-1","heartRate":72,"bloodOxygen":97,"temperature":26,"humidity":58}'

# 查最新
curl "http://127.0.0.1:8000/api/get-latest-data?deviceId=room-1"

# 服务健康
curl "http://127.0.0.1:8000/health"
```
