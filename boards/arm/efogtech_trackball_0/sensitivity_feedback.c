#define DT_DRV_COMPAT zmk_behavior_sensitivity_feedback

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/device.h>

#include <drivers/behavior.h>
#include <drivers/p2sm_runtime.h>
#include <zmk/behavior.h>

#include <zmk_adaptive_feedback/adaptive_feedback.h>

static const float pointer_levels[] = {
    /*
     * Levels 1-10: 0.10 to 0.25
     * Interval: approximately 0.016667
     */
    0.100000f, 0.116667f, 0.133333f, 0.150000f, 0.166667f,
    0.183333f, 0.200000f, 0.216667f, 0.233333f, 0.250000f,

    /*
     * Levels 11-20: above 0.25 to 0.80
     * Interval: 0.055
     */
    0.305000f, 0.360000f, 0.415000f, 0.470000f, 0.525000f,
    0.580000f, 0.635000f, 0.690000f, 0.745000f, 0.800000f,
};

static const float twist_levels[] = {
    /*
     * Levels 1-10: 0.10 to 0.25
     * Interval: approximately 0.016667
     */
    0.100000f, 0.116667f, 0.133333f, 0.150000f, 0.166667f,
    0.183333f, 0.200000f, 0.216667f, 0.233333f, 0.250000f,

    /*
     * Levels 11-20: above 0.25 to 1.00
     * Interval: 0.075
     */
    0.325000f, 0.400000f, 0.475000f, 0.550000f, 0.625000f,
    0.700000f, 0.775000f, 0.850000f, 0.925000f, 1.000000f,
};

#define FLOAT_TOLERANCE 0.0001f
#define DEFAULT_POINTER_SENSITIVITY 0.200000f
#define DEFAULT_TWIST_SENSITIVITY 0.166667f

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
ZAF_CUSTOM_EVENT_DEFINE(sensitivity_reset,
                        "sensitivity-reset");

struct sensitivity_feedback_config {
    bool scroll;
    bool increase;
    bool reset;
};

static bool value_is_endpoint(float value, float minimum,
                              float maximum) {
    const bool at_minimum =
        value >= minimum - FLOAT_TOLERANCE &&
        value <= minimum + FLOAT_TOLERANCE;

    const bool at_maximum =
        value >= maximum - FLOAT_TOLERANCE &&
        value <= maximum + FLOAT_TOLERANCE;

    return at_minimum || at_maximum;
}

static float calculate_new_value(float current, const float *levels,
                                 size_t level_count, bool increase) {
    if (increase) {
        for (size_t i = 0; i < level_count; i++) {
            if (levels[i] > current + FLOAT_TOLERANCE) {
                return levels[i];
            }
        }

        /* Already at or above maximum: remain at maximum. */
        return levels[level_count - 1];
    }

    for (size_t i = level_count; i > 0; i--) {
        if (levels[i - 1] < current - FLOAT_TOLERANCE) {
            return levels[i - 1];
        }
    }

    /* Already at or below minimum: remain at minimum. */
    return levels[0];
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

    if (config->reset) {
        p2sm_set_move_coef(DEFAULT_POINTER_SENSITIVITY);
        p2sm_set_twist_coef(DEFAULT_TWIST_SENSITIVITY);
        zaf_custom_event_trigger(&sensitivity_reset);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    const float *levels =
        config->scroll ? twist_levels : pointer_levels;
    const size_t level_count =
        config->scroll
            ? sizeof(twist_levels) / sizeof(twist_levels[0])
            : sizeof(pointer_levels) / sizeof(pointer_levels[0]);
    const float minimum = levels[0];
    const float maximum = levels[level_count - 1];
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
        value_is_endpoint(new_value, minimum, maximum));

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
            .reset = DT_INST_PROP_OR(n, reset, false),                  \
        };                                                              \
    BEHAVIOR_DT_INST_DEFINE(                                            \
        n, sensitivity_feedback_init, NULL, NULL,                       \
        &sensitivity_feedback_config_##n, POST_KERNEL,                  \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                            \
        &sensitivity_feedback_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SENSITIVITY_FEEDBACK_INST)
