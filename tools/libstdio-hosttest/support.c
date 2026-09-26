/*
 * Host implementations of the libphoenix internals stdio/file.c calls
 * (unistd/file-internal.h) plus the mutex table of shim/sys/threads.h.
 * Compiled against HOST headers only.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>

#define SH_MUTEX_MAX 65536
pthread_mutex_t sh_mutexes[SH_MUTEX_MAX];
int sh_mutexCount;


/* file.c passes Phoenix access-mode bits (see shim/fcntl.h); every other
 * flag is already the host's. */
int __safe_open(const char *path, int oflag, mode_t mode)
{
	int acc = oflag & 0x7, hflags = oflag & ~0x7, fd;

	hflags |= (acc == 0x4) ? O_RDWR : ((acc == 0x2) ? O_WRONLY : O_RDONLY);
	do {
		fd = open(path, hflags, mode);
	} while ((fd < 0) && (errno == EINTR));
	return fd;
}


int __safe_close(int fd)
{
	return close(fd);
}


ssize_t __safe_read_nb(int fd, void *buf, size_t size)
{
	ssize_t r;

	do {
		r = read(fd, buf, size);
	} while ((r < 0) && (errno == EINTR));
	return r;
}


ssize_t __safe_write_nb(int fd, const void *buf, size_t size)
{
	ssize_t r;

	do {
		r = write(fd, buf, size);
	} while ((r < 0) && (errno == EINTR));
	return r;
}


ssize_t __safe_write(int fd, const void *buf, size_t size)
{
	return __safe_write_nb(fd, buf, size);
}


ssize_t __safe_read(int fd, void *buf, size_t size)
{
	return __safe_read_nb(fd, buf, size);
}
