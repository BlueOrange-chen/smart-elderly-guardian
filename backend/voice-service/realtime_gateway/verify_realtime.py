import asyncio
import json
import os
import sys
import wave

from aiohttp import ClientSession, ClientTimeout, WSMsgType


async def verify():
    token = os.getenv("DEVICE_API_TOKEN", "")
    if not token:
        raise RuntimeError("DEVICE_API_TOKEN is not configured")
    url = os.getenv(
        "REALTIME_VERIFY_URL",
        "http://127.0.0.1:8011/api/voice/realtime?deviceId=ecs-verifier",
    )
    timeout = ClientTimeout(total=20)
    received = []
    async with ClientSession(timeout=timeout) as client:
        async with client.ws_connect(
            url,
            headers={"X-Device-Token": token},
            compress=0,
        ) as ws:
            for _index in range(5):
                message = await ws.receive(timeout=10)
                if message.type != WSMsgType.TEXT:
                    continue
                event = json.loads(message.data)
                received.append(event.get("type"))
                if event.get("type") == "session.ready":
                    output = event.get("output") or {}
                    if output.get("sampleRate") != 16000:
                        raise RuntimeError("gateway output is not 16000 Hz")
                if event.get("type") == "error":
                    raise RuntimeError(json.dumps(event, ensure_ascii=False))
                if event.get("type") == "session.updated":
                    break
            await ws.send_bytes(b"\x00" * 3200)
            await ws.send_json({"type": "ping"})
            while "pong" not in received:
                message = await ws.receive(timeout=5)
                if message.type == WSMsgType.TEXT:
                    event = json.loads(message.data)
                    received.append(event.get("type"))
                    if event.get("type") == "error":
                        raise RuntimeError(json.dumps(event, ensure_ascii=False))
            wav_path = os.getenv("REALTIME_VERIFY_WAV", "")
            output_bytes = 0
            if wav_path:
                with wave.open(wav_path, "rb") as source:
                    if (
                        source.getnchannels() != 1
                        or source.getsampwidth() != 2
                        or source.getframerate() != 16000
                    ):
                        raise RuntimeError("verify WAV must be PCM 16kHz 16-bit mono")
                    while True:
                        frame = source.readframes(1600)
                        if not frame:
                            break
                        await ws.send_bytes(frame)
                        await asyncio.sleep(0.1)
                if os.getenv("REALTIME_VERIFY_SKIP_END", "") != "1":
                    await ws.send_json({"type": "input.end"})
                while "response.done" not in received:
                    message = await ws.receive(timeout=20)
                    if message.type == WSMsgType.BINARY:
                        output_bytes += len(message.data)
                    elif message.type == WSMsgType.TEXT:
                        event = json.loads(message.data)
                        received.append(event.get("type"))
                        if event.get("type") == "error":
                            raise RuntimeError(json.dumps(event, ensure_ascii=False))
                if not output_bytes:
                    raise RuntimeError("realtime model returned no PCM audio")
    required = {"session.ready", "session.updated", "pong"}
    missing = required.difference(received)
    if missing:
        raise RuntimeError("missing events: {}".format(sorted(missing)))
    print(
        "REALTIME_E2E_OK events={} output_bytes={}".format(
            ",".join(received),
            output_bytes,
        )
    )


if __name__ == "__main__":
    try:
        asyncio.get_event_loop().run_until_complete(verify())
    except Exception as exc:
        print("REALTIME_E2E_FAILED {}".format(exc))
        sys.exit(1)
