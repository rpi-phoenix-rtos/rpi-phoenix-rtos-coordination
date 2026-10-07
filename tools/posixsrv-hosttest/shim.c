/*
 * The Phoenix kernel as posixsrv sees it, on host threads.
 *
 * Messages: a port is a queue of calls. msgSend() (a client, or posixsrv
 * itself) enqueues a call and blocks until a server thread's msgRespond()
 * answers it; msgRecv() is a posixsrv server thread taking the next call. The
 * message's data pointers point straight into the caller's memory, as the
 * kernel's mapping would make them appear. A send to a port nobody serves
 * answers -EINVAL at once (posixsrv subscribing to, or polling, the made-up
 * objects the event tests watch).
 *
 * Kernel mutexes are error-checking host mutexes, so a relock answers -EDEADLK
 * as on Phoenix. The clock (gettime) can run SCALE times faster than the host's
 * so that a pty's VTIME of 100 ms costs a fraction of that: posixsrv's timeout
 * thread waits in shim_cond_timedwait, which converts the deadline back.
 *
 * Built WITHOUT hostcompat.h: this file uses the host's own pthread_cond_timedwait.
 */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h> /* fakeinc: the host's, plus the ioctl_* helpers */
#include <sys/threads.h>

#include "shim.h"


/* ---- clock ---- */

static int shim_scale = 1;
static time_t shim_quantum = 1; /* fake-clock granularity, us */


void shim_setClock(int scale, int quantum_us)
{
	shim_scale = (scale > 0) ? scale : 1;
	shim_quantum = (quantum_us > 0) ? quantum_us : 1;
}


static time_t host_now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (time_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}


int gettime(time_t *raw, time_t *offs)
{
	time_t t = host_now_us() * shim_scale;

	t -= t % shim_quantum;
	if (raw != NULL) {
		*raw = t;
	}
	if (offs != NULL) {
		*offs = 0;
	}
	return 0;
}


int shim_cond_timedwait(pthread_cond_t *restrict c, pthread_mutex_t *restrict m, const struct timespec *restrict abstime)
{
	/* abstime is in the fake clock (posixsrv derives it from gettime); its condition is CLOCK_MONOTONIC */
	time_t fake = (time_t)abstime->tv_sec * 1000000 + abstime->tv_nsec / 1000;
	time_t real = fake / shim_scale;
	struct timespec ts = { .tv_sec = real / 1000000, .tv_nsec = (real % 1000000) * 1000 };

	return pthread_cond_timedwait(c, m, &ts);
}


/* ---- kernel locks ---- */

#define SHIM_HANDLES 4096

static struct {
	int used, isCond;
	pthread_mutex_t m;
	pthread_cond_t c;
} shim_res[SHIM_HANDLES];
static pthread_mutex_t shim_resLock = PTHREAD_MUTEX_INITIALIZER;


static int shim_newHandle(int isCond)
{
	int h;

	pthread_mutex_lock(&shim_resLock);
	for (h = 1; h < SHIM_HANDLES && shim_res[h].used; ++h) {
	}
	if (h == SHIM_HANDLES) {
		fprintf(stderr, "shim: out of handles\n");
		abort();
	}
	shim_res[h].used = 1;
	shim_res[h].isCond = isCond;
	if (isCond) {
		pthread_condattr_t ca;
		pthread_condattr_init(&ca);
		pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
		pthread_cond_init(&shim_res[h].c, &ca);
		pthread_condattr_destroy(&ca);
	}
	else {
		pthread_mutexattr_t ma;
		pthread_mutexattr_init(&ma);
		pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK);
		pthread_mutex_init(&shim_res[h].m, &ma);
		pthread_mutexattr_destroy(&ma);
	}
	pthread_mutex_unlock(&shim_resLock);
	return h;
}


static void shim_checkHandle(handle_t h, int isCond)
{
	if (h <= 0 || h >= SHIM_HANDLES || !shim_res[h].used || shim_res[h].isCond != isCond) {
		fprintf(stderr, "shim: bad %s handle %d (destroyed or never created)\n", isCond ? "cond" : "mutex", h);
		abort();
	}
}


int mutexCreate(handle_t *h)
{
	*h = shim_newHandle(0);
	return 0;
}


int mutexLock(handle_t h)
{
	shim_checkHandle(h, 0);
	return -pthread_mutex_lock(&shim_res[h].m);
}


int mutexUnlock(handle_t h)
{
	shim_checkHandle(h, 0);
	return -pthread_mutex_unlock(&shim_res[h].m);
}


int condCreate(handle_t *h)
{
	*h = shim_newHandle(1);
	return 0;
}


int condWait(handle_t c, handle_t m, time_t timeout)
{
	shim_checkHandle(c, 1);
	shim_checkHandle(m, 0);
	if (timeout == 0) {
		return -pthread_cond_wait(&shim_res[c].c, &shim_res[m].m);
	}

	time_t t = host_now_us() + timeout / shim_scale;
	struct timespec ts = { .tv_sec = t / 1000000, .tv_nsec = (t % 1000000) * 1000 };
	int err = pthread_cond_timedwait(&shim_res[c].c, &shim_res[m].m, &ts);
	return (err == ETIMEDOUT) ? -ETIME : -err;
}


int condSignal(handle_t c)
{
	shim_checkHandle(c, 1);
	return -pthread_cond_signal(&shim_res[c].c);
}


int condBroadcast(handle_t c)
{
	shim_checkHandle(c, 1);
	return -pthread_cond_broadcast(&shim_res[c].c);
}


int resourceDestroy(handle_t h)
{
	if (h <= 0 || h >= SHIM_HANDLES || !shim_res[h].used) {
		fprintf(stderr, "shim: resourceDestroy(%d) of a free handle\n", h);
		abort();
	}
	if (shim_res[h].isCond) {
		pthread_cond_destroy(&shim_res[h].c);
	}
	else if (pthread_mutex_destroy(&shim_res[h].m) != 0) {
		fprintf(stderr, "shim: resourceDestroy(%d) of a LOCKED mutex\n", h);
		abort();
	}
	pthread_mutex_lock(&shim_resLock);
	shim_res[h].used = 0;
	pthread_mutex_unlock(&shim_resLock);
	return 0;
}


void endthread(void)
{
	pthread_exit(NULL);
}


/* ---- messages ---- */

#define SHIM_PORTS 8
#define SHIM_CALLS (1 << 20)

typedef struct shim_call {
	struct shim_call *next;
	msg_t msg;  /* the server works on its own copy, as with the kernel */
	msg_t *out; /* the caller's message, filled in by msgRespond */
	int rid, done, abandoned;
} shim_call_t;

static struct {
	pthread_mutex_t lock;
	pthread_cond_t recv, resp;
	shim_call_t *head, *tail;
	int served;
} shim_port[SHIM_PORTS];

static shim_call_t *shim_calls[SHIM_CALLS];
static int shim_callNext = 1;
static int shim_portNext = 1;
static __thread int shim_timeoutMs; /* 0: wait forever */
volatile int shim_unanswered;


void shim_setCallTimeout(int ms)
{
	shim_timeoutMs = ms;
}


int portCreate(uint32_t *port)
{
	int p = __atomic_fetch_add(&shim_portNext, 1, __ATOMIC_RELAXED);
	pthread_condattr_t ca;

	if (p >= SHIM_PORTS) {
		abort();
	}
	pthread_mutex_init(&shim_port[p].lock, NULL);
	pthread_condattr_init(&ca);
	pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	pthread_cond_init(&shim_port[p].recv, NULL);
	pthread_cond_init(&shim_port[p].resp, &ca);
	pthread_condattr_destroy(&ca);
	*port = p;
	return 0;
}


int msgSend(uint32_t port, msg_t *m)
{
	shim_call_t *call;
	int rid, err = 0;

	if (port == 0 || port >= SHIM_PORTS || !__atomic_load_n(&shim_port[port].served, __ATOMIC_ACQUIRE)) {
		return -EINVAL;
	}

	call = calloc(1, sizeof(*call));
	call->msg = *m;
	call->out = m;
	call->msg.pid = getpid();

	pthread_mutex_lock(&shim_port[port].lock);
	rid = shim_callNext++;
	if (rid >= SHIM_CALLS) {
		abort();
	}
	shim_calls[rid] = call;
	call->rid = rid;
	if (shim_port[port].tail != NULL) {
		shim_port[port].tail->next = call;
	}
	else {
		shim_port[port].head = call;
	}
	shim_port[port].tail = call;
	pthread_cond_signal(&shim_port[port].recv);

	if (shim_timeoutMs > 0) {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		ts.tv_sec += shim_timeoutMs / 1000;
		ts.tv_nsec += (long)(shim_timeoutMs % 1000) * 1000000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000;
		}
		while (!call->done && err == 0) {
			err = pthread_cond_timedwait(&shim_port[port].resp, &shim_port[port].lock, &ts);
		}
	}
	else {
		while (!call->done) {
			pthread_cond_wait(&shim_port[port].resp, &shim_port[port].lock);
		}
	}

	if (!call->done) {
		/* the call stays in the table: a late msgRespond must find it */
		call->abandoned = 1;
		__atomic_fetch_add(&shim_unanswered, 1, __ATOMIC_RELAXED);
		pthread_mutex_unlock(&shim_port[port].lock);
		return -ETIMEDOUT;
	}
	shim_calls[rid] = NULL;
	pthread_mutex_unlock(&shim_port[port].lock);
	free(call);
	return 0;
}


int msgRecv(uint32_t port, msg_t *m, msg_rid_t *rid)
{
	shim_call_t *call;

	pthread_mutex_lock(&shim_port[port].lock);
	if (!shim_port[port].served) {
		__atomic_store_n(&shim_port[port].served, 1, __ATOMIC_RELEASE);
	}
	while ((call = shim_port[port].head) == NULL) {
		pthread_cond_wait(&shim_port[port].recv, &shim_port[port].lock);
	}
	shim_port[port].head = call->next;
	if (shim_port[port].head == NULL) {
		shim_port[port].tail = NULL;
	}
	*rid = call->rid;
	*m = call->msg;
	pthread_mutex_unlock(&shim_port[port].lock);
	return 0;
}


int msgRespond(uint32_t port, msg_t *m, msg_rid_t rid)
{
	shim_call_t *call;

	pthread_mutex_lock(&shim_port[port].lock);
	call = (rid > 0 && rid < SHIM_CALLS) ? shim_calls[rid] : NULL;
	if (call == NULL || call->done) {
		fprintf(stderr, "shim: msgRespond(rid %d) answers %s\n", rid, (call == NULL) ? "NO call" : "a call TWICE");
		abort();
	}
	if (call->abandoned) {
		__atomic_fetch_sub(&shim_unanswered, 1, __ATOMIC_RELAXED);
		shim_calls[rid] = NULL;
		pthread_mutex_unlock(&shim_port[port].lock);
		return 0; /* leaked: the caller's msg_t is gone */
	}
	/* what the kernel copies back: the output half */
	call->out->o = m->o;
	call->done = 1;
	pthread_cond_broadcast(&shim_port[port].resp);
	pthread_mutex_unlock(&shim_port[port].lock);
	return 0;
}


/* Ports that posixsrv's threads serve, once they are all receiving */
void shim_waitServed(uint32_t port)
{
	while (!__atomic_load_n(&shim_port[port].served, __ATOMIC_ACQUIRE)) {
		usleep(1000);
	}
}


int pollNotify(const oid_t *oid)
{
	(void)oid;
	return 0;
}


/* ---- names ---- */

#define SHIM_NAMES 1024

static struct {
	char path[64];
	oid_t oid;
} shim_names[SHIM_NAMES];
static int shim_nameCount;
static pthread_mutex_t shim_nameLock = PTHREAD_MUTEX_INITIALIZER;


int create_dev(oid_t *oid, const char *path)
{
	pthread_mutex_lock(&shim_nameLock);
	if (shim_nameCount < SHIM_NAMES) {
		snprintf(shim_names[shim_nameCount].path, sizeof(shim_names[0].path), "%s", path);
		shim_names[shim_nameCount++].oid = *oid;
	}
	pthread_mutex_unlock(&shim_nameLock);
	return 0;
}


int lookup(const char *name, oid_t *file, oid_t *dev)
{
	int i, err = -ENOENT;

	pthread_mutex_lock(&shim_nameLock);
	for (i = shim_nameCount - 1; i >= 0; --i) {
		if (strcmp(shim_names[i].path, name) == 0) {
			if (file != NULL) {
				*file = shim_names[i].oid;
			}
			if (dev != NULL) {
				*dev = shim_names[i].oid;
			}
			err = 0;
			break;
		}
	}
	pthread_mutex_unlock(&shim_nameLock);
	return err;
}


/* ---- ioctl packing (this harness's own; see fakeinc/sys/ioctl.h) ---- */

void shim_ioctlPack(msg_t *msg, id_t id, unsigned long request, const void *in, void *out)
{
	shim_ioctl_t io = { .request = request, .id = id, .in = in, .out = out };

	memset(msg, 0, sizeof(*msg));
	msg->type = mtDevCtl;
	memcpy(msg->i.raw, &io, sizeof(io));
}


const void *ioctl_unpackEx(msg_t *msg, unsigned long *request, id_t *id, void **response_buf)
{
	shim_ioctl_t io;

	memcpy(&io, msg->i.raw, sizeof(io));
	if (request != NULL) {
		*request = io.request;
	}
	if (id != NULL) {
		*id = io.id;
	}
	if (response_buf != NULL) {
		*response_buf = io.out;
	}
	return io.in;
}


const void *ioctl_unpack(msg_t *msg, unsigned long *request, id_t *id)
{
	return ioctl_unpackEx(msg, request, id, NULL);
}


pid_t ioctl_getSenderPid(const msg_t *msg)
{
	return msg->pid;
}


void ioctl_setResponse(msg_t *msg, unsigned long request, int err, const void *data)
{
	shim_ioctl_t io;

	memcpy(&io, msg->i.raw, sizeof(io));
	if (data != NULL && io.out != NULL && data != io.out) {
		memcpy(io.out, data, _IOC_SIZE(request));
	}
	msg->o.err = err;
}


/* ---- the host stand-ins named in hostcompat.h ---- */

void shim_syslog(int prio, const char *fmt, ...)
{
	va_list ap;

	(void)prio;
	va_start(ap, fmt);
	fprintf(stderr, "posixsrv syslog: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}


int shim_kill(pid_t pid, int sig)
{
	(void)pid;
	(void)sig;
	return 0;
}


int shim_mkdir(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return 0;
}


int procExists(pid_t pid, pid_t pgid, pid_t sid, unsigned int flags)
{
	(void)pid;
	(void)pgid;
	(void)sid;
	(void)flags;
	return 0;
}


int sessionCtty(pid_t sid, int acquire)
{
	(void)sid;
	(void)acquire;
	return 0;
}


int eventsSend(void *event, int count)
{
	(void)event;
	(void)count;
	return 0;
}


/* posixsrv modules this harness does not build */
int special_init(void)
{
	return 0;
}


int pipe_init(void)
{
	return 0;
}


int tmpfile_init(void)
{
	return 0;
}
