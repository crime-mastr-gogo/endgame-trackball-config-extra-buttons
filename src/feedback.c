/* SPDX-License-Identifier: MIT */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <drivers/ext_power.h>
#include "ankur/feedback.h"

/* Dedicated low-priority queue: SPI LED transfers never run in the input path.
 * One replaceable notification slot bounds memory and prevents stale backlogs. */
K_THREAD_STACK_DEFINE(feedback_stack, 1024);
static struct k_work_q feedback_queue;
static struct k_spinlock state_lock;
static struct ankur_pattern active;
static int64_t started;
static bool show_led, run_motor, mode, ready;
static uint32_t revision;
static const struct device *strip = DEVICE_DT_GET(DT_NODELABEL(led_strip));
static const struct device *motor_port = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *power = DEVICE_DT_GET(DT_NODELABEL(epwr));
static struct led_rgb pixels[16], previous[16];
static bool previous_valid;

static struct led_rgb color_at(uint8_t color, uint32_t elapsed) {
    static const struct led_rgb colors[] = {
        {127,0,0}, {0,127,0}, {0,0,127}, {0,127,127}, {127,0,127},
        {64,0,127}, {127,40,0}, {0,60,0}, {64,127,32}
    };
    if (color < AK_RAINBOW) return colors[color];
    uint32_t hue = (elapsed / 12) % 768;
    uint8_t ramp = hue % 128;
    switch (hue / 128) {
    case 0: return (struct led_rgb){127,ramp,0};
    case 1: return (struct led_rgb){127-ramp,127,0};
    case 2: return (struct led_rgb){0,127,ramp};
    case 3: return (struct led_rgb){0,127-ramp,127};
    case 4: return (struct led_rgb){ramp,0,127};
    default: return (struct led_rgb){127,0,127-ramp};
    }
}

static void tick(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(feedback_tick, tick);

static void tick(struct k_work *work) {
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    struct ankur_pattern p = active;
    uint32_t rev = revision;
    uint32_t elapsed = (uint32_t)(k_uptime_get() - started);
    bool led_enabled = show_led, motor_enabled = run_motor, held = mode;
    k_spin_unlock(&state_lock, key);
    uint32_t total = ankur_duration(&p);
    /* A zero-pulse battery report still has a visible acknowledgment. */
    if (!total && p.color == AK_RED && led_enabled) total = 250;
    bool notification = elapsed < total;
    bool on = false;
    unsigned phase = 0;
    uint32_t boundary = 0;
    while (phase < p.count) {
        boundary += p.ms[phase];
        if (elapsed < boundary) { on = !(phase & 1); break; }
        ++phase;
    }
    gpio_pin_set(motor_port, 24, notification && motor_enabled && on);
    struct led_rgb rgb = {0};
    if (led_enabled && notification) {
        rgb = color_at(p.color, elapsed);
        if (p.effect == AK_FADE && total) {
            rgb.r = rgb.r * (total-elapsed) / total;
            rgb.g = rgb.g * (total-elapsed) / total;
            rgb.b = rgb.b * (total-elapsed) / total;
        } else if (p.effect == AK_FLASH && p.count && !on) {
            rgb = (struct led_rgb){0};
        }
    } else if (held && led_enabled) {
        rgb = color_at(AK_RAINBOW, elapsed);
    }
    if ((notification && p.effect == AK_BREATHE) || (!notification && held)) {
        /* Smoothstep triangle: continuous, zero-slope endpoints, no floating point. */
        uint32_t t = elapsed % 2000;
        uint32_t x = t < 1000 ? t : 2000-t;
        uint32_t level = (x*x*(3000-2*x))/1000000;
        rgb.r = rgb.r * level / 1000;
        rgb.g = rgb.g * level / 1000;
        rgb.b = rgb.b * level / 1000;
    }
    for (size_t i=0;i<ARRAY_SIZE(pixels);++i) pixels[i]=rgb;
    if (!previous_valid || memcmp(previous,pixels,sizeof(pixels))) {
        if (device_is_ready(strip) && led_strip_update_rgb(strip,pixels,ARRAY_SIZE(pixels)) == 0) {
            memcpy(previous,pixels,sizeof(pixels));
            previous_valid=true;
        }
    }
    key = k_spin_lock(&state_lock);
    bool changed = revision != rev;
    k_spin_unlock(&state_lock,key);
    if (notification || held || changed) {
        /* Motor boundaries are scheduled exactly; LED frames at most every 20 ms. */
        uint32_t delay = 20;
        if (notification && boundary > elapsed) delay = MIN(delay,boundary-elapsed);
        k_work_reschedule_for_queue(&feedback_queue,&feedback_tick,K_MSEC(changed?1:delay));
    }
}

void ankur_feedback_play(struct ankur_pattern p, bool leds, bool vibration) {
    if (!ready || p.count > ANKUR_PATTERN_MAX) return;
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    active=p; started=k_uptime_get(); show_led=leds; run_motor=vibration; ++revision;
    k_spin_unlock(&state_lock,key);
    k_work_reschedule_for_queue(&feedback_queue,&feedback_tick,K_NO_WAIT);
}

void ankur_feedback_mode(bool enabled) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    mode=enabled; ++revision;
    k_spin_unlock(&state_lock,key);
    if (ready) k_work_reschedule_for_queue(&feedback_queue,&feedback_tick,K_NO_WAIT);
}

void ankur_feedback_stop(void) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    active=(struct ankur_pattern){0}; mode=false; show_led=false; run_motor=false; ++revision;
    k_spin_unlock(&state_lock,key);
    if (ready) k_work_reschedule_for_queue(&feedback_queue,&feedback_tick,K_NO_WAIT);
}

static int feedback_init(void) {
    if (!device_is_ready(motor_port) || !device_is_ready(power)) return -ENODEV;
    int rc=gpio_pin_configure(motor_port,24,GPIO_OUTPUT_INACTIVE);
    if (rc) return rc;
    rc=ext_power_enable(power);
    if (rc) return rc;
    k_work_queue_start(&feedback_queue,feedback_stack,K_THREAD_STACK_SIZEOF(feedback_stack),
                       K_PRIO_PREEMPT(10),NULL);
    ready=true;
    return 0;
}
SYS_INIT(feedback_init, APPLICATION, 95);
