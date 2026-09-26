/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix - host-side test of the pure marshalling logic
 *
 * Builds natively (gcc -fsanitize=address,undefined) against the patched libdrm
 * headers and drm_phoenix_logic.c; no Pi, no IPC. See run.sh.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drm_phoenix_logic.h"


static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


/* The committed state the baseline callback hands back. */
static kms_atomic_plane_t cur[16];
static int baseline_calls, baseline_err;

static int baseline(void *ctx, uint32_t plane_id, kms_atomic_plane_t *st)
{
	(void)ctx;
	baseline_calls++;
	if (baseline_err != 0) {
		return baseline_err;
	}
	*st = cur[plane_id - KMS_ID_PLANE(0u, 0u)];
	return 0;
}


static void t_kinds(void)
{
	CHECK(drmphx_obj_kind(0x20) == DRMPHX_OBJ_CONNECTOR);
	CHECK(drmphx_obj_kind(0x21) == DRMPHX_OBJ_CONNECTOR);
	CHECK(drmphx_obj_kind(0x22) == DRMPHX_OBJ_NONE);
	CHECK(drmphx_obj_kind(0x30) == DRMPHX_OBJ_ENCODER);
	CHECK(drmphx_obj_kind(0x40) == DRMPHX_OBJ_CRTC);
	CHECK(drmphx_obj_kind(0x41) == DRMPHX_OBJ_CRTC);
	CHECK(drmphx_obj_kind(0x42) == DRMPHX_OBJ_NONE);
	CHECK(drmphx_obj_kind(0x50) == DRMPHX_OBJ_PLANE);
	CHECK(drmphx_obj_kind(0x5f) == DRMPHX_OBJ_PLANE);
	CHECK(drmphx_obj_kind(0x60) == DRMPHX_OBJ_NONE);
	CHECK(drmphx_obj_kind(0x1000) == DRMPHX_OBJ_NONE);
}


static void t_ioc_and_tokens(void)
{
	/* The number/group are identical in both _IOC layouts; this host builds the Linux one. */
	unsigned long phoenix_atomic = 0xc0000000ul | ((unsigned long)sizeof(struct drm_mode_atomic) << 16) |
		((unsigned long)'d' << 8) | 0xbcul;
	CHECK(DRMPHX_IOC_NR(DRM_IOCTL_MODE_ATOMIC) == 0xbcu);
	CHECK(DRMPHX_IOC_NR(phoenix_atomic) == 0xbcu);
	CHECK(DRMPHX_IOC_TYPE(DRM_IOCTL_MODE_ATOMIC) == 'd');
	CHECK(DRMPHX_IOC_TYPE(phoenix_atomic) == 'd');
	CHECK(DRMPHX_IOC_NR(DRM_IOCTL_VERSION) == 0x00u);
	CHECK(DRMPHX_IOC_NR(DRM_IOCTL_GEM_CLOSE) == 0x09u);

	CHECK(DRMPHX_TOKEN_OK(DRMPHX_TOKEN(1u)));
	CHECK(DRMPHX_TOKEN_OK(DRMPHX_TOKEN(0xffffffffu)));
	CHECK(DRMPHX_TOKEN_HANDLE(DRMPHX_TOKEN(0x2001u)) == 0x2001u);
	CHECK(DRMPHX_TOKEN_HANDLE(DRMPHX_TOKEN(0xfffffffeu)) == 0xfffffffeu);
	CHECK((DRMPHX_TOKEN(7u) & 0xfffu) == 0u);   /* page aligned, as mmap() demands */
	CHECK(!DRMPHX_TOKEN_OK(0));
	CHECK(!DRMPHX_TOKEN_OK(0x1000));
	CHECK(!DRMPHX_TOKEN_OK(DRMPHX_TOKEN(1u) | 0x10u));
}


/* kmscube's first atomic commit: connector CRTC_ID, CRTC MODE_ID + ACTIVE, and a
 * complete primary plane. */
static void t_atomic_full(void)
{
	uint32_t objs[3] = { 0x20, 0x40, 0x50 }, cnt[3] = { 1, 2, 10 };
	uint32_t props[13] = { KMS_PROP_CRTC_ID, KMS_PROP_MODE_ID, KMS_PROP_ACTIVE, KMS_PROP_FB_ID, KMS_PROP_CRTC_ID,
		KMS_PROP_SRC_X, KMS_PROP_SRC_Y, KMS_PROP_SRC_W, KMS_PROP_SRC_H, KMS_PROP_CRTC_X, KMS_PROP_CRTC_Y, KMS_PROP_CRTC_W,
		KMS_PROP_CRTC_H };
	uint64_t vals[13] = { 0x40, 0x10005, 1, 0x1000, 0x40, 0, 0, 1920ull << 16, 1080ull << 16, 0, 0, 1920, 1080 };
	drmphx_atomic_t a;

	memset(cur, 0, sizeof(cur));
	baseline_calls = 0;
	CHECK(drmphx_atomic_flatten(objs, cnt, props, vals, 3, baseline, NULL, &a) == 0);
	CHECK(a.crtc_id == 0x40);
	CHECK(a.crtc_named == 1);
	CHECK(a.mode_blob == 0x10005);
	CHECK(a.active == 1);
	CHECK(a.nplanes == 1);
	CHECK(baseline_calls == 1);
	CHECK(a.st[0].plane_id == 0x50);
	CHECK(a.st[0].fb_id == 0x1000);
	CHECK(a.st[0].crtc_id == 0x40);
	CHECK(a.st[0].src_w == (1920u << 16));
	CHECK(a.st[0].src_h == (1080u << 16));
	CHECK(a.st[0].crtc_w == 1920);
	CHECK(a.st[0].crtc_h == 1080);
	CHECK(a.in_fence_fd[0] == -1);
	CHECK(a.out_fence_ptr == 0);
	CHECK(a.st[0].in_fence.seqno == 0);
}


/* A page flip as a partial atomic request: only FB_ID (+ an in-fence fd). The
 * rest must come from the baseline. */
static void t_atomic_partial(void)
{
	uint32_t objs[1] = { 0x50 }, cnt[1] = { 2 }, props[2] = { KMS_PROP_FB_ID, KMS_PROP_IN_FENCE_FD };
	uint64_t vals[2] = { 0x1001, 7 };
	drmphx_atomic_t a;

	memset(cur, 0, sizeof(cur));
	cur[0].plane_id = 0x50;
	cur[0].fb_id = 0x1000;
	cur[0].crtc_id = 0x40;
	cur[0].crtc_w = 1920;
	cur[0].crtc_h = 1080;
	cur[0].src_w = 1920u << 16;
	cur[0].src_h = 1080u << 16;
	cur[0].alpha = 0x8000;
	cur[0].rotation = 1;
	cur[0].zpos = 0;
	cur[0].in_fence.seqno = 99;   /* a stale fence in the mirror must never carry over */
	CHECK(drmphx_atomic_flatten(objs, cnt, props, vals, 1, baseline, NULL, &a) == 0);
	CHECK(a.nplanes == 1);
	CHECK(a.crtc_named == 0);
	CHECK(a.crtc_id == 0x40);   /* from the plane's CRTC_ID */
	CHECK(a.st[0].fb_id == 0x1001);
	CHECK(a.st[0].crtc_w == 1920);
	CHECK(a.st[0].src_h == (1080u << 16));
	CHECK(a.st[0].alpha == 0x8000);
	CHECK(a.st[0].in_fence.seqno == 0);
	CHECK(a.in_fence_fd[0] == 7);
	CHECK(a.active == 1);
	CHECK(a.mode_blob == 0);
}


static void t_atomic_errors(void)
{
	drmphx_atomic_t a;
	uint32_t o1[1] = { 0x50 }, c1[1] = { 1 }, p_type[1] = { KMS_PROP_TYPE }, p_bad[1] = { 0x999 };
	uint64_t v1[1] = { 1 };
	uint32_t o_unknown[1] = { 0x99 };
	uint32_t o2[2] = { 0x40, 0x41 }, c2[2] = { 1, 1 }, p2[2] = { KMS_PROP_ACTIVE, KMS_PROP_ACTIVE };
	uint64_t v2[2] = { 1, 1 };
	uint32_t o9[9], c9[9], p9[9];
	uint64_t v9[9];
	uint32_t o_mix[2] = { 0x40, 0x50 }, c_mix[2] = { 1, 1 }, p_mix[2] = { KMS_PROP_ACTIVE, KMS_PROP_CRTC_ID };
	uint64_t v_mix[2] = { 1, 0x41 };
	uint32_t o_alpha[1] = { 0x51 }, p_alpha[1] = { KMS_PROP_ALPHA };
	uint64_t v_alpha[1] = { 0x10000 };
	uint32_t o_crtcid[1] = { 0x50 }, p_crtcid[1] = { KMS_PROP_CRTC_ID };
	uint64_t v_crtcid[1] = { 0x50 };   /* a plane id where a CRTC id belongs */
	uint32_t i;

	memset(cur, 0, sizeof(cur));
	baseline_err = 0;
	CHECK(drmphx_atomic_flatten(o1, c1, p_type, v1, 1, baseline, NULL, &a) == -EINVAL);   /* immutable */
	CHECK(drmphx_atomic_flatten(o1, c1, p_bad, v1, 1, baseline, NULL, &a) == -ENOENT);    /* unknown prop */
	CHECK(drmphx_atomic_flatten(o_unknown, c1, p_type, v1, 1, baseline, NULL, &a) == -ENOENT);
	CHECK(drmphx_atomic_flatten(o2, c2, p2, v2, 2, baseline, NULL, &a) == -EINVAL);       /* two CRTCs */
	for (i = 0; i < 9; i++) {
		o9[i] = 0x50 + i;
		c9[i] = 1;
		p9[i] = KMS_PROP_FB_ID;
		v9[i] = 0;
	}
	CHECK(drmphx_atomic_flatten(o9, c9, p9, v9, 8, baseline, NULL, &a) == 0);
	CHECK(a.nplanes == 8);
	CHECK(drmphx_atomic_flatten(o9, c9, p9, v9, 9, baseline, NULL, &a) == -E2BIG);
	CHECK(drmphx_atomic_flatten(o_mix, c_mix, p_mix, v_mix, 2, baseline, NULL, &a) == -EINVAL);   /* plane on another CRTC */
	CHECK(drmphx_atomic_flatten(o_alpha, c1, p_alpha, v_alpha, 1, baseline, NULL, &a) == -EINVAL);
	CHECK(drmphx_atomic_flatten(o_crtcid, c1, p_crtcid, v_crtcid, 1, baseline, NULL, &a) == -EINVAL);
	baseline_err = -EIO;
	CHECK(drmphx_atomic_flatten(o1, c1, p9, v9, 1, baseline, NULL, &a) == -EIO);   /* callback error propagates */
	baseline_err = 0;
}


static void t_atomic_crtc_only(void)
{
	uint32_t objs[1] = { 0x40 }, cnt[1] = { 2 }, props[2] = { KMS_PROP_ACTIVE, KMS_PROP_OUT_FENCE_PTR };
	uint64_t vals[2] = { 0, 0xdead0000 };
	uint32_t o0[1] = { 0x50 }, c0[1] = { 0 };
	drmphx_atomic_t a;

	baseline_calls = 0;
	CHECK(drmphx_atomic_flatten(objs, cnt, props, vals, 1, baseline, NULL, &a) == 0);
	CHECK(a.nplanes == 0);
	CHECK(a.active == 0);
	CHECK(a.out_fence_ptr == 0xdead0000);
	CHECK(a.crtc_id == 0x40);
	CHECK(baseline_calls == 0);
	/* an object listed with no properties still names its plane */
	CHECK(drmphx_atomic_flatten(o0, c0, NULL, NULL, 1, baseline, NULL, &a) == 0);
	CHECK(a.nplanes == 1);
	CHECK(a.crtc_id == KMS_ID_CRTC(0u));
}


static void t_plane_props(void)
{
	kms_prop_value_t pv[6] = {
		{ KMS_PROP_TYPE, 0, 1 }, { KMS_PROP_FB_ID, 0, 0x1003 }, { KMS_PROP_CRTC_X, 0, (uint64_t)(int64_t)-5 },
		{ KMS_PROP_SRC_W, 0, 640u << 16 }, { KMS_PROP_ROTATION, 0, 4 }, { KMS_PROP_IN_FENCE_FD, 0, (uint64_t)(int64_t)-1 } };
	kms_atomic_plane_t st;

	memset(&st, 0, sizeof(st));
	st.alpha = 0x1234;
	drmphx_plane_state_from_props(0x57, pv, 6, &st);
	CHECK(st.plane_id == 0x57);
	CHECK(st.fb_id == 0x1003);
	CHECK(st.crtc_x == -5);
	CHECK(st.src_w == (640u << 16));
	CHECK(st.rotation == 4);
	CHECK(st.alpha == 0x1234);   /* not listed: kept */
}


static void t_property(void)
{
	kms_property_t p;
	kms_prop_enum_t en[3] = { { 0, "Overlay" }, { 1, "Primary" }, { 2, "Cursor" } };
	uint64_t range[2] = { 0, 0xffff }, vals[4];
	struct drm_mode_property_enum eb[4];
	struct drm_mode_get_property gp;

	memset(&p, 0, sizeof(p));
	strcpy(p.name, "type");
	p.flags = KMS_PROP_FLAG_ENUM | KMS_PROP_FLAG_IMMUTABLE;
	p.nenums = 3;
	p.nvalues = 3;

	memset(&gp, 0, sizeof(gp));   /* first call: counts only */
	drmphx_fill_property(&p, en, sizeof(en), &gp);
	CHECK(strcmp(gp.name, "type") == 0);
	CHECK(gp.flags == (DRM_MODE_PROP_ENUM | DRM_MODE_PROP_IMMUTABLE));
	CHECK(gp.count_values == 3);
	CHECK(gp.count_enum_blobs == 3);

	memset(&gp, 0, sizeof(gp));
	memset(vals, 0xee, sizeof(vals));
	memset(eb, 0xee, sizeof(eb));
	gp.values_ptr = (uintptr_t)vals;
	gp.enum_blob_ptr = (uintptr_t)eb;
	gp.count_values = 3;
	gp.count_enum_blobs = 3;
	drmphx_fill_property(&p, en, sizeof(en), &gp);
	CHECK(vals[0] == 0 && vals[1] == 1 && vals[2] == 2);
	CHECK(vals[3] == 0xeeeeeeeeeeeeeeeeull);   /* nothing past the capacity */
	CHECK(eb[1].value == 1 && strcmp(eb[1].name, "Primary") == 0);
	CHECK(eb[3].value == 0xeeeeeeeeeeeeeeeeull);

	memset(&gp, 0, sizeof(gp));   /* capacity 1: one value, one enum */
	memset(vals, 0xee, sizeof(vals));
	gp.values_ptr = (uintptr_t)vals;
	gp.enum_blob_ptr = (uintptr_t)eb;
	gp.count_values = 1;
	gp.count_enum_blobs = 1;
	drmphx_fill_property(&p, en, sizeof(en), &gp);
	CHECK(vals[0] == 0 && vals[1] == 0xeeeeeeeeeeeeeeeeull);
	CHECK(gp.count_values == 3);

	memset(&p, 0, sizeof(p));
	strcpy(p.name, "alpha");
	p.flags = KMS_PROP_FLAG_RANGE;
	p.nvalues = 2;
	memset(&gp, 0, sizeof(gp));
	gp.values_ptr = (uintptr_t)vals;
	gp.count_values = 2;
	drmphx_fill_property(&p, range, sizeof(range), &gp);
	CHECK(gp.count_values == 2 && gp.count_enum_blobs == 0);
	CHECK(vals[0] == 0 && vals[1] == 0xffff);

	p.flags = KMS_PROP_FLAG_OBJECT | KMS_PROP_FLAG_ATOMIC;
	p.nvalues = 1;
	range[0] = KMS_OBJ_FB;
	memset(&gp, 0, sizeof(gp));
	gp.values_ptr = (uintptr_t)vals;
	gp.count_values = 4;
	drmphx_fill_property(&p, range, sizeof(range), &gp);
	CHECK(gp.count_values == 1 && vals[0] == KMS_OBJ_FB);
	CHECK(gp.flags == (DRM_MODE_PROP_OBJECT | DRM_MODE_PROP_ATOMIC));

	p.flags = KMS_PROP_FLAG_BLOB | KMS_PROP_FLAG_IMMUTABLE;
	p.nvalues = 0;
	memset(&gp, 0, sizeof(gp));
	drmphx_fill_property(&p, NULL, 0, &gp);
	CHECK(gp.count_values == 0 && gp.count_enum_blobs == 0);

	/* flag values are DRM's own */
	CHECK(KMS_PROP_FLAG_RANGE == DRM_MODE_PROP_RANGE);
	CHECK(KMS_PROP_FLAG_IMMUTABLE == DRM_MODE_PROP_IMMUTABLE);
	CHECK(KMS_PROP_FLAG_ENUM == DRM_MODE_PROP_ENUM);
	CHECK(KMS_PROP_FLAG_BLOB == DRM_MODE_PROP_BLOB);
	CHECK(KMS_PROP_FLAG_BITMASK == DRM_MODE_PROP_BITMASK);
	CHECK(KMS_PROP_FLAG_OBJECT == DRM_MODE_PROP_OBJECT);
	CHECK(KMS_PROP_FLAG_SIGNED == DRM_MODE_PROP_SIGNED_RANGE);
	CHECK(KMS_PROP_FLAG_ATOMIC == DRM_MODE_PROP_ATOMIC);
}


static void t_layouts(void)
{
	/* The server's copies of DRM structs must match DRM's byte for byte. */
	CHECK(sizeof(kms_modeinfo_t) == sizeof(struct drm_mode_modeinfo));
	CHECK(sizeof(kms_drm_event_vblank_t) == sizeof(struct drm_event_vblank));
	CHECK(sizeof(kms_drm_event_crtc_sequence_t) == sizeof(struct drm_event_crtc_sequence));
	CHECK(sizeof(kms_prop_enum_t) == sizeof(struct drm_mode_property_enum));
	CHECK(KMS_DRM_EVENT_FLIP_COMPLETE == DRM_EVENT_FLIP_COMPLETE);
	CHECK(KMS_PAGE_FLIP_EVENT == DRM_MODE_PAGE_FLIP_EVENT);
	CHECK(KMS_ATOMIC_TEST_ONLY == DRM_MODE_ATOMIC_TEST_ONLY);
	CHECK(KMS_ATOMIC_NONBLOCK == DRM_MODE_ATOMIC_NONBLOCK);
	CHECK(KMS_ATOMIC_ALLOW_MODESET == DRM_MODE_ATOMIC_ALLOW_MODESET);
	CHECK(KMS_CAP_PRIME == DRM_CAP_PRIME);
	CHECK(KMS_CAP_CRTC_IN_VBLANK_EVENT == DRM_CAP_CRTC_IN_VBLANK_EVENT);
	CHECK(KMS_CLIENT_CAP_ATOMIC == DRM_CLIENT_CAP_ATOMIC);
	CHECK(KMS_CONNECTOR_HDMIA == DRM_MODE_CONNECTOR_HDMIA);
	/* The server's vblank type bit 0 means ABSOLUTE; DRM's means RELATIVE (the
	 * backend always sends an absolute target, so this only documents it). */
	CHECK(KMS_VBL_ABSOLUTE == 1u && _DRM_VBLANK_RELATIVE == 1u && _DRM_VBLANK_ABSOLUTE == 0u);
	CHECK(KMS_VBL_EVENT == _DRM_VBLANK_EVENT);
	CHECK(DRM_CRTC_SEQUENCE_RELATIVE == 1u && DRM_CRTC_SEQUENCE_NEXT_ON_MISS == 2u);
}


static void t_version(void)
{
	char name[4], date[16], desc[64];
	struct drm_version v;

	memset(&v, 0, sizeof(v));
	drmphx_fill_version(&v, 1, 0, 0, "v3d", "20260926", "Phoenix rpi4-v3d-async (V3D 4.2)");
	CHECK(v.version_major == 1 && v.name_len == 3 && v.date_len == 8 && v.desc_len == 32);
	memset(name, 'x', sizeof(name));
	v.name = name;
	v.name_len = 2;   /* truncation: copy 2, report 3 */
	v.date = date;
	v.date_len = sizeof(date);
	v.desc = desc;
	v.desc_len = sizeof(desc);
	drmphx_fill_version(&v, 1, 0, 0, "v3d", "20260926", "Phoenix rpi4-v3d-async (V3D 4.2)");
	CHECK(name[0] == 'v' && name[1] == '3' && name[2] == 'x');
	CHECK(v.name_len == 3);
	CHECK(memcmp(date, "20260926", 8) == 0);
}


/* libdrm's own drmFreeDevice/drmFreePlatformDevice, verbatim in effect: the
 * layout contract of drmphx_device_new is that THIS frees it completely. */
static void upstream_free_device(drmDevicePtr *device)
{
	char **compatible;

	if ((*device)->bustype == DRM_BUS_PLATFORM && (*device)->deviceinfo.platform &&
			(*device)->deviceinfo.platform->compatible) {
		compatible = (*device)->deviceinfo.platform->compatible;
		while (*compatible) {
			free(*compatible);
			compatible++;
		}
		free((*device)->deviceinfo.platform->compatible);
	}
	free(*device);
	*device = NULL;
}


static void t_device(void)
{
	static const char *const compat[] = { "brcm,2711-v3d", NULL };
	const char *nodes[DRM_NODE_MAX] = { "/dev/dri/card1", NULL, "/dev/dri/renderD128" };
	drmDevicePtr d = drmphx_device_new("/v3dbus/v3d@7ec04000", compat, nodes);

	CHECK(d != NULL);
	if (d == NULL) {
		return;
	}
	CHECK(d->bustype == DRM_BUS_PLATFORM);
	CHECK(d->available_nodes == ((1 << DRM_NODE_PRIMARY) | (1 << DRM_NODE_RENDER)));
	CHECK(strcmp(d->nodes[DRM_NODE_PRIMARY], "/dev/dri/card1") == 0);
	CHECK(strcmp(d->nodes[DRM_NODE_RENDER], "/dev/dri/renderD128") == 0);
	CHECK(d->nodes[DRM_NODE_CONTROL] != NULL && d->nodes[DRM_NODE_CONTROL][0] == '\0');
	CHECK(strcmp(d->businfo.platform->fullname, "/v3dbus/v3d@7ec04000") == 0);
	CHECK(strcmp(d->deviceinfo.platform->compatible[0], "brcm,2711-v3d") == 0);
	CHECK(d->deviceinfo.platform->compatible[1] == NULL);
	/* every pointer lies inside the one block except `compatible` (ASan checks the writes) */
	CHECK((char *)d->businfo.platform > (char *)d);
	upstream_free_device(&d);
	CHECK(d == NULL);
}


int main(void)
{
	t_kinds();
	t_ioc_and_tokens();
	t_atomic_full();
	t_atomic_partial();
	t_atomic_errors();
	t_atomic_crtc_only();
	t_plane_props();
	t_property();
	t_layouts();
	t_version();
	t_device();
	printf("HOSTTEST libdrm-phoenix checks=%d fails=%d verdict=%s\n", checks, fails, (fails == 0) ? "PASS" : "FAIL");
	return (fails == 0) ? 0 : 1;
}
