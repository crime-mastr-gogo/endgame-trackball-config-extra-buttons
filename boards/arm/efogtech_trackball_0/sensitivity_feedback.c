#define DT_DRV_COMPAT zmk_behavior_sensitivity_feedback

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/device.h>

#include <drivers/behavior.h>
#include <drivers/p2sm_runtime.h>
#include <zmk/behavior.h>

#include <zmk_adaptive_feedback/adaptive_feedback.h>

static const float pointer_levels[] = {
    0.10f, 0.12f, 0.14f, 0.16f, 0.18f,
    0.20f, 0.23f, 0.26f, 0.29f, 0.35f,
    0.40f, 0.50f, 0.60f, 0.70f, 0.80f,
};

static const float twist_levels[] = {
    0.10f, 0.13f, 0.16f, 0.20f, 0.23f,
    0.26f, 0.30f, 0.35f, 0.40f, 0.50f,
    0.60f, 0.70f, 0.80f, 0.90f, 1.00f,
};
#define FLOAT_TOLERANCE 0.0001f

ZAF_CUSTOM_EVENT_DEFINE(pointer_sensitivity_increased,
                        "pointer-sensitivity-increased");
ZAF_CUSTOM_EVENT_DEFINE(pointer_sensitivity_decreased,
                        "pointer-sensitivity-decreased");
ZAF_CUSTOM_EVENT_DEFINE(pointer_sensitivity_lowest,
                        "pointer-sensitivity-lowest");
ZAF_CUSTOM_EVENT_DEFINE(twist_sensitivity_increased,
                        "twist-sensitivity-increased");
ZAF_CUSTOM_EVENT_DEFINE(twist_sensitivity_decreased,
                        "twist-sensitivity-decreased");
ZAF_CUSTOM_EVENT_DEFINE(twist_sensitivity_lowest,
                        "twist-sensitivity-lowest");

struct sensitivity_feedback_config {
    bool scroll;
    bool increase;
};

static bool value_is_lowest(float value, float minimum) {
    return value >= minimum - FLOAT_TOLERANCE &&
           value <= minimum + FLOAT_TOLERANCE;
}

static float calculate_new_value(float current, const float *levels,
                                 size_t level_count, bool increase) {
    if (increase) {
        for (size_t i = 0; i < level_count; i++) {
            if (levels[i] > current + FLOAT_TOLERANCE) {
                return levels[i];
            }
        }

        return levels[0];
    }

    for (size_t i = level_count; i > 0; i--) {
        if (levels[i - 1] < current - FLOAT_TOLERANCE) {
            return levels[i - 1];
        }
    }

    return levels[level_count - 1];
}

static void trigger_sensitivity_feedback(bool scroll, bool increase,
                                         bool lowest) {
    if (scroll) {
        if (lowest) {
            zaf_custom_event_trigger(&twist_sensitivity_lowest);
        } else if (increase) {
            zaf_custom_event_trigger(&twist_sensitivity_increased);
        } else {
            zaf_custom_event_trigger(&twist_sensitivity_decreased);
        }

        return;
    }

    if (lowest) {
        zaf_custom_event_trigger(&pointer_sensitivity_lowest);
    } else if (increase) {
        zaf_custom_event_trigger(&pointer_sensitivity_increased);
    } else {
        zaf_custom_event_trigger(&pointer_sensitivity_decreased);
    }
}

static int on_sensitivity_feedback_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    ARG_UNUSED(event);

    const struct device *dev =
        zmk_behavior_get_binding(binding->behavior_dev);
    const struct sensitivity_feedback_config *config = dev->config;

    const float *levels =
        config->scroll ? twist_levels : pointer_levels;
    const size_t level_count =
        config->scroll
            ? sizeof(twist_levels) / sizeof(twist_levels[0])
            : sizeof(pointer_levels) / sizeof(pointer_levels[0]);
    const float minimum = levels[0];
    const float current =
        config->scroll ? p2sm_get_twist_coef() : p2sm_get_move_coef();
    const float new_value =
        calculate_new_value(current, levels, level_count,
                            config->increase);

    if (config->scroll) {
        p2sm_set_twist_coef(new_value);
    } else {
        p2sm_set_move_coef(new_value);
    }

    trigger_sensitivity_feedback(
        config->scroll,
        config->increase,
        value_is_lowest(new_value, minimum));

    return ZMK_BEHAVIOR_OPAQUE;
}

static int sensitivity_feedback_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api sensitivity_feedback_driver_api = {
    .binding_pressed = on_sensitivity_feedback_pressed,
};

#define SENSITIVITY_FEEDBACK_INST(n)                                    \
    static const struct sensitivity_feedback_config                     \
        sensitivity_feedback_config_##n = {                             \
            .scroll = DT_INST_PROP_OR(n, scroll, false),                \
            .increase = DT_INST_PROP_OR(n, increase, false),            \
        };                                                              \
    BEHAVIOR_DT_INST_DEFINE(                                            \
        n, sensitivity_feedback_init, NULL, NULL,                       \
        &sensitivity_feedback_config_##n, POST_KERNEL,                  \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                            \
        &sensitivity_feedback_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SENSITIVITY_FEEDBACK_INST)