/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * labwc-drm host test: compat epoll_pwait() (lwphx_epoll_pwait.c) over the M6
 * epoll emulation (weston-drm/compat/src/wlphx_epoll.c), in foot's pattern: the
 * signal is blocked at all times except inside epoll_pwait(), whose mask
 * unblocks it; a signal sent while the thread waits must run the handler and end
 * the wait, and the caller's mask must be restored afterwards.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "test.h"

static volatile sig_atomic_t got;

static void handler(int sig)
{
	got = sig;
}

static void *sender(void *arg)
{
	(void)arg;
	usleep(50000);
	kill(getpid(), SIGUSR1);
	return NULL;
}

int main(void)
{
	struct epoll_event ev, out[4];
	sigset_t block, waitmask, now;
	struct sigaction sa;
	pthread_t th;
	int ep, sv[2], r, e;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sigaction(SIGUSR1, &sa, NULL);
	sigemptyset(&block);
	sigaddset(&block, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &block, &waitmask); /* waitmask = the mask without SIGUSR1 */

	ep = epoll_create1(0);
	CHECK(ep >= 0, "epoll_create1");
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = sv[0];
	epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev);

	/* the sender thread must not take the signal itself: it inherits the blocked mask */
	pthread_create(&th, NULL, sender, NULL);
	r = epoll_pwait(ep, out, 4, 2000, &waitmask);
	e = errno;
	CHECK((r < 0) && (e == EINTR), "a signal admitted by the wait mask ends epoll_pwait with EINTR (r=%d errno=%d)", r, e);
	CHECK(got == SIGUSR1, "the handler ran during the wait");
	pthread_sigmask(SIG_BLOCK, NULL, &now);
	CHECK(sigismember(&now, SIGUSR1) == 1, "the caller's mask (SIGUSR1 blocked) is restored");
	pthread_join(th, NULL);

	got = 0;
	write(sv[1], "x", 1);
	r = epoll_pwait(ep, out, 4, 1000, &waitmask);
	CHECK((r == 1) && (out[0].data.fd == sv[0]), "readiness still reported through epoll_pwait");
	r = epoll_pwait(ep, out, 4, 0, NULL);
	CHECK(r == 1, "sigmask NULL = plain epoll_wait");
	CHECK(got == 0, "no stray signal");
	return RESULT("epoll_pwait");
}
