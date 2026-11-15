import os
import unittest
from unittest import mock
import audioop

from aiohttp.test_utils import AioHTTPTestCase

import app


class GatewayTests(AioHTTPTestCase):
    async def get_application(self):
        return app.create_app()

    async def test_health_ready(self):
        with mock.patch.dict(
            os.environ,
            {"DASHSCOPE_API_KEY": "key", "DEVICE_API_TOKEN": "token"},
            clear=False,
        ):
            response = await self.client.request("GET", "/health")
            self.assertEqual(200, response.status)
            body = await response.json()
            self.assertEqual(3, body["stage"])

    async def test_realtime_rejects_missing_token(self):
        with mock.patch.dict(
            os.environ,
            {"DASHSCOPE_API_KEY": "key", "DEVICE_API_TOKEN": "token"},
            clear=False,
        ):
            response = await self.client.request(
                "GET",
                "/api/voice/realtime?deviceId=test-device",
            )
            self.assertEqual(401, response.status)

    async def test_output_resampling_is_16khz_sized(self):
        source = b"\x00\x00" * 2400
        converted, state = audioop.ratecv(source, 2, 1, 24000, 16000, None)
        self.assertIsNotNone(state)
        self.assertEqual(3200, len(converted))


if __name__ == "__main__":
    unittest.main()
