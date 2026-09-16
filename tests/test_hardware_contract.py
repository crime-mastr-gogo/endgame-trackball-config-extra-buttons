"""Static checkpoint checks; not a substitute for a ZMK build or hardware test."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/arm/efogtech_trackball_0"


class HardwareContract(unittest.TestCase):
    def test_layout(self):
        text = (BOARD / "buttons.dtsi").read_text()
        rows = [int(x) for x in re.findall(r"RC\((\d+), 0\)", text)]
        self.assertEqual(rows, list(range(8)) + [13, 14, 11, 12, 8, 9, 10])
        self.assertEqual(text.count("<&key_physical_attrs"), 15)

    def test_keymap(self):
        text = (ROOT / "config/efogtech_trackball_0.keymap").read_text()
        bindings = re.findall(r"bindings = <(.*?)>;", text, re.S)
        self.assertEqual(len(bindings), 6)
        for layer in bindings:
            self.assertEqual(layer.count("&"), 15)
        self.assertNotIn("DECLARE_ENCODERS", text)

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
