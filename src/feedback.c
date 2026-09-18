/* SPDX-License-Identifier: MIT */
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ankur/feedback.h"

LOG_MODULE_REGISTER(ankur_feedback, CONFIG_ZMK_LOG_LEVEL);

/*
 * Feedback hardware ownership:
 *
 *   P0.24 = vibration output
 *   P1.00 = feedback 3V3 rail
 *   P1.03 = RGB enable
 *
 * No other firmware component configures these pins.
 *
 * Work runs on a dedicated low-priority queue so SPI transfers and the 5 ms
 * rail-start delay never execute in the pointing/input path.
 */
K_THREAD_STACK_DEFINE(feedback_stack, 1024);

static struct k_work_q feedback_queue;
static struct k_spinlock state_lock;

static struct ankur_pattern active;
static int64_t started;

static bool show_led;
static bool run_motor;
static bool mode;
static bool ready;

static uint32_t revision;

static const struct device *strip =
    DEVICE_DT_GET(DT_NODELABEL(led_strip));

static const struct device *motor_port =
    DEVICE_DT_GET(DT_NODELABEL(gpio0));

static const struct device *power_port =
    DEVICE_DT_GET(DT_NODELABEL(gpio1));

static struct led_rgb pixels[16];
static struct led_rgb previous[16];

static bool previous_valid;
static bool rail_on;
static bool rgb_on;

static bool power_fault_logged;
static bool led_fault_logged;
static bool motor_fault_logged;


static struct led_rgb color_at(uint8_t color, uint32_t elapsed) {
    static const struct led_rgb colors[] = {
        {127, 0, 0},
        {0, 127, 0},
        {0, 0, 127},
        {0, 127, 127},
        {127, 0, 127},
        {64, 0, 127},
        {127, 40, 0},
        {0, 60, 0},
        {64, 127, 32},
    };

    if (color < AK_RAINBOW) {
        return colors[color];
    }

    uint32_t hue = (elapsed / 12) % 768;
    uint8_t ramp = hue % 128;

    switch (hue / 128) {
    case 0:
        return (struct led_rgb){127, ramp, 0};
    case 1:
        return (struct led_rgb){127 - ramp, 127, 0};
    case 2:
        return (struct led_rgb){0, 127, ramp};
    case 3:
        return (struct led_rgb){0, 127 - ramp, 127};
    case 4:
        return (struct led_rgb){ramp, 0, 127};
    default:
        return (struct led_rgb){127, 0, 127 - ramp};
    }
}


/*
 * Only enables hardware here. Disabling happens after motor/LED outputs have
 * first been driven to zero, preventing the next power-up from inheriting a
 * stale WS2812 state.
 */
static int feedback_power_prepare(bool need_power, bool need_led) {
    int rc;

    if (need_power && !rail_on) {
        rc = gpio_pin_set(power_port, 0, 1);
        if (rc) {
            return rc;
        }

        rail_on = true;

        /* Matches the original board's ext-power settling time. */
        k_msleep(5);
    }

    if (need_led && !rgb_on) {
        rc = gpio_pin_set(power_port, 3, 1);
        if (rc) {
            return rc;
        }

        rgb_on = true;

        /* Give the LED-side enable a short settling period. */
        k_msleep(1);
    }

    return 0;
}


static void feedback_power_finish(bool need_power, bool need_led) {
    int rc;

    if (!need_led && rgb_on) {
        rc = gpio_pin_set(power_port, 3, 0);

        if (rc && !power_fault_logged) {
            LOG_ERR("Failed to disable RGB feedback rail: %d", rc);
            power_fault_logged = true;
        }

        if (!rc) {
            rgb_on = false;
        }
    }

    if (!need_power && rail_on) {
        rc = gpio_pin_set(power_port, 0, 0);

        if (rc && !power_fault_logged) {
            LOG_ERR("Failed to disable feedback 3V3 rail: %d", rc);
            power_fault_logged = true;
        }

        if (!rc) {
            rail_on = false;

            /*
             * WS2812 state cannot be assumed after power removal. Force a
             * complete refresh at the next feedback event.
             */
            previous_valid = false;
        }
    }
}


static void tick(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(feedback_tick, tick);


static void tick(struct k_work *work) {
    ARG_UNUSED(work);

    k_spinlock_key_t key = k_spin_lock(&state_lock);

    struct ankur_pattern p = active;
    uint32_t rev = revision;

    uint32_t elapsed =
        (uint32_t)(k_uptime_get() - started);

    bool led_enabled = show_led;
    bool motor_enabled = run_motor;
    bool held = mode;

    k_spin_unlock(&state_lock, key);

    uint32_t total = ankur_duration(&p);

    /* A zero-pulse battery report still receives an acknowledgment. */
    if (!total && p.color == AK_RED && led_enabled) {
        total = 250;
    }

    bool notification = elapsed < total;

    bool on = false;
    unsigned phase = 0;
    uint32_t boundary = 0;

    while (phase < p.count) {
        boundary += p.ms[phase];

        if (elapsed < boundary) {
            on = !(phase & 1);
            break;
        }

        ++phase;
    }

    struct led_rgb rgb = {0};

    if (led_enabled && notification) {
        rgb = color_at(p.color, elapsed);

        if (p.effect == AK_FADE && total) {
            rgb.r = rgb.r * (total - elapsed) / total;
            rgb.g = rgb.g * (total - elapsed) / total;
            rgb.b = rgb.b * (total - elapsed) / total;
        } else if (p.effect == AK_FLASH && p.count && !on) {
            rgb = (struct led_rgb){0};
        }
    } else if (held && led_enabled) {
        rgb = color_at(AK_RAINBOW, elapsed);
    }

    if ((notification && p.effect == AK_BREATHE) ||
        (!notification && held)) {

        uint32_t t = elapsed % 2000;
        uint32_t x = t < 1000 ? t : 2000 - t;

        /* Smoothstep triangle, no floating-point work in the feedback loop. */
        uint32_t level =
            (x * x * (3000 - 2 * x)) / 1000000;

        rgb.r = rgb.r * level / 1000;
        rgb.g = rgb.g * level / 1000;
        rgb.b = rgb.b * level / 1000;
    }

    for (size_t i = 0; i < ARRAY_SIZE(pixels); ++i) {
        pixels[i] = rgb;
    }

    const bool session_led =
        led_enabled && (notification || held);

    const bool session_motor =
        motor_enabled && notification;

    const bool need_power =
        session_led || session_motor;

    int power_rc =
        feedback_power_prepare(need_power, session_led);

    if (power_rc) {
        if (!power_fault_logged) {
            LOG_ERR("Failed to enable feedback power: %d", power_rc);
            power_fault_logged = true;
        }
    } else {
        power_fault_logged = false;

        int motor_rc = gpio_pin_set(
            motor_port,
            24,
            notification && motor_enabled && on
        );

        if (motor_rc) {
            if (!motor_fault_logged) {
                LOG_ERR(
                    "Failed to update vibration output: %d",
                    motor_rc
                );
                motor_fault_logged = true;
            }
        } else {
            motor_fault_logged = false;
        }

        bool pixel_changed =
            !previous_valid ||
            memcmp(previous, pixels, sizeof(pixels)) != 0;

        if (pixel_changed) {
            if (rgb_on) {
                if (!device_is_ready(strip)) {
                    if (!led_fault_logged) {
                        LOG_ERR("LED strip device is not ready");
                        led_fault_logged = true;
                    }
                } else {
                    int led_rc = led_strip_update_rgb(
                        strip,
                        pixels,
                        ARRAY_SIZE(pixels)
                    );

                    if (led_rc) {
                        if (!led_fault_logged) {
                            LOG_ERR(
                                "LED update failed: %d",
                                led_rc
                            );
                            led_fault_logged = true;
                        }

                        previous_valid = false;
                    } else {
                        memcpy(
                            previous,
                            pixels,
                            sizeof(pixels)
                        );

                        previous_valid = true;
                        led_fault_logged = false;
                    }
                }
            } else if (!session_led) {
                /*
                 * With LED power physically removed, zero is the guaranteed
                 * electrical output even though there is no SPI transaction.
                 */
                memcpy(previous, pixels, sizeof(pixels));
                previous_valid = true;
            }
        }
    }

    /*
     * Always turn inactive hardware off only after outputs have first been
     * driven to zero.
     */
    feedback_power_finish(need_power, session_led);

    key = k_spin_lock(&state_lock);

    bool changed = revision != rev;

    k_spin_unlock(&state_lock, key);

    if (notification || held || changed) {
        uint32_t delay = 20;

        if (notification && boundary > elapsed) {
            delay = MIN(delay, boundary - elapsed);
        }

        k_work_reschedule_for_queue(
            &feedback_queue,
            &feedback_tick,
            K_MSEC(changed ? 1 : delay)
        );
    }
}


void ankur_feedback_play(
    struct ankur_pattern p,
    bool leds,
    bool vibration
) {
    if (!ready || p.count > ANKUR_PATTERN_MAX) {
        return;
    }

    k_spinlock_key_t key =
        k_spin_lock(&state_lock);

    active = p;
    started = k_uptime_get();
    show_led = leds;
    run_motor = vibration;
    ++revision;

    k_spin_unlock(&state_lock, key);

    k_work_reschedule_for_queue(
        &feedback_queue,
        &feedback_tick,
        K_NO_WAIT
    );
}


void ankur_feedback_mode(bool enabled) {
    k_spinlock_key_t key =
        k_spin_lock(&state_lock);

    mode = enabled;
    ++revision;

    k_spin_unlock(&state_lock, key);

    if (ready) {
        k_work_reschedule_for_queue(
            &feedback_queue,
            &feedback_tick,
            K_NO_WAIT
        );
    }
}


void ankur_feedback_stop(void) {
    k_spinlock_key_t key =
        k_spin_lock(&state_lock);

    active = (struct ankur_pattern){0};
    mode = false;
    show_led = false;
    run_motor = false;
    ++revision;

    k_spin_unlock(&state_lock, key);

    if (ready) {
        k_work_reschedule_for_queue(
            &feedback_queue,
            &feedback_tick,
            K_NO_WAIT
        );
    }
}


static int feedback_init(void) {
    if (!device_is_ready(motor_port) ||
        !device_is_ready(power_port)) {
        return -ENODEV;
    }

    int rc =
        gpio_pin_configure(
            motor_port,
            24,
            GPIO_OUTPUT_INACTIVE
        );

    if (rc) {
        return rc;
    }

    rc = gpio_pin_configure(
        power_port,
        0,
        GPIO_OUTPUT_INACTIVE
    );

    if (rc) {
        return rc;
    }

    rc = gpio_pin_configure(
        power_port,
        3,
        GPIO_OUTPUT_INACTIVE
    );

    if (rc) {
        return rc;
    }

    rail_on = false;
    rgb_on = false;
    previous_valid = false;

    k_work_queue_start(
        &feedback_queue,
        feedback_stack,
        K_THREAD_STACK_SIZEOF(feedback_stack),
        K_PRIO_PREEMPT(10),
        NULL
    );

    ready = true;

    return 0;
}

SYS_INIT(feedback_init, APPLICATION, 95);
