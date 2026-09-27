/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * weston-drm compat: epoll / timerfd / signalfd emulated over poll() for
 * libwayland's event loop (see compat/include/sys/epoll.h for the contract).
 *
 * Every emulated descriptor is one end of an AF_UNIX socketpair (a real,
 * closable, pollable descriptor) plus a record in a per-process table indexed by
 * the descriptor number:
 *   epoll    the interest list; the socket itself never becomes readable
 *   timer    a deadline; reported EPOLLIN by epoll_wait while expired
 *   signal   the peer end receives one signalfd_siginfo per caught signal, so
 *            the descriptor is readable exactly when a signal is pending
 *   event    eventfd: write() on it (__wrap_write) goes to the peer end, so the
 *            descriptor itself turns readable; read() is the plain socket read
 * epoll_wait() builds a pollfd array from the interest list (timers are not
 * polled, they shorten the timeout), polls, and reports level-triggered events.
 *
 * On Phoenix poll() wakes at once for AF_UNIX sockets and for servers that opt
 * into the kernel's pollNotify (rpi4-kms does); other server-backed descriptors
 * are re-queried on the kernel's poll cycle (M3 G12).
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#define WLPHX_MAX_FDS 1024

enum wlphx_kind { WLPHX_NONE = 0, WLPHX_EPOLL, WLPHX_TIMER, WLPHX_SIGNAL, WLPHX_EVENT };

struct wlphx_item {
	int fd;
	uint32_t events;
	epoll_data_t data;
};

struct wlphx_epoll {
	struct wlphx_item *items;
	int n;
	int cap;
};

struct wlphx_timer {
	clockid_t clock;
	int armed;
	struct timespec deadline; /* absolute, on `clock` */
	struct timespec interval;
};

struct wlphx_signal {
	sigset_t mask;
	struct sigaction old[NSIG];
};

struct wlphx_slot {
	int kind;
	int peer; /* the other end of the socketpair */
	union {
		struct wlphx_epoll ep;
		struct wlphx_timer tm;
		struct wlphx_signal sg;
	} u;
};

static struct wlphx_slot *slots[WLPHX_MAX_FDS];
static pthread_mutex_t wlphx_lock = PTHREAD_MUTEX_INITIALIZER;

/* signal number -> write end of the signalfd that catches it (read by the handler) */
static volatile int sig_wfd[NSIG];

int __real_close(int fd);
int __wrap_close(int fd);
ssize_t __real_write(int fd, const void *buf, size_t n);
ssize_t __wrap_write(int fd, const void *buf, size_t n);


static int slot_new(int kind, int flags_cloexec, int *out_fd)
{
	int sv[2];
	struct wlphx_slot *s;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		return -1;
	}
	if ((sv[0] >= WLPHX_MAX_FDS) || (sv[0] < 0)) {
		__real_close(sv[0]);
		__real_close(sv[1]);
		errno = EMFILE;
		return -1;
	}
	s = calloc(1, sizeof(*s));
	if (s == NULL) {
		__real_close(sv[0]);
		__real_close(sv[1]);
		errno = ENOMEM;
		return -1;
	}
	if (flags_cloexec) {
		(void)fcntl(sv[0], F_SETFD, FD_CLOEXEC);
	}
	(void)fcntl(sv[1], F_SETFD, FD_CLOEXEC);
	(void)fcntl(sv[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(sv[1], F_SETFL, O_NONBLOCK);
	s->kind = kind;
	s->peer = sv[1];
	pthread_mutex_lock(&wlphx_lock);
	slots[sv[0]] = s;
	pthread_mutex_unlock(&wlphx_lock);
	*out_fd = sv[0];
	return 0;
}


static struct wlphx_slot *slot_get(int fd, int kind)
{
	if ((fd < 0) || (fd >= WLPHX_MAX_FDS) || (slots[fd] == NULL) || (slots[fd]->kind != kind)) {
		return NULL;
	}
	return slots[fd];
}


/* ------------------------------------------------------------------------- */
/* time helpers                                                              */

static int ts_cmp(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec != b->tv_sec) {
		return (a->tv_sec < b->tv_sec) ? -1 : 1;
	}
	return (a->tv_nsec < b->tv_nsec) ? -1 : (a->tv_nsec > b->tv_nsec) ? 1 : 0;
}


static void ts_add(struct timespec *r, const struct timespec *a, const struct timespec *b)
{
	r->tv_sec = a->tv_sec + b->tv_sec;
	r->tv_nsec = a->tv_nsec + b->tv_nsec;
	if (r->tv_nsec >= 1000000000L) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000L;
	}
}


static void ts_sub(struct timespec *r, const struct timespec *a, const struct timespec *b)
{
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += 1000000000L;
	}
}


static int ts_is_zero(const struct timespec *t)
{
	return (t->tv_sec == 0) && (t->tv_nsec == 0);
}


/* ------------------------------------------------------------------------- */
/* epoll                                                                     */

int epoll_create1(int flags)
{
	int fd;

	if ((flags & ~EPOLL_CLOEXEC) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (slot_new(WLPHX_EPOLL, (flags & EPOLL_CLOEXEC) != 0, &fd) < 0) {
		return -1;
	}
	return fd;
}


int epoll_create(int size)
{
	if (size <= 0) {
		errno = EINVAL;
		return -1;
	}
	return epoll_create1(0);
}


int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	struct wlphx_slot *s;
	struct wlphx_epoll *ep;
	int i, ret = 0;

	pthread_mutex_lock(&wlphx_lock);
	s = slot_get(epfd, WLPHX_EPOLL);
	if ((s == NULL) || (fd < 0) || (fd == epfd)) {
		pthread_mutex_unlock(&wlphx_lock);
		errno = (s == NULL) ? EBADF : EINVAL;
		return -1;
	}
	if ((op != EPOLL_CTL_DEL) && ((event == NULL) || ((event->events & (EPOLLET | EPOLLONESHOT | EPOLLEXCLUSIVE)) != 0))) {
		/* level-triggered only: libwayland never asks for anything else */
		pthread_mutex_unlock(&wlphx_lock);
		errno = EINVAL;
		return -1;
	}
	ep = &s->u.ep;
	for (i = 0; i < ep->n; i++) {
		if (ep->items[i].fd == fd) {
			break;
		}
	}
	switch (op) {
		case EPOLL_CTL_ADD:
			if (i < ep->n) {
				errno = EEXIST;
				ret = -1;
				break;
			}
			if (ep->n == ep->cap) {
				int ncap = (ep->cap != 0) ? (2 * ep->cap) : 16;
				struct wlphx_item *n = realloc(ep->items, (size_t)ncap * sizeof(*n));
				if (n == NULL) {
					errno = ENOMEM;
					ret = -1;
					break;
				}
				ep->items = n;
				ep->cap = ncap;
			}
			ep->items[ep->n].fd = fd;
			ep->items[ep->n].events = event->events;
			ep->items[ep->n].data = event->data;
			ep->n++;
			break;
		case EPOLL_CTL_MOD:
			if (i == ep->n) {
				errno = ENOENT;
				ret = -1;
				break;
			}
			ep->items[i].events = event->events;
			ep->items[i].data = event->data;
			break;
		case EPOLL_CTL_DEL:
			if (i == ep->n) {
				errno = ENOENT;
				ret = -1;
				break;
			}
			ep->items[i] = ep->items[ep->n - 1];
			ep->n--;
			break;
		default:
			errno = EINVAL;
			ret = -1;
			break;
	}
	pthread_mutex_unlock(&wlphx_lock);
	return ret;
}


static short to_poll(uint32_t ev)
{
	short p = 0;

	if ((ev & (EPOLLIN | EPOLLRDNORM)) != 0) {
		p |= POLLIN;
	}
	if ((ev & EPOLLPRI) != 0) {
		p |= POLLPRI;
	}
	if ((ev & (EPOLLOUT | EPOLLWRNORM)) != 0) {
		p |= POLLOUT;
	}
	return p;
}


static uint32_t from_poll(short rev, uint32_t want)
{
	uint32_t e = 0;

	if ((rev & POLLIN) != 0) {
		e |= want & (EPOLLIN | EPOLLRDNORM);
	}
	if ((rev & POLLPRI) != 0) {
		e |= want & EPOLLPRI;
	}
	if ((rev & POLLOUT) != 0) {
		e |= want & (EPOLLOUT | EPOLLWRNORM);
	}
	if ((rev & POLLHUP) != 0) {
		e |= EPOLLHUP;
	}
	if ((rev & (POLLERR | POLLNVAL)) != 0) {
		e |= EPOLLERR;
	}
	return e;
}


/* Expired-timer test and deadline advance for periodic timers (under the lock). */
static int timer_expired(struct wlphx_timer *t)
{
	struct timespec now;

	if (!t->armed) {
		return 0;
	}
	clock_gettime(t->clock, &now);
	return ts_cmp(&now, &t->deadline) >= 0;
}


/* Milliseconds until the timer fires (>= 0), or -1 when disarmed. */
static int timer_ms_left(struct wlphx_timer *t)
{
	struct timespec now, d;
	long long ms;

	if (!t->armed) {
		return -1;
	}
	clock_gettime(t->clock, &now);
	if (ts_cmp(&now, &t->deadline) >= 0) {
		return 0;
	}
	ts_sub(&d, &t->deadline, &now);
	ms = (long long)d.tv_sec * 1000 + (d.tv_nsec + 999999L) / 1000000L; /* round up: never early */
	return (ms > INT_MAX) ? INT_MAX : (int)ms;
}


int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
	struct wlphx_slot *s;
	struct wlphx_item *items = NULL;
	struct pollfd *pfd = NULL;
	int *pidx = NULL;
	int n, i, np, count, wait_ms, left, r;
	struct timespec start, now, el;

	if ((events == NULL) || (maxevents <= 0)) {
		errno = EINVAL;
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC, &start);

	for (;;) {
		/* Snapshot the interest list: epoll_ctl may run on another thread while we poll. */
		pthread_mutex_lock(&wlphx_lock);
		s = slot_get(epfd, WLPHX_EPOLL);
		if (s == NULL) {
			pthread_mutex_unlock(&wlphx_lock);
			free(items);
			free(pfd);
			free(pidx);
			errno = EBADF;
			return -1;
		}
		n = s->u.ep.n;
		free(items);
		free(pfd);
		free(pidx);
		items = malloc(((size_t)n + 1) * sizeof(*items));
		pfd = malloc(((size_t)n + 1) * sizeof(*pfd));
		pidx = malloc(((size_t)n + 1) * sizeof(*pidx));
		if ((items == NULL) || (pfd == NULL) || (pidx == NULL)) {
			pthread_mutex_unlock(&wlphx_lock);
			free(items);
			free(pfd);
			free(pidx);
			errno = ENOMEM;
			return -1;
		}
		memcpy(items, s->u.ep.items, (size_t)n * sizeof(*items));

		/* Timers are not polled: they bound the wait. The epoll descriptor itself (a
		 * socket that never turns readable) is always in the set, so a list of only
		 * timers still sleeps in poll(): Phoenix's poll() with no descriptors returns
		 * at once for an infinite timeout. */
		wait_ms = timeout;
		count = 0;
		pfd[0].fd = epfd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		pidx[0] = -1;
		np = 1;
		for (i = 0; i < n; i++) {
			struct wlphx_slot *t = slot_get(items[i].fd, WLPHX_TIMER);
			if (t != NULL) {
				left = timer_ms_left(&t->u.tm);
				if ((left >= 0) && ((wait_ms < 0) || (left < wait_ms))) {
					wait_ms = left;
				}
				continue;
			}
			pfd[np].fd = items[i].fd;
			pfd[np].events = to_poll(items[i].events);
			pfd[np].revents = 0;
			pidx[np] = i;
			np++;
		}
		pthread_mutex_unlock(&wlphx_lock);

		r = poll(pfd, (nfds_t)np, wait_ms);
		if (r < 0) {
			free(items);
			free(pfd);
			free(pidx);
			return -1; /* EINTR included: libwayland retries */
		}

		for (i = 1; (i < np) && (count < maxevents); i++) {
			uint32_t e = from_poll(pfd[i].revents, items[pidx[i]].events);
			if (e != 0) {
				events[count].events = e;
				events[count].data = items[pidx[i]].data;
				count++;
			}
		}
		pthread_mutex_lock(&wlphx_lock);
		for (i = 0; (i < n) && (count < maxevents); i++) {
			struct wlphx_slot *t = slot_get(items[i].fd, WLPHX_TIMER);
			if ((t != NULL) && ((items[i].events & EPOLLIN) != 0) && timer_expired(&t->u.tm)) {
				events[count].events = EPOLLIN;
				events[count].data = items[i].data;
				count++;
				/* Periodic timers: step to the next deadline in the future. A one-shot
				 * timer stays expired (readable) until it is re-armed or disarmed. */
				if (!ts_is_zero(&t->u.tm.interval)) {
					clock_gettime(t->u.tm.clock, &now);
					while (ts_cmp(&now, &t->u.tm.deadline) >= 0) {
						ts_add(&t->u.tm.deadline, &t->u.tm.deadline, &t->u.tm.interval);
					}
				}
			}
		}
		pthread_mutex_unlock(&wlphx_lock);

		if ((count > 0) || (timeout == 0)) {
			break;
		}
		if (timeout > 0) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			ts_sub(&el, &now, &start);
			if ((long long)el.tv_sec * 1000 + el.tv_nsec / 1000000L >= timeout) {
				break;
			}
		}
		/* woke for a timer that has not quite expired (poll's ms granularity) or a
		 * spurious wake-up: go round again with the remaining time */
	}
	free(items);
	free(pfd);
	free(pidx);
	return count;
}


/* ------------------------------------------------------------------------- */
/* timerfd                                                                   */

int timerfd_create(int clockid, int flags)
{
	int fd;
	struct wlphx_slot *s;

	if ((clockid != (int)CLOCK_MONOTONIC) && (clockid != (int)CLOCK_REALTIME)) {
		errno = EINVAL;
		return -1;
	}
	if ((flags & ~(TFD_CLOEXEC | TFD_NONBLOCK)) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (slot_new(WLPHX_TIMER, (flags & TFD_CLOEXEC) != 0, &fd) < 0) {
		return -1;
	}
	s = slots[fd];
	s->u.tm.clock = (clockid_t)clockid;
	return fd;
}


int timerfd_settime(int fd, int flags, const struct itimerspec *nv, struct itimerspec *ov)
{
	struct wlphx_slot *s;
	struct timespec now;

	if (nv == NULL) {
		errno = EFAULT;
		return -1;
	}
	pthread_mutex_lock(&wlphx_lock);
	s = slot_get(fd, WLPHX_TIMER);
	if (s == NULL) {
		pthread_mutex_unlock(&wlphx_lock);
		errno = EBADF;
		return -1;
	}
	clock_gettime(s->u.tm.clock, &now);
	if (ov != NULL) {
		ov->it_interval = s->u.tm.interval;
		if (s->u.tm.armed && (ts_cmp(&s->u.tm.deadline, &now) > 0)) {
			ts_sub(&ov->it_value, &s->u.tm.deadline, &now);
		}
		else {
			ov->it_value.tv_sec = 0;
			ov->it_value.tv_nsec = 0;
		}
	}
	s->u.tm.interval = nv->it_interval;
	if (ts_is_zero(&nv->it_value)) {
		s->u.tm.armed = 0;
	}
	else {
		s->u.tm.armed = 1;
		if ((flags & TFD_TIMER_ABSTIME) != 0) {
			s->u.tm.deadline = nv->it_value;
		}
		else {
			ts_add(&s->u.tm.deadline, &now, &nv->it_value);
		}
	}
	pthread_mutex_unlock(&wlphx_lock);
	return 0;
}


int timerfd_gettime(int fd, struct itimerspec *cv)
{
	struct wlphx_slot *s;
	struct timespec now;

	pthread_mutex_lock(&wlphx_lock);
	s = slot_get(fd, WLPHX_TIMER);
	if (s == NULL) {
		pthread_mutex_unlock(&wlphx_lock);
		errno = EBADF;
		return -1;
	}
	memset(cv, 0, sizeof(*cv));
	cv->it_interval = s->u.tm.interval;
	clock_gettime(s->u.tm.clock, &now);
	if (s->u.tm.armed && (ts_cmp(&s->u.tm.deadline, &now) > 0)) {
		ts_sub(&cv->it_value, &s->u.tm.deadline, &now);
	}
	pthread_mutex_unlock(&wlphx_lock);
	return 0;
}


/* ------------------------------------------------------------------------- */
/* signalfd                                                                  */

static void wlphx_sig_handler(int sig)
{
	struct signalfd_siginfo si;
	int saved = errno;
	int wfd;

	if ((sig <= 0) || (sig >= NSIG)) {
		return;
	}
	wfd = sig_wfd[sig];
	if (wfd >= 0) {
		memset(&si, 0, sizeof(si));
		si.ssi_signo = (uint32_t)sig;
		(void)__real_write(wfd, &si, sizeof(si)); /* async-signal-safe; a full socket drops it */
	}
	errno = saved;
}


int signalfd(int fd, const sigset_t *mask, int flags)
{
	struct wlphx_slot *s;
	struct sigaction sa;
	int sig, nfd;
	static int init;

	if ((mask == NULL) || ((flags & ~(SFD_CLOEXEC | SFD_NONBLOCK)) != 0)) {
		errno = EINVAL;
		return -1;
	}
	if (fd != -1) {
		/* changing the mask of an existing signalfd: libwayland never does */
		errno = EINVAL;
		return -1;
	}
	if (!init) {
		for (sig = 0; sig < NSIG; sig++) {
			sig_wfd[sig] = -1;
		}
		init = 1;
	}
	if (slot_new(WLPHX_SIGNAL, (flags & SFD_CLOEXEC) != 0, &nfd) < 0) {
		return -1;
	}
	s = slots[nfd];
	s->u.sg.mask = *mask;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = wlphx_sig_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	for (sig = 1; sig < NSIG; sig++) {
		if (sigismember(mask, sig) == 1) {
			sig_wfd[sig] = s->peer;
			(void)sigaction(sig, &sa, &s->u.sg.old[sig]);
		}
	}
	/* the caller blocked these signals so that they wait for signalfd; with a
	 * handler instead of a kernel queue they have to be deliverable */
	(void)sigprocmask(SIG_UNBLOCK, mask, NULL);
	return nfd;
}


/* ------------------------------------------------------------------------- */
/* eventfd                                                                   */

int eventfd(unsigned int initval, int flags)
{
	int fd;
	eventfd_t v = initval;

	if ((flags & ~(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE)) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (slot_new(WLPHX_EVENT, (flags & EFD_CLOEXEC) != 0, &fd) < 0) {
		return -1;
	}
	if ((flags & EFD_NONBLOCK) == 0) {
		(void)fcntl(fd, F_SETFL, 0);
	}
	if ((initval != 0u) && (__wrap_write(fd, &v, sizeof(v)) != (ssize_t)sizeof(v))) {
		__wrap_close(fd);
		return -1;
	}
	return fd;
}


int eventfd_read(int fd, eventfd_t *value)
{
	return (read(fd, value, sizeof(*value)) == (ssize_t)sizeof(*value)) ? 0 : -1;
}


int eventfd_write(int fd, eventfd_t value)
{
	return (write(fd, &value, sizeof(value)) == (ssize_t)sizeof(value)) ? 0 : -1;
}


ssize_t __wrap_write(int fd, const void *buf, size_t n)
{
	int peer = -1;

	if ((fd >= 0) && (fd < WLPHX_MAX_FDS) && (slots[fd] != NULL)) {
		pthread_mutex_lock(&wlphx_lock);
		if ((slots[fd] != NULL) && (slots[fd]->kind == WLPHX_EVENT)) {
			peer = slots[fd]->peer;
		}
		pthread_mutex_unlock(&wlphx_lock);
	}
	if (peer >= 0) {
		if (n != sizeof(eventfd_t)) {
			errno = EINVAL;
			return -1;
		}
		return __real_write(peer, buf, n);
	}
	return __real_write(fd, buf, n);
}


/* ------------------------------------------------------------------------- */
/* ppoll                                                                     */

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo, const sigset_t *sigmask)
{
	sigset_t old;
	int ms = -1, r, e;

	if (tmo != NULL) {
		long long v = (long long)tmo->tv_sec * 1000 + (tmo->tv_nsec + 999999L) / 1000000L;
		ms = (v > INT_MAX) ? INT_MAX : (int)v;
	}
	if (sigmask != NULL) {
		(void)pthread_sigmask(SIG_SETMASK, sigmask, &old);
	}
	r = poll(fds, nfds, ms);
	e = errno;
	if (sigmask != NULL) {
		(void)pthread_sigmask(SIG_SETMASK, &old, NULL);
	}
	errno = e;
	return r;
}


/* ------------------------------------------------------------------------- */
/* close                                                                     */

int __wrap_close(int fd)
{
	struct wlphx_slot *s = NULL;
	int sig;

	if ((fd >= 0) && (fd < WLPHX_MAX_FDS)) {
		pthread_mutex_lock(&wlphx_lock);
		s = slots[fd];
		slots[fd] = NULL;
		pthread_mutex_unlock(&wlphx_lock);
	}
	if (s != NULL) {
		if (s->kind == WLPHX_SIGNAL) {
			(void)sigprocmask(SIG_BLOCK, &s->u.sg.mask, NULL);
			for (sig = 1; sig < NSIG; sig++) {
				if ((sigismember(&s->u.sg.mask, sig) == 1) && (sig_wfd[sig] == s->peer)) {
					sig_wfd[sig] = -1;
					(void)sigaction(sig, &s->u.sg.old[sig], NULL);
				}
			}
		}
		else if (s->kind == WLPHX_EPOLL) {
			free(s->u.ep.items);
		}
		__real_close(s->peer);
		free(s);
	}
	return __real_close(fd);
}
