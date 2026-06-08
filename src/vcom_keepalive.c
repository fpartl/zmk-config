/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * nice!view (MIP / Sharp memory LCD) VCOM keepalive.
 *
 * Sharp memory-in-pixel displays need their VCOM polarity toggled periodically
 * or DC bias builds up and the image decays into garbage ("rozsypane pixely").
 * The Zephyr ls0xx driver only toggles VCOM when it sends an SPI frame, and ZMK
 * only sends a frame when an LVGL widget is invalidated. On a split *peripheral*
 * the nice!view widget updates very rarely (no WPM/clock, unlike the central),
 * so VCOM nearly stops toggling and the screen decays until the next redraw.
 *
 * This module forces a full-screen redraw on a fixed interval, which makes the
 * ls0xx driver re-issue a frame and toggle VCOM, keeping the image clean. The
 * work is scheduled on the ZMK display work queue, so it is serialized with
 * lv_task_handler() and is safe to call LVGL from.
 *
 * Enabled automatically for builds that include the nice_view_mip shield
 * (the keyboard halves); see boards/shields/nice_view_mip/Kconfig.defconfig.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>

#include <lvgl.h>

#include <zmk/display.h>

#define KEEPALIVE_INTERVAL K_MSEC(CONFIG_NICE_VIEW_VCOM_KEEPALIVE_INTERVAL_MS)

static void vcom_keepalive_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(vcom_keepalive_work, vcom_keepalive_tick);

static void vcom_keepalive_tick(struct k_work *work) {
    if (zmk_display_is_initialized()) {
        /* Mark the whole screen dirty; the next display tick flushes a full
         * frame to the panel, toggling VCOM. */
        lv_obj_invalidate(lv_scr_act());
    }

    k_work_schedule_for_queue(zmk_display_work_q(), &vcom_keepalive_work, KEEPALIVE_INTERVAL);
}

static int vcom_keepalive_init(void) {
    k_work_schedule_for_queue(zmk_display_work_q(), &vcom_keepalive_work, KEEPALIVE_INTERVAL);
    return 0;
}

SYS_INIT(vcom_keepalive_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
