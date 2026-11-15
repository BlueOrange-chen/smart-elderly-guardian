#!/usr/bin/env bash
set -euo pipefail

TEST_DIR=/tmp/smart-elderly-voice-test
VOICE_ENV=/etc/smart-elderly-voice.env
install -d -m 700 "$TEST_DIR"

if [[ ! -s "$TEST_DIR/source.wav" ]]; then
  curl --fail --location --silent --show-error --max-time 60 \
    --user-agent 'Mozilla/5.0' \
    'https://raw.githubusercontent.com/FireRedTeam/FireRedASR/main/examples/wav/BAC009S0764W0121.wav' \
    --output "$TEST_DIR/source.wav"
fi

ffmpeg -hide_banner -loglevel error -y \
  -i "$TEST_DIR/source.wav" \
  -ar 16000 -ac 1 -c:a pcm_s16le \
  "$TEST_DIR/input.wav"

file "$TEST_DIR/input.wav"
echo "INPUT_BYTES=$(wc -c <"$TEST_DIR/input.wav")"

DEVICE_TOKEN="$(sed -n 's/^DEVICE_API_TOKEN=//p' "$VOICE_ENV" | head -1)"
test -n "$DEVICE_TOKEN"

HTTP_METRICS="$(
  curl --silent --show-error --max-time 180 \
    --output "$TEST_DIR/ai-reply.bin" \
    --dump-header "$TEST_DIR/headers.txt" \
    --write-out '%{http_code} %{time_connect} %{time_starttransfer} %{time_total} %{size_upload} %{size_download}' \
    --request POST \
    --header "X-Device-Token: $DEVICE_TOKEN" \
    --header 'Content-Type: audio/wav' \
    --data-binary "@$TEST_DIR/input.wav" \
    'http://127.0.0.1:8010/api/voice/chat?deviceId=codex-speaker-test'
)"
unset DEVICE_TOKEN
read -r HTTP_STATUS CONNECT_SECONDS STARTTRANSFER_SECONDS TOTAL_SECONDS UPLOAD_BYTES DOWNLOAD_BYTES <<<"$HTTP_METRICS"

echo "HTTP_STATUS=$HTTP_STATUS"
echo "CONNECT_SECONDS=$CONNECT_SECONDS"
echo "STARTTRANSFER_SECONDS=$STARTTRANSFER_SECONDS"
echo "TOTAL_SECONDS=$TOTAL_SECONDS"
echo "UPLOAD_BYTES=$UPLOAD_BYTES"
echo "DOWNLOAD_BYTES=$DOWNLOAD_BYTES"
sed -n \
  -e '/^Content-Type:/Ip' \
  -e '/^Content-Length:/Ip' \
  -e '/^X-Request-Id:/Ip' \
  -e '/^X-Session-Id:/Ip' \
  -e '/^X-Transcript:/Ip' \
  -e '/^X-AI-Reply:/Ip' \
  "$TEST_DIR/headers.txt"
echo "RESPONSE_BYTES=$(wc -c <"$TEST_DIR/ai-reply.bin")"

if [[ "$HTTP_STATUS" == "200" ]]; then
  file "$TEST_DIR/ai-reply.bin"
  echo VOICE_TEST_OK
else
  echo ERROR_RESPONSE
  cat "$TEST_DIR/ai-reply.bin"
  exit 1
fi
