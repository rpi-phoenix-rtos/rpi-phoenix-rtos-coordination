/*
 * quake3 — launcher for the quake3e engine on Phoenix-RTOS / RPi4.
 *
 * quake3e searches its CWD for game data by default, so a bare `quake3e` fails
 * with "pak0.pk3 is missing". The demo data is staged at /usr/share/quake3/demoq3.
 * This launcher execs the engine with the correct fs_basepath/fs_game (mirroring
 * how quake1/vkQuake bake basedir=/usr/share/quake), and forwards any extra args
 * the user passes (e.g. `quake3 +map q3dm1`). Install as /usr/bin/quake3.
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
		"quake3e",
		"+set", "fs_basepath", "/usr/share/quake3",
		"+set", "fs_game", "demoq3",
	};
	const int nbase = (int)(sizeof(base) / sizeof(base[0]));
	char **a = calloc((size_t)(nbase + argc + 1), sizeof(char *));
	int i, n = 0;

	if (a == NULL) {
		fprintf(stderr, "quake3: out of memory\n");
		return 1;
	}
	for (i = 0; i < nbase; i++) {
		a[n++] = base[i];
	}
	for (i = 1; i < argc; i++) { /* forward user args after the defaults */
		a[n++] = argv[i];
	}
	a[n] = NULL;

	execv("/usr/bin/quake3e", a);
	perror("quake3: exec /usr/bin/quake3e");
	return 1;
}
