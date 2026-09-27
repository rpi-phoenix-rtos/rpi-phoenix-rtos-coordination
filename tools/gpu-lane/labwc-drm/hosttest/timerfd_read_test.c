/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * labwc-drm host test: read() of an emulated timerfd (M6 compat wlphx_epoll.c +
 * labwc-drm lwphx_read.c, linked with --wrap=read), in foot's delayed-render
 * pattern: arm a TFD_NONBLOCK one-shot timer, wait for it in epoll, read the
 * uint64 count; render only when it is > 0, then the timer must no longer be
 * readable. m7b-foot (2026-09-27) failed exactly here: every read() was EAGAIN,
 * foot's is_armed flag never cleared and no frame was ever committed.
 * Built twice by run.sh; the NEGATIVE build omits the read() wrapper (= the m7b
 * binaries) and must fail the foot rows.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include "test.h"

static long long ms_since(const struct timespec *a)
{
	struct timespec b;

	clock_gettime(CLOCK_MONOTONIC, &b);
	return (long long)(b.tv_sec - a->tv_sec) * 1000 + (b.tv_nsec - a->tv_nsec) / 1000000;
}

int main(void)
{
	struct epoll_event ev, out[4];
	struct itimerspec its;
	struct timespec t0;
	uint64_t v = 0;
	ssize_t r;
	int ep, fd, n, e;

	ep = epoll_create1(EPOLL_CLOEXEC);
	fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	CHECK((ep >= 0) && (fd >= 0), "epoll + TFD_NONBLOCK timer");
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);

	errno = 0;
	r = read(fd, &v, sizeof(v));
	e = errno;
	CHECK((r < 0) && (e == EAGAIN), "read of a disarmed timer: EAGAIN (r=%zd errno=%d)", r, e);

	/* foot: lower delay 0.5 ms, upper 8.333 ms; here 20 ms for a measurable wait */
	memset(&its, 0, sizeof(its));
	its.it_value.tv_nsec = 20 * 1000000L;
	timerfd_settime(fd, 0, &its, NULL);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	errno = 0;
	r = read(fd, &v, sizeof(v));
	CHECK((r < 0) && (errno == EAGAIN), "read before expiry: EAGAIN");
	n = epoll_wait(ep, out, 4, 1000);
	CHECK((n == 1) && (out[0].data.fd == fd) && (ms_since(&t0) >= 19), "epoll reports the timer after ~20 ms (n=%d, %lld ms)",
		n, ms_since(&t0));
	v = 0;
	r = read(fd, &v, sizeof(v));
	CHECK((r == (ssize_t)sizeof(v)) && (v == 1u), "FOOT ROW: read returns 8 bytes, count 1 (r=%zd v=%llu)", r,
		(unsigned long long)v);
	n = epoll_wait(ep, out, 4, 30);
	CHECK(n == 0, "FOOT ROW: after the read the one-shot timer is no longer readable (n=%d)", n);
	errno = 0;
	r = read(fd, &v, sizeof(v));
	CHECK((r < 0) && (errno == EAGAIN), "FOOT ROW: second read: EAGAIN");

	/* periodic: 10 ms, sleep 55 ms without reading -> 5 expirations in one read */
	its.it_value.tv_nsec = 10 * 1000000L;
	its.it_interval.tv_nsec = 10 * 1000000L;
	timerfd_settime(fd, 0, &its, NULL);
	usleep(55000);
	n = epoll_wait(ep, out, 4, 0);
	CHECK(n == 1, "periodic timer readable after 55 ms");
	n = epoll_wait(ep, out, 4, 0);
	CHECK(n == 1, "level-triggered: still readable when not read");
	v = 0;
	r = read(fd, &v, sizeof(v));
	CHECK((r == 8) && (v >= 5u) && (v <= 6u), "FOOT ROW: one read returns every expiration so far (v=%llu, 5..6)",
		(unsigned long long)v);
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_nsec = 0;
	timerfd_settime(fd, 0, &its, NULL);

	/* settime discards unread expirations */
	its.it_value.tv_nsec = 1000000L;
	timerfd_settime(fd, 0, &its, NULL);
	usleep(5000);
	its.it_value.tv_nsec = 0;
	timerfd_settime(fd, 0, &its, NULL);
	errno = 0;
	CHECK((read(fd, &v, sizeof(v)) < 0) && (errno == EAGAIN), "disarming discards an unread expiration");
	CHECK(epoll_wait(ep, out, 4, 0) == 0, "and the timer is not readable");

	errno = 0;
	CHECK((read(fd, &v, 4) < 0) && (errno == EINVAL || errno == EAGAIN), "a 4-byte buffer: EINVAL (Linux)");

	/* a blocking timer sleeps in read() until it expires */
	int bfd = timerfd_create(CLOCK_MONOTONIC, 0);
	its.it_value.tv_nsec = 30 * 1000000L;
	timerfd_settime(bfd, 0, &its, NULL);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	v = 0;
	r = read(bfd, &v, sizeof(v));
	CHECK((r == 8) && (v == 1u) && (ms_since(&t0) >= 29), "FOOT ROW: blocking read waits ~30 ms, count 1 (%lld ms)",
		ms_since(&t0));

	/* ordinary descriptors are untouched by the wrapper */
	int p[2];
	char c = 0;
	CHECK((pipe(p) == 0) && (write(p[1], "z", 1) == 1) && (read(p[0], &c, 1) == 1) && (c == 'z'),
		"a pipe reads through the wrapper unchanged");
	return RESULT(NEGATIVE_NAME);
}
