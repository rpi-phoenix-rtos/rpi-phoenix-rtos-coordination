/*
 * Phoenix-RTOS
 *
 * rpi4-kms host test - minimal stand-in for Phoenix <posix/utils.h> (the device
 * node calls kms_main.c makes; served by hosttest/mode_test.c).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _KMS_SHIM_POSIX_UTILS_H_
#define _KMS_SHIM_POSIX_UTILS_H_

#include <sys/msg.h>

int create_dev(oid_t *oid, const char *path);
int destroy_dev(const char *path);

#endif
