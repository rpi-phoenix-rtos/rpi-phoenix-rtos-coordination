/*
 * test_ident_decode.c — HOST (x86) validation of the GET_PARAM IDENT decode.
 *
 * v3d_screen_create is GET_PARAM-only (allocates no BO), and it gates entirely on
 * v3d_get_device_info() decoding our hardcoded IDENT values into devinfo. That decode
 * is arch-neutral C, so we can validate the single most-likely screen-create failure
 * point on the host with NO Pi cycle: feed v3d_get_device_info a fake ioctl returning
 * the winsys's hardcoded IDENTs and assert ver==42 + sane vpm/qpu.
 *
 * Build+run (host x86), from /tmp/mesa-v3d-build:
 *   MESA=.../external/mesa; INC="-Isrc -I$MESA/src -I$MESA/include -I$MESA/src/broadcom
 *     -Isrc/broadcom -I$MESA/src/util -Isrc/util"; DEF="-DUTIL_ARCH_LITTLE_ENDIAN=1
 *     -DUTIL_ARCH_BIG_ENDIAN=0"
 *   gcc -std=c11 $INC $DEF -c test_ident_decode.c $MESA/src/broadcom/common/v3d_device_info.c
 *   # + a tiny shim providing os_get_page_size() and mesa_log(); link; run.
 * Verified 2026-06-11: ver=42 vpm_size=65536 qpu_count=8 -> "IDENT decode: PASS".
 */
#include <stdio.h>
#include <string.h>
#include "drm-uapi/v3d_drm.h"
#include "broadcom/common/v3d_device_info.h"

/* The exact values v3d_phoenix_winsys.c:ioc_get_param returns for the real Pi4 V3D 4.2. */
static int fake_ioctl(int fd, unsigned long request, void *arg)
{
	(void)fd; (void)request;
	struct drm_v3d_get_param *gp = arg;
	switch (gp->param) {
	case DRM_V3D_PARAM_V3D_CORE0_IDENT0: gp->value = 0x04443356; return 0;
	case DRM_V3D_PARAM_V3D_CORE0_IDENT1: gp->value = 0x81001422; return 0;
	case DRM_V3D_PARAM_V3D_CORE0_IDENT2: gp->value = 0x40078121; return 0;
	case DRM_V3D_PARAM_V3D_HUB_IDENT1:   gp->value = 0x000e1124; return 0;
	case DRM_V3D_PARAM_V3D_HUB_IDENT2:   gp->value = 0x00000100; return 0;
	case DRM_V3D_PARAM_V3D_HUB_IDENT3:   gp->value = 0x00000e00; return 0;
	case DRM_V3D_PARAM_V3D_UIFCFG:       gp->value = 0x00000045; return 0;
	default:                             gp->value = 0;          return 0;
	}
}

int main(void)
{
	struct v3d_device_info devinfo;
	memset(&devinfo, 0, sizeof(devinfo));
	if (!v3d_get_device_info(0, &devinfo, &fake_ioctl)) {
		printf("FAIL: v3d_get_device_info returned false\n");
		return 1;
	}
	printf("devinfo: ver=%d vpm_size=%d qpu_count=%d rev=%d\n",
	       devinfo.ver, devinfo.vpm_size, devinfo.qpu_count, devinfo.rev);
	int ok = 1;
	if (devinfo.ver != 42)       { printf("FAIL: ver != 42\n"); ok = 0; }
	if (devinfo.vpm_size != 65536) { printf("FAIL: vpm_size != 65536\n"); ok = 0; }
	if (devinfo.qpu_count != 8)  { printf("FAIL: qpu_count != 8\n"); ok = 0; }
	printf(ok ? "IDENT decode: PASS (V3D 4.2, vpm=64K, 8 QPUs)\n"
	          : "IDENT decode: FAIL\n");
	return ok ? 0 : 1;
}
