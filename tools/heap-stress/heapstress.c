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
#define CMSG_SPACE_ONE 64u

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
	int only_forkiso = (strcmp(mode, "forkiso") == 0) ? 1 : 0;
	int only_fdpass = (strcmp(mode, "fdpass") == 0) ? 1 : 0;
	int only_zerocheck = (strcmp(mode, "zerocheck") == 0) ? 1 : 0;
	int only_leakcheck = (strcmp(mode, "leakcheck") == 0) ? 1 : 0;
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

	/* mode=forkiso: does a forked child's writes stay out of the parent?
	 *
	 * Chasing an intermittent crash in test-libc-unix-socket where the PARENT
	 * faults in __fflush_unlocked on a FILE full of 0xba, immediately after the
	 * fork tests. If fork's copy is not isolating pages, a child's writes land
	 * in the parent's memory and corrupt arbitrary parent state -- which would
	 * explain that and several other signatures at once.
	 *
	 * The parent fills a heap buffer with 0xAA, forks, the child overwrites it
	 * with 0xBB and _exit()s (no stdio flush), and the parent then checks its
	 * own copy is untouched. Also checks a stack buffer and a .data object.
	 */
	/* mode=fdpass: does SCM_RIGHTS receiving scribble outside its control buffer?
	 *
	 * The kernel's fdpass_unpack() writes the new descriptor numbers into the
	 * receiver's control buffer. One captured signature of the intermittent
	 * corruption is an EL1 fault in hal_memcpy on a USER address, and another is
	 * the parent faulting on poisoned stdio state -- both consistent with the
	 * kernel storing a few bytes at a user address it should not.
	 *
	 * So: paint a large canary region, pass descriptors over AF_UNIX many times,
	 * then scan the canary. A stray descriptor number shows up as a small
	 * integer written over the pattern, and the offset says where.
	 */
	/* mode=zerocheck: is fresh anonymous memory really zero?
	 *
	 * A crash in the exiting test process showed a 64-bit pointer whose UPPER
	 * half was 0xbabababa -- and 0xba is the KERNEL's fresh thread-stack fill
	 * (hal_memset(t->kstack, 0xba, ...)). A kernel pattern inside user data
	 * means a page that had been a kernel stack was handed to userspace without
	 * being cleared, which would silently corrupt anything living in
	 * zero-expected memory: .bss, calloc, and libphoenix's own global lists
	 * (which is where the stdio and atexit crashes were).
	 *
	 * So: churn processes first (every process and thread allocates a kstack and
	 * fills it with 0xba, then frees it), then map anonymous memory and look for
	 * anything non-zero -- reporting the pattern if found.
	 */
	/* mode=leakcheck: do the socket syscalls copy uninitialised KERNEL STACK
	 * bytes out to userspace?
	 *
	 * 0xba appears exactly once in the whole tree -- the kernel's fresh
	 * thread-stack fill -- yet a user pointer was seen with 0xbabababa in its
	 * upper half, and fresh anonymous memory is provably zeroed. That leaves a
	 * kernel-to-user copy that writes more than it initialised, which would
	 * both leak kernel memory and corrupt whatever the caller had there.
	 *
	 * Paint user buffers with a marker, call the syscalls that fill a
	 * caller-supplied struct, and look for 0xba.
	 */
	if (only_leakcheck != 0) {
		long iter;
		size_t ba = 0, changed = 0;

		for (iter = 0; iter < n; ++iter) {
			int sv[2];
			unsigned char abuf[512];
			socklen_t alen = (socklen_t)sizeof(abuf);
			size_t k;

			if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
				printf("HEAPSTRESS-FAIL socketpair errno=%d\n", errno);
				return 1;
			}

			memset(abuf, 0x11, sizeof(abuf));
			if (getsockname(sv[0], (struct sockaddr *)abuf, &alen) == 0) {
				for (k = 0; k < sizeof(abuf); ++k) {
					if (abuf[k] != 0x11u) {
						changed++;
						if (abuf[k] == 0xbau) {
							ba++;
						}
					}
				}
			}

			memset(abuf, 0x11, sizeof(abuf));
			alen = (socklen_t)sizeof(abuf);
			if (getpeername(sv[0], (struct sockaddr *)abuf, &alen) == 0) {
				for (k = 0; k < sizeof(abuf); ++k) {
					if (abuf[k] != 0x11u) {
						changed++;
						if (abuf[k] == 0xbau) {
							ba++;
						}
					}
				}
			}

			close(sv[0]);
			close(sv[1]);
		}

		printf("HEAPSTRESS: leakcheck %ld iters: %zu bytes written by the kernel, %zu of them 0xba\n",
			n, changed, ba);
		if (ba != 0) {
			printf("HEAPSTRESS-RESULT FAIL leakcheck: kernel stack bytes reached userspace\n");
			return 1;
		}
		printf("HEAPSTRESS-RESULT PASS (leakcheck, no 0xba in caller buffers)\n");
		return 0;
	}

	if (only_zerocheck != 0) {
		size_t len = 2u * 1024u * 1024u;
		long iter;
		size_t bad_total = 0, ba_total = 0;

		/* .bss must be zero at startup. */
		{
			static unsigned char bss[65536];
			size_t k, bad = 0;
			for (k = 0; k < sizeof(bss); ++k) {
				if (bss[k] != 0u) {
					bad++;
				}
			}
			printf("HEAPSTRESS: bss non-zero bytes = %zu\n", bad);
			bad_total += bad;
		}

		for (iter = 0; iter < n; ++iter) {
			unsigned char *p;
			size_t k;
			pid_t pid;

			/* churn: a fork+exit pair allocates and frees kernel stacks */
			pid = fork();
			if (pid == 0) {
				_exit(0);
			}
			if (pid > 0) {
				(void)waitpid(pid, NULL, 0);
			}

			p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (p == MAP_FAILED) {
				printf("HEAPSTRESS-FAIL mmap errno=%d\n", errno);
				return 1;
			}
			for (k = 0; k < len; ++k) {
				if (p[k] != 0u) {
					bad_total++;
					if (p[k] == 0xbau) {
						ba_total++;
					}
					if (bad_total == 1u) {
						printf("HEAPSTRESS: FIRST non-zero at iter=%ld off=%zu val=0x%02x\n",
							iter, k, p[k]);
					}
				}
			}
			(void)munmap(p, len);
		}

		if (bad_total != 0) {
			printf("HEAPSTRESS-RESULT FAIL zerocheck: %zu non-zero bytes in fresh anonymous memory (%zu of them 0xba)\n",
				bad_total, ba_total);
			return 1;
		}
		printf("HEAPSTRESS-RESULT PASS (zerocheck %ld iterations, fresh memory all zero)\n", n);
		return 0;
	}

	if (only_fdpass != 0) {
		size_t len = 4u * 1024u * 1024u;
		unsigned char *canary = malloc(len);
		int sv[2];
		long iter;
		size_t k, bad = 0, firstbad = (size_t)-1;

		if (canary == NULL) {
			printf("HEAPSTRESS-FAIL malloc\n");
			return 1;
		}
		for (k = 0; k < len; ++k) {
			canary[k] = (unsigned char)(k ^ 0x5Au);
		}

		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
			printf("HEAPSTRESS-FAIL socketpair errno=%d\n", errno);
			return 1;
		}

		for (iter = 0; iter < n; ++iter) {
			int passed = open(BASE, O_CREAT | O_RDWR, 0666);
			char cbuf[CMSG_SPACE_ONE];
			char data = 'x';
			struct iovec iov = { .iov_base = &data, .iov_len = 1 };
			struct msghdr msg;
			struct cmsghdr *cm;
			int got = -1;

			if (passed < 0) {
				printf("HEAPSTRESS-FAIL open errno=%d\n", errno);
				return 1;
			}

			memset(&msg, 0, sizeof(msg));
			memset(cbuf, 0, sizeof(cbuf));
			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;
			msg.msg_control = cbuf;
			msg.msg_controllen = sizeof(cbuf);
			cm = CMSG_FIRSTHDR(&msg);
			cm->cmsg_level = SOL_SOCKET;
			cm->cmsg_type = SCM_RIGHTS;
			cm->cmsg_len = CMSG_LEN(sizeof(int));
			memcpy(CMSG_DATA(cm), &passed, sizeof(int));
			msg.msg_controllen = cm->cmsg_len;

			if (sendmsg(sv[0], &msg, 0) < 0) {
				printf("HEAPSTRESS-FAIL sendmsg errno=%d\n", errno);
				return 1;
			}
			close(passed);

			memset(&msg, 0, sizeof(msg));
			memset(cbuf, 0, sizeof(cbuf));
			msg.msg_iov = &iov;
			msg.msg_iovlen = 1;
			msg.msg_control = cbuf;
			msg.msg_controllen = sizeof(cbuf);
			if (recvmsg(sv[1], &msg, 0) < 0) {
				printf("HEAPSTRESS-FAIL recvmsg errno=%d\n", errno);
				return 1;
			}
			cm = CMSG_FIRSTHDR(&msg);
			if ((cm != NULL) && (cm->cmsg_type == SCM_RIGHTS)) {
				memcpy(&got, CMSG_DATA(cm), sizeof(int));
				if (got >= 0) {
					close(got);
				}
			}
		}

		for (k = 0; k < len; ++k) {
			if (canary[k] != (unsigned char)(k ^ 0x5Au)) {
				if (firstbad == (size_t)-1) {
					firstbad = k;
				}
				bad++;
			}
		}
		close(sv[0]);
		close(sv[1]);
		(void)unlink(BASE);

		if (bad != 0) {
			printf("HEAPSTRESS-RESULT FAIL fdpass: %zu canary bytes changed, first at +%zu (val=0x%02x)\n",
				bad, firstbad, canary[firstbad]);
			return 1;
		}
		printf("HEAPSTRESS-RESULT PASS (fdpass %ld iterations, canary intact)\n", n);
		return 0;
	}

	if (only_forkiso != 0) {
		static unsigned char sdata[8192];
		size_t len = 256u * 1024u;
		unsigned char *heap = malloc(len);
		unsigned char stackbuf[4096];
		pid_t pid;
		size_t k, bad_heap = 0, bad_stack = 0, bad_data = 0;
		long iter;

		if (heap == NULL) {
			printf("HEAPSTRESS-FAIL malloc\n");
			return 1;
		}

		for (iter = 0; iter < n; ++iter) {
			memset(heap, 0xAA, len);
			memset(stackbuf, 0xAA, sizeof(stackbuf));
			memset(sdata, 0xAA, sizeof(sdata));

			pid = fork();
			if (pid < 0) {
				printf("HEAPSTRESS-FAIL fork errno=%d\n", errno);
				return 1;
			}
			if (pid == 0) {
				memset(heap, 0xBB, len);
				memset(stackbuf, 0xBB, sizeof(stackbuf));
				memset(sdata, 0xBB, sizeof(sdata));
				_exit(0);
			}
			(void)waitpid(pid, NULL, 0);

			for (k = 0; k < len; ++k) {
				if (heap[k] != 0xAAu) {
					bad_heap++;
				}
			}
			for (k = 0; k < sizeof(stackbuf); ++k) {
				if (stackbuf[k] != 0xAAu) {
					bad_stack++;
				}
			}
			for (k = 0; k < sizeof(sdata); ++k) {
				if (sdata[k] != 0xAAu) {
					bad_data++;
				}
			}
			if ((bad_heap | bad_stack | bad_data) != 0) {
				printf("HEAPSTRESS-RESULT FAIL iter=%ld heap=%zu stack=%zu data=%zu bytes changed by the child\n",
					iter, bad_heap, bad_stack, bad_data);
				return 1;
			}
		}

		printf("HEAPSTRESS-RESULT PASS (forkiso %ld iterations, parent memory untouched)\n", n);
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
