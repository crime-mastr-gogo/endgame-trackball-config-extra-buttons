#define DT_DRV_COMPAT zmk_behavior_device_action_feedback
#include <errno.h>

#include <zephyr/device.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include <zmk_adaptive_feedback/adaptive_feedback.h>

#define DEVICE_ACTION_CLEAR_CURRENT_BT 0
#define DEVICE_ACTION_CLEAR_ALL_BT 1
#define DEVICE_ACTION_POWER_OFF 2

ZAF_CUSTOM_EVENT_DEFINE(clear_current_bt_feedback,
                        "clear-current-bt-feedback");
ZAF_CUSTOM_EVENT_DEFINE(clear_all_bt_feedback,
                        "clear-all-bt-feedback");
ZAF_CUSTOM_EVENT_DEFINE(power_off_feedback,
                        "power-off-feedback");

static int on_device_action_feedback_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    ARG_UNUSED(event);

    switch (binding->param1) {
    case DEVICE_ACTION_CLEAR_CURRENT_BT:
        zaf_custom_event_trigger(&clear_current_bt_feedback);
        break;

    case DEVICE_ACTION_CLEAR_ALL_BT:
        zaf_custom_event_trigger(&clear_all_bt_feedback);
        break;

    case DEVICE_ACTION_POWER_OFF:
        zaf_custom_event_trigger(&power_off_feedback);
        break;

    default:
        return -EINVAL;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int device_action_feedback_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api device_action_feedback_driver_api = {
    .binding_pressed = on_device_action_feedback_pressed,
};

#define DEVICE_ACTION_FEEDBACK_INST(n)                                  \
    BEHAVIOR_DT_INST_DEFINE(                                            \
        n, device_action_feedback_init, NULL, NULL, NULL,               \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,               \
        &device_action_feedback_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DEVICE_ACTION_FEEDBACK_INST)