/*
 * stk — launcher for SuperTuxKart on Phoenix-RTOS / RPi4.
 *
 * psh cannot set environment variables, and SuperTuxKart locates its game data
 * and writes its user config through env vars, so a small launcher binary wires
 * those up before exec'ing the engine:
 *   - SUPERTUXKART_DATADIR    -> /usr/share/supertuxkart  (STK reads $DATADIR/data/)
 *   - SUPERTUXKART_SAVEDIR    -> /tmp/stk                 (writable config dir; RAM)
 *   - SUPERTUXKART_ASSETS_DIR -> /usr/share/supertuxkart/stk-assets  (art root)
 * The art assets (karts/tracks/textures/models/music/sfx/library) live in a
 * separate stk-assets root, not in data/. STK's file_manager adds both DATADIR/
 * data/ and ASSETS_DIR as root dirs and resolves each subdir from the first root
 * that has it (see discoverPaths()); data/ supplies gui/shaders/ttf/configs,
 * stk-assets/ supplies the art. We stage the 1.4 mobile-reduced asset set
 * (~149 MB, version-locked to stk-code 1.4) there. The default would be
 * DATADIR/../../stk-assets; we set it explicitly to avoid relying on `../..`
 * path resolution over NFS.
 *
 * Video args mirror the Quake launchers: the Phoenix /dev/fb0 is 1920x1080-only,
 * so force --screensize=1920x1080 --fullscreen. Any extra user args are appended
 * after and win (e.g. `stk --disable-addons`). Install as /bin/stk.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
	/* STK writes its config/players/hardware-detection files into SAVEDIR; make
	 * it exist and be writable first (tmpfs → RAM). EEXIST is fine. */
	if (mkdir("/tmp/stk", 0777) != 0 && errno != EEXIST) {
		fprintf(stderr, "stk: mkdir /tmp/stk: %s\n", strerror(errno));
		return 1;
	}

	if (setenv("SUPERTUXKART_DATADIR", "/usr/share/supertuxkart", 1) != 0 ||
			setenv("SUPERTUXKART_SAVEDIR", "/tmp/stk", 1) != 0 ||
			setenv("SUPERTUXKART_ASSETS_DIR", "/usr/share/supertuxkart/stk-assets", 1) != 0) {
		fprintf(stderr, "stk: setenv failed: %s\n", strerror(errno));
		return 1;
	}

	static char *base[] = {
		"supertuxkart",
		"--screensize=1920x1080",
		"--fullscreen",
		/*
		 * --disable-texture-compression is essential on this port: with it ON,
		 * KartPropertiesManager::loadAllKarts() compresses every kart texture
		 * with libsquish and writes a .sptz cache file at startup (SPTexture,
		 * sp_texture.cpp), which on the Pi4 costs ~200 s per kart (~18 s per
		 * texture) and blocks the main menu for the better part of an hour.
		 * Disabling compression early-returns that whole path (sp_texture.cpp
		 * gates on CVS->isTextureCompressionEnabled()); textures load
		 * uncompressed (~4x GPU/RAM, fine at 1080p) and the menu comes up
		 * promptly. NB: --disable-hd-textures alone is NOT enough — it only
		 * moves the cache from hd/ to resized_N/ and still compresses.
		 */
		"--disable-texture-compression",
		/* Skip scanning the (empty) addon dirs on every boot. */
		"--disable-addon-karts",
		"--disable-addon-tracks",
	};
	const int nbase = (int)(sizeof(base) / sizeof(base[0]));
	char **a = calloc((size_t)(nbase + argc + 1), sizeof(char *));
	int i, n = 0;

	if (a == NULL) {
		fprintf(stderr, "stk: out of memory\n");
		return 1;
	}
	for (i = 0; i < nbase; i++) {
		a[n++] = base[i];
	}
	for (i = 1; i < argc; i++) {
		a[n++] = argv[i]; /* user args appended after → they win */
	}
	a[n] = NULL;

	execv("/usr/bin/supertuxkart", a);
	perror("stk: exec /usr/bin/supertuxkart");
	return 1;
}
