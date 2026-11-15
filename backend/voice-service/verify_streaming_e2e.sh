#!/usr/bin/env bash
set -euo pipefail

APP_DIR="${APP_DIR:-/opt/smart-elderly-voice}"
ENV_FILE="${ENV_FILE:-/etc/smart-elderly-voice.env}"
SOURCE_DIR="${SOURCE_DIR:-$APP_DIR}"
TEST_PORT="${STREAM_TEST_PORT:-18010}"
INPUT="${STREAM_TEST_INPUT:-/tmp/smart-elderly-voice-test/input.wav}"
OUTPUT=/tmp/smart-elderly-stream-test.pcm
HEADERS=/tmp/smart-elderly-stream-test.headers
LOG=/tmp/smart-elderly-stream-preflight.log

set -a
source "$ENV_FILE"
set +a
test -s "$INPUT"
test -n "${DEVICE_API_TOKEN:-}"

cd "$SOURCE_DIR"
"$APP_DIR/venv/bin/gunicorn" \
  --workers 1 --threads 2 --timeout 180 \
  --chdir "$SOURCE_DIR" \
  --bind "127.0.0.1:$TEST_PORT" app:app >"$LOG" 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

for _ in $(seq 1 30); do
  if curl --fail --silent "http://127.0.0.1:$TEST_PORT/health" >/dev/null; then
    break
  fi
  sleep 1
done

METRICS="$(
  curl --silent --show-error --max-time 180 \
    --output "$OUTPUT" \
    --dump-header "$HEADERS" \
    --write-out '%{http_code} %{time_connect} %{time_starttransfer} %{time_total} %{size_upload} %{size_download}' \
    --request POST \
    --header "X-Device-Token: $DEVICE_API_TOKEN" \
    --header 'Content-Type: audio/wav' \
    --data-binary "@$INPUT" \
    "http://127.0.0.1:$TEST_PORT/api/voice/chat-stream?deviceId=stream-preflight&sessionId=stream-preflight"
)"
unset DEVICE_API_TOKEN
read -r STATUS CONNECT START_TRANSFER TOTAL UPLOAD DOWNLOAD <<<"$METRICS"

echo "HTTP_STATUS=$STATUS"
echo "CONNECT_SECONDS=$CONNECT"
echo "STARTTRANSFER_SECONDS=$START_TRANSFER"
echo "TOTAL_SECONDS=$TOTAL"
echo "UPLOAD_BYTES=$UPLOAD"
echo "DOWNLOAD_BYTES=$DOWNLOAD"
sed -n \
  -e '/^Content-Type:/Ip' \
  -e '/^Transfer-Encoding:/Ip' \
  -e '/^X-Audio-/Ip' \
  -e '/^X-ASR-Ms:/Ip' \
  -e '/^X-First-Audio-Ready-Ms:/Ip' \
  "$HEADERS"

test "$STATUS" = 200
test -s "$OUTPUT"
test $(( $(wc -c <"$OUTPUT") % 2 )) -eq 0
echo STREAMING_E2E_OK
