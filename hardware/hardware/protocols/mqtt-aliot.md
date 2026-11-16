# 阿里云 IoT 物模型 MQTT（可选链路）

设备 → 阿里云 IoT 平台，与自建后端**并行**、互不依赖。

固件实现：`mqtt_aliot.c`（连接与发布）、`aliot_dm.c`（物模型 JSON）、`main.c`（属性来源）。

> 这条链路用于课程演示"设备接入云平台"，不是业务必需。不接阿里云时把
> `DEVICE_MQTT_ENABLED` 置 0，其余功能不受影响。**凭据同样不能入库。**

## 1. 连接参数

| 参数 | 生成规则 |
|---|---|
| Broker | `mqtts://{ProductKey}.iot-as-mqtt.{Region}.aliyuncs.com:8883` |
| ClientId | `{MAC}|securemode=2,signmethod=hmacmd5|`（当前固件用 STA MAC 的 12 位大写十六进制） |
| Username | `{DeviceName}&{ProductKey}` |
| Password | `hmacmd5(DeviceSecret, "clientId{MAC}deviceName{DeviceName}productKey{ProductKey}")`，32 位大写十六进制 |
| TLS | 8883 端口，`mqtts`，使用服务端根证书（固件里 `g_aliot_ca`） |

Region 当前为 `cn-shanghai`。换地域要同时改 `DEVICE_MQTT_REGION`。

## 2. 上报主题

```text
/sys/{ProductKey}/{DeviceName}/thing/event/property/post
```

QoS 1，非 retain。

## 3. 报文格式

```json
{
  "id": "123",
  "version": "1.0",
  "method": "thing.event.property.post",
  "params": {"HeartRate": 72}
}
```

`id` 每次自增（固件里是静态计数器，转字符串）。

## 4. 当前上报的属性

| 标识符 | 含义 | 来源 | 单位 |
|---|---|---|---|
| `HeartRate` | 心率 | MAX30102 | bpm |
| `blox` | 血氧 | MAX30102 | % |
| `mtemp` | 温度 | DHT11 | ℃（整数） |
| `mhumi` | 湿度 | DHT11 | %RH（整数） |
| `num` | 调试计数器 | 主循环自增 | — |

物模型（TSL）里必须有同名属性，否则平台侧会丢弃或报错。

## 5. 节奏问题（已知）

当前实现是**每个属性发一条独立消息**，主循环 1 s 一轮 → 每秒 6 条 MQTT 发布。
这有三个代价：

1. 流量与 QoS1 确认开销都被放大 6 倍；
2. 平台侧每秒多条属性消息，看板上曲线会出现"先后不一致"的时间抖动；
3. TLS 下频繁发布更容易触发限流。

改进方向（后端同学可一起评审）：

- 用 `aliot_dm` 的 `params` 对象**一次塞入多个属性**，一轮只发一条消息；
- 或把 `num` 这类调试属性去掉，只上报 4 个业务属性。

当前未改，因为要同步调整物模型报文构造与主循环结构，属于下一个迭代。

## 6. 与自建后端的关系

| 对比 | 阿里云物模型 | 自建后端 |
|---|---|---|
| 数据落地 | 阿里云 | MySQL `sensor_data` |
| 谁消费 | 阿里云控制台 / 演示看板 | 微信小程序 |
| 鉴权 | ProductKey + DeviceSecret（hmacmd5） | `X-Device-Token` |
| 断线行为 | `esp_mqtt_client` 内建重连 | 固件自己重试 |

两套同时上报只是为了演示，**不构成冗余备份**：一边挂了另一边照常，但数据不会互补。

## 7. 安全

- `DeviceSecret` 等同密码，泄露后需在阿里云控制台**禁用设备并重置密钥**；
- 公开仓库只放 `CHANGE_ME`；
- 串口日志里不要打印 DeviceSecret 或完整 Password 字段。
