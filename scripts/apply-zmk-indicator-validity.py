#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "50d5707901f3dd89f967cf4fee90735758fd9766"

if len(sys.argv) != 2:
    raise SystemExit(
        "usage: apply-zmk-indicator-validity.py /path/to/zmk"
    )

ROOT = Path(sys.argv[1]).resolve()

if not ROOT.is_dir():
    raise SystemExit(f"ZMK checkout not found: {ROOT}")

actual = subprocess.check_output(
    ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
    text=True,
).strip()

if actual != EXPECTED_SHA:
    raise SystemExit(
        f"Refusing to patch ZMK {actual}; expected {EXPECTED_SHA}"
    )


def replace_once(relpath, old, new):
    path = ROOT / relpath
    text = path.read_text()

    if old in text:
        path.write_text(text.replace(old, new, 1))
        return

    if new in text:
        return

    raise SystemExit(
        f"Expected source block not found in {relpath}; "
        "refusing partial HID-indicator patch."
    )


###############################################################################
# Public validity API.
###############################################################################

replace_once(
    "app/include/zmk/hid_indicators.h",
    """#pragma once

#include <zmk/endpoints_types.h>
""",
    """#pragma once

#include <stdbool.h>

#include <zmk/endpoints_types.h>
""",
)

header = ROOT / "app/include/zmk/hid_indicators.h"
text = header.read_text()

api = """
/*
 * A zero indicator bitmap is ambiguous until the current host has actually
 * supplied an LED output report. These helpers let exact-output macros
 * distinguish "Caps Lock OFF" from "no state received in this connection
 * epoch yet".
 */
bool zmk_hid_indicators_current_profile_is_valid(void);
bool zmk_hid_indicators_profile_is_valid(struct zmk_endpoint_instance endpoint);
void zmk_hid_indicators_invalidate_profile(struct zmk_endpoint_instance endpoint);
"""

if "zmk_hid_indicators_current_profile_is_valid" not in text:
    text += api

header.write_text(text)


###############################################################################
# Per-endpoint validity state.
###############################################################################

replace_once(
    "app/src/hid_indicators.c",
    """static zmk_hid_indicators_t hid_indicators[ZMK_ENDPOINT_COUNT];
""",
    """static zmk_hid_indicators_t hid_indicators[ZMK_ENDPOINT_COUNT];
static bool hid_indicators_valid[ZMK_ENDPOINT_COUNT];
""",
)

marker = """zmk_hid_indicators_t zmk_hid_indicators_get_current_profile(void) {
    return zmk_hid_indicators_get_profile(zmk_endpoints_selected());
}

"""

insert = """zmk_hid_indicators_t zmk_hid_indicators_get_current_profile(void) {
    return zmk_hid_indicators_get_profile(zmk_endpoints_selected());
}

bool zmk_hid_indicators_current_profile_is_valid(void) {
    return zmk_hid_indicators_profile_is_valid(zmk_endpoints_selected());
}

bool zmk_hid_indicators_profile_is_valid(struct zmk_endpoint_instance endpoint) {
    const int profile = zmk_endpoint_instance_to_index(endpoint);

    if (profile < 0 || profile >= ZMK_ENDPOINT_COUNT) {
        return false;
    }

    return hid_indicators_valid[profile];
}

void zmk_hid_indicators_invalidate_profile(struct zmk_endpoint_instance endpoint) {
    const int profile = zmk_endpoint_instance_to_index(endpoint);

    if (profile < 0 || profile >= ZMK_ENDPOINT_COUNT) {
        return;
    }

    /*
     * Each entry is a naturally aligned byte-sized value on this target.
     * The existing implementation already relies on independent atomic
     * per-entry reads/writes for hid_indicators[]; validity follows the same
     * single-entry lifetime model.
     */
    hid_indicators[profile] = 0;
    hid_indicators_valid[profile] = false;
}

"""

replace_once(
    "app/src/hid_indicators.c",
    marker,
    insert,
)


###############################################################################
# A host report makes the current epoch valid.
###############################################################################

replace_once(
    "app/src/hid_indicators.c",
    """    hid_indicators[profile] = indicators;

    k_work_submit(&led_changed_work);
""",
    """    hid_indicators[profile] = indicators;
    hid_indicators_valid[profile] = true;

    k_work_submit(&led_changed_work);
""",
)


###############################################################################
# Verification.
###############################################################################

required = {
    "app/include/zmk/hid_indicators.h": (
        "zmk_hid_indicators_current_profile_is_valid",
        "zmk_hid_indicators_profile_is_valid",
        "zmk_hid_indicators_invalidate_profile",
    ),
    "app/src/hid_indicators.c": (
        "hid_indicators_valid[ZMK_ENDPOINT_COUNT]",
        "hid_indicators_valid[profile] = true",
        "hid_indicators_valid[profile] = false",
    ),
}

for relpath, tokens in required.items():
    patched = (ROOT / relpath).read_text()

    for token in tokens:
        if token not in patched:
            raise SystemExit(
                f"ZMK indicator validity patch incomplete: "
                f"{relpath}: missing {token}"
            )

subprocess.run(
    ["git", "-C", str(ROOT), "diff", "--check"],
    check=True,
)

print(
    "Applied per-endpoint HID-indicator validity tracking to "
    f"{EXPECTED_SHA}"
)
