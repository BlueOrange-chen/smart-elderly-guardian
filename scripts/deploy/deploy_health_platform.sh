#!/usr/bin/env bash
set -euo pipefail

STAGE=/home/codex/unified_backend
APP=/opt/health-platform
ENV_FILE=/etc/health-platform.env
OLD_UNIT=/etc/systemd/system/sensor-server.service

test -f "$STAGE/app.py"
test -f "$STAGE/health-platform.service"
sudo -n true

echo '=== Prepare application ==='
sudo install -d -o codex -g codex -m 0755 "$APP"
sudo cp "$STAGE/app.py" "$STAGE/requirements.txt" "$STAGE/README.md" "$STAGE/test_app.py" "$APP/"
sudo chown -R codex:codex "$APP"

if [[ ! -x "$APP/venv/bin/python" ]]; then
  python3 -m venv "$APP/venv"
fi
"$APP/venv/bin/pip" install --disable-pip-version-check -r "$APP/requirements.txt"

echo '=== Configure least-privilege database user ==='
ROOT_DB_PASSWORD=$(sudo sed -n 's/.*SPRING_DATASOURCE_PASSWORD=\([^\"]*\).*/\1/p' "$OLD_UNIT")
APP_DB_PASSWORD=$(openssl rand -hex 24)
MYSQL_PWD="$ROOT_DB_PASSWORD" mysql -h 127.0.0.1 -u root <<SQL
CREATE USER IF NOT EXISTS 'health_app'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY '$APP_DB_PASSWORD';
ALTER USER 'health_app'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY '$APP_DB_PASSWORD';
GRANT SELECT, INSERT ON sensor_db.* TO 'health_app'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL

sudo install -o root -g codex -m 0640 /dev/null "$ENV_FILE"
sudo tee "$ENV_FILE" >/dev/null <<ENV
DB_HOST=127.0.0.1
DB_PORT=3306
DB_NAME=sensor_db
DB_USER=health_app
DB_PASSWORD=$APP_DB_PASSWORD
PORT=8000
CORS_ORIGINS=*
DEVICE_API_TOKEN=
FORWARD_URLS=
AI_BASE_URL=https://api.deepseek.com
AI_MODEL=deepseek-chat
AI_API_KEY=
SMTP_HOST=smtp.163.com
SMTP_PORT=465
SMTP_USER=
SMTP_PASSWORD=
LOG_LEVEL=INFO
ENV

echo '=== Unit tests ==='
cd "$APP"
"$APP/venv/bin/python" -m unittest -v test_app.py

echo '=== Preflight on port 18080 ==='
set -a
source "$ENV_FILE"
set +a
"$APP/venv/bin/gunicorn" --workers 1 --bind 127.0.0.1:18080 app:app >/tmp/health-platform-preflight.log 2>&1 &
PREFLIGHT_PID=$!
trap 'kill "$PREFLIGHT_PID" 2>/dev/null || true' EXIT
for _ in $(seq 1 20); do
  if curl -fsS http://127.0.0.1:18080/health >/tmp/health-platform-health.json; then break; fi
  sleep 1
done
cat /tmp/health-platform-health.json
curl -fsS 'http://127.0.0.1:18080/api/get-latest-data' >/tmp/health-platform-latest.json
python3 -m json.tool /tmp/health-platform-latest.json >/dev/null
kill "$PREFLIGHT_PID"
wait "$PREFLIGHT_PID" 2>/dev/null || true
trap - EXIT

echo '=== Cut over ==='
sudo cp "$STAGE/health-platform.service" /etc/systemd/system/health-platform.service
sudo systemctl daemon-reload
sudo systemctl enable --now health-platform.service

for _ in $(seq 1 30); do
  if curl -fsS http://127.0.0.1:8000/health >/tmp/health-platform-live.json; then break; fi
  sleep 1
done
cat /tmp/health-platform-live.json
curl -fsS 'http://127.0.0.1:8000/api/get-latest-data' | python3 -m json.tool | head -40
sudo systemctl --no-pager --full status health-platform.service
echo 'DEPLOYMENT_OK'
