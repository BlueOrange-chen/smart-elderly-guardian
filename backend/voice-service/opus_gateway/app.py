import asyncio
import audioop
import base64
import hmac
import json
import logging
import os
import time
import uuid

from aiohttp import ClientSession, ClientTimeout, WSMsgType, web

from opus_codec import OpusDecoder, OpusEncoder, OpusError


logging.basicConfig(
    level=os.getenv("LOG_LEVEL", "INFO"),
    format="%(asctime)s %(levelname)s %(message)s",
)


def env(name, default=""):
    return os.getenv(name, default).strip()


def integer_env(name, default):
    try:
        return int(env(name, str(default)))
    except ValueError:
        return default


def float_env(name, default):
    try:
        return float(env(name, str(default)))
    except ValueError:
        return default


def caregiver_prompt():
    return env(
        "AI_SYSTEM_PROMPT",
        (
            "你是一名耐心、可靠、尊重老人的智能看护员，正在直接和需要被看护的老人本人交谈。"
            "先理解老人的意思和情绪，再用温和、自然、简短、容易听懂的中文回答。"
            "称呼对方为‘您’，不要称呼对方为‘孩子’，不要把老人当成儿童。"
            "通常只回答一到两句话、八十个汉字以内，先说最重要的内容。"
            "没有听清时礼貌确认，不要自行编造。你可以陪聊、提醒生活事项并给予情绪安慰，"
            "但不要冒充真人或医生。遇到胸痛、呼吸困难、昏厥、自伤或其他紧急危险时，"
            "明确建议立即联系现场看护人、家属、急救电话或就医。"
        ),
    )


def authorized(request):
    expected = env("DEVICE_API_TOKEN")
    supplied = request.headers.get("X-Device-Token", "")
    return bool(expected) and hmac.compare_digest(supplied, expected)


def upstream_url():
    configured = env("QWEN_REALTIME_URL")
    if configured:
        return configured
    workspace_id = env("DASHSCOPE_WORKSPACE_ID")
    model = env("QWEN_REALTIME_MODEL", "qwen-audio-3.0-realtime-flash")
    if workspace_id:
        return (
            "wss://{}.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model={}"
            .format(workspace_id, model)
        )
    return "wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model={}".format(model)


def upstream_session_update():
    return {
        "event_id": "event_{}".format(uuid.uuid4().hex),
        "type": "session.update",
        "session": {
            "modalities": ["text", "audio"],
            "voice": env("QWEN_REALTIME_VOICE", "longanqian"),
            "input_audio_format": "pcm",
            "output_audio_format": "pcm",
            "instructions": caregiver_prompt(),
            "max_history_turns": integer_env("MAX_CONVERSATION_TURNS", 8),
            "turn_detection": {
                "type": "server_vad",
                "threshold": float_env("VOICE_VAD_THRESHOLD", 0.3),
                "silence_duration_ms": integer_env("VOICE_SILENCE_MS", 700),
            },
        },
    }


async def send_event(device_ws, event_type, **fields):
    payload = {"type": event_type}
    payload.update(fields)
    await device_ws.send_json(payload)


def validate_hello(event):
    audio = event.get("audio_params") or {}
    return (
        event.get("type") == "hello"
        and event.get("version") == 1
        and event.get("transport") == "websocket"
        and audio.get("format") == "opus"
        and audio.get("sample_rate") == 16000
        and audio.get("channels") == 1
        and audio.get("frame_duration_ms") in (20, 40, 60)
    )


async def receive_hello(device_ws):
    message = await device_ws.receive(timeout=5)
    if message.type != WSMsgType.TEXT:
        raise ValueError("first WebSocket frame must be JSON hello")
    try:
        event = json.loads(message.data)
    except (TypeError, ValueError):
        raise ValueError("invalid hello JSON")
    if not validate_hello(event):
        raise ValueError("unsupported hello or audio parameters")
    return event


async def append_pcm(cloud_ws, pcm):
    await cloud_ws.send_json(
        {
            "event_id": "event_{}".format(uuid.uuid4().hex),
            "type": "input_audio_buffer.append",
            "audio": base64.b64encode(pcm).decode("ascii"),
        }
    )


async def finish_turn(device_ws, cloud_ws, state, source):
    if not state["listening"]:
        return
    state["listening"] = False
    silence_ms = integer_env("VOICE_SILENCE_MS", 700) + 200
    silence = b"\x00" * (16000 * 2 * silence_ms // 1000)
    await append_pcm(cloud_ws, silence)
    await send_event(
        device_ws,
        "listen",
        state="stopped",
        source=source,
        inputBytes=state["turn_pcm_bytes"],
    )
    state["turn_pcm_bytes"] = 0


async def device_to_cloud(device_ws, cloud_ws, state, decoder):
    async for message in device_ws:
        if message.type == WSMsgType.BINARY:
            if not state["listening"]:
                await send_event(device_ws, "error", code="not_listening")
                continue
            try:
                pcm = decoder.decode(message.data)
            except OpusError as exc:
                await send_event(device_ws, "error", code="invalid_opus", message=str(exc))
                continue
            state["last_audio_at"] = time.monotonic()
            state["turn_pcm_bytes"] += len(pcm)
            maximum = integer_env("VOICE_MAX_TURN_SECONDS", 30) * 16000 * 2
            if state["turn_pcm_bytes"] > maximum:
                await send_event(device_ws, "error", code="turn_too_long")
                await finish_turn(device_ws, cloud_ws, state, "max_duration")
                continue
            await append_pcm(cloud_ws, pcm)
        elif message.type == WSMsgType.TEXT:
            try:
                event = json.loads(message.data)
            except (TypeError, ValueError):
                await send_event(device_ws, "error", code="invalid_json")
                continue
            event_type = event.get("type")
            if event_type == "ping":
                await send_event(device_ws, "pong", timestamp=int(time.time() * 1000))
            elif event_type == "listen" and event.get("state") == "start":
                state["listening"] = True
                state["last_audio_at"] = time.monotonic()
                state["turn_pcm_bytes"] = 0
                await send_event(device_ws, "listen", state="started")
            elif (
                event_type == "input.end"
                or (event_type == "listen" and event.get("state") == "stop")
            ):
                await finish_turn(device_ws, cloud_ws, state, "device")
            elif event_type == "response.cancel":
                await cloud_ws.send_json(
                    {
                        "event_id": "event_{}".format(uuid.uuid4().hex),
                        "type": "response.cancel",
                    }
                )
            else:
                await send_event(device_ws, "error", code="unsupported_event")
        elif message.type in (WSMsgType.CLOSE, WSMsgType.CLOSED, WSMsgType.ERROR):
            break


async def idle_watchdog(device_ws, cloud_ws, state):
    timeout = integer_env("VOICE_INPUT_IDLE_MS", 1000) / 1000.0
    while not device_ws.closed and not cloud_ws.closed:
        await asyncio.sleep(0.1)
        if (
            state["listening"]
            and state["turn_pcm_bytes"]
            and time.monotonic() - state["last_audio_at"] >= timeout
        ):
            await finish_turn(device_ws, cloud_ws, state, "idle_timeout")


async def cloud_to_device(device_ws, cloud_ws, state, encoder):
    rate_state = None
    async for message in cloud_ws:
        if message.type != WSMsgType.TEXT:
            if message.type in (WSMsgType.CLOSE, WSMsgType.CLOSED, WSMsgType.ERROR):
                break
            continue
        try:
            event = json.loads(message.data)
        except (TypeError, ValueError):
            continue
        event_type = event.get("type", "")
        if event_type == "response.audio.delta":
            encoded_audio = event.get("delta", "")
            if not encoded_audio:
                continue
            upstream_pcm = base64.b64decode(encoded_audio)
            pcm_16k, rate_state = audioop.ratecv(
                upstream_pcm, 2, 1, 24000, 16000, rate_state
            )
            packets = encoder.feed(pcm_16k)
            if packets and not state["tts_started"]:
                state["tts_started"] = True
                await send_event(
                    device_ws,
                    "tts",
                    state="start",
                    audio_params={
                        "format": "opus",
                        "sample_rate": 16000,
                        "channels": 1,
                        "frame_duration_ms": integer_env("OPUS_FRAME_MS", 20),
                    },
                )
            for packet in packets:
                await device_ws.send_bytes(packet)
                state["output_packets"] += 1
        elif event_type == "input_audio_buffer.speech_started":
            await send_event(device_ws, "playback.clear")
        elif event_type == "conversation.item.input_audio_transcription.completed":
            await send_event(
                device_ws,
                "stt",
                text=event.get("transcript", ""),
                final=True,
            )
        elif event_type == "response.audio_transcript.delta":
            await send_event(device_ws, "tts.text.delta", text=event.get("delta", ""))
        elif event_type == "response.done":
            if state["tts_started"]:
                await send_event(device_ws, "tts", state="stop")
            state["tts_started"] = False
            await send_event(device_ws, "response.done")
        elif event_type == "error":
            error = event.get("error") or {}
            await send_event(
                device_ws,
                "error",
                code=error.get("code", "upstream_error"),
                message=error.get("message", "AI voice upstream error"),
            )
        elif event_type in ("session.created", "session.updated"):
            state["cloud_ready"] = True


async def voice_stream(request):
    if not authorized(request):
        return web.json_response({"status": "error", "code": "unauthorized"}, status=401)
    if not env("DASHSCOPE_API_KEY"):
        return web.json_response({"status": "error", "code": "not_configured"}, status=503)
    device_id = request.query.get("deviceId", "").strip()
    if not device_id or len(device_id) > 128:
        return web.json_response({"status": "error", "code": "invalid_device_id"}, status=400)

    device_ws = web.WebSocketResponse(
        heartbeat=20,
        receive_timeout=integer_env("DEVICE_IDLE_TIMEOUT_SECONDS", 180),
        max_msg_size=65536,
        compress=False,
    )
    await device_ws.prepare(request)
    session_id = uuid.uuid4().hex
    decoder = None
    encoder = None
    state = {
        "listening": False,
        "last_audio_at": None,
        "turn_pcm_bytes": 0,
        "tts_started": False,
        "output_packets": 0,
        "cloud_ready": False,
    }
    try:
        hello = await receive_hello(device_ws)
        input_frame_ms = hello["audio_params"]["frame_duration_ms"]
        decoder = OpusDecoder(sample_rate=16000, channels=1, max_frame_ms=60)
        encoder = OpusEncoder(
            sample_rate=16000,
            channels=1,
            frame_ms=integer_env("OPUS_FRAME_MS", 20),
            bitrate=integer_env("OPUS_BITRATE", 20000),
        )
        timeout = ClientTimeout(total=None, sock_connect=15, sock_read=None)
        async with ClientSession(timeout=timeout) as client:
            async with client.ws_connect(
                upstream_url(),
                headers={"Authorization": "Bearer " + env("DASHSCOPE_API_KEY")},
                heartbeat=20,
                max_msg_size=16 * 1024 * 1024,
                compress=0,
            ) as cloud_ws:
                await cloud_ws.send_json(upstream_session_update())
                await send_event(
                    device_ws,
                    "hello",
                    version=1,
                    transport="websocket",
                    session_id=session_id,
                    device_id=device_id,
                    input_audio={
                        "format": "opus",
                        "sample_rate": 16000,
                        "channels": 1,
                        "frame_duration_ms": input_frame_ms,
                    },
                    output_audio={
                        "format": "opus",
                        "sample_rate": 16000,
                        "channels": 1,
                        "frame_duration_ms": integer_env("OPUS_FRAME_MS", 20),
                        "bitrate": integer_env("OPUS_BITRATE", 20000),
                    },
                )
                tasks = [
                    asyncio.ensure_future(device_to_cloud(device_ws, cloud_ws, state, decoder)),
                    asyncio.ensure_future(cloud_to_device(device_ws, cloud_ws, state, encoder)),
                    asyncio.ensure_future(idle_watchdog(device_ws, cloud_ws, state)),
                ]
                done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
                for task in pending:
                    task.cancel()
                for task in done:
                    task.result()
    except asyncio.TimeoutError:
        await send_event(device_ws, "error", code="hello_timeout")
    except (ValueError, OpusError) as exc:
        await send_event(device_ws, "error", code="protocol_error", message=str(exc))
    except asyncio.CancelledError:
        raise
    except Exception:
        logging.exception("Opus voice session failed device=%s session=%s", device_id, session_id)
        if not device_ws.closed:
            await send_event(device_ws, "error", code="gateway_error")
    finally:
        if decoder:
            decoder.close()
        if encoder:
            encoder.close()
        if not device_ws.closed:
            await device_ws.close()
        logging.info(
            "Opus voice disconnected device=%s session=%s output_packets=%d",
            device_id,
            session_id,
            state["output_packets"],
        )
    return device_ws


async def health(_request):
    ready = bool(env("DASHSCOPE_API_KEY") and env("DEVICE_API_TOKEN"))
    return web.json_response(
        {
            "status": "ok" if ready else "not_ready",
            "transport": "websocket",
            "codec": "opus",
            "sampleRate": 16000,
            "frameDurationMs": integer_env("OPUS_FRAME_MS", 20),
        },
        status=200 if ready else 503,
    )


def create_app():
    application = web.Application(client_max_size=65536)
    application.router.add_get("/health", health)
    application.router.add_get("/api/voice/stream", voice_stream)
    return application


if __name__ == "__main__":
    web.run_app(
        create_app(),
        host="0.0.0.0",
        port=integer_env("OPUS_VOICE_PORT", 8011),
        access_log=logging.getLogger("aiohttp.access"),
    )
