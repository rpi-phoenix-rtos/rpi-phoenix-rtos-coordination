/*
 * quake2 — launcher for yQuake2 on Phoenix-RTOS / RPi4.
 *
 * The baseq2 assets live on the (slow) NFS root at /usr/share/quake2; loading
 * textures directly over NFS is so slow the demo never paints (owner HW test:
 * black screen). So we RAM-stage first: exec ram-stage-play, which copies the
 * asset tree into the /tmp tmpfs (RAM) and then execs the engine reading from
 * RAM — the demo then renders in full textured 3D on the V3D GPU (HW-verified).
 * Forwards any extra user args. Install as /usr/bin/quake2.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	/* ram-stage-play <src> <dst> <exec> [exec-args...] */
	static char *base[] = {
		"ram-stage-play", "/usr/share/quake2", "/tmp/quake2",
		"/usr/bin/yquake2", "-datadir", "/tmp/quake2",
	};
	const int nbase = (int)(sizeof(base) / sizeof(base[0]));
	char **a = calloc((size_t)(nbase + argc + 1), sizeof(char *));
	int i, n = 0;

	if (a == NULL) { fprintf(stderr, "quake2: out of memory\n"); return 1; }
	for (i = 0; i < nbase; i++) { a[n++] = base[i]; }
	for (i = 1; i < argc; i++) { a[n++] = argv[i]; }
	a[n] = NULL;

	execvp("ram-stage-play", a);  /* PATH search: it lives in /bin */
	perror("quake2: exec /usr/bin/ram-stage-play");
	return 1;
}
