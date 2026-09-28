/*
 * Phoenix-RTOS
 *
 * rpi4-kms host test - minimal stand-in for Phoenix <sys/msg.h>: the types and
 * calls kms_bo.c and kms_main.c use, with the kernel's field names (phoenix/msg.h).
 * The calls are served by hosttest/bo_alias_test.c and hosttest/mode_test.c.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _KMS_SHIM_SYS_MSG_H_
#define _KMS_SHIM_SYS_MSG_H_

#include <stddef.h>
#include <stdint.h>

#define MAP_PHYSMEM    0x10000000
#define MAP_UNCACHED   0x20000000
#define MAP_CONTIGUOUS 0x40000000

#ifndef _PAGE_SIZE
#define _PAGE_SIZE 4096UL
#endif

typedef uintptr_t addr_t;
typedef int handle_t;
typedef int msg_rid_t;

typedef struct {
	uint32_t port;
	uint64_t id;
} oid_t;

enum { mtOpen = 0, mtClose, mtRead, mtWrite, mtTruncate, mtDevCtl, mtCreate, mtDestroy, mtSetAttr, mtGetAttr,
	mtGetAttrAll, mtLookup };

enum { atMode = 0, atUid, atGid, atSize, atBlocks, atIOBlock, atType, atPort, atPollStatus, atEventMask, atCTime,
	atMTime, atATime, atLinks, atDev };

enum { otDir = 0, otFile, otDev, otSymlink, otUnknown };

struct _attr {
	long long val;
	int err;
};

struct _attrAll {
	struct _attr mode, uid, gid, size, blocks, ioblock, type, port, pollStatus, eventMask, cTime, mTime, aTime, links, dev;
};

typedef struct {
	int type;
	int pid;
	int priority;
	oid_t oid;
	struct {
		union {
			struct {
				long long val;
				int type;
			} attr;
			struct {
				long long offs;
				size_t len;
				unsigned mode;
			} io;
			unsigned char raw[64];
		};
		size_t size;
		const void *data;
	} i;
	struct {
		union {
			struct {
				long long val;
			} attr;
			struct {
				oid_t fil;
				oid_t dev;
			} lookup;
			unsigned char raw[64];
		};
		int err;
		size_t size;
		void *data;
	} o;
} msg_t;

#ifndef EOK
#define EOK 0
#endif

int msgSend(uint32_t port, msg_t *m);
int msgRecv(uint32_t port, msg_t *m, msg_rid_t *rid);
int portCreate(uint32_t *port);
int portRegister(uint32_t port, const char *name, oid_t *oid);
int portUnregister(const char *name);
int msgRespond(uint32_t port, msg_t *m, msg_rid_t rid);
int lookup(const char *name, oid_t *file, oid_t *dev);
int memExport(oid_t *oid, void *va, size_t size);
int memUnexport(oid_t *oid);
addr_t va2pa(void *va);

#endif
