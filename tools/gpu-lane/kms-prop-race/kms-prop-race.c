/*
 * Phoenix-RTOS
 *
 * kms-prop-race: reproduce KNOWN-ISSUES C21 (an rpi4-kms commit that stays in flight, so every
 * later commit of the client fails with EBUSY).
 *
 * What gate R did when it happened: a client put an NV12 picture on an overlay plane, then set
 * COLOR_ENCODING and COLOR_RANGE with two legacy SETPROPERTY calls in a row (each a one-plane
 * commit), then flipped with SETPLANE. The second property call got EBUSY and every later
 * SETPLANE of that process failed; a client before it had done the same without error. So
 * this program does exactly that, and is meant to be run several times in a row (each run is a
 * new client that finds the plane its predecessor left on exit):
 *
 *   kms-prop-race [flips]
 *
 * A refused commit is retried every 5 ms; one that is still refused after 1 s is a stuck
 * commit. Every line starts with "KMSRACE "; the last is
 * "KMSRACE result=PASS|FAIL|SKIP ..." (FAIL = a stuck commit). With rpi4-kms from devices
 * 1e69f11b on, the server logs "commit stuck ..." with what the commit waits for.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define W 1920u
#define H 1080u

#define RETRY_US  5000u
#define STUCK_MS  1000u


static uint64_t now_ms(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}


static int find_plane(int fd, uint32_t *plane_id)
{
	drmModePlaneRes *pr = drmModeGetPlaneResources(fd);
	uint32_t i, j;
	int found = 0;

	if (pr == NULL) {
		return 0;
	}
	for (i = 0; (i < pr->count_planes) && !found; i++) {
		drmModePlane *p = drmModeGetPlane(fd, pr->planes[i]);
		if (p == NULL) {
			continue;
		}
		if ((p->possible_crtcs & 1u) != 0u) {
			for (j = 0; j < p->count_formats; j++) {
				if (p->formats[j] == DRM_FORMAT_NV12) {
					*plane_id = p->plane_id;
					found = 1;
					break;
				}
			}
		}
		drmModeFreePlane(p);
	}
	drmModeFreePlaneResources(pr);
	return found;
}


static uint32_t prop_id(int fd, uint32_t plane, const char *name)
{
	drmModeObjectProperties *op = drmModeObjectGetProperties(fd, plane, DRM_MODE_OBJECT_PLANE);
	uint32_t i, id = 0;

	if (op == NULL) {
		return 0;
	}
	for (i = 0; (i < op->count_props) && (id == 0u); i++) {
		drmModePropertyRes *pr = drmModeGetProperty(fd, op->props[i]);
		if (pr != NULL) {
			if (strcmp(pr->name, name) == 0) {
				id = pr->prop_id;
			}
			drmModeFreeProperty(pr);
		}
	}
	drmModeFreeObjectProperties(op);
	return id;
}


/* Retry a refused commit every RETRY_US. Returns the last rc; *busy_ms = how long it was refused. */
static int setprop(int fd, uint32_t plane, uint32_t prop, uint64_t v, uint32_t *busy_ms)
{
	uint64_t t0 = now_ms();
	int rc;

	while (((rc = drmModeObjectSetProperty(fd, plane, DRM_MODE_OBJECT_PLANE, prop, v)) != 0) && (errno == EBUSY) &&
		((now_ms() - t0) < STUCK_MS)) {
		usleep(RETRY_US);
	}
	*busy_ms = (uint32_t)(now_ms() - t0);
	return rc;
}


static int setplane(int fd, uint32_t plane, uint32_t crtc, uint32_t fb, uint32_t *busy_ms)
{
	uint64_t t0 = now_ms();
	int rc;

	while (((rc = drmModeSetPlane(fd, plane, crtc, fb, 0, 0, 0, W / 2u, H / 2u, 0, 0, W << 16, H << 16)) != 0) &&
		(errno == EBUSY) && ((now_ms() - t0) < STUCK_MS)) {
		usleep(RETRY_US);
	}
	*busy_ms = (uint32_t)(now_ms() - t0);
	return rc;
}


int main(int argc, char *argv[])
{
	uint32_t flips = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0) : 120u;
	uint32_t h16 = (H + 15u) & ~15u, colh = h16 + h16 / 2u, cols = (W + 127u) / 128u;
	uint32_t plane = 0, crtc, fb[2] = { 0, 0 }, handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	uint32_t enc, range, i, busy, busy_max = 0, refused = 0;
	uint64_t mods[4] = { 0 };
	struct drm_mode_create_dumb cd;
	struct drm_mode_map_dumb md;
	drmModeRes *res;
	uint8_t *buf;
	int fd, rc, k;

	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		printf("KMSRACE result=FAIL open errno=%d\n", errno);
		return 1;
	}
	(void)drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	res = drmModeGetResources(fd);
	if ((res == NULL) || (res->count_crtcs == 0)) {
		printf("KMSRACE result=FAIL no crtc\n");
		return 1;
	}
	crtc = res->crtcs[0];
	if (!find_plane(fd, &plane)) {
		printf("KMSRACE result=SKIP no plane offers NV12 (rpi4-kms -o)\n");
		return 2;
	}
	enc = prop_id(fd, plane, "COLOR_ENCODING");
	range = prop_id(fd, plane, "COLOR_RANGE");
	if ((enc == 0u) || (range == 0u)) {
		printf("KMSRACE result=SKIP plane %u has no COLOR_ENCODING/COLOR_RANGE\n", plane);
		return 2;
	}

	/* One SAND128 NV12 buffer (as sand-plane), mid grey, shown through two framebuffers */
	memset(&cd, 0, sizeof(cd));
	cd.width = cols * 128u;
	cd.height = colh;
	cd.bpp = 8u;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) != 0) {
		printf("KMSRACE result=FAIL create_dumb errno=%d\n", errno);
		return 1;
	}
	memset(&md, 0, sizeof(md));
	md.handle = cd.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) != 0) {
		printf("KMSRACE result=FAIL map_dumb errno=%d\n", errno);
		return 1;
	}
	buf = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)md.offset);
	if (buf == MAP_FAILED) {
		printf("KMSRACE result=FAIL mmap errno=%d\n", errno);
		return 1;
	}
	memset(buf, 128, cd.size);
	handles[0] = handles[1] = cd.handle;
	pitches[0] = pitches[1] = W;
	offsets[1] = h16 * 128u;
	mods[0] = mods[1] = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(colh);
	for (k = 0; k < 2; k++) {
		rc = drmModeAddFB2WithModifiers(fd, W, H, DRM_FORMAT_NV12, handles, pitches, offsets, mods, &fb[k],
			DRM_MODE_FB_MODIFIERS);
		if (rc != 0) {
			printf("KMSRACE result=FAIL addfb2 rc=%d\n", rc);
			return 1;
		}
	}

	/* What gate R's client did: the first picture, then the two properties in a row */
	rc = setplane(fd, plane, crtc, fb[0], &busy);
	printf("KMSRACE pid=%d plane=%u first setplane rc=%d busy_ms=%u\n", (int)getpid(), plane, rc, busy);
	if (rc != 0) {
		printf("KMSRACE result=FAIL first setplane errno=%d busy_ms=%u\n", errno, busy);
		return 1;
	}
	rc = setprop(fd, plane, enc, 1u, &busy);   /* BT.709 */
	printf("KMSRACE COLOR_ENCODING rc=%d busy_ms=%u\n", rc, busy);
	if (rc != 0) {
		printf("KMSRACE result=FAIL COLOR_ENCODING errno=%d busy_ms=%u\n", errno, busy);
		return 1;
	}
	rc = setprop(fd, plane, range, 0u, &busy);   /* limited */
	printf("KMSRACE COLOR_RANGE rc=%d busy_ms=%u\n", rc, busy);
	if (rc != 0) {
		printf("KMSRACE result=FAIL COLOR_RANGE errno=%d busy_ms=%u\n", errno, busy);
		return 1;
	}

	for (i = 0; i < flips; i++) {
		rc = setplane(fd, plane, crtc, fb[(i + 1u) & 1u], &busy);
		busy_max = (busy > busy_max) ? busy : busy_max;
		refused += (busy > 0u) ? 1u : 0u;
		if (rc != 0) {
			printf("KMSRACE result=FAIL flip %u errno=%d busy_ms=%u\n", i, errno, busy);
			return 1;
		}
	}
	/* Leave the plane as gate R's client did: the server takes it off when this client goes */
	printf("KMSRACE result=PASS flips=%u refused=%u busy_max_ms=%u\n", flips, refused, busy_max);
	return 0;
}
