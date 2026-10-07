/* Phoenix <sys/msg.h> for the host: the kernel's own msg_t, the IPC calls as shim.c models them. */
#ifndef HOSTTEST_SYS_MSG_H
#define HOSTTEST_SYS_MSG_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct _oid_t {
	uint32_t port;
	id_t id;
} oid_t;

#include <phoenix/msg.h>
#include <phoenix/file.h>

int portCreate(uint32_t *port);
int lookup(const char *name, oid_t *file, oid_t *dev);
int msgSend(uint32_t port, msg_t *m);
int msgRecv(uint32_t port, msg_t *m, msg_rid_t *rid);
int msgRespond(uint32_t port, msg_t *m, msg_rid_t rid);
int pollNotify(const oid_t *oid);

#endif
