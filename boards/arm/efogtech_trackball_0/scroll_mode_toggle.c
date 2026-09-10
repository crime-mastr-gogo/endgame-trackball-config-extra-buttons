#define DT_DRV_COMPAT zmk_behavior_scroll_mode_toggle

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include <zmk_adaptive_feedback/adaptive_feedback.h>

#define STANDARD_SCROLL_LAYER 6
#define FEEDBACK_DELAY_MS 30
#define RESTORE_DELAY_MS 250
#define SETTINGS_SAVE_DELAY_MS 2500

/*
 * Standard scrolling is the compiled fallback.
 *
 * true  = standard scrolling
 * false = high-resolution scrolling
 */
static bool standard_scroll_enabled = true;

ZAF_CUSTOM_EVENT_DEFINE(standard_scroll_selected,
                        "standard-scroll-selected");
ZAF_CUSTOM_EVENT_DEFINE(high_res_scroll_selected,
                        "high-res-scroll-selected");

static void standard_scroll_feedback_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    zaf_custom_event_trigger(&standard_scroll_selected);
}

static void high_res_scroll_feedback_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    zaf_custom_event_trigger(&high_res_scroll_selected);
}

static void scroll_mode_save_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t stored_value = standard_scroll_enabled ? 1U : 0U;

    settings_save_one("endgame/scroll/standard",
                      &stored_value,
                      sizeof(stored_value));
}

static void scroll_mode_restore_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    /*
     * Restore silently: waking or booting should not produce the
     * "scroll mode changed" feedback.
     */
    if (standard_scroll_enabled) {
        zmk_keymap_layer_activate(STANDARD_SCROLL_LAYER);
    } else {
        zmk_keymap_layer_deactivate(STANDARD_SCROLL_LAYER);
    }
}

K_WORK_DELAYABLE_DEFINE(standard_scroll_feedback_work,
                        standard_scroll_feedback_work_handler);
K_WORK_DELAYABLE_DEFINE(high_res_scroll_feedback_work,
                        high_res_scroll_feedback_work_handler);
K_WORK_DELAYABLE_DEFINE(scroll_mode_save_work,
                        scroll_mode_save_work_handler);
K_WORK_DELAYABLE_DEFINE(scroll_mode_restore_work,
                        scroll_mode_restore_work_handler);

static void schedule_scroll_mode_save(void) {
    k_work_reschedule(&scroll_mode_save_work,
                      K_MSEC(SETTINGS_SAVE_DELAY_MS));
}

static int scroll_mode_settings_set(
    const char *name,
    size_t len,
    settings_read_cb read_cb,
    void *cb_arg) {

    uint8_t stored_value;
    ssize_t read_len;

    if (!settings_name_steq(name, "standard", NULL)) {
        return -ENOENT;
    }

    if (len != sizeof(stored_value)) {
        return -EINVAL;
    }

    read_len = read_cb(cb_arg, &stored_value, sizeof(stored_value));

    if (read_len < 0) {
        return (int)read_len;
    }

    if (read_len != sizeof(stored_value)) {
        return -EINVAL;
    }

    if (stored_value > 1U) {
        return -EINVAL;
    }

    standard_scroll_enabled = stored_value == 1U;
    return 0;
}

static int scroll_mode_settings_commit(void) {
    /*
     * Delay restoration briefly so that the keymap and its layers are
     * fully initialized before layer 6 is changed.
     */
    k_work_reschedule(&scroll_mode_restore_work,
                      K_MSEC(RESTORE_DELAY_MS));
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(
    endgame_scroll_mode,
    "endgame/scroll",
    NULL,
    scroll_mode_settings_set,
    scroll_mode_settings_commit,
    NULL);

static int on_scroll_mode_toggle_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (zmk_keymap_layer_active(STANDARD_SCROLL_LAYER)) {
        int rc = zmk_keymap_layer_deactivate(STANDARD_SCROLL_LAYER);

        if (rc == 0) {
            standard_scroll_enabled = false;
            schedule_scroll_mode_save();

            k_work_reschedule(&high_res_scroll_feedback_work,
                              K_MSEC(FEEDBACK_DELAY_MS));
        }

        return rc;
    }

    int rc = zmk_keymap_layer_activate(STANDARD_SCROLL_LAYER);

    if (rc == 0) {
        standard_scroll_enabled = true;
        schedule_scroll_mode_save();

        k_work_reschedule(&standard_scroll_feedback_work,
                          K_MSEC(FEEDBACK_DELAY_MS));
    }

    return rc;
}

static int scroll_mode_toggle_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api scroll_mode_toggle_driver_api = {
    .binding_pressed = on_scroll_mode_toggle_pressed,
};

#define SCROLL_MODE_TOGGLE_INST(n)                                      \
    BEHAVIOR_DT_INST_DEFINE(                                            \
        n, scroll_mode_toggle_init, NULL, NULL, NULL,                   \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,               \
        &scroll_mode_toggle_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCROLL_MODE_TOGGLE_INST)
