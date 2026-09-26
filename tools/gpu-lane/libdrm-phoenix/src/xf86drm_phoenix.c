/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix - core of the Phoenix backend
 *
 * - descriptor -> connection table: a DRM descriptor is identified by the path
 *   it was opened under (sys_fdpath, which lives on the kernel's open_file_t and
 *   so also survives dup() and SCM_RIGHTS), then HELLO on the descriptor gives
 *   the per-open client id the server stored as its oid.id. Every later request
 *   is a direct raw msgSend to {server port, client id} (the M1/M2 transport);
 * - the raw-request transport with a page-aligned bounce buffer (E5: an
 *   unaligned payload end costs a shadow page and ~30 us);
 * - drm_phoenix_ioctl(): the one entry drmIoctl() makes on Phoenix;
 * - device identity (a static list of two platform devices);
 * - drmPhoenixMmap(): MMAP_BO / MAP_DUMB tokens -> the buffer's memref;
 * - PRIME descriptor bookkeeping and the in-process sync-file emulation.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/msg.h>

#include "libdrm_macros.h"
#include "xf86drm.h"
#include "xf86drm_phoenix.h"
#include "drm_phoenix_priv.h"


/* libphoenix: the canonical path a descriptor was opened under (no header). */
extern int sys_fdpath(int fd, char *buf, size_t size);

#define DRMPHX_MAX_FD      1024
#define DRMPHX_MAX_SYNCFD  64u
#define DRMPHX_MAX_PRIMEFD 64u

typedef struct {
	drmphx_conn_t *conn;
	char path[DRMPHX_NODE_PATH_MAX];
} drmphx_fdent_t;

static struct {
	pthread_mutex_t lock;          /* descriptor table, connection list, the two fd tables */
	drmphx_fdent_t fd[DRMPHX_MAX_FD];
	drmphx_conn_t *conns;

	struct {
		int used;
		int fd;
		v3da_fence_t fence;
	} sf[DRMPHX_MAX_SYNCFD];       /* sync-file emulation (newest wins) */
	uint32_t sf_next;

	struct {
		int fd;
		int srv;
		uint32_t handle;
		kms_memref_t mem;
		char path[DRMPHX_NODE_PATH_MAX];
	} pf[DRMPHX_MAX_PRIMEFD];      /* PRIME descriptors this process created */
	uint32_t pf_next;
} G = { .lock = PTHREAD_MUTEX_INITIALIZER };


/* ========================================================================= */
/* Small helpers                                                              */
/* ========================================================================= */

int drmphx_fail(int negerr)
{
	errno = (negerr < 0) ? -negerr : EIO;
	return -1;
}


uint64_t drmphx_now_us(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}


int64_t drmphx_now_ns(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}


static size_t page_round(size_t n)
{
	return (n + (size_t)_PAGE_SIZE - 1u) & ~((size_t)_PAGE_SIZE - 1u);
}


static int port_of(const char *path, uint32_t *port)
{
	oid_t dev;

	if (lookup(path, NULL, &dev) < 0) {
		return -ENOENT;
	}
	*port = dev.port;
	return 0;
}


/* The first of two names that resolves (a canonical /dev/dri name or the name
 * the server registers today). NULL if neither does. */
static const char *resolve_node(const char *canonical, const char *legacy)
{
	uint32_t port;

	if (port_of(canonical, &port) == 0) {
		return canonical;
	}
	if (port_of(legacy, &port) == 0) {
		return legacy;
	}
	return NULL;
}


/* ========================================================================= */
/* Connections                                                                */
/* ========================================================================= */

static const struct {
	const char *path;
	int srv;
	int type;
} known_nodes[] = {
	{ DRMPHX_PATH_CARD0, DRMPHX_SRV_KMS, DRM_NODE_PRIMARY },
	{ DRMPHX_PATH_KMS_LEGACY, DRMPHX_SRV_KMS, DRM_NODE_PRIMARY },
	{ DRMPHX_PATH_CARD1, DRMPHX_SRV_V3D, DRM_NODE_PRIMARY },
	{ DRMPHX_PATH_RENDER, DRMPHX_SRV_V3D, DRM_NODE_RENDER },
	{ DRMPHX_PATH_V3D_LEGACY, DRMPHX_SRV_V3D, DRM_NODE_RENDER },
};


static void conn_destroy(drmphx_conn_t *c)
{
	if (c->srv == DRMPHX_SRV_V3D) {
		drmphx_v3d_release(c);
	}
	if (c->scratch != NULL) {
		(void)munmap(c->scratch, c->scratch_size);
	}
	(void)pthread_mutex_destroy(&c->lock);
	(void)pthread_mutex_destroy(&c->xfer);
	free(c);
}


static void conn_unref_locked(drmphx_conn_t *c)
{
	drmphx_conn_t **pp;

	if (--c->refs > 0) {
		return;
	}
	for (pp = &G.conns; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == c) {
			*pp = c->next;
			break;
		}
	}
	c->dead = 1;
	if (c->users == 0) {
		conn_destroy(c);
	}
}


static void drmphx_put(drmphx_conn_t *c)
{
	(void)pthread_mutex_lock(&G.lock);
	c->users--;
	if ((c->dead != 0) && (c->users == 0) && (c->refs == 0)) {
		conn_destroy(c);
	}
	(void)pthread_mutex_unlock(&G.lock);
}


/* A new connection for (fd, path): resolve the server, HELLO. */
static int identify(int fd, const char *path, drmphx_conn_t **out)
{
	drmphx_conn_t *c;
	uint32_t i, port;
	int srv = DRMPHX_SRV_NONE, type = DRM_NODE_PRIMARY, rc;

	for (i = 0u; i < sizeof(known_nodes) / sizeof(known_nodes[0]); i++) {
		if (strcmp(path, known_nodes[i].path) == 0) {
			srv = known_nodes[i].srv;
			type = known_nodes[i].type;
			break;
		}
	}
	if ((srv == DRMPHX_SRV_NONE) && (strncmp(path, "/dev/", 5) != 0)) {
		return -ENOTTY;   /* not a device node: never probe other servers with DRM HELLOs */
	}
	if (port_of(path, &port) != 0) {
		return -ENODEV;
	}

	c = calloc(1, sizeof(*c));
	if (c == NULL) {
		return -ENOMEM;
	}
	(void)pthread_mutex_init(&c->lock, NULL);
	(void)pthread_mutex_init(&c->xfer, NULL);
	c->oid.port = port;
	c->node_type = type;

	if (srv == DRMPHX_SRV_NONE) {
		/* An unknown /dev name (an alias): ask the server itself. Each server
		 * answers the other's HELLO with -ENOTTY. */
		c->srv = DRMPHX_SRV_KMS;
		rc = drmphx_kms_hello(c, fd);
		if (rc == -ENOTTY) {
			c->srv = DRMPHX_SRV_V3D;
			c->node_type = DRM_NODE_RENDER;
			rc = drmphx_v3d_hello(c, fd);
		}
	}
	else {
		c->srv = srv;
		rc = (srv == DRMPHX_SRV_KMS) ? drmphx_kms_hello(c, fd) : drmphx_v3d_hello(c, fd);
	}
	if (rc != 0) {
		conn_destroy(c);
		return (rc == -ENOTTY) ? -ENOTTY : rc;
	}
	*out = c;
	return 0;
}


/* The connection of a DRM descriptor (identified on first use, re-identified
 * when the descriptor number now names something else). Pair with drmphx_put. */
static int drmphx_get(int fd, drmphx_conn_t **out)
{
	char path[DRMPHX_NODE_PATH_MAX];
	drmphx_fdent_t *e;
	drmphx_conn_t *c = NULL, *n, *it;
	int rc;

	if ((fd < 0) || (fd >= DRMPHX_MAX_FD)) {
		return -EBADF;
	}
	rc = sys_fdpath(fd, path, sizeof(path));
	if (rc < 0) {
		return (rc == -EBADF) ? -EBADF : -ENOTTY;
	}

	(void)pthread_mutex_lock(&G.lock);
	e = &G.fd[fd];
	if ((e->conn != NULL) && (strcmp(e->path, path) == 0) &&
			((e->conn->srv != DRMPHX_SRV_V3D) || (drmphx_v3d_stale(e->conn) == 0))) {
		c = e->conn;
		c->users++;
		(void)pthread_mutex_unlock(&G.lock);
		*out = c;
		return 0;
	}
	if (e->conn != NULL) {
		conn_unref_locked(e->conn);
		e->conn = NULL;
	}
	(void)pthread_mutex_unlock(&G.lock);

	rc = identify(fd, path, &n);   /* IPC: outside the global lock */
	if (rc != 0) {
		return rc;
	}

	(void)pthread_mutex_lock(&G.lock);
	/* dup()ed descriptors (and a racing identify of this one) share one client. */
	for (it = G.conns; it != NULL; it = it->next) {
		if ((it->srv == n->srv) && (it->oid.port == n->oid.port) && (it->oid.id == n->oid.id) &&
				((it->srv != DRMPHX_SRV_V3D) || (drmphx_v3d_stale(it) == 0))) {
			c = it;
			break;
		}
	}
	if (c == NULL) {
		c = n;
		n = NULL;
		c->next = G.conns;
		G.conns = c;
	}
	if (e->conn != c) {
		if (e->conn != NULL) {
			conn_unref_locked(e->conn);
		}
		e->conn = c;
		c->refs++;
		(void)snprintf(e->path, sizeof(e->path), "%s", path);
	}
	c->users++;
	(void)pthread_mutex_unlock(&G.lock);
	if (n != NULL) {
		conn_destroy(n);
	}
	*out = c;
	return 0;
}


/* Forget a descriptor's connection (the server no longer knows our client id). */
static void drmphx_forget(int fd)
{
	(void)pthread_mutex_lock(&G.lock);
	if ((fd >= 0) && (fd < DRMPHX_MAX_FD) && (G.fd[fd].conn != NULL)) {
		conn_unref_locked(G.fd[fd].conn);
		G.fd[fd].conn = NULL;
	}
	(void)pthread_mutex_unlock(&G.lock);
}


/* ========================================================================= */
/* Transport                                                                  */
/* ========================================================================= */

int drmphx_call(drmphx_conn_t *c, const void *req, void *resp, const void *idata, size_t isize, size_t iwire,
	void *odata, size_t osize)
{
	msg_t msg;
	size_t ir = page_round(iwire), orr = page_round(osize), need = ir + orr;
	void *nb;
	int err, locked = 0;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = c->oid;
	memcpy(msg.i.raw, req, 64);

	if ((isize != 0u) || (osize != 0u)) {
		(void)pthread_mutex_lock(&c->xfer);
		locked = 1;
		if (c->scratch_size < need) {
			nb = mmap(NULL, need, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
			if (nb == MAP_FAILED) {
				(void)pthread_mutex_unlock(&c->xfer);
				return -ENOMEM;
			}
			if (c->scratch != NULL) {
				(void)munmap(c->scratch, c->scratch_size);
			}
			c->scratch = nb;
			c->scratch_size = need;
		}
		if (isize != 0u) {
			memcpy(c->scratch, idata, isize);
			if (iwire > isize) {
				memset((uint8_t *)c->scratch + isize, 0, iwire - isize);
			}
			msg.i.data = c->scratch;
			msg.i.size = iwire;
		}
		if (osize != 0u) {
			msg.o.data = (uint8_t *)c->scratch + ir;
			msg.o.size = orr;
		}
	}

	err = msgSend(c->oid.port, &msg);
	if ((err == 0) && (msg.o.err >= 0) && (osize != 0u) && (odata != NULL)) {
		memcpy(odata, msg.o.data, osize);
	}
	if (locked != 0) {
		(void)pthread_mutex_unlock(&c->xfer);
	}
	if (err < 0) {
		return err;
	}
	if (msg.o.err < 0) {
		return msg.o.err;
	}
	if (resp != NULL) {
		memcpy(resp, msg.o.raw, 64);
	}
	return ((const int32_t *)(const void *)msg.o.raw)[0];   /* v3da_resp_t.err / kms_resp_t.err */
}


/* ========================================================================= */
/* drmIoctl                                                                   */
/* ========================================================================= */

#define NR(r) DRMPHX_IOC_NR(r)

/* Requests answered the same way on every node (no server state). Returns 1
 * when the request is not one of them. */
static int generic_ioctl(drmphx_conn_t *c, unsigned nr, void *arg)
{
	switch (nr) {
		case NR(DRM_IOCTL_VERSION):
			if (c->srv == DRMPHX_SRV_KMS) {
				drmphx_fill_version(arg, 0, 0, 0, KMS_DRIVER_NAME, "20260926",
					"Phoenix rpi4-kms (BCM2711 firmware planes)");
			}
			else {
				drmphx_fill_version(arg, 1, 0, 0, "v3d", "20260926", "Phoenix rpi4-v3d-async (V3D 4.2)");
			}
			return 0;

		case NR(DRM_IOCTL_GET_UNIQUE): {
			struct drm_unique *u = arg;
			const char *s = (c->srv == DRMPHX_SRV_KMS) ? "gpu" : "fec00000.v3d";
			size_t n = strlen(s);
			if ((u->unique != NULL) && (u->unique_len != 0u)) {
				memcpy(u->unique, s, (n < u->unique_len) ? n : u->unique_len);
			}
			u->unique_len = n;
			return 0;
		}

		case NR(DRM_IOCTL_GET_MAGIC):
			((struct drm_auth *)arg)->magic = (drm_magic_t)c->oid.id;
			return 0;

		case NR(DRM_IOCTL_SET_VERSION):
			return 0;   /* the interface version is fixed */

		default:
			return 1;
	}
}


int drm_phoenix_ioctl(int fd, unsigned long request, void *arg)
{
	unsigned nr = DRMPHX_IOC_NR(request);
	drmphx_conn_t *c;
	int rc, tries;

	if (DRMPHX_IOC_TYPE(request) != DRM_IOCTL_BASE) {
		return drmphx_fail(-ENOTTY);
	}
	for (tries = 0; tries < 2; tries++) {
		rc = drmphx_get(fd, &c);
		if (rc != 0) {
			return drmphx_fail(rc);
		}
		rc = generic_ioctl(c, nr, arg);
		if (rc == 1) {
			rc = (c->srv == DRMPHX_SRV_KMS) ? drmphx_kms_ioctl(c, fd, nr, arg) : drmphx_v3d_ioctl(c, fd, nr, arg);
		}
		drmphx_put(c);
		if (rc != -EBADF) {
			break;
		}
		/* The server does not know our client id (the descriptor was closed and
		 * its number reused for a new open): identify again, once. */
		drmphx_forget(fd);
	}
	return (rc == 0) ? 0 : drmphx_fail(rc);
}


/* ========================================================================= */
/* Device identity                                                            */
/* ========================================================================= */

/* Linux on a Pi 4 exposes the same topology: vc4 (display, card0, no render
 * node) and v3d (card1 + renderD128). v3dv's enumerate_devices() calls
 * try_device(nodes[DRM_NODE_PRIMARY]) on the v3d device unconditionally, so the
 * v3d device carries a primary node too. */
static const char *const compat_vc4[] = { "brcm,bcm2711-vc5", NULL };
static const char *const compat_v3d[] = { "brcm,2711-v3d", NULL };

static drmDevicePtr device_for(int srv)
{
	const char *nodes[DRM_NODE_MAX] = { NULL, NULL, NULL };

	if (srv == DRMPHX_SRV_KMS) {
		nodes[DRM_NODE_PRIMARY] = resolve_node(DRMPHX_PATH_CARD0, DRMPHX_PATH_KMS_LEGACY);
		if (nodes[DRM_NODE_PRIMARY] == NULL) {
			return NULL;
		}
		return drmphx_device_new("/gpu", compat_vc4, nodes);
	}
	nodes[DRM_NODE_RENDER] = resolve_node(DRMPHX_PATH_RENDER, DRMPHX_PATH_V3D_LEGACY);
	if (nodes[DRM_NODE_RENDER] == NULL) {
		return NULL;
	}
	nodes[DRM_NODE_PRIMARY] = resolve_node(DRMPHX_PATH_CARD1, DRMPHX_PATH_V3D_LEGACY);
	return drmphx_device_new("/v3dbus/v3d@7ec04000", compat_v3d, nodes);
}


int drm_phoenix_get_devices2(uint32_t flags, drmDevicePtr devices[], int max_devices)
{
	static const int order[2] = { DRMPHX_SRV_KMS, DRMPHX_SRV_V3D };
	drmDevicePtr d;
	int i, n = 0;

	(void)flags;
	if ((devices != NULL) && (max_devices < 0)) {
		return -EINVAL;
	}
	for (i = 0; i < 2; i++) {
		d = device_for(order[i]);
		if (d == NULL) {
			continue;
		}
		if ((devices != NULL) && (n < max_devices)) {
			devices[n++] = d;
		}
		else {
			drmFreeDevice(&d);
			if (devices == NULL) {
				n++;
			}
		}
	}
	return n;
}


int drm_phoenix_get_device2(int fd, uint32_t flags, drmDevicePtr *device)
{
	drmphx_conn_t *c;
	int rc, srv;

	(void)flags;
	if ((fd < 0) || (device == NULL)) {
		return -EINVAL;
	}
	rc = drmphx_get(fd, &c);
	if (rc != 0) {
		return rc;
	}
	srv = c->srv;
	drmphx_put(c);
	*device = device_for(srv);
	return (*device != NULL) ? 0 : -ENODEV;
}


/* dev_t on Phoenix: stat() reports st_rdev = the device server's port. The v3d
 * primary and render nodes share one server, hence one dev_t (distinct dev_t per
 * node needs one port per node - server work, documented in M3). */
static int srv_of_devid(dev_t devid, int *type)
{
	uint32_t port;
	const char *p;

	p = resolve_node(DRMPHX_PATH_CARD0, DRMPHX_PATH_KMS_LEGACY);
	if ((p != NULL) && (port_of(p, &port) == 0) && ((dev_t)port == devid)) {
		*type = DRM_NODE_PRIMARY;
		return DRMPHX_SRV_KMS;
	}
	p = resolve_node(DRMPHX_PATH_RENDER, DRMPHX_PATH_V3D_LEGACY);
	if ((p != NULL) && (port_of(p, &port) == 0) && ((dev_t)port == devid)) {
		*type = DRM_NODE_RENDER;
		return DRMPHX_SRV_V3D;
	}
	return DRMPHX_SRV_NONE;
}


int drm_phoenix_get_device_from_devid(dev_t find_rdev, uint32_t flags, drmDevicePtr *device)
{
	int type, srv = srv_of_devid(find_rdev, &type);

	(void)flags;
	if (device == NULL) {
		return -EINVAL;
	}
	if (srv == DRMPHX_SRV_NONE) {
		return -ENODEV;
	}
	*device = device_for(srv);
	return (*device != NULL) ? 0 : -ENODEV;
}


int drm_phoenix_node_type_from_devid(dev_t devid)
{
	int type;

	return (srv_of_devid(devid, &type) == DRMPHX_SRV_NONE) ? -ENODEV : type;
}


int drm_phoenix_node_type_from_fd(int fd)
{
	drmphx_conn_t *c;
	int rc, type;

	rc = drmphx_get(fd, &c);
	if (rc != 0) {
		errno = (rc == -ENOTTY) ? EINVAL : -rc;
		return -1;
	}
	type = c->node_type;
	drmphx_put(c);
	return type;
}


char *drm_phoenix_device_name_from_fd(int fd)
{
	char path[DRMPHX_NODE_PATH_MAX];
	drmphx_conn_t *c;

	if (drmphx_get(fd, &c) != 0) {
		return NULL;
	}
	drmphx_put(c);
	if (sys_fdpath(fd, path, sizeof(path)) < 0) {
		return NULL;
	}
	return strdup(path);
}


char *drm_phoenix_minor_name_for_fd(int fd, int type)
{
	const char *p = NULL;
	drmphx_conn_t *c;
	int srv;

	if (drmphx_get(fd, &c) != 0) {
		return NULL;
	}
	srv = c->srv;
	drmphx_put(c);
	if (srv == DRMPHX_SRV_KMS) {
		p = (type == DRM_NODE_PRIMARY) ? resolve_node(DRMPHX_PATH_CARD0, DRMPHX_PATH_KMS_LEGACY) : NULL;
	}
	else if (type == DRM_NODE_RENDER) {
		p = resolve_node(DRMPHX_PATH_RENDER, DRMPHX_PATH_V3D_LEGACY);
	}
	else if (type == DRM_NODE_PRIMARY) {
		p = resolve_node(DRMPHX_PATH_CARD1, DRMPHX_PATH_V3D_LEGACY);
	}
	return (p != NULL) ? strdup(p) : NULL;
}


/* drmOpenMinor(): the legacy drmOpen*() paths (by driver name, by minor). */
int drm_phoenix_open_minor(int minor, int type)
{
	const char *p = NULL;
	int fd;

	if ((type == DRM_NODE_PRIMARY) && (minor == 0)) {
		p = resolve_node(DRMPHX_PATH_CARD0, DRMPHX_PATH_KMS_LEGACY);
	}
	else if ((type == DRM_NODE_PRIMARY) && (minor == 1)) {
		p = resolve_node(DRMPHX_PATH_CARD1, DRMPHX_PATH_V3D_LEGACY);
	}
	else if ((type == DRM_NODE_RENDER) && (minor == 128)) {
		p = resolve_node(DRMPHX_PATH_RENDER, DRMPHX_PATH_V3D_LEGACY);
	}
	if (p == NULL) {
		return -ENODEV;
	}
	fd = open(p, O_RDWR | O_CLOEXEC);
	return (fd >= 0) ? fd : -errno;
}


/* ========================================================================= */
/* Buffer mapping                                                             */
/* ========================================================================= */

static const char *ns_of_port(uint32_t port)
{
	uint32_t p;

	if ((port_of(KMS_BUF_NS, &p) == 0) && (p == port)) {
		return KMS_BUF_NS;
	}
	if ((port_of(V3DA_BUF_NS_EXT, &p) == 0) && (p == port)) {
		return V3DA_BUF_NS_EXT;
	}
	return NULL;
}


void *drmphx_map_memref(uint16_t kind, uint16_t cache, uint32_t port, uint64_t size, uint64_t addr, size_t len,
	int prot, void *hint, int fixed)
{
	char path[48];
	const char *ns;
	void *p;
	int flags = (cache == KMS_CACHE_UNCACHED) ? MAP_UNCACHED : 0, bfd;

	if (fixed != 0) {
		flags |= MAP_FIXED;
	}
	if ((len == 0u) || ((size != 0u) && (len > size))) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (kind == KMS_MEM_PHYS) {
		return mmap(hint, len, prot, flags | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)addr);
	}
	if (kind != KMS_MEM_OID) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	ns = ns_of_port(port);
	if (ns == NULL) {
		errno = ENODEV;
		return MAP_FAILED;
	}
	(void)snprintf(path, sizeof(path), "%s/%llu", ns, (unsigned long long)addr);
	bfd = open(path, O_RDONLY);   /* O_RDONLY: O_RDWR would stat() the name (E1 section 1) */
	if (bfd < 0) {
		return MAP_FAILED;
	}
	p = mmap(hint, len, prot, flags, bfd, 0);
	(void)close(bfd);   /* the mapping holds the object (E1 window refcount) */
	return p;
}


/* mmap() for DRM descriptors: `offset` is the token MMAP_BO / MAP_DUMB answered.
 * A PRIME descriptor ("/kmsbuf/<id>") maps with the export's memory type. Any
 * other descriptor falls through to mmap() unchanged. */
drm_public void *drmPhoenixMmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	drmphx_conn_t *c;
	kms_memref_t km;
	v3da_memref_t vm;
	uint32_t handle;
	int rc, fixed = ((flags & MAP_FIXED) != 0) ? 1 : 0;

	if (fd < 0) {
		return mmap(addr, length, prot, flags, fd, offset);
	}
	if (!DRMPHX_TOKEN_OK(offset)) {
		if ((offset == 0) && (drmphx_prime_fd_lookup(fd, &km) == 0)) {
			/* dma-buf mmap: the export's memory type is mandatory (E1) */
			return mmap(addr, length, prot, (flags & MAP_FIXED) | ((km.cache == KMS_CACHE_UNCACHED) ? MAP_UNCACHED : 0),
				fd, 0);
		}
		return mmap(addr, length, prot, flags, fd, offset);
	}
	rc = drmphx_get(fd, &c);
	if (rc != 0) {
		return mmap(addr, length, prot, flags, fd, offset);
	}
	handle = DRMPHX_TOKEN_HANDLE(offset);
	if (c->srv == DRMPHX_SRV_KMS) {
		rc = drmphx_kms_token_memref(c, handle, &km);
		drmphx_put(c);
		if (rc != 0) {
			errno = -rc;
			return MAP_FAILED;
		}
		return drmphx_map_memref(km.kind, km.cache, km.port, km.size, km.addr, length, prot, addr, fixed);
	}
	rc = drmphx_v3d_token_memref(c, handle, &vm);
	drmphx_put(c);
	if (rc != 0) {
		errno = -rc;
		return MAP_FAILED;
	}
	return drmphx_map_memref(vm.kind, vm.cache, vm.port, vm.size, vm.addr, length, prot, addr, fixed);
}


drm_public int drmPhoenixMunmap(void *addr, size_t length)
{
	return munmap(addr, length);   /* every mapping kind is a plain mapping once made */
}


/* ========================================================================= */
/* PRIME descriptors and sync files                                           */
/* ========================================================================= */

void drmphx_prime_fd_note(int fd, const kms_memref_t *m, int srv, uint32_t handle)
{
	uint32_t k;

	(void)pthread_mutex_lock(&G.lock);
	k = G.pf_next;
	G.pf[k].fd = fd;
	G.pf[k].srv = srv;
	G.pf[k].handle = handle;
	G.pf[k].mem = *m;
	if (sys_fdpath(fd, G.pf[k].path, sizeof(G.pf[k].path)) < 0) {
		G.pf[k].path[0] = '\0';
	}
	G.pf_next = (k + 1u) % DRMPHX_MAX_PRIMEFD;
	(void)pthread_mutex_unlock(&G.lock);
}


int drmphx_prime_fd_lookup(int fd, kms_memref_t *m)
{
	char path[DRMPHX_NODE_PATH_MAX], *end;
	unsigned long long id;
	const char *ns = NULL;
	uint32_t port, i, k;

	if (sys_fdpath(fd, path, sizeof(path)) < 0) {
		return -EBADF;
	}
	if (strncmp(path, KMS_BUF_NS "/", sizeof(KMS_BUF_NS)) == 0) {
		ns = KMS_BUF_NS;
	}
	else if (strncmp(path, V3DA_BUF_NS_EXT "/", sizeof(V3DA_BUF_NS_EXT)) == 0) {
		ns = V3DA_BUF_NS_EXT;
	}
	if (ns == NULL) {
		return -EINVAL;   /* not a buffer name we know */
	}
	id = strtoull(path + strlen(ns) + 1u, &end, 10);
	if ((*end != '\0') || (port_of(ns, &port) != 0)) {
		return -EINVAL;
	}
	memset(m, 0, sizeof(*m));
	m->kind = KMS_MEM_OID;
	m->cache = KMS_CACHE_UNCACHED;   /* both namespaces export Normal-NC memory */
	m->port = port;
	m->addr = id;

	/* Size: known only for descriptors this process exported (newest first). */
	(void)pthread_mutex_lock(&G.lock);
	for (i = 0u; i < DRMPHX_MAX_PRIMEFD; i++) {
		k = (G.pf_next + DRMPHX_MAX_PRIMEFD - 1u - i) % DRMPHX_MAX_PRIMEFD;
		if ((G.pf[k].path[0] != '\0') && (strcmp(G.pf[k].path, path) == 0)) {
			m->size = G.pf[k].mem.size;
			m->cache = G.pf[k].mem.cache;
			break;
		}
	}
	(void)pthread_mutex_unlock(&G.lock);
	return 0;
}


int drmphx_syncfile_new(int dev_fd, const v3da_fence_t *f)
{
	int nfd = dup(dev_fd);
	uint32_t k;

	if (nfd < 0) {
		return -errno;
	}
	(void)pthread_mutex_lock(&G.lock);
	k = G.sf_next;
	G.sf[k].used = 1;
	G.sf[k].fd = nfd;
	G.sf[k].fence = *f;
	G.sf_next = (k + 1u) % DRMPHX_MAX_SYNCFD;
	(void)pthread_mutex_unlock(&G.lock);
	return nfd;
}


int drmphx_syncfile_get(int fd, v3da_fence_t *f)
{
	uint32_t i, k;
	int rc = -EINVAL;

	if (fd < 0) {
		return -EINVAL;
	}
	(void)pthread_mutex_lock(&G.lock);
	for (i = 0u; i < DRMPHX_MAX_SYNCFD; i++) {   /* newest first: a recycled fd number resolves to its latest export */
		k = (G.sf_next + DRMPHX_MAX_SYNCFD - 1u - i) % DRMPHX_MAX_SYNCFD;
		if ((G.sf[k].used != 0) && (G.sf[k].fd == fd)) {
			*f = G.sf[k].fence;
			rc = 0;
			break;
		}
	}
	(void)pthread_mutex_unlock(&G.lock);
	return rc;
}
