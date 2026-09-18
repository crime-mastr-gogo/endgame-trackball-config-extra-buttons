#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "8f41b5e6d971d8973873a465e678c88a36e86c8c"

if len(sys.argv) != 2:
    raise SystemExit(
        "usage: apply-p2sm-cleanup.py /path/to/zmk-pointer-2s-mixer"
    )

ROOT = Path(sys.argv[1]).resolve()

if not ROOT.is_dir():
    raise SystemExit(
        f"P2SM module not found: {ROOT}"
    )

actual = subprocess.check_output(
    [
        "git",
        "-C",
        str(ROOT),
        "rev-parse",
        "HEAD",
    ],
    text=True,
).strip()

if actual != EXPECTED_SHA:
    raise SystemExit(
        f"Refusing to patch P2SM revision {actual}; "
        f"expected {EXPECTED_SHA}"
    )

path = ROOT / "src/pointing/pointer_2s_mixer.c"
text = path.read_text()

old = """    p2sm_sens_driver_init();
"""

new = """#if DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_p2sm_sens)
    /*
     * The sensitivity-behavior driver only exists when at least one
     * zmk,behavior-p2sm-sens node exists.
     *
     * Ankur's firmware manages sensitivity directly through the P2SM runtime
     * API, so no legacy sensitivity-behavior nodes are required.
     */
    p2sm_sens_driver_init();
#endif
"""

if old in text:
    text = text.replace(
        old,
        new,
        1,
    )
elif new not in text:
    raise SystemExit(
        "Expected p2sm_sens_driver_init() call was not found"
    )

path.write_text(text)

subprocess.run(
    [
        "git",
        "-C",
        str(ROOT),
        "diff",
        "--check",
    ],
    check=True,
)

print(
    "Applied P2SM no-sensitivity-behavior integration fix to "
    f"{EXPECTED_SHA}"
)
