#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/split/central.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Must stay <= 8 chars to fit ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN (9). */
#define PET_SYNC_BEHAVIOR_NAME "petSync"
#define PET_SYNC_MIN_INTERVAL_MS 180
#define PET_SYNC_TURBO_WPM 60

K_MUTEX_DEFINE(pet_sync_mutex);

static uint8_t latest_wpm;
static uint32_t pulse_seq;
static int64_t last_send_ms;

static void pet_sync_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t wpm;
    uint32_t seq;

    k_mutex_lock(&pet_sync_mutex, K_FOREVER);
    wpm = latest_wpm;
    seq = pulse_seq;
    k_mutex_unlock(&pet_sync_mutex);

    struct zmk_behavior_binding binding = {
        .behavior_dev = PET_SYNC_BEHAVIOR_NAME,
        .param1 = wpm,
        .param2 = seq,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };

    for (uint8_t src = 0; src < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; src++) {
        zmk_split_central_invoke_behavior(src, &binding, event, true);
    }
}

K_WORK_DEFINE(pet_sync_work, pet_sync_work_cb);

static int pet_sync_central_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos_ev = as_zmk_position_state_changed(eh);
    if (pos_ev != NULL && pos_ev->state) {
        int64_t now = k_uptime_get();
        bool should_send = false;

        k_mutex_lock(&pet_sync_mutex, K_FOREVER);
        if ((now - last_send_ms) >= PET_SYNC_MIN_INTERVAL_MS) {
            last_send_ms = now;
            pulse_seq++;
            should_send = true;
        }
        k_mutex_unlock(&pet_sync_mutex);

        if (should_send) {
            k_work_submit(&pet_sync_work);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_wpm_state_changed *wpm_ev = as_zmk_wpm_state_changed(eh);
    if (wpm_ev != NULL) {
        uint8_t new_wpm = (wpm_ev->state > 0) ? (uint8_t)MIN(wpm_ev->state, 255) : 0;
        bool should_send = false;

        k_mutex_lock(&pet_sync_mutex, K_FOREVER);
        bool old_turbo = (latest_wpm >= PET_SYNC_TURBO_WPM);
        bool new_turbo = (new_wpm >= PET_SYNC_TURBO_WPM);
        if (old_turbo != new_turbo || (new_wpm == 0 && latest_wpm != 0)) {
            should_send = true;
        }
        latest_wpm = new_wpm;
        k_mutex_unlock(&pet_sync_mutex);

        if (should_send) {
            k_work_submit(&pet_sync_work);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(pet_sync_central, pet_sync_central_listener);
ZMK_SUBSCRIPTION(pet_sync_central, zmk_position_state_changed);
ZMK_SUBSCRIPTION(pet_sync_central, zmk_wpm_state_changed);
