/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* labwc-drm host tests: the CHECK/RESULT convention of weston-drm/hosttest. */
#ifndef LWPHX_HOSTTEST_H
#define LWPHX_HOSTTEST_H

#include <stdio.h>

static int fails, checks;

#define CHECK(cond, ...) \
	do { \
		checks++; \
		if (cond) { \
			printf("ok   "); \
		} \
		else { \
			printf("FAIL "); \
			fails++; \
		} \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (0)

#define RESULT(name) \
	(printf("LWHOST %s checks=%d fails=%d verdict=%s\n", (name), checks, fails, (fails == 0) ? "PASS" : "FAIL"), \
		(fails == 0) ? 0 : 1)

#endif
