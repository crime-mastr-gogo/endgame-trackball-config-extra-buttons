/* Custom features sit above the pinned efog drivers, not inside sensor code. */
#define DT_DRV_COMPAT ankur_control
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <drivers/behavior.h>
#include <drivers/p2sm_runtime.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/ble.h>
#include <zmk/usb.h>
#include <zmk/battery.h>
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/feedback_common/feedback_gpio.h>
#include <zmk_adaptive_feedback/adaptive_feedback.h>
#include <zmk_esb/endpoint.h>
#include <ankur/policy.h>

LOG_MODULE_REGISTER(ankur_controls, CONFIG_ZMK_LOG_LEVEL);
static struct ankur_settings state = {1, 6, 4, 1, 1, 1};
static struct ankur_settings saved = {0};
K_MUTEX_DEFINE(state_lock);
static bool restored, suspended, dragging, powering_off;
static uint32_t epoch;
static const float pointer_levels[ANKUR_LEVELS] = {
    .1f,.116667f,.133333f,.15f,.166667f,.183333f,.2f,.216667f,.233333f,.25f,
    .305f,.36f,.415f,.47f,.525f,.58f,.635f,.69f,.745f,.8f
};
static const float twist_levels[ANKUR_LEVELS] = {
    .1f,.116667f,.133333f,.15f,.166667f,.183333f,.2f,.216667f,.233333f,.25f,
    .325f,.4f,.475f,.55f,.625f,.7f,.775f,.85f,.925f,1.f
};
#define EVENT(id, name) ZAF_CUSTOM_EVENT_DEFINE(id, name)
EVENT(pointer_up, "pointer-sensitivity-increased");
EVENT(pointer_down, "pointer-sensitivity-decreased");
EVENT(twist_up, "twist-sensitivity-increased");
EVENT(twist_down, "twist-sensitivity-decreased");
EVENT(boundary, "sensitivity-boundary");
EVENT(reset, "sensitivity-reset");
EVENT(standard, "standard-scroll-selected");
EVENT(highres, "high-res-scroll-selected");
EVENT(clear_current, "clear-current-bt-feedback");
EVENT(clear_all, "clear-all-bt-feedback");
EVENT(shutdown, "power-off-feedback");
EVENT(status, "status-report");
EVENT(status_usb, "status-usb");
EVENT(status_esb, "status-esb");
EVENT(drag_on, "drag-lock-on");
EVENT(drag_off, "drag-lock-off");

static int persist(void) {
    struct ankur_settings snapshot;
    k_mutex_lock(&state_lock, K_FOREVER);
    snapshot = state;
    bool dirty = memcmp(&snapshot, &saved, sizeof(snapshot)) != 0;
    k_mutex_unlock(&state_lock);
    if (!dirty) return 0;
    int rc = settings_save_one("ankur/v1", &snapshot, sizeof(snapshot));
    if (!rc) {
        k_mutex_lock(&state_lock, K_FOREVER);
        saved = snapshot;
        k_mutex_unlock(&state_lock);
    } else LOG_ERR("Settings save failed: %d", rc);
    return rc;
}
static void save_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(save_work, save_work_fn);
static void save_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    /* One bounded retry per subsequent user change, no endless flash loop. */
    persist();
}

static void apply(void) {
    p2sm_set_move_coef(pointer_levels[state.pointer]);
    p2sm_set_twist_coef(twist_levels[state.twist]);
    if (state.standard) zmk_keymap_layer_activate(ANKUR_STANDARD_LAYER);
    else zmk_keymap_layer_deactivate(ANKUR_STANDARD_LAYER);
    fbc_set_enabled(restored && state.vibration && !suspended);
    zaf_set_led_enabled(restored && state.led && !suspended);
}
static int settings_set(const char *name, size_t len, settings_read_cb read, void *arg) {
    if (strcmp(name, "v1")) return -ENOENT;
    struct ankur_settings candidate;
    if (len != sizeof(candidate)) return -EINVAL;
    int rc = read(arg, &candidate, sizeof(candidate));
    if (rc != sizeof(candidate)) return rc < 0 ? rc : -EIO;
    if (!ankur_settings_valid(&candidate)) return -EINVAL;
    state = saved = candidate;
    return 0;
}
static int settings_commit(void) {
    restored = true;
    apply();
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(ankur, "ankur", NULL, settings_set, settings_commit, NULL);

static void release_drag(void) {
    if (!dragging) return;
    int rc = input_report_key(DEVICE_DT_GET(DT_NODELABEL(mkp)), INPUT_BTN_0, 0, true, K_NO_WAIT);
    if (!rc) dragging = false;
    else LOG_ERR("Drag release failed: %d", rc);
}

static void power_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    release_drag();
    if (persist()) { powering_off = false; return; }
    fbc_set_enabled(false);
    zaf_set_led_enabled(false);
    struct zmk_behavior_binding b = {.behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(soft_off))};
    struct zmk_behavior_binding_event e = {.timestamp = k_uptime_get()};
    int rc = zmk_behavior_invoke_binding(&b, e, true);
    if (rc < 0) { powering_off = false; apply(); LOG_ERR("Power off failed: %d", rc); }
}
K_WORK_DELAYABLE_DEFINE(power_work, power_work_fn);

/* Tens use 260ms, units 60ms. <=20 levels => <=19 entries.
 * The shared service copies the stack pattern before returning. */
static void report_number(unsigned number, unsigned units_per_long) {
    int pattern[32];
    unsigned count = 0;
    number = MIN(number, 20);
    for (unsigned i = 0; i < number / units_per_long; i++) {
        pattern[count++] = 260; pattern[count++] = 180;
    }
    for (unsigned i = 0; i < number % units_per_long; i++) {
        pattern[count++] = 60; pattern[count++] = 75;
    }
    if (!count) { pattern[count++] = 600; }
    else --count; /* no trailing silence */
    fbc_trigger_pattern_priority(pattern, count, 0);
    zaf_custom_event_trigger(&status);
}

static int execute_locked(unsigned action) {
    if (!restored || powering_off) return -EAGAIN;
    struct ankur_settings before = state;
    struct zaf_custom_event *feedback = NULL;
    switch (action) {
    case POINTER_UP: case POINTER_DOWN:
        state.pointer = ankur_step(state.pointer, action == POINTER_UP);
        feedback = state.pointer == 0 || state.pointer == 19 ? &boundary :
                   action == POINTER_UP ? &pointer_up : &pointer_down;
        break;
    case TWIST_UP: case TWIST_DOWN:
        state.twist = ankur_step(state.twist, action == TWIST_UP);
        feedback = state.twist == 0 || state.twist == 19 ? &boundary :
                   action == TWIST_UP ? &twist_up : &twist_down;
        break;
    case SCROLL_TOGGLE:
        state.standard = !state.standard;
        feedback = state.standard ? &standard : &highres;
        break;
    case RESET_SENSITIVITY:
        state.pointer = 6; state.twist = 4; feedback = &reset; break;
    case TOGGLE_LED: state.led = !state.led; break;
    case TOGGLE_VIBRATION: state.vibration = !state.vibration; break;
    case DRAG_TOGGLE:
        if (dragging) { release_drag(); feedback = &drag_off; }
        else {
            int rc = input_report_key(DEVICE_DT_GET(DT_NODELABEL(mkp)), INPUT_BTN_0, 1, true, K_NO_WAIT);
            if (rc) return rc;
            dragging = true; feedback = &drag_on;
        }
        break;
    case REPORT_POINTER: report_number(state.pointer + 1, 10); break;
    case REPORT_TWIST: report_number(state.twist + 1, 10); break;
    case REPORT_SCROLL: feedback = state.standard ? &standard : &highres; break;
    case REPORT_CONNECTION:
        if (zmk_esb_endpoint_is_active()) feedback = &status_esb;
        else if (zmk_usb_is_hid_ready()) feedback = &status_usb;
        else report_number(zmk_ble_active_profile_index() + 1, 10);
        break;
    case REPORT_BATTERY: report_number(MIN(zmk_battery_state_of_charge(), 100) / 5, 5); break;
    case CLEAR_CURRENT:
        release_drag(); zmk_ble_clear_bonds(); feedback = &clear_current; break;
    case CLEAR_ALL:
        release_drag(); zmk_ble_clear_all_bonds(); feedback = &clear_all; break;
    case POWER_OFF:
        release_drag(); powering_off = true; feedback = &shutdown;
        k_work_reschedule(&power_work, K_MSEC(350)); break;
    default: return -EINVAL;
    }
    if (memcmp(&before, &state, sizeof(state))) {
        apply();
        k_work_reschedule(&save_work, K_MSEC(2500));
    }
    if (feedback) zaf_custom_event_trigger(feedback);
    return ZMK_BEHAVIOR_OPAQUE;
}

struct control_config { unsigned action; };
static int execute(unsigned action) {
    k_mutex_lock(&state_lock, K_FOREVER);
    int rc = execute_locked(action);
    k_mutex_unlock(&state_lock);
    return rc;
}
struct control_data { bool held; uint32_t position, epoch; int64_t pressed_at; };
static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event e) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct control_config *cfg = dev->config;
    struct control_data *data = dev->data;
    if (!ankur_guarded(cfg->action)) return execute(cfg->action);
    if (!data->held || data->epoch != epoch) {
        data->held = true; data->pressed_at = e.timestamp;
        data->position = e.position; data->epoch = epoch;
    }
    return ZMK_BEHAVIOR_OPAQUE;
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event e) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct control_config *cfg = dev->config;
    struct control_data *data = dev->data;
    if (!ankur_guarded(cfg->action)) return ZMK_BEHAVIOR_OPAQUE;
    bool armed = data->held && data->position == e.position && data->epoch == epoch &&
                 e.timestamp - data->pressed_at >= ANKUR_GUARD_MS;
    data->held = false;
    return armed ? execute(cfg->action) : ZMK_BEHAVIOR_OPAQUE;
}
static const struct behavior_driver_api api = {.binding_pressed = pressed, .binding_released = released};
#define CONTROL(n) \
    static struct control_data data_##n; \
    static const struct control_config config_##n = {.action = DT_INST_PROP(n, action)}; \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &data_##n, &config_##n, POST_KERNEL, \
                           CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
DT_INST_FOREACH_STATUS_OKAY(CONTROL)

static int lifecycle(const zmk_event_t *event) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(event);
    if (pos) {
        if (pos->state && pos->position == 4) release_drag();
        return ZMK_EV_EVENT_BUBBLE;
    }
    ++epoch; /* Armed destructive actions cannot survive a connection/sleep transition. */
    release_drag();
    fbc_stop();
    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(event);
    if (activity) {
        suspended = activity->state != ZMK_ACTIVITY_ACTIVE;
        fbc_set_enabled(restored && state.vibration && !suspended);
        zaf_set_led_enabled(restored && state.led && !suspended);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(ankur_lifecycle, lifecycle);
ZMK_SUBSCRIPTION(ankur_lifecycle, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(ankur_lifecycle, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(ankur_lifecycle, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(ankur_lifecycle, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(ankur_lifecycle, zmk_position_state_changed);
