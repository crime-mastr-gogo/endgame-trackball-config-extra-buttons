/* SPDX-License-Identifier: MIT */
#include <stdbool.h>
#include <zmk/usb.h>

/* Preserve the receiver's radio configuration. Only divert HID routing while
 * the USB host has configured its HID interface (a charging cable is insufficient). */
bool __real_zmk_esb_endpoint_is_active(void);
bool __wrap_zmk_esb_endpoint_is_active(void) {
    return zmk_usb_get_conn_state()!=ZMK_USB_CONN_HID &&
           __real_zmk_esb_endpoint_is_active();
}
