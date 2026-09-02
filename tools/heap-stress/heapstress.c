/*
 * Phoenix-RTOS RPi4 — kernel heap stress for the link/unlink/recreate path
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Reproduction driver for the intermittent kernel Data Abort in _vm_zalloc
 * described in docs/misc/2026-09-02-kernel-heap-corruption-workorder.md: a
 * zone free-list link word found overwritten with the ASCII path "/test_st".
 *
 * The crash was only ever seen on the FIRST /bin/test-libc-misc of a boot,
 * during TEST(stat_nlink_size_blk_tim, tim) -- i.e. roughly 2 boots in 5, one
 * trial per boot, which is far too slow a trigger to bisect against. That test
 * reaches it via a very specific shape, which this tool does nothing but repeat:
 *
 *   nlink:  link(path, A); link(path, B); link(path, C);
 *           unlink(A); unlink(B); unlink(C);
 *   tim:    open(B, O_CREAT)   <-- create of a name just unlinked
 *           stat/lstat/fstat; close; remove
 *
 * B is "test_stat" -- exactly the "/test_st" prefix that appeared in the
 * corrupted link. Every iteration hands the kernel the same path string to
 * copy, so if a path buffer is being freed twice (or freed while still owned),
 * a loop of these lands the same write on a free block many times per boot
 * instead of once.
 *
 * Usage: heapstress [iterations]   (default 2000)
 * Prints progress so a crash localises to an iteration, and a final PASS line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#define BASE  "test_stat.txt"                  /* the file everything links to */
#define LNK_A "test_stat_symlink"
#define LNK_B "test_stat"                      /* the "/test_st" in the crash */
#define LNK_C "test_stat_another_link_path"
#define FIFO  "test_stat_fifo"
#define SOCKP "/tmp/test_stat_socket"
#define SYMLOOP 8

/* fifo_type: mkfifo reaches posix_mkfifo -> proc_create/proc_link ->
 * posix_create, the sequence the work order names as the prime suspect. On an
 * NFS root this is also the path that only started completing once nfs-fs
 * learned to splice the owner's oid. */
static int phase_fifo(void)
{
	struct stat st;

	(void)unlink(FIFO);
	if (mkfifo(FIFO, 0777) != 0) {
		printf("HEAPSTRESS-FAIL mkfifo errno=%d\n", errno);
		return -1;
	}
	if (stat(FIFO, &st) != 0) {
		printf("HEAPSTRESS-FAIL stat(fifo) errno=%d\n", errno);
		return -1;
	}
	(void)lstat(FIFO, &st);
	(void)unlink(FIFO);
	return 0;
}


/* sock_type: an AF_UNIX socket bound in the filesystem, stat'ed and removed. */
static int phase_socket(void)
{
	struct sockaddr_un sa;
	struct stat st;
	int s;

	(void)unlink(SOCKP);
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		return 0; /* not the path under test; skip rather than fail the run */
	}
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	(void)snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", SOCKP);
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
		(void)stat(SOCKP, &st);
		(void)lstat(SOCKP, &st);
	}
	close(s);
	(void)unlink(SOCKP);
	return 0;
}


/* symloop_max: the chain of symlinks whose names ("link1", ...) showed up in
 * the crash registers. */
static int phase_symloop(void)
{
	char src[24], tgt[24];
	struct stat st;
	int i;

	for (i = 0; i < SYMLOOP; ++i) {
		(void)snprintf(src, sizeof(src), "link%d", i + 1);
		(void)snprintf(tgt, sizeof(tgt), "link%d", i + 2);
		(void)symlink(tgt, src);
	}
	(void)snprintf(src, sizeof(src), "link1");
	(void)stat(src, &st);
	(void)lstat(src, &st);
	for (i = 0; i < SYMLOOP; ++i) {
		(void)snprintf(src, sizeof(src), "link%d", i + 1);
		(void)unlink(src);
	}
	return 0;
}


/* The double-free repro, in three lines of intent:
 *
 *   open()+close() of a regular file leaves a freed open_file_t block whose
 *   `path` slot still holds the (also just-freed) path pointer, because
 *   posix_fileDeref frees f->path and then f itself. pipe() then vm_kmallocs
 *   an open_file_t WITHOUT initialising `path` (posix_newFile memsets, pipe
 *   does not), so it inherits that stale pointer -- and closing the pipe fd
 *   makes posix_fileDeref free it a SECOND time.
 *
 * The doubly-freed block is a real former block start, so it is in range and
 * block-aligned: both _vm_zfree guards pass and the free list ends up with a
 * duplicate entry. A later path copy then writes into a block that is still on
 * the list, replacing the link word with the first 8 bytes of a path -- the
 * "/test_st" seen in the crash.
 */
static int phase_pipe(void)
{
	int pfd[2];
	int fd;

	/* Free a path-carrying open_file_t so its block (with the stale path
	 * pointer still in it) is at the head of the free list. */
	fd = open(BASE, O_CREAT | O_RDWR, 0666);
	if (fd < 0) {
		printf("HEAPSTRESS-FAIL open(pipe phase) errno=%d\n", errno);
		return -1;
	}
	close(fd);

	/* Recycle it as a pipe end, then close -- the second free of that path. */
	if (pipe(pfd) != 0) {
		printf("HEAPSTRESS-FAIL pipe errno=%d\n", errno);
		return -1;
	}
	close(pfd[0]);
	close(pfd[1]);
	return 0;
}


static int iter_nlink_tim(void)
{
	struct stat st;
	int fd, tfd;

	fd = open(BASE, O_CREAT | O_RDWR, 0666);
	if (fd < 0) {
		printf("HEAPSTRESS-FAIL open(%s) errno=%d\n", BASE, errno);
		return -1;
	}

	/* nlink: three hard links to the same inode, then drop them all. */
	if ((link(BASE, LNK_A) != 0) || (link(BASE, LNK_B) != 0) || (link(BASE, LNK_C) != 0)) {
		printf("HEAPSTRESS-FAIL link errno=%d\n", errno);
		close(fd);
		return -1;
	}
	if (stat(BASE, &st) != 0) {
		printf("HEAPSTRESS-FAIL stat errno=%d\n", errno);
		close(fd);
		return -1;
	}
	(void)lstat(BASE, &st);
	(void)fstat(fd, &st);

	(void)unlink(LNK_A);
	(void)unlink(LNK_B);
	(void)unlink(LNK_C);

	/* tim: recreate the name that was just unlinked, stat it, remove it. */
	tfd = open(LNK_B, O_CREAT, 0666);
	if (tfd < 0) {
		printf("HEAPSTRESS-FAIL open(%s,O_CREAT) errno=%d\n", LNK_B, errno);
		close(fd);
		return -1;
	}
	(void)stat(LNK_B, &st);
	(void)lstat(LNK_B, &st);
	(void)fstat(tfd, &st);
	close(tfd);
	(void)remove(LNK_B);

	close(fd);
	(void)unlink(BASE);
	return 0;
}

int main(int argc, char **argv)
{
	long n = (argc > 1) ? strtol(argv[1], NULL, 10) : 2000;
	const char *mode = (argc > 2) ? argv[2] : "all";
	int only_pipe = (strcmp(mode, "pipe") == 0) ? 1 : 0;
	long i;

	/* Start from a clean slate: leftovers from a crashed run change which
	 * branch each create takes, and the report has to say what was tested. */
	(void)unlink(LNK_A);
	(void)unlink(LNK_B);
	(void)unlink(LNK_C);
	(void)unlink(BASE);
	(void)unlink(FIFO);
	(void)unlink(SOCKP);

	printf("HEAPSTRESS: %ld iterations mode=%s\n", n, mode);
	fflush(stdout);

	for (i = 0; i < n; ++i) {
		if (only_pipe != 0) {
			if (phase_pipe() != 0) {
				printf("HEAPSTRESS-RESULT FAIL at iteration %ld\n", i);
				return 1;
			}
			if (((i + 1) % 100) == 0) {
				printf("HEAPSTRESS: %ld/%ld ok\n", i + 1, n);
				fflush(stdout);
			}
			continue;
		}
		if ((phase_pipe() != 0) || (phase_fifo() != 0) || (phase_socket() != 0) ||
				(phase_symloop() != 0) || (iter_nlink_tim() != 0)) {
			printf("HEAPSTRESS-RESULT FAIL at iteration %ld\n", i);
			return 1;
		}
		if (((i + 1) % 100) == 0) {
			printf("HEAPSTRESS: %ld/%ld ok\n", i + 1, n);
			fflush(stdout);
		}
	}

	printf("HEAPSTRESS-RESULT PASS (%ld iterations, clean)\n", n);
	return 0;
}
