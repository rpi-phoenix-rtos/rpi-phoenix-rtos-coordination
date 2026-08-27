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
 * SAVEDIR is /tmp (RAM), so it is wiped every boot and STK sees a "first run"
 * each time: with no saved player profile PlayerManager::getCurrentPlayer()
 * returns NULL, so main() pushes the UserScreen/RegisterScreen and the
 * internet-permission dialog instead of the main menu (see stk-code
 * src/main.cpp, the getCurrentPlayer()/m_always_show_login_screen branch, and
 * askForInternetPermission()). To boot straight to the menu we seed two files
 * into SAVEDIR before launching, unless they already exist (so a future
 * persistent SAVEDIR keeps its real profile):
 *   - players.xml : one local non-guest player "Player" marked <current>, which
 *                   is exactly what makes getCurrentPlayer() non-NULL.
 *   - config.xml  : version 8 (the version stk-code 1.4 expects; an older
 *                   number is deleted and regenerated) with enable_internet=2
 *                   (IPERM_NOT_ALLOWED), which suppresses the internet dialog.
 * The files are embedded below rather than installed to a data path, so the
 * launcher stays self-contained. If a seed write fails we still launch: the
 * worst case is STK showing its first-run screens, which beats not starting.
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

/*
 * Minimal players.xml: PlayerManager::load() reads the <current player="..."/>
 * node and matches it by name against the <player> list to set the current
 * player. The four <player> attributes are all required — the XMLNode ctor
 * leaves name/guest/use-frequency/unique-id uninitialised if the attribute is
 * absent. **use-frequency="1" (not 0) suppresses the first-run "Would you like to
 * play the tutorial?" modal**: MainMenuScreen::onUpdate (main_menu_screen.cpp:250)
 * shows it only "when profile is newly created", i.e. `getUseFrequency()==0`, then
 * increments it — so a seeded non-zero use-frequency skips the modal and STK boots
 * straight to a clean, immediately-usable main menu. Story-mode/achievements child
 * nodes are omitted; STK creates them fresh on load.
 */
static const char SEED_PLAYERS_XML[] =
	"<?xml version=\"1.0\"?>\n"
	"<players version=\"1\" >\n"
	"    <current player=\"Player\"/>\n"
	"    <player name=\"Player\" guest=\"false\" use-frequency=\"1\" unique-id=\"1\"/>\n"
	"</players>\n";

/*
 * Minimal config.xml: the version must be exactly 8 (stk-code 1.4's
 * m_current_config_version); a smaller number is treated as too old and the
 * file is discarded. enable_internet=2 is IPERM_NOT_ALLOWED — the "asked and
 * declined" state — which makes askForInternetPermission() early-return instead
 * of popping the privacy dialog. All other params fall back to their defaults.
 */
static const char SEED_CONFIG_XML[] =
	"<?xml version=\"1.0\"?>\n"
	"<stkconfig version=\"8\" >\n"
	"\n"
	"    <!-- Status of internet: 0 user wasn't asked, 1: allowed, 2: not allowed -->\n"
	"    <enable_internet value=\"2\" />\n"
	"\n"
	"</stkconfig>\n";

/*
 * Write a seed file into SAVEDIR only if it is not already present, so a real
 * (persisted) profile is never clobbered. A failure is non-fatal: report it and
 * let the caller launch STK anyway. NB: Phoenix libc rejects the "wt"/"rt" mode
 * strings, so use plain "w".
 */
static void seed_file(const char *path, const char *contents)
{
	FILE *f;
	size_t len;

	if (access(path, F_OK) == 0) {
		return; /* keep an existing (possibly persisted) file */
	}

	f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, "stk: seed %s: %s\n", path, strerror(errno));
		return;
	}
	len = strlen(contents);
	if (fwrite(contents, 1, len, f) != len) {
		fprintf(stderr, "stk: seed %s: short write\n", path);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	/* STK writes its config/players/hardware-detection files into SAVEDIR; make
	 * it exist and be writable first (tmpfs → RAM). EEXIST is fine. But STK does
	 * NOT read config.xml/players.xml directly from SAVEDIR: FileManager::
	 * checkAndCreateConfigDir() takes SUPERTUXKART_SAVEDIR and appends the
	 * version subdir "config-0.10/" (stk-code 1.4, file_manager.cpp:1068), so the
	 * real config dir is /tmp/stk/config-0.10/. Create both levels. */
	if ((mkdir("/tmp/stk", 0777) != 0 && errno != EEXIST) ||
			(mkdir("/tmp/stk/config-0.10", 0777) != 0 && errno != EEXIST)) {
		fprintf(stderr, "stk: mkdir /tmp/stk/config-0.10: %s\n", strerror(errno));
		return 1;
	}

	/* Seed a current player + declined-internet config so STK boots straight to
	 * the main menu instead of the first-run login/register/privacy screens.
	 * These must land in the version subdir (see above), not SAVEDIR itself.
	 * Skipped for any file that already exists (see seed_file). */
	seed_file("/tmp/stk/config-0.10/players.xml", SEED_PLAYERS_XML);
	seed_file("/tmp/stk/config-0.10/config.xml", SEED_CONFIG_XML);

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
