/*
 * quakespasm-drm: process-start hooks of the new-lane clone (nothing engine-side).
 *
 * 1. One identifying line, so a UART log shows which GPU lane a quakespasm run used (the
 *    shipped binary, quakespasm-v3da and this clone print the same engine text otherwise).
 *    write(2), not stdio: nothing may touch stdout before main()'s setvbuf().
 * 2. SDL's VIDEO and INPUT log categories at DEBUG, so the KMSDRM driver reports its init
 *    steps (device opened, connector/CRTC counts, one "New DRM FB" per scan-out buffer) and
 *    the Phoenix HID source its opens -- a dozen lines at start, nothing per frame. Setting
 *    it here instead of `export SDL_LOGGING=...` keeps the Pi cycle free of an extra psh
 *    command.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <unistd.h>

#include <SDL2/SDL_log.h>

static const char qsdrm_banner[] =
	"quakespasm-drm: new GPU lane -- SDL 2.30.12 KMSDRM + Mesa 26.2 GBM/EGL (desktop GL) + libdrm-phoenix "
	"-> rpi4-kms (card0) + rpi4-v3d-async (renderD128)\n";

__attribute__((constructor)) static void qsdrm_start(void)
{
	(void)write(1, qsdrm_banner, sizeof(qsdrm_banner) - 1u);
	SDL_LogSetPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_DEBUG);
	SDL_LogSetPriority(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_DEBUG);
}
