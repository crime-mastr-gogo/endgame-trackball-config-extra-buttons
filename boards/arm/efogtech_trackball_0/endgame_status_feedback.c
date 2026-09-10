#define DT_DRV_COMPAT zmk_behavior_endgame_control

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/battery.h>
#include <zmk/ble.h>

#include <zmk_adaptive_feedback/adaptive_feedback.h>
#include <zmk_esb/endpoint.h>

#define ACTION_REPORT_TWIST 0
#define ACTION_REPORT_BLUETOOTH 1
#define ACTION_REPORT_POINTER 2
#define ACTION_REPORT_SCROLL 3
#define ACTION_TOGGLE_VIBRATION 4
#define ACTION_TOGGLE_LED 5
#define ACTION_REPORT_BATTERY 6

#define SETTINGS_APPLY_DELAY_MS 500
#define LED_DISABLE_DELAY_MS 425
#define MAX_PATTERN_ENTRIES 21

extern uint8_t endgame_pointer_sensitivity_level(void);
extern uint8_t endgame_twist_sensitivity_level(void);
extern bool endgame_standard_scroll_enabled(void);

static const struct gpio_dt_spec motor_gpio =
    GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(adaptive_feedback), feedback_gpios, 0);
static const struct gpio_dt_spec rgb_enable_gpio =
    GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(epwr), control_gpios, 1);

static bool vibration_enabled = true;
static bool led_enabled = true;
static uint16_t pulse_pattern[MAX_PATTERN_ENTRIES];
static uint8_t pulse_pattern_length;
static uint8_t pulse_pattern_index;

ZAF_CUSTOM_EVENT_DEFINE(status_twist, "status-twist");
ZAF_CUSTOM_EVENT_DEFINE(status_bluetooth, "status-bluetooth");
ZAF_CUSTOM_EVENT_DEFINE(status_pointer, "status-pointer");
ZAF_CUSTOM_EVENT_DEFINE(status_scroll_standard, "status-scroll-standard");
ZAF_CUSTOM_EVENT_DEFINE(status_scroll_high_res, "status-scroll-high-res");
ZAF_CUSTOM_EVENT_DEFINE(status_battery_high, "status-battery-high");
ZAF_CUSTOM_EVENT_DEFINE(status_battery_medium, "status-battery-medium");
ZAF_CUSTOM_EVENT_DEFINE(status_battery_low, "status-battery-low");
ZAF_CUSTOM_EVENT_DEFINE(vibration_enabled_event, "vibration-enabled");
ZAF_CUSTOM_EVENT_DEFINE(vibration_disabled_event, "vibration-disabled");
ZAF_CUSTOM_EVENT_DEFINE(led_enabled_event, "led-enabled");
ZAF_CUSTOM_EVENT_DEFINE(led_disabled_event, "led-disabled");

static void motor_make_available(void) {
    if (device_is_ready(motor_gpio.port)) {
        gpio_pin_configure_dt(&motor_gpio, GPIO_OUTPUT_INACTIVE);
    }
}

static void motor_block(void) {
    if (!device_is_ready(motor_gpio.port)) {
        return;
    }

    gpio_pin_set_dt(&motor_gpio, 0);
    gpio_pin_configure_dt(&motor_gpio, GPIO_INPUT | GPIO_PULL_DOWN);
}

static void led_make_available(void) {
    if (device_is_ready(rgb_enable_gpio.port)) {
        gpio_pin_configure_dt(&rgb_enable_gpio, GPIO_OUTPUT_INACTIVE);
    }
}

static void led_block(void) {
    if (!device_is_ready(rgb_enable_gpio.port)) {
        return;
    }

    gpio_pin_set_dt(&rgb_enable_gpio, 0);
    gpio_pin_configure_dt(&rgb_enable_gpio, GPIO_INPUT | GPIO_PULL_DOWN);
}

static void pulse_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!vibration_enabled || pulse_pattern_index >= pulse_pattern_length) {
        if (device_is_ready(motor_gpio.port)) {
            gpio_pin_set_dt(&motor_gpio, 0);
        }
        return;
    }

    const bool on = (pulse_pattern_index % 2U) == 0U;
    gpio_pin_set_dt(&motor_gpio, on ? 1 : 0);

    const uint16_t duration = pulse_pattern[pulse_pattern_index++];
    k_work_reschedule(k_work_delayable_from_work(work), K_MSEC(duration));
}

K_WORK_DELAYABLE_DEFINE(pulse_work, pulse_work_handler);

static void apply_settings_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (vibration_enabled) {
        motor_make_available();
    } else {
        motor_block();
    }

    if (led_enabled) {
        led_make_available();
    } else {
        led_block();
    }
}

static void led_disable_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (!led_enabled) {
        led_block();
    }
}

K_WORK_DELAYABLE_DEFINE(apply_settings_work, apply_settings_work_handler);
K_WORK_DELAYABLE_DEFINE(led_disable_work, led_disable_work_handler);

void endgame_cancel_status_feedback(void) {
    k_work_cancel_delayable(&pulse_work);
    pulse_pattern_length = 0U;
    pulse_pattern_index = 0U;

    if (device_is_ready(motor_gpio.port)) {
        gpio_pin_set_dt(&motor_gpio, 0);
    }
}

static void start_pattern(const uint16_t *pattern, uint8_t length) {
    /* A newer report always replaces an unfinished direct status pattern. */
    endgame_cancel_status_feedback();

    if (!vibration_enabled || length == 0U || length > MAX_PATTERN_ENTRIES) {
        return;
    }

    for (uint8_t i = 0; i < length; i++) {
        pulse_pattern[i] = pattern[i];
    }

    pulse_pattern_length = length;
    pulse_pattern_index = 0U;
    k_work_reschedule(&pulse_work, K_NO_WAIT);
}

static void append_pulse(uint8_t *length, uint16_t on_ms, uint16_t gap_ms) {
    if (*length != 0U) {
        pulse_pattern[(*length)++] = gap_ms;
    }
    pulse_pattern[(*length)++] = on_ms;
}

static void report_level(uint8_t level) {
    uint8_t length = 0U;
    const uint8_t tens = level / 10U;
    const uint8_t units = level % 10U;

    for (uint8_t i = 0; i < tens; i++) {
        append_pulse(&length, 260U, 180U);
    }
    for (uint8_t i = 0; i < units; i++) {
        append_pulse(&length, 60U, 75U);
    }

    start_pattern(pulse_pattern, length);
}

static void report_bluetooth(void) {
    uint8_t length = 0U;

    if (IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT) &&
        zmk_ble_active_profile_index() == (ZMK_BLE_PROFILE_COUNT - 1) &&
        zmk_esb_endpoint_is_active()) {
        append_pulse(&length, 600U, 0U);
    } else {
        const uint8_t profile = zmk_ble_active_profile_index() + 1U;
        for (uint8_t i = 0; i < profile; i++) {
            append_pulse(&length, 80U, 85U);
        }
    }

    start_pattern(pulse_pattern, length);
}

static void report_battery(void) {
    const uint8_t charge = zmk_battery_state_of_charge();
    const uint8_t long_pulses = charge / 25U;
    const uint8_t short_pulses = (charge % 25U) / 5U;
    uint8_t length = 0U;

    for (uint8_t i = 0; i < long_pulses; i++) {
        append_pulse(&length, 260U, 180U);
    }
    for (uint8_t i = 0; i < short_pulses; i++) {
        append_pulse(&length, 60U, 75U);
    }

    if (charge <= 20U) {
        zaf_custom_event_trigger(&status_battery_low);
    } else if (charge <= 50U) {
        zaf_custom_event_trigger(&status_battery_medium);
    } else {
        zaf_custom_event_trigger(&status_battery_high);
    }

    start_pattern(pulse_pattern, length);
}

static int feedback_settings_set(const char *name, size_t len,
                                 settings_read_cb read_cb, void *cb_arg) {
    uint8_t value;
    ssize_t read_len;

    if (!settings_name_steq(name, "vibration", NULL) &&
        !settings_name_steq(name, "led", NULL)) {
        return -ENOENT;
    }
    if (len != sizeof(value)) {
        return -EINVAL;
    }

    read_len = read_cb(cb_arg, &value, sizeof(value));
    if (read_len < 0) {
        return (int)read_len;
    }
    if (read_len != sizeof(value) || value > 1U) {
        return -EINVAL;
    }

    if (settings_name_steq(name, "vibration", NULL)) {
        vibration_enabled = value != 0U;
    } else {
        led_enabled = value != 0U;
    }
    return 0;
}

static int feedback_settings_commit(void) {
    k_work_reschedule(&apply_settings_work, K_MSEC(SETTINGS_APPLY_DELAY_MS));
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(endgame_feedback, "endgame/feedback", NULL,
                               feedback_settings_set,
                               feedback_settings_commit, NULL);

static void save_bool(const char *key, bool value) {
    const uint8_t stored = value ? 1U : 0U;
    settings_save_one(key, &stored, sizeof(stored));
}

static int on_endgame_control_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    switch (binding->param1) {
    case ACTION_REPORT_TWIST:
        zaf_custom_event_trigger(&status_twist);
        report_level(endgame_twist_sensitivity_level());
        break;
    case ACTION_REPORT_BLUETOOTH:
        zaf_custom_event_trigger(&status_bluetooth);
        report_bluetooth();
        break;
    case ACTION_REPORT_POINTER:
        zaf_custom_event_trigger(&status_pointer);
        report_level(endgame_pointer_sensitivity_level());
        break;
    case ACTION_REPORT_SCROLL: {
        const bool standard = endgame_standard_scroll_enabled();
        const uint16_t pattern_standard[] = {300U};
        const uint16_t pattern_high_res[] = {90U, 90U, 90U};

        zaf_custom_event_trigger(standard ? &status_scroll_standard
                                          : &status_scroll_high_res);
        if (standard) {
            start_pattern(pattern_standard, ARRAY_SIZE(pattern_standard));
        } else {
            start_pattern(pattern_high_res, ARRAY_SIZE(pattern_high_res));
        }
        break;
    }
    case ACTION_REPORT_BATTERY:
        report_battery();
        break;
    case ACTION_TOGGLE_VIBRATION:
        vibration_enabled = !vibration_enabled;
        save_bool("endgame/feedback/vibration", vibration_enabled);

        if (vibration_enabled) {
            const uint16_t pattern[] = {180U};
            motor_make_available();
            zaf_custom_event_trigger(&vibration_enabled_event);
            start_pattern(pattern, ARRAY_SIZE(pattern));
        } else {
            k_work_cancel_delayable(&pulse_work);
            motor_block();
            zaf_custom_event_trigger(&vibration_disabled_event);
        }
        break;
    case ACTION_TOGGLE_LED:
        led_enabled = !led_enabled;
        save_bool("endgame/feedback/led", led_enabled);

        if (led_enabled) {
            led_make_available();
            zaf_custom_event_trigger(&led_enabled_event);
        } else {
            zaf_custom_event_trigger(&led_disabled_event);
            k_work_reschedule(&led_disable_work,
                              K_MSEC(LED_DISABLE_DELAY_MS));
        }
        break;
    default:
        return -EINVAL;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int endgame_control_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api endgame_control_driver_api = {
    .binding_pressed = on_endgame_control_pressed,
};

#define ENDGAME_CONTROL_INST(n)                                         \
    BEHAVIOR_DT_INST_DEFINE(                                            \
        n, endgame_control_init, NULL, NULL, NULL, POST_KERNEL,         \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                            \
        &endgame_control_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ENDGAME_CONTROL_INST)
