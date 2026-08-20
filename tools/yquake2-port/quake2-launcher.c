/*
 * quake2 — launcher for the yQuake2 engine on Phoenix-RTOS / RPi4.
 *
 * yQuake2 can't determine its executable path on Phoenix ("Using ./ instead"),
 * so a bare `yquake2` looks for game data in the CWD and dies (GetPCXPalette:
 * Couldn't load pics/colormap.pcx). The baseq2 data is staged at
 * /usr/share/quake2/baseq2. This launcher execs the engine with -datadir set to
 * that location (mirroring how quake1/vkQuake bake basedir=/usr/share/quake) and
 * forwards any extra args (e.g. `quake2 +playdemo demo1`). Install as /usr/bin/quake2.
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
		"yquake2",
		"-datadir", "/usr/share/quake2",
	};
	const int nbase = (int)(sizeof(base) / sizeof(base[0]));
	char **a = calloc((size_t)(nbase + argc + 1), sizeof(char *));
	int i, n = 0;

	if (a == NULL) {
		fprintf(stderr, "quake2: out of memory\n");
		return 1;
	}
	for (i = 0; i < nbase; i++) {
		a[n++] = base[i];
	}
	for (i = 1; i < argc; i++) { /* forward user args after the defaults */
		a[n++] = argv[i];
	}
	a[n] = NULL;

	execv("/usr/bin/yquake2", a);
	perror("quake2: exec /usr/bin/yquake2");
	return 1;
}
