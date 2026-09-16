#pragma once
#include <stdbool.h>
#include <stdint.h>
int fbc_trigger(uint32_t duration_ms);
int fbc_trigger_pattern(const int *pattern, uint8_t count);
int fbc_trigger_pattern_priority(const int *pattern, uint8_t count, uint8_t priority);
bool fbc_is_active(void);
void fbc_set_enabled(bool enabled);
void fbc_stop(void);
