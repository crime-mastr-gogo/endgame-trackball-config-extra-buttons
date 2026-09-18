#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "4b8941e47b9dd87797e98335c150f7723bb2675d"

if len(sys.argv) != 2:
    raise SystemExit(
        "usage: apply-dongle-keystring-indicators.py "
        "/path/to/endgame-trackball-firmware"
    )

ROOT = Path(sys.argv[1]).resolve()

if not ROOT.is_dir():
    raise SystemExit(
        f"Dongle firmware checkout not found: {ROOT}"
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
        f"Refusing to patch dongle source {actual}; "
        f"expected {EXPECTED_SHA}"
    )


def replace_once(pathname, old, new):
    path = ROOT / pathname
    text = path.read_text()

    if old in text:
        path.write_text(
            text.replace(old, new, 1)
        )
        return

    if new in text:
        return

    raise SystemExit(
        f"Expected source block not found in {pathname}"
    )


###############################################################################
# Enable interrupt OUT endpoint as well as control SET_REPORT handling.
###############################################################################

prj = ROOT / "dongle-1k-firmware/prj.conf"
text = prj.read_text()

if "CONFIG_ENABLE_HID_INT_OUT_EP=y" not in text:
    text += "\nCONFIG_ENABLE_HID_INT_OUT_EP=y\n"

prj.write_text(text)


###############################################################################
# Public getter for current keyboard LED state.
###############################################################################

header = ROOT / "dongle-1k-firmware/src/usb_hid.h"
text = header.read_text()

if "#include <stdbool.h>" not in text:
    text = text.replace(
        "#include <stdint.h>\n",
        "#include <stdbool.h>\n#include <stdint.h>\n",
        1,
    )

decl = """
bool usb_hid_get_keyboard_leds(uint8_t *leds);
"""

if "usb_hid_get_keyboard_leds" not in text:
    text += decl

header.write_text(text)


###############################################################################
# USB HID descriptor + output-report handling
###############################################################################

usb = ROOT / "dongle-1k-firmware/src/usb_hid.c"
text = usb.read_text()


text = text.replace(
    """#define HID_REPORT_TYPE_FEATURE  0x300
""",
    """#define HID_REPORT_TYPE_OUTPUT   0x200
#define HID_REPORT_TYPE_FEATURE  0x300
""",
    1,
)


descriptor_old = """        /* Reserved byte */
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(1),
        HID_INPUT(0x01),    /* Const */
        /* Keycodes: 6 bytes */
        HID_USAGE_MIN8(0x00),
"""

descriptor_new = """        /* Reserved byte */
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(1),
        HID_INPUT(0x01),    /* Const */

        /*
         * Host -> keyboard LED output report.
         * Standard five HID indicators:
         * Num, Caps, Scroll, Compose, Kana.
         */
        HID_USAGE_PAGE(HID_USAGE_GEN_LEDS),
        HID_USAGE_MIN8(1),
        HID_USAGE_MAX8(5),
        HID_LOGICAL_MIN8(0),
        HID_LOGICAL_MAX8(1),
        HID_REPORT_SIZE(1),
        HID_REPORT_COUNT(5),
        HID_OUTPUT(0x02),
        HID_REPORT_SIZE(3),
        HID_REPORT_COUNT(1),
        HID_OUTPUT(0x03),

        /* Restore keyboard usage page for key array. */
        HID_USAGE_PAGE(HID_USAGE_GEN_KEYBOARD),

        /* Keycodes: 6 bytes */
        HID_USAGE_MIN8(0x00),
"""

if descriptor_new not in text:
    if descriptor_old not in text:
        raise SystemExit(
            "Could not find keyboard descriptor insertion point"
        )

    text = text.replace(
        descriptor_old,
        descriptor_new,
        1,
    )


state_old = """static bool ep_busy;
static struct k_spinlock tx_lock;
"""

state_new = """static bool ep_busy;
static struct k_spinlock tx_lock;

/* Host keyboard LED output state. */
static struct k_spinlock keyboard_led_lock;
static uint8_t keyboard_leds;
static bool keyboard_leds_valid;
"""

if state_new not in text:
    if state_old not in text:
        raise SystemExit(
            "Could not locate dongle USB state block"
        )

    text = text.replace(
        state_old,
        state_new,
        1,
    )


helper_marker = """static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup,
"""

helper = r'''static void keyboard_leds_update(const uint8_t leds) {
    const k_spinlock_key_t key =
        k_spin_lock(&keyboard_led_lock);

    keyboard_leds = leds & 0x1F;
    keyboard_leds_valid = true;

    k_spin_unlock(&keyboard_led_lock, key);

    LOG_DBG(
        "Keyboard LEDs: num=%u caps=%u scroll=%u",
        !!(leds & HID_KBD_LED_NUM_LOCK),
        !!(leds & HID_KBD_LED_CAPS_LOCK),
        !!(leds & HID_KBD_LED_SCROLL_LOCK)
    );
}

static void keyboard_leds_invalidate(void) {
    const k_spinlock_key_t key =
        k_spin_lock(&keyboard_led_lock);

    keyboard_leds = 0;
    keyboard_leds_valid = false;

    k_spin_unlock(&keyboard_led_lock, key);
}

bool usb_hid_get_keyboard_leds(uint8_t *leds) {
    if (leds == NULL) {
        return false;
    }

    const k_spinlock_key_t key =
        k_spin_lock(&keyboard_led_lock);

    const bool valid =
        m_configured && keyboard_leds_valid;

    if (valid) {
        *leds = keyboard_leds;
    }

    k_spin_unlock(&keyboard_led_lock, key);

    return valid;
}

'''

if "static void keyboard_leds_update" not in text:
    if helper_marker not in text:
        raise SystemExit(
            "Could not locate get_report_cb insertion point"
        )

    text = text.replace(
        helper_marker,
        helper + helper_marker,
        1,
    )


set_old = r'''static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup,
                         int32_t *len, uint8_t **data) {
    ARG_UNUSED(dev);
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_FEATURE) {
        return -ENOTSUP;
    }
    if ((setup->wValue & HID_GET_REPORT_ID_MASK) != REPORT_ID_MOUSE) {
        return -ENOTSUP;
    }
    /* Host may or may not include the report_id prefix. Accept either. */
    if (*len == sizeof(res_feature_report) && (*data)[0] == REPORT_ID_MOUSE) {
        res_feature_report[1] = (*data)[1];
    } else if (*len == 1) {
        res_feature_report[1] = (*data)[0];
    } else {
        return -EINVAL;
    }
    LOG_INF("SET_REPORT feature mouse res-mult=0x%02X", res_feature_report[1]);
    return 0;
}
'''

set_new = r'''static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup,
                         int32_t *len, uint8_t **data) {
    ARG_UNUSED(dev);

    const uint16_t report_type =
        setup->wValue & HID_GET_REPORT_TYPE_MASK;

    const uint8_t report_id =
        setup->wValue & HID_GET_REPORT_ID_MASK;

    /*
     * Keyboard LED output report. Windows may provide either the one-byte
     * body or [report_id, body], so accept both forms.
     */
    if (report_type == HID_REPORT_TYPE_OUTPUT &&
        report_id == REPORT_ID_KB) {

        if (*len == 2 && (*data)[0] == REPORT_ID_KB) {
            keyboard_leds_update((*data)[1]);
            return 0;
        }

        if (*len == 1) {
            keyboard_leds_update((*data)[0]);
            return 0;
        }

        return -EINVAL;
    }

    if (report_type != HID_REPORT_TYPE_FEATURE) {
        return -ENOTSUP;
    }

    if (report_id != REPORT_ID_MOUSE) {
        return -ENOTSUP;
    }

    /* Host may or may not include the report_id prefix. Accept either. */
    if (*len == sizeof(res_feature_report) &&
        (*data)[0] == REPORT_ID_MOUSE) {

        res_feature_report[1] = (*data)[1];

    } else if (*len == 1) {

        res_feature_report[1] = (*data)[0];

    } else {
        return -EINVAL;
    }

    LOG_INF(
        "SET_REPORT feature mouse res-mult=0x%02X",
        res_feature_report[1]
    );

    return 0;
}
'''

if set_new not in text:
    if set_old not in text:
        raise SystemExit(
            "Could not find original dongle set_report_cb"
        )

    text = text.replace(
        set_old,
        set_new,
        1,
    )


ops_old = """static const struct hid_ops ops = {
    .get_report   = get_report_cb,
    .set_report   = set_report_cb,
    .int_in_ready = int_in_ready_cb,
};
"""

ops_new = r'''#if defined(CONFIG_ENABLE_HID_INT_OUT_EP)
static void int_out_ready_cb(const struct device *dev) {
    uint8_t buf[8];
    uint32_t read_len = 0;

    const int err = hid_int_ep_read(
        dev,
        buf,
        sizeof(buf),
        &read_len
    );

    if (err) {
        LOG_WRN("hid_int_ep_read: %d", err);
        return;
    }

    if (read_len == 2 && buf[0] == REPORT_ID_KB) {
        keyboard_leds_update(buf[1]);
        return;
    }

    if (read_len == 1) {
        keyboard_leds_update(buf[0]);
        return;
    }

    if (read_len != 0) {
        LOG_WRN(
            "Unexpected keyboard LED OUT report length: %u",
            (unsigned)read_len
        );
    }
}
#endif

static const struct hid_ops ops = {
    .get_report   = get_report_cb,
    .set_report   = set_report_cb,
    .int_in_ready = int_in_ready_cb,
#if defined(CONFIG_ENABLE_HID_INT_OUT_EP)
    .int_out_ready = int_out_ready_cb,
#endif
};
'''

if ops_new not in text:
    if ops_old not in text:
        raise SystemExit(
            "Could not find dongle HID ops table"
        )

    text = text.replace(
        ops_old,
        ops_new,
        1,
    )


# Reset validity at USB lifecycle boundaries.
text = text.replace(
    """    case USB_DC_RESET:
        if (m_configured) {
            LOG_INF("USB reset");
        }
        m_configured = false;
        break;
""",
    """    case USB_DC_RESET:
        if (m_configured) {
            LOG_INF("USB reset");
        }
        m_configured = false;
        keyboard_leds_invalidate();
        break;
""",
    1,
)

text = text.replace(
    """    case USB_DC_CONFIGURED:
        if (!m_configured) {
            const k_spinlock_key_t key = k_spin_lock(&tx_lock);
""",
    """    case USB_DC_CONFIGURED:
        if (!m_configured) {
            keyboard_leds_invalidate();

            const k_spinlock_key_t key = k_spin_lock(&tx_lock);
""",
    1,
)

text = text.replace(
    """    case USB_DC_DISCONNECTED:
        LOG_INF("USB disconnected");
        m_configured = false;
        break;
""",
    """    case USB_DC_DISCONNECTED:
        LOG_INF("USB disconnected");
        m_configured = false;
        keyboard_leds_invalidate();
        break;
""",
    1,
)

usb.write_text(text)


###############################################################################
# ESB query handling
###############################################################################

main = ROOT / "dongle-1k-firmware/src/main.c"
text = main.read_text()

anchor = """    case ESB_PKT_DISCONNECT:
        LOG_INF("DISCONNECT from keyboard, forgetting peer (-> UNPAIRED)");
"""

case = r'''    case ESB_PKT_HID_INDICATOR_REQ: {
        if (m_state != STATE_PAIRED ||
            len < sizeof(struct esb_pkt_hid_indicator_req)) {
            break;
        }

        uint8_t indicators = 0;

        const bool valid =
            usb_hid_get_keyboard_leds(&indicators);

        const struct esb_pkt_hid_indicators response = {
            .type = ESB_PKT_HID_INDICATORS,
            .indicators = indicators,
            .valid = valid ? 1 : 0,
        };

        const int ack_err = esb_prx_queue_ack(
            ESB_PIPE_DATA,
            (const uint8_t *)&response,
            sizeof(response)
        );

        if (ack_err) {
            LOG_WRN(
                "Unable to queue HID indicator response: %d",
                ack_err
            );
        }

        break;
    }

'''

if "case ESB_PKT_HID_INDICATOR_REQ:" not in text:
    if anchor not in text:
        raise SystemExit(
            "Could not locate dongle DISCONNECT switch anchor"
        )

    text = text.replace(
        anchor,
        case + anchor,
        1,
    )

main.write_text(text)


###############################################################################
# Validation
###############################################################################

checks = {
    "dongle-1k-firmware/prj.conf": (
        "CONFIG_ENABLE_HID_INT_OUT_EP=y",
    ),
    "dongle-1k-firmware/src/usb_hid.c": (
        "HID_REPORT_TYPE_OUTPUT",
        "HID_USAGE_GEN_LEDS",
        "keyboard_leds_update",
        "int_out_ready_cb",
        "HID_KBD_LED_CAPS_LOCK",
    ),
    "dongle-1k-firmware/src/usb_hid.h": (
        "usb_hid_get_keyboard_leds",
    ),
    "dongle-1k-firmware/src/main.c": (
        "case ESB_PKT_HID_INDICATOR_REQ:",
        "ESB_PKT_HID_INDICATORS",
    ),
}

for relpath, tokens in checks.items():
    content = (ROOT / relpath).read_text()

    for token in tokens:
        if token not in content:
            raise SystemExit(
                f"Dongle patch incomplete: "
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
    "Applied Caps Lock HID-indicator relay to upstream "
    f"dongle source {EXPECTED_SHA}"
)
