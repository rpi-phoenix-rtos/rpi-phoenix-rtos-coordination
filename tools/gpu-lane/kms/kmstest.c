/*
 * Phoenix-RTOS
 *
 * kmstest - first client of the new-lane display server rpi4-kms (M2 Stage A)
 *
 * Usage: kmstest [-n flips] [-H hold_s] [-p] <cmd>...
 *   info    HELLO, caps, resources, connector + mode, encoder, CRTC, planes,
 *           properties (names + values), IN_FORMATS / EDID blobs
 *   pool    a pool BO (KMS_DUMB_POOL): open /kmsbuf/<id> + mmap(MAP_UNCACHED), write,
 *           server-side checksum must match; a cached mapping must be refused;
 *           after DESTROY the name must be gone
 *   flip    two mode-sized BOs, CPU-drawn (marker left / right + a moving box),
 *           flipped N times (-n, default 600) with FLIP_COMPLETE events read()
 *           from the card fd; flip interval histogram, missed vblanks, commit ->
 *           event latency; EBUSY + TEST_ONLY checks; -H holds the last frame
 *   vblank  60 blocking WAIT_VBLANKs + 10 QUEUE_SEQUENCE events
 *   stats   the server's counters
 *   quit    ask the server to restore the display and exit
 *   all     info pool flip vblank stats
 *   -p      flip: force pool BOs. With the pan backend a pool BO is not scannable:
 *           one 256x256 pool framebuffer is flipped to and must get -EINVAL
 *           (a negative test); with the plane backend the flip test runs on pool BOs
 *
 * Every result line starts with "KMSTEST "; a "KMSTEST tick" line is printed at
 * least every 2 s so a harness idle timer never cuts a running command.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/types.h>

#include "kms_proto.h"


static void kt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void kt(const char *fmt, ...)
{
	va_list ap;
	char line[400];
	int n = snprintf(line, sizeof(line), "KMSTEST ");

	va_start(ap, fmt);
	(void)vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
	va_end(ap);
	puts(line);
}


static uint64_t cnt_hz;

static inline uint64_t cnt_now(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)::"memory");
	return v;
}

static inline uint64_t cnt_us(uint64_t t)
{
	return (t * 1000000ULL) / cnt_hz;
}

static int64_t mono_ns(void)
{
	struct timespec ts;
	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}


static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return (x > y) - (x < y);
}

static uint32_t pctl(uint32_t *v, uint32_t n, uint32_t pct)
{
	if (n == 0u) {
		return 0u;
	}
	qsort(v, n, sizeof(*v), cmp_u32);
	return v[((uint64_t)(n - 1u) * pct) / 100u];
}


/* ========================================================================= */
/* Minimal client (libdrm-phoenix's shape, M3)                                */
/* ========================================================================= */

static struct {
	int fd;
	oid_t oid;
	kms_hello_t h;
	uint32_t crtc, conn, primary;
	uint32_t fmt;
	uint32_t w, h_;
} k = { .fd = -1 };


static int kreq(uint32_t op, const void *u, size_t usz, const void *idata, size_t isz, void *odata, size_t osz,
	kms_resp_t *out)
{
	msg_t msg;
	kms_req_t *rq = (kms_req_t *)msg.i.raw;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = k.oid;
	rq->magic = KMS_MAGIC;
	rq->op = op;
	if ((u != NULL) && (usz <= sizeof(rq->u))) {
		memcpy(&rq->u, u, usz);
	}
	msg.i.data = idata;
	msg.i.size = isz;
	msg.o.data = odata;
	msg.o.size = osz;
	err = msgSend(k.oid.port, &msg);
	if (err < 0) {
		return err;
	}
	if (msg.o.err < 0) {
		return msg.o.err;
	}
	memcpy(out, msg.o.raw, sizeof(*out));
	return out->err;
}


static int kconnect(void)
{
	oid_t dev;

	if (k.fd >= 0) {
		return 0;
	}
	k.fd = open("/dev/" KMS_DEV_NAME, O_RDONLY);
	if (k.fd < 0) {
		kt("connect FAIL open errno=%d (server running?)", errno);
		return -1;
	}
	if (lookup("/dev/" KMS_DEV_NAME, NULL, &dev) < 0) {
		kt("connect FAIL lookup");
		return -1;
	}
	memset(&k.h, 0, sizeof(k.h));
	k.h.proto = KMS_PROTO_VERSION;
	if (ioctl(k.fd, KMS_IOC_HELLO, &k.h) < 0) {
		kt("connect FAIL hello errno=%d", errno);
		return -1;
	}
	k.oid.port = dev.port;
	k.oid.id = k.h.client_id;
	k.crtc = KMS_ID_CRTC(0);
	k.conn = KMS_ID_CONNECTOR(0);
	k.primary = KMS_ID_PLANE(0u, 0u);
	k.w = k.h.width;
	k.h_ = k.h.height;
	kt("connect client=%u server_pid=%u backend=%s vblank_src=%u ncrtc=%u buf_port=%u mode=%ux%u refresh_mhz=%u",
		k.h.client_id, k.h.server_pid, (k.h.backend == KMS_BACKEND_PLANE) ? "plane" : "pan", k.h.vblank_src, k.h.ncrtc,
		k.h.buf_port, k.h.width, k.h.height, k.h.refresh_mhz);
	return 0;
}


static void *kmap(const kms_memref_t *m, int cached, int *err)
{
	char path[48];
	void *p;
	int fd, flags = cached ? 0 : MAP_UNCACHED;

	*err = 0;
	if (m->kind == KMS_MEM_PHYS) {
		p = mmap(NULL, (size_t)m->size, PROT_READ | PROT_WRITE, flags | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)m->addr);
	}
	else if (m->kind == KMS_MEM_OID) {
		snprintf(path, sizeof(path), "%s/%llu", KMS_BUF_NS, (unsigned long long)m->addr);
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			*err = -errno;
			return NULL;
		}
		p = mmap(NULL, (size_t)m->size, PROT_READ | PROT_WRITE, flags, fd, 0);
		if (p == MAP_FAILED) {
			*err = -errno;
		}
		close(fd);   /* the mapping holds the object */
	}
	else {
		*err = -EINVAL;
		return NULL;
	}
	if (p == MAP_FAILED) {
		if (*err == 0) {
			*err = -errno;
		}
		return NULL;
	}
	return p;
}


static uint32_t pack(uint32_t r, uint32_t g, uint32_t b)
{
	uint32_t a = ((k.fmt == KMS_FMT_ARGB8888) || (k.fmt == KMS_FMT_ABGR8888)) ? 0xff000000u : 0u;

	if ((k.fmt == KMS_FMT_XBGR8888) || (k.fmt == KMS_FMT_ABGR8888)) {
		return a | (b << 16) | (g << 8) | r;
	}
	return a | (r << 16) | (g << 8) | b;
}


static uint32_t fnv(const volatile uint32_t *p, uint32_t words)
{
	uint32_t h = 2166136261u, i;

	for (i = 0u; i < words; i++) {
		h = (h ^ p[i]) * 16777619u;
	}
	return h;
}


static const char *prop_name(uint32_t id, char *buf, size_t n)
{
	kms_obj_req_t q = { .id = id };
	kms_resp_t r;

	if (kreq(KMS_OP_GET_PROPERTY, &q, sizeof(q), NULL, 0, NULL, 0, &r) == 0) {
		snprintf(buf, n, "%s", r.u.prop.name);
	}
	else {
		snprintf(buf, n, "?%u", id);
	}
	return buf;
}


static void print_props(const char *what, uint32_t obj, uint32_t type)
{
	kms_prop_value_t v[24];
	kms_obj_req_t q = { .id = obj, .type = type, .max = 24 };
	kms_resp_t r;
	char line[360], name[KMS_PROP_NAME_LEN];
	uint32_t i;
	int n = 0, rc;

	rc = kreq(KMS_OP_GET_PROPERTIES, &q, sizeof(q), NULL, 0, v, sizeof(v), &r);
	if (rc != 0) {
		kt("info props %s id=0x%x rc=%d", what, obj, rc);
		return;
	}
	line[0] = '\0';
	for (i = 0u; (i < r.u.count) && (i < 24u); i++) {
		n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%s=%lld", (i == 0u) ? "" : " ",
			prop_name(v[i].prop_id, name, sizeof(name)), (long long)v[i].value);
		if (n >= (int)sizeof(line) - 40) {
			break;
		}
	}
	kt("info props %s id=0x%x n=%u %s", what, obj, r.u.count, line);
}


/* ========================================================================= */
/* info                                                                       */
/* ========================================================================= */

static int cmd_info(void)
{
	static const struct { uint32_t cap; const char *name; } caps[] = {
		{ KMS_CAP_DUMB_BUFFER, "dumb" }, { KMS_CAP_PRIME, "prime" }, { KMS_CAP_TIMESTAMP_MONOTONIC, "mono" },
		{ KMS_CAP_ASYNC_PAGE_FLIP, "async" }, { KMS_CAP_CURSOR_WIDTH, "cursor_w" },
		{ KMS_CAP_ADDFB2_MODIFIERS, "modifiers" }, { KMS_CAP_CRTC_IN_VBLANK_EVENT, "crtc_in_ev" },
		{ KMS_CAP_DUMB_PREFERRED_DEPTH, "depth" },
	};
	kms_resp_t r;
	kms_modeinfo_t mode;
	kms_obj_req_t q;
	kms_cap_req_t cq;
	char line[256];
	uint32_t i, n = 0u, fails = 0u;
	int rc;

	line[0] = '\0';
	for (i = 0u; i < sizeof(caps) / sizeof(caps[0]); i++) {
		memset(&cq, 0, sizeof(cq));
		cq.cap = caps[i].cap;
		rc = kreq(KMS_OP_GET_CAP, &cq, sizeof(cq), NULL, 0, NULL, 0, &r);
		n += (uint32_t)snprintf(line + n, sizeof(line) - n, " %s=%lld", caps[i].name, (rc == 0) ? (long long)r.u.cap.value : (long long)rc);
	}
	kt("info caps%s", line);
	memset(&cq, 0, sizeof(cq));
	cq.cap = KMS_CLIENT_CAP_UNIVERSAL_PLANES;
	cq.value = 1u;
	rc = kreq(KMS_OP_SET_CLIENT_CAP, &cq, sizeof(cq), NULL, 0, NULL, 0, &r);
	cq.cap = KMS_CLIENT_CAP_ATOMIC;
	fails += (kreq(KMS_OP_SET_CLIENT_CAP, &cq, sizeof(cq), NULL, 0, NULL, 0, &r) != 0) + (rc != 0);

	memset(&q, 0, sizeof(q));
	rc = kreq(KMS_OP_GET_RESOURCES, &q, sizeof(q), NULL, 0, NULL, 0, &r);
	kt("info resources rc=%d crtcs=%u conns=%u encs=%u crtc0=0x%x conn0=0x%x enc0=0x%x fbs=%u max=%ux%u", rc,
		r.u.res.ncrtc, r.u.res.nconn, r.u.res.nenc, r.u.res.crtc[0], r.u.res.conn[0], r.u.res.enc[0], r.u.res.nfb,
		r.u.res.max_w, r.u.res.max_h);
	fails += (rc != 0);

	memset(&mode, 0, sizeof(mode));
	q.id = k.conn;
	q.max = 1u;
	rc = kreq(KMS_OP_GET_CONNECTOR, &q, sizeof(q), NULL, 0, &mode, sizeof(mode), &r);
	kt("info connector rc=%d type=%u connection=%u mm=%ux%u encoder=0x%x modes=%u props=%u fw_display_id=%u", rc,
		r.u.conn.type, r.u.conn.connection, r.u.conn.mm_width, r.u.conn.mm_height, r.u.conn.encoder_id, r.u.conn.nmodes,
		r.u.conn.nprops, r.u.conn.fw_display_id);
	kt("info mode name=%s clock_khz=%u h=%u/%u/%u/%u v=%u/%u/%u/%u vrefresh=%u flags=0x%x type=0x%x", mode.name,
		mode.clock, mode.hdisplay, mode.hsync_start, mode.hsync_end, mode.htotal, mode.vdisplay, mode.vsync_start,
		mode.vsync_end, mode.vtotal, mode.vrefresh, mode.flags, mode.type);
	fails += (rc != 0);

	q.id = KMS_ID_ENCODER(0);
	rc = kreq(KMS_OP_GET_ENCODER, &q, sizeof(q), NULL, 0, NULL, 0, &r);
	kt("info encoder rc=%d type=%u crtc=0x%x possible_crtcs=0x%x", rc, r.u.enc.type, r.u.enc.crtc_id,
		r.u.enc.possible_crtcs);
	q.id = k.crtc;
	rc = kreq(KMS_OP_GET_CRTC, &q, sizeof(q), NULL, 0, NULL, 0, &r);
	kt("info crtc rc=%d fb=%u mode=%ux%u@%u seq=%llu pending_fb=%u", rc, r.u.crtc.fb_id, r.u.crtc.hdisplay,
		r.u.crtc.vdisplay, r.u.crtc.vrefresh, (unsigned long long)r.u.crtc.sequence, r.u.crtc.pending_fb);
	fails += (rc != 0);

	memset(&q, 0, sizeof(q));
	rc = kreq(KMS_OP_GET_PLANE_RESOURCES, &q, sizeof(q), NULL, 0, NULL, 0, &r);
	{
		kms_plane_res_t pr = r.u.plane_res;
		kt("info planes rc=%d n=%u", rc, pr.nplanes);
		for (i = 0u; (i < pr.nplanes) && (i < 13u); i++) {
			q.id = pr.plane[i];
			rc = kreq(KMS_OP_GET_PLANE, &q, sizeof(q), NULL, 0, NULL, 0, &r);
			kt("info plane id=0x%x rc=%d type=%u fb=%u crtc=0x%x formats=%u fmt0=%.4s fmt1=%.4s", pr.plane[i], rc,
				r.u.plane.type, r.u.plane.fb_id, r.u.plane.crtc_id, r.u.plane.nformats, (const char *)&r.u.plane.formats[0],
				(r.u.plane.nformats > 1u) ? (const char *)&r.u.plane.formats[1] : "----");
			if ((rc == 0) && (r.u.plane.type == KMS_PLANE_TYPE_PRIMARY)) {
				k.primary = pr.plane[i];
				k.fmt = r.u.plane.formats[0];
			}
			print_props("plane", pr.plane[i], KMS_OBJ_PLANE);
		}
	}
	print_props("crtc", k.crtc, KMS_OBJ_CRTC);
	print_props("connector", k.conn, KMS_OBJ_CONNECTOR);

	/* IN_FORMATS of the primary: a drm_format_modifier_blob */
	{
		kms_prop_value_t v[24];
		uint8_t blob[256];
		kms_blob_t bq;
		memset(&q, 0, sizeof(q));
		q.id = k.primary;
		q.max = 24u;
		if (kreq(KMS_OP_GET_PROPERTIES, &q, sizeof(q), NULL, 0, v, sizeof(v), &r) == 0) {
			uint32_t cnt = r.u.count;
			for (i = 0u; (i < cnt) && (i < 24u); i++) {
				if ((v[i].prop_id == KMS_PROP_IN_FORMATS) && (v[i].value != 0u)) {
					memset(&bq, 0, sizeof(bq));
					bq.id = (uint32_t)v[i].value;
					rc = kreq(KMS_OP_GET_BLOB, &bq, sizeof(bq), NULL, 0, blob, sizeof(blob), &r);
					kt("info in_formats rc=%d len=%u version=%u nfmt=%u fmt0=%.4s nmod=%u", rc, r.u.blob.length,
						((uint32_t *)blob)[0], ((uint32_t *)blob)[2], (const char *)&((uint32_t *)blob)[6],
						((uint32_t *)blob)[4]);
				}
			}
		}
	}
	kt("info result fails=%u verdict=%s", fails, (fails == 0u) ? "PASS" : "FAIL");
	return (fails == 0u) ? 0 : 1;
}


/* ========================================================================= */
/* pool                                                                       */
/* ========================================================================= */

static int cmd_pool(void)
{
	kms_create_dumb_req_t cd = { .width = 256, .height = 256, .bpp = 32, .flags = KMS_DUMB_POOL };
	kms_handle_req_t hq;
	kms_checksum_req_t cs;
	kms_resp_t r;
	kms_dumb_resp_t d;
	volatile uint32_t *p;
	uint32_t i, words, local, fails = 0u;
	int rc, err, cached_refused = 0, gone = 0;
	void *cp;
	uint64_t t0;

	t0 = cnt_now();
	rc = kreq(KMS_OP_CREATE_DUMB, &cd, sizeof(cd), NULL, 0, NULL, 0, &r);
	if (rc != 0) {
		kt("pool FAIL create rc=%d", rc);
		kt("pool result fails=1 verdict=FAIL");
		return 1;
	}
	d = r.u.dumb;
	kt("pool create handle=%u pitch=%u size=%llu kind=%u port=%u id=%llu cache=%u us=%llu", d.handle, d.pitch,
		(unsigned long long)d.size, d.mem.kind, d.mem.port, (unsigned long long)d.mem.addr, d.mem.cache,
		(unsigned long long)cnt_us(cnt_now() - t0));
	fails += (d.mem.kind != KMS_MEM_OID);

	p = kmap(&d.mem, 0, &err);
	kt("pool map ok=%d err=%d", p != NULL, err);
	if (p == NULL) {
		fails++;
	}
	else {
		words = (uint32_t)(d.size / 4u);
		/* fresh BO must be zeroed by the server */
		for (i = 0u; (i < words) && (p[i] == 0u); i++) {
		}
		kt("pool zeroed=%d first_nonzero=%u", i == words, i);
		fails += (i != words);
		for (i = 0u; i < words; i++) {
			p[i] = 0x5a000000u ^ (i * 2654435761u);
		}
		__asm__ volatile("dsb sy" ::: "memory");
		local = fnv(p, words);
		memset(&cs, 0, sizeof(cs));
		cs.handle = d.handle;
		cs.len = (uint32_t)d.size;
		rc = kreq(KMS_OP_DBG_BO_CHECKSUM, &cs, sizeof(cs), NULL, 0, NULL, 0, &r);
		kt("pool checksum rc=%d local=0x%08x server=0x%08x match=%d", rc, local, r.u.checksum.sum,
			(rc == 0) && (r.u.checksum.sum == local));
		fails += (rc != 0) || (r.u.checksum.sum != local);

		/* one memory type per export: a cached mapping must be refused */
		cp = kmap(&d.mem, 1, &err);
		cached_refused = (cp == NULL) && (err == -EINVAL);
		if (cp != NULL) {
			(void)munmap(cp, (size_t)d.size);
		}
		kt("pool memtype_refused=%d err=%d", cached_refused, err);
		fails += !cached_refused;
		(void)munmap((void *)p, (size_t)d.size);
	}

	memset(&hq, 0, sizeof(hq));
	hq.handle = d.handle;
	rc = kreq(KMS_OP_DESTROY_DUMB, &hq, sizeof(hq), NULL, 0, NULL, 0, &r);
	cp = kmap(&d.mem, 0, &err);
	gone = (cp == NULL);
	if (cp != NULL) {
		(void)munmap(cp, (size_t)d.size);
	}
	kt("pool destroy rc=%d name_gone=%d err=%d", rc, gone, err);
	fails += (rc != 0) || !gone;
	kt("pool result fails=%u verdict=%s", fails, (fails == 0u) ? "PASS" : "FAIL");
	return (fails == 0u) ? 0 : 1;
}


/* ========================================================================= */
/* flip                                                                       */
/* ========================================================================= */

typedef struct {
	kms_dumb_resp_t d;
	volatile uint32_t *px;
	uint32_t fb;
} buf_t;


static void draw_full(buf_t *b, int variant)
{
	static const uint8_t bars[8][3] = {
		{ 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
		{ 255, 0, 255 }, { 255, 0, 0 }, { 0, 0, 255 }, { 32, 32, 32 }
	};
	uint32_t x, y, stride = b->d.pitch / 4u;
	uint32_t mx0 = (variant == 0) ? 80u : ((k.w > 320u) ? k.w - 320u : 0u);
	uint32_t my0 = (k.h_ > 480u) ? k.h_ / 2u - 120u : 0u;

	for (y = 0u; y < k.h_; y++) {
		volatile uint32_t *row = b->px + (size_t)y * stride;
		for (x = 0u; x < k.w; x++) {
			uint32_t c;
			if ((x >= mx0) && (x < mx0 + 240u) && (y >= my0) && (y < my0 + 240u)) {
				c = ((((x - mx0) / 30u) + ((y - my0) / 30u)) & 1u) ? pack(255, 255, 255) : pack(0, 0, 0);
			}
			else if (((x % 64u) == 0u) || ((y % 64u) == 0u)) {
				c = pack(128, 128, 128);
			}
			else if (y < (k.h_ * 2u) / 3u) {
				const uint8_t *bc = bars[(x * 8u) / k.w];
				c = pack(bc[0], bc[1], bc[2]);
			}
			else {
				uint32_t v = (x * 255u) / k.w;
				c = pack(v, v, v);
			}
			row[x] = c;
		}
	}
	__asm__ volatile("dsb sy" ::: "memory");
}


/* A 64x64 box that moves 8 px per flip along the bottom third (erasing its
 * previous position in this buffer, which is two flips old). */
static void draw_box(buf_t *b, uint32_t pos, uint32_t oldpos)
{
	uint32_t x, y, stride = b->d.pitch / 4u, span = (k.w > 64u) ? k.w - 64u : 1u;
	uint32_t y0 = (k.h_ * 5u) / 6u, x0 = (pos * 8u) % span, ox = (oldpos * 8u) % span;

	if (y0 + 64u > k.h_) {
		return;
	}
	for (y = y0; y < y0 + 64u; y++) {
		volatile uint32_t *row = b->px + (size_t)y * stride;
		for (x = ox; x < ox + 64u; x++) {
			uint32_t v = (x * 255u) / k.w;
			row[x] = pack(v, v, v);
		}
		for (x = x0; x < x0 + 64u; x++) {
			row[x] = pack(255, 0, 255);
		}
	}
	__asm__ volatile("dsb sy" ::: "memory");
}


static int read_event(kms_drm_event_vblank_t *ev, uint64_t *t_after)
{
	uint8_t buf[64];
	ssize_t n;
	int tries;

	for (tries = 0; tries < 4; tries++) {
		n = read(k.fd, buf, sizeof(buf));
		if ((n < 0) && (errno == EAGAIN)) {
			continue;   /* bounded park (KMS_READ_MAX_MS) expired: loop */
		}
		*t_after = cnt_now();
		if (n < (ssize_t)sizeof(*ev)) {
			return (n < 0) ? -errno : -EIO;
		}
		memcpy(ev, buf, sizeof(*ev));
		return (int)n;
	}
	return -ETIMEDOUT;
}


/* With the pan backend a pool BO is mappable but not scannable: a flip to it
 * must be refused (-EINVAL), and the screen must not change. */
static int neg_pool_on_pan(void)
{
	kms_create_dumb_req_t cd = { .width = 256, .height = 256, .bpp = 32, .flags = KMS_DUMB_POOL };
	kms_addfb2_req_t af;
	kms_page_flip_req_t fr;
	kms_handle_req_t hq;
	kms_fb_resp_t fq;
	kms_resp_t r;
	uint32_t handle, fb = 0u;
	int rc, rcf = 0;

	rc = kreq(KMS_OP_CREATE_DUMB, &cd, sizeof(cd), NULL, 0, NULL, 0, &r);
	if (rc != 0) {
		kt("flip result negative_test=pool_on_pan create_rc=%d verdict=FAIL", rc);
		return 1;
	}
	handle = r.u.dumb.handle;
	memset(&af, 0, sizeof(af));
	af.width = 256u;
	af.height = 256u;
	af.format = k.fmt;
	af.handle = handle;
	af.pitch = r.u.dumb.pitch;
	rc = kreq(KMS_OP_ADDFB2, &af, sizeof(af), NULL, 0, NULL, 0, &r);
	if (rc == 0) {
		fb = r.u.fb.fb_id;
		memset(&fr, 0, sizeof(fr));
		fr.crtc_id = k.crtc;
		fr.fb_id = fb;
		fr.flags = KMS_PAGE_FLIP_EVENT;
		rcf = kreq(KMS_OP_PAGE_FLIP, &fr, sizeof(fr), NULL, 0, NULL, 0, &r);
		fq.fb_id = fb;
		fq.pad = 0u;
		(void)kreq(KMS_OP_RMFB, &fq, sizeof(fq), NULL, 0, NULL, 0, &r);
	}
	memset(&hq, 0, sizeof(hq));
	hq.handle = handle;
	(void)kreq(KMS_OP_DESTROY_DUMB, &hq, sizeof(hq), NULL, 0, NULL, 0, &r);
	kt("flip result negative_test=pool_on_pan addfb_rc=%d flip_rc=%d (expect -22) verdict=%s", rc, rcf,
		((rc == 0) && (rcf == -EINVAL)) ? "PASS" : "FAIL");
	return ((rc == 0) && (rcf == -EINVAL)) ? 0 : 1;
}


#define MAXF 4096u
static uint32_t s_lat[MAXF], s_int[MAXF], s_apply[MAXF], s_deliv[MAXF];

static int cmd_flip(uint32_t nflips, uint32_t hold, int force_pool)
{
	kms_create_dumb_req_t cd;
	kms_addfb2_req_t af;
	kms_page_flip_req_t fr;
	kms_resp_t r;
	kms_drm_event_vblank_t ev;
	buf_t b[2];
	uint32_t i, fails = 0u, missed = 0u, ebusy_seen = 0u, errors = 0u, deferred = 0u, nl = 0u, ni = 0u;
	uint32_t hist[6] = { 0u, 0u, 0u, 0u, 0u, 0u }, first_seq = 0u, last_seq = 0u, frame_us;
	int64_t last_ts = 0, ts;
	uint64_t t0, tc, te, tlast_tick, tstart;
	int rc, err, cur = 1;

	if (k.fmt == 0u) {
		kms_obj_req_t q = { .id = k.primary };
		if (kreq(KMS_OP_GET_PLANE, &q, sizeof(q), NULL, 0, NULL, 0, &r) == 0) {
			k.fmt = r.u.plane.formats[0];
		}
	}
	if (force_pool && (k.h.backend == KMS_BACKEND_PAN)) {
		return neg_pool_on_pan();
	}
	frame_us = (k.h.refresh_mhz != 0u) ? (uint32_t)(1000000000ULL / k.h.refresh_mhz) : 16667u;
	memset(b, 0, sizeof(b));
	for (i = 0u; i < 2u; i++) {
		memset(&cd, 0, sizeof(cd));
		cd.width = k.w;
		cd.height = k.h_;
		cd.bpp = 32u;
		cd.flags = force_pool ? KMS_DUMB_POOL : 0u;
		rc = kreq(KMS_OP_CREATE_DUMB, &cd, sizeof(cd), NULL, 0, NULL, 0, &r);
		if (rc != 0) {
			kt("flip FAIL create buf=%u rc=%d", i, rc);
			return 1;
		}
		b[i].d = r.u.dumb;
		b[i].px = kmap(&b[i].d.mem, 0, &err);
		kt("flip buf=%u handle=%u kind=%s addr=0x%llx pitch=%u size=%llu map=%d err=%d", i, b[i].d.handle,
			(b[i].d.mem.kind == KMS_MEM_PHYS) ? "phys" : "oid", (unsigned long long)b[i].d.mem.addr, b[i].d.pitch,
			(unsigned long long)b[i].d.size, b[i].px != NULL, err);
		if (b[i].px == NULL) {
			return 1;
		}
		t0 = cnt_now();
		draw_full(&b[i], (int)i);
		kt("flip draw buf=%u us=%llu", i, (unsigned long long)cnt_us(cnt_now() - t0));
		memset(&af, 0, sizeof(af));
		af.width = k.w;
		af.height = k.h_;
		af.format = k.fmt;
		af.handle = b[i].d.handle;
		af.pitch = b[i].d.pitch;
		rc = kreq(KMS_OP_ADDFB2, &af, sizeof(af), NULL, 0, NULL, 0, &r);
		if (rc != 0) {
			kt("flip FAIL addfb2 buf=%u rc=%d fmt=%.4s", i, rc, (const char *)&k.fmt);
			return 1;
		}
		b[i].fb = r.u.fb.fb_id;
	}

	/* TEST_ONLY must validate without touching the screen */
	{
		kms_atomic_plane_t st;
		kms_atomic_req_t ar;
		memset(&st, 0, sizeof(st));
		st.plane_id = k.primary;
		st.fb_id = b[0].fb;
		st.crtc_id = k.crtc;
		st.crtc_w = k.w;
		st.crtc_h = k.h_;
		st.src_w = k.w << 16;
		st.src_h = k.h_ << 16;
		st.alpha = 0xffffu;
		st.rotation = 1u;
		memset(&ar, 0, sizeof(ar));
		ar.flags = KMS_ATOMIC_TEST_ONLY;
		ar.nplanes = 1u;
		ar.crtc_id = k.crtc;
		ar.active = 1u;
		rc = kreq(KMS_OP_ATOMIC, &ar, sizeof(ar), &st, sizeof(st), NULL, 0, &r);
		kt("flip test_only rc=%d (expect 0)", rc);
		fails += (rc != 0);
	}

	/* One commit in flight per CRTC: a second flip before the first completes must
	 * get -EBUSY. A vblank can land between the two requests (then both are
	 * accepted): drain both events and retry, up to 3 times. */
	{
		int attempt, rc2 = 0;
		for (attempt = 0; attempt < 3; attempt++) {
			kms_page_flip_req_t f2;
			kms_resp_t r2;
			memset(&fr, 0, sizeof(fr));
			fr.crtc_id = k.crtc;
			fr.fb_id = b[1].fb;
			fr.flags = KMS_PAGE_FLIP_EVENT;
			fr.user_data = 0xeb00u;
			rc = kreq(KMS_OP_PAGE_FLIP, &fr, sizeof(fr), NULL, 0, NULL, 0, &r);
			f2 = fr;
			f2.fb_id = b[0].fb;
			f2.user_data = 0xeb01u;
			rc2 = (rc == 0) ? kreq(KMS_OP_PAGE_FLIP, &f2, sizeof(f2), NULL, 0, NULL, 0, &r2) : rc;
			if ((rc != 0) || (read_event(&ev, &te) < 0)) {
				break;
			}
			cur = 0;   /* b[1] is on screen: the loop starts by flipping to b[0] */
			if (rc2 == -EBUSY) {
				ebusy_seen = 1u;
				break;
			}
			if (rc2 == 0) {
				(void)read_event(&ev, &te);   /* raced a vblank: b[0] is on screen now */
				cur = 1;
				continue;
			}
			break;
		}
		kt("flip ebusy_check rc=%d second_rc=%d attempts=%d ebusy=%u", rc, rc2, attempt + 1, ebusy_seen);
	}

	kt("flip start n=%u frame_us=%u fmt=%.4s backend=%s", nflips, frame_us, (const char *)&k.fmt,
		(k.h.backend == KMS_BACKEND_PLANE) ? "plane" : "pan");
	tstart = tlast_tick = cnt_now();
	for (i = 0u; i < nflips; i++) {
		memset(&fr, 0, sizeof(fr));
		fr.crtc_id = k.crtc;
		fr.fb_id = b[cur].fb;
		fr.flags = KMS_PAGE_FLIP_EVENT;
		fr.user_data = 0x4b4d5300u + i;
		tc = cnt_now();
		rc = kreq(KMS_OP_PAGE_FLIP, &fr, sizeof(fr), NULL, 0, NULL, 0, &r);
		if (rc != 0) {
			errors++;
			if (errors <= 3u) {
				kt("flip error i=%u rc=%d", i, rc);
			}
			if (errors > 20u) {
				break;
			}
			continue;
		}
		if (r.u.flip.applied) {
			if (nl < MAXF) {
				s_apply[nl] = r.u.flip.apply_us;
			}
		}
		else {
			deferred++;
		}
		rc = read_event(&ev, &te);
		if ((rc < 0) || (ev.base.type != KMS_DRM_EVENT_FLIP_COMPLETE) || (ev.user_data != fr.user_data)) {
			errors++;
			if (errors <= 3u) {
				kt("flip event_bad i=%u rc=%d type=%u len=%u user=0x%llx", i, rc, ev.base.type, ev.base.length,
					(unsigned long long)ev.user_data);
			}
			if (rc < 0) {
				break;
			}
			continue;
		}
		ts = (int64_t)ev.tv_sec * 1000000000LL + (int64_t)ev.tv_usec * 1000LL;
		if (nl < MAXF) {
			s_lat[nl] = (uint32_t)cnt_us(te - tc);
			s_deliv[nl] = (uint32_t)((mono_ns() - ts) / 1000);
			nl++;
		}
		if (i == 0u) {
			first_seq = ev.sequence;
		}
		else {
			uint32_t d = ev.sequence - last_seq;
			uint32_t iv = (uint32_t)((ts - last_ts) / 1000);
			if (d > 1u) {
				missed += d - 1u;
			}
			if (ni < MAXF) {
				s_int[ni++] = iv;
			}
			/* buckets in frames: <0.75, 0.75-1.25 (on time), 1.25-1.75, 1.75-2.25, 2.25-3.5, >3.5 */
			hist[(iv * 4u < frame_us * 3u) ? 0 : (iv * 4u < frame_us * 5u) ? 1 : (iv * 4u < frame_us * 7u) ? 2 :
				(iv * 4u < frame_us * 9u) ? 3 : (iv * 2u < frame_us * 7u) ? 4 : 5]++;
		}
		last_seq = ev.sequence;
		last_ts = ts;
		cur ^= 1;
		draw_box(&b[cur], i + 1u, (i >= 1u) ? i - 1u : 0u);   /* the buffer just released */
		if (cnt_us(cnt_now() - tlast_tick) >= 2000000u) {
			tlast_tick = cnt_now();
			kt("tick flip i=%u missed=%u errors=%u seq=%u", i, missed, errors, last_seq);
		}
	}
	te = cnt_now() - tstart;
	{
		uint32_t done = nl, fps_x100 = (uint32_t)(((uint64_t)done * 100u * 1000000u) / (cnt_us(te) ? cnt_us(te) : 1u));
		uint32_t l50 = pctl(s_lat, nl, 50), l99 = pctl(s_lat, nl, 99), lmax = pctl(s_lat, nl, 100);
		uint32_t i50 = pctl(s_int, ni, 50), i1 = pctl(s_int, ni, 1), i99 = pctl(s_int, ni, 99), imax = pctl(s_int, ni, 100);
		uint32_t a50 = pctl(s_apply, (nl < MAXF) ? nl : MAXF, 50), a99 = pctl(s_apply, (nl < MAXF) ? nl : MAXF, 99);
		uint32_t d50 = pctl(s_deliv, nl, 50), d99 = pctl(s_deliv, nl, 99);
		uint32_t ref_x100 = k.h.refresh_mhz / 10u;
		int rate_ok = (ref_x100 == 0u) || ((fps_x100 * 100u >= ref_x100 * 97u) && (fps_x100 * 100u <= ref_x100 * 103u));

		kt("flip hist frames<0.75=%u on_time=%u 1.5=%u 2=%u 3=%u >3.5=%u", hist[0], hist[1], hist[2], hist[3], hist[4],
			hist[5]);
		kt("flip latency commit_to_event_us p50=%u p99=%u max=%u apply_us p50=%u p99=%u event_delivery_us p50=%u p99=%u",
			l50, l99, lmax, a50, a99, d50, d99);
		kt("flip interval_us p1=%u p50=%u p99=%u max=%u frame_us=%u", i1, i50, i99, imax, frame_us);
		fails += (done != nflips) || (errors != 0u) || (!ebusy_seen && (nflips > 2u)) ||
			((uint64_t)missed * 100u > (uint64_t)nflips) || !rate_ok;
		kt("flip result flips=%u/%u fps=%u.%02u refresh_mhz=%u seq=%u..%u missed=%u errors=%u deferred=%u ebusy=%d "
		   "rate_ok=%d verdict=%s",
			done, nflips, fps_x100 / 100u, fps_x100 % 100u, k.h.refresh_mhz, first_seq, last_seq, missed, errors, deferred,
			ebusy_seen, rate_ok, (fails == 0u) ? "PASS" : "FAIL");
	}
	for (i = 0u; i < hold; i += 2u) {
		kt("tick hold s=%u/%u", i, hold);
		sleep((hold - i >= 2u) ? 2u : 1u);
	}
	/* Back to the console: SET_CRTC fb 0 (closing the fd would do it too). */
	{
		kms_set_crtc_req_t sc;
		kms_handle_req_t hq;
		memset(&sc, 0, sizeof(sc));
		sc.crtc_id = k.crtc;
		rc = kreq(KMS_OP_SET_CRTC, &sc, sizeof(sc), NULL, 0, NULL, 0, &r);
		usleep(100000);
		kt("flip restore set_crtc0 rc=%d", rc);
		for (i = 0u; i < 2u; i++) {
			kms_fb_resp_t fq = { .fb_id = b[i].fb };
			(void)kreq(KMS_OP_RMFB, &fq, sizeof(fq), NULL, 0, NULL, 0, &r);
			(void)munmap((void *)b[i].px, (size_t)b[i].d.size);
			memset(&hq, 0, sizeof(hq));
			hq.handle = b[i].d.handle;
			(void)kreq(KMS_OP_DESTROY_DUMB, &hq, sizeof(hq), NULL, 0, NULL, 0, &r);
		}
	}
	return (fails == 0u) ? 0 : 1;
}


/* ========================================================================= */
/* vblank                                                                     */
/* ========================================================================= */

static int cmd_vblank(void)
{
	kms_vblank_req_t vq;
	kms_resp_t r;
	kms_drm_event_vblank_t ev;
	uint32_t i, n = 0u, errs = 0u, iv[64], evok = 0u;
	int64_t last = 0;
	uint64_t te;
	int rc;

	for (i = 0u; i < 60u; i++) {
		memset(&vq, 0, sizeof(vq));
		vq.crtc_id = k.crtc;
		vq.type = KMS_VBL_RELATIVE;
		vq.sequence = 1u;
		rc = kreq(KMS_OP_WAIT_VBLANK, &vq, sizeof(vq), NULL, 0, NULL, 0, &r);
		if (rc != 0) {
			errs++;
			continue;
		}
		if ((last != 0) && (n < 64u)) {
			iv[n++] = (uint32_t)((r.u.vblank.time_ns - last) / 1000);
		}
		last = r.u.vblank.time_ns;
	}
	kt("vblank wait n=%u errs=%u interval_us p1=%u p50=%u p99=%u", n, errs, pctl(iv, n, 1), pctl(iv, n, 50),
		pctl(iv, n, 99));
	for (i = 0u; i < 10u; i++) {
		memset(&vq, 0, sizeof(vq));
		vq.crtc_id = k.crtc;
		vq.type = 1u | 2u;   /* RELATIVE | NEXT_ON_MISS */
		vq.sequence = 2u;
		vq.user_data = 0x5e0000u + i;
		rc = kreq(KMS_OP_CRTC_QUEUE_SEQUENCE, &vq, sizeof(vq), NULL, 0, NULL, 0, &r);
		if ((rc == 0) && (read_event(&ev, &te) > 0) && (ev.base.type == KMS_DRM_EVENT_CRTC_SEQUENCE) &&
				(ev.user_data == vq.user_data)   /* user_data is at offset 8 in both layouts */) {
			evok++;
		}
	}
	kt("vblank queue_sequence events=%u/10", evok);
	kt("vblank result verdict=%s", ((errs == 0u) && (n >= 50u) && (evok == 10u)) ? "PASS" : "FAIL");
	return ((errs == 0u) && (evok == 10u)) ? 0 : 1;
}


static int cmd_stats(void)
{
	kms_resp_t r;
	kms_stats_t *s = &r.u.stats;
	int rc = kreq(KMS_OP_DBG_STATS, NULL, 0, NULL, 0, NULL, 0, &r);

	kt("stats rc=%d vblanks=%u src=%u irq=%u spurious=%u applied=%u completed=%u fence_deferred=%u apply_us_max=%u "
	   "apply_errors=%u events=%u dropped=%u parked_max=%u bos=%u exports=%u pool_free_kib=%u",
		rc, s->vblanks, s->vblank_src, s->irq_count, s->irq_spurious, s->flips_applied, s->flips_completed,
		s->flips_fence_deferred, s->apply_us_max, s->apply_errors, s->events_queued, s->events_dropped, s->reads_parked_max, s->bos_live,
		s->exports_live, s->pool_free_kib);
	return rc;
}


int main(int argc, char **argv)
{
	uint32_t nflips = 600u, hold = 0u;
	int c, i, fails = 0, force_pool = 0;
	uint64_t f;

	setvbuf(stdout, NULL, _IOLBF, 0);
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	cnt_hz = (f != 0u) ? f : 54000000u;

	while ((c = getopt(argc, argv, "n:H:p")) != -1) {
		switch (c) {
			case 'n': nflips = (uint32_t)strtoul(optarg, NULL, 0); break;
			case 'H': hold = (uint32_t)strtoul(optarg, NULL, 0); break;
			case 'p': force_pool = 1; break;
			default:
				printf("usage: %s [-n flips] [-H hold_s] [-p] info|pool|flip|vblank|stats|quit|all ...\n", argv[0]);
				return 1;
		}
	}
	if (nflips > MAXF) {
		nflips = MAXF;
	}
	if (optind >= argc) {
		printf("usage: %s [-n flips] [-H hold_s] [-p] info|pool|flip|vblank|stats|quit|all ...\n", argv[0]);
		return 1;
	}
	if (kconnect() != 0) {
		kt("done fails=1");
		return 1;
	}
	for (i = optind; i < argc; i++) {
		const char *cmd = argv[i];
		kt("cmd %s start", cmd);
		if (strcmp(cmd, "info") == 0) {
			fails += cmd_info();
		}
		else if (strcmp(cmd, "pool") == 0) {
			fails += cmd_pool();
		}
		else if (strcmp(cmd, "flip") == 0) {
			fails += cmd_flip(nflips, hold, force_pool);
		}
		else if (strcmp(cmd, "vblank") == 0) {
			fails += cmd_vblank();
		}
		else if (strcmp(cmd, "stats") == 0) {
			fails += (cmd_stats() != 0);
		}
		else if (strcmp(cmd, "all") == 0) {
			fails += cmd_info();
			fails += cmd_pool();
			fails += cmd_flip(nflips, hold, force_pool);
			fails += cmd_vblank();
			fails += (cmd_stats() != 0);
		}
		else if (strcmp(cmd, "quit") == 0) {
			kms_resp_t r;
			int rc = kreq(KMS_OP_DBG_QUIT, NULL, 0, NULL, 0, NULL, 0, &r);
			kt("quit rc=%d", rc);
			fails += (rc != 0);
		}
		else if (strcmp(cmd, "&") != 0) {
			kt("unknown command %s", cmd);
			fails++;
		}
	}
	kt("done fails=%d", fails);
	close(k.fd);
	return (fails == 0) ? 0 : 1;
}
