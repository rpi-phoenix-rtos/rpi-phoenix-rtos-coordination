/*
 * The host's <sys/ioctl.h> plus Phoenix's ioctl message helpers. The packing is
 * this harness's own (see shim.c): both the client side (main.c) and the server
 * side (pty.c, libtty) go through these functions, so it only has to agree with
 * itself.
 */
#ifndef HOSTTEST_SYS_IOCTL_H
#define HOSTTEST_SYS_IOCTL_H

#include_next <sys/ioctl.h>
#include <sys/msg.h>

/* Phoenix-only tty requests (values: no clash with the host's) */
#define TCDRAIN    _IO('T', 0xf6)
#define TIOCGHALFD _IOR('T', 0xf7, int)
#define TIOCSHALFD _IOW('T', 0xf8, int)

const void *ioctl_unpack(msg_t *msg, unsigned long *request, id_t *id);
const void *ioctl_unpackEx(msg_t *msg, unsigned long *request, id_t *id, void **response_buf);
pid_t ioctl_getSenderPid(const msg_t *msg);
void ioctl_setResponse(msg_t *msg, unsigned long request, int err, const void *data);

#endif
