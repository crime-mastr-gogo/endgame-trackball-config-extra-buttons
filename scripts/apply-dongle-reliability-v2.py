#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "4b8941e47b9dd87797e98335c150f7723bb2675d"

if len(sys.argv) != 2:
    raise SystemExit(
        "usage: apply-dongle-reliability-v2.py "
        "/path/to/endgame-trackball-firmware"
    )

ROOT = Path(sys.argv[1]).resolve()

if not ROOT.is_dir():
    raise SystemExit(f"Dongle firmware checkout not found: {ROOT}")

actual = subprocess.check_output(
    ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
    text=True,
).strip()

if actual != EXPECTED_SHA:
    raise SystemExit(
        f"Refusing to patch dongle source {actual}; "
        f"expected {EXPECTED_SHA}"
    )


def replace_once(pathname, old, new):
    path = ROOT / pathname
    text = path.read_text()

    if old in text:
        path.write_text(text.replace(old, new, 1))
        return

    if new in text:
        return

    raise SystemExit(
        f"Expected source block not found in {pathname}"
    )


###############################################################################
# USB host convergence primitive.
###############################################################################

header = ROOT / "dongle-1k-firmware/src/usb_hid.h"
text = header.read_text()

decl = """
/*
 * Drop pending stale reports and enqueue absolute neutral keyboard, consumer
 * and mouse states. The currently in-flight USB report, if any, may complete
 * first; the three neutral reports are guaranteed to follow it.
 */
int usb_hid_clear_all(void);
"""

if "int usb_hid_clear_all(void);" not in text:
    text += decl

header.write_text(text)


usb = ROOT / "dongle-1k-firmware/src/usb_hid.c"
text = usb.read_text()

clear_impl = r'''
int usb_hid_clear_all(void) {
    if (!m_configured) {
        return -ENOTCONN;
    }

    /*
     * Remove stale reports which have not reached the USB interrupt endpoint
     * yet. We intentionally do not try to cancel the one report which may
     * already be in flight; the neutral reports below are queued after it and
     * therefore become the host's final absolute state.
     */
    const k_spinlock_key_t key =
        k_spin_lock(&tx_lock);

    tx_head = 0;
    tx_tail = 0;

    k_spin_unlock(&tx_lock, key);

    static const uint8_t neutral_keyboard[8];
    static const uint8_t neutral_consumer[2];
    static const uint8_t neutral_mouse[9];

    int first_err = 0;

    int rc = usb_hid_send(
        REPORT_ID_KB,
        neutral_keyboard,
        sizeof(neutral_keyboard)
    );

    if (rc && first_err == 0) {
        first_err = rc;
    }

    rc = usb_hid_send(
        REPORT_ID_CONSUMER,
        neutral_consumer,
        sizeof(neutral_consumer)
    );

    if (rc && first_err == 0) {
        first_err = rc;
    }

    rc = usb_hid_send(
        REPORT_ID_MOUSE,
        neutral_mouse,
        sizeof(neutral_mouse)
    );

    if (rc && first_err == 0) {
        first_err = rc;
    }

    return first_err;
}
'''

if "int usb_hid_clear_all(void)" not in text:
    text += clear_impl

usb.write_text(text)


###############################################################################
# Indicator protocol v2: echo the request sequence number.
###############################################################################

replace_once(
    "dongle-1k-firmware/src/main.c",
    """        uint8_t indicators = 0;

        const bool valid =
            usb_hid_get_keyboard_leds(&indicators);
""",
    """        const struct esb_pkt_hid_indicator_req *req =
            (const void *)data;

        uint8_t indicators = 0;

        const bool valid =
            usb_hid_get_keyboard_leds(&indicators);
""",
)

replace_once(
    "dongle-1k-firmware/src/main.c",
    """        const struct esb_pkt_hid_indicators response = {
            .type = ESB_PKT_HID_INDICATORS,
            .indicators = indicators,
            .valid = valid ? 1 : 0,
        };
""",
    """        const struct esb_pkt_hid_indicators response = {
            .type = ESB_PKT_HID_INDICATORS,
            .seq = req->seq,
            .indicators = indicators,
            .valid = valid ? 1 : 0,
        };
""",
)


###############################################################################
# Explicit host neutralisation and held-state heartbeat receive handlers.
###############################################################################

main = ROOT / "dongle-1k-firmware/src/main.c"
text = main.read_text()

anchor = """    case ESB_PKT_DISCONNECT:
        LOG_INF("DISCONNECT from keyboard, forgetting peer (-> UNPAIRED)");
"""

cases = r'''    case ESB_PKT_HOST_NEUTRAL: {
        if (m_state != STATE_PAIRED) {
            break;
        }

        const int clear_err =
            usb_hid_clear_all();

        if (clear_err && clear_err != -ENOTCONN) {
            LOG_WRN(
                "Unable to neutralize USB HID host: %d",
                clear_err
            );
        }

        break;
    }

    case ESB_PKT_HELD_KEEPALIVE:
        /*
         * The RX preamble already treated every non-IDLE packet as active,
         * so receiving this packet rearms the long-loss watchdog. No further
         * payload action is required.
         */
        break;

'''

if "case ESB_PKT_HOST_NEUTRAL:" not in text:
    if anchor not in text:
        raise SystemExit(
            "Could not locate dongle DISCONNECT switch anchor"
        )

    text = text.replace(
        anchor,
        cases + anchor,
        1,
    )

main.write_text(text)


###############################################################################
# An explicit disconnect also converges the USB host before forgetting peer
# identity.
###############################################################################

replace_once(
    "dongle-1k-firmware/src/main.c",
    """    case ESB_PKT_DISCONNECT:
        LOG_INF("DISCONNECT from keyboard, forgetting peer (-> UNPAIRED)");
        save_paired(false);
""",
    """    case ESB_PKT_DISCONNECT:
        LOG_INF("DISCONNECT from keyboard, forgetting peer (-> UNPAIRED)");

        {
            const int clear_err =
                usb_hid_clear_all();

            if (clear_err && clear_err != -ENOTCONN) {
                LOG_WRN(
                    "USB HID clear during disconnect failed: %d",
                    clear_err
                );
            }
        }

        save_paired(false);
""",
)


###############################################################################
# Genuine prolonged radio loss: fail safe to neutral before the rollback
# recovery cycle. The short 141 ms speculative-hop timeout intentionally does
# NOT clear HID; this runs only on the 385 ms long-loss watchdog. A physically
# held input prevents peer-idle masking by sending HELD_KEEPALIVE every 60 ms.
###############################################################################

replace_once(
    "dongle-1k-firmware/src/channel_hop_dongle.c",
    """#include <zephyr/kernel.h>
#include <zmk_esb/protocol.h>
""",
    """#include <errno.h>
#include <zephyr/kernel.h>
#include <zmk_esb/protocol.h>
""",
)

replace_once(
    "dongle-1k-firmware/src/channel_hop_dongle.c",
    """#include "esb_prx.h"
#include "led_status.h"
""",
    """#include "esb_prx.h"
#include "led_status.h"
#include "usb_hid.h"
""",
)

replace_once(
    "dongle-1k-firmware/src/channel_hop_dongle.c",
    """static void rollback_silence_work_fn(struct k_work *w) {
    ARG_UNUSED(w);
    enter_rollback();
}
""",
    """static void rollback_silence_work_fn(struct k_work *w) {
    ARG_UNUSED(w);

    /*
     * This timer represents prolonged genuine loss, not the short
     * speculative-hop recovery window. If the peer disappears while the host
     * believes a key/button is held, fail safe to released state before radio
     * rollback/reacquisition begins.
     */
    if (m_paired && !m_peer_idle) {
        const int clear_err =
            usb_hid_clear_all();

        if (clear_err && clear_err != -ENOTCONN) {
            LOG_WRN(
                "USB HID fail-safe neutralization failed: %d",
                clear_err
            );
        }
    }

    enter_rollback();
}
""",
)


###############################################################################
# Verification.
###############################################################################

required = {
    "dongle-1k-firmware/src/usb_hid.h": (
        "usb_hid_clear_all",
    ),
    "dongle-1k-firmware/src/usb_hid.c": (
        "int usb_hid_clear_all(void)",
        "tx_head = 0",
        "neutral_keyboard[8]",
        "neutral_consumer[2]",
        "neutral_mouse[9]",
    ),
    "dongle-1k-firmware/src/main.c": (
        "const struct esb_pkt_hid_indicator_req *req",
        ".seq = req->seq",
        "case ESB_PKT_HOST_NEUTRAL:",
        "case ESB_PKT_HELD_KEEPALIVE:",
        "usb_hid_clear_all();",
    ),
    "dongle-1k-firmware/src/channel_hop_dongle.c": (
        "rollback_silence_work_fn",
        "USB HID fail-safe neutralization failed",
        "usb_hid_clear_all();",
    ),
}

for relpath, tokens in required.items():
    patched = (ROOT / relpath).read_text()

    for token in tokens:
        if token not in patched:
            raise SystemExit(
                f"Dongle reliability-v2 patch incomplete: "
                f"{relpath}: missing {token}"
            )

subprocess.run(
    [
        "git",
        "-C",
        str(ROOT),
        "diff",
        "--check",
        "--",
        "dongle-1k-firmware",
    ],
    check=True,
)

print(
    "Applied dongle reliability-v2 patch to upstream source "
    f"{EXPECTED_SHA}"
)
