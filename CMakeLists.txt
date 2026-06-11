# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

target_sources_ifdef(CONFIG_NICE_VIEW_VCOM_KEEPALIVE app PRIVATE src/vcom_keepalive.c)
target_sources_ifdef(CONFIG_ZMK_DONGLE_USB_BATTERY app PRIVATE src/usb_battery_hid.c)
