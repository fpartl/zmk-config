/*
 * Custom status screen for the keyboard halves: our own peripheral widget
 * (battery + connection) with a custom art image. Replaces nice_view's screen,
 * which is disabled via CONFIG_NICE_VIEW_WIDGET_STATUS=n so this strong
 * definition of zmk_display_status_screen() wins over ZMK's weak fallback.
 * SPDX-License-Identifier: MIT
 */

#include "peripheral_status.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static struct zmk_widget_status status_widget;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    zmk_widget_status_init(&status_widget, screen);
    lv_obj_align(zmk_widget_status_obj(&status_widget), LV_ALIGN_TOP_LEFT, 0, 0);
    return screen;
}
