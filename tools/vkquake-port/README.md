# vkQuake — Phoenix-RTOS port (GPL-2.0-or-later)

Phoenix platform backend for [vkQuake](https://github.com/Novum/vkQuake) running
on the ported V3DV Vulkan driver: it replaces vkQuake's SDL platform layer with
shims that drive Phoenix directly (`/dev/fb0` scanout, `/dev/kbd0`,
`/dev/mouse0`, `/dev/audio0`) plus a no-WSI Vulkan present path.

**License.** This directory is a derivative work of vkQuake and is licensed
**GPL-2.0-or-later** (see `COPYING`), separate from the BSD-licensed Phoenix
core. It is an **optional showcase** — building it is opt-in and pulls the GPL
vkQuake source at build time; the Phoenix core does not depend on it.

The upstream engine + game code is **not** vendored here. `build-vkquake-phoenix.py`
compiles this glue against a local vkQuake clone (`external/vkquake/`, not
tracked) fetched by the bootstrap scripts.
