#define DT_DRV_COMPAT zmk_behavior_drag_lock

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include <zmk_adaptive_feedback/adaptive_feedback.h>

extern void endgame_cancel_status_feedback(void);

ZAF_CUSTOM_EVENT_DEFINE(drag_lock_enabled,
                        "drag-lock-enabled");
ZAF_CUSTOM_EVENT_DEFINE(drag_lock_disabled,
                        "drag-lock-disabled");

struct drag_lock_data {
    bool enabled;
};

static int on_drag_lock_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    ARG_UNUSED(event);
    endgame_cancel_status_feedback();

    const struct device *dev =
        zmk_behavior_get_binding(binding->behavior_dev);
    struct drag_lock_data *data = dev->data;
    const bool new_state = !data->enabled;

    const int result =
        input_report_key(DEVICE_DT_GET(DT_NODELABEL(mkp)),
                         INPUT_BTN_0,
                         new_state ? 1 : 0,
                         true,
                         K_FOREVER);

    if (result < 0) {
        return result;
    }

    data->enabled = new_state;

    if (data->enabled) {
        zaf_custom_event_trigger(&drag_lock_enabled);
    } else {
        zaf_custom_event_trigger(&drag_lock_disabled);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int drag_lock_init(const struct device *dev) {
    struct drag_lock_data *data = dev->data;
    data->enabled = false;
    return 0;
}

static const struct behavior_driver_api drag_lock_driver_api = {
    .binding_pressed = on_drag_lock_pressed,
};

#define DRAG_LOCK_INST(n)                                              \
    static struct drag_lock_data drag_lock_data_##n = {};              \
    BEHAVIOR_DT_INST_DEFINE(                                           \
        n, drag_lock_init, NULL, &drag_lock_data_##n, NULL,            \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,              \
        &drag_lock_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DRAG_LOCK_INST)
