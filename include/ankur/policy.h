#pragma once
#include <stdint.h>
#include <stdbool.h>
#define ANKUR_LEVELS 20
#define ANKUR_STANDARD_LAYER 6
#define ANKUR_GUARD_MS 2000
enum ankur_action {
    POINTER_UP, POINTER_DOWN, TWIST_UP, TWIST_DOWN, SCROLL_TOGGLE,
    DRAG_TOGGLE, REPORT_POINTER, REPORT_TWIST, REPORT_CONNECTION,
    REPORT_SCROLL, REPORT_BATTERY, TOGGLE_LED, TOGGLE_VIBRATION,
    RESET_SENSITIVITY, CLEAR_CURRENT, CLEAR_ALL, POWER_OFF,
    ANKUR_ACTION_COUNT
};
struct ankur_settings {
    uint8_t version, pointer, twist, standard, led, vibration;
};
static inline bool ankur_settings_valid(const struct ankur_settings *s) {
    return s->version == 1 && s->pointer < ANKUR_LEVELS &&
        s->twist < ANKUR_LEVELS && s->standard <= 1 &&
        s->led <= 1 && s->vibration <= 1;
}
static inline uint8_t ankur_step(uint8_t level, bool up) {
    if (level >= ANKUR_LEVELS) level = 0;
    return up ? (level < ANKUR_LEVELS - 1 ? level + 1 : level) :
                (level ? level - 1 : 0);
}
static inline bool ankur_guarded(unsigned action) {
    return action >= RESET_SENSITIVITY && action < ANKUR_ACTION_COUNT;
}
