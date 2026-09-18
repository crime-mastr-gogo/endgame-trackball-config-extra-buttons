#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

EXPECTED_SHA = "89b695a9aca6dc5a7daf4488140ef46e19fc266c"

if len(sys.argv) != 2:
    raise SystemExit(
        "usage: apply-esb-lifecycle-fix.py /path/to/zmk-esb-endpoint"
    )

ROOT = Path(sys.argv[1]).resolve()

if not ROOT.is_dir():
    raise SystemExit(f"ESB module not found: {ROOT}")

actual_sha = subprocess.check_output(
    ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
    text=True,
).strip()

if actual_sha != EXPECTED_SHA:
    raise SystemExit(
        f"Refusing to patch unexpected ESB revision {actual_sha}; "
        f"expected {EXPECTED_SHA}"
    )


def replace_once(relpath: str, old: str, new: str) -> None:
    path = ROOT / relpath
    text = path.read_text()

    if new in text:
        return

    if old not in text:
        raise SystemExit(
            f"Expected source block not found in {relpath}; "
            "refusing an unsafe partial patch."
        )

    path.write_text(text.replace(old, new, 1))


###############################################################################
# 1. ESB KEYBOARD / CONSUMER LOCAL HID STATE
###############################################################################

replace_once(
    "src/esb/hid_relay.c",
    """static struct k_work_delayable hid_retry_work;
""",
    """static struct k_work_delayable hid_retry_work;
static bool relay_was_active;
""",
)

marker = """/* Transport delivery-failure resync (fix for retry-exhausted / flushed
"""

insert = r"""/*
 * Clear every ESB-local keyboard/consumer state component.
 *
 * A receiver loss/reboot defines a new HID delivery epoch. State that
 * existed before the break must never be resurrected after VERIFY.
 *
 * Cancel the retry worker first. State is then mutated under the same
 * scheduler lock already used by the ESB HID event/retry paths.
 */
void zmk_esb_hid_relay_reset_state(void) {
    (void)k_work_cancel_delayable(&hid_retry_work);

    k_sched_lock();

    memset(&kb_body, 0, sizeof(kb_body));
    memset(&cons_body, 0, sizeof(cons_body));

    memset(explicit_mod_refcount, 0, sizeof(explicit_mod_refcount));
    memset(implicit_mod_refcount, 0, sizeof(implicit_mod_refcount));

    explicit_mods = 0;
    implicit_mods = 0;

    memset(kb_pending, 0, sizeof(kb_pending));
    memset(cons_pending, 0, sizeof(cons_pending));

    kb_pending_count = 0;
    cons_pending_count = 0;
    relay_was_active = false;

    k_sched_unlock();
}

/*
 * After successful pairing / VERIFY, explicitly transmit neutral
 * keyboard and consumer reports.
 *
 * This ensures a host which retained stale HID state through receiver
 * recovery converges back to "everything released" before new input.
 */
void zmk_esb_hid_relay_sync_neutral(void) {
    if (!zmk_esb_endpoint_is_active() || !pairing_is_connected()) {
        return;
    }

    send_keyboard();
    send_consumer();
}

"""

replace_once(
    "src/esb/hid_relay.c",
    marker,
    insert + marker,
)

old_falling = r"""    /* Falling-edge cleanup: when ESB deactivates mid-quiet, leftover
     * snapshots would later drain into a dead transport and produce
     * harmless but noisy LOG_WRN spam. Catch the edge here, cancel the
     * pending work, and clear the queues. The next reactivation starts
     * fresh. */
    static bool was_active;
    if (was_active && !active) {
        k_work_cancel_delayable(&hid_retry_work);
        k_sched_lock();
        kb_pending_count = 0;
        cons_pending_count = 0;
        k_sched_unlock();
    }
    was_active = active;
"""

new_falling = r"""    /*
     * A falling ESB-active edge starts a completely new HID delivery
     * epoch. Clear the complete report/refcount state rather than only
     * the pending queues so a held key cannot reappear after reconnect.
     */
    if (relay_was_active && !active) {
        zmk_esb_hid_relay_reset_state();
    }

    relay_was_active = active;
"""

replace_once(
    "src/esb/hid_relay.c",
    old_falling,
    new_falling,
)


###############################################################################
# 2. ESB MOUSE / POINTER LOCAL HID STATE
###############################################################################

old_tail = r"""DT_INST_FOREACH_STATUS_OKAY(ESB_IP_INST)
"""

new_tail = r"""DT_INST_FOREACH_STATUS_OKAY(ESB_IP_INST)

/*
 * Reset one ESB input-processor instance after a link/lifecycle break.
 *
 * The retry item is cancelled before its accumulator is touched. The
 * scheduler lock is the same synchronization mechanism already used by
 * the live input/retry paths on this single-core target.
 */
static void esb_ip_reset_one(struct esb_ip_data *d) {
    (void)k_work_cancel_delayable(&d->retry_work);

    k_sched_lock();

    d->dx = 0;
    d->dy = 0;
    d->scroll_x = 0;
    d->scroll_y = 0;

    d->buttons = 0;

    d->accum_start_ms = 0;

    memset(d->pending_buttons, 0, sizeof(d->pending_buttons));
    d->pending_count = 0;

    k_sched_unlock();
}

#define ESB_IP_RESET_INSTANCE(n) esb_ip_reset_one(&esb_ip_data_##n);

void zmk_esb_input_reset_all(void) {
    DT_INST_FOREACH_STATUS_OKAY(ESB_IP_RESET_INSTANCE)
}

#undef ESB_IP_RESET_INSTANCE

/*
 * Re-send the neutral absolute mouse-button state after recovery.
 *
 * esb_ip_resync_cb already contains the proper quiet-window and pointer
 * back-pressure handling, so reuse that proven path rather than creating
 * another transport send implementation.
 */
#define ESB_IP_SYNC_INSTANCE(n) \
    esb_ip_resync_cb(ESB_HID_RESYNC_MOUSE, &esb_ip_data_##n);

void zmk_esb_input_sync_neutral_all(void) {
    DT_INST_FOREACH_STATUS_OKAY(ESB_IP_SYNC_INSTANCE)
}

#undef ESB_IP_SYNC_INSTANCE
"""

replace_once(
    "src/esb/input_processor_esb.c",
    old_tail,
    new_tail,
)


###############################################################################
# 3. ESB PAIRING / CONNECTION LIFECYCLE INTEGRATION
###############################################################################

register_marker = """LOG_MODULE_REGISTER(zmk_esb_pairing, CONFIG_ZMK_ESB_ENDPOINT_LOG_LEVEL);
"""

register_new = r"""LOG_MODULE_REGISTER(zmk_esb_pairing, CONFIG_ZMK_ESB_ENDPOINT_LOG_LEVEL);

/*
 * Internal HID lifecycle hooks implemented by the ESB HID relay and ESB
 * input-processor translation units.
 */
void zmk_esb_hid_relay_reset_state(void);
void zmk_esb_hid_relay_sync_neutral(void);

void zmk_esb_input_reset_all(void);
void zmk_esb_input_sync_neutral_all(void);

/*
 * Optional application hook.
 *
 * The stock ESB module stays independent from the customised firmware.
 * Our firmware supplies a strong implementation which cancels Drag Lock,
 * temporary modes and macros after a genuine ESB connection loss.
 */
__attribute__((weak))
void zmk_esb_endpoint_connection_state_changed(bool connected) {
    ARG_UNUSED(connected);
}

static bool connection_notified;

static void notify_connection_state(const bool connected) {
    if (connection_notified == connected) {
        return;
    }

    connection_notified = connected;
    zmk_esb_endpoint_connection_state_changed(connected);
}

static void esb_hid_lifecycle_reset(void) {
    zmk_esb_hid_relay_reset_state();
    zmk_esb_input_reset_all();
}

static void esb_hid_lifecycle_sync_neutral(void) {
    zmk_esb_hid_relay_sync_neutral();
    zmk_esb_input_sync_neutral_all();
}
"""

replace_once(
    "src/esb/pairing.c",
    register_marker,
    register_new,
)


###############################################################################
# Starting an ESB slot begins a fresh HID delivery epoch.
###############################################################################

replace_once(
    "src/esb/pairing.c",
    r"""void pairing_start(void) {
    if (m_has_stored_peer) {
""",
    r"""void pairing_start(void) {
    /*
     * Starting an ESB slot begins a fresh HID delivery epoch. Never carry
     * keyboard/mouse state from a previous use of this profile.
     */
    esb_hid_lifecycle_reset();
    notify_connection_state(false);

    if (m_has_stored_peer) {
""",
)


###############################################################################
# Leaving ESB clears all locally accumulated HID state.
###############################################################################

replace_once(
    "src/esb/pairing.c",
    r"""void pairing_stop(void) {
    m_state = PAIRING_STATE_IDLE;
    k_work_cancel_delayable(&beacon_work);
    k_work_cancel_delayable(&verify_work);
""",
    r"""void pairing_stop(void) {
    m_state = PAIRING_STATE_IDLE;

    k_work_cancel_delayable(&beacon_work);
    k_work_cancel_delayable(&verify_work);

    esb_hid_lifecycle_reset();
    notify_connection_state(false);
""",
)


###############################################################################
# ESB unpair.
###############################################################################

old_unpair = r"""void pairing_unpair(void) {
    m_has_stored_peer = false;
    memset(m_peer_device_id, 0, sizeof(m_peer_device_id));
    save_paired(false);
    k_work_cancel_delayable(&verify_work);
    if (m_state != PAIRING_STATE_IDLE) {
        k_work_cancel_delayable(&beacon_work);
        m_state = PAIRING_STATE_UNPAIRED;
        k_work_reschedule(&beacon_work, K_MSEC(CONFIG_ZMK_ESB_ENDPOINT_BEACON_INITIAL_DELAY_MS));
    }
}
"""

new_unpair = r"""void pairing_unpair(void) {
    const bool slot_active = m_state != PAIRING_STATE_IDLE;

    if (slot_active) {
        /*
         * Mark the link unavailable before flushing. Any HID resync work
         * generated by the transport flush will therefore refuse to replay
         * stale state.
         */
        m_state = PAIRING_STATE_UNPAIRED;
    }

    esb_hid_lifecycle_reset();
    notify_connection_state(false);

    if (slot_active) {
        esb_transport_flush_tx();
    }

    m_has_stored_peer = false;
    memset(m_peer_device_id, 0, sizeof(m_peer_device_id));

    save_paired(false);

    k_work_cancel_delayable(&verify_work);

    if (slot_active) {
        k_work_cancel_delayable(&beacon_work);
        k_work_reschedule(
            &beacon_work,
            K_MSEC(CONFIG_ZMK_ESB_ENDPOINT_BEACON_INITIAL_DELAY_MS)
        );
    }
}
"""

replace_once(
    "src/esb/pairing.c",
    old_unpair,
    new_unpair,
)


###############################################################################
# Fresh pairing successfully connects.
###############################################################################

old_pair_connected = r"""            send_pair_resp();
            m_has_stored_peer = true;
            m_state = PAIRING_STATE_CONNECTED;
            LOG_INF("paired with %02X%02X%02X%02X%02X%02X, connected",
                    m_peer_device_id[0], m_peer_device_id[1], m_peer_device_id[2],
                    m_peer_device_id[3], m_peer_device_id[4], m_peer_device_id[5]);
            save_paired(true);
#if IS_ENABLED(CONFIG_ZMK_ADAPTIVE_FEEDBACK)
            zaf_custom_event_trigger(&esb_dongle_paired);
#endif
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
            esb_shell_relay_on_connected();
#endif
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP)
            channel_hop_ep_on_connected();
#endif
"""

new_pair_connected = r"""            send_pair_resp();
            m_has_stored_peer = true;

            /*
             * Input accumulated while unpaired is deliberately discarded.
             * A newly paired host begins from a neutral HID state.
             */
            esb_hid_lifecycle_reset();

            m_state = PAIRING_STATE_CONNECTED;
            notify_connection_state(true);

            LOG_INF("paired with %02X%02X%02X%02X%02X%02X, connected",
                    m_peer_device_id[0], m_peer_device_id[1], m_peer_device_id[2],
                    m_peer_device_id[3], m_peer_device_id[4], m_peer_device_id[5]);

            save_paired(true);

#if IS_ENABLED(CONFIG_ZMK_ADAPTIVE_FEEDBACK)
            zaf_custom_event_trigger(&esb_dongle_paired);
#endif

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
            esb_shell_relay_on_connected();
#endif

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP)
            channel_hop_ep_on_connected();
#endif

            esb_hid_lifecycle_sync_neutral();
"""

replace_once(
    "src/esb/pairing.c",
    old_pair_connected,
    new_pair_connected,
)


###############################################################################
# Existing paired dongle completes VERIFY.
###############################################################################

old_verify_connected = r"""                k_work_cancel_delayable(&verify_work);
                m_state = PAIRING_STATE_CONNECTED;
                LOG_DBG("dongle connected");
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
                esb_shell_relay_on_connected();
#endif
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP)
                channel_hop_ep_on_connected();
#endif
"""

new_verify_connected = r"""                k_work_cancel_delayable(&verify_work);

                /*
                 * VERIFY completion is a new delivery epoch. Discard
                 * anything accumulated before or during receiver recovery
                 * and explicitly converge the host to released state.
                 */
                esb_hid_lifecycle_reset();

                m_state = PAIRING_STATE_CONNECTED;
                notify_connection_state(true);

                LOG_DBG("dongle connected");

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
                esb_shell_relay_on_connected();
#endif

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP)
                channel_hop_ep_on_connected();
#endif

                esb_hid_lifecycle_sync_neutral();
"""

replace_once(
    "src/esb/pairing.c",
    old_verify_connected,
    new_verify_connected,
)


###############################################################################
# Explicit receiver disconnect.
###############################################################################

old_disconnect = r"""    case ESB_PKT_DISCONNECT:
        LOG_DBG("dongle disconnected");
        m_state = PAIRING_STATE_UNPAIRED;
        m_has_stored_peer = false;
        memset(m_peer_device_id, 0, sizeof(m_peer_device_id));
        save_paired(false);
        k_work_cancel_delayable(&verify_work);
        k_work_reschedule(&beacon_work, K_MSEC(CONFIG_ZMK_ESB_ENDPOINT_BEACON_INITIAL_DELAY_MS));
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
        esb_shell_relay_on_disconnected();
#endif
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP)
        channel_hop_ep_on_disconnected();
#endif
        break;
"""

new_disconnect = r"""    case ESB_PKT_DISCONNECT:
        LOG_DBG("dongle disconnected");

        /*
         * Set the pairing state first. Transport-generated HID resync
         * callbacks now see !CONNECTED and therefore cannot replay old HID
         * state while we clear/flush the previous receiver epoch.
         */
        m_state = PAIRING_STATE_UNPAIRED;

        esb_hid_lifecycle_reset();
        notify_connection_state(false);

        /*
         * Reports queued for the old receiver must never be transmitted to a
         * later connection.
         */
        esb_transport_flush_tx();

        m_has_stored_peer = false;
        memset(m_peer_device_id, 0, sizeof(m_peer_device_id));

        save_paired(false);

        k_work_cancel_delayable(&verify_work);

        k_work_reschedule(
            &beacon_work,
            K_MSEC(CONFIG_ZMK_ESB_ENDPOINT_BEACON_INITIAL_DELAY_MS)
        );

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
        esb_shell_relay_on_disconnected();
#endif

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP)
        channel_hop_ep_on_disconnected();
#endif

        break;
"""

replace_once(
    "src/esb/pairing.c",
    old_disconnect,
    new_disconnect,
)


###############################################################################
# Dongle reboot / ESB RESYNC.
###############################################################################

old_resync = r"""    case ESB_PKT_RESYNC:
        /* Dongle rebooted and wants us to re-run the VERIFY handshake. Keep
         * the stored peer and drop back to VERIFYING; verify_work will send
         * VERIFY_REQ, which the dongle (still in STATE_VERIFYING) answers
         * with VERIFY_RESP, returning us to CONNECTED without a re-pair. */
        if (m_state == PAIRING_STATE_CONNECTED) {
            LOG_DBG("dongle requested RESYNC, re-verifying");
            m_state = PAIRING_STATE_VERIFYING;
            k_work_reschedule(&verify_work, K_NO_WAIT);
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
            esb_shell_relay_on_disconnected();
#endif
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP)
            channel_hop_ep_on_disconnected();
#endif
        }
        break;
"""

new_resync = r"""    case ESB_PKT_RESYNC:
        /*
         * Dongle rebooted and wants us to re-run VERIFY. Keep the stored
         * peer identity but start a completely new HID delivery epoch.
         */
        if (m_state == PAIRING_STATE_CONNECTED) {
            LOG_DBG("dongle requested RESYNC, re-verifying");

            /*
             * Leave CONNECTED before resetting/flushing. This prevents HID
             * resync callbacks generated by a FIFO flush from resurrecting
             * the pre-reboot report state.
             */
            m_state = PAIRING_STATE_VERIFYING;

            esb_hid_lifecycle_reset();
            notify_connection_state(false);

            esb_transport_flush_tx();

            k_work_reschedule(&verify_work, K_NO_WAIT);

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY)
            esb_shell_relay_on_disconnected();
#endif

#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP)
            channel_hop_ep_on_disconnected();
#endif
        }

        break;
"""

replace_once(
    "src/esb/pairing.c",
    old_resync,
    new_resync,
)


###############################################################################
# FINAL PATCHER ASSERTIONS
###############################################################################

required = {
    "src/esb/hid_relay.c": (
        "zmk_esb_hid_relay_reset_state",
        "zmk_esb_hid_relay_sync_neutral",
        "relay_was_active",
    ),
    "src/esb/input_processor_esb.c": (
        "zmk_esb_input_reset_all",
        "zmk_esb_input_sync_neutral_all",
    ),
    "src/esb/pairing.c": (
        "esb_hid_lifecycle_reset",
        "esb_hid_lifecycle_sync_neutral",
        "notify_connection_state(false)",
        "notify_connection_state(true)",
        "esb_transport_flush_tx();",
    ),
}

for relpath, tokens in required.items():
    text = (ROOT / relpath).read_text()

    for token in tokens:
        if token not in text:
            raise SystemExit(
                f"ESB patch verification failed: "
                f"{relpath}: missing {token}"
            )

subprocess.run(
    ["git", "-C", str(ROOT), "diff", "--check"],
    check=True,
)

print(
    "Applied ESB HID lifecycle hardening to pinned module "
    f"{EXPECTED_SHA}"
)
