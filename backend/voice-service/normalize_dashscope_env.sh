#!/usr/bin/env bash
set -euo pipefail

ENV_FILE=/etc/smart-elderly-voice.env
SNIPPET=/home/codex/voice_service_stage/dashscope.env.snippet

sudo sed -i -E \
  '/^(SPEECH_BASE_URL|SPEECH_API_KEY|ASR_PATH|TTS_PATH|ASR_MODEL|TTS_MODEL|TTS_VOICE|TTS_FORMAT|TTS_SPEED|SPEECH_PROVIDER|DASHSCOPE_API_KEY|DASHSCOPE_ASR_URL|DASHSCOPE_TTS_URL|TTS_SAMPLE_RATE)=/d' \
  "$ENV_FILE"
sudo tee -a "$ENV_FILE" <"$SNIPPET" >/dev/null
sudo systemctl restart smart-elderly-voice.service
sleep 2

echo CONFIG
sudo grep -E \
  '^(SPEECH_PROVIDER|DASHSCOPE_ASR_URL|DASHSCOPE_TTS_URL|ASR_MODEL|TTS_MODEL|TTS_VOICE|TTS_FORMAT)=' \
  "$ENV_FILE"
echo SERVICES
systemctl is-active \
  smart-elderly-voice.service \
  sensor-server.service \
  health-platform.service \
  esp32-camera-stream.service
echo HEALTH
curl -sS -o /tmp/voice-health -w '%{http_code}' http://127.0.0.1:8010/health
echo
cat /tmp/voice-health
echo
