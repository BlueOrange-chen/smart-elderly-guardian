import os
import unittest
from unittest import mock

os.environ.setdefault("DEVICE_API_TOKEN", "test-token")
os.environ.setdefault("DASHSCOPE_API_KEY", "test-key")

from app import caregiver_prompt, validate_hello


class ProtocolTests(unittest.TestCase):
    def test_accepts_required_20ms_opus_hello(self):
        self.assertTrue(
            validate_hello(
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
        )

    def test_rejects_pcm_and_wrong_sample_rate(self):
        for audio in (
            {"format": "pcm", "sample_rate": 16000, "channels": 1, "frame_duration_ms": 20},
            {"format": "opus", "sample_rate": 24000, "channels": 1, "frame_duration_ms": 20},
        ):
            self.assertFalse(
                validate_hello(
                    {
                        "type": "hello",
                        "version": 1,
                        "transport": "websocket",
                        "audio_params": audio,
                    }
                )
            )

    def test_prompt_keeps_elderly_caregiver_role(self):
        with mock.patch.dict(os.environ):
            os.environ.pop("AI_SYSTEM_PROMPT", None)
            prompt = caregiver_prompt()
        self.assertIn("智能看护员", prompt)
        self.assertIn("老人本人", prompt)
        self.assertIn("八十个汉字以内", prompt)


if __name__ == "__main__":
    unittest.main()
