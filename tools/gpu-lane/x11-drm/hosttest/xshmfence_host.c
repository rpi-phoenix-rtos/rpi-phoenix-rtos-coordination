/*
 * Phoenix-RTOS
 *
 * Host test of libxshmfence's Phoenix-RTOS backend (x11-drm patch 0001): the
 * polled fence word, exercised by two and more PROCESSES sharing one mapping,
 * the way the X server (trigger/reset/query) and a DRI3 client (await) use it.
 * The backing is a Linux memfd passed over an AF_UNIX socketpair with
 * SCM_RIGHTS (the Phoenix build takes shmsrv's /shm object the same way: an fd
 * that crosses the X socket). Native gcc + ASan/UBSan, seconds.
 *
 * Lines: "XSHMF <check> ... ok=0|1", then "XSHMF RESULT pass=N fail=M".
 *
 * Copyright 2026 Phoenix Systems
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "xshmfenceint.h"

static int npass, nfail;

static void check(const char *name, int ok, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void check(const char *name, int ok, const char *fmt, ...)
{
	va_list ap;

	printf("XSHMF %s ", name);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf(" ok=%d\n", ok);
	fflush(stdout);
	if (ok)
		npass++;
	else
		nfail++;
}

static uint64_t now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* The Phoenix allocator talks to shmsrv; on the host a memfd plays that part. */
int xshmfence_phoenix_alloc_shm(void)
{
	return memfd_create("xshmfence-host", MFD_CLOEXEC);
}

static void send_fd(int sock, int fd)
{
	char c = 'F', cbuf[CMSG_SPACE(sizeof(int))];
	struct iovec iov = { &c, 1 };
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof(cbuf) };
	struct cmsghdr *cm = CMSG_FIRSTHDR(&m);

	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_RIGHTS;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &fd, sizeof(int));
	if (sendmsg(sock, &m, 0) != 1) {
		perror("sendmsg");
		exit(2);
	}
}

static int recv_fd(int sock)
{
	char c, cbuf[CMSG_SPACE(sizeof(int))];
	struct iovec iov = { &c, 1 };
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof(cbuf) };
	struct cmsghdr *cm;
	int fd = -1;

	if (recvmsg(sock, &m, 0) != 1)
		return -1;
	cm = CMSG_FIRSTHDR(&m);
	if (cm == NULL || cm->cmsg_type != SCM_RIGHTS)
		return -1;
	memcpy(&fd, CMSG_DATA(cm), sizeof(int));
	return fd;
}

/* A second process that maps the fence from a received descriptor ("the X server"). */
typedef void (*role_fn)(struct xshmfence *f[], int n, int sock);

static pid_t spawn(role_fn fn, int nfences, int fds[], int *sock_out)
{
	int sv[2];
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		perror("socketpair");
		exit(2);
	}
	pid = fork();
	if (pid == 0) {
		struct xshmfence *f[8];
		int i;

		close(sv[0]);
		for (i = 0; i < nfences; i++) {
			int fd = recv_fd(sv[1]);

			f[i] = xshmfence_map_shm(fd);
			close(fd);
			if (f[i] == NULL)
				_exit(3);
		}
		fn(f, nfences, sv[1]);
		_exit(0);
	}
	close(sv[1]);
	for (int i = 0; i < nfences; i++)
		send_fd(sv[0], fds[i]);
	*sock_out = sv[0];
	return pid;
}

static int reap(pid_t pid)
{
	int st;

	if (waitpid(pid, &st, 0) != pid)
		return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static void sync_byte(int sock)
{
	char c = 'S';

	if (write(sock, &c, 1) != 1)
		_exit(4);
}

static void wait_byte(int sock)
{
	char c;

	if (read(sock, &c, 1) != 1)
		_exit(5);
}

/* server role: trigger after 20 ms */
static void role_delayed_trigger(struct xshmfence *f[], int n, int sock)
{
	(void)n;
	wait_byte(sock);
	usleep(20000);
	xshmfence_trigger(f[0]);
}

/* server role: ping-pong -- await f[0], reset it, trigger f[1]; rounds from the socket */
static void role_pong(struct xshmfence *f[], int n, int sock)
{
	int rounds;

	(void)n;
	if (read(sock, &rounds, sizeof(rounds)) != sizeof(rounds))
		_exit(6);
	for (int i = 0; i < rounds; i++) {
		xshmfence_await(f[0]);
		xshmfence_reset(f[0]);
		xshmfence_trigger(f[1]);
	}
}

/* waiter role: await, then report through the socket */
static void role_waiter(struct xshmfence *f[], int n, int sock)
{
	(void)n;
	sync_byte(sock);   /* mapped and about to wait */
	xshmfence_await(f[0]);
	sync_byte(sock);
}

int main(void)
{
	int fd[2], sock, rc;
	struct xshmfence *f[2];
	pid_t pid;
	uint64_t t0, dt;

	/* 1. a fresh fence is untriggered; the local state machine */
	fd[0] = xshmfence_alloc_shm();
	fd[1] = xshmfence_alloc_shm();
	f[0] = fd[0] >= 0 ? xshmfence_map_shm(fd[0]) : NULL;
	f[1] = fd[1] >= 0 ? xshmfence_map_shm(fd[1]) : NULL;
	if (f[0] == NULL || f[1] == NULL) {
		printf("XSHMF alloc fd0=%d fd1=%d errno=%d ok=0\n", fd[0], fd[1], errno);
		return 1;
	}
	check("fresh", xshmfence_query(f[0]) == 0 && xshmfence_query(f[1]) == 0,
		"q0=%d q1=%d size=%zu", xshmfence_query(f[0]), xshmfence_query(f[1]), sizeof(struct xshmfence));
	xshmfence_trigger(f[0]);
	rc = xshmfence_query(f[0]);
	xshmfence_trigger(f[0]);   /* idempotent */
	t0 = now_us();
	xshmfence_await(f[0]);     /* already triggered: immediate */
	dt = now_us() - t0;
	check("local", rc == 1 && xshmfence_query(f[0]) == 1 && dt < 1000, "q_after_trigger=%d await_us=%llu",
		rc, (unsigned long long)dt);
	xshmfence_reset(f[0]);
	xshmfence_reset(f[0]);     /* idempotent */
	check("reset", xshmfence_query(f[0]) == 0, "q=%d", xshmfence_query(f[0]));

	/* 2. cross-process: the other process triggers 20 ms later, we await */
	pid = spawn(role_delayed_trigger, 1, fd, &sock);
	sync_byte(sock);
	t0 = now_us();
	xshmfence_await(f[0]);
	dt = now_us() - t0;
	rc = reap(pid);
	close(sock);
	/* 20 ms + at most one back-off sleep (1 ms) + scheduling slack */
	check("xproc_trigger", rc == 0 && xshmfence_query(f[0]) == 1 && dt >= 19000 && dt < 40000,
		"child_rc=%d await_us=%llu", rc, (unsigned long long)dt);

	/* 3. cross-process reset is visible here */
	xshmfence_reset(f[0]);
	check("xproc_reset_visible", xshmfence_query(f[0]) == 0, "q=%d", xshmfence_query(f[0]));

	/* 4. ping-pong: 2000 round trips through two fences, both directions */
	{
		int rounds = 2000, i;

		xshmfence_reset(f[0]);
		xshmfence_reset(f[1]);
		pid = spawn(role_pong, 2, fd, &sock);
		if (write(sock, &rounds, sizeof(rounds)) != sizeof(rounds))
			return 2;
		t0 = now_us();
		for (i = 0; i < rounds; i++) {
			xshmfence_trigger(f[0]);
			xshmfence_await(f[1]);
			xshmfence_reset(f[1]);
		}
		dt = now_us() - t0;
		rc = reap(pid);
		close(sock);
		check("pingpong", rc == 0 && i == rounds, "rounds=%d child_rc=%d total_us=%llu per_round_us=%.1f",
			i, rc, (unsigned long long)dt, (double)dt / rounds);
	}

	/* 5. three waiters in three processes, one trigger releases all */
	{
		pid_t w[3];
		int ws[3], k, okw = 1;

		xshmfence_reset(f[0]);
		for (k = 0; k < 3; k++) {
			w[k] = spawn(role_waiter, 1, fd, &ws[k]);
			wait_byte(ws[k]);
		}
		usleep(30000);   /* all three deep in the sleep back-off */
		for (k = 0; k < 3; k++) {
			if (waitpid(w[k], NULL, WNOHANG) != 0)
				okw = 0;   /* nobody may pass an untriggered fence */
		}
		t0 = now_us();
		xshmfence_trigger(f[0]);
		for (k = 0; k < 3; k++) {
			wait_byte(ws[k]);
			rc = reap(w[k]);
			close(ws[k]);
			if (rc != 0)
				okw = 0;
		}
		dt = now_us() - t0;
		check("multi_waiter", okw && dt < 20000, "waiters=3 held_while_untriggered=%d release_us=%llu", okw,
			(unsigned long long)dt);
	}

	/* 6. the mapping outlives the descriptor (the X server closes its fd after mapping) */
	close(fd[0]);
	xshmfence_trigger(f[0]);
	check("after_close", xshmfence_query(f[0]) == 1, "q=%d", xshmfence_query(f[0]));
	xshmfence_unmap_shm(f[0]);
	xshmfence_unmap_shm(f[1]);
	close(fd[1]);

	printf("XSHMF RESULT pass=%d fail=%d\n", npass, nfail);
	return nfail == 0 ? 0 : 1;
}
