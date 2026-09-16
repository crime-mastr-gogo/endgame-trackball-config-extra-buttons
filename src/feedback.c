/* Ankur Endgame: single owner of the motor and its supply-enable pin.
 * Compatibility API for efogdev/zmk-feedback-common 92251ce.
 * Work callbacks and callers serialize through one mutex. No caller-owned
 * pattern pointers, allocation, busy waits, or unbounded pulse extension.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zmk/feedback_common/feedback_gpio.h>

#define MAX_STEPS 32
#define MAX_PULSE_MS 600
#define MAX_PATTERN_MS 6000
#define COOLDOWN_MS 100
static const struct gpio_dt_spec motor = GPIO_DT_SPEC_GET(DT_NODELABEL(feedback_common), feedback_gpios);
static const struct gpio_dt_spec supply = GPIO_DT_SPEC_GET(DT_NODELABEL(feedback_common), feedback_extra_gpios);
K_MUTEX_DEFINE(feedback_lock);
static int steps[MAX_STEPS];
static uint8_t count, index, active_priority;
static bool enabled, ready, active;
static int64_t available_at;
static void step(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(step_work, step);

static void stop_locked(void) {
    gpio_pin_set_dt(&motor, 0);
    gpio_pin_set_dt(&supply, 0);
    if (active) available_at = k_uptime_get() + COOLDOWN_MS;
    active = false;
    count = index = 0;
}

static void step(struct k_work *work) {
    ARG_UNUSED(work);
    k_mutex_lock(&feedback_lock, K_FOREVER);
    if (!enabled || !active || index >= count) {
        stop_locked();
    } else {
        int rc = gpio_pin_set_dt(&motor, (index % 2) == 0);
        if (rc < 0) stop_locked();
        else k_work_reschedule(&step_work, K_MSEC(steps[index++]));
    }
    k_mutex_unlock(&feedback_lock);
}

int fbc_trigger_pattern_priority(const int *pattern, uint8_t length, uint8_t priority) {
    if (!pattern || !length || length > MAX_STEPS || priority > 2 || k_is_in_isr()) return -EINVAL;
    unsigned total = 0;
    for (unsigned i = 0; i < length; i++) {
        if (pattern[i] < 1 || pattern[i] > MAX_PULSE_MS) return -EINVAL;
        total += pattern[i];
    }
    if (total > MAX_PATTERN_MS) return -EINVAL;
    k_mutex_lock(&feedback_lock, K_FOREVER);
    int rc = 0;
    if (!ready || !enabled) rc = -EACCES;
    /* Do not restart/extend a running pulse under repeated input. */
    else if ((active && priority <= active_priority) ||
             (!active && priority < 2 && k_uptime_get() < available_at)) rc = -EBUSY;
    else {
        if (active) {
            k_work_cancel_delayable(&step_work);
            stop_locked();
        }
        active_priority = priority;
        memcpy(steps, pattern, length * sizeof(steps[0]));
        count = length;
        index = 0;
        rc = gpio_pin_set_dt(&supply, 1);
        if (!rc) {
            active = true;
            k_work_reschedule(&step_work, K_MSEC(5));
        } else stop_locked();
    }
    k_mutex_unlock(&feedback_lock);
    return rc;
}

int fbc_trigger_pattern(const int *pattern, uint8_t length) {
    return fbc_trigger_pattern_priority(pattern, length, 1);
}

int fbc_trigger(uint32_t duration) {
    if (!duration || duration > MAX_PULSE_MS) return -EINVAL;
    int pulse = duration;
    return fbc_trigger_pattern(&pulse, 1);
}

void fbc_stop(void) {
    k_mutex_lock(&feedback_lock, K_FOREVER);
    k_work_cancel_delayable(&step_work);
    if (ready) stop_locked();
    k_mutex_unlock(&feedback_lock);
}

void fbc_set_enabled(bool value) {
    k_mutex_lock(&feedback_lock, K_FOREVER);
    enabled = value;
    if (!value && ready) {
        k_work_cancel_delayable(&step_work);
        stop_locked();
    }
    k_mutex_unlock(&feedback_lock);
}

bool fbc_is_active(void) {
    k_mutex_lock(&feedback_lock, K_FOREVER);
    bool result = active;
    k_mutex_unlock(&feedback_lock);
    return result;
}

static int init(void) {
    if (!gpio_is_ready_dt(&motor) || !gpio_is_ready_dt(&supply)) return -ENODEV;
    int rc = gpio_pin_configure_dt(&motor, GPIO_OUTPUT_INACTIVE);
    if (!rc) rc = gpio_pin_configure_dt(&supply, GPIO_OUTPUT_INACTIVE);
    ready = !rc;
    /* Remains muted until persisted settings have been restored. */
    return rc;
}
SYS_INIT(init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
