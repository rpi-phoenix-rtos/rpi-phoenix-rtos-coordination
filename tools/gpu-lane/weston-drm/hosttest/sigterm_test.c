/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * weston-drm host test: SIGTERM reaches Weston's signal source through the
 * compat signalfd (compat/src/wlphx_epoll.c) when the program does what
 * libwayland 1.24's wl_event_loop_add_signal() does -- signalfd() FIRST, then
 * sigprocmask(SIG_BLOCK) -- and then starts threads that inherit that mask
 * (Mesa's, libinput-phoenix's reader), as Weston does. Run: run.sh.
 *
 * The process-directed signal finds no thread with it unblocked, so it can only
 * be taken while some thread waits in the emulated epoll_wait(). Linux and the
 * Phoenix kernel (proc/threads.c threads_sigpost: first thread whose mask
 * admits the signal, else process-pending until a thread unblocks it) agree on
 * that part; see M6-wayland.md §14 for what this test does not cover.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/signalfd.h>

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


/* wl_event_loop_add_signal(), event-loop.c:731-738, same order */
static int add_signal(int ep, int sig, uint32_t tag)
{
	struct epoll_event ev;
	sigset_t mask;
	int fd;

	sigemptyset(&mask);
	sigaddset(&mask, sig);
	fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.u32 = tag;
	epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
	return fd;
}


/* wl_event_loop_dispatch()'s wait: retry on EINTR/EAGAIN */
static int dispatch_wait(int ep, struct epoll_event *out, int timeout)
{
	int n;

	do {
		n = epoll_wait(ep, out, 4, timeout);
	} while ((n < 0) && ((errno == EINTR) || (errno == EAGAIN)));
	return n;
}


/* a thread started after the sources: it inherits the blocked mask and idles */
static void *idler(void *arg)
{
	(void)arg;
	for (;;) {
		pause();
	}
	return NULL;
}


/* a thread that sends SIGTERM to the process while the main thread sleeps in epoll_wait */
static void *late_kill(void *arg)
{
	(void)arg;
	usleep(100 * 1000);
	kill(getpid(), SIGTERM);
	return NULL;
}


static int blocked_here(int sig)
{
	sigset_t cur;

	pthread_sigmask(SIG_BLOCK, NULL, &cur);
	return sigismember(&cur, sig) == 1;
}


int main(void)
{
	struct epoll_event out[4];
	struct signalfd_siginfo si;
	pthread_t t1, t2;
	int ep, sfd_term, sfd_usr2, n;
	long long t0, dt;

	ep = epoll_create1(EPOLL_CLOEXEC);
	sfd_term = add_signal(ep, SIGTERM, 15);
	sfd_usr2 = add_signal(ep, SIGUSR2, 12);
	CHECK((sfd_term >= 0) && (sfd_usr2 >= 0), "two signal sources, libwayland order (signalfd, then SIG_BLOCK)");
	CHECK(blocked_here(SIGTERM), "SIGTERM blocked outside epoll_wait (as with a real signalfd)");
	pthread_create(&t1, NULL, idler, NULL);

	/* 1: SIGTERM sent while no thread waits (all block it): taken by the next wait */
	kill(getpid(), SIGTERM);
	t0 = now_ms();
	n = dispatch_wait(ep, out, 1000);
	dt = now_ms() - t0;
	CHECK((n == 1) && (out[0].data.u32 == 15), "SIGTERM pending before the wait -> signal source readable (n=%d after %lld ms)",
		n, dt);
	memset(&si, 0, sizeof(si));
	CHECK((read(sfd_term, &si, sizeof(si)) == (ssize_t)sizeof(si)) && (si.ssi_signo == SIGTERM), "read gives ssi_signo=%u",
		si.ssi_signo);
	CHECK(dispatch_wait(ep, out, 0) == 0, "drained: nothing more");

	/* 2: SIGTERM sent by another thread while the main thread sleeps in epoll_wait(-1) */
	pthread_create(&t2, NULL, late_kill, NULL);
	t0 = now_ms();
	n = dispatch_wait(ep, out, 1000);
	dt = now_ms() - t0;
	CHECK((n == 1) && (out[0].data.u32 == 15) && (dt < 1000), "SIGTERM during the wait -> readable after %lld ms (n=%d)", dt,
		n);
	(void)read(sfd_term, &si, sizeof(si));
	pthread_join(t2, NULL);
	CHECK(blocked_here(SIGTERM), "SIGTERM blocked again after epoll_wait returned");

	/* 3: the other source still works and reports only itself */
	kill(getpid(), SIGUSR2);
	n = dispatch_wait(ep, out, 1000);
	CHECK((n == 1) && (out[0].data.u32 == 12), "SIGUSR2 -> its own source (n=%d)", n);
	(void)read(sfd_usr2, &si, sizeof(si));

	close(sfd_usr2);
	close(sfd_term);
	close(ep);
	printf("RESULT fails=%d verdict=%s\n", fails, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
