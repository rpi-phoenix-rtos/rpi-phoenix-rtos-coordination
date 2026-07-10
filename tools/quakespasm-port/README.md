# QuakeSpasm — Phoenix-RTOS port (GPL-2.0-or-later)

Phoenix platform backend for [QuakeSpasm](https://github.com/sezero/quakespasm)
running on the ported Mesa/V3D GL stack: it replaces QuakeSpasm's SDL platform
layer (`sys_sdl_unix.c`, `pl_linux.c`, `main_sdl.c`, `in_sdl.c`, `snd_sdl.c`,
`gl_vidsdl.c`) with shims that drive Phoenix directly (`/dev/fb0`, `/dev/kbd0`,
`/dev/mouse0`, `/dev/audio0`, the V3D GL context).

**License.** This directory is a derivative work of QuakeSpasm and is licensed
**GPL-2.0-or-later** (see `COPYING`), separate from the BSD-licensed Phoenix
core. It is an **optional showcase** — building it is opt-in and pulls the GPL
QuakeSpasm source at build time; the Phoenix core does not depend on it.

The upstream engine + game code is **not** vendored here. `build-quakespasm-phoenix.py`
compiles this glue against a local QuakeSpasm clone (`external/quakespasm/`, not
tracked) fetched by the bootstrap scripts.
