import asyncio
import json
import os
import sys
import wave

from aiohttp import ClientSession, ClientTimeout, WSMsgType

from opus_codec import OpusDecoder, OpusEncoder


async def verify():
    token = os.getenv("DEVICE_API_TOKEN", "")
    wav_path = os.getenv("OPUS_VERIFY_WAV", "/tmp/smart-elderly-voice-test/input.wav")
    url = os.getenv(
        "OPUS_VERIFY_URL",
        "http://127.0.0.1:8011/api/voice/stream?deviceId=ecs-opus-verifier",
    )
    encoder = OpusEncoder(sample_rate=16000, channels=1, frame_ms=20, bitrate=20000)
    decoder = OpusDecoder(sample_rate=16000, channels=1, max_frame_ms=60)
    events = []
    output_packets = 0
    output_pcm_bytes = 0
    try:
        timeout = ClientTimeout(total=60)
        async with ClientSession(timeout=timeout) as client:
            async with client.ws_connect(
                url,
                headers={"X-Device-Token": token},
                compress=0,
            ) as ws:
                await ws.send_json(
                    {
                        "type": "hello",
                        "version": 1,
                        "transport": "websocket",
                        "audio_params": {
                            "format": "opus",
                            "sample_rate": 16000,
                            "channels": 1,
                            "frame_duration_ms": 20,
                        },
                    }
                )
                hello = await ws.receive(timeout=10)
                hello_event = json.loads(hello.data)
                assert hello_event["type"] == "hello"
                assert hello_event["output_audio"]["sample_rate"] == 16000
                events.append("hello")

                await ws.send_json({"type": "listen", "state": "start"})
                started = await ws.receive(timeout=5)
                started_event = json.loads(started.data)
                assert started_event == {"type": "listen", "state": "started"}
                events.append("listen.started")

                with wave.open(wav_path, "rb") as source:
                    assert source.getnchannels() == 1
                    assert source.getsampwidth() == 2
                    assert source.getframerate() == 16000
                    while True:
                        pcm = source.readframes(320)
                        if not pcm:
                            break
                        if len(pcm) < 640:
                            pcm += b"\x00" * (640 - len(pcm))
                        for packet in encoder.feed(pcm):
                            await ws.send_bytes(packet)
                        await asyncio.sleep(0.02)
                await ws.send_json({"type": "listen", "state": "stop"})

                while "response.done" not in events:
                    message = await ws.receive(timeout=30)
                    if message.type == WSMsgType.BINARY:
                        output_packets += 1
                        output_pcm_bytes += len(decoder.decode(message.data))
                    elif message.type == WSMsgType.TEXT:
                        event = json.loads(message.data)
                        event_type = event.get("type")
                        if event_type == "listen" and event.get("state") == "stopped":
                            events.append("listen.stopped")
                        elif event_type == "tts":
                            events.append("tts." + event.get("state", ""))
                        elif event_type == "response.done":
                            events.append("response.done")
                        elif event_type == "error":
                            raise RuntimeError(json.dumps(event, ensure_ascii=False))
                assert output_packets > 0
                assert output_pcm_bytes > 0
        print(
            "OPUS_WEBSOCKET_E2E_OK events={} packets={} pcm_bytes={}".format(
                ",".join(events), output_packets, output_pcm_bytes
            )
        )
    finally:
        encoder.close()
        decoder.close()


if __name__ == "__main__":
    try:
        asyncio.get_event_loop().run_until_complete(verify())
    except Exception as exc:
        print("OPUS_WEBSOCKET_E2E_FAILED {}".format(exc))
        sys.exit(1)
