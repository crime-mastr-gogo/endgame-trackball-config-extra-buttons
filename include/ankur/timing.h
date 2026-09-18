/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ANKUR_STRING_HOLD_MS 1000
#define ANKUR_PROTECTED_HOLD_MS 2000

static inline bool ankur_deadline_due(
    int64_t now,
    int64_t deadline
) {
    return deadline > 0 && now >= deadline;
}
