/*
 * Phoenix-RTOS RPi4 — cross-open fcntl record-lock proof (Phase 0)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * fs/test_fcntl already proves the kernel lock table, but every one of its
 * subtests shares the file descriptor across fork()/dup() — i.e. the SAME
 * open_file_t/oid by construction. Multi-process SQLite needs something that
 * test never exercised: two INDEPENDENT open() calls of the same path must
 * resolve to the same kernel oid, so a lock taken through one open() is seen
 * through the other. Over an NFS root that hinges on nfs-fs lookup stability.
 *
 * Parent opens the file and takes a whole-file write lock, then forks. The
 * child does its OWN open() of the same path (not the inherited fd) and checks
 * that F_GETLK reports the parent's lock+pid and that F_SETLK is refused.
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
	const char *path = (argc > 1) ? argv[1] : "/tmp/xopen_lockfile";
	int pfd, wstatus;
	pid_t ppid;
	struct flock fl;

	printf("PHASE0: cross-open fcntl proof on '%s'\n", path);

	pfd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (pfd < 0) {
		printf("PHASE0: FATAL open(parent) failed errno=%d\n", errno);
		return 2;
	}
	(void)write(pfd, "0123456789", 10);

	/* parent takes a whole-file write lock */
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0; /* to EOF */
	if (fcntl(pfd, F_SETLK, &fl) != 0) {
		printf("PHASE0: FATAL parent F_SETLK(WRLCK) failed errno=%d\n", errno);
		return 2;
	}
	ppid = getpid();
	printf("PHASE0: parent pid=%d holds WRLCK [0,EOF)\n", (int)ppid);

	switch (fork()) {
		case -1:
			printf("PHASE0: FATAL fork failed\n");
			return 2;

		case 0: {
			int cfd, failed = 0, rc;
			struct flock q;

			/* the child's OWN independent open() of the same path */
			cfd = open(path, O_RDWR);
			if (cfd < 0) {
				printf("PHASE0: FATAL child open() failed errno=%d\n", errno);
				_exit(3);
			}

			/* 1) F_GETLK must see the parent's lock and report its pid */
			memset(&q, 0, sizeof(q));
			q.l_type = F_WRLCK;
			q.l_whence = SEEK_SET;
			q.l_start = 0;
			q.l_len = 4;
			if (fcntl(cfd, F_GETLK, &q) != 0) {
				printf("PHASE0: [1] F_GETLK call failed errno=%d\n", errno);
				failed++;
			}
			else if (q.l_type == F_UNLCK) {
				printf("PHASE0: [1] FAIL F_GETLK saw no lock via an independent open (oid mismatch?)\n");
				failed++;
			}
			else if (q.l_pid != (int)getppid()) {
				printf("PHASE0: [1] FAIL F_GETLK l_pid=%d, expected parent %d\n", q.l_pid, (int)getppid());
				failed++;
			}
			else {
				printf("PHASE0: [1] OK F_GETLK reports WRLCK owned by pid %d\n", q.l_pid);
			}

			/* 2) F_SETLK for an overlapping write lock must be refused */
			memset(&q, 0, sizeof(q));
			q.l_type = F_WRLCK;
			q.l_whence = SEEK_SET;
			q.l_start = 0;
			q.l_len = 4;
			rc = fcntl(cfd, F_SETLK, &q);
			if (rc == 0) {
				printf("PHASE0: [2] FAIL child acquired a conflicting WRLCK (no mutual exclusion)\n");
				failed++;
			}
			else if (errno != EAGAIN && errno != EACCES) {
				printf("PHASE0: [2] FAIL F_SETLK errno=%d (expected EAGAIN/EACCES)\n", errno);
				failed++;
			}
			else {
				printf("PHASE0: [2] OK conflicting F_SETLK refused (errno=%d)\n", errno);
			}

			_exit(failed == 0 ? 0 : 1);
		}

		default:
			(void)wait(&wstatus);
			/* release + close only after the child has observed the lock */
			fl.l_type = F_UNLCK;
			(void)fcntl(pfd, F_SETLK, &fl);
			(void)close(pfd);

			if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
				printf("PHASE0: PASS\n");
				return 0;
			}
			printf("PHASE0: FAIL (child exit %d)\n", WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
			return 1;
	}
}
