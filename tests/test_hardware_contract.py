"""Static firmware contracts; not a substitute for physical hardware testing."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/arm/efogtech_trackball_0"


class HardwareContract(unittest.TestCase):

    def test_added_pins_in_order_and_pullups(self):
        text = (BOARD / "buttons.dtsi").read_text()

        pins = re.findall(
            r"<&gpio([01]) (\d+) "
            r"\(GPIO_ACTIVE_LOW \| GPIO_PULL_UP\)>",
            text,
        )

        self.assertEqual(
            pins,
            [
                ("0", "17"),
                ("0", "16"),
                ("1", "8"),
                ("1", "9"),
                ("0", "11"),
                ("0", "15"),
                ("0", "20"),
            ],
        )

        self.assertEqual(len(set(pins)), 7)


    def test_layout_has_fifteen_positions(self):
        text = (BOARD / "buttons.dtsi").read_text()

        self.assertEqual(
            re.findall(r"RC\((\d+), 0\)", text),
            list(map(str, range(15))),
        )

        self.assertEqual(
            text.count("<&key_physical_attrs"),
            15,
        )

        sizes = re.findall(
            r"<&key_physical_attrs\s+(\d+)\s+(\d+)",
            text,
        )

        self.assertEqual(
            sizes[8:],
            [("100", "200")] * 7,
        )


    def test_no_encoder_pin_owners(self):
        text = (BOARD / "encoders.dtsi").read_text()

        self.assertNotIn("a-gpios", text)
        self.assertNotIn("sensor-bindings", text)

        west = (ROOT / "config/west.yml").read_text()

        self.assertNotIn(
            "zmk-ec11-ish-driver",
            west,
        )


    def test_no_runtime_probe_of_switch_pins(self):
        text = (
            BOARD / "efogtech_trackball_0.c"
        ).read_text()

        self.assertIsNone(
            re.search(
                r"gpio_pin_configure\(p0,\s*(11|15|20),",
                text,
            )
        )


    def test_feedback_has_one_gpio_owner(self):
        board = (
            BOARD / "efogtech_trackball_0.c"
        ).read_text()

        visuals = (
            BOARD / "visuals.dtsi"
        ).read_text()

        feedback = (
            ROOT / "src/feedback.c"
        ).read_text()

        self.assertNotIn("set_3v3_en", board)
        self.assertNotIn("set_rgb_en", board)

        self.assertNotIn(
            "zmk,adaptive-feedback",
            visuals,
        )

        self.assertNotIn(
            "zmk,feedback-common",
            visuals,
        )

        self.assertNotIn(
            "zmk,ext-power-generic",
            visuals,
        )

        self.assertNotIn(
            "ext_power_",
            feedback,
        )

        self.assertIn(
            "feedback_power_prepare",
            feedback,
        )

        self.assertIn(
            "feedback_power_finish",
            feedback,
        )

        self.assertIn(
            "gpio_pin_set(power_port, 0, 0)",
            feedback,
        )

        # The obsolete runtime RGB detector previously outlived CONFIG_SHELL
        # and broke production compilation.
        self.assertNotIn(
            "rgb_hw_check_work_handler",
            board,
        )

        self.assertNotIn(
            "rgb_supported",
            board,
        )


    def test_debounce(self):
        text = (
            BOARD /
            "efogtech_trackball_0_defconfig"
        ).read_text()

        for edge in ("PRESS", "RELEASE"):
            self.assertIn(
                f"CONFIG_ZMK_KSCAN_DEBOUNCE_{edge}_MS=15",
                text,
            )


    def test_five_layer_contract_and_tap_hold_timing(self):
        keymap = (
            ROOT /
            "config/efogtech_trackball_0.keymap"
        ).read_text()

        for name in (
            "default_layer",
            "control_layer",
            "device_layer",
            "feedback_layer",
            "status_layer",
        ):
            self.assertIn(name, keymap)

        self.assertEqual(
            keymap.count("display-name ="),
            5,
        )

        behaviors = (
            BOARD / "behaviors_macros.dtsi"
        ).read_text()

        for behavior in (
            "copy_fine",
            "paste_scroll",
        ):
            block = re.search(
                rf"{behavior}:.*?\n\s*\}};",
                behaviors,
                re.S,
            )

            self.assertIsNotNone(block)

            self.assertIn(
                "tapping-term-ms = <300>",
                block.group(0),
            )

        controls = (
            ROOT / "src/controls.c"
        ).read_text()

        self.assertIn(
            "ANKUR_STRING_HOLD_MS",
            controls,
        )

        self.assertIn(
            "ANKUR_PROTECTED_HOLD_MS",
            controls,
        )

        self.assertIn(
            "ankur_deadline_due",
            controls,
        )


    def test_exact_final_workflow_names_and_artifacts(self):
        names = {
            "ankurs-customised-endgame-production":
                "18 SEPT CODE IMPROVEMENTS",

            "ankurs-customised-endgame-debug":
                "18 SEPT CODE IMPROVEMENTS DEBUG",
        }

        for artifact_name, display_name in names.items():
            wrapper = (
                ROOT /
                f".github/workflows/{artifact_name}.yml"
            )

            self.assertTrue(wrapper.is_file())

            text = wrapper.read_text()

            self.assertIn(
                f"name: {display_name}",
                text,
            )

            self.assertIn(
                f"run-name: {display_name}",
                text,
            )

            self.assertIn(
                f"build_name: {artifact_name}",
                text,
            )

        contract = (
            ROOT /
            ".github/workflows/contract-checks.yml"
        ).read_text()

        self.assertIn(
            "name: 18 SEPT CODE IMPROVEMENTS CONTRACT CHECKS",
            contract,
        )

        production = (
            ROOT /
            "snippets/ankur-production/ankur-production.conf"
        ).read_text()

        self.assertIn(
            "CONFIG_ZMK_USB_LOGGING=n",
            production,
        )

        self.assertIn(
            "CONFIG_SHELL=n",
            production,
        )

        debug = (
            ROOT /
            ".github/workflows/ankurs-customised-endgame-debug.yml"
        ).read_text()

        self.assertIn(
            "zmk-usb-logging",
            debug,
        )


    def test_identifiable_action_metadata(self):
        controls = (
            ROOT / "src/controls.c"
        ).read_text()

        for label in (
            "Toggle Drag Lock",
            "Next Bluetooth Profile",
            "Increase Twist Sensitivity",
            "Hold Drag Scroll",
        ):
            self.assertIn(
                f'ACTION_METADATA("{label}"',
                controls,
            )

        self.assertIn(
            ".parameter_metadata=&ankur_metadata",
            controls,
        )


    def test_drag_scroll_matches_proven_efog_processing(self):
        pointer = (
            ROOT / "src/pointer.c"
        ).read_text()

        ordered_tokens = [
            "STAGE(zip_bistable_scroll_xy_scaler)",
            "STAGE(zip_axis_clamper)",
            "STAGE(zip_xy_to_scroll_mapper)",
            "DT_NODELABEL(zip_scroll_transform)",
            "INPUT_TRANSFORM_Y_INVERT",
            "STAGE(zip_dragscroll_accel)",
            "STAGE(zip_rotate_scroll)",
        ]

        previous = -1

        for token in ordered_tokens:
            current = pointer.find(token)

            self.assertGreater(
                current,
                previous,
                msg=f"Drag Scroll order broken at {token}",
            )

            previous = current


    def test_scroll_modes_match_proven_twist_processing(self):
        pointer_dtsi = (
            BOARD / "pointer.dtsi"
        ).read_text()

        twist = re.search(
            r"zip_bistable_twist_scaler:"
            r".*?\n\s*};",
            pointer_dtsi,
            re.S,
        )

        self.assertIsNotNone(twist)

        self.assertIn(
            "default-coef = <ZBS_SCALE(1, 40)>",
            twist.group(0),
        )

        notch = re.search(
            r"zip_twist_full_notch_scaler:"
            r".*?\n\s*};",
            pointer_dtsi,
            re.S,
        )

        self.assertIsNotNone(notch)

        self.assertIn(
            "default-coef = <ZBS_SCALE(16, 1)>",
            notch.group(0),
        )


    def test_pointer_hot_path_uses_cached_scroll_mode(self):
        pointer = (
            ROOT / "src/pointer.c"
        ).read_text()

        self.assertNotIn(
            "ankur_settings_get()",
            pointer,
        )

        self.assertIn(
            "atomic_get(&high_res_mode)",
            pointer,
        )

        controls = (
            ROOT / "src/controls.c"
        ).read_text()

        self.assertIn(
            "ankur_pointer_set_high_res",
            controls,
        )


    def test_current_sleep_policy(self):
        conf = (
            ROOT /
            "config/efogtech_trackball_0.conf"
        ).read_text()

        self.assertIn(
            "CONFIG_ZMK_IDLE_TIMEOUT=900000",
            conf,
        )

        self.assertIn(
            "CONFIG_ZMK_IDLE_SLEEP_TIMEOUT=900000",
            conf,
        )


    def test_esb_lifecycle_hardening_is_preserved(self):
        patcher = (
            ROOT /
            "scripts/apply-esb-lifecycle-fix.py"
        ).read_text()

        for token in (
            "zmk_esb_hid_relay_reset_state",
            "zmk_esb_hid_relay_sync_neutral",
            "zmk_esb_input_reset_all",
            "zmk_esb_input_sync_neutral_all",
            "esb_transport_flush_tx",
            'EXPECTED_SHA = '
            '"89b695a9aca6dc5a7daf4488140ef46e19fc266c"',
        ):
            self.assertIn(token, patcher)

        controls = (
            ROOT / "src/controls.c"
        ).read_text()

        self.assertIn(
            "zmk_esb_endpoint_connection_state_changed",
            controls,
        )

        self.assertIn(
            "ankur_controls_cancel_esb_loss",
            controls,
        )


    def test_usb_ble_hid_lifecycle_patch(self):
        patcher = (
            ROOT /
            "scripts/apply-zmk-reliability-fix.py"
        ).read_text()

        for token in (
            "zmk_endpoints_send_mouse_report();",
            "explicit_modifier_counts, 0",
            "explicit_button_counts, 0",
            "zephyr/pm/device_runtime.h",
            "already-cleared mouse button",
        ):
            self.assertIn(token, patcher)

        controls = (
            ROOT / "src/controls.c"
        ).read_text()

        self.assertIn(
            "BT_CONN_CB_DEFINE",
            controls,
        )

        self.assertIn(
            "ble_disconnect_cleanup",
            controls,
        )

        self.assertIn(
            "zmk_endpoints_clear_current();",
            controls,
        )


    def test_dependencies_are_pruned_and_fully_pinned(self):
        west = (
            ROOT / "config/west.yml"
        ).read_text()

        revisions = re.findall(
            r'revision:\s*"([0-9a-f]+)"',
            west,
        )

        self.assertGreater(
            len(revisions),
            0,
        )

        for revision in revisions:
            self.assertEqual(
                len(revision),
                40,
            )

        for removed in (
            "zmk-ec11-ish-driver",
            "zmk-auto-hold",
            "zmk-adaptive-feedback",
            "zmk-behavior-follower",
            "zmk-keymap-shell",
            "zmk-ble-shell",
            "zmk-feedback-common",
        ):
            self.assertNotIn(
                removed,
                west,
            )

        defconfig = (
            BOARD /
            "efogtech_trackball_0_defconfig"
        ).read_text()

        self.assertIn(
            "CONFIG_ZMK_ESB_ENDPOINT=y",
            defconfig,
        )

        self.assertIn(
            "CONFIG_ZMK_ESB_ENDPOINT_SHELL_RELAY=n",
            defconfig,
        )

        self.assertIn(
            "CONFIG_ZMK_ESB_ENDPOINT_CHANNEL_HOP=y",
            defconfig,
        )


    def test_build_reproducibility_and_health_contract(self):
        workflow = (
            ROOT /
            ".github/workflows/ankur-build.yml"
        ).read_text()

        self.assertIn(
            "zmkfirmware/zmk-build-arm@sha256:"
            "edb1c953438c6f720ddb79c3762f3972013b7fbbaf4fff3592fc869983e7afc5",
            workflow,
        )

        for sha in (
            "11d5960a326750d5838078e36cf38b85af677262",
            "0057852bfaa89a56745cba8c7296529d2fc39830",
            "ea165f8d65b6e75b540449e92b4886f43607fa02",
        ):
            self.assertIn(
                sha,
                workflow +
                (
                    ROOT /
                    ".github/workflows/contract-checks.yml"
                ).read_text(),
            )

        self.assertIn(
            "check-build-health.py",
            workflow,
        )

        self.assertIn(
            "apply-p2sm-cleanup.py",
            workflow,
        )

        p2sm_patcher = (
            ROOT /
            "scripts/apply-p2sm-cleanup.py"
        ).read_text()

        self.assertIn(
            "DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_p2sm_sens)",
            p2sm_patcher,
        )

        checker = (
            ROOT /
            "scripts/check-build-health.py"
        ).read_text()

        self.assertIn(
            "implicit declaration of function",
            checker,
        )

        self.assertIn(
            '"flash": 95.0',
            checker,
        )

        self.assertIn(
            "duplicate 'const' declaration specifier",
            checker,
        )

        defconfig = (
            BOARD /
            "efogtech_trackball_0_defconfig"
        ).read_text()

        self.assertIn(
            "CONFIG_SHELL=n",
            defconfig,
        )

        self.assertNotIn(
            "CONFIG_ZMK_BEHAVIOR_TAP_DANCE_MAX_HELD",
            defconfig,
        )


    def test_dead_code_cleanup(self):
        self.assertFalse(
            (
                ROOT /
                "dts/bindings/input_processors/"
                "ankur,scroll-mode.yaml"
            ).exists()
        )

        policy = (
            ROOT /
            "include/ankur/policy.h"
        ).read_text()

        self.assertNotIn(
            "ankur_scale",
            policy,
        )

        behaviors = (
            BOARD /
            "behaviors_macros.dtsi"
        ).read_text()

        for obsolete in (
            "bst_copy",
            "bst_paste",
            "click-double-click",
            "ble_shell_adv",
            "esb_shell_req",
            "auto_hold",
        ):
            self.assertNotIn(
                obsolete,
                behaviors,
            )


    def test_custom_module_exposes_snippets(self):
        module = (
            ROOT / "zephyr/module.yml"
        ).read_text()

        self.assertIn(
            "snippet_root: .",
            module,
        )


if __name__ == "__main__":
    unittest.main()
