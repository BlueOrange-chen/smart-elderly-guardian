#!/usr/bin/env bash
set -euo pipefail

APP=/opt/smart-elderly-voice
ENV_FILE=/etc/smart-elderly-voice.env
BASE_URL=http://127.0.0.1:8010
SEED=/tmp/smart-elderly-voice-seed.wav
REPLY=/tmp/smart-elderly-voice-reply.wav
HEADERS=/tmp/smart-elderly-voice-reply.headers

set -a
source "$ENV_FILE"
set +a

cd "$APP"
"$APP/venv/bin/python" - <<'PY'
from app import synthesize_speech

audio, content_type, extension = synthesize_speech("你好，请简单介绍一下你自己。")
if content_type != "audio/wav" or extension != "wav":
    raise RuntimeError("unexpected seed audio format: %s/%s" % (content_type, extension))
with open("/tmp/smart-elderly-voice-seed.wav", "wb") as handle:
    handle.write(audio)
print("seed_audio_bytes=%d" % len(audio))
PY

http_code=$(
  curl -sS \
    --max-time 180 \
    -D "$HEADERS" \
    -o "$REPLY" \
    -w '%{http_code}' \
    -X POST \
    "$BASE_URL/api/voice/chat?deviceId=deployment-check&sessionId=deployment-check" \
    -H "X-Device-Token: $DEVICE_API_TOKEN" \
    -H 'Content-Type: audio/wav' \
    --data-binary "@$SEED"
)

echo "http_code=$http_code"
grep -Ei '^(Content-Type|X-Request-Id|X-Session-Id|X-Transcript|X-AI-Reply):' "$HEADERS"
echo "reply_audio_bytes=$(wc -c <"$REPLY")"

test "$http_code" = "200"
test "$(head -c 4 "$REPLY")" = "RIFF"
echo E2E_OK
