/*
 * harness_screen_create.c — Path-C Phase-2 first-runtime-contact harness.
 *
 * The first point Mesa's v3d gallium driver code runs against our Phoenix winsys
 * is screen creation: v3d_screen_create(fd,...) issues GET_PARAM ioctls (→ our
 * phoenix_v3d_ioctl → real V3D-4.2 IDENT values) to populate devinfo and build a
 * pipe_screen. We bypass the gallium pipe-loader (it dlopen()s pipe_*.so — a
 * dead-end on Phoenix) and call v3d_screen_create directly with a fake fd.
 *
 * This proves link + winsys dispatch + driver init TOGETHER, cheaply, before any
 * triangle/CL/shader work (which stacks on verified ground). Per the routing doc:
 * each HW cycle should test exactly one new thing.
 *
 * PRECONDITION (HW): the V3D must be powered on (scout v3d_powerOn) before the
 * winsys touches MMIO — wired in the on-device variant, not this link/smoke build.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stdio.h>
#include "pipe/p_screen.h"
#include "pipe/p_defines.h"

/* Forward-declare v3d_screen_create rather than #include "v3d_screen.h": that header
 * drags in renderonly.h -> c11/threads.h -> c11/time.h, which redefines struct timespec
 * and clashes with glibc when this harness is also built on the host (x86) for the
 * MMIO-free screen-create validation. The prototype is stable. */
struct pipe_screen_config;
struct renderonly;
struct pipe_screen *v3d_screen_create(int fd,
                                      const struct pipe_screen_config *config,
                                      struct renderonly *ro);

int main(void)
{
	/* Unbuffered: a fault must not eat buffered output over the captured UART path. */
	setvbuf(stdout, NULL, _IONBF, 0);
	/* Markers make the one HW cycle crash-interpretable: no output => ELF didn't load;
	 * marker-only => faulted INSIDE screen_create (fence/program/caps/util_queue init,
	 * the first exercise of the mtx_/call_once/syncobj stubs); "NULL" => clean null. */
	printf("harness: entering v3d_screen_create\n");
	/* fd is a token routed to phoenix_v3d_ioctl by the libdrm shim; value unused. */
	struct pipe_screen *pscreen = v3d_screen_create(0, NULL, NULL);
	if (pscreen == NULL) {
		printf("v3d_screen_create: NULL\n");
		return 1;
	}
	const char *name = pscreen->get_name ? pscreen->get_name(pscreen) : "(no get_name)";
	const char *vendor = pscreen->get_vendor ? pscreen->get_vendor(pscreen) : "(no vendor)";
	printf("v3d pipe_screen OK: name=%s vendor=%s\n", name, vendor);

	pscreen->destroy(pscreen);
	printf("v3d screen-create harness: PASS\n");
	return 0;
}
