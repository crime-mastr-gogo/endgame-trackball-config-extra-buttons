"""Static hardware ownership checks; not a substitute for electrical testing."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/arm/efogtech_trackball_0"


class HardwareContract(unittest.TestCase):
    def test_added_pins_in_order_and_pullups(self):
        text = (BOARD / "buttons.dtsi").read_text()
        pins = re.findall(r"<&gpio([01]) (\d+) \(GPIO_ACTIVE_LOW \| GPIO_PULL_UP\)>", text)
        self.assertEqual(pins, [("0", "17"), ("0", "16"), ("1", "8"),
                                ("1", "9"), ("0", "11"), ("0", "15"), ("0", "20")])
        self.assertEqual(len(set(pins)), 7)

    def test_layout_has_fifteen_positions(self):
        text = (BOARD / "buttons.dtsi").read_text()
        self.assertEqual(re.findall(r"RC\((\d+), 0\)", text), list(map(str, range(15))))
        self.assertEqual(text.count("<&key_physical_attrs"), 15)

    def test_no_encoder_pin_owners(self):
        text = (BOARD / "encoders.dtsi").read_text()
        self.assertNotIn("a-gpios", text)
        self.assertNotIn("sensor-bindings", text)

    def test_no_runtime_probe_of_switch_pins(self):
        text = (BOARD / "efogtech_trackball_0.c").read_text()
        self.assertIsNone(re.search(r"gpio_pin_configure\(p0,\s*(11|15|20),", text))

    def test_debounce(self):
        text = (BOARD / "efogtech_trackball_0_defconfig").read_text()
        for edge in ("PRESS", "RELEASE"):
            self.assertIn(f"CONFIG_ZMK_KSCAN_DEBOUNCE_{edge}_MS=15", text)

    def test_removed_encoder_macro_not_invoked(self):
        text = (ROOT / "config/efogtech_trackball_0.keymap").read_text()
        self.assertNotIn("DECLARE_ENCODERS", text)

    def test_five_layer_contract_and_tap_hold_timing(self):
        keymap = (ROOT / "config/efogtech_trackball_0.keymap").read_text()
        for name in ("default_layer", "control_layer", "device_layer",
                     "feedback_layer", "status_layer"):
            self.assertIn(name, keymap)
        self.assertEqual(keymap.count("display-name ="), 5)
        behaviors = (BOARD / "behaviors_macros.dtsi").read_text()
        for behavior in ("copy_fine", "paste_scroll"):
            block = re.search(rf"{behavior}:.*?\n\s*\}};", behaviors, re.S)
            self.assertIsNotNone(block)
            self.assertIn("tapping-term-ms = <300>", block.group(0))

    def test_exact_build_names_and_production_logging_policy(self):
        workflow = (ROOT / ".github/workflows/ankur-build.yml").read_text()
        self.assertIn("sha256sum", workflow)
        for name in ("ankurs-customised-endgame-production",
                     "ankurs-customised-endgame-debug"):
            wrapper = ROOT / f".github/workflows/{name}.yml"
            self.assertTrue(wrapper.is_file())
            self.assertIn(f"name: {name}", wrapper.read_text())
        production = (ROOT / "snippets/ankur-production/ankur-production.conf").read_text()
        self.assertIn("CONFIG_ZMK_USB_LOGGING=n", production)
        debug = (ROOT / ".github/workflows/ankurs-customised-endgame-debug.yml").read_text()
        self.assertIn("zmk-usb-logging", debug)

    def test_custom_module_exposes_snippets(self):
        module = (ROOT / "zephyr/module.yml").read_text()
        self.assertIn("snippet_root: .", module)


if __name__ == "__main__":
    unittest.main()
