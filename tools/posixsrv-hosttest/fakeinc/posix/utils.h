/* Phoenix <posix/utils.h> for the host: create_dev records the name in shim.c. */
#ifndef HOSTTEST_POSIX_UTILS_H
#define HOSTTEST_POSIX_UTILS_H

#include <sys/msg.h>

int create_dev(oid_t *oid, const char *path);

#endif
