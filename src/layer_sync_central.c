#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LAYER_SYNC_BEHAVIOR_NAME "layer_sync"

static uint8_t last_sent_layer = UINT8_MAX;
static bool peripheral_seen;

static void layer_sync_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t layer = zmk_keymap_highest_layer_active();
    struct zmk_behavior_binding binding = {
        .behavior_dev = LAYER_SYNC_BEHAVIOR_NAME,
        .param1 = layer,
        .param2 = 0,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };

    for (uint8_t src = 0; src < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; src++) {
        zmk_split_central_invoke_behavior(src, &binding, event, true);
    }

    last_sent_layer = layer;
}

K_WORK_DEFINE(layer_sync_work, layer_sync_work_cb);

static int layer_sync_central_listener(const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) != NULL) {
        if (zmk_keymap_highest_layer_active() != last_sent_layer) {
            k_work_submit(&layer_sync_work);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *pos_ev = as_zmk_position_state_changed(eh);
    if (pos_ev != NULL && pos_ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL &&
        !peripheral_seen) {
        peripheral_seen = true;
        k_work_submit(&layer_sync_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_sync_central, layer_sync_central_listener);
ZMK_SUBSCRIPTION(layer_sync_central, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(layer_sync_central, zmk_position_state_changed);
