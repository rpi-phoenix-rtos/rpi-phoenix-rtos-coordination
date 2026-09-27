/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * weston-drm shim: <linux/input.h> for Phoenix-RTOS. Weston and its clients use
 * only the evdev event codes (KEY_, BTN_, EV_, ABS_ and REL_ names), which come from
 * FreeBSD's BSD-licensed copy, sys/dev/evdev/input-event-codes.h (build.sh places
 * it in <prefix>/include/evdev/). The event structure is FreeBSD's layout.
 */
#ifndef WLPHX_LINUX_INPUT_H
#define WLPHX_LINUX_INPUT_H

#include <stdint.h>
#include <sys/time.h>

#include <evdev/input-event-codes.h>

struct input_event {
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

struct input_id {
	uint16_t bustype;
	uint16_t vendor;
	uint16_t product;
	uint16_t version;
};

#endif
