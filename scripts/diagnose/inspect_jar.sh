#!/usr/bin/env bash
set -euo pipefail
rm -rf /home/codex/jar_classes
mkdir -p /home/codex/jar_classes
cd /home/codex/jar_classes
unzip -q /opt/sensor-server/sensor-server-0.0.1-SNAPSHOT.jar 'BOOT-INF/classes/*'
for c in BOOT-INF/classes/com/sensorserver/sensor_server/controller/*.class; do
  echo "=====$c====="
  javap -v "$c" 2>/dev/null | grep -E 'RequestMapping|GetMapping|PostMapping|PutMapping|DeleteMapping|Utf8 +/' -A4 -B2 || true
done
