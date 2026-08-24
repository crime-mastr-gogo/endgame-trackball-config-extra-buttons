#define DT_DRV_COMPAT zmk_behavior_scroll_mode_toggle

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include <zmk_adaptive_feedback/adaptive_feedback.h>

#define STANDARD_SCROLL_LAYER 6
#define FLASH_COUNT 3
#define FLASH_INTERVAL_MS 250

ZAF_CUSTOM_EVENT_DEFINE(standard_scroll_selected, "standard-scroll-selected");
ZAF_CUSTOM_EVENT_DEFINE(high_res_scroll_selected, "high-res-scroll-selected");

static bool flash_standard_mode;
static uint8_t flashes_remaining;
static void scroll_mode_feedback_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(scroll_mode_feedback_work,
                        scroll_mode_feedback_work_handler);

static void trigger_selected_feedback(void) {
    if (flash_standard_mode) {
        zaf_custom_event_trigger(&standard_scroll_selected);
    } else {
        zaf_custom_event_trigger(&high_res_scroll_selected);
    }
}

static void scroll_mode_feedback_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (flashes_remaining == 0) {
        return;
    }

    trigger_selected_feedback();
    flashes_remaining--;

    if (flashes_remaining > 0) {
        k_work_reschedule(&scroll_mode_feedback_work,
                          K_MSEC(FLASH_INTERVAL_MS));
    }
}


static void start_scroll_mode_feedback(bool standard_mode) {
    k_work_cancel_delayable(&scroll_mode_feedback_work);

    flash_standard_mode = standard_mode;
    flashes_remaining = FLASH_COUNT;

    trigger_selected_feedback();
    flashes_remaining--;

    if (flashes_remaining > 0) {
        k_work_reschedule(&scroll_mode_feedback_work,
                          K_MSEC(FLASH_INTERVAL_MS));
    }
}

static int on_scroll_mode_toggle_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (zmk_keymap_layer_active(STANDARD_SCROLL_LAYER)) {
        int rc = zmk_keymap_layer_deactivate(STANDARD_SCROLL_LAYER);

        if (rc == 0) {
            start_scroll_mode_feedback(false);
        }

        return rc;
    }

    int rc = zmk_keymap_layer_activate(STANDARD_SCROLL_LAYER);

    if (rc == 0) {
        start_scroll_mode_feedback(true);
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