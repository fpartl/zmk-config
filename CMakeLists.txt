# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

target_sources_ifdef(CONFIG_NICE_VIEW_VCOM_KEEPALIVE app PRIVATE src/vcom_keepalive.c)
