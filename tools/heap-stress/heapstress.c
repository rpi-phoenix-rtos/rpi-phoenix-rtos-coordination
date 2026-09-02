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
#include <pthread.h>
#include <sys/wait.h>
#include <sys/mman.h>

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


/* mode=race: an fd sweep racing an open.
 *
 * posix_open must publish p->fds[fd].file before the blocking IPCs, because the
 * slot is how the descriptor is reserved. That makes a half-built open_file_t
 * reachable from every other thread of the process, and a thread that walks the
 * fd space closing everything (a close-all sweep, or the exit-time sweep) will
 * find it. Before the construction reference, that close decremented an
 * uninitialised refs and could free the file while open was still writing to it.
 *
 * Nothing here can assert on the race directly -- the pass condition is that
 * the kernel neither faults nor reports a corrupt free list. */
static volatile int race_stop;
static long race_opens, race_closes;

static void *race_opener(void *arg)
{
	(void)arg;
	while (race_stop == 0) {
		int fd = open(BASE, O_CREAT | O_RDWR, 0666);
		if (fd >= 0) {
			race_opens++;
			close(fd);
		}
	}
	return NULL;
}


static void *race_sweeper(void *arg)
{
	(void)arg;
	while (race_stop == 0) {
		int fd;
		/* from 3: leave stdin/stdout/stderr alone so output survives */
		for (fd = 3; fd < 32; ++fd) {
			if (close(fd) == 0) {
				race_closes++;
			}
		}
	}
	return NULL;
}


static int phase_race(long secs)
{
	pthread_t a, b;

	race_stop = 0;
	race_opens = 0;
	race_closes = 0;

	if (pthread_create(&a, NULL, race_opener, NULL) != 0) {
		printf("HEAPSTRESS-FAIL pthread_create(opener)\n");
		return -1;
	}
	if (pthread_create(&b, NULL, race_sweeper, NULL) != 0) {
		race_stop = 1;
		(void)pthread_join(a, NULL);
		printf("HEAPSTRESS-FAIL pthread_create(sweeper)\n");
		return -1;
	}

	sleep((unsigned int)secs);
	race_stop = 1;
	(void)pthread_join(a, NULL);
	(void)pthread_join(b, NULL);

	printf("HEAPSTRESS: race %ld opens, %ld sweep-closes\n", race_opens, race_closes);
	return 0;
}


/* mode=sockrace: an fd sweep racing socket()/socketpair()/accept-style creation.
 *
 * Same window as mode=race, but through posix_newFile instead of posix_open:
 * the fd slot is published before the caller fills the file in, so a thread
 * closing descriptors can clear the slot mid-construction. Before the
 * construction reference, the socket paths wrote through p->fds[fd].file after
 * that point and their error paths tore the file down without refcounting --
 * a second free of the same block. */
static void *sockrace_maker(void *arg)
{
	(void)arg;
	while (race_stop == 0) {
		int sv[2];
		int s = socket(AF_UNIX, SOCK_STREAM, 0);
		if (s >= 0) {
			race_opens++;
			close(s);
		}
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
			race_opens++;
			close(sv[0]);
			close(sv[1]);
		}
	}
	return NULL;
}


static int phase_sockrace(long secs)
{
	pthread_t a, b;

	race_stop = 0;
	race_opens = 0;
	race_closes = 0;

	if (pthread_create(&a, NULL, sockrace_maker, NULL) != 0) {
		printf("HEAPSTRESS-FAIL pthread_create(maker)\n");
		return -1;
	}
	if (pthread_create(&b, NULL, race_sweeper, NULL) != 0) {
		race_stop = 1;
		(void)pthread_join(a, NULL);
		printf("HEAPSTRESS-FAIL pthread_create(sweeper)\n");
		return -1;
	}

	sleep((unsigned int)secs);
	race_stop = 1;
	(void)pthread_join(a, NULL);
	(void)pthread_join(b, NULL);

	printf("HEAPSTRESS: sockrace %ld sockets created, %ld sweep-closes\n",
		race_opens, race_closes);
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
	int only_race = (strcmp(mode, "race") == 0) ? 1 : 0;
	int only_segv = (strcmp(mode, "segv") == 0) ? 1 : 0;
	int only_badptr = (strcmp(mode, "badptr") == 0) ? 1 : 0;
	int only_cow = (strcmp(mode, "cow") == 0) ? 1 : 0;
	int only_sockrace = (strcmp(mode, "sockrace") == 0) ? 1 : 0;
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

	/* mode=segv: deliberately take an UNRESOLVABLE fault, to prove the kernel
	 * still reports one. map_pageFault no longer dumps a kernel-PC fault on a
	 * user map up front (that printed a register dump on passing tests); the
	 * guarantee that has to survive is that a fault vm_mapForce cannot satisfy
	 * is still dumped. Expected: an "Exception #" dump naming this process,
	 * then SIGSEGV. */
	if (only_segv != 0) {
		printf("HEAPSTRESS: taking a deliberate null write (expect an exception dump)\n");
		fflush(stdout);
		*(volatile int *)0 = 1;
		printf("HEAPSTRESS-RESULT FAIL (null write did not fault)\n");
		return 1;
	}

	/* mode=badptr: make the KERNEL fault on a user address it cannot map, by
	 * handing a syscall an unmapped user buffer. This is precisely the case
	 * map_pageFault no longer dumps up front (kernel PC, user map), so it is
	 * the one that has to still be reported when vm_mapForce cannot satisfy it.
	 * Expected: either a clean errno (the syscall validated the pointer) or an
	 * exception dump naming this process -- never silence plus success. */
	if (only_badptr != 0) {
		void *bad = (void *)0x40000000000ULL; /* nothing is mapped up here */
		ssize_t w;
		int fd = open(BASE, O_CREAT | O_RDWR, 0666);

		if (fd < 0) {
			printf("HEAPSTRESS-FAIL open errno=%d\n", errno);
			return 1;
		}
		printf("HEAPSTRESS: write() from an unmapped user buffer %p\n", bad);
		fflush(stdout);
		w = write(fd, bad, 64);
		printf("HEAPSTRESS: write returned %zd errno=%d (no dump above = SILENCED, a bug)\n", w, errno);
		close(fd);
		(void)unlink(BASE);
		return 0;
	}

	/* mode=cow: generate RESOLVABLE page faults on demand. Page faults are
	 * otherwise almost nonexistent here (the system maps eagerly -- a whole
	 * three-suite test run resolved exactly one), which makes the kernel's
	 * fault paths hard to exercise deliberately. Touch a buffer, fork, then
	 * have the child write every page: each write hits a copy-on-write page
	 * and must be resolved by the kernel. */
	if (only_cow != 0) {
		size_t len = 4u * 1024u * 1024u;
		unsigned char *buf = mmap(NULL, len, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		size_t k;
		pid_t pid;

		if (buf == MAP_FAILED) {
			printf("HEAPSTRESS-FAIL mmap errno=%d\n", errno);
			return 1;
		}
		for (k = 0; k < len; k += 4096u) {
			buf[k] = 1u; /* fault them in / dirty them in the parent */
		}

		pid = fork();
		if (pid < 0) {
			printf("HEAPSTRESS-FAIL fork errno=%d\n", errno);
			return 1;
		}
		if (pid == 0) {
			for (k = 0; k < len; k += 4096u) {
				buf[k] = 2u; /* each one is a COW fault in the child */
			}
			_exit(0);
		}
		(void)waitpid(pid, NULL, 0);
		printf("HEAPSTRESS-RESULT PASS (cow: %zu pages written in a forked child)\n",
			len / 4096u);
		return 0;
	}

	if (only_sockrace != 0) {
		if (phase_sockrace(n) != 0) {
			return 1;
		}
		printf("HEAPSTRESS-RESULT PASS (%ld s sockrace, clean)\n", n);
		return 0;
	}

	if (only_race != 0) {
		/* n is seconds of racing here, not iterations. */
		if (phase_race(n) != 0) {
			return 1;
		}
		(void)unlink(BASE);
		printf("HEAPSTRESS-RESULT PASS (%ld s race, clean)\n", n);
		return 0;
	}

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
