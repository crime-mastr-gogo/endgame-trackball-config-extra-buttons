"""Executable reliability state models for lifecycle invariants.

These host tests intentionally model only the safety properties added by the
18 September reliability-v2 pass. Static contract tests bind the model's
constants and transition ordering back to the production C/patch sources.
"""

import unittest


HELD_KEEPALIVE_MS = 60
LONG_LOSS_MS = 385


class IndicatorTransaction:
    def __init__(self):
        self.expected = None
        self.state = None

    def request(self, seq):
        self.expected = seq & 0xFF
        self.state = None

    def receive(self, seq, valid, caps_on):
        if (seq & 0xFF) != self.expected:
            return False

        self.state = None if not valid else bool(caps_on)
        return True

    def result(self, seq):
        if (seq & 0xFF) != self.expected:
            return None

        return self.state


class EsbHostEpoch:
    """Minimal absolute-HID / queue convergence model."""

    def __init__(self):
        self.local_held = False
        self.radio_queue = []
        self.usb_inflight = None
        self.usb_queue = []
        self.host_held = False

    def press(self):
        self.local_held = True
        self.radio_queue.append(True)

    def deliver_radio(self):
        while self.radio_queue:
            state = self.radio_queue.pop(0)
            self.usb_queue.append(state)

    def drain_usb(self):
        if self.usb_inflight is not None:
            self.host_held = self.usb_inflight
            self.usb_inflight = None

        while self.usb_queue:
            self.host_held = self.usb_queue.pop(0)

    def explicit_neutralize(self):
        # Endpoint ordering: reset local absolute state, then flush radio queue.
        self.local_held = False
        self.radio_queue.clear()

        # Dongle ordering: discard queued stale USB reports, then neutral.
        self.usb_queue.clear()
        self.usb_queue.extend([False, False, False])

    def liveness_action(self):
        return "keepalive" if self.local_held else "idle"


class LongLossReceiver:
    def __init__(self):
        self.last_rx_ms = 0
        self.host_held = False

    def receive_held_report(self, now_ms):
        self.last_rx_ms = now_ms
        self.host_held = True

    def receive_keepalive(self, now_ms):
        self.last_rx_ms = now_ms

    def check(self, now_ms):
        if now_ms - self.last_rx_ms >= LONG_LOSS_MS:
            self.host_held = False
            return "neutralize"
        return "wait"


class ReliabilityModelTests(unittest.TestCase):

    def test_stale_indicator_response_cannot_satisfy_new_request(self):
        txn = IndicatorTransaction()

        txn.request(41)
        self.assertTrue(txn.receive(41, True, True))
        self.assertTrue(txn.result(41))

        txn.request(42)

        # Delayed ACK from the previous invocation is ignored.
        self.assertFalse(txn.receive(41, True, True))
        self.assertIsNone(txn.result(42))

        self.assertTrue(txn.receive(42, True, False))
        self.assertFalse(txn.result(42))


    def test_invalid_indicator_report_never_becomes_caps_off(self):
        txn = IndicatorTransaction()
        txn.request(7)

        self.assertTrue(txn.receive(7, False, False))
        self.assertIsNone(txn.result(7))


    def test_explicit_neutralization_discards_stale_reports(self):
        epoch = EsbHostEpoch()

        epoch.press()
        epoch.deliver_radio()

        # One stale pressed report is already in flight and another is queued.
        epoch.usb_inflight = True
        epoch.usb_queue.append(True)

        epoch.explicit_neutralize()

        self.assertFalse(epoch.local_held)
        self.assertEqual(epoch.radio_queue, [])
        self.assertEqual(epoch.usb_queue, [False, False, False])

        # Even if the already-in-flight press reaches the host first, neutral
        # reports follow and become the final absolute state.
        epoch.drain_usb()
        self.assertFalse(epoch.host_held)


    def test_held_state_uses_keepalive_not_idle(self):
        epoch = EsbHostEpoch()

        self.assertEqual(epoch.liveness_action(), "idle")

        epoch.press()
        self.assertEqual(epoch.liveness_action(), "keepalive")

        epoch.explicit_neutralize()
        self.assertEqual(epoch.liveness_action(), "idle")


    def test_keepalive_prevents_false_long_loss_while_held(self):
        receiver = LongLossReceiver()
        receiver.receive_held_report(0)

        now = HELD_KEEPALIVE_MS

        while now < 1000:
            receiver.receive_keepalive(now)
            self.assertEqual(
                receiver.check(now),
                "wait",
            )
            self.assertTrue(receiver.host_held)
            now += HELD_KEEPALIVE_MS


    def test_genuine_long_loss_fails_safe_to_neutral(self):
        receiver = LongLossReceiver()
        receiver.receive_held_report(0)

        self.assertEqual(
            receiver.check(LONG_LOSS_MS - 1),
            "wait",
        )
        self.assertTrue(receiver.host_held)

        self.assertEqual(
            receiver.check(LONG_LOSS_MS),
            "neutralize",
        )
        self.assertFalse(receiver.host_held)


if __name__ == "__main__":
    unittest.main()
