/* Host shim: the Phoenix struct dirent (d_namlen, d_type as an object type). */
#ifndef NFS_HOSTTEST_DIRENT_H
#define NFS_HOSTTEST_DIRENT_H

#include <stdint.h>
#include <sys/types.h>

struct dirent {
	ino_t d_ino;
	uint16_t d_reclen;
	uint16_t d_namlen;
	unsigned char d_type;
	char d_name[];
};

#endif
