/* SPDX-License-Identifier: MIT */
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <drivers/input_processor.h>
#include <dt-bindings/zmk/input_transform.h>
#include "ankur/controls.h"
#include "ankur/settings.h"

struct stage { const struct device *dev; uint32_t param; };
#define STAGE(label) {DEVICE_DT_GET(DT_NODELABEL(label)),0}
static const struct stage high_res[]={
    STAGE(zip_bistable_twist_scaler),STAGE(zip_pointer_accel),
    STAGE(zip_scroll_accel),STAGE(zip_rotate_pointer)
};
static const struct stage standard[]={
    STAGE(zip_bistable_twist_scaler),STAGE(zip_twist_full_notch_scaler),
    STAGE(zip_pointer_accel),STAGE(zip_scroll_accel),STAGE(zip_rotate_pointer)
};
static const struct stage fine[]={STAGE(zip_bistable_snipe_scaler),STAGE(zip_rotate_pointer)};
static const struct stage drag[]={
    STAGE(zip_bistable_scroll_xy_scaler),STAGE(zip_axis_clamper),STAGE(zip_xy_to_scroll_mapper),
    {DEVICE_DT_GET(DT_NODELABEL(zip_scroll_transform)),INPUT_TRANSFORM_Y_INVERT},
    STAGE(zip_dragscroll_accel),STAGE(zip_rotate_scroll)
};
static int16_t remainders[4][6][4];
static uint8_t previous_mode=0xff;
static int axis_index(uint16_t code) {
    switch(code) {
    case INPUT_REL_X:return 0; case INPUT_REL_Y:return 1;
    case INPUT_REL_WHEEL:return 2; case INPUT_REL_HWHEEL:return 3;
    default:return -1;
    }
}
static int modes(const struct device *dev,struct input_event *event,uint32_t p1,uint32_t p2,
                 struct zmk_input_processor_state *state) {
    ARG_UNUSED(dev); ARG_UNUSED(p1); ARG_UNUSED(p2);
    uint8_t mode=ankur_drag_scroll_active()?3:
                 ankur_fine_active()?2:
                 ankur_settings_get().high_res?0:1;
    if (mode!=previous_mode) { memset(remainders,0,sizeof(remainders)); previous_mode=mode; }
    const struct stage *stages=mode==3?drag:mode==2?fine:mode==1?standard:high_res;
    size_t count=mode==3?ARRAY_SIZE(drag):mode==2?ARRAY_SIZE(fine):
                 mode==1?ARRAY_SIZE(standard):ARRAY_SIZE(high_res);
    for (size_t i=0;i<count;++i) {
        int axis=axis_index(event->code);
        struct zmk_input_processor_state sub={
            .input_device_index=state?state->input_device_index:0,
            .remainder=axis<0?NULL:&remainders[mode][i][axis]
        };
        int rc=zmk_input_processor_handle_event(stages[i].dev,event,stages[i].param,0,&sub);
        if (rc!=ZMK_INPUT_PROC_CONTINUE) return rc;
    }
    return ZMK_INPUT_PROC_CONTINUE;
}
static const struct zmk_input_processor_driver_api modes_api={.handle_event=modes};
DEVICE_DT_DEFINE(DT_NODELABEL(ankur_modes),NULL,NULL,NULL,NULL,POST_KERNEL,
                 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,&modes_api);
