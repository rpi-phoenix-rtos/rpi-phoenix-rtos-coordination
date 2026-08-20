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
