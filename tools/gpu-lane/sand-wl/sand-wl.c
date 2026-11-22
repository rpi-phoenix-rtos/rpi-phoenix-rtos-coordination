/*
 * Phoenix-RTOS
 *
 * sand-wl: step 2a's first probe (docs/misc/2026-11-21-yuv-sand-plane-design.md §5): can the
 * Wayland compositor (labwc/wlroots, GLES2 renderer on Mesa V3D) take an NV12 picture in the
 * HEVC block's SAND128 column layout as a linux-dmabuf buffer and composite it?
 *
 * The picture is sand-plane's (luma ramp left to right, eight chroma bars), 1280x720, in an
 * rpi4-kms dumb buffer exported as a dma-buf, shown in an xdg toplevel for a few seconds.
 * A wrong column layout shows as torn 128-pixel stripes, a wrong chroma offset as grey or
 * garbage colour: judge the HDMI snapshot.
 *
 *   sand-wl [seconds]
 *
 * Every line starts with "SANDWL "; it lists the NV12 modifiers the compositor offers, then
 * the last line is "SANDWL result=PASS|FAIL|SKIP ..." (SKIP: NV12 + SAND128 not offered;
 * FAIL: the compositor refused the buffer or a protocol error ended the client).
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
#include <drm_fourcc.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"

#define W 1280u
#define H 720u


static struct {
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm;
	struct zwp_linux_dmabuf_v1 *dmabuf;
	int nv12_sand;      /* the compositor offered NV12 with a SAND128 modifier */
	int nv12_any;
	int configured;
	int buffer_state;   /* 0 waiting, 1 created, -1 failed */
	struct wl_buffer *buffer;
} g;


static void dmabuf_format(void *data, struct zwp_linux_dmabuf_v1 *d, uint32_t format)
{
	(void)data;
	(void)d;
	(void)format;
}


static void dmabuf_modifier(void *data, struct zwp_linux_dmabuf_v1 *d, uint32_t format, uint32_t hi, uint32_t lo)
{
	uint64_t mod = ((uint64_t)hi << 32) | lo;

	(void)data;
	(void)d;
	if (format != DRM_FORMAT_NV12) {
		return;
	}
	g.nv12_any = 1;
	printf("SANDWL compositor offers NV12 modifier 0x%016llx%s\n", (unsigned long long)mod,
		(fourcc_mod_broadcom_mod(mod) == DRM_FORMAT_MOD_BROADCOM_SAND128) ? " (SAND128)" : "");
	if (fourcc_mod_broadcom_mod(mod) == DRM_FORMAT_MOD_BROADCOM_SAND128) {
		g.nv12_sand = 1;
	}
}


static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = { dmabuf_format, dmabuf_modifier };


static void wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm, serial);
}


static const struct xdg_wm_base_listener wm_listener = { wm_ping };


static void registry_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	(void)data;
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		g.compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	}
	else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
		g.wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(g.wm, &wm_listener, NULL);
	}
	else if ((strcmp(iface, zwp_linux_dmabuf_v1_interface.name) == 0) && (ver >= 3u)) {
		g.dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);   /* v3: modifier events */
		zwp_linux_dmabuf_v1_add_listener(g.dmabuf, &dmabuf_listener, NULL);
	}
}


static void registry_remove(void *data, struct wl_registry *r, uint32_t name)
{
	(void)data;
	(void)r;
	(void)name;
}


static const struct wl_registry_listener registry_listener = { registry_global, registry_remove };


static void surface_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
	(void)data;
	xdg_surface_ack_configure(s, serial);
	g.configured = 1;
}


static const struct xdg_surface_listener surface_listener = { surface_configure };


static void params_created(void *data, struct zwp_linux_buffer_params_v1 *p, struct wl_buffer *b)
{
	(void)data;
	(void)p;
	g.buffer = b;
	g.buffer_state = 1;
}


static void params_failed(void *data, struct zwp_linux_buffer_params_v1 *p)
{
	(void)data;
	(void)p;
	g.buffer_state = -1;
}


static const struct zwp_linux_buffer_params_v1_listener params_listener = { params_created, params_failed };


/* sand-plane's picture in one SAND128 buffer: per 128-byte column, H16 luma lines then H16/2 chroma lines */
static void fill(uint8_t *buf, uint32_t cols, uint32_t colh, uint32_t h16)
{
	static const uint8_t bars[8][2] = {
		{ 128, 128 }, { 16, 138 }, { 166, 16 }, { 54, 34 }, { 202, 222 }, { 90, 240 }, { 240, 118 }, { 128, 128 }
	};
	uint32_t x, y;

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
}


int main(int argc, char *argv[])
{
	uint32_t secs = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0) : 8u;
	uint32_t h16 = (H + 15u) & ~15u, colh = h16 + h16 / 2u, cols = (W + 127u) / 128u;
	uint64_t mod = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(colh);
	struct zwp_linux_buffer_params_v1 *params;
	struct drm_mode_create_dumb cd;
	struct drm_mode_map_dumb md;
	struct wl_display *dpy;
	struct wl_surface *surface;
	struct xdg_surface *xs;
	struct xdg_toplevel *top;
	time_t end;
	uint8_t *buf;
	int card, fd;

	dpy = wl_display_connect(NULL);
	if (dpy == NULL) {
		printf("SANDWL result=FAIL no Wayland display (run inside the desktop session)\n");
		return 1;
	}
	wl_registry_add_listener(wl_display_get_registry(dpy), &registry_listener, NULL);
	wl_display_roundtrip(dpy);
	wl_display_roundtrip(dpy);   /* the dmabuf format/modifier events */
	if ((g.compositor == NULL) || (g.wm == NULL) || (g.dmabuf == NULL)) {
		printf("SANDWL result=FAIL missing globals compositor=%d xdg_wm_base=%d linux_dmabuf_v3=%d\n",
			g.compositor != NULL, g.wm != NULL, g.dmabuf != NULL);
		return 1;
	}
	if (!g.nv12_sand) {
		printf("SANDWL result=SKIP NV12 offered=%d, with SAND128=0: the renderer does not import it\n", g.nv12_any);
		return 2;
	}

	card = open("/dev/dri/card0", O_RDWR);
	if (card < 0) {
		printf("SANDWL result=FAIL open card0 errno=%d\n", errno);
		return 1;
	}
	memset(&cd, 0, sizeof(cd));
	cd.width = cols * 128u;
	cd.height = colh;
	cd.bpp = 8u;
	if (drmIoctl(card, DRM_IOCTL_MODE_CREATE_DUMB, &cd) != 0) {
		printf("SANDWL result=FAIL create_dumb errno=%d\n", errno);
		return 1;
	}
	memset(&md, 0, sizeof(md));
	md.handle = cd.handle;
	if (drmIoctl(card, DRM_IOCTL_MODE_MAP_DUMB, &md) != 0) {
		printf("SANDWL result=FAIL map_dumb errno=%d\n", errno);
		return 1;
	}
	buf = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, card, (off_t)md.offset);
	if (buf == MAP_FAILED) {
		printf("SANDWL result=FAIL mmap errno=%d\n", errno);
		return 1;
	}
	fill(buf, cols, colh, h16);
	if (drmPrimeHandleToFD(card, cd.handle, DRM_CLOEXEC, &fd) != 0) {
		printf("SANDWL result=FAIL prime export errno=%d\n", errno);
		return 1;
	}

	surface = wl_compositor_create_surface(g.compositor);
	xs = xdg_wm_base_get_xdg_surface(g.wm, surface);
	xdg_surface_add_listener(xs, &surface_listener, NULL);
	top = xdg_surface_get_toplevel(xs);
	xdg_toplevel_set_title(top, "sand-wl");
	wl_surface_commit(surface);
	while (!g.configured && (wl_display_dispatch(dpy) >= 0)) {
	}

	/* Plane strides as sand-plane gives them to ADDFB2; the modifier carries the column height */
	params = zwp_linux_dmabuf_v1_create_params(g.dmabuf);
	zwp_linux_buffer_params_v1_add_listener(params, &params_listener, NULL);
	zwp_linux_buffer_params_v1_add(params, fd, 0, 0, W, (uint32_t)(mod >> 32), (uint32_t)mod);
	zwp_linux_buffer_params_v1_add(params, fd, 1, h16 * 128u, W, (uint32_t)(mod >> 32), (uint32_t)mod);
	zwp_linux_buffer_params_v1_create(params, (int32_t)W, (int32_t)H, DRM_FORMAT_NV12, 0);
	while ((g.buffer_state == 0) && (wl_display_dispatch(dpy) >= 0)) {
	}
	if (g.buffer_state < 0) {
		printf("SANDWL result=FAIL the compositor refused the SAND128 NV12 buffer (params failed)\n");
		return 1;
	}
	printf("SANDWL buffer created %ux%u modifier 0x%016llx\n", W, H, (unsigned long long)mod);

	wl_surface_attach(surface, g.buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, (int32_t)W, (int32_t)H);
	wl_surface_commit(surface);
	fflush(stdout);
	end = time(NULL) + (time_t)secs;
	while (time(NULL) < end) {
		if (wl_display_roundtrip(dpy) < 0) {
			printf("SANDWL result=FAIL protocol error %d while shown\n", wl_display_get_error(dpy));
			return 1;
		}
		usleep(200000);
	}
	printf("SANDWL result=PASS shown %u s (judge the HDMI snapshot: ramp + eight bars, no stripes)\n", secs);
	return 0;
}
