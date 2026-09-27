/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * weston-drm host test: the compat epoll/timerfd/signalfd/eventfd emulation
 * (compat/src/wlphx_epoll.c) built natively against its own headers, which
 * shadow the host's; the program's definitions interpose libc's. Run: run.sh.
 * Checks the behaviour libwayland's event loop relies on.
 */

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

static int fails;

#define CHECK(cond, ...) \
	do { \
		if (cond) { \
			printf("ok   "); \
		} \
		else { \
			printf("FAIL "); \
			fails++; \
		} \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (0)

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int wait_one(int ep, int timeout, struct epoll_event *ev)
{
	return epoll_wait(ep, ev, 1, timeout);
}

int main(void)
{
	struct epoll_event ev, out[8];
	struct itimerspec its;
	struct signalfd_siginfo si;
	int ep, sv[2], tfd, sfd, efd, n;
	long long t0, dt;
	sigset_t mask;
	eventfd_t v;
	char c = 'x';

	ep = epoll_create1(EPOLL_CLOEXEC);
	CHECK(ep >= 0, "epoll_create1 fd=%d", ep);

	/* sockets: EPOLLIN after a peer write, EPOLLOUT after MOD */
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.u32 = 1;
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) == 0, "ctl add socket");
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) < 0 && errno == EEXIST, "ctl add twice -> EEXIST");
	CHECK(wait_one(ep, 0, out) == 0, "idle socket: 0 events at timeout 0");
	(void)write(sv[1], &c, 1);
	n = wait_one(ep, 100, out);
	CHECK(n == 1 && out[0].data.u32 == 1 && (out[0].events & EPOLLIN), "peer write -> EPOLLIN (n=%d ev=0x%x)", n,
		n > 0 ? out[0].events : 0);
	n = wait_one(ep, 0, out);
	CHECK(n == 1, "level-triggered: still reported before read");
	(void)read(sv[0], &c, 1);
	CHECK(wait_one(ep, 0, out) == 0, "after read: nothing");
	ev.events = EPOLLIN | EPOLLOUT;
	CHECK(epoll_ctl(ep, EPOLL_CTL_MOD, sv[0], &ev) == 0, "ctl mod +EPOLLOUT");
	n = wait_one(ep, 0, out);
	CHECK(n == 1 && (out[0].events & EPOLLOUT), "EPOLLOUT on a writable socket");
	CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, sv[0], NULL) == 0, "ctl del");
	ev.events = EPOLLIN | EPOLLET;
	CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &ev) < 0 && errno == EINVAL, "EPOLLET refused (EINVAL)");

	/* timerfd: one-shot relative 50 ms */
	tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	CHECK(tfd >= 0, "timerfd_create fd=%d", tfd);
	ev.events = EPOLLIN;
	ev.data.u32 = 2;
	epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev);
	memset(&its, 0, sizeof(its));
	its.it_value.tv_nsec = 50 * 1000000L;
	timerfd_settime(tfd, 0, &its, NULL);
	CHECK(wait_one(ep, 0, out) == 0, "armed timer not yet due: 0 events at timeout 0 (returns at once)");
	t0 = now_ms();
	n = wait_one(ep, -1, out);
	dt = now_ms() - t0;
	CHECK(n == 1 && out[0].data.u32 == 2 && dt >= 49 && dt < 120, "timer fires via epoll_wait(-1) after %lld ms", dt);
	CHECK(wait_one(ep, 0, out) == 1, "expired one-shot stays readable until re-armed");
	memset(&its, 0, sizeof(its));
	timerfd_settime(tfd, 0, &its, NULL);
	CHECK(wait_one(ep, 0, out) == 0, "disarmed: not reported");
	/* absolute deadline in the past -> immediate */
	clock_gettime(CLOCK_MONOTONIC, &its.it_value);
	its.it_value.tv_sec -= 1;
	timerfd_settime(tfd, TFD_TIMER_ABSTIME, &its, NULL);
	t0 = now_ms();
	n = wait_one(ep, 1000, out);
	CHECK(n == 1 && now_ms() - t0 < 20, "absolute deadline in the past -> immediate");
	/* timeout shorter than the timer: returns 0 on the timeout */
	memset(&its, 0, sizeof(its));
	its.it_value.tv_sec = 5;
	timerfd_settime(tfd, 0, &its, NULL);
	t0 = now_ms();
	n = wait_one(ep, 30, out);
	dt = now_ms() - t0;
	CHECK(n == 0 && dt >= 29 && dt < 80, "timeout 30 ms with a 5 s timer: 0 after %lld ms", dt);
	/* a timer as the only interest (poll with nfds == 0) */
	epoll_ctl(ep, EPOLL_CTL_DEL, sv[0], NULL);
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 20 * 1000000L;
	timerfd_settime(tfd, 0, &its, NULL);
	t0 = now_ms();
	n = wait_one(ep, -1, out);
	dt = now_ms() - t0;
	CHECK(n == 1 && dt >= 19 && dt < 80, "timer-only interest list: fires after %lld ms (poll nfds=0 sleeps)", dt);
	memset(&its, 0, sizeof(its));
	timerfd_settime(tfd, 0, &its, NULL);

	/* signalfd after the caller blocked the signal */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	CHECK(sfd >= 0, "signalfd fd=%d", sfd);
	ev.data.u32 = 3;
	epoll_ctl(ep, EPOLL_CTL_ADD, sfd, &ev);
	raise(SIGUSR1);
	n = wait_one(ep, 100, out);
	CHECK(n == 1 && out[0].data.u32 == 3, "raised SIGUSR1 -> signalfd readable");
	memset(&si, 0, sizeof(si));
	CHECK(read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si) && si.ssi_signo == SIGUSR1, "read gives ssi_signo=%u",
		si.ssi_signo);
	CHECK(sizeof(si) == 128, "signalfd_siginfo is 128 bytes");

	/* eventfd */
	efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	CHECK(efd >= 0, "eventfd fd=%d", efd);
	ev.data.u32 = 4;
	epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);
	CHECK(wait_one(ep, 0, out) == 0, "fresh eventfd: not readable");
	v = 1;
	CHECK(write(efd, &v, sizeof(v)) == (ssize_t)sizeof(v), "write(eventfd) 8 bytes");
	n = wait_one(ep, 100, out);
	CHECK(n == 1 && out[0].data.u32 == 4, "eventfd readable after its own write");
	v = 0;
	CHECK(read(efd, &v, sizeof(v)) == (ssize_t)sizeof(v) && v == 1, "read(eventfd) = %llu", (unsigned long long)v);
	CHECK(wait_one(ep, 0, out) == 0, "eventfd drained");

	/* close drops the record: a recycled number is a plain descriptor again */
	close(tfd);
	CHECK(timerfd_settime(tfd, 0, &its, NULL) < 0 && errno == EBADF, "closed timer: EBADF");
	close(sfd);
	close(efd);
	close(ep);
	CHECK(epoll_wait(ep, out, 1, 0) < 0 && errno == EBADF, "closed epoll: EBADF");

	printf("RESULT fails=%d verdict=%s\n", fails, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
