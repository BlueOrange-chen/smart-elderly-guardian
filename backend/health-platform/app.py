import json
import logging
import os
import smtplib
import ssl
import threading
from datetime import datetime, timedelta
from email.mime.text import MIMEText
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

import pymysql
from flask import Flask, jsonify, request


logging.basicConfig(level=os.getenv("LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
app = Flask(__name__)
app.config["JSON_AS_ASCII"] = False


def env(name, default=""):
    return os.getenv(name, default).strip()


def db_connection():
    return pymysql.connect(
        host=env("DB_HOST", "127.0.0.1"),
        port=int(env("DB_PORT", "3306")),
        user=env("DB_USER", "health_app"),
        password=env("DB_PASSWORD"),
        database=env("DB_NAME", "sensor_db"),
        charset="utf8mb4",
        cursorclass=pymysql.cursors.DictCursor,
        autocommit=True,
        connect_timeout=5,
    )


def first(data, *names, default=None):
    for name in names:
        if name in data and data[name] is not None:
            return data[name]
    return default


def number(value, name, required=True):
    if value in (None, ""):
        if required:
            raise ValueError("缺少字段：%s" % name)
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        raise ValueError("字段 %s 必须是数字" % name)


def normalize_sensor_data(data):
    if not isinstance(data, dict):
        raise ValueError("请求体必须是 JSON 对象")
    result = {
        "device_id": str(first(data, "deviceId", "device_id", default="default-device")).strip(),
        "heart_rate": number(first(data, "heartRate", "heart_Rate", "heart_rate"), "heartRate"),
        "blood_oxygen": number(first(data, "bloodOxygen", "blood_Oxygen", "blood_oxygen", "spo2"), "bloodOxygen"),
        "temperature": number(first(data, "temperature"), "temperature"),
        "humidity": number(first(data, "humidity"), "humidity"),
    }
    if not result["device_id"] or len(result["device_id"]) > 255:
        raise ValueError("deviceId 无效")
    ranges = {"heart_rate": (20, 250), "blood_oxygen": (0, 100), "temperature": (-40, 100), "humidity": (0, 100)}
    for key, bounds in ranges.items():
        if not bounds[0] <= result[key] <= bounds[1]:
            raise ValueError("字段 %s 超出合理范围" % key)
    result["is_abnormal"] = result["heart_rate"] < 60 or result["heart_rate"] > 100 or result["blood_oxygen"] < 95
    result["timestamp"] = datetime.now()
    return result


def bool_value(value):
    if isinstance(value, (bytes, bytearray)):
        return int.from_bytes(value, byteorder="big") != 0
    return bool(value)


def public_sensor(row):
    ts = row.get("timestamp")
    if isinstance(ts, datetime):
        ts = ts.isoformat()
    return {
        "id": row.get("id"), "deviceId": row.get("device_id"), "heartRate": row.get("heart_rate"),
        "bloodOxygen": row.get("blood_oxygen"), "temperature": row.get("temperature"),
        "humidity": row.get("humidity"), "isAbnormal": bool_value(row.get("is_abnormal")), "timestamp": ts,
    }


def insert_sensor(item):
    sql = """INSERT INTO sensor_data
        (blood_oxygen, device_id, heart_rate, humidity, is_abnormal, temperature, timestamp)
        VALUES (%s,%s,%s,%s,%s,%s,%s)"""
    with db_connection() as conn:
        with conn.cursor() as cur:
            cur.execute(sql, (item["blood_oxygen"], item["device_id"], item["heart_rate"], item["humidity"],
                              item["is_abnormal"], item["temperature"], item["timestamp"]))
            item["id"] = cur.lastrowid
    return item


def forward_payload(item):
    urls = [x.strip() for x in env("FORWARD_URLS").split(",") if x.strip()]
    if not urls:
        return
    payload = json.dumps(public_sensor(item), ensure_ascii=False).encode("utf-8")
    for target in urls:
        try:
            req = Request(target, data=payload, headers={"Content-Type": "application/json"}, method="POST")
            with urlopen(req, timeout=5) as response:
                logging.info("Forwarded sensor %s to %s: %s", item.get("id"), target, response.status)
        except Exception as exc:
            logging.warning("Forward failed for %s: %s", target, exc)


def check_device_token():
    expected = env("DEVICE_API_TOKEN")
    return not expected or request.headers.get("X-Device-Token", "") == expected


@app.after_request
def cors(response):
    allowed = env("CORS_ORIGINS", "*")
    origin = request.headers.get("Origin")
    if allowed == "*":
        response.headers["Access-Control-Allow-Origin"] = "*"
    elif origin and origin in [x.strip() for x in allowed.split(",")]:
        response.headers["Access-Control-Allow-Origin"] = origin
        response.headers["Vary"] = "Origin"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, X-Device-Token"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    return response


@app.route("/health", methods=["GET"])
def health():
    try:
        with db_connection() as conn:
            with conn.cursor() as cur:
                cur.execute("SELECT 1")
        return jsonify(status="ok", database="ok", ai="configured" if env("AI_API_KEY") else "not_configured")
    except Exception:
        logging.exception("Health check failed")
        return jsonify(status="error", database="unavailable"), 503


def save_response(legacy=False):
    if not check_device_token():
        return jsonify(status="error", message="设备凭据无效"), 401
    try:
        item = insert_sensor(normalize_sensor_data(request.get_json(silent=True)))
    except ValueError as exc:
        return jsonify(status="error", message=str(exc)), 400
    except Exception:
        logging.exception("Sensor insert failed")
        return jsonify(status="error", message="数据保存失败"), 500
    threading.Thread(target=forward_payload, args=(item,), daemon=True).start()
    return (jsonify(public_sensor(item)), 200) if legacy else (jsonify(status="success", message="数据接收成功", data=public_sensor(item)), 201)


@app.route("/sensor/save", methods=["POST", "OPTIONS"])
def legacy_sensor_save():
    return ("", 204) if request.method == "OPTIONS" else save_response(True)


@app.route("/api/upload-data", methods=["POST", "OPTIONS"])
def upload_data():
    return ("", 204) if request.method == "OPTIONS" else save_response(False)


def query_sensors(device_id=None, abnormal_only=False, days=None, limit=100):
    where, args = [], []
    if device_id:
        where.append("device_id=%s"); args.append(device_id)
    if abnormal_only:
        where.append("is_abnormal=1")
    if days:
        where.append("timestamp >= %s"); args.append(datetime.now() - timedelta(days=days))
    clause = " WHERE " + " AND ".join(where) if where else ""
    sql = "SELECT * FROM sensor_data" + clause + " ORDER BY timestamp DESC LIMIT %s"
    args.append(min(max(int(limit), 1), 1000))
    with db_connection() as conn:
        with conn.cursor() as cur:
            cur.execute(sql, args)
            return cur.fetchall()


@app.route("/api/get-latest-data", methods=["GET"])
def latest_data():
    rows = query_sensors(device_id=request.args.get("deviceId"), limit=1)
    if not rows:
        return jsonify(status="error", message="暂无数据"), 404
    return jsonify(status="success", data=public_sensor(rows[0]))


def ai_request(message, image_url=None):
    api_key = env("AI_API_KEY")
    if not api_key:
        raise RuntimeError("AI 服务尚未配置 API 密钥")
    if image_url:
        raise ValueError("当前 DeepSeek 文本模型不支持图片输入")
    body = {
        "model": env("AI_MODEL", "deepseek-chat"),
        "messages": [
            {"role": "system", "content": "你是面向老年人与家属的健康信息助手。使用温和、通俗的中文解释，不做确定性诊断，不替代医生。遇到胸痛、呼吸困难、昏厥、严重低血氧等情况，明确建议立即拨打急救电话或就医。"},
            {"role": "user", "content": message},
        ],
        "max_tokens": 500,
    }
    url = env("AI_BASE_URL", "https://api.deepseek.com").rstrip("/") + "/chat/completions"
    req = Request(url, data=json.dumps(body).encode("utf-8"), headers={"Authorization": "Bearer " + api_key, "Content-Type": "application/json"}, method="POST")
    with urlopen(req, timeout=30) as response:
        result = json.loads(response.read().decode("utf-8"))
    return result["choices"][0]["message"]["content"]


@app.route("/api/ai-chat", methods=["POST", "OPTIONS"])
@app.route("/chat", methods=["POST", "OPTIONS"])
def ai_chat():
    if request.method == "OPTIONS":
        return "", 204
    data = request.get_json(silent=True) or {}
    message = str(data.get("message", "")).strip(); image_url = str(data.get("image_url", "")).strip()
    if not message and not image_url:
        return jsonify(status="error", message="必须提供文字或图片"), 400
    try:
        return jsonify(status="success", reply=ai_request(message, image_url))
    except (HTTPError, URLError, RuntimeError, KeyError, ValueError) as exc:
        logging.warning("AI request failed: %s", exc)
        return jsonify(status="error", message=str(exc)), 503
    except Exception:
        logging.exception("AI request failed")
        return jsonify(status="error", message="AI 服务暂时不可用"), 503


def evaluate(hr, spo2):
    notes = []
    if hr < 60: notes.append("平均心率偏低，建议结合症状咨询医生。")
    elif hr > 100: notes.append("平均心率偏高，建议休息后复测，持续异常时就医。")
    else: notes.append("平均心率处于常见成人静息范围。")
    if spo2 < 90: notes.append("平均血氧明显偏低；如当前读数仍低或伴呼吸困难，请立即就医。")
    elif spo2 < 95: notes.append("平均血氧偏低，建议复测并咨询医生。")
    else: notes.append("平均血氧处于常见范围。")
    return "".join(notes)


def build_weekly_report(device_id):
    rows = query_sensors(device_id=device_id, days=7, limit=1000)
    if not rows:
        raise ValueError("最近 7 天没有健康数据")
    avg_hr = sum(float(x["heart_rate"]) for x in rows) / len(rows)
    avg_spo2 = sum(float(x["blood_oxygen"]) for x in rows) / len(rows)
    avg_temp = sum(float(x["temperature"]) for x in rows) / len(rows)
    avg_humidity = sum(float(x["humidity"]) for x in rows) / len(rows)
    abnormal = sum(1 for x in rows if bool_value(x["is_abnormal"]))
    end = datetime.now(); start = end - timedelta(days=7)
    text = ("每周健康报告（%s 至 %s）\n\n设备：%s\n记录数：%d\n异常记录数：%d\n"
            "平均心率：%.1f bpm\n平均血氧：%.1f %%\n平均温度：%.1f ℃\n平均湿度：%.1f %%\n\n健康提示：%s\n\n"
            "本报告仅用于健康趋势参考，不能替代医生诊断。" %
            (start.strftime("%Y-%m-%d"), end.strftime("%Y-%m-%d"), device_id, len(rows), abnormal,
             avg_hr, avg_spo2, avg_temp, avg_humidity, evaluate(avg_hr, avg_spo2)))
    return {"deviceId": device_id, "recordCount": len(rows), "abnormalCount": abnormal,
            "averageHeartRate": round(avg_hr, 1), "averageBloodOxygen": round(avg_spo2, 1),
            "averageTemperature": round(avg_temp, 1), "averageHumidity": round(avg_humidity, 1), "report": text}


def send_email(report, receiver):
    host = env("SMTP_HOST"); user = env("SMTP_USER"); password = env("SMTP_PASSWORD")
    if not host or not user or not password:
        raise RuntimeError("邮件服务尚未配置")
    msg = MIMEText(report, "plain", "utf-8"); msg["From"] = user; msg["To"] = receiver; msg["Subject"] = "每周健康报告"
    with smtplib.SMTP_SSL(host, int(env("SMTP_PORT", "465")), context=ssl.create_default_context(), timeout=15) as server:
        server.login(user, password); server.sendmail(user, [receiver], msg.as_string())


@app.route("/api/reports/weekly", methods=["GET", "POST"])
@app.route("/send-report", methods=["POST"])
def weekly_report():
    data = request.get_json(silent=True) or {}
    device_id = str(first(data, "deviceId", "device_id", default=request.args.get("deviceId", "default-device"))).strip()
    try:
        result = build_weekly_report(device_id)
        receiver = str(data.get("email", "")).strip()
        if receiver:
            send_email(result["report"], receiver); result["emailSent"] = True
        return jsonify(status="success", **result)
    except (ValueError, RuntimeError) as exc:
        return jsonify(status="error", message=str(exc)), 400
    except Exception:
        logging.exception("Weekly report failed")
        return jsonify(status="error", message="周报生成或发送失败"), 500


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=int(env("PORT", "8000")), debug=False)
