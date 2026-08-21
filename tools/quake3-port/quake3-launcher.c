/*
 * quake3 — launcher for the quake3e engine on Phoenix-RTOS / RPi4.
 *
 * Demo data lives on the slow NFS root at /usr/share/quake3; loading over NFS is
 * slow (owner HW test). RAM-stage first via ram-stage-play (copy the asset tree
 * into the /tmp tmpfs, then exec the engine reading from RAM) so load is fast.
 * fs_basepath points at the RAM copy; fs_game=demoq3 (the demo paks). Forwards
 * extra user args. Install as /usr/bin/quake3.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	static char *base[] = {
		"ram-stage-play", "/usr/share/quake3", "/tmp/quake3",
		"/usr/bin/quake3e", "+set", "fs_basepath", "/tmp/quake3", "+set", "fs_game", "demoq3",
		/* r_mergeLightmaps: quake3e's default (1, merged POT lightmap atlas) is now used.
		 * The old r_mergeLightmaps=0 workaround (individual 128x128 lightmaps) is REMOVED:
		 * the underlying V3D bug is fixed. A large (>=1024x768) merged atlas was being laid
		 * out RASTER instead of UIF_XOR — the Phoenix should_tile optimization forced large
		 * RENDER_TARGET textures to linear for fast glReadPixels, but Mesa marks renderable
		 * RGBA8 SAMPLED textures RENDER_TARGET too, so the sampled atlas matched and the TMU
		 * read linear-as-UIF → black sectors (q3dm7). Fixed in libv3d by excluding
		 * SAMPLER_VIEW from that gate (external/mesa 4363822955b); HW-confirmed the 1024 atlas
		 * is now UIF_XOR and q3dm7 renders equivalently to the individual-lightmap path.
		 * See memory project_quake3_lightmap_uif_xor + docs .../2026-08-21-v3d-uif-xor-1024-redirect.md. */
	};
	const int nbase = (int)(sizeof(base) / sizeof(base[0]));
	char **a = calloc((size_t)(nbase + argc + 1), sizeof(char *));
	int i, n = 0;

	if (a == NULL) { fprintf(stderr, "quake3: out of memory\n"); return 1; }
	for (i = 0; i < nbase; i++) { a[n++] = base[i]; }
	for (i = 1; i < argc; i++) { a[n++] = argv[i]; }
	a[n] = NULL;

	execvp("ram-stage-play", a);  /* PATH search: it lives in /bin */
	perror("quake3: exec /usr/bin/ram-stage-play");
	return 1;
}
