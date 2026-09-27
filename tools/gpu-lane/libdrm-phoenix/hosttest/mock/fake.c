/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix host harness - fake rpi4-kms and rpi4-v3d-async
 *
 * Just enough of both servers' request semantics (kms_proto.h / v3da_proto.h, as
 * tools/gpu-lane/kms/kms_main.c and v3d-async/v3da_main.c implement them) to run
 * the real drmprobe + the real libdrm-phoenix natively: per-open client ids at
 * open(), HELLO ioctls, raw requests by msgSend, memrefs backed by one host
 * arena (PHYS = arena offset + base; OID = "/kmsbuf/<id>" and, G4, "/v3dbuf/<id>"
 * descriptors that map only with MAP_UNCACHED, as E1 enforces), DRM events by
 * read(). FAKE_V3DA_PROTO=2 in the environment makes the fake render server a
 * proto-2 one (before G4: HELLO exactly 2, no BO_EXPORT, no /v3dbuf);
 * FAKE_KMS_PROTO=1 makes the fake display server a proto-1 one (before G7: HELLO
 * exactly 1, no PRIME_IMPORT); FAKE_KMS_IMPORT_HIGH=1 places every imported buffer
 * above 1 GiB (the case the Pi cannot be made to produce). Jobs complete
 * at submit and flips at commit; there is no GPU, so the CL clear's pixels stay
 * as they were (the harness expects exactly that).
 *
 * Also counts payload windows whose start/end is not page aligned (E5: each
 * unaligned end costs a shadow page and ~30 us on the Pi).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#define MOCK_IMPL
#include "phx_mock.h"
/* run.sh force-includes phx_mock.h into every TU (this one too, before MOCK_IMPL
 * could take effect): the fakes themselves call the real functions. */
#undef mmap
#undef munmap
#undef ioctl
#undef open
#undef close
#undef dup
#undef read
#undef poll

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/msg.h>

#include "kms_proto.h"
#include "kms_scanout.h"
#include "v3da_proto.h"


#define KMS_PORT 11u
#define BUF_PORT 12u
#define V3D_PORT 13u
#define V3D_CARD_PORT 14u   /* /dev/dri/card1 (M3 part 2, G10: its own port = its own dev_t) */
#define VBUF_PORT 15u       /* /v3dbuf (G4) */
#define PA_BASE  0x10000000ull
#define FENCE_PA 0x0f000000ull
#define ARENA_SZ (64u << 20)
#define MAXFD    256

enum { K_NONE = 0, K_KMS, K_V3D, K_BUF, K_VBUF };

static struct {
	int kind[MAXFD];
	char path[MAXFD][64];
	uint32_t client[MAXFD];
	uint32_t port[MAXFD];    /* the port the descriptor's oid names (fstat's st_rdev) */
	uint8_t *arena;
	size_t arena_used;
	int dri;                 /* 1: the servers also registered /dev/dri names */
	uint32_t unaligned_ends, payload_msgs, msgs;
	uint32_t deferred_flips;   /* commits that arrived with an unsignalled render fence (G13) */
	uint32_t fstats, atsizes;  /* mtGetAttrAll / atSize answered (G2 / G3) */
	int old_v3d;               /* FAKE_V3DA_PROTO=2: a render server from before G4 */
	int old_kms;               /* FAKE_KMS_PROTO=1: a display server from before G7 */
	int import_high;           /* FAKE_KMS_IMPORT_HIGH=1: imports land above 1 GiB */
} F;

uint32_t fake_unaligned_ends(void);
uint32_t fake_payload_msgs(void);
uint32_t fake_msgs(void);
void fake_set_dri(int on);


static uint64_t arena_alloc(size_t size)
{
	uint64_t off;

	size = (size + 4095u) & ~(size_t)4095u;
	if (F.arena == NULL) {
		F.arena = mmap(NULL, ARENA_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}
	if (F.arena_used + size > ARENA_SZ) {
		return ~0ull;
	}
	off = F.arena_used;
	F.arena_used += size;
	memset(F.arena + off, 0, size);
	return off;
}


static int path_kind(const char *p)
{
	if ((strcmp(p, "/dev/kms") == 0) || (F.dri && (strcmp(p, "/dev/dri/card0") == 0))) {
		return K_KMS;
	}
	if ((strcmp(p, "/dev/v3d-async") == 0) ||
			(F.dri && ((strcmp(p, "/dev/dri/renderD128") == 0) || (strcmp(p, "/dev/dri/card1") == 0)))) {
		return K_V3D;
	}
	if (strncmp(p, "/kmsbuf/", 8) == 0) {
		return K_BUF;
	}
	if ((strncmp(p, "/v3dbuf/", 8) == 0) && !F.old_v3d) {
		return K_VBUF;
	}
	return K_NONE;
}


static uint32_t path_port(const char *p)
{
	switch (path_kind(p)) {
		case K_KMS: return KMS_PORT;
		case K_V3D: return (F.dri && (strcmp(p, "/dev/dri/card1") == 0)) ? V3D_CARD_PORT : V3D_PORT;
		case K_BUF: return BUF_PORT;
		case K_VBUF: return VBUF_PORT;
		default: return 0u;
	}
}


/* ========================================================================= */
/* fake rpi4-kms                                                              */
/* ========================================================================= */

#define NCLIENT 16
#define NBO 64
#define NFB 64
#define NBLOB 32

static struct {
	struct {
		int used, caps;
		kms_drm_event_vblank_t ev[64];
		uint32_t head, tail;
	} cl[NCLIENT + 1];
	uint32_t next_client;
	struct {
		int used, exported, prime;
		uint32_t handle, owner, w, h, pitch;
		uint64_t size, off;
		/* G7 imports (as kms_bo.c): the exporter's /v3dbuf name stays open - one
		 * reference on the render BO - until the handle is closed and no framebuffer
		 * uses the buffer */
		int imported, handle_open;
		int vbo;
		uint64_t imp_id;
		const char *why;
	} bo[NBO];
	uint32_t next_handle;
	uint32_t imports, imports_live, imports_released;
	struct {
		int used;
		uint32_t id, owner, bo, w, h, fmt;
	} fb[NFB];
	struct {
		int used;
		uint32_t id, owner, len;
		uint8_t data[256];
	} blob[NBLOB];
	kms_atomic_plane_t cur[8];
	kms_modeinfo_t mode;
	uint64_t seq;
	uint32_t mode_blob;
} K;


static void kms_init(void)
{
	if (K.next_handle != 0u) {
		return;
	}
	K.next_handle = 1u;
	memset(&K.mode, 0, sizeof(K.mode));
	K.mode.clock = 148500;
	K.mode.hdisplay = 1920;
	K.mode.hsync_start = 2008;
	K.mode.hsync_end = 2052;
	K.mode.htotal = 2200;
	K.mode.vdisplay = 1080;
	K.mode.vsync_start = 1084;
	K.mode.vsync_end = 1089;
	K.mode.vtotal = 1125;
	K.mode.vrefresh = 60;
	K.mode.type = KMS_MODE_TYPE_PREFERRED | KMS_MODE_TYPE_DRIVER;
	strcpy(K.mode.name, "1920x1080");
	K.blob[0].used = 1;
	K.blob[0].id = KMS_ID_BLOB_BASE;
	K.blob[0].len = sizeof(K.mode);
	memcpy(K.blob[0].data, &K.mode, sizeof(K.mode));
	K.mode_blob = KMS_ID_BLOB_BASE;
	K.seq = 1000;
}


static void kms_event(uint32_t client, uint32_t type, uint64_t user, uint32_t crtc)
{
	kms_drm_event_vblank_t *e;
	struct timespec ts;

	if (K.cl[client].head - K.cl[client].tail >= 64u) {
		return;
	}
	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	e = &K.cl[client].ev[K.cl[client].head++ % 64u];
	e->base.type = type;
	e->base.length = sizeof(*e);
	e->user_data = user;
	e->tv_sec = (uint32_t)ts.tv_sec;
	e->tv_usec = (uint32_t)(ts.tv_nsec / 1000);
	e->sequence = (uint32_t)K.seq;
	e->crtc_id = crtc;
}


static int plane_index(uint32_t id)
{
	return ((id == KMS_ID_PLANE(0u, 0u)) || (id == KMS_ID_PLANE(0u, 7u))) ? (int)(id - KMS_ID_PLANE(0u, 0u)) : -1;
}


static int fb_find(uint32_t id)
{
	int i;

	for (i = 0; i < NFB; i++) {
		if (K.fb[i].used && (K.fb[i].id == id)) {
			return i;
		}
	}
	return -1;
}


static int bo_find(uint32_t client, uint32_t handle)
{
	int i;

	for (i = 0; i < NBO; i++) {
		if (K.bo[i].used && (K.bo[i].handle == handle) && ((client == 0u) || (K.bo[i].owner == client)) &&
				(!K.bo[i].imported || (K.bo[i].handle_open && (client != 0u)))) {
			return i;
		}
	}
	return -1;
}


/* v3d fence check (the -G fence page) */
static int v3d_fence_done(const kms_fence_t *f);
static void v3d_complete_all(void);
/* G7: the reference rpi4-kms's open /v3dbuf descriptor holds on a render BO */
static int vbo_kms_ref(uint64_t id, uint64_t *size, uint64_t *off);
static void vbo_kms_unref(uint64_t id);


/* An import whose handle is closed goes with its last framebuffer (kms_bo_unref). */
static void kms_import_maybe_release(int b)
{
	int i;

	if ((b < 0) || (b >= NBO) || !K.bo[b].used || !K.bo[b].imported || K.bo[b].handle_open) {
		return;
	}
	for (i = 0; i < NFB; i++) {
		if (K.fb[i].used && (K.fb[i].bo == (uint32_t)b)) {
			return;
		}
	}
	vbo_kms_unref(K.bo[b].imp_id);
	K.imports_live--;
	K.imports_released++;
	memset(&K.bo[b], 0, sizeof(K.bo[b]));
}

static int kms_commit(uint32_t client, const kms_atomic_plane_t *st, uint32_t n, uint32_t flags, uint64_t user,
	kms_flip_resp_t *out)
{
	uint32_t i;
	int p, f;

	for (i = 0u; i < n; i++) {
		p = plane_index(st[i].plane_id);
		if (p < 0) {
			return -ENOENT;
		}
		if (st[i].fb_id != 0u) {
			f = fb_find(st[i].fb_id);
			if (f < 0) {
				return -ENOENT;
			}
			if (K.fb[f].owner != client) {
				return -EACCES;
			}
			if ((st[i].crtc_id != 0u) && (st[i].crtc_id != KMS_ID_CRTC(0u))) {
				return -EINVAL;
			}
			if ((st[i].src_w == 0u) || (st[i].crtc_w == 0u)) {
				return -EINVAL;   /* an empty rectangle: the real backend's check refuses it too */
			}
			if ((st[i].in_fence.seqno != 0u) && !v3d_fence_done(&st[i].in_fence)) {
				/* The real server (-G) holds the commit until the render fence page shows
				 * the fence; the fake GPU finishes its pending jobs "now" and counts it. */
				v3d_complete_all();
				F.deferred_flips++;
			}
		}
	}
	if ((flags & KMS_ATOMIC_TEST_ONLY) != 0u) {
		return 0;
	}
	if (out != NULL) {
		out->sequence = K.seq;
		out->applied = 1u;
	}
	for (i = 0u; i < n; i++) {
		p = plane_index(st[i].plane_id);
		K.cur[p] = st[i];
		K.cur[p].crtc_id = (st[i].fb_id != 0u) ? KMS_ID_CRTC(0u) : 0u;
		memset(&K.cur[p].in_fence, 0, sizeof(K.cur[p].in_fence));
	}
	K.seq++;   /* completes at the next vblank */
	if ((flags & KMS_PAGE_FLIP_EVENT) != 0u) {
		kms_event(client, KMS_DRM_EVENT_FLIP_COMPLETE, user, KMS_ID_CRTC(0u));
	}
	return 0;
}


static void fullscreen(uint32_t p, int f, kms_atomic_plane_t *st)
{
	memset(st, 0, sizeof(*st));
	st->plane_id = KMS_ID_PLANE(0u, p);
	st->crtc_id = KMS_ID_CRTC(0u);
	st->fb_id = K.fb[f].id;
	st->crtc_w = K.mode.hdisplay;
	st->crtc_h = K.mode.vdisplay;
	st->src_w = K.fb[f].w << 16;
	st->src_h = K.fb[f].h << 16;
	st->alpha = 0xffffu;
	st->rotation = 1u;
}


static uint32_t kms_props(uint32_t obj, kms_prop_value_t *out, uint32_t max)
{
	kms_prop_value_t v[20];
	uint32_t n = 0u, i;
	int p;

#define PV(id_, val_) do { v[n].prop_id = (id_); v[n].pad = 0u; v[n].value = (uint64_t)(val_); n++; } while (0)
	if (obj == KMS_ID_CONNECTOR(0u)) {
		PV(KMS_PROP_CRTC_ID, KMS_ID_CRTC(0u));
		PV(KMS_PROP_DPMS, 0);
		PV(KMS_PROP_EDID, 0);
		PV(KMS_PROP_LINK_STATUS, 0);
		PV(KMS_PROP_NON_DESKTOP, 0);
	}
	else if (obj == KMS_ID_CRTC(0u)) {
		PV(KMS_PROP_ACTIVE, 1);
		PV(KMS_PROP_MODE_ID, K.mode_blob);
		PV(KMS_PROP_OUT_FENCE_PTR, 0);
		PV(KMS_PROP_VRR_ENABLED, 0);
	}
	else if ((p = plane_index(obj)) >= 0) {
		const kms_atomic_plane_t *s = &K.cur[p];
		PV(KMS_PROP_TYPE, (p == 0) ? 1 : 2);
		PV(KMS_PROP_FB_ID, s->fb_id);
		PV(KMS_PROP_CRTC_ID, s->crtc_id);
		PV(KMS_PROP_SRC_X, s->src_x);
		PV(KMS_PROP_SRC_Y, s->src_y);
		PV(KMS_PROP_SRC_W, s->src_w);
		PV(KMS_PROP_SRC_H, s->src_h);
		PV(KMS_PROP_CRTC_X, (int64_t)s->crtc_x);
		PV(KMS_PROP_CRTC_Y, (int64_t)s->crtc_y);
		PV(KMS_PROP_CRTC_W, s->crtc_w);
		PV(KMS_PROP_CRTC_H, s->crtc_h);
		PV(KMS_PROP_IN_FENCE_FD, (int64_t)-1);
		PV(KMS_PROP_IN_FORMATS, 0);
		PV(KMS_PROP_ZPOS, p);
		PV(KMS_PROP_ALPHA, (s->fb_id != 0u) ? s->alpha : 0xffffu);
		PV(KMS_PROP_ROTATION, (s->rotation != 0u) ? s->rotation : 1u);
	}
#undef PV
	for (i = 0u; (i < n) && (i < max) && (out != NULL); i++) {
		out[i] = v[i];
	}
	return n;
}


static const struct {
	uint32_t id;
	const char *name;
	uint32_t flags, nen;
	uint64_t a, b;
} kprops[] = {
	{ KMS_PROP_TYPE, "type", KMS_PROP_FLAG_ENUM | KMS_PROP_FLAG_IMMUTABLE, 3, 0, 0 },
	{ KMS_PROP_FB_ID, "FB_ID", KMS_PROP_FLAG_OBJECT | KMS_PROP_FLAG_ATOMIC, 0, KMS_OBJ_FB, 0 },
	{ KMS_PROP_CRTC_ID, "CRTC_ID", KMS_PROP_FLAG_OBJECT | KMS_PROP_FLAG_ATOMIC, 0, KMS_OBJ_CRTC, 0 },
	{ KMS_PROP_SRC_X, "SRC_X", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_ATOMIC, 0, 0, 0xffffffffu },
	{ KMS_PROP_SRC_Y, "SRC_Y", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_ATOMIC, 0, 0, 0xffffffffu },
	{ KMS_PROP_SRC_W, "SRC_W", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_ATOMIC, 0, 0, 0xffffffffu },
	{ KMS_PROP_SRC_H, "SRC_H", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_ATOMIC, 0, 0, 0xffffffffu },
	{ KMS_PROP_CRTC_X, "CRTC_X", KMS_PROP_FLAG_SIGNED | KMS_PROP_FLAG_ATOMIC, 0, (uint64_t)(int64_t)-2147483648LL, 2147483647u },
	{ KMS_PROP_CRTC_Y, "CRTC_Y", KMS_PROP_FLAG_SIGNED | KMS_PROP_FLAG_ATOMIC, 0, (uint64_t)(int64_t)-2147483648LL, 2147483647u },
	{ KMS_PROP_CRTC_W, "CRTC_W", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_ATOMIC, 0, 0, 2147483647u },
	{ KMS_PROP_CRTC_H, "CRTC_H", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_ATOMIC, 0, 0, 2147483647u },
	{ KMS_PROP_IN_FENCE_FD, "IN_FENCE_FD", KMS_PROP_FLAG_SIGNED | KMS_PROP_FLAG_ATOMIC, 0, (uint64_t)(int64_t)-1, 2147483647u },
	{ KMS_PROP_IN_FORMATS, "IN_FORMATS", KMS_PROP_FLAG_BLOB | KMS_PROP_FLAG_IMMUTABLE, 0, 0, 0 },
	{ KMS_PROP_ZPOS, "zpos", KMS_PROP_FLAG_RANGE, 0, 0, 7 },
	{ KMS_PROP_ALPHA, "alpha", KMS_PROP_FLAG_RANGE, 0, 0, 0xffff },
	{ KMS_PROP_ROTATION, "rotation", KMS_PROP_FLAG_BITMASK, 4, 0, 0 },
	{ KMS_PROP_ACTIVE, "ACTIVE", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_ATOMIC, 0, 0, 1 },
	{ KMS_PROP_MODE_ID, "MODE_ID", KMS_PROP_FLAG_BLOB | KMS_PROP_FLAG_ATOMIC, 0, 0, 0 },
	{ KMS_PROP_OUT_FENCE_PTR, "OUT_FENCE_PTR", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_ATOMIC, 0, 0, ~0ull },
	{ KMS_PROP_VRR_ENABLED, "VRR_ENABLED", KMS_PROP_FLAG_RANGE, 0, 0, 1 },
	{ KMS_PROP_DPMS, "DPMS", KMS_PROP_FLAG_ENUM, 4, 0, 0 },
	{ KMS_PROP_EDID, "EDID", KMS_PROP_FLAG_BLOB | KMS_PROP_FLAG_IMMUTABLE, 0, 0, 0 },
	{ KMS_PROP_LINK_STATUS, "link-status", KMS_PROP_FLAG_ENUM, 2, 0, 0 },
	{ KMS_PROP_NON_DESKTOP, "non-desktop", KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_IMMUTABLE, 0, 0, 1 },
};

static const char *const en_names[4][4] = {
	{ "Overlay", "Primary", "Cursor", NULL }, { "rotate-0", "rotate-180", "reflect-x", "reflect-y" },
	{ "On", "Standby", "Suspend", "Off" }, { "Good", "Bad", NULL, NULL } };
static const uint64_t en_rot_vals[4] = { 0, 2, 4, 5 };


static int kms_get_property(uint32_t id, kms_property_t *o, msg_t *m, uint32_t max)
{
	unsigned i, k, which;
	kms_prop_enum_t *en = m->o.data;
	uint64_t *vals = m->o.data;

	for (i = 0; i < sizeof(kprops) / sizeof(kprops[0]); i++) {
		if (kprops[i].id != id) {
			continue;
		}
		memset(o, 0, sizeof(*o));
		snprintf(o->name, sizeof(o->name), "%s", kprops[i].name);
		o->flags = kprops[i].flags;
		if ((o->flags & (KMS_PROP_FLAG_ENUM | KMS_PROP_FLAG_BITMASK)) != 0u) {
			which = (id == KMS_PROP_TYPE) ? 0 : (id == KMS_PROP_ROTATION) ? 1 : (id == KMS_PROP_DPMS) ? 2 : 3;
			o->nenums = o->nvalues = kprops[i].nen;
			for (k = 0; (k < kprops[i].nen) && (k < max) && (m->o.data != NULL) &&
					((k + 1) * sizeof(kms_prop_enum_t) <= m->o.size); k++) {
				en[k].value = (which == 1) ? en_rot_vals[k] : k;
				snprintf(en[k].name, sizeof(en[k].name), "%s", en_names[which][k]);
			}
		}
		else if ((o->flags & (KMS_PROP_FLAG_RANGE | KMS_PROP_FLAG_SIGNED | KMS_PROP_FLAG_OBJECT)) != 0u) {
			o->nvalues = ((o->flags & KMS_PROP_FLAG_OBJECT) != 0u) ? 1u : 2u;
			if ((m->o.data != NULL) && (max != 0u) && (m->o.size >= o->nvalues * 8u)) {
				vals[0] = ((o->flags & KMS_PROP_FLAG_OBJECT) != 0u) ? kprops[i].a : kprops[i].a;
				if (o->nvalues == 2u) {
					vals[1] = kprops[i].b;
				}
			}
		}
		return 0;
	}
	return -ENOENT;
}


static void kms_handle(msg_t *m)
{
	kms_req_t rq;
	kms_resp_t *r = (kms_resp_t *)m->o.raw;
	uint32_t c = (uint32_t)m->oid.id, i, n, max;
	int rc = -EINVAL, b, f;

	kms_init();
	memcpy(&rq, m->i.raw, sizeof(rq));
	memset(r, 0, sizeof(*r));
	r->op = rq.op;
	m->o.err = 0;
	if (rq.magic != KMS_MAGIC) {
		m->o.err = -ENOTTY;
		return;
	}
	if ((c == 0u) || (c > NCLIENT) || !K.cl[c].used) {
		r->err = -EBADF;
		return;
	}
	max = rq.u.obj.max;
	switch (rq.op) {
		case KMS_OP_GET_CAP:
			switch (rq.u.cap.cap) {
				case KMS_CAP_DUMB_BUFFER: r->u.cap.value = 1; rc = 0; break;
				case KMS_CAP_PRIME: r->u.cap.value = 3; rc = 0; break;
				case KMS_CAP_TIMESTAMP_MONOTONIC: r->u.cap.value = 1; rc = 0; break;
				case KMS_CAP_CRTC_IN_VBLANK_EVENT: r->u.cap.value = 1; rc = 0; break;
				case KMS_CAP_ASYNC_PAGE_FLIP: r->u.cap.value = 0; rc = 0; break;
				default: rc = -EINVAL; break;
			}
			break;
		case KMS_OP_SET_CLIENT_CAP:
			if ((rq.u.cap.cap == KMS_CLIENT_CAP_UNIVERSAL_PLANES) || (rq.u.cap.cap == KMS_CLIENT_CAP_ATOMIC)) {
				K.cl[c].caps |= (rq.u.cap.value != 0u) ? (1 << rq.u.cap.cap) : 0;
				rc = 0;
			}
			break;
		case KMS_OP_SET_MASTER:
		case KMS_OP_DROP_MASTER:
		case KMS_OP_AUTH_MAGIC:
			rc = 0;
			break;
		case KMS_OP_GET_RESOURCES:
			r->u.res.min_w = r->u.res.min_h = 1;
			r->u.res.max_w = r->u.res.max_h = 4096;
			r->u.res.ncrtc = r->u.res.nconn = r->u.res.nenc = 1;
			r->u.res.crtc[0] = KMS_ID_CRTC(0u);
			r->u.res.conn[0] = KMS_ID_CONNECTOR(0u);
			r->u.res.enc[0] = KMS_ID_ENCODER(0u);
			for (i = 0, n = 0; i < NFB; i++) {
				if (K.fb[i].used && (K.fb[i].owner == c)) {
					if ((m->o.data != NULL) && (n < max)) {
						((uint32_t *)m->o.data)[n] = K.fb[i].id;
					}
					n++;
				}
			}
			r->u.res.nfb = n;
			rc = 0;
			break;
		case KMS_OP_GET_CONNECTOR:
			if (rq.u.obj.id != KMS_ID_CONNECTOR(0u)) {
				rc = -ENOENT;
				break;
			}
			r->u.conn.type = KMS_CONNECTOR_HDMIA;
			r->u.conn.type_id = 1;
			r->u.conn.connection = KMS_CONNECTED;
			r->u.conn.mm_width = 600;
			r->u.conn.mm_height = 340;
			r->u.conn.subpixel = 1;
			r->u.conn.encoder_id = KMS_ID_ENCODER(0u);
			r->u.conn.nmodes = 1;
			r->u.conn.nprops = kms_props(rq.u.obj.id, NULL, 0);
			if ((max >= 1u) && (m->o.data != NULL) && (m->o.size >= sizeof(K.mode))) {
				memcpy(m->o.data, &K.mode, sizeof(K.mode));
			}
			rc = 0;
			break;
		case KMS_OP_GET_ENCODER:
			rc = (rq.u.obj.id == KMS_ID_ENCODER(0u)) ? 0 : -ENOENT;
			r->u.enc.type = KMS_ENCODER_TMDS;
			r->u.enc.crtc_id = KMS_ID_CRTC(0u);
			r->u.enc.possible_crtcs = 1;
			break;
		case KMS_OP_GET_CRTC:
			if (rq.u.obj.id != KMS_ID_CRTC(0u)) {
				rc = -ENOENT;
				break;
			}
			r->u.crtc.fb_id = K.cur[0].fb_id;
			r->u.crtc.mode_valid = 1;
			r->u.crtc.hdisplay = K.mode.hdisplay;
			r->u.crtc.vdisplay = K.mode.vdisplay;
			r->u.crtc.vrefresh = 60;
			r->u.crtc.sequence = K.seq;
			if ((m->o.data != NULL) && (m->o.size >= sizeof(K.mode))) {
				memcpy(m->o.data, &K.mode, sizeof(K.mode));
			}
			rc = 0;
			break;
		case KMS_OP_SET_CRTC:
			if (rq.u.set_crtc.fb_id == 0u) {
				kms_atomic_plane_t off[2];
				memset(off, 0, sizeof(off));
				off[0].plane_id = KMS_ID_PLANE(0u, 0u);
				off[1].plane_id = KMS_ID_PLANE(0u, 7u);
				rc = kms_commit(c, off, 2, 0, 0, NULL);
			}
			else if ((rq.u.set_crtc.mode_hdisplay != 0u) && (rq.u.set_crtc.mode_hdisplay != K.mode.hdisplay)) {
				rc = -EINVAL;
			}
			else if ((f = fb_find(rq.u.set_crtc.fb_id)) < 0) {
				rc = -ENOENT;
			}
			else {
				kms_atomic_plane_t st;
				fullscreen(0, f, &st);
				rc = kms_commit(c, &st, 1, 0, 0, NULL);
			}
			break;
		case KMS_OP_GET_PLANE_RESOURCES:
			n = 0;
			if ((K.cl[c].caps & (1 << KMS_CLIENT_CAP_UNIVERSAL_PLANES)) != 0) {
				r->u.plane_res.plane[n++] = KMS_ID_PLANE(0u, 0u);
				r->u.plane_res.plane[n++] = KMS_ID_PLANE(0u, 7u);
			}
			r->u.plane_res.nplanes = n;
			rc = 0;
			break;
		case KMS_OP_GET_PLANE:
			if (plane_index(rq.u.obj.id) < 0) {
				rc = -ENOENT;
				break;
			}
			r->u.plane.crtc_id = K.cur[plane_index(rq.u.obj.id)].crtc_id;
			r->u.plane.fb_id = K.cur[plane_index(rq.u.obj.id)].fb_id;
			r->u.plane.possible_crtcs = 1;
			r->u.plane.type = (plane_index(rq.u.obj.id) == 0) ? KMS_PLANE_TYPE_PRIMARY : KMS_PLANE_TYPE_CURSOR;
			r->u.plane.nformats = 2;
			r->u.plane.formats[0] = KMS_FMT_XRGB8888;
			r->u.plane.formats[1] = KMS_FMT_ARGB8888;
			rc = 0;
			break;
		case KMS_OP_GET_PROPERTIES:
			n = kms_props(rq.u.obj.id, NULL, 0);
			if (n == 0u) {
				rc = -ENOENT;
				break;
			}
			if ((m->o.data != NULL) && (max != 0u)) {
				uint32_t fit = (uint32_t)(m->o.size / sizeof(kms_prop_value_t));
				(void)kms_props(rq.u.obj.id, m->o.data, (max < fit) ? max : fit);
			}
			r->u.count = n;
			rc = 0;
			break;
		case KMS_OP_GET_PROPERTY:
			rc = kms_get_property(rq.u.obj.id, &r->u.prop, m, max);
			break;
		case KMS_OP_GET_BLOB:
			rc = -ENOENT;
			for (i = 0; i < NBLOB; i++) {
				if (K.blob[i].used && (K.blob[i].id == rq.u.blob.id)) {
					r->u.blob.id = K.blob[i].id;
					r->u.blob.length = K.blob[i].len;
					if (m->o.data != NULL) {
						memcpy(m->o.data, K.blob[i].data, (K.blob[i].len < m->o.size) ? K.blob[i].len : m->o.size);
					}
					rc = 0;
				}
			}
			break;
		case KMS_OP_CREATE_BLOB:
			rc = -ENOSPC;
			if ((m->i.data == NULL) || (m->i.size == 0u) || (m->i.size > 256u)) {
				rc = -EINVAL;
				break;
			}
			for (i = 1; i < NBLOB; i++) {
				if (!K.blob[i].used) {
					K.blob[i].used = 1;
					K.blob[i].id = KMS_ID_BLOB_BASE + i;
					K.blob[i].owner = c;
					K.blob[i].len = (uint32_t)m->i.size;
					memcpy(K.blob[i].data, m->i.data, m->i.size);
					r->u.blob.id = K.blob[i].id;
					r->u.blob.length = K.blob[i].len;
					rc = 0;
					break;
				}
			}
			break;
		case KMS_OP_DESTROY_BLOB:
			rc = -ENOENT;
			for (i = 1; i < NBLOB; i++) {
				if (K.blob[i].used && (K.blob[i].id == rq.u.blob.id) && (K.blob[i].owner == c)) {
					K.blob[i].used = 0;
					rc = 0;
				}
			}
			break;
		case KMS_OP_CREATE_DUMB:
			rc = -ENOSPC;
			for (i = 0; i < NBO; i++) {
				if (!K.bo[i].used) {
					uint64_t off;
					K.bo[i].pitch = ((rq.u.create_dumb.width * rq.u.create_dumb.bpp / 8u) + 63u) & ~63u;
					K.bo[i].size = ((uint64_t)K.bo[i].pitch * rq.u.create_dumb.height + 4095u) & ~4095ull;
					off = arena_alloc(K.bo[i].size);
					if (off == ~0ull) {
						break;
					}
					K.bo[i].used = K.bo[i].exported = 1;
					K.bo[i].prime = 0;
					K.bo[i].off = off;
					K.bo[i].owner = c;
					K.bo[i].w = rq.u.create_dumb.width;
					K.bo[i].h = rq.u.create_dumb.height;
					K.bo[i].handle = K.next_handle++;
					r->u.dumb.handle = K.bo[i].handle;
					r->u.dumb.pitch = K.bo[i].pitch;
					r->u.dumb.size = K.bo[i].size;
					r->u.dumb.mem.kind = KMS_MEM_OID;
					r->u.dumb.mem.cache = KMS_CACHE_UNCACHED;
					r->u.dumb.mem.port = BUF_PORT;
					r->u.dumb.mem.size = K.bo[i].size;
					r->u.dumb.mem.addr = K.bo[i].handle;
					rc = 0;
					break;
				}
			}
			break;
		case KMS_OP_MAP_DUMB:
		case KMS_OP_PRIME_EXPORT:
			b = bo_find(c, rq.u.handle.handle);
			if (b < 0) {
				rc = -ENOENT;
				break;
			}
			if (rq.op == KMS_OP_PRIME_EXPORT) {
				K.bo[b].prime = 1;
			}
			r->u.dumb.handle = K.bo[b].handle;
			r->u.dumb.pitch = K.bo[b].pitch;
			r->u.dumb.size = K.bo[b].size;
			r->u.dumb.mem.kind = KMS_MEM_OID;
			r->u.dumb.mem.cache = KMS_CACHE_UNCACHED;
			r->u.dumb.mem.port = K.bo[b].imported ? VBUF_PORT : BUF_PORT;
			r->u.dumb.mem.size = K.bo[b].size;
			r->u.dumb.mem.addr = K.bo[b].imported ? K.bo[b].imp_id : K.bo[b].handle;
			rc = 0;
			break;
		case KMS_OP_DESTROY_DUMB:
			b = bo_find(c, rq.u.handle.handle);
			rc = (b < 0) ? -ENOENT : 0;
			if ((b >= 0) && K.bo[b].imported) {
				K.bo[b].handle_open = 0;
				kms_import_maybe_release(b);
			}
			else if (b >= 0) {
				K.bo[b].used = 0;
			}
			break;
		case KMS_OP_PRIME_IMPORT: {
			const kms_prime_import_req_t *q = &rq.u.prime_import;
			uint64_t size = 0u, off = 0u;
			if (F.old_kms) {
				rc = -EINVAL;   /* a proto-1 server: unknown opcode */
				break;
			}
			if ((q->ns == KMS_IMPORT_NS_KMSBUF) && (q->port == BUF_PORT) && ((b = bo_find(c, (uint32_t)q->id)) >= 0)) {
				r->u.dumb.handle = K.bo[b].handle;   /* an own export: the original handle */
				rc = 0;
				break;
			}
			if ((q->ns != KMS_IMPORT_NS_V3DBUF) || (q->port != VBUF_PORT) || (q->pad != 0u)) {
				rc = -EINVAL;
				break;
			}
			for (i = 0; i < NBO; i++) {   /* the same buffer again: the same handle */
				if (K.bo[i].used && K.bo[i].imported && K.bo[i].handle_open && (K.bo[i].owner == c) &&
						(K.bo[i].imp_id == q->id)) {
					break;
				}
			}
			if (i == NBO) {
				for (i = 0; (i < NBO) && K.bo[i].used; i++) {
				}
				if (i == NBO) {
					rc = -ENOSPC;
					break;
				}
				if (vbo_kms_ref(q->id, &size, &off) != 0) {
					rc = -ENOENT;   /* not (or no longer) exported */
					break;
				}
				memset(&K.bo[i], 0, sizeof(K.bo[i]));
				K.bo[i].used = K.bo[i].imported = K.bo[i].handle_open = 1;
				K.bo[i].owner = c;
				K.bo[i].handle = K.next_handle++;
				K.bo[i].size = size;
				K.bo[i].off = off;
				K.bo[i].imp_id = q->id;
				K.bo[i].why = F.import_high ? "above_1g" : NULL;
				K.imports++;
				K.imports_live++;
			}
			r->u.dumb.handle = K.bo[i].handle;
			r->u.dumb.pitch = 0u;
			r->u.dumb.size = K.bo[i].size;
			r->u.dumb.mem.kind = KMS_MEM_OID;
			r->u.dumb.mem.cache = KMS_CACHE_UNCACHED;
			r->u.dumb.mem.port = VBUF_PORT;
			r->u.dumb.mem.size = K.bo[i].size;
			r->u.dumb.mem.addr = K.bo[i].imp_id;
			rc = 0;
			break;
		}
		case KMS_OP_ADDFB2:
			b = bo_find(c, rq.u.addfb2.handle);
			if ((b >= 0) && K.bo[b].imported && (kms_import_fb_why(&rq.u.addfb2, K.bo[b].size, K.bo[b].why) != NULL)) {
				rc = -EINVAL;   /* the real server's rule (kms_scanout.h), refused at ADDFB2 */
				break;
			}
			if ((b < 0) || ((rq.u.addfb2.format != KMS_FMT_XRGB8888) && (rq.u.addfb2.format != KMS_FMT_ARGB8888)) ||
					(rq.u.addfb2.pitch < rq.u.addfb2.width * 4u) || (rq.u.addfb2.modifier != KMS_MOD_LINEAR)) {
				rc = (b < 0) ? -ENOENT : -EINVAL;
				break;
			}
			rc = -ENOSPC;
			for (i = 0; i < NFB; i++) {
				if (!K.fb[i].used) {
					K.fb[i].used = 1;
					K.fb[i].id = KMS_ID_FB_BASE + i;
					K.fb[i].owner = c;
					K.fb[i].bo = (uint32_t)b;
					K.fb[i].w = rq.u.addfb2.width;
					K.fb[i].h = rq.u.addfb2.height;
					K.fb[i].fmt = rq.u.addfb2.format;
					r->u.fb.fb_id = K.fb[i].id;
					rc = 0;
					break;
				}
			}
			break;
		case KMS_OP_RMFB:
			f = fb_find(rq.u.fb.fb_id);
			rc = ((f < 0) || (K.fb[f].owner != c)) ? -ENOENT : 0;
			if (rc == 0) {
				K.fb[f].used = 0;
				kms_import_maybe_release((int)K.fb[f].bo);
			}
			break;
		case KMS_OP_PAGE_FLIP: {
			kms_atomic_plane_t st;
			if (rq.u.flip.crtc_id != KMS_ID_CRTC(0u)) {
				rc = -ENOENT;
				break;
			}
			if ((f = fb_find(rq.u.flip.fb_id)) < 0) {
				rc = -ENOENT;
				break;
			}
			if (K.cur[0].fb_id != 0u) {
				st = K.cur[0];
				st.fb_id = rq.u.flip.fb_id;
			}
			else {
				fullscreen(0, f, &st);
			}
			st.in_fence = rq.u.flip.in_fence;
			rc = kms_commit(c, &st, 1, rq.u.flip.flags & KMS_PAGE_FLIP_EVENT, rq.u.flip.user_data, &r->u.flip);
			break;
		}
		case KMS_OP_ATOMIC:
			if (rq.u.atomic.crtc_id != KMS_ID_CRTC(0u)) {
				rc = -ENOENT;
				break;
			}
			if (rq.u.atomic.mode_blob != 0u) {
				const kms_modeinfo_t *mi = NULL;
				for (i = 0; i < NBLOB; i++) {
					if (K.blob[i].used && (K.blob[i].id == rq.u.atomic.mode_blob) && (K.blob[i].len >= sizeof(*mi))) {
						mi = (const kms_modeinfo_t *)K.blob[i].data;
					}
				}
				if ((mi == NULL) || (mi->hdisplay != K.mode.hdisplay)) {
					rc = -EINVAL;
					break;
				}
			}
			if (rq.u.atomic.active == 0u) {
				kms_atomic_plane_t off[2];
				memset(off, 0, sizeof(off));
				off[0].plane_id = KMS_ID_PLANE(0u, 0u);
				off[1].plane_id = KMS_ID_PLANE(0u, 7u);
				rc = kms_commit(c, off, 2, rq.u.atomic.flags, rq.u.atomic.user_data, &r->u.flip);
				break;
			}
			n = rq.u.atomic.nplanes;
			if ((n == 0u) || (n > KMS_ATOMIC_MAX_PLANES) || (m->i.data == NULL) ||
					(m->i.size < n * sizeof(kms_atomic_plane_t))) {
				rc = -EINVAL;
				break;
			}
			rc = kms_commit(c, m->i.data, n, rq.u.atomic.flags, rq.u.atomic.user_data, &r->u.flip);
			break;
		case KMS_OP_WAIT_VBLANK:
		case KMS_OP_CRTC_GET_SEQUENCE:
		case KMS_OP_CRTC_QUEUE_SEQUENCE: {
			uint64_t target;
			if (rq.u.vblank.crtc_id != KMS_ID_CRTC(0u)) {
				rc = -ENOENT;
				break;
			}
			if (rq.op == KMS_OP_CRTC_GET_SEQUENCE) {
				r->u.vblank.sequence = K.seq;
				r->u.vblank.time_ns = 1;
				rc = 0;
				break;
			}
			if (rq.op == KMS_OP_CRTC_QUEUE_SEQUENCE) {
				target = ((rq.u.vblank.type & 1u) != 0u) ? K.seq + rq.u.vblank.sequence : rq.u.vblank.sequence;
			}
			else {   /* the real server's encoding: bit 0 = ABSOLUTE */
				target = ((rq.u.vblank.type & KMS_VBL_ABSOLUTE) != 0u) ? rq.u.vblank.sequence : K.seq + rq.u.vblank.sequence;
			}
			if ((rq.op == KMS_OP_CRTC_QUEUE_SEQUENCE) || ((rq.u.vblank.type & KMS_VBL_EVENT) != 0u)) {
				r->u.vblank.sequence = target;
				rc = 0;
				break;
			}
			if (target > K.seq + 1000u) {
				rc = -EAGAIN;   /* would park past KMS_READ_MAX_MS */
				break;
			}
			if (target > K.seq) {
				K.seq = target;   /* time passes */
			}
			r->u.vblank.sequence = K.seq;
			r->u.vblank.time_ns = 1;
			rc = 0;
			break;
		}
		default:
			rc = -EINVAL;
			break;
	}
	r->err = rc;
}


/* ========================================================================= */
/* fake rpi4-v3d-async                                                        */
/* ========================================================================= */

static struct {
	struct {
		int used;
		struct {
			uint32_t handle;
			int state;
			v3da_fence_t fence;
		} sync[64];
		uint32_t next_sync;
	} cl[NCLIENT + 1];
	uint32_t next_client;
	struct {
		int used, imported, exported;
		uint32_t handle, owner, size;
		uint64_t off, imp_id;
		uint32_t refs, fd_opens;   /* G4: creator + sharers + open /v3dbuf descriptors, as v3da_bo.c */
		uint64_t sharers;
	} bo[256];
	uint32_t exports, v3dbuf_imports;
	uint32_t gen;
	uint64_t seqno;
	uint64_t pending[NCLIENT + 1][V3DA_Q_COUNT];   /* the fake GPU completes lazily: at the next wait */
	uint32_t imports, imports_closed;
	v3da_cl_desc_t last_cl;
	uint32_t last_nbo, last_nin, last_nout, last_bos[8];
	uint32_t submits;
} V;
static v3da_fence_page_t fence_page __attribute__((aligned(4096)));

void fake_last_cl(v3da_cl_desc_t *d, uint32_t *nbo, uint32_t *nin, uint32_t *nout, uint32_t *submits);


static int v3d_fence_done(const kms_fence_t *f)
{
	return (fence_page.slot[f->slot].completed[f->queue] >= f->seqno) ? 1 : 0;
}


/* Jobs "run" when someone waits for them (a server wait, or a fence-gated flip):
 * between submit and that point a fence is really pending, which is what the
 * library's fast paths and G13's implicit flip fence have to cope with. */
static void v3d_complete_all(void)
{
	uint32_t c, q;

	for (c = 0; c <= NCLIENT; c++) {
		for (q = 0; q < V3DA_Q_COUNT; q++) {
			if (V.pending[c][q] > fence_page.slot[c].completed[q]) {
				fence_page.slot[c].completed[q] = V.pending[c][q];
			}
		}
	}
}


static void mem_of(int b, v3da_memref_t *mem)
{
	memset(mem, 0, sizeof(*mem));
	mem->cache = V3DA_CACHE_UNCACHED;
	mem->size = V.bo[b].size;
	if (V.bo[b].imported) {
		mem->kind = V3DA_MEM_OID;   /* BO_MMAP of an import answers the exporter's name */
		mem->port = BUF_PORT;
		mem->addr = V.bo[b].imp_id;
	}
	else {
		mem->kind = V3DA_MEM_PHYS;
		mem->addr = PA_BASE + V.bo[b].off;
	}
}


static int bo_by_handle(uint32_t h)
{
	int i;

	for (i = 0; i < 256; i++) {
		if (V.bo[i].used && (V.bo[i].handle == h)) {
			return i;
		}
	}
	return -1;
}


/* A /v3dbuf-exported BO by id (the handle), or -1. */
static int vbuf_find(uint64_t id)
{
	int b = (id <= 0xffffffffu) ? bo_by_handle((uint32_t)id) : -1;

	return ((b >= 0) && V.bo[b].exported && (V.bo[b].refs > 0u)) ? b : -1;
}


static void vbo_unref(int b)
{
	if (V.bo[b].refs > 0u) {
		V.bo[b].refs--;
	}
	if (V.bo[b].refs == 0u) {
		V.imports_closed += V.bo[b].imported ? 1u : 0u;
		V.exports -= V.bo[b].exported ? 1u : 0u;
		memset(&V.bo[b], 0, sizeof(V.bo[b]));
	}
}


static int vbo_kms_ref(uint64_t id, uint64_t *size, uint64_t *off)
{
	int b = vbuf_find(id);

	if (b < 0) {
		return -ENOENT;
	}
	V.bo[b].fd_opens++;   /* rpi4-kms open()s the name and keeps the descriptor */
	V.bo[b].refs++;
	*size = V.bo[b].size;
	*off = V.bo[b].off;
	return 0;
}


static void vbo_kms_unref(uint64_t id)
{
	int b = (id <= 0xffffffffu) ? bo_by_handle((uint32_t)id) : -1;

	if ((b >= 0) && (V.bo[b].fd_opens > 0u)) {
		V.bo[b].fd_opens--;
		vbo_unref(b);
	}
}


static int sync_find(uint32_t c, uint32_t h)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (V.cl[c].sync[i].handle == h) {
			return i;
		}
	}
	return -1;
}


static int sync_signalled(uint32_t c, int s)
{
	return (V.cl[c].sync[s].state == V3DA_SYNC_SIGNALED) ||
		((V.cl[c].sync[s].state == V3DA_SYNC_FENCE) &&
		 (fence_page.slot[V.cl[c].sync[s].fence.slot].completed[V.cl[c].sync[s].fence.queue] >= V.cl[c].sync[s].fence.seqno));
}


static void v3d_handle(msg_t *m)
{
	v3da_req_t rq;
	v3da_resp_t *r = (v3da_resp_t *)m->o.raw;
	uint32_t c = (uint32_t)m->oid.id, i;
	int rc = -EINVAL, b, s;

	memcpy(&rq, m->i.raw, sizeof(rq));
	memset(r, 0, sizeof(*r));
	r->op = rq.op;
	m->o.err = 0;
	if (rq.magic != V3DA_MAGIC) {
		m->o.err = -ENOTTY;
		return;
	}
	if ((c == 0u) || (c > NCLIENT) || !V.cl[c].used) {
		r->err = -EBADF;
		return;
	}
	switch (rq.op) {
		case V3DA_OP_GET_PARAM:
			switch (rq.u.get_param.param) {
				case 4: r->u.get_param.value = 0x04443356u; break;   /* CORE0_IDENT0 */
				case 1: r->u.get_param.value = 0x000e1124u; break;   /* HUB_IDENT1 */
				case 7: case 8: case 9: case 11: case 12: r->u.get_param.value = 1u; break;
				default: r->u.get_param.value = 0u; break;
			}
			rc = 0;
			break;
		case V3DA_OP_BO_CREATE:
			rc = -ENOSPC;
			for (i = 0; i < 256; i++) {
				if (!V.bo[i].used) {
					uint64_t off = arena_alloc(rq.u.bo_create.size);
					if (off == ~0ull) {
						break;
					}
					V.bo[i].used = 1;
					V.bo[i].owner = c;
					V.bo[i].refs = 1u;
					V.bo[i].size = (rq.u.bo_create.size + 4095u) & ~4095u;
					V.bo[i].off = off;
					V.bo[i].handle = ((++V.gen) << 13) | (i + 1u);
					r->u.bo_create.handle = V.bo[i].handle;
					r->u.bo_create.gpuva = 0x100000u + (uint32_t)off;
					r->u.bo_create.size = V.bo[i].size;
					r->u.bo_create.mem.kind = V3DA_MEM_PHYS;
					r->u.bo_create.mem.cache = V3DA_CACHE_UNCACHED;
					r->u.bo_create.mem.size = V.bo[i].size;
					r->u.bo_create.mem.addr = PA_BASE + off;
					rc = 0;
					break;
				}
			}
			break;
		case V3DA_OP_BO_CLOSE:
		case V3DA_OP_BO_MMAP:
		case V3DA_OP_BO_GET_OFFSET:
		case V3DA_OP_BO_WAIT:
			b = bo_by_handle(rq.u.bo.handle);
			if (b < 0) {
				rc = -ENOENT;
				break;
			}
			if (rq.op == V3DA_OP_BO_WAIT) {
				v3d_complete_all();
			}
			r->u.bo.gpuva = 0x100000u + (uint32_t)V.bo[b].off;
			r->u.bo.size = V.bo[b].size;
			mem_of(b, &r->u.bo.mem);
			if (rq.op == V3DA_OP_BO_CLOSE) {   /* as v3da_bo_close: this client's import, else the creator's handle */
				uint64_t bit = 1ull << (c - 1u);
				if ((V.bo[b].sharers & bit) != 0u) {
					V.bo[b].sharers &= ~bit;
					vbo_unref(b);
				}
				else if (V.bo[b].owner != 0u) {
					V.bo[b].owner = 0u;
					vbo_unref(b);
				}
			}
			rc = 0;
			break;
		case V3DA_OP_BO_EXPORT:
			if (F.old_v3d) {
				rc = -EINVAL;   /* a proto-2 server: unknown opcode */
				break;
			}
			b = bo_by_handle(rq.u.bo.handle);
			if ((b < 0) || ((V.bo[b].owner != c) && ((V.bo[b].sharers & (1ull << (c - 1u))) == 0u))) {
				rc = -ENOENT;
				break;
			}
			if (V.bo[b].imported) {
				rc = -EINVAL;
				break;
			}
			V.exports += V.bo[b].exported ? 0u : 1u;
			V.bo[b].exported = 1;
			r->u.bo.gpuva = 0x100000u + (uint32_t)V.bo[b].off;
			r->u.bo.size = V.bo[b].size;
			r->u.bo.mem.kind = V3DA_MEM_OID;
			r->u.bo.mem.cache = V3DA_CACHE_UNCACHED;
			r->u.bo.mem.port = VBUF_PORT;
			r->u.bo.mem.size = V.bo[b].size;
			r->u.bo.mem.addr = V.bo[b].handle;
			rc = 0;
			break;
		case V3DA_OP_BO_IMPORT: {
			/* as v3da_bo.c v3da_bo_import: resolve {port, id}, size 0 = the whole export
			 * (lseek/atSize), same client + same buffer = same handle, no extra ref */
			const v3da_bo_import_req_t *q = &rq.u.bo_import;
			uint64_t size;
			int kb;
			if ((q->ns == V3DA_IMPORT_NS_V3DBUF) && F.old_v3d) {
				rc = -ENOSYS;
				break;
			}
			if (q->ns == V3DA_IMPORT_NS_V3DBUF) {   /* as v3da_bo.c import_v3dbuf: share THE BO */
				uint64_t bit = 1ull << (c - 1u);
				b = (q->port == VBUF_PORT) ? vbuf_find(q->id) : -1;
				if (b < 0) {
					rc = (q->port == VBUF_PORT) ? -ENOENT : -EINVAL;
					break;
				}
				if ((V.bo[b].owner != c) && ((V.bo[b].sharers & bit) == 0u)) {
					V.bo[b].sharers |= bit;
					V.bo[b].refs++;
					V.v3dbuf_imports++;
				}
				r->u.bo_create.handle = V.bo[b].handle;
				r->u.bo_create.gpuva = 0x100000u + (uint32_t)V.bo[b].off;
				r->u.bo_create.size = V.bo[b].size;
				r->u.bo_create.mem.kind = V3DA_MEM_OID;
				r->u.bo_create.mem.cache = V3DA_CACHE_UNCACHED;
				r->u.bo_create.mem.port = VBUF_PORT;
				r->u.bo_create.mem.size = V.bo[b].size;
				r->u.bo_create.mem.addr = V.bo[b].handle;
				rc = 0;
				break;
			}
			if ((q->ns != V3DA_IMPORT_NS_KMSBUF) || (q->port != BUF_PORT) || (q->pad != 0u)) {
				rc = -EINVAL;
				break;
			}
			kb = bo_find(0, (uint32_t)q->id);
			if ((kb < 0) || !K.bo[kb].exported) {
				rc = -ENOENT;
				break;
			}
			size = (q->size != 0u) ? q->size : K.bo[kb].size;
			if ((size > K.bo[kb].size) || ((size & 4095u) != 0u)) {
				rc = -EINVAL;
				break;
			}
			for (i = 0, b = -1; i < 256; i++) {
				if (V.bo[i].used && V.bo[i].imported && (V.bo[i].owner == c) && (V.bo[i].imp_id == q->id)) {
					b = (int)i;
					break;
				}
			}
			for (i = 0; (b < 0) && (i < 256); i++) {
				if (!V.bo[i].used) {
					b = (int)i;
					V.bo[b].used = V.bo[b].imported = 1;
					V.bo[b].owner = c;
					V.bo[b].refs = 1u;
					V.bo[b].size = (uint32_t)size;
					V.bo[b].off = K.bo[kb].off;   /* the kms arena pages: the same memory */
					V.bo[b].imp_id = q->id;
					V.bo[b].handle = ((++V.gen) << 13) | (i + 1u);
					V.imports++;
				}
			}
			if (b < 0) {
				rc = -ENOMEM;
				break;
			}
			r->u.bo_create.handle = V.bo[b].handle;
			r->u.bo_create.gpuva = 0x100000u + (uint32_t)V.bo[b].off;
			r->u.bo_create.size = V.bo[b].size;
			mem_of(b, &r->u.bo_create.mem);
			rc = 0;
			break;
		}
		case V3DA_OP_SUBMIT_CL:
		case V3DA_OP_SUBMIT_TFU:
		case V3DA_OP_SUBMIT_CSD: {
			const uint8_t *p = m->i.data;
			const v3da_sem_t *in, *out;
			size_t need = rq.u.submit.desc_size + rq.u.submit.nbo * 4u +
				(rq.u.submit.nin + rq.u.submit.nout) * sizeof(v3da_sem_t);
			if ((p == NULL) || (m->i.size < need)) {
				rc = -EINVAL;
				break;
			}
			if (rq.op == V3DA_OP_SUBMIT_CL) {
				memcpy(&V.last_cl, p, sizeof(V.last_cl));
			}
			V.last_nbo = rq.u.submit.nbo;
			V.last_nin = rq.u.submit.nin;
			V.last_nout = rq.u.submit.nout;
			memcpy(V.last_bos, p + rq.u.submit.desc_size, ((V.last_nbo < 8u) ? V.last_nbo : 8u) * 4u);
			in = (const v3da_sem_t *)(const void *)(p + rq.u.submit.desc_size + rq.u.submit.nbo * 4u);
			out = in + rq.u.submit.nin;
			for (i = 0; i < rq.u.submit.nin; i++) {
				s = sync_find(c, in[i].handle);
				if ((s < 0) || (V.cl[c].sync[s].state == V3DA_SYNC_EMPTY)) {
					rc = -EINVAL;
					goto done;
				}
			}
			V.seqno++;
			r->u.submit.first.slot = (uint16_t)c;
			r->u.submit.first.queue = (rq.op == V3DA_OP_SUBMIT_CL) ? V3DA_Q_BIN : (rq.op == V3DA_OP_SUBMIT_TFU) ? V3DA_Q_TFU : V3DA_Q_CSD;
			r->u.submit.first.gen = (uint32_t)fence_page.slot[c].gen;
			r->u.submit.first.seqno = V.seqno;
			r->u.submit.last = r->u.submit.first;
			if (rq.op == V3DA_OP_SUBMIT_CL) {
				r->u.submit.last.queue = V3DA_Q_RENDER;
			}
			V.pending[c][r->u.submit.first.queue] = V.seqno;   /* completes at the next wait (v3d_complete_all) */
			V.pending[c][r->u.submit.last.queue] = V.seqno;
			for (i = 0; i < rq.u.submit.nout; i++) {
				s = sync_find(c, out[i].handle);
				if (s >= 0) {
					V.cl[c].sync[s].state = V3DA_SYNC_FENCE;
					V.cl[c].sync[s].fence = r->u.submit.last;
				}
			}
			V.submits++;
			rc = 0;
			break;
		}
		case V3DA_OP_FENCE_WAIT:
			v3d_complete_all();
			r->u.fence_wait.completed = fence_page.slot[rq.u.fence_wait.fence.slot].completed[rq.u.fence_wait.fence.queue];
			r->u.fence_wait.echo = (uint32_t)rq.u.fence_wait.fence.seqno;
			rc = (r->u.fence_wait.completed >= rq.u.fence_wait.fence.seqno) ? 0 : -ETIMEDOUT;
			break;
		case V3DA_OP_SYNCOBJ_CREATE:
			rc = -ENOSPC;
			for (i = 0; i < 64; i++) {
				if (V.cl[c].sync[i].handle == 0u) {
					V.cl[c].sync[i].handle = ++V.cl[c].next_sync;
					V.cl[c].sync[i].state = ((rq.u.syncobj.flags & V3DA_SYNCOBJ_CREATE_SIGNALED) != 0u) ?
						V3DA_SYNC_SIGNALED : V3DA_SYNC_EMPTY;
					r->u.syncobj.handle = V.cl[c].sync[i].handle;
					rc = 0;
					break;
				}
			}
			break;
		case V3DA_OP_SYNCOBJ_DESTROY:
		case V3DA_OP_SYNCOBJ_RESET:
		case V3DA_OP_SYNCOBJ_SIGNAL:
		case V3DA_OP_SYNCOBJ_QUERY:
		case V3DA_OP_SYNCOBJ_IMPORT: {
			uint32_t h = (rq.op == V3DA_OP_SYNCOBJ_IMPORT) ? rq.u.syncobj_import.handle : rq.u.syncobj.handle;
			s = sync_find(c, h);
			if ((s < 0) || (h == 0u)) {
				rc = -ENOENT;
				break;
			}
			if (rq.op == V3DA_OP_SYNCOBJ_DESTROY) {
				memset(&V.cl[c].sync[s], 0, sizeof(V.cl[c].sync[s]));
			}
			else if (rq.op == V3DA_OP_SYNCOBJ_RESET) {
				V.cl[c].sync[s].state = V3DA_SYNC_EMPTY;
			}
			else if (rq.op == V3DA_OP_SYNCOBJ_SIGNAL) {
				V.cl[c].sync[s].state = V3DA_SYNC_SIGNALED;
			}
			else if (rq.op == V3DA_OP_SYNCOBJ_IMPORT) {
				V.cl[c].sync[s].state = V3DA_SYNC_FENCE;
				V.cl[c].sync[s].fence = rq.u.syncobj_import.fence;
			}
			else {
				r->u.syncobj.state = (uint32_t)V.cl[c].sync[s].state;
				r->u.syncobj.fence = V.cl[c].sync[s].fence;
			}
			rc = 0;
			break;
		}
		case V3DA_OP_SYNCOBJ_WAIT: {
			uint32_t nsig = 0, first = 0;
			v3d_complete_all();
			for (i = 0; (i < rq.u.syncobj.count) && (i < V3DA_SYNCOBJ_WAIT_MAX); i++) {
				s = sync_find(c, rq.u.syncobj.handles[i]);
				if (s < 0) {
					rc = -ENOENT;
					goto done;
				}
				if (sync_signalled(c, s)) {
					if (nsig++ == 0u) {
						first = i;
					}
				}
			}
			if (((rq.u.syncobj.flags & V3DA_SYNCOBJ_WAIT_ALL) != 0u) ? (nsig == rq.u.syncobj.count) : (nsig != 0u)) {
				r->u.syncobj.first = first;
				rc = 0;
			}
			else {
				rc = -ETIMEDOUT;
			}
			break;
		}
		case V3DA_OP_SUBMIT_CPU:
		case V3DA_OP_PERFMON_CREATE:
			rc = -ENOSYS;   /* reserved, as the real server */
			break;
		default:
			rc = -EINVAL;
			break;
	}
done:
	r->err = rc;
}


/* ========================================================================= */
/* Phoenix call stand-ins                                                     */
/* ========================================================================= */

static void count_alignment(const void *p, size_t n)
{
	if ((p == NULL) || (n == 0u)) {
		return;
	}
	F.payload_msgs++;
	F.unaligned_ends += (((uintptr_t)p & 4095u) != 0u) ? 1u : 0u;
	F.unaligned_ends += ((((uintptr_t)p + n) & 4095u) != 0u) ? 1u : 0u;
}


/* mtGetAttrAll (G2) on the node ports and /kmsbuf, atSize (G3) on /kmsbuf - as
 * kms_main.c/v3da_main.c/kms_bo.c answer them in M3 part 2. */
static void fake_attr(uint32_t port, msg_t *m)
{
	struct _attrAll *a = m->o.data;
	uint64_t size = 0;
	int b = -1;

	if ((port == BUF_PORT) || (port == VBUF_PORT)) {
		if (port == BUF_PORT) {
			b = (m->oid.id != 0u) ? bo_find(0, (uint32_t)m->oid.id) : -1;
			if ((b >= 0) && !K.bo[b].exported) {
				b = -1;
			}
			size = (b >= 0) ? K.bo[b].size : 0u;
		}
		else {
			b = (m->oid.id != 0u) ? vbuf_find(m->oid.id) : -1;
			size = (b >= 0) ? V.bo[b].size : 0u;
		}
		if ((m->oid.id != 0u) && (b < 0)) {
			m->o.err = -ENOENT;
			return;
		}
		if (m->type == mtGetAttr) {
			if (m->i.attr.type != atSize) {
				m->o.err = -ENOENT;
				return;
			}
			m->o.attr.val = (long long)size;
			m->o.err = 0;
			F.atsizes++;
			return;
		}
	}
	else if (m->type != mtGetAttrAll) {
		m->o.err = -EINVAL;   /* the node servers answer atMode only; nothing asks it here */
		return;
	}
	if ((a == NULL) || (m->o.size < sizeof(*a))) {
		m->o.err = -EINVAL;
		return;
	}
	memset(a, 0, sizeof(*a));
	a->mode.val = S_IFCHR | 0666;
	a->size.val = (long long)size;
	a->ioblock.val = 4096;
	a->links.val = 1;
	a->port.val = port;
	a->pollStatus.err = -EINVAL;
	a->eventMask.err = -EINVAL;
	m->o.err = 0;
	F.fstats++;
}


int msgSend(uint32_t port, msg_t *m)
{
	F.msgs++;
	count_alignment(m->i.data, m->i.size);
	count_alignment(m->o.data, m->o.size);
	if ((m->type == mtGetAttr) || (m->type == mtGetAttrAll)) {
		fake_attr(port, m);
		return 0;
	}
	if (m->type != mtDevCtl) {
		return -ENOSYS;
	}
	switch (port) {
		case KMS_PORT: kms_handle(m); return 0;
		case V3D_PORT:
		case V3D_CARD_PORT: v3d_handle(m); return 0;   /* both v3d ports serve the whole protocol */
		default: return -EINVAL;
	}
}


/* The kernel's posix_fstat (kernel-internal send: not counted as a payload):
 * st_rdev = the descriptor's port, the rest from mtGetAttrAll, first negative
 * err wins. */
int mock_fstat(int fd, struct stat *st)
{
	struct _attrAll a;
	msg_t m;
	int err;

	if ((fd < 0) || (fd >= MAXFD) || (F.kind[fd] == K_NONE)) {
		return fstat(fd, st);
	}
	memset(&m, 0, sizeof(m));
	m.type = mtGetAttrAll;
	m.oid.port = F.port[fd];
	m.oid.id = ((F.kind[fd] == K_BUF) || (F.kind[fd] == K_VBUF)) ? strtoull(F.path[fd] + 8, NULL, 10) : F.client[fd];
	m.o.data = &a;
	m.o.size = sizeof(a);
	fake_attr(F.port[fd], &m);
	err = m.o.err;
	if (err == 0) {
		const struct _attr *chk[] = { &a.mTime, &a.aTime, &a.cTime, &a.links, &a.mode, &a.uid, &a.gid, &a.size,
			&a.blocks, &a.ioblock };
		unsigned k;
		for (k = 0; (k < sizeof(chk) / sizeof(chk[0])) && (err == 0); k++) {
			err = chk[k]->err;
		}
	}
	if (err < 0) {
		errno = -err;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_dev = 1;
	st->st_ino = (ino_t)fd;
	st->st_rdev = (dev_t)F.port[fd];
	st->st_mode = (mode_t)a.mode.val;
	st->st_nlink = (nlink_t)a.links.val;
	st->st_size = (off_t)a.size.val;
	st->st_blksize = (blksize_t)a.ioblock.val;
	return 0;
}


/* The kernel's posix_lseek: SEEK_END = proc_size() = mtGetAttr(atSize). */
off_t mock_lseek(int fd, off_t off, int whence)
{
	msg_t m;

	if ((fd >= 0) && (fd < MAXFD) && ((F.kind[fd] == K_BUF) || (F.kind[fd] == K_VBUF))) {
		if (whence != SEEK_END) {
			return (whence == SEEK_SET) ? off : 0;
		}
		memset(&m, 0, sizeof(m));
		m.type = mtGetAttr;
		m.oid.port = F.port[fd];
		m.oid.id = strtoull(F.path[fd] + 8, NULL, 10);
		m.i.attr.type = atSize;
		fake_attr(F.port[fd], &m);
		if (m.o.err < 0) {
			errno = -m.o.err;
			return -1;
		}
		return (off_t)m.o.attr.val + off;
	}
	return lseek(fd, off, whence);
}


int lookup(const char *name, oid_t *file, oid_t *dev)
{
	oid_t o = { 0, 0 };
	int k = path_kind(name);

	if (k == K_KMS) {
		o.port = KMS_PORT;
	}
	else if (k == K_V3D) {
		o.port = path_port(name);
	}
	else if (strcmp(name, "/kmsbuf") == 0) {
		o.port = BUF_PORT;
	}
	else if ((strcmp(name, "/v3dbuf") == 0) && !F.old_v3d) {
		o.port = VBUF_PORT;
	}
	else if (k == K_VBUF) {
		o.port = VBUF_PORT;
		o.id = strtoull(name + 8, NULL, 10);
		if (vbuf_find(o.id) < 0) {
			return -ENOENT;
		}
	}
	else if (k == K_BUF) {
		o.port = BUF_PORT;
		o.id = strtoull(name + 8, NULL, 10);
		if (bo_find(0, (uint32_t)o.id) < 0) {
			return -ENOENT;
		}
	}
	else {
		return -ENOENT;
	}
	if (file != NULL) {
		*file = o;
	}
	if (dev != NULL) {
		*dev = o;
	}
	return 0;
}


int sys_fdpath(int fd, char *buf, size_t size)
{
	size_t n;

	if ((fd < 0) || (fd >= MAXFD)) {
		return -EBADF;
	}
	if (F.path[fd][0] == '\0') {
		return -ENOENT;
	}
	n = strlen(F.path[fd]);
	if (n >= size) {
		return -ERANGE;
	}
	memcpy(buf, F.path[fd], n + 1u);
	return (int)n;
}


int mock_open(const char *path, int flags, ...)
{
	int k = path_kind(path), fd;
	va_list ap;
	mode_t mode;

	va_start(ap, flags);
	mode = va_arg(ap, mode_t);
	va_end(ap);
	if (k == K_NONE) {
		if ((strncmp(path, "/dev/", 5) == 0) || (strcmp(path, "/kmsbuf") == 0)) {
			errno = ENOENT;
			return -1;
		}
		return open(path, flags, mode);
	}
	if (((k == K_BUF) || (k == K_VBUF)) && (lookup(path, NULL, NULL) != 0)) {
		errno = ENOENT;
		return -1;
	}
	fd = open("/dev/null", O_RDWR);
	if ((fd < 0) || (fd >= MAXFD)) {
		return -1;
	}
	F.kind[fd] = k;
	snprintf(F.path[fd], sizeof(F.path[fd]), "%s", path);
	F.client[fd] = 0;
	F.port[fd] = path_port(path);
	if (k == K_KMS) {   /* mtOpen: a new client */
		kms_init();
		F.client[fd] = ++K.next_client;
		K.cl[F.client[fd]].used = 1;
	}
	else if (k == K_V3D) {
		F.client[fd] = ++V.next_client;
		V.cl[F.client[fd]].used = 1;
		fence_page.slot[F.client[fd]].gen++;
	}
	else if (k == K_VBUF) {   /* mtOpen of an export: the descriptor holds a reference (G4) */
		int b = vbuf_find(strtoull(path + 8, NULL, 10));
		V.bo[b].fd_opens++;
		V.bo[b].refs++;
	}
	return fd;
}


int mock_close(int fd)
{
	if ((fd >= 0) && (fd < MAXFD)) {
		int last = 1, i;
		for (i = 0; i < MAXFD; i++) {
			if ((i != fd) && (F.kind[i] == F.kind[fd]) && (F.client[i] == F.client[fd]) && (F.client[fd] != 0u)) {
				last = 0;
			}
		}
		if (last && (F.kind[fd] == K_KMS)) {
			K.cl[F.client[fd]].used = 0;   /* mtClose */
		}
		if (last && (F.kind[fd] == K_V3D)) {
			V.cl[F.client[fd]].used = 0;
		}
		if (F.kind[fd] == K_VBUF) {   /* mtClose (each open() is its own open file here; dup is not used on these) */
			int b = bo_by_handle((uint32_t)strtoull(F.path[fd] + 8, NULL, 10));
			if ((b >= 0) && (V.bo[b].fd_opens > 0u)) {
				V.bo[b].fd_opens--;
				vbo_unref(b);
			}
		}
		F.kind[fd] = K_NONE;
		F.path[fd][0] = '\0';
		F.client[fd] = 0;
		F.port[fd] = 0;
	}
	return close(fd);
}


int mock_dup(int fd)
{
	int n = dup(fd);

	if ((n >= 0) && (n < MAXFD) && (fd >= 0) && (fd < MAXFD)) {
		F.kind[n] = F.kind[fd];
		memcpy(F.path[n], F.path[fd], sizeof(F.path[n]));   /* Phoenix: dup shares the open_file_t (and its path) */
		F.client[n] = F.client[fd];
		F.port[n] = F.port[fd];
	}
	return n;
}


/* mock_ioctl = ioctl() everywhere in the host build (library + drmprobe): it goes
 * through libdrm-phoenix's __wrap_ioctl exactly as the Pi link does
 * (--wrap=ioctl), and __wrap_ioctl's __real_ioctl is the fake device below. */
int mock_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	return __wrap_ioctl(fd, req, arg);
}


int __real_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	if ((fd < 0) || (fd >= MAXFD)) {
		errno = EBADF;
		return -1;
	}
	if ((req == KMS_IOC_HELLO) && (F.kind[fd] == K_KMS)) {
		kms_hello_t *h = arg;
		if (F.old_kms ? (h->proto != KMS_PROTO_BASE) : ((h->proto < KMS_PROTO_BASE) || (h->proto > KMS_PROTO_VERSION))) {
			errno = EPROTO;   /* a proto-1 server takes exactly 1; the G7 server BASE..VERSION */
			return -1;
		}
		memset(h, 0, sizeof(*h));
		h->proto = F.old_kms ? KMS_PROTO_BASE : KMS_PROTO_VERSION;
		h->client_id = F.client[fd];
		h->server_pid = 42;
		h->backend = KMS_BACKEND_PLANE;
		h->vblank_src = KMS_VBL_IRQ;
		h->ncrtc = 1;
		h->buf_port = BUF_PORT;
		h->refresh_mhz = 60010;
		h->width = 1920;
		h->height = 1080;
		return 0;
	}
	if ((req == V3DA_IOC_HELLO) && (F.kind[fd] == K_V3D)) {
		v3da_hello_t *h = arg;
		if (F.old_v3d ? (h->proto != V3DA_PROTO_BASE) : ((h->proto < V3DA_PROTO_BASE) || (h->proto > V3DA_PROTO_VERSION))) {
			errno = EPROTO;   /* a proto-2 server takes exactly 2; the G4 server BASE..VERSION */
			return -1;
		}
		memset(h, 0, sizeof(*h));
		h->proto = F.old_v3d ? V3DA_PROTO_BASE : V3DA_PROTO_VERSION;
		h->client_id = F.client[fd];
		h->slot = F.client[fd];
		h->slot_gen = (uint32_t)fence_page.slot[h->slot].gen;
		h->server_pid = 43;
		h->fence_page.kind = V3DA_MEM_PHYS;
		h->fence_page.cache = V3DA_CACHE_CACHED;
		h->fence_page.size = 4096;
		h->fence_page.addr = FENCE_PA;
		return 0;
	}
	errno = ENOTTY;   /* each server answers every other ioctl -ENOTTY */
	return -1;
}


void *__real_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	int phx = flags & (MAP_PHYSMEM | MAP_UNCACHED | MAP_CONTIGUOUS);

	if ((flags & MAP_PHYSMEM) != 0) {
		if ((uint64_t)off == FENCE_PA) {
			if ((flags & MAP_UNCACHED) != 0) {
				errno = EINVAL;   /* one memory type per page (E5) */
				return MAP_FAILED;
			}
			return &fence_page;
		}
		if (((uint64_t)off >= PA_BASE) && ((uint64_t)off + len <= PA_BASE + ARENA_SZ) && (F.arena != NULL)) {
			return F.arena + ((uint64_t)off - PA_BASE);
		}
		errno = EINVAL;
		return MAP_FAILED;
	}
	if ((fd >= 0) && (fd < MAXFD) && (F.kind[fd] == K_BUF)) {
		int b = bo_find(0, (uint32_t)strtoull(F.path[fd] + 8, NULL, 10));
		if ((b < 0) || ((flags & MAP_UNCACHED) == 0) || (len > K.bo[b].size) || (off != 0)) {
			errno = EINVAL;   /* E1: the export's memory type, within the window */
			return MAP_FAILED;
		}
		return F.arena + K.bo[b].off;
	}
	if ((fd >= 0) && (fd < MAXFD) && (F.kind[fd] == K_VBUF)) {
		int b = vbuf_find(strtoull(F.path[fd] + 8, NULL, 10));
		if ((b < 0) || ((flags & MAP_UNCACHED) == 0) || (len > V.bo[b].size) || (off != 0)) {
			errno = EINVAL;
			return MAP_FAILED;
		}
		return F.arena + V.bo[b].off;
	}
	if ((fd >= 0) && (fd < MAXFD) && (F.kind[fd] != K_NONE)) {
		errno = EINVAL;   /* a node descriptor's object is not a buffer (atSize refused) */
		return MAP_FAILED;
	}
	flags &= ~phx;
	if (((flags & MAP_ANONYMOUS) != 0) && ((flags & (MAP_PRIVATE | MAP_SHARED)) == 0)) {
		flags |= MAP_PRIVATE;
	}
	return mmap(addr, len, prot, flags, fd, off);
}


int mock_munmap(void *addr, size_t len)
{
	if (((F.arena != NULL) && ((uint8_t *)addr >= F.arena) && ((uint8_t *)addr < F.arena + ARENA_SZ)) ||
			(addr == (void *)&fence_page)) {
		return 0;
	}
	return munmap(addr, len);
}


ssize_t mock_read(int fd, void *buf, size_t n)
{
	uint32_t c;
	size_t done = 0;

	if ((fd < 0) || (fd >= MAXFD) || (F.kind[fd] != K_KMS)) {
		return read(fd, buf, n);
	}
	c = F.client[fd];
	while ((K.cl[c].head != K.cl[c].tail) && (done + 32u <= n)) {
		memcpy((uint8_t *)buf + done, &K.cl[c].ev[K.cl[c].tail++ % 64u], 32u);
		done += 32u;
	}
	if (done == 0u) {
		errno = EAGAIN;   /* the real server parks <= 2 s, then answers -EAGAIN */
		return -1;
	}
	return (ssize_t)done;
}


int mock_poll(struct pollfd *fds, nfds_t n, int timeout)
{
	nfds_t i;
	int ready = 0;

	(void)timeout;
	for (i = 0; i < n; i++) {
		fds[i].revents = 0;
		if ((fds[i].fd >= 0) && (fds[i].fd < MAXFD) && (F.kind[fds[i].fd] == K_KMS) &&
				(K.cl[F.client[fds[i].fd]].head != K.cl[F.client[fds[i].fd]].tail)) {
			fds[i].revents = POLLIN;
			ready++;
		}
	}
	return ready;
}


uint32_t fake_unaligned_ends(void) { return F.unaligned_ends; }
uint32_t fake_payload_msgs(void) { return F.payload_msgs; }
uint32_t fake_msgs(void) { return F.msgs; }
uint32_t fake_deferred_flips(void) { return F.deferred_flips; }
void fake_m3p2(uint32_t *fstats, uint32_t *atsizes, uint32_t *imports, uint32_t *imports_closed)
{
	*fstats = F.fstats;
	*atsizes = F.atsizes;
	*imports = V.imports;
	*imports_closed = V.imports_closed;
}
void fake_set_dri(int on) { F.dri = on; }
void fake_set_old_v3d(int on) { F.old_v3d = on; }
void fake_set_kms(int old_kms, int import_high) { F.old_kms = old_kms; F.import_high = import_high; }
void fake_g7(uint32_t *imports, uint32_t *imports_live, uint32_t *imports_released)
{
	*imports = K.imports;
	*imports_live = K.imports_live;
	*imports_released = K.imports_released;
}
void fake_g4(uint32_t *exports_live, uint32_t *v3dbuf_imports, uint32_t *bos_live)
{
	uint32_t i, n = 0;

	for (i = 0; i < 256u; i++) {
		n += V.bo[i].used ? 1u : 0u;
	}
	*exports_live = V.exports;
	*v3dbuf_imports = V.v3dbuf_imports;
	*bos_live = n;
}

void fake_last_cl(v3da_cl_desc_t *d, uint32_t *nbo, uint32_t *nin, uint32_t *nout, uint32_t *submits)
{
	*d = V.last_cl;
	*nbo = V.last_nbo;
	*nin = V.last_nin;
	*nout = V.last_nout;
	*submits = V.submits;
}
