#define DT_DRV_COMPAT zmk_behavior_scroll_mode_toggle

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include <zmk_adaptive_feedback/adaptive_feedback.h>

#define STANDARD_SCROLL_LAYER 6
#define FEEDBACK_DELAY_MS 30

ZAF_CUSTOM_EVENT_DEFINE(standard_scroll_selected, "standard-scroll-selected");
ZAF_CUSTOM_EVENT_DEFINE(high_res_scroll_selected, "high-res-scroll-selected");

static void standard_scroll_feedback_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    zaf_custom_event_trigger(&standard_scroll_selected);
}

static void high_res_scroll_feedback_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    zaf_custom_event_trigger(&high_res_scroll_selected);
}

K_WORK_DELAYABLE_DEFINE(standard_scroll_feedback_work,
                        standard_scroll_feedback_work_handler);
K_WORK_DELAYABLE_DEFINE(high_res_scroll_feedback_work,
                        high_res_scroll_feedback_work_handler);

static int on_scroll_mode_toggle_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    if (zmk_keymap_layer_active(STANDARD_SCROLL_LAYER)) {
        int rc = zmk_keymap_layer_deactivate(STANDARD_SCROLL_LAYER);

        if (rc == 0) {
            k_work_reschedule(&high_res_scroll_feedback_work,
                              K_MSEC(FEEDBACK_DELAY_MS));
        }

        return rc;
    }

    int rc = zmk_keymap_layer_activate(STANDARD_SCROLL_LAYER);

    if (rc == 0) {
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