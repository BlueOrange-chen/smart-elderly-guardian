#!/usr/bin/env bash
set -euo pipefail
set -a
source /etc/health-platform.env
set +a
echo "AI_BASE_URL=${AI_BASE_URL:-<default>}"
echo "AI_MODEL=${AI_MODEL:-<default>}"
echo "AI_API_KEY_CONFIGURED=$([[ -n ${AI_API_KEY:-} ]] && echo yes || echo no)"
echo "AI_API_KEY_LENGTH=${#AI_API_KEY}"
curl --max-time 10 -sS -o /dev/null -w 'health_http=%{http_code}\n' http://127.0.0.1:8000/health
