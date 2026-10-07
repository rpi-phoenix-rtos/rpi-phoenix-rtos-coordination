/* What main.c uses of shim.c beyond the Phoenix calls it models. */
#ifndef HOSTTEST_SHIM_H
#define HOSTTEST_SHIM_H

#include <sys/msg.h>

typedef struct {
	unsigned long request;
	id_t id;
	const void *in;
	void *out;
} shim_ioctl_t;

/* The fake clock runs scale times faster than the host's, in steps of quantum_us */
void shim_setClock(int scale, int quantum_us);

/* msgSend() from this thread gives up after ms (0: never), answering -ETIMEDOUT */
void shim_setCallTimeout(int ms);

/* Calls msgSend() gave up on that nobody has answered yet */
extern volatile int shim_unanswered;

void shim_waitServed(uint32_t port);

void shim_ioctlPack(msg_t *msg, id_t id, unsigned long request, const void *in, void *out);

#endif
