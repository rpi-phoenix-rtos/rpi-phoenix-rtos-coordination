/*
 * Phoenix-RTOS
 *
 * rpi4-kms host test - minimal stand-in for Phoenix <sys/threads.h> (the test is
 * single-threaded: the server mutex is a counter the test checks).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _KMS_SHIM_SYS_THREADS_H_
#define _KMS_SHIM_SYS_THREADS_H_

#include <sys/msg.h>

int mutexLock(handle_t h);
int mutexUnlock(handle_t h);
int mutexCreate(handle_t *h);
int condCreate(handle_t *h);
int condSignal(handle_t h);
int beginthread(void (*start)(void *), int priority, void *stack, unsigned int stacksz, void *arg);
int setPriority(int priority);

#endif
