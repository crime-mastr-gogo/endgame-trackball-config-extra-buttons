/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include "ankur/settings.h"

LOG_MODULE_REGISTER(ankur_settings, CONFIG_ZMK_LOG_LEVEL);

/* Snapshot state is protected briefly. Never hold its lock while writing flash. */
static struct k_spinlock state_lock;
static struct ankur_preferences current = ANKUR_DEFAULTS;
static uint32_t generation, saved_generation;
K_MUTEX_DEFINE(write_mutex);

struct ankur_preferences ankur_settings_get(void) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    struct ankur_preferences snapshot = current;
    k_spin_unlock(&state_lock, key);
    return snapshot;
}

int ankur_settings_flush(void) {
    k_mutex_lock(&write_mutex, K_FOREVER);
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    struct ankur_preferences snapshot = current;
    uint32_t target = generation;
    bool dirty = target != saved_generation;
    k_spin_unlock(&state_lock, key);
    int rc = dirty ? settings_save_one("ankur/preferences", &snapshot, sizeof(snapshot)) : 0;
    if (!rc && dirty) {
        key = k_spin_lock(&state_lock);
        saved_generation = target;
        k_spin_unlock(&state_lock, key);
    }
    k_mutex_unlock(&write_mutex);
    return rc;
}

static void save_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(save_work, save_work_fn);

static void save_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    int rc = ankur_settings_flush();
    if (rc) {
        LOG_ERR("Preference save failed: %d", rc);
        /* Bounded retry frequency; a flash fault must never spin or block input. */
        k_work_reschedule(&save_work, K_SECONDS(30));
    }
}

int ankur_settings_set(struct ankur_preferences preferences) {
    if (!ankur_preferences_valid(&preferences)) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool changed = memcmp(&current, &preferences, sizeof(current)) != 0;
    if (changed) {
        current = preferences;
        ++generation;
    }
    k_spin_unlock(&state_lock, key);
    if (changed) k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
    return 0;
}

int ankur_settings_reset(void) {
    const struct ankur_preferences defaults = ANKUR_DEFAULTS;
    int rc = ankur_settings_set(defaults);
    return rc ? rc : ankur_settings_flush();
}

static int ankur_settings_load(const char *name, size_t len, settings_read_cb read_cb, void *arg) {
    if (strcmp(name, "preferences") != 0) return -ENOENT;
    struct ankur_preferences loaded = ANKUR_DEFAULTS;
    if (len != sizeof(loaded)) {
        LOG_WRN("Ignoring preference record with incompatible length");
        return 0;
    }
    int rc = read_cb(arg, &loaded, sizeof(loaded));
    if (rc < 0) return rc;
    if (rc != sizeof(loaded) || !ankur_preferences_valid(&loaded)) {
        LOG_WRN("Ignoring invalid preference record");
        return 0;
    }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    current = loaded;
    generation = saved_generation = 0;
    k_spin_unlock(&state_lock, key);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(ankur, "ankur", NULL, ankur_settings_load, NULL, NULL);
