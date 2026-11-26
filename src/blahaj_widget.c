#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/wpm_state_changed.h>

#include "blahaj_widget.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define BLAHAJ_FLAP_MIN_MS 80
#define BLAHAJ_IDLE_DELAY_MS 1200
#define BLAHAJ_SLEEP_DELAY_MS 5000

enum blahaj_pose {
    BLAHAJ_SLEEP = 0,
    BLAHAJ_IDLE,
    BLAHAJ_TAP_LEFT,
    BLAHAJ_TAP_RIGHT,
};

K_MUTEX_DEFINE(blahaj_mutex);

static lv_obj_t *shark_img;
static lv_obj_t *wpm_title_label;
static lv_obj_t *wpm_value_label;

static enum blahaj_pose current_pose = BLAHAJ_IDLE;
static uint8_t current_wpm;
static int64_t last_flap_ms;

static void blahaj_render_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    enum blahaj_pose pose;
    uint8_t wpm;

    k_mutex_lock(&blahaj_mutex, K_FOREVER);
    pose = current_pose;
    wpm = current_wpm;
    k_mutex_unlock(&blahaj_mutex);

    if (shark_img != NULL) {
        const lv_img_dsc_t *src = &blahaj_idle;
        switch (pose) {
        case BLAHAJ_SLEEP:
            src = &blahaj_sleep;
            break;
        case BLAHAJ_TAP_LEFT:
            src = &blahaj_tap_left;
            break;
        case BLAHAJ_TAP_RIGHT:
            src = &blahaj_tap_right;
            break;
        case BLAHAJ_IDLE:
        default:
            src = &blahaj_idle;
            break;
        }
        lv_img_set_src(shark_img, src);
    }

    if (wpm_value_label != NULL) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", wpm);
        lv_label_set_text(wpm_value_label, buf);
    }
}

K_WORK_DEFINE(blahaj_render_work, blahaj_render_work_cb);

static void schedule_render(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &blahaj_render_work);
    }
}

static void blahaj_idle_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    k_mutex_lock(&blahaj_mutex, K_FOREVER);
    current_pose = BLAHAJ_IDLE;
    k_mutex_unlock(&blahaj_mutex);
    schedule_render();
}

static void blahaj_sleep_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    k_mutex_lock(&blahaj_mutex, K_FOREVER);
    current_pose = BLAHAJ_SLEEP;
    k_mutex_unlock(&blahaj_mutex);
    schedule_render();
}

K_WORK_DELAYABLE_DEFINE(blahaj_idle_work, blahaj_idle_work_cb);
K_WORK_DELAYABLE_DEFINE(blahaj_sleep_work, blahaj_sleep_work_cb);

static int blahaj_event_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos_ev = as_zmk_position_state_changed(eh);
    if (pos_ev != NULL && pos_ev->state) {
        int64_t now = k_uptime_get();
        bool should_render = false;

        k_mutex_lock(&blahaj_mutex, K_FOREVER);
        if ((now - last_flap_ms) >= BLAHAJ_FLAP_MIN_MS) {
            enum blahaj_pose preferred =
                (pos_ev->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) ? BLAHAJ_TAP_LEFT
                                                                           : BLAHAJ_TAP_RIGHT;
            last_flap_ms = now;
            if (current_pose == preferred) {
                current_pose =
                    (preferred == BLAHAJ_TAP_LEFT) ? BLAHAJ_TAP_RIGHT : BLAHAJ_TAP_LEFT;
            } else {
                current_pose = preferred;
            }
            should_render = true;
        }
        k_mutex_unlock(&blahaj_mutex);

        k_work_reschedule(&blahaj_idle_work, K_MSEC(BLAHAJ_IDLE_DELAY_MS));
        k_work_reschedule(&blahaj_sleep_work, K_MSEC(BLAHAJ_SLEEP_DELAY_MS));

        if (should_render) {
            schedule_render();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_wpm_state_changed *wpm_ev = as_zmk_wpm_state_changed(eh);
    if (wpm_ev != NULL) {
        k_mutex_lock(&blahaj_mutex, K_FOREVER);
        current_wpm = (wpm_ev->state > 0) ? (uint8_t)MIN(wpm_ev->state, 255) : 0;
        k_mutex_unlock(&blahaj_mutex);
        schedule_render();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(blahaj_widget, blahaj_event_listener);
ZMK_SUBSCRIPTION(blahaj_widget, zmk_position_state_changed);
ZMK_SUBSCRIPTION(blahaj_widget, zmk_wpm_state_changed);

void blahaj_widget_init(lv_obj_t *parent) {
    shark_img = lv_img_create(parent);
    lv_obj_align(shark_img, LV_ALIGN_LEFT_MID, 0, 0);

    wpm_title_label = lv_label_create(parent);
    lv_obj_set_style_text_font(wpm_title_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text(wpm_title_label, LV_SYMBOL_USB " WPM");
    lv_obj_align(wpm_title_label, LV_ALIGN_TOP_RIGHT, -2, 1);

    wpm_value_label = lv_label_create(parent);
    lv_obj_set_style_text_font(wpm_value_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(wpm_value_label, "0");
    lv_obj_align(wpm_value_label, LV_ALIGN_BOTTOM_RIGHT, -4, -1);

    blahaj_render_work_cb(NULL);
    k_work_reschedule(&blahaj_sleep_work, K_MSEC(BLAHAJ_SLEEP_DELAY_MS));
}
