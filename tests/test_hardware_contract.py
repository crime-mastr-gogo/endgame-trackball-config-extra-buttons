"""Static checkpoint checks; not a substitute for a ZMK build or hardware test."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/arm/efogtech_trackball_0"


class HardwareContract(unittest.TestCase):
    def test_no_encoder_build_dependencies(self):
        west = (ROOT / "config/west.yml").read_text()
        defconfig = (BOARD / "efogtech_trackball_0_defconfig").read_text()
        encoders = (BOARD / "encoders.dtsi").read_text()
        build = (ROOT / "build.yaml").read_text()
        self.assertNotIn("zmk-ec11-ish-driver", west)
        self.assertNotIn("zmk-behavior-follower", west)
        self.assertNotIn("CONFIG_EC11", defconfig)
        self.assertIn("CONFIG_ZMK_USB_LOGGING=n", defconfig)
        self.assertNotIn("behaviors/follower.dtsi", encoders)
        self.assertNotIn("zmk-usb-logging", build)
        board_source = (BOARD / "efogtech_trackball_0.c").read_text()
        self.assertIn("#ifdef CONFIG_LOG_DOMAIN_ID\nstatic int16_t settings_log_source_id", board_source)
        self.assertNotIn("config EC11", (BOARD / "Kconfig.defconfig").read_text())
        self.assertNotIn("behavior-sensor-rotate", (BOARD / "behaviors_macros.dtsi").read_text())
        config = (ROOT / "config/efogtech_trackball_0.conf").read_text()
        self.assertNotIn("CONFIG_ZMK_KSCAN_COMPOSITE_DRIVER=", config)
        self.assertNotIn("CONFIG_EC11", config)

    def test_layout(self):
        text = (BOARD / "buttons.dtsi").read_text()
        rows = [int(x) for x in re.findall(r"RC\((\d+), 0\)", text)]
        self.assertEqual(rows, list(range(8)) + [13, 14, 11, 12, 8, 9, 10])
        self.assertEqual(text.count("<&key_physical_attrs"), 15)

    def test_keymap(self):
        text = (ROOT / "config/efogtech_trackball_0.keymap").read_text()
        bindings = re.findall(r"bindings = <(.*?)>;", text, re.S)
        self.assertEqual(len(bindings), 8)
        for layer in bindings:
            self.assertEqual(layer.count("&"), 15)
        self.assertNotIn("DECLARE_ENCODERS", text)
        self.assertNotIn("type_right_encoder_b", text)

        without_comments = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
        compact = " ".join(without_comments.split())
        self.assertIn("&ltmkp LAYER_SNIPE LC(C) &ltmkp LAYER_SCROLL LC(V)", compact)
        self.assertIn("&drag_lock &mo LAYER_EXTRAS &none &none &mo LAYER_FEEDBACK "
                      "&mo LAYER_STATUS &mo LAYER_DEVICE", compact)
        self.assertIn("&twist_sens_up_fb &trans &twist_sens_down_fb &bt BT_NXT "
                      "&bt BT_PRV &ptr_sens_up_fb &ptr_sens_down_fb", compact)
        self.assertIn("&studio_unlock &hold_power_off &scroll_mode_toggle "
                      "&hold_clear_all_bt &hold_clear_current_bt &hold_sens_reset", compact)

    def test_protected_action_contract(self):
        controls = (BOARD / "custom_controls.dtsi").read_text()
        source = (ROOT / "src/controls.c").read_text()
        policy = (ROOT / "include/ankur/policy.h").read_text()
        for name in ("hold_power_off", "hold_clear_current_bt",
                     "hold_clear_all_bt", "hold_sens_reset"):
            self.assertIn(name, controls)
        self.assertIn("#define ANKUR_GUARD_MS 2000", policy)
        self.assertIn("guarded_epoch[slot] == epoch", source)
        self.assertIn("e.timestamp - guarded_pressed_at[slot] >= ANKUR_GUARD_MS", source)
        self.assertIn("release_drag();", source)

    def test_single_motor_owner(self):
        pointer = (BOARD / "pointer.dtsi").read_text()
        visuals = (BOARD / "visuals.dtsi").read_text()
        adaptive = (ROOT / "vendor/adaptive-feedback/src/adaptive_feedback.c").read_text()
        self.assertNotIn("feedback-gpios", pointer)
        self.assertEqual(visuals.count("feedback-gpios"), 1)
        self.assertNotIn("gpio_pin_set_dt", adaptive)
        self.assertIn("fbc_trigger_pattern_priority", adaptive)

    def test_no_pin_probe_or_encoder_nodes(self):
        text = (BOARD / "efogtech_trackball_0.c").read_text()
        self.assertNotIn("gpio_pin_configure(p0", text)
        self.assertNotIn("set_bl_en", text)
        self.assertNotIn("efog,ec11-ish", (BOARD / "encoders.dtsi").read_text())

    def test_added_pullups(self):
        text = (BOARD / "buttons.dtsi").read_text()
        self.assertEqual(text.count("GPIO_ACTIVE_LOW | GPIO_PULL_UP"), 7)


if __name__ == "__main__":
    unittest.main()
