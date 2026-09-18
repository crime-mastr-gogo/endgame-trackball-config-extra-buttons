#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "d5d2e1a6607a60ff8e8cde9cf2d33e49763ee7f6"

if len(sys.argv) != 2:
    raise SystemExit("usage: apply-pmw3610-cleanup.py /path/to/pmw3610")

ROOT = Path(sys.argv[1]).resolve()

actual = subprocess.check_output(
    ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
    text=True,
).strip()

if actual != EXPECTED_SHA:
    raise SystemExit(
        f"Refusing to patch PMW3610 {actual}; expected {EXPECTED_SHA}"
    )

path = ROOT / "src/pmw3610.c"
text = path.read_text()

old = """        const struct device *dev = pmw3610_devs[i];
        const struct pixart_config *config = dev->config;
        struct pixart_data *data = dev->data;

        if (!data->ready) {
"""

new = """        const struct device *dev = pmw3610_devs[i];
        struct pixart_data *data = dev->data;

        if (!data->ready) {
"""

if new not in text:
    if old not in text:
        raise SystemExit("Expected PMW3610 warning block not found")
    text = text.replace(old, new, 1)

path.write_text(text)

subprocess.run(
    ["git", "-C", str(ROOT), "diff", "--check"],
    check=True,
)

print(f"Applied PMW3610 warning cleanup to {EXPECTED_SHA}")
