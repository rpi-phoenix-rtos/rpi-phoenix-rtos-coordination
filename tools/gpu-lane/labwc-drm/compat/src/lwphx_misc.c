/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * labwc-drm compat: small libphoenix gaps.
 */

#include <errno.h>
#include <pthread.h>
#include <string.h>

/* Thread names are a debugging aid; Phoenix-RTOS threads have none. Linux limits
 * a name to 15 bytes and refuses longer ones with ERANGE: keep that contract. */
int pthread_setname_np(pthread_t thread, const char *name)
{
	(void)thread;
	return (strlen(name) > 15u) ? ERANGE : 0;
}
