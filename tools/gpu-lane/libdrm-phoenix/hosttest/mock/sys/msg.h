/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix host harness - minimal stand-in for Phoenix <sys/msg.h>
 * (only what the libdrm-phoenix backend uses; served by fake.c).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _MOCK_SYS_MSG_H_
#define _MOCK_SYS_MSG_H_

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t port;
	uint64_t id;
} oid_t;

typedef int msg_rid_t;

enum { mtOpen = 0, mtClose, mtRead, mtWrite, mtTruncate, mtDevCtl };

typedef struct {
	int type;
	int pid;
	int priority;
	oid_t oid;
	struct {
		unsigned char raw[64];
		size_t size;
		const void *data;
	} i;
	struct {
		unsigned char raw[64];
		size_t size;
		void *data;
		int err;
	} o;
} msg_t;

#ifndef _PAGE_SIZE
#define _PAGE_SIZE 4096UL
#endif

int msgSend(uint32_t port, msg_t *m);
int lookup(const char *name, oid_t *file, oid_t *dev);

#endif
