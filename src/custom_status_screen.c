#include <zephyr/sys/util.h>
#include <lvgl.h>
#include <zmk/display/status_screen.h>

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include "blahaj_widget.h"
#else
#include "layer_sync_peripheral.h"
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    blahaj_widget_init(screen);
#else
    layer_sync_peripheral_widget_init(screen);
#endif

    return screen;
}
