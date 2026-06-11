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
 * Power: the keepalive only matters while the panel is actually lit, so it is
 * gated twice. The work item only runs while ZMK activity is ACTIVE (it is
 * cancelled on IDLE/SLEEP), so an idle keyboard never wakes for it. And each
 * tick only forces a redraw when external power (the display rail) is on, so a
 * powered-off panel costs nothing but a cheap GPIO read. ZMK raises no event on
 * an ext-power toggle, so we cannot react to it directly; gating the redraw on
 * ext_power_get() is the closest we get without patching ZMK.
 *
 * Enabled automatically for builds that include the nice_view_mip shield
 * (the keyboard halves); see boards/shields/nice_view_mip/Kconfig.defconfig.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>

#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ext_power_generic) && IS_ENABLED(CONFIG_ZMK_EXT_POWER)
#include <drivers/ext_power.h>
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));
/* True only when the display power rail is actually on. */
#define EXT_POWER_IS_ON() (device_is_ready(ext_power) && ext_power_get(ext_power) == 1)
#else
/* No controllable ext power on this build: the panel is always powered. */
#define EXT_POWER_IS_ON() true
#endif

#define KEEPALIVE_INTERVAL K_MSEC(CONFIG_NICE_VIEW_VCOM_KEEPALIVE_INTERVAL_MS)

static void vcom_keepalive_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(vcom_keepalive_work, vcom_keepalive_tick);

static void vcom_keepalive_tick(struct k_work *work) {
    /* Only a powered, lit panel needs VCOM upkeep; if the display rail is off
     * there is nothing to refresh, so skip the (expensive) full-frame redraw. */
    if (zmk_display_is_initialized() && EXT_POWER_IS_ON()) {
        /* Mark the whole screen dirty; the next display tick flushes a full
         * frame to the panel, toggling VCOM. */
        lv_obj_invalidate(lv_scr_act());
    }

    k_work_reschedule_for_queue(zmk_display_work_q(), &vcom_keepalive_work, KEEPALIVE_INTERVAL);
}

/* Run the keepalive only while the keyboard is active. When it goes idle/asleep
 * the panel is blanked or unpowered anyway, so stop waking the CPU entirely. */
static int vcom_keepalive_activity_listener(const zmk_event_t *eh) {
    if (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE) {
        k_work_reschedule_for_queue(zmk_display_work_q(), &vcom_keepalive_work, KEEPALIVE_INTERVAL);
    } else {
        k_work_cancel_delayable(&vcom_keepalive_work);
    }
    return 0;
}

ZMK_LISTENER(vcom_keepalive, vcom_keepalive_activity_listener);
ZMK_SUBSCRIPTION(vcom_keepalive, zmk_activity_state_changed);

static int vcom_keepalive_init(void) {
    /* Boot state is ACTIVE and the boot screen is shown, so start ticking now;
     * the activity listener takes over from here. */
    k_work_reschedule_for_queue(zmk_display_work_q(), &vcom_keepalive_work, KEEPALIVE_INTERVAL);
    return 0;
}

SYS_INIT(vcom_keepalive_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
