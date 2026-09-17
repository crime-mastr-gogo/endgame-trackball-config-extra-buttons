/* SPDX-License-Identifier: MIT */
#pragma once

#include <zephyr/sys/iterable_sections.h>

/* Minimal registration ABI from the pinned ZMK Studio rpc.h. Including the
 * complete RPC header from an external module races nanopb header generation. */
typedef int (*zmk_rpc_subsystem_settings_reset_func)(void);

struct zmk_rpc_subsystem_settings_reset {
    zmk_rpc_subsystem_settings_reset_func callback;
};

#define ANKUR_STUDIO_SETTINGS_RESET(prefix, callback_fn)                                          \
    STRUCT_SECTION_ITERABLE(zmk_rpc_subsystem_settings_reset, _##prefix##_settings_reset) = {      \
        .callback = callback_fn,                                                                   \
    }
