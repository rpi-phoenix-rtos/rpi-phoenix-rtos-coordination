/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * labwc-drm host test: compat/src/lwphx_shm.c (shm_open/shm_unlink) under
 * wlroots' own util/shm.c (allocate_shm_file / allocate_shm_file_pair, patched
 * by wlroots 0002), with a stand-in for shmsrv: wlphx_shm_create() hands out
 * fresh ids and creates the object as a file <dir>/<id> (run.sh sets
 * LWPHX_SHM_NS to that directory). Checks the POSIX contract wlroots relies on:
 * O_CREAT|O_EXCL, EEXIST, a second (read-only) descriptor of the same object,
 * shm_unlink, descriptors outliving the name, name validation.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"

int shm_open(const char *name, int oflag, mode_t mode);
int shm_unlink(const char *name);
int allocate_shm_file(size_t size);
bool allocate_shm_file_pair(size_t size, int *rw_fd_ptr, int *ro_fd_ptr);

static unsigned next_id = 1;
static int creates;

/* the shmsrv stand-in */
int wlphx_shm_create(unsigned int *id);
int wlphx_shm_create(unsigned int *id)
{
	char path[128];
	int fd;

	*id = next_id++;
	(void)snprintf(path, sizeof(path), LWPHX_SHM_NS "/%u", *id);
	fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		return -1;
	}
	close(fd);
	creates++;
	return 0;
}

/* fchmod() of a shmsrv object fails on Phoenix-RTOS (no modes): FCHMOD_FAILS=1
 * makes this one fail the same way, for every descriptor */
int fchmod(int fd, mode_t mode);
int fchmod(int fd, mode_t mode)
{
	const char *e = getenv("FCHMOD_FAILS");
	char p[64];

	if ((e != NULL) && (e[0] == '1')) {
		errno = ENOSYS;
		return -1;
	}
	(void)snprintf(p, sizeof(p), "/proc/self/fd/%d", fd);
	return chmod(p, mode);
}

/* wlroots' log sink (util/shm.c logs only on the Phoenix path) */
void _wlr_log(int verbosity, const char *format, ...);
void _wlr_log(int verbosity, const char *format, ...)
{
	(void)verbosity;
	(void)format;
}

int main(void)
{
	int a, b, c, rw, ro;
	char buf[16];
	void *m;

	a = shm_open("/lwphx-a", O_RDWR | O_CREAT | O_EXCL, 0600);
	CHECK(a >= 0, "create /lwphx-a (fd=%d)", a);
	CHECK((fcntl(a, F_GETFD) & FD_CLOEXEC) != 0, "descriptor is FD_CLOEXEC");
	errno = 0;
	b = shm_open("/lwphx-a", O_RDWR | O_CREAT | O_EXCL, 0600);
	CHECK((b < 0) && (errno == EEXIST), "O_EXCL on an existing name -> EEXIST");
	b = shm_open("/lwphx-a", O_RDONLY, 0);
	CHECK(b >= 0, "second descriptor, read-only");
	CHECK(write(a, "hello", 5) == 5, "write through the first");
	CHECK((pread(b, buf, 5, 0) == 5) && (memcmp(buf, "hello", 5) == 0), "read the same bytes through the second");
	errno = 0;
	CHECK((write(b, "x", 1) < 0) && (errno == EBADF), "the read-only descriptor refuses writes");
	CHECK(shm_unlink("/lwphx-a") == 0, "unlink");
	errno = 0;
	c = shm_open("/lwphx-a", O_RDWR, 0);
	CHECK((c < 0) && (errno == ENOENT), "open after unlink -> ENOENT");
	CHECK((pread(a, buf, 5, 0) == 5) && (memcmp(buf, "hello", 5) == 0), "the object lives on in its descriptors");
	errno = 0;
	CHECK((shm_unlink("/lwphx-a") < 0) && (errno == ENOENT), "second unlink -> ENOENT");
	c = shm_open("/lwphx-a", O_RDWR | O_CREAT | O_EXCL, 0600);
	CHECK((c >= 0) && (pread(c, buf, 5, 0) == 0), "the name again: a new, empty object (ids are never reused)");
	close(a);
	close(b);
	close(c);
	shm_unlink("/lwphx-a");

	errno = 0;
	CHECK((shm_open("lwphx", O_RDWR | O_CREAT, 0600) < 0) && (errno == EINVAL), "name without a leading slash -> EINVAL");
	errno = 0;
	CHECK((shm_open("/a/b", O_RDWR | O_CREAT, 0600) < 0) && (errno == EINVAL), "name with an inner slash -> EINVAL");
	errno = 0;
	CHECK((shm_open("/", O_RDWR | O_CREAT, 0600) < 0) && (errno == EINVAL), "\"/\" -> EINVAL");
	errno = 0;
	CHECK((shm_open("/x", O_WRONLY | O_CREAT, 0600) < 0) && (errno == EINVAL), "O_WRONLY -> EINVAL (POSIX: RDONLY or RDWR)");
	errno = 0;
	CHECK((shm_open("/missing", O_RDWR, 0) < 0) && (errno == ENOENT), "open without O_CREAT of an unknown name -> ENOENT");

	/* wlroots util/shm.c */
	int before = creates;
	a = allocate_shm_file(4096);
	CHECK(a >= 0, "wlroots allocate_shm_file(4096)");
	m = (a >= 0) ? mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, a, 0) : MAP_FAILED;
	CHECK(m != MAP_FAILED, "mapped read-write");
	if (m != MAP_FAILED) {
		munmap(m, 4096);
	}
	close(a);
	const char *ff = getenv("FCHMOD_FAILS");
	bool pair = allocate_shm_file_pair(65536, &rw, &ro);
#ifndef __phoenix__
	if ((ff != NULL) && (ff[0] == '1')) {
		/* negative control: unpatched wlroots with Phoenix's fchmod() */
		CHECK(!pair, "unpatched util/shm.c + failing fchmod(): allocate_shm_file_pair fails (the Pi's keymap failure)");
		return RESULT("shm-negative");
	}
#endif
	CHECK(pair, "wlroots allocate_shm_file_pair(64 KiB) (the keymap path)%s",
		((ff != NULL) && (ff[0] == '1')) ? " with a failing fchmod()" : "");
	m = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, rw, 0);
	CHECK(m != MAP_FAILED, "rw mapped read-write");
	memcpy(m, "xkb_keymap {", 12);
	void *r = mmap(NULL, 65536, PROT_READ, MAP_PRIVATE, ro, 0);
	CHECK((r != MAP_FAILED) && (memcmp(r, "xkb_keymap {", 12) == 0), "ro (the client's descriptor) sees the keymap, MAP_PRIVATE");
	errno = 0;
	CHECK((mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, ro, 0) == MAP_FAILED) && (errno == EACCES),
		"ro refuses a shared writable mapping");
	CHECK(creates - before == 2, "one shmsrv object per allocation (%d)", creates - before);
	errno = 0;
	CHECK((shm_open("/wlroots-AAAAAA", O_RDONLY, 0) < 0), "wlroots unlinked its names");
	return RESULT("shm");
}
