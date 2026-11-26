#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#endif

#include "layer_sync_peripheral.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LAYER_SYNC_BEHAVIOR_NAME "layer_sync"

/* Keep in sync with layer order in config/corne.keymap. */
static const char *const layer_names[] = {
    "Base",
    "Num",
    "Sym",
    "Fn",
    "Nav",
    "Mouse",
    "Media",
    "Spare 7",
    "Spare 8",
    "Spare 9",
};

K_MUTEX_DEFINE(layer_sync_mutex);

static lv_obj_t *layer_index_label;
static lv_obj_t *layer_name_label;
static lv_obj_t *link_status_label;

static uint8_t current_layer;
static bool link_connected;

static void layer_render_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t layer;
    bool connected;

    k_mutex_lock(&layer_sync_mutex, K_FOREVER);
    layer = current_layer;
    connected = link_connected;
    k_mutex_unlock(&layer_sync_mutex);

    if (layer_index_label != NULL) {
        char idx_buf[16];
        snprintf(idx_buf, sizeof(idx_buf), "LAYER %u", layer);
        lv_label_set_text(layer_index_label, idx_buf);
    }

    if (layer_name_label != NULL) {
        char name_buf[24];
        if (layer < ARRAY_SIZE(layer_names)) {
            snprintf(name_buf, sizeof(name_buf), LV_SYMBOL_KEYBOARD " %s", layer_names[layer]);
        } else {
            snprintf(name_buf, sizeof(name_buf), LV_SYMBOL_KEYBOARD " L%u", layer);
        }
        lv_label_set_text(layer_name_label, name_buf);
    }

    if (link_status_label != NULL) {
        lv_label_set_text(link_status_label, connected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    }
}

K_WORK_DEFINE(layer_render_work, layer_render_work_cb);

static void schedule_layer_render(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &layer_render_work);
    }
}

static int behavior_layer_sync_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static int on_layer_sync_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    k_mutex_lock(&layer_sync_mutex, K_FOREVER);
    current_layer = (uint8_t)binding->param1;
    link_connected = true;
    k_mutex_unlock(&layer_sync_mutex);

    schedule_layer_render();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_layer_sync_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_layer_sync_driver_api = {
    .binding_pressed = on_layer_sync_pressed,
    .binding_released = on_layer_sync_released,
};

DEVICE_DEFINE(behavior_layer_sync, LAYER_SYNC_BEHAVIOR_NAME, behavior_layer_sync_init, NULL, NULL,
              NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
              &behavior_layer_sync_driver_api);

static const STRUCT_SECTION_ITERABLE(zmk_behavior_ref, zmk_behavior_layer_sync_ref) = {
    .device = DEVICE_GET(behavior_layer_sync),
    .metadata = {},
};

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
static int link_status_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    k_mutex_lock(&layer_sync_mutex, K_FOREVER);
    link_connected = zmk_split_bt_peripheral_is_connected();
    k_mutex_unlock(&layer_sync_mutex);

    schedule_layer_render();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_sync_link, link_status_listener);
ZMK_SUBSCRIPTION(layer_sync_link, zmk_split_peripheral_status_changed);
#endif

void layer_sync_peripheral_widget_init(lv_obj_t *parent) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    k_mutex_lock(&layer_sync_mutex, K_FOREVER);
    link_connected = zmk_split_bt_peripheral_is_connected();
    k_mutex_unlock(&layer_sync_mutex);
#else
    k_mutex_lock(&layer_sync_mutex, K_FOREVER);
    link_connected = true;
    k_mutex_unlock(&layer_sync_mutex);
#endif

    layer_index_label = lv_label_create(parent);
    lv_obj_set_style_text_font(layer_index_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(layer_index_label, LV_ALIGN_TOP_LEFT, 2, 1);

    link_status_label = lv_label_create(parent);
    lv_obj_set_style_text_font(link_status_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(link_status_label, LV_ALIGN_TOP_RIGHT, -2, 1);

    layer_name_label = lv_label_create(parent);
    lv_obj_set_style_text_font(layer_name_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(layer_name_label, LV_ALIGN_BOTTOM_LEFT, 2, -1);

    layer_render_work_cb(NULL);
}
