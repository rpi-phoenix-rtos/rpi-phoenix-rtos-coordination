/* Host shim: the slice of Phoenix <sys/msg.h> the nfs server's handlers use
 * (layouts from phoenix-rtos-kernel include/types.h and include/msg.h). */
#ifndef NFS_HOSTTEST_SYS_MSG_H
#define NFS_HOSTTEST_SYS_MSG_H

#include <stdint.h>
#include <sys/types.h>

typedef struct _oid_t {
	uint32_t port;
	id_t id;
} oid_t;

struct _attr {
	long long val;
	int err;
};

struct _attrAll {
	struct _attr mode, uid, gid, size, blocks, ioblock, type, port, pollStatus, eventMask, cTime, mTime, aTime, links, dev;
};

#endif
