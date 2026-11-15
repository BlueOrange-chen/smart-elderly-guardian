#!/usr/bin/env bash
set -euo pipefail
BASE=http://127.0.0.1:8080

echo '=== HEALTH ==='
curl -fsS "$BASE/health"
echo

echo '=== LEGACY WRITE ==='
RESPONSE=$(curl -fsS -X POST "$BASE/sensor/save" -H 'Content-Type: application/json' \
  -d '{"deviceId":"codex-deploy-test","heartRate":78,"bloodOxygen":98,"temperature":25.5,"humidity":48}')
echo "$RESPONSE" | python3 -m json.tool
TEST_ID=$(echo "$RESPONSE" | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')

echo '=== LATEST ==='
curl -fsS "$BASE/api/get-latest-data?deviceId=codex-deploy-test" | python3 -m json.tool

echo '=== WEEKLY REPORT ==='
curl -fsS "$BASE/api/reports/weekly?deviceId=fixedDeviceId" | python3 -m json.tool | head -60

echo '=== ABNORMAL COMPATIBILITY ==='
curl -fsS "$BASE/sensor/abnormal-data/fixedDeviceId?limit=2" | python3 -m json.tool | head -60

echo '=== AI CONFIG GUARD ==='
STATUS=$(curl -sS -o /tmp/ai-check.json -w '%{http_code}' -X POST "$BASE/api/ai-chat" \
  -H 'Content-Type: application/json' -d '{"message":"你好"}')
echo "HTTP $STATUS"
cat /tmp/ai-check.json
echo

echo '=== CLEAN TEST RECORD ==='
ROOT_DB_PASSWORD=$(sudo sed -n 's/.*SPRING_DATASOURCE_PASSWORD=\([^\"]*\).*/\1/p' /etc/systemd/system/sensor-server.service)
MYSQL_PWD="$ROOT_DB_PASSWORD" mysql -h 127.0.0.1 -u root -e "DELETE FROM sensor_db.sensor_data WHERE id=$TEST_ID AND device_id='codex-deploy-test'"
echo "deleted id=$TEST_ID"

echo '=== SERVICE ==='
systemctl is-active health-platform.service
systemctl is-enabled health-platform.service
systemctl is-active sensor-server.service || true
echo VALIDATION_OK
