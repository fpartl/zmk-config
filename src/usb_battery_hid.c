/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Report split peripheral battery level to a USB-connected host.
 *
 * In dongle mode the host talks to the dongle (central) over USB HID. ZMK only
 * reports battery through the BLE Battery Service, which a USB host never sees,
 * so the OS gets no battery info at all and cannot warn on low charge.
 *
 * This module adds a SECOND USB HID interface (HID_1) exposing a Generic Device
 * Controls "Battery Strength" field. The Linux HID core maps usage 0x0006/0x0020
 * (HID_DC_BATTERYSTRENGTH) to a power_supply, so upower and the desktop pick it
 * up like any other device battery; Windows reads it the same way. The value
 * reported is the LOWEST charge among the connected split peripherals (the
 * keyboard halves), so a low reading on either half — in particular the one
 * without a usable display — raises the OS warning.
 *
 * Why the top-level collection is a Keyboard with a dummy modifier byte:
 * Linux registers an input-report battery as part of an input device, then
 * DISCARDS that input device — and the battery with it — if the device has no
 * "populated" EV_* capability (hidinput_cleanup_hidinput). Crucially, whether a
 * field populates anything depends on the collection's top-level Application:
 * inside an unrecognized application (e.g. Battery Strength itself) the kernel
 * maps every usage to Sync.Report (ignored), so even a Button stays inert and
 * the input is dropped (verified via /sys/kernel/debug/hid/.../rdesc). Wrapping
 * the report in a Keyboard application makes the 8 modifier bits map to EV_KEY,
 * so the input device is populated and kept — exactly how a real keyboard with a
 * battery field is handled. The modifiers are never reported, so they are inert.
 *
 * The level is delivered by sending input reports (Linux updates the HID battery
 * from received input reports, not from on-demand GET_REPORT — a GET_REPORT-only
 * device just reads back 0). A periodic work pushes the current level and also
 * resends on change; get_report_cb additionally answers any GET_REPORT poll.
 *
 * Battery data comes from the central's peripheral battery fetching
 * (CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING), which emits
 * zmk_peripheral_battery_state_changed events; see central_bas_proxy.c for the
 * BLE-host counterpart of this.
 *
 * Requires CONFIG_USB_HID_DEVICE_COUNT >= 2 so the "HID_1" device exists.
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

LOG_MODULE_REGISTER(usb_battery_hid, CONFIG_ZMK_LOG_LEVEL);

#define NUM_PERIPHERALS CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS

/* Single report (no report ID): a dummy keyboard modifier byte purely to give
 * the input device an EV_KEY capability (so Linux keeps the battery), followed
 * by a 0..100 Generic Device Controls Battery Strength byte. See file header. */
static const uint8_t battery_report_desc[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop)          */
    0x09, 0x06, /* Usage (Keyboard)                      */
    0xA1, 0x01, /* Collection (Application)              */
    0x05, 0x07, /*   Usage Page (Keyboard/Keypad)        */
    0x19, 0xE0, /*   Usage Minimum (Left Control)        */
    0x29, 0xE7, /*   Usage Maximum (Right GUI)           */
    0x15, 0x00, /*   Logical Minimum (0)                 */
    0x25, 0x01, /*   Logical Maximum (1)                 */
    0x75, 0x01, /*   Report Size (1)                     */
    0x95, 0x08, /*   Report Count (8)                    */
    0x81, 0x02, /*   Input (Data,Var,Abs) -> modifiers   */
    0x05, 0x06, /*   Usage Page (Generic Device Controls) */
    0x09, 0x20, /*   Usage (Battery Strength)            */
    0x15, 0x00, /*   Logical Minimum (0)                 */
    0x25, 0x64, /*   Logical Maximum (100)               */
    0x75, 0x08, /*   Report Size (8)                     */
    0x95, 0x01, /*   Report Count (1)                    */
    0x81, 0x02, /*   Input (Data,Var,Abs) -> battery     */
    0xC0,       /* End Collection                        */
};

/* Report payload: inert modifier byte + battery byte. */
struct battery_report {
    uint8_t modifiers;
    uint8_t level;
} __packed;

/* How often the dongle re-asserts the level to the host. This runs on the
 * USB-powered dongle and never touches the halves, so it costs no peripheral
 * battery; it only needs to be frequent enough to correct a cold-start or
 * post-resume 0% reasonably quickly. The real values arrive immediately on each
 * peripheral battery event (see the listener), so this is just a safety net. */
#define BATTERY_PUSH_INTERVAL K_SECONDS(300)

static const struct device *hid_dev;

/* Signalled when the IN endpoint is free again, so writes don't race. */
static K_SEM_DEFINE(ep_write_sem, 1, 1);

static void battery_push_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(battery_push_work, battery_push_work_cb);

static uint8_t periph_level[NUM_PERIPHERALS];
static bool periph_known[NUM_PERIPHERALS];

/* Lowest known peripheral charge; 100 until anything has reported, so the host
 * never sees a spurious 0% (critical) at boot before the halves check in. */
static uint8_t lowest_known_level(void) {
    uint8_t level = 100;
    for (int i = 0; i < NUM_PERIPHERALS; i++) {
        if (periph_known[i] && periph_level[i] < level) {
            level = periph_level[i];
        }
    }
    return level;
}

/* Serves the host's GET_REPORT polling with the current level. */
static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    static struct battery_report report;
    report.modifiers = 0;
    report.level = lowest_known_level();
    *data = (uint8_t *)&report;
    *len = sizeof(report);
    return 0;
}

static void int_in_ready_cb(const struct device *dev) { k_sem_give(&ep_write_sem); }

static const struct hid_ops ops = {
    .get_report = get_report_cb,
    .int_in_ready = int_in_ready_cb,
};

static void send_battery_report(void) {
    if (hid_dev == NULL) {
        return;
    }

    /* If the previous transfer is still pending (host not polling yet) just
     * skip; the periodic work and the next event will resend. */
    if (k_sem_take(&ep_write_sem, K_MSEC(50)) != 0) {
        return;
    }

    struct battery_report report = {.modifiers = 0, .level = lowest_known_level()};
    int rc = hid_int_ep_write(hid_dev, (uint8_t *)&report, sizeof(report), NULL);
    if (rc != 0) {
        k_sem_give(&ep_write_sem);
    }
}

/* Periodically resend so the host's reading stays current and a cold-start 0%
 * (before any peripheral has reported) is corrected to the 100% floor. */
static void battery_push_work_cb(struct k_work *work) {
    send_battery_report();
    k_work_reschedule(&battery_push_work, BATTERY_PUSH_INTERVAL);
}

static int peripheral_batt_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL || ev->source >= NUM_PERIPHERALS) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    periph_level[ev->source] = ev->state_of_charge;
    periph_known[ev->source] = true;
    LOG_DBG("Peripheral %u battery %u%%, reporting lowest %u%% to USB host", ev->source,
            ev->state_of_charge, lowest_known_level());

    send_battery_report();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_battery_hid, peripheral_batt_listener);
ZMK_SUBSCRIPTION(usb_battery_hid, zmk_peripheral_battery_state_changed);

static int usb_battery_hid_init(void) {
    hid_dev = device_get_binding("HID_1");
    if (hid_dev == NULL) {
        LOG_ERR("Unable to locate HID_1 device (need CONFIG_USB_HID_DEVICE_COUNT >= 2)");
        return -EINVAL;
    }

    usb_hid_register_device(hid_dev, battery_report_desc, sizeof(battery_report_desc), &ops);
    usb_hid_init(hid_dev);

    /* First push a few seconds after boot (once USB is up), then periodically. */
    k_work_schedule(&battery_push_work, K_SECONDS(3));
    return 0;
}

/* Same init level as ZMK's own HID_0 registration (see usb_hid.c). */
SYS_INIT(usb_battery_hid_init, APPLICATION, CONFIG_ZMK_USB_HID_INIT_PRIORITY);
