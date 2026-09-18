#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "50d5707901f3dd89f967cf4fee90735758fd9766"

if len(sys.argv) != 2:
    raise SystemExit("usage: apply-zmk-reliability-fix.py /path/to/zmk")

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

    # Always prefer replacing the exact OLD block when it still exists.
    # A similar NEW string elsewhere in the same file must not cause this
    # patch to be skipped.
    if old in text:
        path.write_text(text.replace(old, new, 1))
        return

    if new in text:
        return

    raise SystemExit(
        f"Expected source block not found in {relpath}; "
        "refusing partial ZMK patch."
    )


# ---------------------------------------------------------------------------
# Runtime-PM declaration.
# ---------------------------------------------------------------------------

replace_once(
    "app/module/drivers/kscan/kscan_gpio_direct.c",
    """#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>
""",
    """#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>
""",
)


# ---------------------------------------------------------------------------
# Modifier release after an explicit lifecycle clear must be harmless.
# ---------------------------------------------------------------------------

replace_once(
    "app/src/hid.c",
    """    if (explicit_modifier_counts[modifier] <= 0) {
        LOG_ERR("Tried to unregister modifier %d too often", modifier);
        return -EINVAL;
    }
""",
    """    if (explicit_modifier_counts[modifier] <= 0) {
        /*
         * A lifecycle/endpoint clear may have reset the refcount before the
         * physical release event arrives. Treat that trailing release as an
         * idempotent no-op rather than corrupting state or reporting an error.
         */
        LOG_DBG("Ignoring release of already-cleared modifier %d", modifier);
        return 0;
    }
""",
)


# ---------------------------------------------------------------------------
# Complete keyboard clear: report + all hidden modifier/refcount state.
# ---------------------------------------------------------------------------

replace_once(
    "app/src/hid.c",
    """void zmk_hid_keyboard_clear(void) {
    memset(&keyboard_report.body, 0, sizeof(keyboard_report.body));
}
""",
    """void zmk_hid_keyboard_clear(void) {
    memset(&keyboard_report.body, 0, sizeof(keyboard_report.body));

    memset(explicit_modifier_counts, 0, sizeof(explicit_modifier_counts));
    explicit_modifiers = 0;
    implicit_modifiers = 0;
    masked_modifiers = 0;

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    keys_held = 0;
#endif
}
""",
)


# ---------------------------------------------------------------------------
# Mouse release after lifecycle reset must also be harmless.
# ---------------------------------------------------------------------------

replace_once(
    "app/src/hid.c",
    """    if (explicit_button_counts[button] <= 0) {
        LOG_ERR("Tried to release button %d too often", button);
        return -EINVAL;
    }
""",
    """    if (explicit_button_counts[button] <= 0) {
        /*
         * Endpoint/lifecycle clearing may precede the physical release.
         * The trailing release is intentionally idempotent.
         */
        LOG_DBG("Ignoring release of already-cleared mouse button %d", button);
        return 0;
    }
""",
)


# ---------------------------------------------------------------------------
# Complete mouse clear: report + hidden mouse-button refcount state.
# ---------------------------------------------------------------------------

replace_once(
    "app/src/hid.c",
    """void zmk_hid_mouse_clear(void) {
    LOG_DBG("Mouse report cleared");
    memset(&mouse_report.body, 0, sizeof(mouse_report.body));
}
""",
    """void zmk_hid_mouse_clear(void) {
    LOG_DBG("Mouse report and explicit button state cleared");

    memset(&mouse_report.body, 0, sizeof(mouse_report.body));
    memset(explicit_button_counts, 0, sizeof(explicit_button_counts));
    explicit_buttons = 0;
}
""",
)


# ---------------------------------------------------------------------------
# zmk_endpoints_clear_current() previously sent keyboard+consumer neutral
# reports but never the neutral mouse report.
# ---------------------------------------------------------------------------

replace_once(
    "app/src/endpoints.c",
    """    zmk_endpoints_send_report(HID_USAGE_KEY);
    zmk_endpoints_send_report(HID_USAGE_CONSUMER);
}
""",
    """    zmk_endpoints_send_report(HID_USAGE_KEY);
    zmk_endpoints_send_report(HID_USAGE_CONSUMER);

#if IS_ENABLED(CONFIG_ZMK_POINTING)
    /*
     * A mouse-button release is stateful just like a keyboard release.
     * Send the cleared mouse state to the old endpoint before switching.
     */
    zmk_endpoints_send_mouse_report();
#endif
}
""",
)


# ---------------------------------------------------------------------------
# Inherited physical-layout compiler warning cleanup.
#
# The upstream macro contains TWO duplicated const qualifiers:
#
#   static const struct zmk_key_physical_attrs const ...
#   static const struct zmk_physical_layout const ...
#
# GCC reports the warning at the macro expansion line, so both declarations
# must be corrected.
# ---------------------------------------------------------------------------

replace_once(
    "app/src/physical_layouts.c",
    """    static const struct zmk_key_physical_attrs const _CONCAT(                                      \\
""",
    """    static const struct zmk_key_physical_attrs _CONCAT(                                            \\
""",
)

replace_once(
    "app/src/physical_layouts.c",
    """    static const struct zmk_physical_layout const _CONCAT(_zmk_physical_layout_,                   \\
""",
    """    static const struct zmk_physical_layout _CONCAT(_zmk_physical_layout_,                         \\
""",
)


physical_layouts = (
    ROOT / "app/src/physical_layouts.c"
).read_text()

for bad_duplicate_const in (
    "struct zmk_key_physical_attrs const _CONCAT(",
    "struct zmk_physical_layout const _CONCAT(",
):
    if bad_duplicate_const in physical_layouts:
        raise SystemExit(
            "ZMK physical-layout duplicate-const cleanup failed: "
            f"{bad_duplicate_const}"
        )


required = {
    "app/src/hid.c": (
        "explicit_modifier_counts, 0",
        "explicit_button_counts, 0",
        "Ignoring release of already-cleared mouse button",
    ),
    "app/src/endpoints.c": (
        "zmk_endpoints_send_mouse_report();",
    ),
    "app/module/drivers/kscan/kscan_gpio_direct.c": (
        "zephyr/pm/device_runtime.h",
    ),
}

for relpath, tokens in required.items():
    text = (ROOT / relpath).read_text()
    for token in tokens:
        if token not in text:
            raise SystemExit(
                f"ZMK reliability verification failed: {relpath}: {token}"
            )

subprocess.run(
    ["git", "-C", str(ROOT), "diff", "--check"],
    check=True,
)

print(f"Applied ZMK reliability patch to {EXPECTED_SHA}")
