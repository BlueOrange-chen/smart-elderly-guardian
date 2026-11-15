#!/usr/bin/env bash
set -euo pipefail

STAGE=/home/codex/voice_service_stage
APP=/opt/smart-elderly-voice
ENV_FILE=/etc/smart-elderly-voice.env
UNIT_FILE=/etc/systemd/system/smart-elderly-voice.service
UNIT_NAME=smart-elderly-voice.service
PORT=8010

test -f "$STAGE/app.py"
test -f "$STAGE/test_app.py"
sudo -n true

if sudo test -e "$APP" || sudo test -e "$ENV_FILE" || sudo test -e "$UNIT_FILE"; then
  echo "Refusing to overwrite an existing voice-service deployment." >&2
  exit 20
fi
if ss -lnt | grep -q ":${PORT} "; then
  echo "Refusing to use occupied port ${PORT}." >&2
  exit 21
fi

sudo install -d -o codex -g codex -m 0755 "$APP"
sudo install -o codex -g codex -m 0644 \
  "$STAGE/app.py" "$STAGE/test_app.py" "$STAGE/README.md" "$APP/"

python3 -m venv "$APP/venv"
"$APP/venv/bin/pip" install --disable-pip-version-check \
  Flask==2.0.3 Werkzeug==2.0.3 requests==2.27.1 gunicorn==20.1.0

cd "$APP"
"$APP/venv/bin/python" -m unittest -v test_app.py

AI_BASE_URL=https://api.deepseek.com
AI_MODEL=deepseek-chat
AI_API_KEY=
if sudo test -r /etc/health-platform.env; then
  AI_BASE_URL=$(sudo sed -n 's/^AI_BASE_URL=//p' /etc/health-platform.env | head -n1)
  AI_MODEL=$(sudo sed -n 's/^AI_MODEL=//p' /etc/health-platform.env | head -n1)
  AI_API_KEY=$(sudo sed -n 's/^AI_API_KEY=//p' /etc/health-platform.env | head -n1)
fi
DEVICE_API_TOKEN=$(openssl rand -hex 32)

ENV_TMP=$(mktemp)
trap 'rm -f "$ENV_TMP"' EXIT
cat >"$ENV_TMP" <<ENV
PORT=$PORT
LOG_LEVEL=INFO
DEVICE_API_TOKEN=$DEVICE_API_TOKEN
AI_BASE_URL=${AI_BASE_URL:-https://api.deepseek.com}
AI_CHAT_PATH=/chat/completions
AI_MODEL=${AI_MODEL:-deepseek-chat}
AI_API_KEY=$AI_API_KEY
AI_MAX_TOKENS=300
AI_TEMPERATURE=0.6
AI_TIMEOUT_SECONDS=45
SPEECH_PROVIDER=dashscope
DASHSCOPE_API_KEY=
DASHSCOPE_ASR_URL=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions
DASHSCOPE_TTS_URL=https://dashscope.aliyuncs.com/api/v1/services/audio/tts/SpeechSynthesizer
ASR_MODEL=qwen3-asr-flash
ASR_LANGUAGE=zh
ASR_TIMEOUT_SECONDS=60
TTS_MODEL=qwen-audio-3.0-tts-flash
TTS_VOICE=longanhuan_v3.6
TTS_FORMAT=wav
TTS_SAMPLE_RATE=24000
TTS_TIMEOUT_SECONDS=60
MAX_AUDIO_BYTES=10485760
MIN_AUDIO_BYTES=128
SESSION_TTL_SECONDS=1800
MAX_CONVERSATION_TURNS=6
MAX_SESSIONS=1000
ENV
sudo install -o root -g codex -m 0640 "$ENV_TMP" "$ENV_FILE"

UNIT_TMP=$(mktemp)
trap 'rm -f "$ENV_TMP" "$UNIT_TMP"' EXIT
cat >"$UNIT_TMP" <<UNIT
[Unit]
Description=Smart Elderly Isolated AI Voice Service
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=codex
Group=codex
WorkingDirectory=$APP
EnvironmentFile=$ENV_FILE
ExecStart=$APP/venv/bin/gunicorn --workers 1 --threads 4 --timeout 180 --bind 0.0.0.0:$PORT app:app
Restart=on-failure
RestartSec=5
PrivateTmp=true
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
UNIT
sudo install -o root -g root -m 0644 "$UNIT_TMP" "$UNIT_FILE"
sudo systemctl daemon-reload
sudo systemctl enable --now "$UNIT_NAME"

for _ in $(seq 1 20); do
  if curl -sS -o /tmp/smart-elderly-voice-health.json \
    -w '%{http_code}' "http://127.0.0.1:${PORT}/health" >/tmp/smart-elderly-voice-health.code; then
    break
  fi
  sleep 1
done

echo "HEALTH_HTTP=$(cat /tmp/smart-elderly-voice-health.code)"
cat /tmp/smart-elderly-voice-health.json
echo
sudo systemctl --no-pager --full status "$UNIT_NAME"
echo "DEVICE_API_TOKEN=$DEVICE_API_TOKEN"
echo "DEPLOYMENT_STARTED"
