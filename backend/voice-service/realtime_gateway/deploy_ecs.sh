#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${1:-/tmp/smart-elderly-realtime-upload}"
TARGET_DIR="/opt/smart-elderly-realtime"
SERVICE_FILE="/etc/systemd/system/smart-elderly-realtime.service"
ENV_FILE="/etc/smart-elderly-realtime.env"

if ss -ltn | awk '{print $4}' | grep -qE '(^|:)8011$' \
  && ! systemctl is-active --quiet smart-elderly-realtime.service; then
  echo "Port 8011 is already occupied; refusing to deploy."
  ss -ltnp | grep -E '(^|:)8011[[:space:]]'
  exit 1
fi

id smartvoice >/dev/null 2>&1 || useradd --system --home-dir /opt/smart-elderly-voice --shell /sbin/nologin smartvoice
install -d -o smartvoice -g smartvoice -m 0750 "${TARGET_DIR}"
install -o smartvoice -g smartvoice -m 0640 "${SOURCE_DIR}/app.py" "${TARGET_DIR}/app.py"
install -o smartvoice -g smartvoice -m 0640 "${SOURCE_DIR}/requirements.txt" "${TARGET_DIR}/requirements.txt"

if [[ ! -x "${TARGET_DIR}/venv/bin/python" ]]; then
  python3 -m venv "${TARGET_DIR}/venv"
fi
"${TARGET_DIR}/venv/bin/pip" install --disable-pip-version-check \
  "pip==21.3.1" "setuptools==59.6.0" "wheel==0.37.1"
"${TARGET_DIR}/venv/bin/pip" install --disable-pip-version-check -r "${TARGET_DIR}/requirements.txt"
chown -R smartvoice:smartvoice "${TARGET_DIR}"

if [[ ! -e "${ENV_FILE}" ]]; then
  install -o root -g smartvoice -m 0640 /dev/null "${ENV_FILE}"
fi
install -o root -g root -m 0644 "${SOURCE_DIR}/realtime-voice.service" "${SERVICE_FILE}"

systemctl daemon-reload
systemctl enable --now smart-elderly-realtime.service
sleep 2
systemctl --no-pager --full status smart-elderly-realtime.service
curl --fail --silent --show-error http://127.0.0.1:8011/health
