/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * Phoenix-RTOS platform backend for QuakeSpasm (QuakeSpasm is Copyright (C) id
 * Software, Inc. and the QuakeSpasm developers, GPL-2.0-or-later). It implements
 * the QuakeSpasm platform interface and is distributed under the same license as
 * the program it is built into; see COPYING in this directory.
 */
/*
 * Minimal SDL2-compat shim for the Quakespasm → Phoenix (V3D) port.
 *
 * Quake's renderer (gl_*.c, r_*.c) and engine-core TUs include <SDL.h> via
 * quakedef.h, but reference essentially no SDL API from headers — only the
 * fixed-width integer typedefs and the endianness macros. The real SDL platform
 * surface (window / GL context / input / audio / timer) is intentionally NOT
 * emulated: the SDL platform TUs (gl_vidsdl.c, in_sdl.c, snd_sdl.c, sys_sdl_unix.c,
 * main_sdl.c) are replaced wholesale by Phoenix platform shims that drive our V3D
 * Mesa GL context + /dev/fb0 + /dev/kbd0 + clock directly.
 *
 * So this header only needs to satisfy what leaks through quakedef.h's header
 * chain. Grow it (driven by compile errors) if a core TU turns out to need more.
 */
#ifndef PHOENIX_QS_SDL_SHIM_H
#define PHOENIX_QS_SDL_SHIM_H

#include <stdint.h>

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER  SDL_LIL_ENDIAN   /* aarch64 is little-endian */

/*
 * SDL_RWops file-I/O abstraction: common.c uses it (only) in the localization-text
 * loader. Map it onto stdio FILE*. (SDL_RWsize is defined by common.c itself under
 * #ifndef USE_SDL2, so it is intentionally NOT provided here.)
 */
#include <stdio.h>

#define SDLCALL
#define RW_SEEK_SET SEEK_SET
#define RW_SEEK_CUR SEEK_CUR
#define RW_SEEK_END SEEK_END

typedef FILE SDL_RWops;

static inline SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
	return fopen(file, mode);
}
static inline Sint64 SDL_RWseek(SDL_RWops *ctx, Sint64 offset, int whence)
{
	if (fseek((FILE *)ctx, (long)offset, whence) != 0)
		return -1;
	return (Sint64)ftell((FILE *)ctx);
}
static inline Sint64 SDL_RWtell(SDL_RWops *ctx)
{
	return (Sint64)ftell((FILE *)ctx);
}
static inline size_t SDL_RWread(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
	return fread(ptr, size, maxnum, (FILE *)ctx);
}
static inline size_t SDL_RWwrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
	return fwrite(ptr, size, num, (FILE *)ctx);
}
static inline int SDL_RWclose(SDL_RWops *ctx)
{
	return fclose((FILE *)ctx);
}

#endif /* PHOENIX_QS_SDL_SHIM_H */
