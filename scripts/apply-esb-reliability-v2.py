#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "89b695a9aca6dc5a7daf4488140ef46e19fc266c"

if len(sys.argv) != 2:
    raise SystemExit(
        "usage: apply-esb-reliability-v2.py /path/to/zmk-esb-endpoint"
    )

ROOT = Path(sys.argv[1]).resolve()

if not ROOT.is_dir():
    raise SystemExit(f"ESB module not found: {ROOT}")

actual = subprocess.check_output(
    ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
    text=True,
).strip()

if actual != EXPECTED_SHA:
    raise SystemExit(
        f"Refusing to patch ESB revision {actual}; expected {EXPECTED_SHA}"
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
        "refusing partial ESB reliability-v2 patch."
    )


###############################################################################
# Protocol v2:
# - sequence-numbered host HID-indicator query/response
# - explicit receiver-host neutralisation command
# - liveness packet while any ESB-local HID state remains held
###############################################################################

replace_once(
    "include/zmk_esb/protocol.h",
    """struct esb_pkt_hid_indicator_req {
    uint8_t type;
} __attribute__((__packed__));

struct esb_pkt_hid_indicators {
    uint8_t type;
    uint8_t indicators;
    uint8_t valid;
} __attribute__((__packed__));
""",
    """struct esb_pkt_hid_indicator_req {
    uint8_t type;
    uint8_t seq;
} __attribute__((__packed__));

struct esb_pkt_hid_indicators {
    uint8_t type;
    uint8_t seq;
    uint8_t indicators;
    uint8_t valid;
} __attribute__((__packed__));

/*
 * Reliability-v2 control packets.
 *
 * HOST_NEUTRAL tells the paired receiver to converge USB keyboard, consumer
 * and mouse state to "everything released" before the endpoint leaves ESB.
 *
 * HELD_KEEPALIVE is emitted only while the endpoint has non-neutral
 * keyboard/consumer/mouse-button state. It prevents the receiver from
 * classifying a physically-held input as ordinary idle radio silence.
 */
#define ESB_PKT_HOST_NEUTRAL      0x1D
#define ESB_PKT_HELD_KEEPALIVE    0x1E

struct esb_pkt_host_neutral {
    uint8_t type;
} __attribute__((__packed__));

struct esb_pkt_held_keepalive {
    uint8_t type;
} __attribute__((__packed__));
""",
)


###############################################################################
# Public endpoint API.
###############################################################################

replace_once(
    "include/zmk_esb/endpoint.h",
    """int zmk_esb_endpoint_request_hid_indicators(void);
int zmk_esb_endpoint_get_hid_indicators(uint8_t *indicators);
""",
    """int zmk_esb_endpoint_request_hid_indicators(uint8_t seq);
int zmk_esb_endpoint_get_hid_indicators(uint8_t seq, uint8_t *indicators);

/*
 * Reset ESB-local HID state, discard queued pre-neutral reports, and wait for
 * the paired receiver to ACK an explicit host-neutral command.
 */
int zmk_esb_endpoint_neutralize_host(void);
""",
)


###############################################################################
# Keystring indicator responses accept only the current transaction.
###############################################################################

replace_once(
    "src/esb/pairing.c",
    """static atomic_t hid_indicator_response_state;
static atomic_t hid_indicator_value;
""",
    """static atomic_t hid_indicator_response_state;
static atomic_t hid_indicator_value;
static atomic_t hid_indicator_expected_seq;
""",
)

replace_once(
    "src/esb/pairing.c",
    """            atomic_set(
                &hid_indicator_value,
                pkt->indicators
            );

            atomic_set(
                &hid_indicator_response_state,
                pkt->valid ? 2 : 1
            );
""",
    """            const uint8_t expected =
                (uint8_t)atomic_get(
                    &hid_indicator_expected_seq
                );

            /*
             * ACK payloads are asynchronous. Ignore a response left in the
             * receiver FIFO by an older macro invocation.
             */
            if (pkt->seq != expected) {
                break;
            }

            atomic_set(
                &hid_indicator_value,
                pkt->indicators
            );

            atomic_set(
                &hid_indicator_response_state,
                pkt->valid ? 2 : 1
            );
""",
)

replace_once(
    "src/esb/pairing.c",
    """int zmk_esb_endpoint_request_hid_indicators(void) {
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
""",
    """int zmk_esb_endpoint_request_hid_indicators(uint8_t seq) {
    if (!pairing_is_connected()) {
        return -ENOTCONN;
    }

    /*
     * Every request starts a fresh transaction. A response is accepted only
     * when its echoed sequence matches this value.
     */
    atomic_set(&hid_indicator_expected_seq, seq);
    atomic_clear(&hid_indicator_response_state);

    const struct esb_pkt_hid_indicator_req req = {
        .type = ESB_PKT_HID_INDICATOR_REQ,
        .seq = seq,
    };

    return esb_transport_send(
        ESB_PIPE_DATA,
        (const uint8_t *)&req,
        sizeof(req)
    );
}

int zmk_esb_endpoint_get_hid_indicators(
    uint8_t seq,
    uint8_t *indicators
) {
    if (indicators == NULL) {
        return -EINVAL;
    }

    if ((uint8_t)atomic_get(
            &hid_indicator_expected_seq
        ) != seq) {
        return -EAGAIN;
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
""",
)


###############################################################################
# Expose whether the ESB-local keyboard/consumer state is neutral.
###############################################################################

relay = ROOT / "src/esb/hid_relay.c"
relay_text = relay.read_text()

relay_marker = """void zmk_esb_hid_relay_sync_neutral(void) {
    if (!zmk_esb_endpoint_is_active() || !pairing_is_connected()) {
        return;
    }

    send_keyboard();
    send_consumer();
}

"""

relay_insert = relay_marker + """bool zmk_esb_hid_relay_is_neutral(void) {
    static const struct zmk_hid_keyboard_report_body neutral_kb;
    static const struct zmk_hid_consumer_report_body neutral_cons;

    bool neutral;

    k_sched_lock();

    neutral =
        memcmp(&kb_body, &neutral_kb, sizeof(kb_body)) == 0 &&
        memcmp(&cons_body, &neutral_cons, sizeof(cons_body)) == 0 &&
        explicit_mods == 0 &&
        implicit_mods == 0;

    k_sched_unlock();

    return neutral;
}

"""

if "bool zmk_esb_hid_relay_is_neutral(void)" not in relay_text:
    if relay_marker not in relay_text:
        raise SystemExit(
            "Could not locate ESB relay neutral-sync insertion point"
        )

    relay.write_text(
        relay_text.replace(
            relay_marker,
            relay_insert,
            1,
        )
    )


###############################################################################
# Expose whether every ESB mouse/input-processor instance is neutral.
###############################################################################

ip = ROOT / "src/esb/input_processor_esb.c"
ip_text = ip.read_text()

ip_marker = """void zmk_esb_input_sync_neutral_all(void) {
    DT_INST_FOREACH_STATUS_OKAY(ESB_IP_SYNC_INSTANCE)
}

#undef ESB_IP_SYNC_INSTANCE
"""

ip_insert = """void zmk_esb_input_sync_neutral_all(void) {
    DT_INST_FOREACH_STATUS_OKAY(ESB_IP_SYNC_INSTANCE)
}

#undef ESB_IP_SYNC_INSTANCE

static bool esb_ip_instance_is_neutral(
    struct esb_ip_data *d
) {
    bool neutral;

    k_sched_lock();
    neutral = d->buttons == 0;
    k_sched_unlock();

    return neutral;
}

bool zmk_esb_input_all_neutral(void) {
    bool neutral = true;

#define ESB_IP_CHECK_NEUTRAL(n) \
    do { \
        if (!esb_ip_instance_is_neutral(&esb_ip_data_##n)) { \
            neutral = false; \
        } \
    } while (0);

    DT_INST_FOREACH_STATUS_OKAY(ESB_IP_CHECK_NEUTRAL)

#undef ESB_IP_CHECK_NEUTRAL

    return neutral;
}
"""

if "bool zmk_esb_input_all_neutral(void)" not in ip_text:
    if ip_marker not in ip_text:
        raise SystemExit(
            "Could not locate ESB input neutral-sync insertion point"
        )

    ip.write_text(
        ip_text.replace(
            ip_marker,
            ip_insert,
            1,
        )
    )


###############################################################################
# Pairing owns the complete ESB-local HID epoch. Give endpoint/channel-hop
# code a single reset + neutral-state API.
###############################################################################

replace_once(
    "src/esb/pairing.c",
    """void zmk_esb_hid_relay_reset_state(void);
void zmk_esb_hid_relay_sync_neutral(void);

void zmk_esb_input_reset_all(void);
void zmk_esb_input_sync_neutral_all(void);
""",
    """void zmk_esb_hid_relay_reset_state(void);
void zmk_esb_hid_relay_sync_neutral(void);
bool zmk_esb_hid_relay_is_neutral(void);

void zmk_esb_input_reset_all(void);
void zmk_esb_input_sync_neutral_all(void);
bool zmk_esb_input_all_neutral(void);
""",
)

pairing = ROOT / "src/esb/pairing.c"
pairing_text = pairing.read_text()

pairing_marker = """static void esb_hid_lifecycle_sync_neutral(void) {
    zmk_esb_hid_relay_sync_neutral();
    zmk_esb_input_sync_neutral_all();
}
"""

pairing_insert = pairing_marker + """
void pairing_reset_hid_state(void) {
    esb_hid_lifecycle_reset();
}

bool pairing_hid_state_is_neutral(void) {
    return
        zmk_esb_hid_relay_is_neutral() &&
        zmk_esb_input_all_neutral();
}
"""

if "bool pairing_hid_state_is_neutral(void)" not in pairing_text:
    if pairing_marker not in pairing_text:
        raise SystemExit(
            "Could not locate ESB pairing HID lifecycle helper"
        )

    pairing.write_text(
        pairing_text.replace(
            pairing_marker,
            pairing_insert,
            1,
        )
    )

pairing_h = ROOT / "src/esb/pairing.h"
pairing_h_text = pairing_h.read_text()

pairing_api = """
/* Reliability-v2 HID lifecycle helpers. */
void pairing_reset_hid_state(void);
bool pairing_hid_state_is_neutral(void);
"""

if "pairing_reset_hid_state" not in pairing_h_text:
    pairing_h.write_text(
        pairing_h_text + pairing_api
    )


###############################################################################
# A physically held key/button is not radio-idle. Keep the receiver's long-loss
# watchdog armed with a tiny liveness packet until all local HID state is
# neutral. Only then may the normal IDLE handshake disarm that watchdog.
###############################################################################

replace_once(
    "src/esb/channel_hop_ep.c",
    """#include "channel_hop_ep.h"
#include "esb_transport.h"
#include <zephyr/settings/settings.h>
""",
    """#include "channel_hop_ep.h"
#include "esb_transport.h"
#include "pairing.h"
#include <zephyr/settings/settings.h>
""",
)

replace_once(
    "src/esb/channel_hop_ep.c",
    """static void idle_check_work_fn(struct k_work *w) {
    ARG_UNUSED(w);
    /* Scheduled only while active; arrival here means IDLE_THRESHOLD_MS
     * have elapsed with no user TX touching the activity timestamp. */
    if (!m_link_up || !m_active) {
        return;
    }
    enter_idle_state();
}
""",
    """#define HELD_HID_KEEPALIVE_MS 60

static void idle_check_work_fn(struct k_work *w) {
    ARG_UNUSED(w);

    if (!m_link_up || !m_active) {
        return;
    }

    /*
     * A held key/mouse button can remain unchanged for seconds, but the
     * receiver must not interpret that quiet period as permission to disarm
     * its link-loss watchdog. Keep the radio epoch explicitly alive until
     * the absolute HID state becomes neutral.
     */
    if (!pairing_hid_state_is_neutral()) {
        const struct esb_pkt_held_keepalive pkt = {
            .type = ESB_PKT_HELD_KEEPALIVE,
        };

        const int err = esb_transport_send(
            ESB_PIPE_DATA,
            (const uint8_t *)&pkt,
            sizeof(pkt)
        );

        if (err && err != -EAGAIN) {
            LOG_DBG("held-HID keepalive send failed: %d", err);
        }

        k_work_reschedule(
            &idle_check_work,
            K_MSEC(HELD_HID_KEEPALIVE_MS)
        );

        return;
    }

    enter_idle_state();
}
""",
)


###############################################################################
# Treat neutralisation and held-state liveness as reliability control packets
# for the ordinary non-blocking path.
###############################################################################

replace_once(
    "src/esb/esb_transport.c",
    """        data[0] == ESB_PKT_CHANNEL_HOP_PROPOSAL ||
        data[0] == ESB_PKT_HOP_OFFER            ||
        data[0] == ESB_PKT_IDLE);
""",
    """        data[0] == ESB_PKT_CHANNEL_HOP_PROPOSAL ||
        data[0] == ESB_PKT_HOP_OFFER            ||
        data[0] == ESB_PKT_IDLE                 ||
        data[0] == ESB_PKT_HOST_NEUTRAL         ||
        data[0] == ESB_PKT_HELD_KEEPALIVE);
""",
)


###############################################################################
# Explicit host-neutral API. Reset local state before flushing so a flush-
# generated resync can only reproduce neutral state. Then wait for the receiver
# ACK before profile teardown / USB takeover proceeds.
###############################################################################

replace_once(
    "src/esb/esb_endpoint.c",
    """#include <zephyr/kernel.h>
#include <zephyr/device.h>
""",
    """#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
""",
)

replace_once(
    "src/esb/esb_endpoint.c",
    """#include "esb_transport.h"
#include "pairing.h"
""",
    """#include "esb_transport.h"
#include "pairing.h"
#include <zmk_esb/protocol.h>
""",
)

endpoint = ROOT / "src/esb/esb_endpoint.c"
endpoint_text = endpoint.read_text()

endpoint_marker = """bool zmk_esb_endpoint_is_active(void) {
    return esb_active;
}

"""

endpoint_insert = endpoint_marker + """int zmk_esb_endpoint_neutralize_host(void) {
    if (!esb_active || !pairing_is_connected()) {
        return -ENOTCONN;
    }

    /*
     * Order is safety-critical:
     *   1. local absolute HID becomes neutral,
     *   2. stale queued reports from the old epoch are discarded,
     *   3. receiver ACKs an explicit host-neutral command.
     */
    pairing_reset_hid_state();
    esb_transport_flush_tx();

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_RENDEZVOUS)
    const struct esb_pkt_host_neutral pkt = {
        .type = ESB_PKT_HOST_NEUTRAL,
    };

    return esb_transport_send_blocking(
        esb_transport_get_channel(),
        ESB_PIPE_DATA,
        (const uint8_t *)&pkt,
        sizeof(pkt),
        K_MSEC(
            CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP_RENDEZVOUS_TIMEOUT_MS
        )
    );
#else
    return -ENOTSUP;
#endif
}

"""

if "int zmk_esb_endpoint_neutralize_host(void)" not in endpoint_text:
    if endpoint_marker not in endpoint_text:
        raise SystemExit(
            "Could not locate ESB endpoint active-state helper"
        )

    endpoint.write_text(
        endpoint_text.replace(
            endpoint_marker,
            endpoint_insert,
            1,
        )
    )

replace_once(
    "src/esb/esb_endpoint.c",
    """        } else if (cmd == ESB_CMD_DEACTIVATE) {
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
            esb_shell_relay_on_deactivate();
#endif
            esb_active = false;
""",
    """        } else if (cmd == ESB_CMD_DEACTIVATE) {
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
            esb_shell_relay_on_deactivate();
#endif

            const int neutral_rc =
                zmk_esb_endpoint_neutralize_host();

            if (neutral_rc &&
                neutral_rc != -ENOTCONN) {
                LOG_WRN(
                    "Receiver host neutralization before ESB deactivate "
                    "failed: %d",
                    neutral_rc
                );
            }

            esb_active = false;
""",
)


###############################################################################
# Verification.
###############################################################################

required = {
    "include/zmk_esb/protocol.h": (
        "uint8_t seq;",
        "ESB_PKT_HOST_NEUTRAL",
        "ESB_PKT_HELD_KEEPALIVE",
    ),
    "include/zmk_esb/endpoint.h": (
        "zmk_esb_endpoint_request_hid_indicators(uint8_t seq)",
        "zmk_esb_endpoint_neutralize_host",
    ),
    "src/esb/pairing.c": (
        "hid_indicator_expected_seq",
        "pkt->seq != expected",
        "pairing_reset_hid_state",
        "pairing_hid_state_is_neutral",
    ),
    "src/esb/channel_hop_ep.c": (
        "HELD_HID_KEEPALIVE_MS 60",
        "ESB_PKT_HELD_KEEPALIVE",
        "pairing_hid_state_is_neutral",
    ),
    "src/esb/esb_endpoint.c": (
        "pairing_reset_hid_state();",
        "esb_transport_flush_tx();",
        "ESB_PKT_HOST_NEUTRAL",
        "esb_transport_send_blocking",
    ),
}

for relpath, tokens in required.items():
    patched = (ROOT / relpath).read_text()

    for token in tokens:
        if token not in patched:
            raise SystemExit(
                f"ESB reliability-v2 patch incomplete: "
                f"{relpath}: missing {token}"
            )

# Assert the host-neutral ordering in the endpoint implementation.
endpoint_text = (ROOT / "src/esb/esb_endpoint.c").read_text()
reset_pos = endpoint_text.index("pairing_reset_hid_state();")
flush_pos = endpoint_text.index("esb_transport_flush_tx();", reset_pos)
send_pos = endpoint_text.index("esb_transport_send_blocking(", flush_pos)

if not (reset_pos < flush_pos < send_pos):
    raise SystemExit(
        "ESB host-neutral ordering contract violated"
    )

subprocess.run(
    ["git", "-C", str(ROOT), "diff", "--check"],
    check=True,
)

print(
    "Applied ESB reliability-v2 patch to "
    f"{EXPECTED_SHA}"
)
