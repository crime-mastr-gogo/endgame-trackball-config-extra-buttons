/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT ankur_control
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <drivers/p2sm_runtime.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>
#include <zmk/usb.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/battery.h>
#include <zmk/pm.h>
#include <zmk/studio/core.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/pointing.h>
#include <dt-bindings/ankur/actions.h>
#include "ankur/controls.h"
#include "ankur/settings.h"
#include "ankur/feedback.h"
#include "ankur/studio_reset.h"

K_MUTEX_DEFINE(control_mutex);
static atomic_t fine_count, scroll_count;
static bool drag_locked;
static uint8_t held_actions[15];
static int64_t deadlines[15];
static bool guard_fired[15];
static bool initialized;
static bool usb_hid_active;

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
#define ACTION_METADATA(label, action)                                                \
    { .display_name = label, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = action }
static const struct behavior_parameter_value_metadata ankur_action_values[] = {
    ACTION_METADATA("Toggle Drag Lock", AK_DRAG),
    ACTION_METADATA("Hold 1 Second: Type Custom String", AK_STRING),
    ACTION_METADATA("Increase Twist Sensitivity", AK_TWIST_INC),
    ACTION_METADATA("Decrease Twist Sensitivity", AK_TWIST_DEC),
    ACTION_METADATA("Increase Pointer Sensitivity", AK_POINTER_INC),
    ACTION_METADATA("Decrease Pointer Sensitivity", AK_POINTER_DEC),
    ACTION_METADATA("Next Bluetooth Profile", AK_NEXT),
    ACTION_METADATA("Previous Bluetooth Profile", AK_PREV),
    ACTION_METADATA("Hold 2 Seconds: Power Off", AK_OFF),
    ACTION_METADATA("Unlock ZMK Studio", AK_UNLOCK),
    ACTION_METADATA("Toggle Standard/High-Resolution Scroll", AK_SCROLL),
    ACTION_METADATA("Hold 2 Seconds: Clear Current Bluetooth Profile", AK_BT_CLEAR),
    ACTION_METADATA("Hold 2 Seconds: Clear All Bluetooth Profiles", AK_BT_CLEAR_ALL),
    ACTION_METADATA("Hold 2 Seconds: Reset Sensitivity", AK_SENS_RESET),
    ACTION_METADATA("Toggle Vibration", AK_VIB_TOGGLE),
    ACTION_METADATA("Toggle LEDs", AK_LED_TOGGLE),
    ACTION_METADATA("Report Twist Sensitivity", AK_REPORT_TWIST),
    ACTION_METADATA("Report Bluetooth Profile", AK_REPORT_PROFILE),
    ACTION_METADATA("Report Pointer Sensitivity", AK_REPORT_POINTER),
    ACTION_METADATA("Report Battery Level", AK_REPORT_BATTERY),
    ACTION_METADATA("Hold Fine Cursor", AK_FINE),
    ACTION_METADATA("Hold Drag Scroll", AK_SCROLL_HELD),
};
#undef ACTION_METADATA

static const struct behavior_parameter_metadata_set ankur_metadata_set = {
    .param1_values = ankur_action_values,
    .param1_values_len = ARRAY_SIZE(ankur_action_values),
};
static const struct behavior_parameter_metadata_set ankur_metadata_sets[] = {
    ankur_metadata_set,
};
static const struct behavior_parameter_metadata ankur_metadata = {
    .sets_len = ARRAY_SIZE(ankur_metadata_sets),
    .sets = ankur_metadata_sets,
};
#endif

bool ankur_fine_active(void) { return atomic_get(&fine_count)>0; }
bool ankur_drag_scroll_active(void) { return atomic_get(&scroll_count)>0; }

static void feedback(enum ankur_feedback_event event) {
    struct ankur_preferences p=ankur_settings_get();
    ankur_feedback_play(ankur_patterns[event],p.leds,p.vibration);
}

static int mouse_left(bool down) {
    struct zmk_behavior_binding binding={.behavior_dev=DEVICE_DT_NAME(DT_NODELABEL(mkp)),.param1=LCLK};
    struct zmk_behavior_binding_event event={.position=8,.timestamp=k_uptime_get()};
    return zmk_behavior_invoke_binding(&binding,event,down);
}

static void apply_sensitivity(void) {
    struct ankur_preferences p=ankur_settings_get();
    float move=(float)ankur_pointer_coeff[p.pointer-1]/1000000.0f;
    float twist=(float)ankur_twist_coeff[p.twist-1]/1000000.0f;
    if (p2sm_get_move_coef()!=move) p2sm_set_move_coef(move);
    if (p2sm_get_twist_coef()!=twist) p2sm_set_twist_coef(twist);
}

static const uint32_t macro_keys[]={SLASH,SEMI,DOT,L,COMMA,K,M,J,
                                   LS(SLASH),LS(SEMI),LS(DOT),LS(L),LS(COMMA),LS(K),LS(M),LS(J),ENTER};
BUILD_ASSERT(ARRAY_SIZE(macro_keys)==17);
static uint8_t macro_index;
static bool macro_running, macro_pressed;
static void macro_tick(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(macro_work,macro_tick);

static void macro_cancel_locked(bool *release_key, uint32_t *keycode) {
    k_work_cancel_delayable(&macro_work);
    if (macro_pressed) {
        *release_key=true;
        *keycode=macro_keys[macro_index];
    }
    macro_pressed=false;
    macro_running=false;
}

static void macro_tick(struct k_work *work) {
    ARG_UNUSED(work);
    k_mutex_lock(&control_mutex,K_FOREVER);
    if (!macro_running) { k_mutex_unlock(&control_mutex); return; }
    if (macro_pressed) {
        raise_zmk_keycode_state_changed_from_encoded(macro_keys[macro_index],false,k_uptime_get());
        macro_pressed=false;
        if (++macro_index==ARRAY_SIZE(macro_keys)) macro_running=false;
    } else {
        macro_pressed=true;
        raise_zmk_keycode_state_changed_from_encoded(macro_keys[macro_index],true,k_uptime_get());
    }
    if (macro_running) k_work_reschedule(&macro_work,K_MSEC(30));
    k_mutex_unlock(&control_mutex);
}

void ankur_controls_cancel(void) {
    bool release_key=false, release_drag=false;
    uint32_t keycode=0;
    k_mutex_lock(&control_mutex,K_FOREVER);
    macro_cancel_locked(&release_key,&keycode);
    release_drag=drag_locked;
    drag_locked=false;
    atomic_clear(&fine_count); atomic_clear(&scroll_count);
    for (unsigned i=0;i<ARRAY_SIZE(deadlines);++i) deadlines[i]=0;
    /* Suppress delayed actions until all currently held controls are released. */
    for (unsigned i=0;i<ARRAY_SIZE(guard_fired);++i) guard_fired[i]=true;
    k_mutex_unlock(&control_mutex);
    if (release_key)
        raise_zmk_keycode_state_changed_from_encoded(keycode,false,k_uptime_get());
    if (release_drag) mouse_left(false);
    ankur_feedback_stop();
}

static void power_off(struct k_work *work) {
    ARG_UNUSED(work);
    ankur_controls_cancel();
    ankur_settings_flush();
    zmk_endpoints_clear_current();
    zmk_pm_soft_off();
}
K_WORK_DELAYABLE_DEFINE(off_work,power_off);

static void perform(uint8_t action) {
    struct ankur_preferences p=ankur_settings_get();
    uint8_t old;
    switch (action) {
    case AK_DRAG:
        k_mutex_lock(&control_mutex,K_FOREVER);
        drag_locked=!drag_locked;
        bool drag_now=drag_locked;
        k_mutex_unlock(&control_mutex);
        mouse_left(drag_now);
        feedback(AK_DRAG_LOCK); break;
    case AK_STRING:
        k_mutex_lock(&control_mutex,K_FOREVER);
        if (!macro_running) {
            macro_index=0; macro_pressed=false; macro_running=true;
            k_work_reschedule(&macro_work,K_NO_WAIT);
        }
        k_mutex_unlock(&control_mutex);
        break;
    case AK_POINTER_INC: case AK_POINTER_DEC:
        old=p.pointer; p.pointer=ankur_level_step(old,action==AK_POINTER_INC);
        ankur_settings_set(p); apply_sensitivity();
        feedback(p.pointer==old ?
                 (action==AK_POINTER_INC?AK_POINTER_MAX:AK_POINTER_MIN) :
                 (action==AK_POINTER_INC?AK_POINTER_UP:AK_POINTER_DOWN)); break;
    case AK_TWIST_INC: case AK_TWIST_DEC:
        old=p.twist; p.twist=ankur_level_step(old,action==AK_TWIST_INC);
        ankur_settings_set(p); apply_sensitivity();
        feedback(p.twist==old ?
                 (action==AK_TWIST_INC?AK_TWIST_MAX:AK_TWIST_MIN) :
                 (action==AK_TWIST_INC?AK_TWIST_UP:AK_TWIST_DOWN)); break;
    case AK_NEXT: case AK_PREV:
        ankur_controls_cancel();
        zmk_endpoints_clear_current();
        zmk_ble_prof_select((zmk_ble_active_profile_index()+
                            (action==AK_NEXT?1:ZMK_BLE_PROFILE_COUNT-1))%
                           ZMK_BLE_PROFILE_COUNT);
        break;
    case AK_OFF:
        ankur_controls_cancel(); feedback(AK_POWER_OFF);
        k_work_reschedule(&off_work,K_MSEC(500)); break;
    case AK_UNLOCK: zmk_studio_core_unlock(); feedback(AK_STUDIO_UNLOCK); break;
    case AK_SCROLL: p.high_res=!p.high_res; ankur_settings_set(p);
        feedback(p.high_res?AK_HIGH_RES:AK_STANDARD); break;
    case AK_BT_CLEAR:
        if (zmk_ble_active_profile_index()<5) {
            ankur_controls_cancel(); zmk_ble_clear_bonds(); feedback(AK_CLEAR_CURRENT);
        }
        break;
    case AK_BT_CLEAR_ALL:
        ankur_controls_cancel();
        /* ZMK clears BT bonds; ESB pairing is stored by a separate module. */
        zmk_ble_clear_all_bonds(); feedback(AK_CLEAR_ALL); break;
    case AK_SENS_RESET:
        p.pointer=ANKUR_POINTER_DEFAULT; p.twist=ANKUR_TWIST_DEFAULT;
        ankur_settings_set(p); apply_sensitivity(); feedback(AK_RESET_SENS); break;
    case AK_VIB_TOGGLE:
        p.vibration=!p.vibration; ankur_settings_set(p);
        ankur_feedback_play(ankur_patterns[p.vibration?AK_VIB_ON:AK_VIB_OFF],p.leds,true); break;
    case AK_LED_TOGGLE:
        p.leds=!p.leds; ankur_settings_set(p);
        ankur_feedback_play(ankur_patterns[p.leds?AK_LED_ON:AK_LED_OFF],true,p.vibration); break;
    case AK_REPORT_TWIST:
        ankur_feedback_play(ankur_report(p.twist,5,AK_BLUE),p.leds,p.vibration); break;
    case AK_REPORT_POINTER:
        ankur_feedback_play(ankur_report(p.pointer,5,AK_GREEN),p.leds,p.vibration); break;
    case AK_REPORT_PROFILE:
        ankur_feedback_play(ankur_profile_report(zmk_ble_active_profile_index()),p.leds,p.vibration); break;
    case AK_REPORT_BATTERY:
        ankur_feedback_play(ankur_battery_report(zmk_battery_state_of_charge()),p.leds,p.vibration); break;
    }
}

static void guard_tick(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(guard_work,guard_tick);
static void guard_tick(struct k_work *work) {
    ARG_UNUSED(work);
    uint8_t due[ARRAY_SIZE(deadlines)];
    size_t due_count=0;
    k_mutex_lock(&control_mutex,K_FOREVER);
    int64_t now=k_uptime_get(), next=INT64_MAX;
    for (unsigned i=0;i<ARRAY_SIZE(deadlines);++i) {
        if (!deadlines[i] || guard_fired[i]) continue;
        if (now>=deadlines[i]) {
            guard_fired[i]=true;
            deadlines[i]=0;
            due[due_count++]=held_actions[i];
        } else next=MIN(next,deadlines[i]);
    }
    if (next!=INT64_MAX) k_work_reschedule(&guard_work,K_MSEC(MAX(1,next-now)));
    k_mutex_unlock(&control_mutex);
    for (size_t i=0;i<due_count;++i) perform(due[i]);
}

static int control_press(struct zmk_behavior_binding *binding,struct zmk_behavior_binding_event event) {
    if (event.position>=15 || binding->param1>AK_SCROLL_HELD || binding->param1==0) return -EINVAL;
    k_mutex_lock(&control_mutex,K_FOREVER);
    uint8_t action=binding->param1;
    bool held_mode=false, delayed=false;
    held_actions[event.position]=action; guard_fired[event.position]=false;
    if (action==AK_FINE || action==AK_SCROLL_HELD) {
        atomic_inc(action==AK_FINE?&fine_count:&scroll_count);
        held_mode=true;
    } else if (action==AK_STRING || action==AK_OFF || action==AK_BT_CLEAR ||
               action==AK_BT_CLEAR_ALL || action==AK_SENS_RESET) {
        deadlines[event.position]=k_uptime_get()+(action==AK_STRING?1000:2000);
        delayed=true;
    }
    k_mutex_unlock(&control_mutex);
    if (held_mode) {
        struct ankur_preferences p=ankur_settings_get();
        ankur_feedback_play((struct ankur_pattern){0},p.leds,false);
        ankur_feedback_mode(true);
    } else if (delayed) {
        k_work_reschedule(&guard_work,K_NO_WAIT);
    } else {
        perform(action);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int control_release(struct zmk_behavior_binding *binding,struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    if (event.position>=15) return -EINVAL;
    k_mutex_lock(&control_mutex,K_FOREVER);
    uint8_t action=held_actions[event.position];
    if (action==AK_FINE && atomic_get(&fine_count)>0) atomic_dec(&fine_count);
    if (action==AK_SCROLL_HELD && atomic_get(&scroll_count)>0) atomic_dec(&scroll_count);
    held_actions[event.position]=0; deadlines[event.position]=0; guard_fired[event.position]=false;
    bool update_mode=action==AK_FINE || action==AK_SCROLL_HELD;
    bool keep_mode=ankur_fine_active() || ankur_drag_scroll_active();
    k_mutex_unlock(&control_mutex);
    if (update_mode) ankur_feedback_mode(keep_mode);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api control_api={
    .binding_pressed=control_press,.binding_released=control_release,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata=&ankur_metadata,
#endif
};
BEHAVIOR_DT_INST_DEFINE(0,NULL,NULL,NULL,NULL,POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,&control_api);

static int reset_preferences(void) {
    ankur_controls_cancel();
    int rc=ankur_settings_reset();
    apply_sensitivity();
    return rc;
}
ANKUR_STUDIO_SETTINGS_RESET(ankur,reset_preferences);

static int control_event(const zmk_event_t *eh) {
    if (!initialized) return ZMK_EV_EVENT_BUBBLE;
    const struct zmk_activity_state_changed *activity=as_zmk_activity_state_changed(eh);
    if (activity) {
        if (activity->state!=ZMK_ACTIVITY_ACTIVE) {
            ankur_controls_cancel(); zmk_endpoints_clear_current(); zmk_keymap_layer_to(0);
        } else {
            apply_sensitivity();
        }
    }
    if (as_zmk_endpoint_changed(eh)) ankur_controls_cancel();
    if (as_zmk_ble_active_profile_changed(eh)) {
        ankur_controls_cancel(); perform(AK_REPORT_PROFILE);
    }
    const struct zmk_layer_state_changed *layer=as_zmk_layer_state_changed(eh);
    if (layer && layer->state && layer->layer>=1 && layer->layer<=4)
        feedback(AK_CONTROL_LAYER+layer->layer-1);
    if (as_zmk_usb_conn_state_changed(eh)) {
        bool now_hid=zmk_usb_get_conn_state()==ZMK_USB_CONN_HID;
        if (now_hid!=usb_hid_active) {
            ankur_controls_cancel();
            zmk_endpoints_select_transport(now_hid?ZMK_TRANSPORT_USB:ZMK_TRANSPORT_BLE);
            usb_hid_active=now_hid;
            feedback(now_hid?AK_USB_ON:AK_USB_OFF);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(ankur_controls,control_event);
ZMK_SUBSCRIPTION(ankur_controls,zmk_activity_state_changed);
ZMK_SUBSCRIPTION(ankur_controls,zmk_endpoint_changed);
ZMK_SUBSCRIPTION(ankur_controls,zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(ankur_controls,zmk_layer_state_changed);
ZMK_SUBSCRIPTION(ankur_controls,zmk_usb_conn_state_changed);

static void initialize(struct k_work *work) {
    ARG_UNUSED(work);
    apply_sensitivity();
    usb_hid_active=zmk_usb_get_conn_state()==ZMK_USB_CONN_HID;
    zmk_endpoints_select_transport(usb_hid_active?ZMK_TRANSPORT_USB:ZMK_TRANSPORT_BLE);
    initialized=true;
}
K_WORK_DELAYABLE_DEFINE(initialize_work,initialize);
static int control_init(void) {
    k_work_reschedule(&initialize_work,K_MSEC(500));
    return 0;
}
SYS_INIT(control_init,APPLICATION,99);
