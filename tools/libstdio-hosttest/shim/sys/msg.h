/* Host stand-in: only what stdio needs (tmpfile()'s lookup). */
#ifndef SH_SYS_MSG_H
#define SH_SYS_MSG_H
#include <errno.h>
#include <stdint.h>
typedef struct {
	uint32_t port;
	uint64_t id;
} oid_t;
static inline int lookup(const char *name, oid_t *oid, oid_t *dev)
{
	(void)name; (void)oid; (void)dev;
	return -ENOENT;
}
/* for __safe_msgSend() in libphoenix stdio/file-internal.h */
typedef struct {
	int type;
} msg_t;
static inline int msgSend(uint32_t port, msg_t *msg)
{
	(void)port; (void)msg;
	return -ENOSYS;
}
#endif
