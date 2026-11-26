#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#include "right_blahaj_widget.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Must stay <= 8 chars to fit ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN (9). */
#define PET_SYNC_BEHAVIOR_NAME "petSync"
#define RIGHT_BLAHAJ_TURBO_WPM 60
#define RIGHT_BLAHAJ_FLAP_MIN_MS 150
#define RIGHT_BLAHAJ_IDLE_DELAY_MS 1500
#define RIGHT_BLAHAJ_SLEEP_DELAY_MS 5000

enum right_blahaj_pose {
    RIGHT_BLAHAJ_SLEEP = 0,
    RIGHT_BLAHAJ_IDLE,
    RIGHT_BLAHAJ_CRUISE_A,
    RIGHT_BLAHAJ_CRUISE_B,
    RIGHT_BLAHAJ_TURBO_A,
    RIGHT_BLAHAJ_TURBO_B,
};

K_MUTEX_DEFINE(right_blahaj_mutex);

static lv_obj_t *right_shark_img;
static enum right_blahaj_pose current_pose = RIGHT_BLAHAJ_IDLE;
static uint8_t synced_wpm;
static uint32_t last_pulse_seq;
static int64_t last_anim_ms;
static bool alt_phase;

static void right_blahaj_render_cb(struct k_work *work) {
    ARG_UNUSED(work);

    enum right_blahaj_pose pose;
    k_mutex_lock(&right_blahaj_mutex, K_FOREVER);
    pose = current_pose;
    k_mutex_unlock(&right_blahaj_mutex);

    if (right_shark_img == NULL) {
        return;
    }

    const lv_img_dsc_t *src = &right_blahaj_idle;
    switch (pose) {
    case RIGHT_BLAHAJ_SLEEP:
        src = &right_blahaj_sleep;
        break;
    case RIGHT_BLAHAJ_CRUISE_A:
        src = &right_blahaj_cruise_a;
        break;
    case RIGHT_BLAHAJ_CRUISE_B:
        src = &right_blahaj_cruise_b;
        break;
    case RIGHT_BLAHAJ_TURBO_A:
        src = &right_blahaj_turbo_a;
        break;
    case RIGHT_BLAHAJ_TURBO_B:
        src = &right_blahaj_turbo_b;
        break;
    case RIGHT_BLAHAJ_IDLE:
    default:
        src = &right_blahaj_idle;
        break;
    }
    lv_img_set_src(right_shark_img, src);
}

K_WORK_DEFINE(right_blahaj_render_work, right_blahaj_render_cb);

static void schedule_right_render(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &right_blahaj_render_work);
    }
}

static void right_idle_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    k_mutex_lock(&right_blahaj_mutex, K_FOREVER);
    current_pose = RIGHT_BLAHAJ_IDLE;
    k_mutex_unlock(&right_blahaj_mutex);
    schedule_right_render();
}

static void right_sleep_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    k_mutex_lock(&right_blahaj_mutex, K_FOREVER);
    current_pose = RIGHT_BLAHAJ_SLEEP;
    k_mutex_unlock(&right_blahaj_mutex);
    schedule_right_render();
}

K_WORK_DELAYABLE_DEFINE(right_idle_work, right_idle_work_cb);
K_WORK_DELAYABLE_DEFINE(right_sleep_work, right_sleep_work_cb);

static void trigger_typing_step(void) {
    int64_t now = k_uptime_get();
    bool should_render = false;

    k_mutex_lock(&right_blahaj_mutex, K_FOREVER);
    if ((now - last_anim_ms) >= RIGHT_BLAHAJ_FLAP_MIN_MS) {
        last_anim_ms = now;
        alt_phase = !alt_phase;
        if (synced_wpm >= RIGHT_BLAHAJ_TURBO_WPM) {
            current_pose = alt_phase ? RIGHT_BLAHAJ_TURBO_A : RIGHT_BLAHAJ_TURBO_B;
        } else {
            current_pose = alt_phase ? RIGHT_BLAHAJ_CRUISE_A : RIGHT_BLAHAJ_CRUISE_B;
        }
        should_render = true;
    }
    k_mutex_unlock(&right_blahaj_mutex);

    k_work_reschedule(&right_idle_work, K_MSEC(RIGHT_BLAHAJ_IDLE_DELAY_MS));
    k_work_reschedule(&right_sleep_work, K_MSEC(RIGHT_BLAHAJ_SLEEP_DELAY_MS));

    if (should_render) {
        schedule_right_render();
    }
}

static int behavior_pet_sync_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static int on_pet_sync_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    bool is_pulse = false;
    bool tier_changed = false;

    k_mutex_lock(&right_blahaj_mutex, K_FOREVER);
    uint8_t new_wpm = (uint8_t)binding->param1;
    bool was_turbo = (synced_wpm >= RIGHT_BLAHAJ_TURBO_WPM);
    bool is_turbo = (new_wpm >= RIGHT_BLAHAJ_TURBO_WPM);
    synced_wpm = new_wpm;

    if (binding->param2 != last_pulse_seq) {
        last_pulse_seq = binding->param2;
        is_pulse = true;
    } else if (was_turbo != is_turbo &&
               (current_pose == RIGHT_BLAHAJ_CRUISE_A || current_pose == RIGHT_BLAHAJ_CRUISE_B ||
                current_pose == RIGHT_BLAHAJ_TURBO_A || current_pose == RIGHT_BLAHAJ_TURBO_B)) {
        current_pose = is_turbo ? (alt_phase ? RIGHT_BLAHAJ_TURBO_A : RIGHT_BLAHAJ_TURBO_B)
                                : (alt_phase ? RIGHT_BLAHAJ_CRUISE_A : RIGHT_BLAHAJ_CRUISE_B);
        tier_changed = true;
    }
    k_mutex_unlock(&right_blahaj_mutex);

    if (is_pulse) {
        trigger_typing_step();
    } else if (tier_changed) {
        schedule_right_render();
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_pet_sync_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_pet_sync_driver_api = {
    .binding_pressed = on_pet_sync_pressed,
    .binding_released = on_pet_sync_released,
};

DEVICE_DEFINE(behavior_pet_sync, PET_SYNC_BEHAVIOR_NAME, behavior_pet_sync_init, NULL, NULL, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_pet_sync_driver_api);

static const STRUCT_SECTION_ITERABLE(zmk_behavior_ref, zmk_behavior_pet_sync_ref) = {
    .device = DEVICE_GET(behavior_pet_sync),
    .metadata = {},
};

static int right_local_key_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos_ev = as_zmk_position_state_changed(eh);
    if (pos_ev != NULL && pos_ev->state) {
        trigger_typing_step();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(right_blahaj_local, right_local_key_listener);
ZMK_SUBSCRIPTION(right_blahaj_local, zmk_position_state_changed);

void right_blahaj_widget_init(lv_obj_t *parent) {
    right_shark_img = lv_img_create(parent);
    lv_obj_align(right_shark_img, LV_ALIGN_TOP_LEFT, 0, 0);

    right_blahaj_render_cb(NULL);
    k_work_reschedule(&right_sleep_work, K_MSEC(RIGHT_BLAHAJ_SLEEP_DELAY_MS));
}
