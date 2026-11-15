#!/usr/bin/env bash
set -euo pipefail

ENV_FILE="${ENV_FILE:-/etc/smart-elderly-voice.env}"
INPUT="${STREAM_TEST_INPUT:-/tmp/smart-elderly-voice-test/input.wav}"
OUTPUT=/tmp/smart-elderly-production-stream.pcm
HEADERS=/tmp/smart-elderly-production-stream.headers

set -a
source "$ENV_FILE"
set +a
test -s "$INPUT"
test -n "${DEVICE_API_TOKEN:-}"

METRICS="$(
  curl --silent --show-error --max-time 180 \
    --output "$OUTPUT" \
    --dump-header "$HEADERS" \
    --write-out '%{http_code} %{time_total} %{size_upload} %{size_download}' \
    --request POST \
    --header "X-Device-Token: $DEVICE_API_TOKEN" \
    --header 'Content-Type: audio/wav' \
    --data-binary "@$INPUT" \
    'http://127.0.0.1:8010/api/voice/chat-stream?deviceId=stream-production-check&sessionId=stream-production-check'
)"
unset DEVICE_API_TOKEN
read -r STATUS TOTAL UPLOAD DOWNLOAD <<<"$METRICS"

echo "HTTP_STATUS=$STATUS"
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
echo PRODUCTION_STREAMING_OK
