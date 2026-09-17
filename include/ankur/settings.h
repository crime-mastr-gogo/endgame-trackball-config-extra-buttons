/* SPDX-License-Identifier: MIT */
#pragma once
#include "ankur/policy.h"

struct ankur_preferences ankur_settings_get(void);
int ankur_settings_set(struct ankur_preferences preferences);
int ankur_settings_reset(void);
int ankur_settings_flush(void);
