/*
 * Phoenix-RTOS
 *
 * sand-plane: step 1 of docs/misc/2026-11-21-yuv-sand-plane-design.md on the hardware.
 *
 * Shows a synthetic 1920x1080 NV12 picture in the HEVC block's one-buffer SAND128 layout
 * (128-byte columns; each holds the luma, H16 lines, then the chroma, H16/2 lines) on an
 * overlay plane, through ADDFB2 with DRM_FORMAT_MOD_BROADCOM_SAND128(column height), for a
 * few seconds, then takes it down. The picture: a luma ramp left to right and eight chroma
 * bars, so a wrong column layout shows as torn 128-pixel stripes and a wrong chroma offset
 * as grey or garbage colour. Judge it on an HDMI snapshot.
 *
 *   sand-plane [seconds]
 *
 * Every line starts with "SANDPLANE "; the last is "SANDPLANE result=PASS|FAIL|SKIP ...".
 * SKIP: no plane offers NV12 (rpi4-kms exposes overlays only with -o).
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
#include <unistd.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define W 1920u
#define H 1080u


static int find_plane(int fd, uint32_t crtc_index, uint32_t *plane_id)
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
		if ((p->possible_crtcs & (1u << crtc_index)) != 0u) {
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


int main(int argc, char *argv[])
{
	static const uint8_t bars[8][2] = {   /* Cb, Cr (BT.709 limited, roughly the colour-bar hues) */
		{ 128, 128 }, { 16, 138 }, { 166, 16 }, { 54, 34 }, { 202, 222 }, { 90, 240 }, { 240, 118 }, { 128, 128 }
	};
	uint32_t secs = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0) : 8u;
	uint32_t h16 = (H + 15u) & ~15u, colh = h16 + h16 / 2u, cols = (W + 127u) / 128u;
	uint32_t plane = 0, crtc, fb = 0, x, y, handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	uint64_t mods[4] = { 0 };
	struct drm_mode_create_dumb cd;
	struct drm_mode_map_dumb md;
	drmModeRes *res;
	uint8_t *buf;
	int fd, rc;

	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		printf("SANDPLANE result=FAIL open errno=%d\n", errno);
		return 1;
	}
	(void)drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	res = drmModeGetResources(fd);
	if ((res == NULL) || (res->count_crtcs == 0)) {
		printf("SANDPLANE result=FAIL no crtc\n");
		return 1;
	}
	crtc = res->crtcs[0];
	if (!find_plane(fd, 0u, &plane)) {
		printf("SANDPLANE result=SKIP no plane offers NV12 (rpi4-kms -o)\n");
		return 2;
	}

	/* The buffer as a dumb BO: 8-bit "pixels", one row per line of a column, cols * 128 wide */
	memset(&cd, 0, sizeof(cd));
	cd.width = cols * 128u;
	cd.height = colh;
	cd.bpp = 8u;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) != 0) {
		printf("SANDPLANE result=FAIL create_dumb errno=%d\n", errno);
		return 1;
	}
	memset(&md, 0, sizeof(md));
	md.handle = cd.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) != 0) {
		printf("SANDPLANE result=FAIL map_dumb errno=%d\n", errno);
		return 1;
	}
	buf = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)md.offset);
	if (buf == MAP_FAILED) {
		printf("SANDPLANE result=FAIL mmap errno=%d\n", errno);
		return 1;
	}
	printf("SANDPLANE buffer handle=%u size=%llu cols=%u colh=%u chroma_at=%u\n", cd.handle,
		(unsigned long long)cd.size, cols, colh, h16 * 128u);

	for (y = 0; y < h16; y++) {
		for (x = 0; x < cols * 128u; x++) {
			buf[(size_t)(x / 128u) * 128u * colh + (size_t)y * 128u + (x % 128u)] =
				(uint8_t)(16u + (219u * ((x < W) ? x : W - 1u)) / (W - 1u));
		}
	}
	for (y = 0; y < h16 / 2u; y++) {
		for (x = 0; x < cols * 128u; x += 2u) {
			size_t o = (size_t)(x / 128u) * 128u * colh + (size_t)(h16 + y) * 128u + (x % 128u);
			const uint8_t *c = bars[((x < W) ? x : W - 1u) * 8u / W];
			buf[o] = c[0];
			buf[o + 1u] = c[1];
		}
	}

	handles[0] = handles[1] = cd.handle;
	pitches[0] = pitches[1] = W;
	offsets[0] = 0;
	offsets[1] = h16 * 128u;
	mods[0] = mods[1] = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(colh);
	rc = drmModeAddFB2WithModifiers(fd, W, H, DRM_FORMAT_NV12, handles, pitches, offsets, mods, &fb, DRM_MODE_FB_MODIFIERS);
	if (rc != 0) {
		printf("SANDPLANE result=FAIL addfb2 rc=%d\n", rc);
		return 1;
	}
	/* Half the screen, top left, scaled down 2x by the HVS */
	rc = drmModeSetPlane(fd, plane, crtc, fb, 0, 0, 0, W / 2u, H / 2u, 0, 0, W << 16, H << 16);
	if (rc != 0) {
		printf("SANDPLANE result=FAIL setplane plane=%u rc=%d\n", plane, rc);
		return 1;
	}
	printf("SANDPLANE shown plane=%u fb=%u for %u s\n", plane, fb, secs);
	fflush(stdout);
	sleep(secs);

	(void)drmModeSetPlane(fd, plane, crtc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	(void)drmModeRmFB(fd, fb);
	munmap(buf, cd.size);
	printf("SANDPLANE result=PASS plane=%u (judge the HDMI snapshot)\n", plane);
	return 0;
}
