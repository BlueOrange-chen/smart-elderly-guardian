import io
import os
import unittest
import wave
from unittest.mock import patch
from urllib.parse import unquote

os.environ.setdefault("DEVICE_API_TOKEN", "test-token")
os.environ.setdefault("AI_API_KEY", "test-ai-key")
os.environ.setdefault("SPEECH_API_KEY", "test-speech-key")
os.environ.setdefault("SPEECH_BASE_URL", "https://speech.invalid/v1")
os.environ.setdefault("MIN_AUDIO_BYTES", "4")

from app import ConversationStore, app, build_system_prompt, wav_pcm


def pcm_wav(pcm=b"\x01\x00\x02\x00", sample_rate=16000):
    output = io.BytesIO()
    with wave.open(output, "wb") as target:
        target.setnchannels(1)
        target.setsampwidth(2)
        target.setframerate(sample_rate)
        target.writeframes(pcm)
    return output.getvalue()


class VoiceServiceTests(unittest.TestCase):
    def setUp(self):
        app.config["TESTING"] = True
        self.client = app.test_client()
        self.headers = {"X-Device-Token": "test-token"}

    def test_rejects_missing_token(self):
        response = self.client.post(
            "/api/voice/chat?deviceId=esp32-1",
            data=b"RIFFaudio",
            content_type="audio/wav",
        )
        self.assertEqual(response.status_code, 401)

    def test_rejects_missing_device_id(self):
        response = self.client.post(
            "/api/voice/chat",
            headers=self.headers,
            data=b"RIFFaudio",
            content_type="audio/wav",
        )
        self.assertEqual(response.status_code, 400)
        self.assertEqual(response.get_json()["code"], "invalid_device_id")

    def test_rejects_unsupported_audio(self):
        response = self.client.post(
            "/api/voice/chat?deviceId=esp32-1",
            headers=self.headers,
            data=b"not audio",
            content_type="text/plain",
        )
        self.assertEqual(response.status_code, 400)

    @patch("app.synthesize_speech", return_value=(b"fake-mp3", "audio/mpeg", "mp3"))
    @patch("app.generate_reply", return_value="您好，今天感觉怎么样？")
    @patch("app.transcribe_audio", return_value="你好")
    def test_raw_audio_conversation(self, _asr, _ai, _tts):
        response = self.client.post(
            "/api/voice/chat?deviceId=esp32-1&sessionId=s-1",
            headers=self.headers,
            data=b"RIFFaudio",
            content_type="audio/wav",
        )
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.data, b"fake-mp3")
        self.assertEqual(response.content_type, "audio/mpeg")
        self.assertEqual(unquote(response.headers["X-Transcript"]), "你好")
        self.assertEqual(unquote(response.headers["X-AI-Reply"]), "您好，今天感觉怎么样？")

    @patch("app.synthesize_speech", return_value=(b"fake-mp3", "audio/mpeg", "mp3"))
    @patch("app.generate_reply", return_value="我听到了。")
    @patch("app.transcribe_audio", return_value="测试")
    def test_multipart_audio_conversation(self, _asr, _ai, _tts):
        response = self.client.post(
            "/api/voice/chat",
            headers=self.headers,
            data={
                "deviceId": "esp32-2",
                "sessionId": "s-2",
                "audio": (io.BytesIO(b"RIFFaudio"), "speech.wav"),
            },
            content_type="multipart/form-data",
        )
        self.assertEqual(response.status_code, 200)

    def test_conversation_store_limits_history(self):
        store = ConversationStore()
        with patch.dict(os.environ, {"MAX_CONVERSATION_TURNS": "2"}):
            store.append_turn("s", "一", "答一")
            store.append_turn("s", "二", "答二")
            store.append_turn("s", "三", "答三")
        messages = store.messages("s")
        self.assertEqual(len(messages), 4)
        self.assertEqual(messages[0]["content"], "二")

    def test_clear_session(self):
        response = self.client.delete("/api/voice/sessions/s-1", headers=self.headers)
        self.assertEqual(response.status_code, 204)

    def test_default_prompt_is_elderly_caregiver(self):
        with patch.dict(os.environ):
            os.environ.pop("AI_SYSTEM_PROMPT", None)
            prompt = build_system_prompt()
        self.assertIn("老人本人", prompt)
        self.assertIn("智能看护员", prompt)
        self.assertIn("不要称呼对方为“孩子”", prompt)
        self.assertIn("八十个汉字以内", prompt)

    @patch("app.synthesize_speech")
    @patch("app.stream_reply_sentences")
    @patch("app.transcribe_audio", return_value="您好")
    def test_pcm_stream_conversation(self, _asr, reply_sentences, tts):
        reply_sentences.return_value = iter(["第一句。", "第二句。"])
        tts.side_effect = [
            (pcm_wav(b"\x01\x00"), "audio/wav", "wav"),
            (pcm_wav(b"\x02\x00"), "audio/wav", "wav"),
        ]
        response = self.client.post(
            "/api/voice/chat-stream?deviceId=esp32-stream&sessionId=stream-test",
            headers=self.headers,
            data=b"RIFFaudio",
            content_type="audio/wav",
            buffered=True,
        )
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.data, b"\x01\x00\x02\x00")
        self.assertEqual(response.headers["X-Audio-Format"], "pcm_s16le")
        self.assertEqual(response.headers["X-Audio-Sample-Rate"], "16000")
        self.assertNotIn("Content-Length", response.headers)
        self.assertEqual(tts.call_args_list[0][1]["sample_rate"], 16000)

    def test_wav_pcm_extracts_frames(self):
        self.assertEqual(wav_pcm(pcm_wav(b"\x03\x00"), 16000), b"\x03\x00")

    def test_wav_pcm_rejects_wrong_sample_rate(self):
        with self.assertRaises(ValueError):
            wav_pcm(pcm_wav(sample_rate=24000), 16000)

    @patch("app.requests.post")
    def test_dashscope_asr(self, post):
        post.return_value.raise_for_status.return_value = None
        post.return_value.json.return_value = {
            "choices": [{"message": {"content": "测试识别"}}]
        }
        with patch.dict(
            os.environ,
            {"SPEECH_PROVIDER": "dashscope", "DASHSCOPE_API_KEY": "test-key"},
        ):
            from app import transcribe_audio

            text = transcribe_audio(b"RIFFaudio", "speech.wav", "audio/wav")
        self.assertEqual(text, "测试识别")
        body = post.call_args[1]["json"]
        self.assertEqual(body["model"], "qwen3-asr-flash")
        self.assertTrue(
            body["messages"][0]["content"][0]["input_audio"]["data"].startswith(
                "data:audio/wav;base64,"
            )
        )

    @patch("app.requests.get")
    @patch("app.requests.post")
    def test_dashscope_tts(self, post, get):
        post.return_value.raise_for_status.return_value = None
        post.return_value.json.return_value = {
            "output": {"audio": {"url": "https://audio.invalid/reply.wav"}}
        }
        get.return_value.raise_for_status.return_value = None
        get.return_value.content = b"RIFFreply"
        with patch.dict(
            os.environ,
            {
                "SPEECH_PROVIDER": "dashscope",
                "DASHSCOPE_API_KEY": "test-key",
                "TTS_FORMAT": "wav",
            },
        ):
            from app import synthesize_speech

            audio, content_type, extension = synthesize_speech("您好")
        self.assertEqual(audio, b"RIFFreply")
        self.assertEqual(content_type, "audio/wav")
        self.assertEqual(extension, "wav")


if __name__ == "__main__":
    unittest.main()
