/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ANKUR_LEVELS 20
#define ANKUR_POINTER_DEFAULT 7
#define ANKUR_TWIST_DEFAULT 5
#define ANKUR_PATTERN_MAX 15
#define ANKUR_SETTINGS_VERSION 1
#define ANKUR_LED_MAX 127

/* Millionths, so the documented six-decimal coefficients are exact. */
static const int32_t ankur_pointer_coeff[ANKUR_LEVELS] = {
    100000,116667,133333,150000,166667,183333,200000,216667,233333,250000,
    305000,360000,415000,470000,525000,580000,635000,690000,745000,800000
};
static const int32_t ankur_twist_coeff[ANKUR_LEVELS] = {
    100000,116667,133333,150000,166667,183333,200000,216667,233333,250000,
    325000,400000,475000,550000,625000,700000,775000,850000,925000,1000000
};

struct ankur_preferences {
    uint8_t version, pointer, twist, high_res, leds, vibration;
};
#define ANKUR_DEFAULTS { ANKUR_SETTINGS_VERSION, 7, 5, 0, 1, 1 }

static inline bool ankur_preferences_valid(const struct ankur_preferences *p) {
    return p->version == ANKUR_SETTINGS_VERSION && p->pointer >= 1 &&
        p->pointer <= 20 && p->twist >= 1 && p->twist <= 20 &&
        p->high_res <= 1 && p->leds <= 1 && p->vibration <= 1;
}

static inline uint8_t ankur_level_step(uint8_t level, bool up) {
    if (level < 1) level = 1;
    if (level > 20) level = 20;
    return up ? (level < 20 ? level + 1 : 20) : (level > 1 ? level - 1 : 1);
}

/* Signed fixed-point scaling; separate remainder for every axis and mode.
 * 64-bit intermediate prevents overflow at full sensor delta and sensitivity. */
static inline int32_t ankur_scale(int32_t value, int32_t coefficient, int64_t *rem) {
    int64_t v = (int64_t)value * coefficient + *rem;
    int32_t out = (int32_t)(v / 1000000);
    *rem = v - (int64_t)out * 1000000;
    return out;
}

enum ankur_color { AK_RED, AK_GREEN, AK_BLUE, AK_CYAN, AK_MAGENTA,
    AK_PURPLE, AK_ORANGE, AK_DARK_GREEN, AK_LIGHT_GREEN, AK_RAINBOW };
enum ankur_effect { AK_FLASH, AK_BREATHE, AK_FADE };
struct ankur_pattern {
    uint16_t ms[ANKUR_PATTERN_MAX];
    uint8_t count, color, effect;
};

static inline uint32_t ankur_duration(const struct ankur_pattern *p) {
    uint32_t duration = 0;
    for (uint8_t i = 0; i < p->count; ++i) duration += p->ms[i];
    return duration;
}

static inline struct ankur_pattern ankur_report(unsigned value, unsigned group,
                                                uint8_t color) {
    struct ankur_pattern p = { .color = color, .effect = AK_FLASH };
    if (!group) return p;
    unsigned longs = value / group, shorts = value % group;
    for (unsigned i = 0; i < longs + shorts && p.count < ANKUR_PATTERN_MAX; ++i) {
        if (p.count) p.ms[p.count++] = 150;
        p.ms[p.count++] = i < longs ? 250 : 100;
    }
    return p;
}

static inline struct ankur_pattern ankur_battery_report(unsigned percent) {
    if (percent > 100) percent = 100;
    uint8_t color = percent > 75 ? AK_DARK_GREEN : percent >= 50 ? AK_LIGHT_GREEN :
                    percent >= 25 ? AK_ORANGE : AK_RED;
    return ankur_report(percent / 5, 5, color);
}

static inline struct ankur_pattern ankur_profile_report(unsigned profile) {
    static const uint8_t colors[] = {AK_RED,AK_PURPLE,AK_GREEN,AK_BLUE,AK_MAGENTA};
    struct ankur_pattern p = { .effect = AK_FLASH };
    if (profile > 5) return p;
    if (profile == 5) {
        p = (struct ankur_pattern){ .ms={250,150,250,150,250}, .count=5,
                                    .color=AK_RAINBOW, .effect=AK_BREATHE };
    } else {
        p.color = colors[profile];
        p.count = 2 * profile + 1;
        for (uint8_t i = 0; i < p.count; ++i) p.ms[i] = 150;
    }
    return p;
}

enum ankur_feedback_event {
    AK_POINTER_UP, AK_POINTER_DOWN, AK_POINTER_MIN, AK_POINTER_MAX,
    AK_TWIST_UP, AK_TWIST_DOWN, AK_TWIST_MIN, AK_TWIST_MAX,
    AK_STANDARD, AK_HIGH_RES, AK_CONTROL_LAYER, AK_DEVICE_LAYER,
    AK_FEEDBACK_LAYER, AK_STATUS_LAYER, AK_DRAG_LOCK, AK_POWER_OFF,
    AK_STUDIO_UNLOCK, AK_CLEAR_CURRENT, AK_CLEAR_ALL, AK_RESET_SENS,
    AK_VIB_ON, AK_VIB_OFF, AK_LED_ON, AK_LED_OFF, AK_USB_ON, AK_USB_OFF,
    AK_EVENT_COUNT
};
#define AK_P(c,e,...) { .ms={__VA_ARGS__}, .count=sizeof((uint16_t[]){__VA_ARGS__})/sizeof(uint16_t), .color=c, .effect=e }
static const struct ankur_pattern ankur_patterns[AK_EVENT_COUNT] = {
    [AK_POINTER_UP]=AK_P(AK_RED,AK_FLASH,100,150,250),
    [AK_POINTER_DOWN]=AK_P(AK_GREEN,AK_FLASH,250,150,100),
    [AK_POINTER_MIN]=AK_P(AK_RAINBOW,AK_BREATHE,100,150,100,150,100),
    [AK_POINTER_MAX]=AK_P(AK_RAINBOW,AK_BREATHE,250,150,250,150,250),
    [AK_TWIST_UP]=AK_P(AK_MAGENTA,AK_FLASH,100,150,250),
    [AK_TWIST_DOWN]=AK_P(AK_CYAN,AK_FLASH,250,150,100),
    [AK_TWIST_MIN]=AK_P(AK_RAINBOW,AK_BREATHE,100,150,100,150,100),
    [AK_TWIST_MAX]=AK_P(AK_RAINBOW,AK_BREATHE,250,150,250,150,250),
    [AK_STANDARD]=AK_P(AK_GREEN,AK_FLASH,100,150,150,150,250),
    [AK_HIGH_RES]=AK_P(AK_RED,AK_FLASH,250,150,250,150,100),
    [AK_CONTROL_LAYER]=AK_P(AK_BLUE,AK_FLASH,150),
    [AK_DEVICE_LAYER]=AK_P(AK_RED,AK_FLASH,150,150,150),
    [AK_FEEDBACK_LAYER]=AK_P(AK_GREEN,AK_FLASH,150,150,150,150,150),
    [AK_STATUS_LAYER]=AK_P(AK_MAGENTA,AK_FLASH,150,150,150,150,150,150,150),
    [AK_DRAG_LOCK]=AK_P(AK_PURPLE,AK_FLASH,150,150,150),
    [AK_POWER_OFF]=AK_P(AK_RED,AK_FADE,500),
    [AK_STUDIO_UNLOCK]=AK_P(AK_CYAN,AK_FLASH,150,150,150),
    [AK_CLEAR_CURRENT]=AK_P(AK_BLUE,AK_FLASH,150,150,150,150,150),
    [AK_CLEAR_ALL]=AK_P(AK_RED,AK_FLASH,250,150,250,150,250,150,250),
    [AK_RESET_SENS]=AK_P(AK_RAINBOW,AK_BREATHE,150,150,150,150,150),
    [AK_VIB_ON]=AK_P(AK_GREEN,AK_FLASH,250,150,250,150,250),
    [AK_VIB_OFF]=AK_P(AK_RED,AK_FLASH,100,150,100,150,100),
    [AK_LED_ON]=AK_P(AK_CYAN,AK_FLASH,250,150,250,150,250),
    [AK_LED_OFF]=AK_P(AK_ORANGE,AK_FLASH,100,150,100,150,100),
    [AK_USB_ON]=AK_P(AK_GREEN,AK_FLASH,250),
    [AK_USB_OFF]=AK_P(AK_BLUE,AK_FLASH,150,150,150)
};
#undef AK_P
