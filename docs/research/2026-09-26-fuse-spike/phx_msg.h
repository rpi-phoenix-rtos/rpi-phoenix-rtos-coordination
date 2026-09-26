/* SPDX-License-Identifier: BSD-3-Clause -- host-only spike for the FUSE feasibility study (2026-09-26) */
/* Host mock of the Phoenix-RTOS message ABI (copied from
 * sources/phoenix-rtos-kernel/include/msg.h, file.h, types.h and
 * libphoenix include/sys/file.h, include/dirent.h). On a real Phoenix build
 * these come from <sys/msg.h>, <sys/file.h>, <dirent.h>. */
#ifndef PHX_MSG_H
#define PHX_MSG_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

typedef uint64_t phx_id_t;
typedef struct { uint32_t port; phx_id_t id; } oid_t;

enum { mtOpen = 0, mtClose, mtRead, mtWrite, mtTruncate, mtDevCtl,
	mtCreate, mtDestroy, mtSetAttr, mtGetAttr, mtGetAttrAll,
	mtLookup, mtLink, mtUnlink, mtReaddir, mtCount, mtStat = 0xf53 };
enum { atMode = 0, atUid, atGid, atSize, atBlocks, atIOBlock, atType, atPort,
	atPollStatus, atEventMask, atCTime, atMTime, atATime, atLinks, atDev };
enum { otDir = 0, otFile, otDev, otSymlink, otUnknown };

#pragma pack(push, 8)
struct _attr { long long val; int err; };
struct _attrAll {
	struct _attr mode, uid, gid, size, blocks, ioblock, type, port,
		pollStatus, eventMask, cTime, mTime, aTime, links, dev;
};
typedef struct _msg_t {
	int type; int pid; int priority; oid_t oid;
	struct {
		union {
			struct { unsigned int flags; } openclose;
			struct { off_t offs; size_t len; unsigned int mode; } io;
			struct { int type; unsigned int mode; oid_t dev; } create;
			struct { long long val; int type; } attr;
			struct { oid_t oid; } ln;
			struct { off_t offs; } readdir;
			unsigned char raw[64];
		};
		size_t size; const void *data;
	} i;
	struct {
		union {
			struct { long long val; } attr;
			struct { oid_t oid; } create;
			struct { oid_t fil; oid_t dev; } lookup;
			unsigned char raw[64];
		};
		int err; size_t size; void *data;
	} o;
} msg_t;
#pragma pack(pop)

/* Phoenix struct dirent (libphoenix include/dirent.h:42) */
struct phx_dirent { uint64_t d_ino; uint32_t d_type; uint16_t d_reclen; uint16_t d_namlen; char d_name[]; };

#endif
