/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix host harness - force-included into every harness TU.
 *
 * Phoenix-only mmap flags, and redirections of the calls whose Phoenix meaning
 * differs from Linux (device HELLO ioctls, MAP_PHYSMEM mappings, per-descriptor
 * paths, event reads) to the fake servers in fake.c. mmap() goes through
 * libdrm-phoenix's own __wrap_mmap, exactly as the Pi link does (--wrap=mmap).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _PHX_MOCK_H_
#define _PHX_MOCK_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#define MAP_PHYSMEM    0x10000000
#define MAP_UNCACHED   0x20000000
#define MAP_CONTIGUOUS 0x40000000

void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
void *__real_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int mock_munmap(void *addr, size_t length);
int mock_ioctl(int fd, unsigned long req, ...);
int __wrap_ioctl(int fd, unsigned long req, ...);
int __real_ioctl(int fd, unsigned long req, ...);
int mock_open(const char *path, int flags, ...);
int mock_close(int fd);
int mock_dup(int fd);
int __wrap_fcntl(int fd, int cmd, ...);
int __real_fcntl(int fd, int cmd, ...);
int __wrap_dup(int fd);
int __real_dup(int fd);
int __wrap_dup2(int fd, int fd2);
int __real_dup2(int fd, int fd2);
ssize_t mock_read(int fd, void *buf, size_t n);
int mock_poll(struct pollfd *fds, nfds_t n, int timeout);
int sys_fdpath(int fd, char *buf, size_t size);
int mock_fstat(int fd, struct stat *st);
off_t mock_lseek(int fd, off_t off, int whence);

#ifndef MOCK_IMPL
#define mmap   __wrap_mmap
#define munmap mock_munmap
#define ioctl  mock_ioctl
#define open   mock_open
#define close  mock_close
#ifdef MOCK_WRAP_FD
/* the library has the fcntl/dup interposers: route as -Wl,--wrap=fcntl,--wrap=dup,--wrap=dup2 */
#define fcntl  __wrap_fcntl
#define dup    __wrap_dup
#define dup2   __wrap_dup2
#else
#define dup    mock_dup
#endif
#define read   mock_read
#define poll   mock_poll
#define fstat  mock_fstat
#define lseek  mock_lseek
#endif

#endif
