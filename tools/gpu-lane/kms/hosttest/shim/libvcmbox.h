/*
 * Phoenix-RTOS
 *
 * rpi4-kms host test - stand-in for rpi4-vcmbox's libvcmbox.h: the two mailbox
 * calls kms_backend.c makes. hosttest/mode_test.c answers them and records every
 * SET_PLANE value the backend sends.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _KMS_SHIM_LIBVCMBOX_H_
#define _KMS_SHIM_LIBVCMBOX_H_

#include <stdint.h>

int vcmbox_call(uint32_t tag, uint32_t valBufSize, const uint32_t *in, uint32_t nIn, uint32_t *out, uint32_t nOut);
int vcmbox_callXL(uint32_t tag, const void *in, uint32_t valBufSize, void *out);

#endif
