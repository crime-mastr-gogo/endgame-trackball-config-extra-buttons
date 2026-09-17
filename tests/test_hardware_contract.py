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
        sizes = re.findall(r"<&key_physical_attrs\s+(\d+)\s+(\d+)", text)
        self.assertEqual(sizes[8:], [("100", "200")] * 7)

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
        names = {
            "ankurs-customised-endgame-production":
                "ankurs customised endgame firmware production",
            "ankurs-customised-endgame-debug":
                "ankurs customised endgame firmware debug",
        }
        for artifact_name, display_name in names.items():
            wrapper = ROOT / f".github/workflows/{artifact_name}.yml"
            self.assertTrue(wrapper.is_file())
            text = wrapper.read_text()
            self.assertIn(f"name: {display_name}", text)
            self.assertIn(f"run-name: {display_name}", text)
            self.assertIn(f"build_name: {artifact_name}", text)
        production = (ROOT / "snippets/ankur-production/ankur-production.conf").read_text()
        self.assertIn("CONFIG_ZMK_USB_LOGGING=n", production)
        debug = (ROOT / ".github/workflows/ankurs-customised-endgame-debug.yml").read_text()
        self.assertIn("zmk-usb-logging", debug)

    def test_identifiable_action_metadata(self):
        controls = (ROOT / "src/controls.c").read_text()
        for label in ("Toggle Drag Lock", "Next Bluetooth Profile",
                      "Increase Twist Sensitivity", "Hold Drag Scroll"):
            self.assertIn(f'ACTION_METADATA("{label}"', controls)
        self.assertIn(".parameter_metadata=&ankur_metadata", controls)

    def test_scroll_modes_match_824a58a(self):
        pointer_dtsi = (BOARD / "pointer.dtsi").read_text()
        twist = re.search(r"zip_bistable_twist_scaler:.*?\n\s*};", pointer_dtsi, re.S)
        self.assertIsNotNone(twist)
        self.assertIn("default-coef = <ZBS_SCALE(1, 40)>", twist.group(0))
        self.assertIn("default-coef-slot1 = <ZBS_SCALE(1, 40)>", twist.group(0))
        notch = re.search(r"zip_twist_full_notch_scaler:.*?\n\s*};", pointer_dtsi, re.S)
        self.assertIsNotNone(notch)
        self.assertIn("default-coef = <ZBS_SCALE(16, 1)>", notch.group(0))
        pointer_c = (ROOT / "src/pointer.c").read_text()
        self.assertIn("STAGE(zip_bistable_twist_scaler),STAGE(zip_twist_full_notch_scaler)",
                      pointer_c)
        self.assertNotIn("accumulated/16", pointer_c)
        keymap = (ROOT / "config/efogtech_trackball_0.keymap").read_text()
        self.assertNotIn("<&ankur_scroll_mode>", keymap)

    def test_current_sleep_policy(self):
        conf = (ROOT / "config/efogtech_trackball_0.conf").read_text()
        self.assertIn("CONFIG_ZMK_IDLE_TIMEOUT=900000", conf)
        self.assertIn("CONFIG_ZMK_IDLE_SLEEP_TIMEOUT=900000", conf)

    def test_custom_module_exposes_snippets(self):
        module = (ROOT / "zephyr/module.yml").read_text()
        self.assertIn("snippet_root: .", module)


if __name__ == "__main__":
    unittest.main()
