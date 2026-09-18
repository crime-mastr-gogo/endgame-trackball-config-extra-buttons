#!/usr/bin/env python3

from pathlib import Path
import re
import sys

if len(sys.argv) != 3:
    raise SystemExit(
        "usage: check-build-health.py <production|debug> <build.log>"
    )

kind = sys.argv[1]
log_path = Path(sys.argv[2])

if kind not in {"production", "debug"}:
    raise SystemExit(f"Unknown build kind: {kind}")

text = log_path.read_text(errors="replace")

# Remove terminal colour codes before parsing.
text = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)

limits = {
    "production": {
        "flash": 95.0,
        "ram": 85.0,
    },
    "debug": {
        "flash": 95.0,
        "ram": 92.0,
    },
}

usage = {}

for region, used, size_kb, percent in re.findall(
    r"^\s*(FLASH|RAM):\s+([0-9]+)\s+B\s+([0-9]+)\s+KB\s+([0-9.]+)%",
    text,
    re.MULTILINE,
):
    usage[region.lower()] = {
        "bytes": int(used),
        "size_kb": int(size_kb),
        "percent": float(percent),
    }

for region in ("flash", "ram"):
    if region not in usage:
        raise SystemExit(
            f"BUILD HEALTH FAILED: could not parse {region.upper()} usage"
        )

    percent = usage[region]["percent"]
    maximum = limits[kind][region]

    print(
        f"{kind} {region.upper()}: "
        f"{usage[region]['bytes']} B, {percent:.2f}% "
        f"(limit {maximum:.2f}%)"
    )

    if percent > maximum:
        raise SystemExit(
            f"BUILD HEALTH FAILED: {region.upper()} "
            f"{percent:.2f}% exceeds {maximum:.2f}%"
        )


serious_patterns = (
    "implicit declaration of function",
    "undefined reference",
    "warning: EC11 ",
    "warning: ZMK_ADAPTIVE_FEEDBACK",
    "warning: ZMK_FEEDBACK_COMMON",
    "warning: CONSOLE ",
    "warning: UART_CONSOLE ",
    "warning: SHELL_BACKEND_SERIAL",
    "Deprecated symbol NFCT_PINS_AS_GPIOS",
)

problems = []

for line in text.splitlines():
    if any(pattern in line for pattern in serious_patterns):
        problems.append(line.strip())

if problems:
    print("\nSerious build diagnostics:")
    for problem in problems:
        print(f"  {problem}")

    raise SystemExit(
        "BUILD HEALTH FAILED: serious compiler/Kconfig diagnostic detected"
    )

print(f"{kind} build health passed.")
