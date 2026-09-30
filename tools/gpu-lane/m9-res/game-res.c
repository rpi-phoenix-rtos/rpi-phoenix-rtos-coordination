/*
 * game-res - start a game full screen in a lower resolution (M9)
 *
 *   game-res <game> [WxH] [game args...]
 *     game  stk | qs | q2 | q3 | vkq
 *     WxH   the fullscreen mode, e.g. 1280x720. Without it: $GAME_RES; without that
 *           too: 1920x1080 (the native mode, asked for explicitly).
 *
 * rpi4-kms's connector lists 1600x900, 1440x1080, 1280x720, 1024x768,
 * 960x540, 800x600 and 640x480 besides the native 1920x1080; a game that goes
 * fullscreen in one of them renders that many pixels and the display scales the
 * picture to the screen (docs/gpu-new-lane/M9-scaled-fullscreen.md). What each
 * engine needs to really switch the mode, read from its source:
 *
 *   stk  /bin/stk --screensize=WxH: the launcher drops its own
 *        --screensize=1920x1080 when one is given (stk-launcher.c user_overrides) and
 *        keeps --fullscreen; Irrlicht's GL device then asks SDL for
 *        SDL_WINDOW_FULLSCREEN (CIrrDeviceSDL.cpp), which switches the mode.
 *   qs   /usr/bin/quakespasm-drm -width W -height H -fullscreen: fullscreen needs an
 *        exact display mode at vid_refreshrate 60 (gl_vidsdl.c VID_ValidMode ->
 *        VID_SDL2_GetDisplayMode); vid_desktopfullscreen stays 0 (exclusive).
 *   q2   /usr/bin/quake2 + "+set vid_fullscreen 1 +set r_mode -1 +set
 *        r_customwidth W +set r_customheight H": the launcher's vid_fullscreen 2 is
 *        SDL_WINDOW_FULLSCREEN_DESKTOP (glimp_sdl2.c), which never switches the mode;
 *        the later +set wins.
 *   q3   /usr/bin/quake3 + "+set r_fullscreen 1 +set r_mode -1 +set
 *        r_modeFullscreen -1 +set r_customwidth W +set r_customheight H":
 *        r_modeFullscreen defaults to -2 (the desktop size) and overrides r_mode
 *        whenever fullscreen (cl_main.c CL_GetModeInfo).
 *   vkq  /usr/bin/vkquake -width W -height H: the launcher leaves out its own -width 1920
 *        -height 1080 when one is given (vkQuake takes the FIRST -width/-height,
 *        COM_CheckParm). SDL's Vulkan KMSDRM surface picks the display mode equal to the
 *        window size.
 *
 * Extra game arguments are passed on: `game-res stk 1280x720 race` (the stk launcher's AI
 * race), `game-res vkq 1280x720 +playdemo demo1`.
 *
 * GAME_RES_DRYRUN=1 prints the command instead of running it (host test).
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXARGS 96


static int parse_res(const char *s, unsigned *w, unsigned *h)
{
	char tail;

	if ((s == NULL) || (sscanf(s, "%ux%u%c", w, h, &tail) != 2)) {
		return -1;
	}
	return ((*w >= 320u) && (*w <= 4096u) && (*h >= 200u) && (*h <= 4096u)) ? 0 : -1;
}


static int listed(unsigned w, unsigned h)
{
	static const unsigned modes[][2] = {
		{ 1920u, 1080u }, { 1600u, 900u }, { 1440u, 1080u }, { 1280u, 720u }, { 1024u, 768u }, { 960u, 540u },
		{ 800u, 600u }, { 640u, 480u },
	};
	unsigned i;

	for (i = 0u; i < sizeof(modes) / sizeof(modes[0]); i++) {
		if ((modes[i][0] == w) && (modes[i][1] == h)) {
			return 1;
		}
	}
	return 0;
}


static void usage(void)
{
	fprintf(stderr, "usage: game-res stk|qs|q2|q3|vkq [WxH] [game args...]   (or export GAME_RES=WxH)\n");
}


int main(int argc, char **argv)
{
	static char ws[16], hs[16], wxh[48];
	char *a[MAXARGS];
	const char *game, *res = NULL, *path;
	unsigned w = 0u, h = 0u;
	int n = 0, i, first;

	if (argc < 2) {
		usage();
		return 2;
	}
	game = argv[1];
	first = 2;
	if ((argc > 2) && (parse_res(argv[2], &w, &h) == 0)) {
		res = argv[2];
		first = 3;
	}
	else if (getenv("GAME_RES") != NULL) {
		res = getenv("GAME_RES");
		if (parse_res(res, &w, &h) != 0) {
			fprintf(stderr, "game-res: GAME_RES=%s is not WxH\n", res);
			return 2;
		}
	}
	else {
		w = 1920u;
		h = 1080u;
	}
	if ((argc - first) + 24 > MAXARGS) {
		fprintf(stderr, "game-res: too many arguments\n");
		return 2;
	}
	snprintf(ws, sizeof(ws), "%u", w);
	snprintf(hs, sizeof(hs), "%u", h);

	if (strcmp(game, "stk") == 0) {
		path = "/bin/stk";
		snprintf(wxh, sizeof(wxh), "--screensize=%ux%u", w, h);
		a[n++] = "stk";
		a[n++] = wxh;
	}
	else if (strcmp(game, "qs") == 0) {
		path = "/usr/bin/quakespasm-drm";
		a[n++] = "quakespasm-drm";
		a[n++] = "-width";
		a[n++] = ws;
		a[n++] = "-height";
		a[n++] = hs;
		a[n++] = "-fullscreen";
	}
	else if (strcmp(game, "q2") == 0) {
		path = "/usr/bin/quake2";
		a[n++] = "quake2";
		a[n++] = "+set";
		a[n++] = "vid_fullscreen";
		a[n++] = "1";
		a[n++] = "+set";
		a[n++] = "r_mode";
		a[n++] = "-1";
		a[n++] = "+set";
		a[n++] = "r_customwidth";
		a[n++] = ws;
		a[n++] = "+set";
		a[n++] = "r_customheight";
		a[n++] = hs;
	}
	else if (strcmp(game, "q3") == 0) {
		path = "/usr/bin/quake3";
		a[n++] = "quake3";
		a[n++] = "+set";
		a[n++] = "r_fullscreen";
		a[n++] = "1";
		a[n++] = "+set";
		a[n++] = "r_mode";
		a[n++] = "-1";
		a[n++] = "+set";
		a[n++] = "r_modeFullscreen";
		a[n++] = "-1";
		a[n++] = "+set";
		a[n++] = "r_customwidth";
		a[n++] = ws;
		a[n++] = "+set";
		a[n++] = "r_customheight";
		a[n++] = hs;
	}
	else if (strcmp(game, "vkq") == 0) {
		path = "/usr/bin/vkquake";
		a[n++] = "vkquake";
		a[n++] = "-width";
		a[n++] = ws;
		a[n++] = "-height";
		a[n++] = hs;
	}
	else {
		usage();
		return 2;
	}
	for (i = first; i < argc; i++) {
		a[n++] = argv[i];
	}
	a[n] = NULL;

	printf("game-res: %s %ux%u%s%s -> exec %s", game, w, h, (res == NULL) ? " (default)" : "",
		listed(w, h) ? "" : " (NOT a listed mode: the engine picks the closest or refuses)", path);
	for (i = 1; i < n; i++) {
		printf(" %s", a[i]);
	}
	printf("\n");
	fflush(stdout);
	if ((getenv("GAME_RES_DRYRUN") != NULL) && (strcmp(getenv("GAME_RES_DRYRUN"), "1") == 0)) {
		return 0;
	}
	execv(path, a);
	perror("game-res: exec");
	return 1;
}
