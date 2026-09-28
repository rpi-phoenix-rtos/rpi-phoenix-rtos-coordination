/*
 * Phoenix-RTOS
 *
 * rpi4-kms host test - minimal stand-in for Phoenix <sys/ioctl.h>: ioctl() and
 * the ioctl message helpers kms_main.c uses (served by hosttest/mode_test.c).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _KMS_SHIM_SYS_IOCTL_H_
#define _KMS_SHIM_SYS_IOCTL_H_

#include <sys/types.h>
#include <sys/msg.h>

int ioctl(int fd, unsigned long request, ...);
const void *ioctl_unpack(msg_t *msg, unsigned long *request, id_t *id);
void ioctl_setResponse(msg_t *msg, unsigned long request, int err, const void *data);

#endif
