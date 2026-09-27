/*
 * Phoenix-RTOS
 *
 * drmprobe - end-to-end probe of libdrm-phoenix against rpi4-kms + rpi4-v3d-async
 *
 * Uses only the upstream libdrm API (xf86drm.h, xf86drmMode.h) and DRM ioctls,
 * as a Linux DRM program would; buffers are mapped with plain mmap(), which the
 * link routes through libdrm-phoenix (-Wl,--wrap=mmap). Every result is one
 * tagged line `DRMPROBE <test> ... ok=0|1` (or `gap=1`: a documented server gap
 * answered as predicted), and the run ends with `DRMPROBE RESULT ... verdict=`.
 *
 * Needs: rpi4-v3d-async running, then rpi4-kms (started with -G for the in-fence
 * test). Neither server may share the hardware with an old-lane GPU app.
 *
 * Usage: drmprobe [-n flips] [-k] (-k: keep the last frame on screen at exit)
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
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "v3d_drm.h"
#include "v3da_clgen.h"


extern int sys_fdpath(int fd, char *buf, size_t size);

#define TAG "DRMPROBE "

static struct {
	int pass, fail, gap;
	char failed[512];
	int card, render, card1;
	uint32_t crtc, conn, primary;
	drmModeModeInfo mode;
	uint32_t fmt;
	struct {
		uint32_t handle, pitch, fb;
		uint64_t size;
		uint32_t *px;
	} buf[2];
	int flips_done;
	uint64_t last_flip_seq;
	uint64_t last_flip_us;
	uint32_t last_flip_crtc;
} P = { .card = -1, .render = -1, .card1 = -1 };


static uint64_t now_us(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}


static void verdict(const char *key, int ok)
{
	if (ok) {
		P.pass++;
	}
	else {
		P.fail++;
		if (strlen(P.failed) + strlen(key) + 2 < sizeof(P.failed)) {
			strcat(P.failed, key);
			strcat(P.failed, ",");
		}
	}
}


/* A result predicted to hit a documented server gap: counts as `gap`, not fail,
 * when the error is the predicted one. */
static void gapcheck(const char *key, int rc_errno, int expect)
{
	if (rc_errno == expect) {
		P.gap++;
	}
	else if (rc_errno == 0) {
		P.pass++;   /* the gap was closed meanwhile */
	}
	else {
		verdict(key, 0);
	}
}


/* ========================================================================= */
/* 1. identity                                                                */
/* ========================================================================= */

static const char *node_or_dash(drmDevicePtr d, int t)
{
	return ((d->available_nodes & (1 << t)) != 0) ? d->nodes[t] : "-";
}


static void t_devices(char *card_path, char *render_path, char *card1_path, size_t n)
{
	drmDevicePtr devs[8];
	int cnt, i, cnt0;

	card_path[0] = render_path[0] = card1_path[0] = '\0';
	cnt0 = drmGetDevices2(0, NULL, 0);
	cnt = drmGetDevices2(0, devs, 8);
	printf(TAG "devices count_null=%d n=%d ok=%d\n", cnt0, cnt, (cnt == 2) && (cnt0 == 2));
	verdict("devices", (cnt == 2) && (cnt0 == 2));
	for (i = 0; i < cnt; i++) {
		drmDevicePtr d = devs[i];
		const char *compat = ((d->bustype == DRM_BUS_PLATFORM) && (d->deviceinfo.platform->compatible != NULL) &&
			(d->deviceinfo.platform->compatible[0] != NULL)) ? d->deviceinfo.platform->compatible[0] : "-";
		printf(TAG "device i=%d bus=%s fullname=%s compat=%s primary=%s render=%s\n", i,
			(d->bustype == DRM_BUS_PLATFORM) ? "platform" : "other",
			(d->bustype == DRM_BUS_PLATFORM) ? d->businfo.platform->fullname : "-", compat,
			node_or_dash(d, DRM_NODE_PRIMARY), node_or_dash(d, DRM_NODE_RENDER));
		if ((d->available_nodes & (1 << DRM_NODE_RENDER)) != 0) {
			snprintf(render_path, n, "%s", d->nodes[DRM_NODE_RENDER]);
			if ((d->available_nodes & (1 << DRM_NODE_PRIMARY)) != 0) {
				snprintf(card1_path, n, "%s", d->nodes[DRM_NODE_PRIMARY]);   /* the v3d device's primary node */
			}
		}
		else if ((d->available_nodes & (1 << DRM_NODE_PRIMARY)) != 0) {
			snprintf(card_path, n, "%s", d->nodes[DRM_NODE_PRIMARY]);
		}
	}
	drmFreeDevices(devs, (cnt > 0) ? cnt : 0);
}


/* Open exactly as Mesa does (O_RDWR | O_CLOEXEC); report, then fall back. */
static int t_open(const char *what, const char *path)
{
	int fd, rdwr = 1, e = 0;

	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		e = errno;
		rdwr = 0;
		fd = open(path, O_RDONLY | O_CLOEXEC);
	}
	printf(TAG "open node=%s path=%s rdwr=%d rdwr_errno=%d fd=%d ok=%d\n", what, path, rdwr, e, fd, rdwr && (fd >= 0));
	verdict(what, rdwr && (fd >= 0));
	return fd;
}


static void t_identity(const char *what, int fd, const char *want_name, int want_type, int want_kms)
{
	drmVersionPtr v = drmGetVersion(fd);
	drmDevicePtr d = NULL;
	char *n2 = drmGetDeviceNameFromFd2(fd), *rn = drmGetRenderDeviceNameFromFd(fd),
		*pn = drmGetPrimaryDeviceNameFromFd(fd);
	int type = drmGetNodeTypeFromFd(fd), kms = drmIsKMS(fd), dev_rc = drmGetDevice2(fd, 0, &d), ok;

	ok = (v != NULL) && (strcmp(v->name, want_name) == 0) && (type == want_type) && (kms == want_kms) && (dev_rc == 0) &&
		(n2 != NULL);
	printf(TAG "identity node=%s version=%s %d.%d.%d date=%s desc=\"%s\" node_type=%d is_kms=%d name2=%s "
		"primary_name=%s render_name=%s get_device=%d dev_primary=%s dev_render=%s ok=%d\n", what,
		(v != NULL) ? v->name : "-", (v != NULL) ? v->version_major : -1, (v != NULL) ? v->version_minor : -1,
		(v != NULL) ? v->version_patchlevel : -1, (v != NULL) ? v->date : "-", (v != NULL) ? v->desc : "-", type, kms,
		(n2 != NULL) ? n2 : "-", (pn != NULL) ? pn : "-", (rn != NULL) ? rn : "-", dev_rc,
		(d != NULL) ? node_or_dash(d, DRM_NODE_PRIMARY) : "-", (d != NULL) ? node_or_dash(d, DRM_NODE_RENDER) : "-", ok);
	verdict(what, ok);
	drmFreeVersion(v);
	drmFreeDevice(&d);
	free(n2);
	free(rn);
	free(pn);
}


/* G2 + G10 (M3 part 2): fstat() on every node - Mesa's gbm_create_device()
 * needs S_ISCHR, v3dv compares the primary and render st_rdev - and the dev_t
 * maps back to the right device and node type through libdrm. */
static void t_fstat(void)
{
	const struct {
		const char *what;
		int fd;
		int type;
	} n[3] = { { "card0", P.card, DRM_NODE_PRIMARY }, { "card1", P.card1, DRM_NODE_PRIMARY },
		{ "render", P.render, DRM_NODE_RENDER } };
	struct stat st[3];
	int rc[3] = { -1, -1, -1 }, i, nopen = 0, nok = 0, nsys = 0, chr_all = 1, types_ok = 1, distinct, ok;

	memset(st, 0, sizeof(st));
	for (i = 0; i < 3; i++) {
		drmDevicePtr d = NULL;
		int e, t = -1, drc = -1;
		if (n[i].fd < 0) {
			continue;
		}
		nopen++;
		rc[i] = fstat(n[i].fd, &st[i]);
		e = (rc[i] != 0) ? errno : 0;
		if (rc[i] == 0) {
			nok++;
			chr_all &= S_ISCHR(st[i].st_mode) ? 1 : 0;
			t = drmGetNodeTypeFromDevId(st[i].st_rdev);
			drc = drmGetDeviceFromDevId(st[i].st_rdev, 0, &d);
			types_ok &= ((t == n[i].type) && (drc == 0)) ? 1 : 0;
		}
		else {
			nsys += (e == ENOSYS) ? 1 : 0;
		}
		printf(TAG "fstat node=%s fd=%d rc=%d errno=%d mode=0%o chr=%d rdev=%llu devid_type=%d devid_dev=%d\n", n[i].what,
			n[i].fd, rc[i], e, (unsigned)st[i].st_mode, (rc[i] == 0) ? (S_ISCHR(st[i].st_mode) ? 1 : 0) : 0,
			(unsigned long long)st[i].st_rdev, t, drc);
		drmFreeDevice(&d);
	}
	/* card0 != render always; card1 (when its own name exists) differs from both */
	distinct = (rc[0] == 0) && (rc[2] == 0) && (st[0].st_rdev != st[2].st_rdev) &&
		((P.card1 < 0) || ((rc[1] == 0) && (st[1].st_rdev != st[2].st_rdev) && (st[1].st_rdev != st[0].st_rdev)));
	ok = (nok == nopen) && chr_all && types_ok && distinct;
	printf(TAG "fstat_nodes n=%d answered=%d chr_all=%d distinct=%d devid_ok=%d ok=%d gap=%d\n", nopen, nok, chr_all,
		distinct, types_ok, ok, (nsys == nopen) ? 1 : 0);
	if ((nsys == nopen) && (nopen > 0)) {
		P.gap++;   /* every server answered -ENOSYS: servers from before M3 part 2 (G2) */
	}
	else {
		verdict("fstat", ok);
	}
}


/* ========================================================================= */
/* 2. KMS enumeration                                                         */
/* ========================================================================= */

static const char *prop_name_of(int fd, uint32_t prop, char *buf, size_t n)
{
	drmModePropertyPtr p = drmModeGetProperty(fd, prop);

	snprintf(buf, n, "%s", (p != NULL) ? p->name : "?");
	drmModeFreeProperty(p);
	return buf;
}


static int t_kms_enum(void)
{
	drmModeResPtr res;
	drmModeConnectorPtr conn = NULL;
	drmModeEncoderPtr enc = NULL;
	drmModeCrtcPtr crtc = NULL;
	drmModePlaneResPtr pres;
	drmModeObjectPropertiesPtr props;
	uint64_t cap_dumb = 0, cap_prime = 0, cap_mono = 0, cap_invbl = 0, cap_async = 9;
	int rc_u, rc_a, ok, i, j, nprops_total = 0, enum_ok = 1;
	char nb[40];

	rc_u = drmSetClientCap(P.card, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	rc_a = drmSetClientCap(P.card, DRM_CLIENT_CAP_ATOMIC, 1);
	(void)drmGetCap(P.card, DRM_CAP_DUMB_BUFFER, &cap_dumb);
	(void)drmGetCap(P.card, DRM_CAP_PRIME, &cap_prime);
	(void)drmGetCap(P.card, DRM_CAP_TIMESTAMP_MONOTONIC, &cap_mono);
	(void)drmGetCap(P.card, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap_invbl);
	(void)drmGetCap(P.card, DRM_CAP_ASYNC_PAGE_FLIP, &cap_async);
	ok = (rc_u == 0) && (rc_a == 0) && (cap_dumb == 1u) && (cap_prime == 3u) && (cap_mono == 1u);
	printf(TAG "caps universal=%d atomic=%d dumb=%llu prime=%llu monotonic=%llu crtc_in_vblank=%llu async_flip=%llu ok=%d\n",
		rc_u, rc_a, (unsigned long long)cap_dumb, (unsigned long long)cap_prime, (unsigned long long)cap_mono,
		(unsigned long long)cap_invbl, (unsigned long long)cap_async, ok);
	verdict("caps", ok);

	res = drmModeGetResources(P.card);
	if (res == NULL) {
		printf(TAG "resources errno=%d ok=0\n", errno);
		verdict("resources", 0);
		return -1;
	}
	printf(TAG "resources crtcs=%d connectors=%d encoders=%d fbs=%d min=%ux%u max=%ux%u ok=%d\n", res->count_crtcs,
		res->count_connectors, res->count_encoders, res->count_fbs, res->min_width, res->min_height, res->max_width,
		res->max_height, (res->count_crtcs >= 1) && (res->count_connectors >= 1));
	verdict("resources", (res->count_crtcs >= 1) && (res->count_connectors >= 1));

	if (res->count_connectors >= 1) {
		conn = drmModeGetConnector(P.card, res->connectors[0]);
	}
	ok = (conn != NULL) && (conn->count_modes >= 1) && (conn->connection == DRM_MODE_CONNECTED);
	if (conn != NULL) {
		printf(TAG "connector id=0x%x type=%u type_id=%u connection=%u mm=%ux%u modes=%d mode0=%s %ux%u@%u clock=%u "
			"encoder=0x%x props=%d ok=%d\n", conn->connector_id, conn->connector_type, conn->connector_type_id,
			conn->connection, conn->mmWidth, conn->mmHeight, conn->count_modes,
			(conn->count_modes > 0) ? conn->modes[0].name : "-", (conn->count_modes > 0) ? conn->modes[0].hdisplay : 0,
			(conn->count_modes > 0) ? conn->modes[0].vdisplay : 0, (conn->count_modes > 0) ? conn->modes[0].vrefresh : 0,
			(conn->count_modes > 0) ? conn->modes[0].clock : 0, conn->encoder_id, conn->count_props, ok);
	}
	else {
		printf(TAG "connector errno=%d ok=0\n", errno);
	}
	verdict("connector", ok);
	if (!ok) {
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return -1;
	}
	P.conn = conn->connector_id;
	P.mode = conn->modes[0];

	enc = drmModeGetEncoder(P.card, conn->encoder_id);
	ok = (enc != NULL) && (enc->crtc_id != 0u);
	printf(TAG "encoder id=0x%x type=%u crtc=0x%x possible_crtcs=0x%x ok=%d\n", conn->encoder_id,
		(enc != NULL) ? enc->encoder_type : 0u, (enc != NULL) ? enc->crtc_id : 0u,
		(enc != NULL) ? enc->possible_crtcs : 0u, ok);
	verdict("encoder", ok);
	P.crtc = (enc != NULL) ? enc->crtc_id : res->crtcs[0];

	crtc = drmModeGetCrtc(P.card, P.crtc);
	ok = (crtc != NULL) && (crtc->mode_valid != 0) && (crtc->mode.hdisplay == P.mode.hdisplay);
	printf(TAG "crtc id=0x%x fb=%u mode_valid=%d mode=%ux%u@%u gamma=%d ok=%d\n", P.crtc,
		(crtc != NULL) ? crtc->buffer_id : 0u, (crtc != NULL) ? crtc->mode_valid : 0,
		(crtc != NULL) ? crtc->mode.hdisplay : 0, (crtc != NULL) ? crtc->mode.vdisplay : 0,
		(crtc != NULL) ? crtc->mode.vrefresh : 0, (crtc != NULL) ? crtc->gamma_size : -1, ok);
	verdict("crtc", ok);

	pres = drmModeGetPlaneResources(P.card);
	ok = (pres != NULL) && (pres->count_planes >= 1);
	printf(TAG "planes n=%d ok=%d\n", (pres != NULL) ? (int)pres->count_planes : -1, ok);
	verdict("planes", ok);
	P.fmt = DRM_FORMAT_XRGB8888;
	for (i = 0; (pres != NULL) && (i < (int)pres->count_planes); i++) {
		drmModePlanePtr pl = drmModeGetPlane(P.card, pres->planes[i]);
		uint64_t type = 99;
		props = drmModeObjectGetProperties(P.card, pres->planes[i], DRM_MODE_OBJECT_PLANE);
		for (j = 0; (props != NULL) && (j < (int)props->count_props); j++) {
			drmModePropertyPtr p = drmModeGetProperty(P.card, props->props[j]);
			if (p == NULL) {
				enum_ok = 0;
				continue;
			}
			if (strcmp(p->name, "type") == 0) {
				type = props->prop_values[j];
				if (((p->flags & DRM_MODE_PROP_ENUM) == 0) || (p->count_enums != 3)) {
					enum_ok = 0;
				}
			}
			nprops_total++;
			drmModeFreeProperty(p);
		}
		printf(TAG "plane id=0x%x type=%llu crtc=0x%x fb=%u formats=%u fmt0=0x%08x props=%d\n", pres->planes[i],
			(unsigned long long)type, (pl != NULL) ? pl->crtc_id : 0u, (pl != NULL) ? pl->fb_id : 0u,
			(pl != NULL) ? pl->count_formats : 0u, ((pl != NULL) && (pl->count_formats > 0)) ? pl->formats[0] : 0u,
			(props != NULL) ? (int)props->count_props : -1);
		if (type == DRM_PLANE_TYPE_PRIMARY) {
			int has_xrgb = 0;
			P.primary = pres->planes[i];
			for (j = 0; (pl != NULL) && (j < (int)pl->count_formats); j++) {
				has_xrgb |= (pl->formats[j] == DRM_FORMAT_XRGB8888);
			}
			if (!has_xrgb && (pl != NULL) && (pl->count_formats > 0)) {
				P.fmt = pl->formats[0];   /* pan backend: the firmware fb's own format */
			}
		}
		drmModeFreeObjectProperties(props);
		drmModeFreePlane(pl);
	}
	props = drmModeObjectGetProperties(P.card, P.crtc, DRM_MODE_OBJECT_CRTC);
	for (j = 0; (props != NULL) && (j < (int)props->count_props); j++) {
		printf(TAG "crtc_prop %s=%llu\n", prop_name_of(P.card, props->props[j], nb, sizeof(nb)),
			(unsigned long long)props->prop_values[j]);
	}
	drmModeFreeObjectProperties(props);
	ok = (P.primary != 0u) && enum_ok && (nprops_total > 0);
	printf(TAG "properties primary=0x%x total=%d enum_ok=%d fmt=0x%08x ok=%d\n", P.primary, nprops_total, enum_ok, P.fmt,
		ok);
	verdict("properties", ok);

	drmModeFreePlaneResources(pres);
	drmModeFreeCrtc(crtc);
	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);
	return 0;
}


/* ========================================================================= */
/* 3. dumb buffers, flips, events                                             */
/* ========================================================================= */

static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b)
{
	if ((P.fmt == DRM_FORMAT_XBGR8888) || (P.fmt == DRM_FORMAT_ABGR8888)) {
		return 0xff000000u | (b << 16) | (g << 8) | r;
	}
	return 0xff000000u | (r << 16) | (g << 8) | b;
}


/* Buffer i: a background colour per buffer (drawn once, full frame) and a white
 * 128x128 square whose position follows the frame number k. Per frame only the
 * square moves (erase + draw): a full 1080p redraw into uncached memory costs
 * 10-60 ms (M2 section 13) and would pace the flip test instead of the vblank. */
static uint32_t sq_x[2] = { ~0u, ~0u };

static void fill(int i, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t colour)
{
	uint32_t stride = P.buf[i].pitch / 4u, x, y;
	volatile uint32_t *px = P.buf[i].px;

	for (y = y0; y < y0 + h; y++) {
		for (x = x0; x < x0 + w; x++) {
			px[y * stride + x] = colour;
		}
	}
}


static uint32_t bg_of(int i)
{
	return (i == 0) ? rgb(0, 96, 128) : rgb(128, 0, 96);
}


static void draw(int i, int k)
{
	uint32_t w = P.mode.hdisplay, h = P.mode.vdisplay, x0 = (uint32_t)(k * 16) % (w - 128u), y0 = h / 2u - 64u;

	if (sq_x[i] == ~0u) {
		fill(i, 0, 0, w, h, bg_of(i));
	}
	else {
		fill(i, sq_x[i], y0, 128u, 128u, bg_of(i));
	}
	fill(i, x0, y0, 128u, 128u, rgb(255, 255, 255));
	sq_x[i] = x0;
}


static void on_flip(int fd, unsigned int seq, unsigned int sec, unsigned int usec, void *user)
{
	(void)fd;
	(void)sec;
	(void)usec;
	(void)user;
	P.flips_done++;
	P.last_flip_seq = seq;
	P.last_flip_us = now_us();
}


static void on_vblank(int fd, unsigned int seq, unsigned int sec, unsigned int usec, void *user)
{
	on_flip(fd, seq, sec, usec, user);
}


/* Block until `target` flip events arrived (drmHandleEvent read()s the card fd;
 * the server parks the read <= 2 s, then -EAGAIN). Returns 0 or -ETIMEDOUT. */
static int wait_flips(int target, unsigned timeout_ms)
{
	drmEventContext ev;
	uint64_t t0 = now_us();

	memset(&ev, 0, sizeof(ev));
	ev.version = 2;
	ev.page_flip_handler = on_flip;
	ev.vblank_handler = on_vblank;
	while (P.flips_done < target) {
		if ((drmHandleEvent(P.card, &ev) < 0) && (errno != EAGAIN) && (errno != EINTR)) {
			return -errno;
		}
		if ((now_us() - t0) > (uint64_t)timeout_ms * 1000u) {
			return -ETIMEDOUT;
		}
	}
	return 0;
}


static int t_dumb(void)
{
	int i, ok;
	void *p;
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	uint64_t off;

	for (i = 0; i < 2; i++) {
		int rc = drmModeCreateDumbBuffer(P.card, P.mode.hdisplay, P.mode.vdisplay, 32, 0, &P.buf[i].handle,
			&P.buf[i].pitch, &P.buf[i].size);
		int mrc = (rc == 0) ? drmModeMapDumbBuffer(P.card, P.buf[i].handle, &off) : -1;
		p = (mrc == 0) ? mmap(NULL, (size_t)P.buf[i].size, PROT_READ | PROT_WRITE, MAP_SHARED, P.card, (off_t)off) : MAP_FAILED;
		P.buf[i].px = (p != MAP_FAILED) ? p : NULL;
		ok = (rc == 0) && (mrc == 0) && (P.buf[i].px != NULL);
		if (ok) {
			draw(i, 0);
			ok = (P.buf[i].px[0] == bg_of(i));
		}
		handles[0] = P.buf[i].handle;
		pitches[0] = P.buf[i].pitch;
		if (ok) {
			ok = (drmModeAddFB2(P.card, P.mode.hdisplay, P.mode.vdisplay, P.fmt, handles, pitches, offsets,
				&P.buf[i].fb, 0) == 0);
		}
		printf(TAG "dumb i=%d create=%d handle=%u pitch=%u size=%llu map=%d token=0x%llx mmap=%s readback=%d fb=%u ok=%d\n",
			i, rc, P.buf[i].handle, P.buf[i].pitch, (unsigned long long)P.buf[i].size, mrc, (unsigned long long)off,
			(P.buf[i].px != NULL) ? "ok" : strerror(errno), (P.buf[i].px != NULL) ? 1 : 0, P.buf[i].fb, ok);
		verdict((i == 0) ? "dumb0" : "dumb1", ok);
		if (!ok) {
			return -1;
		}
	}
	return 0;
}


static void t_flips(int nflips)
{
	uint64_t t0, t1, seq0;
	int rc, i, missed = 0, ok;
	drmVBlank vb;
	uint64_t cseq = 0, cns = 0;

	t0 = now_us();
	rc = drmModeSetCrtc(P.card, P.crtc, P.buf[0].fb, 0, 0, &P.conn, 1, &P.mode);
	t1 = now_us();
	printf(TAG "setcrtc rc=%d errno=%d blocking_ms=%llu ok=%d\n", rc, (rc != 0) ? errno : 0,
		(unsigned long long)((t1 - t0) / 1000u), rc == 0);
	verdict("setcrtc", rc == 0);
	if (rc != 0) {
		return;
	}

	/* one flip with an event: latency of the event path */
	P.flips_done = 0;
	draw(1, 1);
	t0 = now_us();
	rc = drmModePageFlip(P.card, P.crtc, P.buf[1].fb, DRM_MODE_PAGE_FLIP_EVENT, (void *)(uintptr_t)0x1234);
	if (rc == 0) {
		rc = wait_flips(1, 3000);
	}
	ok = (rc == 0) && (P.flips_done == 1);
	printf(TAG "flip_event rc=%d events=%d seq=%llu latency_us=%llu ok=%d\n", rc, P.flips_done,
		(unsigned long long)P.last_flip_seq, (unsigned long long)(P.last_flip_us - t0), ok);
	verdict("flip_event", ok);
	if (!ok) {
		return;
	}

	/* a run of vsynced flips, each waiting for its event */
	seq0 = P.last_flip_seq;
	P.flips_done = 0;
	t0 = now_us();
	for (i = 0; i < nflips; i++) {
		int b = i & 1;
		draw(b, i + 2);
		rc = drmModePageFlip(P.card, P.crtc, P.buf[b].fb, DRM_MODE_PAGE_FLIP_EVENT, NULL);
		if (rc != 0) {
			break;
		}
		rc = wait_flips(i + 1, 3000);
		if (rc != 0) {
			break;
		}
	}
	t1 = now_us();
	missed = (int)(P.last_flip_seq - seq0) - P.flips_done;
	ok = (rc == 0) && (P.flips_done == nflips);
	printf(TAG "flips n=%d done=%d rc=%d ms=%llu fps_x100=%llu vblanks=%llu missed_vblanks=%d ok=%d\n", nflips,
		P.flips_done, rc, (unsigned long long)((t1 - t0) / 1000u),
		(unsigned long long)((t1 > t0) ? ((uint64_t)P.flips_done * 100000000u / (t1 - t0)) : 0u),
		(unsigned long long)(P.last_flip_seq - seq0), missed, ok);
	verdict("flips", ok);

	/* drmWaitVBlank (relative 1, blocking) and the sequence API */
	memset(&vb, 0, sizeof(vb));
	vb.request.type = DRM_VBLANK_RELATIVE;
	vb.request.sequence = 1;
	t0 = now_us();
	rc = drmWaitVBlank(P.card, &vb);
	t1 = now_us();
	i = drmCrtcGetSequence(P.card, P.crtc, &cseq, &cns);
	ok = (rc == 0) && (i == 0) && (cseq >= vb.reply.sequence) && ((t1 - t0) < 40000u);
	printf(TAG "vblank wait_rc=%d seq=%u waited_us=%llu tv=%ld.%06ld get_seq_rc=%d seq64=%llu ok=%d\n", rc,
		vb.reply.sequence, (unsigned long long)(t1 - t0), vb.reply.tval_sec, vb.reply.tval_usec, i,
		(unsigned long long)cseq, ok);
	verdict("vblank", ok);

	/* poll() readiness on the card fd (quantised to 20 ms until the kernel block_ms change, E5) */
	{
		struct pollfd pfd = { .fd = P.card, .events = POLLIN };
		P.flips_done = 0;
		rc = drmModePageFlip(P.card, P.crtc, P.buf[0].fb, DRM_MODE_PAGE_FLIP_EVENT, NULL);
		t0 = now_us();
		i = (rc == 0) ? poll(&pfd, 1, 1000) : -1;
		t1 = now_us();
		ok = (rc == 0) && (i == 1) && ((pfd.revents & POLLIN) != 0) && (wait_flips(1, 3000) == 0);
		printf(TAG "poll flip_rc=%d poll=%d revents=0x%x wake_us=%llu ok=%d\n", rc, i, pfd.revents,
			(unsigned long long)(t1 - t0), ok);
		verdict("poll", ok);
	}
}


/* Atomic: TEST_ONLY, then a real NONBLOCK commit with an event. */
static void t_atomic(void)
{
	drmModeObjectPropertiesPtr props;
	drmModeAtomicReqPtr req;
	uint32_t fb_prop = 0;
	int j, rc_t, rc_c = -1, ok;
	char nb[40];

	props = drmModeObjectGetProperties(P.card, P.primary, DRM_MODE_OBJECT_PLANE);
	for (j = 0; (props != NULL) && (j < (int)props->count_props); j++) {
		if (strcmp(prop_name_of(P.card, props->props[j], nb, sizeof(nb)), "FB_ID") == 0) {
			fb_prop = props->props[j];
		}
	}
	drmModeFreeObjectProperties(props);

	req = drmModeAtomicAlloc();
	(void)drmModeAtomicAddProperty(req, P.primary, fb_prop, P.buf[1].fb);
	rc_t = drmModeAtomicCommit(P.card, req, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
	if (rc_t == 0) {
		P.flips_done = 0;
		rc_c = drmModeAtomicCommit(P.card, req, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, NULL);
		if (rc_c == 0) {
			rc_c = wait_flips(1, 3000);
		}
	}
	drmModeAtomicFree(req);
	ok = (fb_prop != 0u) && (rc_t == 0) && (rc_c == 0);
	printf(TAG "atomic fb_prop=0x%x test_only=%d commit=%d events=%d ok=%d\n", fb_prop, rc_t, rc_c, P.flips_done, ok);
	verdict("atomic", ok);
}


/* ========================================================================= */
/* 4. render node: params, BOs, a CL clear, syncobjs, sync files              */
/* ========================================================================= */

typedef struct {
	uint32_t handle, offset, size;
	uint32_t *cpu;
} rbo_t;


static int rbo_new(rbo_t *b, uint32_t size)
{
	struct drm_v3d_create_bo cb = { .size = size };
	struct drm_v3d_mmap_bo mb;
	void *p;

	memset(b, 0, sizeof(*b));
	if (drmIoctl(P.render, DRM_IOCTL_V3D_CREATE_BO, &cb) != 0) {
		return -errno;
	}
	b->handle = cb.handle;
	b->offset = cb.offset;
	b->size = size;
	memset(&mb, 0, sizeof(mb));
	mb.handle = cb.handle;
	if (drmIoctl(P.render, DRM_IOCTL_V3D_MMAP_BO, &mb) != 0) {
		return -errno;
	}
	p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, P.render, (off_t)mb.offset);
	if (p == MAP_FAILED) {
		return -errno;
	}
	b->cpu = p;
	return 0;
}


static void rbo_free(rbo_t *b)
{
	struct drm_gem_close gc = { .handle = b->handle };

	if (b->cpu != NULL) {
		(void)munmap(b->cpu, b->size);
	}
	if (b->handle != 0u) {
		(void)drmIoctl(P.render, DRM_IOCTL_GEM_CLOSE, &gc);
	}
	memset(b, 0, sizeof(*b));
}


static void t_render_basics(void)
{
	static const uint32_t params[] = { DRM_V3D_PARAM_V3D_CORE0_IDENT0, DRM_V3D_PARAM_V3D_HUB_IDENT1,
		DRM_V3D_PARAM_SUPPORTS_TFU, DRM_V3D_PARAM_SUPPORTS_CSD, DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH,
		DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT, DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE, DRM_V3D_PARAM_SUPPORTS_PERFMON };
	uint64_t v[8], cap_sync = 9, cap_tl = 9, cap_prime = 9;
	struct drm_v3d_get_param gp;
	struct drm_v3d_get_bo_offset go;
	struct drm_v3d_wait_bo wb;
	rbo_t b;
	unsigned i;
	int rc, ok, wrc;

	for (i = 0; i < 8u; i++) {
		memset(&gp, 0, sizeof(gp));
		gp.param = params[i];
		v[i] = (drmIoctl(P.render, DRM_IOCTL_V3D_GET_PARAM, &gp) == 0) ? gp.value : 0xdeadu;
	}
	ok = (v[0] == 0x04443356u) && (v[2] == 1u) && (v[3] == 1u) && (v[5] == 1u);
	printf(TAG "v3d_params ident0=0x%08llx hub_ident1=0x%08llx tfu=%llu csd=%llu cache_flush=%llu multisync=%llu "
		"cpu_queue=%llu perfmon=%llu ok=%d\n", (unsigned long long)v[0], (unsigned long long)v[1],
		(unsigned long long)v[2], (unsigned long long)v[3], (unsigned long long)v[4], (unsigned long long)v[5],
		(unsigned long long)v[6], (unsigned long long)v[7], ok);
	verdict("v3d_params", ok);

	(void)drmGetCap(P.render, DRM_CAP_SYNCOBJ, &cap_sync);
	(void)drmGetCap(P.render, DRM_CAP_SYNCOBJ_TIMELINE, &cap_tl);
	(void)drmGetCap(P.render, DRM_CAP_PRIME, &cap_prime);
	ok = (cap_sync == 1u) && (cap_tl == 0u);
	printf(TAG "render_caps syncobj=%llu timeline=%llu prime=%llu ok=%d\n", (unsigned long long)cap_sync,
		(unsigned long long)cap_tl, (unsigned long long)cap_prime, ok);
	verdict("render_caps", ok);

	rc = rbo_new(&b, 65536u);
	ok = (rc == 0);
	if (ok) {
		for (i = 0; i < 65536u / 4u; i++) {
			b.cpu[i] = 0xa5a50000u | i;
		}
		ok = (b.cpu[1234] == (0xa5a50000u | 1234u));
		memset(&go, 0, sizeof(go));
		go.handle = b.handle;
		ok = ok && (drmIoctl(P.render, DRM_IOCTL_V3D_GET_BO_OFFSET, &go) == 0) && (go.offset == b.offset);
	}
	memset(&wb, 0, sizeof(wb));
	wb.handle = b.handle;
	wrc = drmIoctl(P.render, DRM_IOCTL_V3D_WAIT_BO, &wb);
	printf(TAG "v3d_bo create=%d handle=0x%x gpuva=0x%x cpu=%p readback=%d wait_idle=%d ok=%d\n", rc, b.handle, b.offset,
		(void *)b.cpu, (b.cpu != NULL) ? 1 : 0, wrc, ok && (wrc == 0));
	verdict("v3d_bo", ok && (wrc == 0));
	rbo_free(&b);
}


/* The M1-proven clear (v3da_clgen.c): 64x64 RGBA8, zero draws. Returns the sync
 * handle of the job (or 0), leaves the RT mapped in *rt for a second check. */
static int cl_clear(rbo_t *rt, rbo_t *bcl, rbo_t *rcl, rbo_t *ta, rbo_t *ts, uint32_t colour, uint32_t in_sync,
	uint32_t *out_sync, int *pixels_ok, int wait)
{
	struct drm_v3d_submit_cl s;
	v3da_clgen_buf_t gb, gr;
	uint32_t handles[5], i, bad = 0;
	int rc;

	for (i = 0; i < 64u * 64u; i++) {
		rt->cpu[i] = 0xdeadbeefu;
	}
	gb.cpu = bcl->cpu;
	gb.gpuva = bcl->offset;
	gb.size = bcl->size;
	gr.cpu = rcl->cpu;
	gr.gpuva = rcl->offset;
	gr.size = rcl->size;
	rc = v3da_clgen_clear(64u, 64u, rt->offset, colour, &gb, &gr, ta->offset, ta->size, ts->offset, &s);
	if (rc != 0) {
		return rc;
	}
	handles[0] = rt->handle;
	handles[1] = bcl->handle;
	handles[2] = rcl->handle;
	handles[3] = ta->handle;
	handles[4] = ts->handle;
	s.bo_handles = (uintptr_t)handles;
	s.bo_handle_count = 5u;
	s.in_sync_bcl = in_sync;
	s.out_sync = *out_sync;
	if (drmIoctl(P.render, DRM_IOCTL_V3D_SUBMIT_CL, &s) != 0) {
		return -errno;
	}
	if (!wait) {
		*pixels_ok = 0;   /* the caller checks later (G13: after a flip that must have waited for it) */
		return 0;
	}
	rc = drmSyncobjWait(P.render, out_sync, 1, INT64_MAX, 0, NULL);
	if (rc != 0) {
		return rc;
	}
	for (i = 0; i < 64u * 64u; i++) {
		bad += (rt->cpu[i] != colour);
	}
	*pixels_ok = (bad == 0u);
	return 0;
}


static void t_render_clear(void)
{
	rbo_t rt, bcl, rcl, ta, ts;
	uint32_t bsz, rsz, tasz, tssz, s1 = 0, s2 = 0, s3 = 0, first = 99;
	struct drm_v3d_wait_bo wb;
	int rc, px1 = 0, px2 = 0, ok, sfd = -1, rc_exp = -1, rc_imp = -1, rc_w2 = -1, rc_tr = -1, rc_w3 = -1, rc_empty = 0,
		rc_sig = -1, rc_w4 = -1, wrc;
	uint64_t t0, t1;

	memset(&rt, 0, sizeof(rt));
	memset(&bcl, 0, sizeof(bcl));
	memset(&rcl, 0, sizeof(rcl));
	memset(&ta, 0, sizeof(ta));
	memset(&ts, 0, sizeof(ts));
	(void)v3da_clgen_clear_sizes(64u, 64u, &bsz, &rsz, &tasz, &tssz);
	rc = rbo_new(&rt, 64u * 64u * 4u);
	if (rc == 0) rc = rbo_new(&bcl, 4096u);
	if (rc == 0) rc = rbo_new(&rcl, 4096u);
	if (rc == 0) rc = rbo_new(&ta, tasz);
	if (rc == 0) rc = rbo_new(&ts, 4096u);
	if (rc == 0) rc = drmSyncobjCreate(P.render, 0, &s1);
	t0 = now_us();
	if (rc == 0) rc = cl_clear(&rt, &bcl, &rcl, &ta, &ts, 0xff3366ccu, 0u, &s1, &px1, 1);
	t1 = now_us();
	memset(&wb, 0, sizeof(wb));
	wb.handle = rt.handle;
	wb.timeout_ns = ~0ull;
	wrc = drmIoctl(P.render, DRM_IOCTL_V3D_WAIT_BO, &wb);
	ok = (rc == 0) && px1 && (wrc == 0);
	printf(TAG "cl_clear rc=%d pixels_ok=%d rt0=0x%08x wait_bo=%d us=%llu sizes=%u/%u/%u/%u ok=%d\n", rc, px1,
		(rt.cpu != NULL) ? rt.cpu[0] : 0u, wrc, (unsigned long long)(t1 - t0), bsz, rsz, tasz, tssz, ok);
	verdict("cl_clear", ok);

	/* a dependent second job (in_sync_bcl = the first job's syncobj) */
	if (rc == 0) {
		rc = cl_clear(&rt, &bcl, &rcl, &ta, &ts, 0xff00ff00u, s1, &s1, &px2, 1);
		printf(TAG "cl_clear_dep rc=%d pixels_ok=%d rt0=0x%08x ok=%d\n", rc, px2, rt.cpu[0], (rc == 0) && px2);
		verdict("cl_clear_dep", (rc == 0) && px2);
	}

	/* sync files (in-process emulation), transfer, reset/signal */
	if (rc == 0) {
		rc_exp = drmSyncobjExportSyncFile(P.render, s1, &sfd);
		if ((drmSyncobjCreate(P.render, 0, &s2) == 0) && (rc_exp == 0)) {
			rc_imp = drmSyncobjImportSyncFile(P.render, s2, sfd);
			rc_w2 = drmSyncobjWait(P.render, &s2, 1, INT64_MAX, 0, &first);
		}
		if (drmSyncobjCreate(P.render, 0, &s3) == 0) {
			rc_tr = drmSyncobjTransfer(P.render, s3, 0, s1, 0, 0);
			rc_w3 = drmSyncobjWait(P.render, &s3, 1, INT64_MAX, 0, NULL);
			(void)drmSyncobjReset(P.render, &s3, 1);
			rc_empty = drmSyncobjWait(P.render, &s3, 1, 0, 0, NULL);   /* empty, no WAIT_FOR_SUBMIT: -EINVAL */
			rc_sig = drmSyncobjSignal(P.render, &s3, 1);
			rc_w4 = drmSyncobjWait(P.render, &s3, 1, INT64_MAX, 0, NULL);
		}
		ok = (rc_exp == 0) && (sfd >= 0) && (rc_imp == 0) && (rc_w2 == 0) && (rc_tr == 0) && (rc_w3 == 0) &&
			(rc_empty == -EINVAL) && (rc_sig == 0) && (rc_w4 == 0);
		printf(TAG "syncobj export=%d sfd=%d import=%d wait=%d first=%u transfer=%d wait_t=%d wait_empty=%d signal=%d "
			"wait_s=%d ok=%d\n", rc_exp, sfd, rc_imp, rc_w2, first, rc_tr, rc_w3, rc_empty, rc_sig, rc_w4, ok);
		verdict("syncobj", ok);
	}

	/* cross-server fence: flip to a buffer only after a GPU job, IN_FENCE_FD = the sync file */
	if ((rc == 0) && (sfd >= 0) && (P.primary != 0u) && (P.buf[0].fb != 0u)) {
		drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(P.card, P.primary, DRM_MODE_OBJECT_PLANE);
		drmModeAtomicReqPtr req = drmModeAtomicAlloc();
		uint32_t fb_prop = 0, fence_prop = 0;
		char nb[40];
		int j, rcf;
		for (j = 0; (props != NULL) && (j < (int)props->count_props); j++) {
			prop_name_of(P.card, props->props[j], nb, sizeof(nb));
			if (strcmp(nb, "FB_ID") == 0) fb_prop = props->props[j];
			if (strcmp(nb, "IN_FENCE_FD") == 0) fence_prop = props->props[j];
		}
		drmModeFreeObjectProperties(props);
		(void)drmModeAtomicAddProperty(req, P.primary, fb_prop, P.buf[0].fb);
		(void)drmModeAtomicAddProperty(req, P.primary, fence_prop, (uint64_t)sfd);
		P.flips_done = 0;
		rcf = drmModeAtomicCommit(P.card, req, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, NULL);
		if (rcf == 0) {
			rcf = wait_flips(1, 3000);
		}
		drmModeAtomicFree(req);
		printf(TAG "in_fence fence_prop=0x%x commit=%d errno=%d events=%d ok=%d\n", fence_prop, rcf,
			(rcf != 0) ? errno : 0, P.flips_done, rcf == 0);
		verdict("in_fence", rcf == 0);
	}
	if (sfd >= 0) {
		close(sfd);
	}
	if (s1 != 0u) (void)drmSyncobjDestroy(P.render, s1);
	if (s2 != 0u) (void)drmSyncobjDestroy(P.render, s2);
	if (s3 != 0u) (void)drmSyncobjDestroy(P.render, s3);
	rbo_free(&rt);
	rbo_free(&bcl);
	rbo_free(&rcl);
	rbo_free(&ta);
	rbo_free(&ts);
}


/* ========================================================================= */
/* 5. PRIME                                                                   */
/* ========================================================================= */

/* G1 round trip (M3 part 2): a kms dumb buffer, PRIME-exported and imported on
 * the render node, is the render target of a GPU clear; the CPU reads the result
 * back through the kms mapping (the same pages, three mappings: kms server, v3d
 * server, this process). Then G13: a second clear is submitted and NOT waited
 * for, and the buffer is flipped with no IN_FENCE_FD - libdrm-phoenix attaches
 * the BO's last-use fence, rpi4-kms -G holds the flip until the GPU is done, so
 * the pixels are complete when the flip event arrives. */
static void t_import_rt(uint32_t h_render)
{
	rbo_t rt, bcl, rcl, ta, ts;
	struct drm_v3d_get_bo_offset go;
	struct drm_v3d_wait_bo wb;
	uint32_t bsz, rsz, tasz, tssz, s = 0, i, bad = 0;
	int rc, px = 0, wrc = -1, rcs = -1, rcf = -1, ok;
	uint64_t t0 = 0, t1 = 0;

	memset(&rt, 0, sizeof(rt));
	memset(&bcl, 0, sizeof(bcl));
	memset(&rcl, 0, sizeof(rcl));
	memset(&ta, 0, sizeof(ta));
	memset(&ts, 0, sizeof(ts));
	memset(&go, 0, sizeof(go));
	(void)v3da_clgen_clear_sizes(64u, 64u, &bsz, &rsz, &tasz, &tssz);
	go.handle = h_render;
	rc = (drmIoctl(P.render, DRM_IOCTL_V3D_GET_BO_OFFSET, &go) == 0) ? 0 : -errno;
	rt.handle = h_render;
	rt.offset = go.offset;
	rt.size = 64u * 64u * 4u;
	rt.cpu = P.buf[0].px;   /* the kms dumb mapping: the CPU view of the imported pages */
	if (rc == 0) rc = rbo_new(&bcl, 4096u);
	if (rc == 0) rc = rbo_new(&rcl, 4096u);
	if (rc == 0) rc = rbo_new(&ta, tasz);
	if (rc == 0) rc = rbo_new(&ts, 4096u);
	if (rc == 0) rc = drmSyncobjCreate(P.render, 0, &s);
	if (rc == 0) rc = cl_clear(&rt, &bcl, &rcl, &ta, &ts, 0xff2080ffu, 0u, &s, &px, 1);
	memset(&wb, 0, sizeof(wb));
	wb.handle = h_render;
	wb.timeout_ns = ~0ull;
	if (rc == 0) {
		wrc = drmIoctl(P.render, DRM_IOCTL_V3D_WAIT_BO, &wb);   /* an imported BO: the server's BO_WAIT */
	}
	ok = (rc == 0) && px && (wrc == 0);
	printf(TAG "import_clear handle=0x%x gpuva=0x%x rc=%d pixels_ok=%d px0=0x%08x wait_bo=%d ok=%d\n", h_render, go.offset, rc,
		px, (P.buf[0].px != NULL) ? P.buf[0].px[0] : 0u, wrc, ok);
	verdict("import_clear", ok);

	/* G13: show buf[1], render buf[0] without waiting, flip to buf[0] with no fence */
	if ((rc == 0) && (P.buf[1].fb != 0u)) {
		P.flips_done = 0;
		if ((drmModePageFlip(P.card, P.crtc, P.buf[1].fb, DRM_MODE_PAGE_FLIP_EVENT, NULL) == 0) &&
				(wait_flips(1, 3000) == 0)) {
			rcs = cl_clear(&rt, &bcl, &rcl, &ta, &ts, 0xff80ff20u, 0u, &s, &px, 0);
			t0 = now_us();
			P.flips_done = 0;
			rcf = (rcs == 0) ? drmModePageFlip(P.card, P.crtc, P.buf[0].fb, DRM_MODE_PAGE_FLIP_EVENT, NULL) : -1;
			if (rcf == 0) {
				rcf = wait_flips(1, 3000);
			}
			t1 = now_us();
			for (i = 0; i < 64u * 64u; i++) {
				bad += (P.buf[0].px[i] != 0xff80ff20u);   /* read BEFORE any explicit wait */
			}
		}
		ok = (rcs == 0) && (rcf == 0) && (P.flips_done == 1) && (bad == 0u);
		printf(TAG "implicit_flip submit=%d flip=%d events=%d pixels_ok=%d px0=0x%08x flip_us=%llu ok=%d\n", rcs, rcf,
			P.flips_done, bad == 0u, P.buf[0].px[0], (unsigned long long)(t1 - t0), ok);
		verdict("implicit_flip", ok);
		(void)drmSyncobjWait(P.render, &s, 1, INT64_MAX, 0, NULL);   /* nothing in flight before the BOs go */
	}
	if (s != 0u) {
		(void)drmSyncobjDestroy(P.render, s);
	}
	rbo_free(&bcl);
	rbo_free(&rcl);
	rbo_free(&ta);
	rbo_free(&ts);
}

static void t_prime(void)
{
	char path[64] = "-";
	uint32_t h_self = 0, h_render = 0;
	int pfd = -1, rc_exp, rc_self = -1, rc_imp = -1, e_imp = 0, same = 0, rc_rexp, e_rexp = 0, pfd2 = -1;
	uint32_t *m;
	struct drm_v3d_create_bo cb = { .size = 4096 };
	struct drm_gem_close gc;

	rc_exp = drmPrimeHandleToFD(P.card, P.buf[0].handle, DRM_CLOEXEC, &pfd);
	if (rc_exp == 0) {
		/* G3: a dma-buf's size by lseek(SEEK_END), as Mesa reads it (v3d_bufmgr.c:454) */
		off_t end = lseek(pfd, 0, SEEK_END);
		int e_end = (end < 0) ? errno : 0, ok_end = (end == (off_t)P.buf[0].size);
		(void)lseek(pfd, 0, SEEK_SET);
		printf(TAG "dmabuf_size end=%lld errno=%d want=%llu ok=%d gap=%d\n", (long long)end, e_end,
			(unsigned long long)P.buf[0].size, ok_end, (end < 0) && (e_end == ENOENT));
		if ((end < 0) && (e_end == ENOENT)) {
			P.gap++;   /* rpi4-kms from before M3 part 2 refuses atSize */
		}
		else {
			verdict("dmabuf_size", ok_end);
		}
		(void)sys_fdpath(pfd, path, sizeof(path));
		m = mmap(NULL, (size_t)P.buf[0].size, PROT_READ, MAP_SHARED, pfd, 0);
		if (m != MAP_FAILED) {
			same = (m[0] == P.buf[0].px[0]) && (m[P.buf[0].size / 8u] == P.buf[0].px[P.buf[0].size / 8u]);
			P.buf[0].px[1] ^= 0x00ffffffu;   /* write through the dumb mapping, read through the dma-buf */
			same = same && (m[1] == P.buf[0].px[1]);
			(void)munmap(m, (size_t)P.buf[0].size);
		}
		rc_self = drmPrimeFDToHandle(P.card, pfd, &h_self);
		rc_imp = drmPrimeFDToHandle(P.render, pfd, &h_render);
		e_imp = (rc_imp != 0) ? errno : 0;
	}
	printf(TAG "prime_export rc=%d fd=%d path=%s mmap_same_pages=%d self_import=%d handle=%u/%u ok=%d\n", rc_exp, pfd,
		path, same, rc_self, h_self, P.buf[0].handle, (rc_exp == 0) && same && (rc_self == 0) && (h_self == P.buf[0].handle));
	verdict("prime_export", (rc_exp == 0) && same && (rc_self == 0) && (h_self == P.buf[0].handle));
	printf(TAG "prime_import_render rc=%d errno=%d handle=0x%x ok=%d gap=%d\n", rc_imp, e_imp, h_render,
		(rc_imp == 0) && (h_render != 0u), e_imp == ENOSYS);
	gapcheck("prime_import_render", e_imp, ENOSYS);   /* ENOSYS: a render server from before M3 part 2 (G1) */
	if (pfd >= 0) {
		close(pfd);   /* the import holds the pages through the render server's own mapping (E1) */
	}
	if ((rc_imp == 0) && (h_render != 0u)) {
		uint32_t h_again = 0;
		int rc_again = -1;
		pfd = -1;
		if (drmPrimeHandleToFD(P.card, P.buf[0].handle, DRM_CLOEXEC, &pfd) == 0) {
			rc_again = drmPrimeFDToHandle(P.render, pfd, &h_again);   /* DRM: same buffer, same handle */
			close(pfd);
		}
		printf(TAG "prime_reimport rc=%d handle=0x%x same=%d ok=%d\n", rc_again, h_again, h_again == h_render,
			(rc_again == 0) && (h_again == h_render));
		verdict("prime_reimport", (rc_again == 0) && (h_again == h_render));
		t_import_rt(h_render);
		memset(&gc, 0, sizeof(gc));
		gc.handle = h_render;
		(void)drmIoctl(P.render, DRM_IOCTL_GEM_CLOSE, &gc);   /* the one close releases it (the server logs "import released") */
	}

	/* render-node export (V3DA_OP_BO_EXPORT does not exist yet) */
	rc_rexp = -1;
	if (drmIoctl(P.render, DRM_IOCTL_V3D_CREATE_BO, &cb) == 0) {
		rc_rexp = drmPrimeHandleToFD(P.render, cb.handle, DRM_CLOEXEC, &pfd2);
		e_rexp = (rc_rexp != 0) ? errno : 0;
		memset(&gc, 0, sizeof(gc));
		gc.handle = cb.handle;
		(void)drmIoctl(P.render, DRM_IOCTL_GEM_CLOSE, &gc);
	}
	printf(TAG "prime_export_render rc=%d errno=%d gap=%d\n", rc_rexp, e_rexp, e_rexp == ENOSYS);
	gapcheck("prime_export_render", e_rexp, ENOSYS);
	if (pfd2 >= 0) {
		close(pfd2);
	}
}


int main(int argc, char **argv)
{
	char card_path[64], render_path[64], card1_path[64];
	int c, nflips = 60, keep = 0, i;
	uint64_t t_start = now_us();

	setvbuf(stdout, NULL, _IOLBF, 0);
	while ((c = getopt(argc, argv, "n:k")) != -1) {
		switch (c) {
			case 'n': nflips = atoi(optarg); break;
			case 'k': keep = 1; break;
			default:
				printf("usage: %s [-n flips] [-k]\n", argv[0]);
				return 2;
		}
	}
	printf(TAG "start pid=%d flips=%d libdrm=libdrm-phoenix\n", (int)getpid(), nflips);

	t_devices(card_path, render_path, card1_path, sizeof(card_path));
	if ((card_path[0] == '\0') || (render_path[0] == '\0')) {
		printf(TAG "RESULT pass=%d fail=%d gap=%d failed=%s verdict=FAIL (servers running?)\n", P.pass, P.fail + 1, P.gap,
			P.failed);
		return 1;
	}
	P.card = t_open("card", card_path);
	P.render = t_open("render", render_path);
	if ((P.card < 0) || (P.render < 0)) {
		printf(TAG "RESULT pass=%d fail=%d gap=%d failed=%s verdict=FAIL\n", P.pass, P.fail, P.gap, P.failed);
		return 1;
	}
	t_identity("card", P.card, "vc4", DRM_NODE_PRIMARY, 1);
	t_identity("render", P.render, "v3d", DRM_NODE_RENDER, 0);
	if ((card1_path[0] != '\0') && (strcmp(card1_path, render_path) != 0)) {
		P.card1 = t_open("card1", card1_path);   /* G10: the v3d primary node has a name (and port) of its own */
		if (P.card1 >= 0) {
			t_identity("card1", P.card1, "v3d", DRM_NODE_PRIMARY, 0);
		}
	}
	else {
		printf(TAG "card1 path=%s distinct=0 gap=1 (no /dev/dri/card1: servers from before M3 part 2, G10)\n",
			(card1_path[0] != '\0') ? card1_path : "-");
		P.gap++;
	}
	t_fstat();

	if (t_kms_enum() == 0) {
		printf(TAG "kms_flip start mode=%ux%u@%u\n", P.mode.hdisplay, P.mode.vdisplay, P.mode.vrefresh);
		if (t_dumb() == 0) {
			t_flips(nflips);
			t_atomic();
		}
	}
	t_render_basics();
	t_render_clear();
	if (P.buf[0].handle != 0u) {
		t_prime();
	}

	if (!keep && (P.crtc != 0u)) {
		(void)drmModeSetCrtc(P.card, P.crtc, 0, 0, 0, NULL, 0, NULL);   /* planes off: the console comes back */
	}
	for (i = 0; i < 2; i++) {
		if (P.buf[i].fb != 0u) {
			(void)drmModeRmFB(P.card, P.buf[i].fb);
		}
		if (P.buf[i].px != NULL) {
			(void)munmap(P.buf[i].px, (size_t)P.buf[i].size);
		}
		if (P.buf[i].handle != 0u) {
			(void)drmModeDestroyDumbBuffer(P.card, P.buf[i].handle);
		}
	}
	if (P.card1 >= 0) {
		close(P.card1);
	}
	close(P.render);
	close(P.card);
	printf(TAG "RESULT pass=%d fail=%d gap=%d failed=%s secs=%llu verdict=%s\n", P.pass, P.fail, P.gap,
		(P.failed[0] != '\0') ? P.failed : "-", (unsigned long long)((now_us() - t_start) / 1000000u),
		(P.fail == 0) ? "PASS" : "FAIL");
	return (P.fail == 0) ? 0 : 1;
}
