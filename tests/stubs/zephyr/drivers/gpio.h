#pragma once
#include <stdbool.h>
struct gpio_dt_spec { unsigned pin; };
#define GPIO_OUTPUT_INACTIVE 0
#define DT_NODELABEL(n) 0
#define feedback_gpios 0
#define feedback_extra_gpios 1
#define GPIO_DT_SPEC_GET(n, prop) {.pin = prop}
static int gpio_values[2];
static bool gpio_ready = true;
static inline bool gpio_is_ready_dt(const struct gpio_dt_spec *s) { (void)s; return gpio_ready; }
static inline int gpio_pin_set_dt(const struct gpio_dt_spec *s, int value) { gpio_values[s->pin] = value; return 0; }
static inline int gpio_pin_configure_dt(const struct gpio_dt_spec *s, int flags) { (void)flags; gpio_values[s->pin] = 0; return 0; }
