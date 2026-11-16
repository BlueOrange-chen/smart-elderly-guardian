#!/usr/bin/env python3
"""引脚冲突自检。

扫描 hardware/firmware/*/main 下的引脚宏定义，检查：
  1. 同一个 GPIO 被多个功能占用（冲突）；
  2. 是否占用了 ESP32-S3 的保留引脚（Flash / PSRAM / USB / UART0 / strapping）；
  3. 汇总所有已分配引脚，便于和 PIN_MAP.md 对照。

用法：
    python hardware/tools/check_pin_conflicts.py
    python hardware/tools/check_pin_conflicts.py --strict   # 有冲突时返回非 0

不依赖第三方库，纯标准库。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# ESP32-S3 保留引脚
RESERVED: dict[int, str] = {}
for _n in range(26, 33):
    RESERVED[_n] = "SPI Flash"
for _n in range(33, 38):
    RESERVED[_n] = "八线 PSRAM"
RESERVED[19] = "原生 USB D-"
RESERVED[20] = "原生 USB D+"
RESERVED[43] = "UART0 TX"
RESERVED[44] = "UART0 RX"
RESERVED[45] = "Strapping"
RESERVED[46] = "Strapping"

# 只关心名字里带这些词的宏，避免把超时/参数值误当成引脚
PIN_HINT = re.compile(
    r"(GPIO|_PIN|PIN_|_IO\b|SDA|SCL|BCLK|LRCLK|_WS\b|DIN|DOUT|VSYNC|HREF|PCLK|XCLK|SD\b|D[0-7]\b)"
)

RE_GPIO_NUM = re.compile(r"#define\s+(?P<name>[A-Za-z_]\w*)\s+GPIO_NUM_(?P<num>\d+)\b")
RE_PLAIN = re.compile(r"#define\s+(?P<name>[A-Za-z_]\w*)\s+(?P<num>\d+)\b")

# 明确不是引脚的宏名片段
SKIP = re.compile(r"(TIMER|CHANNEL|FREQ|QUALITY|COUNT|MS|BYTES|SAMPLES|LEVEL|BIT|STACK|PRIO)")


def scan(firmware_root: Path) -> dict[int, list[tuple[str, str]]]:
    """返回 {gpio: [(宏名, 相对文件)]}"""
    pins: dict[int, list[tuple[str, str]]] = {}
    for path in sorted(firmware_root.rglob("*")):
        if path.suffix not in {".c", ".h"} or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            if not line.lstrip().startswith("#define"):
                continue
            for regex in (RE_GPIO_NUM, RE_PLAIN):
                m = regex.search(line)
                if not m:
                    continue
                name, num = m.group("name"), int(m.group("num"))
                if not PIN_HINT.search(name) or SKIP.search(name):
                    continue
                rel = f"{path.relative_to(firmware_root.parent.parent)}:{lineno}"
                pins.setdefault(num, []).append((name, rel))
                break
    return pins


def main() -> int:
    ap = argparse.ArgumentParser(description="扫描固件引脚定义并检查冲突")
    ap.add_argument("--strict", action="store_true", help="发现冲突或占用保留脚时返回非 0")
    ap.add_argument("--root", default=None, help="hardware 目录，默认脚本所在目录的上一级")
    args = ap.parse_args()

    hardware = Path(args.root) if args.root else Path(__file__).resolve().parent.parent
    firmware = hardware / "firmware"
    if not firmware.is_dir():
        print(f"[错误] 找不到固件目录：{firmware}")
        return 2

    pins = scan(firmware)
    if not pins:
        print(f"[警告] 在 {firmware} 下没有扫描到任何引脚宏定义")
        return 2

    conflicts, reserved_hits = [], []
    for gpio in sorted(pins):
        users = pins[gpio]
        names = sorted({name for name, _ in users})
        if gpio in RESERVED:
            reserved_hits.append((gpio, RESERVED[gpio], names))
        if len(names) > 1:
            conflicts.append((gpio, names, users))

    print(f"扫描目录：{firmware}")
    print(f"共发现 {len(pins)} 个已分配 GPIO\n")

    print("GPIO  功能宏                              位置")
    print("-" * 92)
    for gpio in sorted(pins):
        for name, where in pins[gpio]:
            flag = "  <-- 保留脚!" if gpio in RESERVED else ""
            print(f"{gpio:<5} {name:<36} {where}{flag}")

    print()
    if conflicts:
        print(f"[冲突] {len(conflicts)} 个 GPIO 被多个功能占用：")
        for gpio, names, users in conflicts:
            print(f"  GPIO{gpio}: {', '.join(names)}")
            for name, where in users:
                print(f"      - {name} @ {where}")
    else:
        print("[OK] 没有发现引脚重复占用")

    if reserved_hits:
        print(f"\n[保留脚] {len(reserved_hits)} 个 GPIO 落在 S3 保留区：")
        for gpio, why, names in reserved_hits:
            print(f"  GPIO{gpio} ({why}): {', '.join(names)}")
    else:
        print("[OK] 没有占用 S3 保留引脚")

    print("\n提示：本脚本只做静态检查，最终以 PIN_MAP.md 与实物接线为准。")

    if conflicts or reserved_hits:
        return 1 if args.strict else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
