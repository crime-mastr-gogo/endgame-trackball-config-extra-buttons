#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "89b695a9aca6dc5a7daf4488140ef46e19fc266c"

if len(sys.argv) != 2:
    raise SystemExit(
        "usage: apply-esb-keystring-indicators.py "
        "/path/to/zmk-esb-endpoint"
    )

ROOT = Path(sys.argv[1]).resolve()

if not ROOT.is_dir():
    raise SystemExit(
        f"ESB module not found: {ROOT}"
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
        f"Refusing to patch ESB revision {actual}; "
        f"expected {EXPECTED_SHA}"
    )


###############################################################################
# Protocol definitions
###############################################################################

protocol = ROOT / "include/zmk_esb/protocol.h"
text = protocol.read_text()

extension = r'''

/*
 * HID indicator bridge used by the Caps-Lock-independent custom string.
 *
 * REQ is sent by the trackball immediately before typing the string.
 * INDICATORS is returned by the USB dongle in an ACK payload.
 *
 * indicators uses the standard HID LED bitmap:
 *   bit 0 Num Lock
 *   bit 1 Caps Lock
 *   bit 2 Scroll Lock
 *   bit 3 Compose
 *   bit 4 Kana
 *
 * valid=0 means the dongle has not yet received an LED output report from
 * the USB host. In that case the trackball waits/retries instead of typing
 * a potentially incorrect mixed-case string.
 */
#define ESB_PKT_HID_INDICATOR_REQ 0x1B
#define ESB_PKT_HID_INDICATORS    0x1C

struct esb_pkt_hid_indicator_req {
    uint8_t type;
} __attribute__((__packed__));

struct esb_pkt_hid_indicators {
    uint8_t type;
    uint8_t indicators;
    uint8_t valid;
} __attribute__((__packed__));
'''

if "ESB_PKT_HID_INDICATOR_REQ" not in text:
    text += extension

protocol.write_text(text)


###############################################################################
# Public endpoint API
###############################################################################

header = ROOT / "include/zmk_esb/endpoint.h"
text = header.read_text()

if "#include <stdint.h>" not in text:
    text = text.replace(
        "#include <stdbool.h>\n",
        "#include <stdbool.h>\n#include <stdint.h>\n",
        1,
    )

api = r'''

/*
 * Request and read the host HID indicator state through the ESB dongle.
 *
 * get:
 *   0        valid response copied to *indicators
 *   -EAGAIN  response has not arrived yet
 *   -ENODATA dongle responded but the USB host has not supplied indicators
 */
int zmk_esb_endpoint_request_hid_indicators(void);
int zmk_esb_endpoint_get_hid_indicators(uint8_t *indicators);
'''

if "zmk_esb_endpoint_request_hid_indicators" not in text:
    text += api

header.write_text(text)


###############################################################################
# Endpoint implementation
###############################################################################

pairing = ROOT / "src/esb/pairing.c"
text = pairing.read_text()

if "#include <errno.h>" not in text:
    text = text.replace(
        "#include <zephyr/kernel.h>\n",
        "#include <errno.h>\n"
        "#include <zephyr/kernel.h>\n"
        "#include <zephyr/sys/atomic.h>\n",
        1,
    )


state_anchor = """static struct k_work_delayable beacon_work;
static struct k_work_delayable verify_work;
"""

state_new = """static struct k_work_delayable beacon_work;
static struct k_work_delayable verify_work;

/*
 * 0 = no response
 * 1 = response received but host indicator state unavailable
 * 2 = valid indicator state available
 */
static atomic_t hid_indicator_response_state;
static atomic_t hid_indicator_value;
"""

if "hid_indicator_response_state" not in text:
    if state_anchor not in text:
        raise SystemExit(
            "Could not locate ESB pairing state anchor"
        )

    text = text.replace(
        state_anchor,
        state_new,
        1,
    )


case_anchor = """    case ESB_PKT_LINK_STATS:
        esb_transport_on_rx_link_stats(data, len);
        break;
"""

case_new = """    case ESB_PKT_HID_INDICATORS:
        if (m_state == PAIRING_STATE_CONNECTED &&
            len >= sizeof(struct esb_pkt_hid_indicators)) {

            const struct esb_pkt_hid_indicators *pkt =
                (const void *)data;

            atomic_set(
                &hid_indicator_value,
                pkt->indicators
            );

            atomic_set(
                &hid_indicator_response_state,
                pkt->valid ? 2 : 1
            );
        }
        break;

    case ESB_PKT_LINK_STATS:
        esb_transport_on_rx_link_stats(data, len);
        break;
"""

if "case ESB_PKT_HID_INDICATORS:" not in text:
    if case_anchor not in text:
        raise SystemExit(
            "Could not locate ESB LINK_STATS switch anchor"
        )

    text = text.replace(
        case_anchor,
        case_new,
        1,
    )


api_impl = r'''

int zmk_esb_endpoint_request_hid_indicators(void) {
    if (!pairing_is_connected()) {
        return -ENOTCONN;
    }

    /*
     * Every request starts a fresh transaction. Never reuse a Caps Lock
     * response from a previous macro invocation or previous host state.
     */
    atomic_clear(&hid_indicator_response_state);

    const struct esb_pkt_hid_indicator_req req = {
        .type = ESB_PKT_HID_INDICATOR_REQ,
    };

    return esb_transport_send(
        ESB_PIPE_DATA,
        (const uint8_t *)&req,
        sizeof(req)
    );
}

int zmk_esb_endpoint_get_hid_indicators(uint8_t *indicators) {
    if (indicators == NULL) {
        return -EINVAL;
    }

    const atomic_val_t state =
        atomic_get(&hid_indicator_response_state);

    if (state == 0) {
        return -EAGAIN;
    }

    if (state == 1) {
        return -ENODATA;
    }

    *indicators =
        (uint8_t)atomic_get(&hid_indicator_value);

    return 0;
}
'''

if "int zmk_esb_endpoint_request_hid_indicators(void)" not in text:
    text += api_impl

pairing.write_text(text)


###############################################################################
# Assertions
###############################################################################

checks = {
    "include/zmk_esb/protocol.h": (
        "ESB_PKT_HID_INDICATOR_REQ",
        "ESB_PKT_HID_INDICATORS",
        "struct esb_pkt_hid_indicators",
    ),
    "include/zmk_esb/endpoint.h": (
        "zmk_esb_endpoint_request_hid_indicators",
        "zmk_esb_endpoint_get_hid_indicators",
    ),
    "src/esb/pairing.c": (
        "hid_indicator_response_state",
        "case ESB_PKT_HID_INDICATORS:",
        "atomic_clear(&hid_indicator_response_state)",
    ),
}

for relpath, tokens in checks.items():
    content = (ROOT / relpath).read_text()

    for token in tokens:
        if token not in content:
            raise SystemExit(
                f"ESB indicator patch incomplete: "
                f"{relpath}: missing {token}"
            )

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
    "Applied ESB HID-indicator bridge to "
    f"{EXPECTED_SHA}"
)
