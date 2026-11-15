#!/usr/bin/env bash
set -euo pipefail

STAGE=/home/codex/voice_phase2_stage
APP=/opt/smart-elderly-voice
ENV_FILE=/etc/smart-elderly-voice.env
UNIT=smart-elderly-voice.service
BACKUP="$APP/backups/phase2-$(date +%Y%m%d-%H%M%S)"

test -f "$STAGE/app.py"
test -f "$STAGE/test_app.py"
test -f "$STAGE/README.md"
sudo -n true

cd "$STAGE"
"$APP/venv/bin/python" -m py_compile app.py test_app.py
"$APP/venv/bin/python" -m unittest -v test_app.py

sudo install -d -o codex -g codex -m 0755 "$BACKUP"
sudo cp -a "$APP/app.py" "$BACKUP/app.py"
sudo cp -a "$ENV_FILE" "$BACKUP/smart-elderly-voice.env"

sudo install -o codex -g codex -m 0644 "$STAGE/app.py" "$APP/app.py"
sudo install -o codex -g codex -m 0644 "$STAGE/test_app.py" "$APP/test_app.py"
sudo install -o codex -g codex -m 0644 "$STAGE/README.md" "$APP/README.md"
sudo install -o codex -g codex -m 0755 \
  "$STAGE/verify_streaming_e2e.sh" "$APP/verify_streaming_e2e.sh"

set_env() {
  local name="$1"
  local value="$2"
  if sudo grep -q "^${name}=" "$ENV_FILE"; then
    sudo sed -i "s/^${name}=.*/${name}=${value}/" "$ENV_FILE"
  else
    printf '%s=%s\n' "$name" "$value" | sudo tee -a "$ENV_FILE" >/dev/null
  fi
}

set_env AI_MAX_TOKENS 160
set_env AI_STREAM_MAX_TOKENS 160
set_env STREAM_SAMPLE_RATE 16000

sudo systemctl restart "$UNIT"
sleep 2
curl --fail --silent --show-error http://127.0.0.1:8010/health
echo
systemctl is-active "$UNIT"
echo "BACKUP=$BACKUP"
echo PHASE2_DEPLOYMENT_OK
