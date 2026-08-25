# Mesa V3D/GL/Vulkan port — patch

The Phoenix-RTOS Raspberry Pi 4 GPU stack (libGL / libv3d / libv3dv) is built from
**upstream Mesa plus this single patch** — we do NOT vendor a full Mesa fork.

## Base

- Upstream: `https://gitlab.freedesktop.org/mesa/mesa.git`
- Pinned tag: **`mesa-26.2.0`** (the released final tag) — an *immutable* git tag. Because
  it never moves, this patch can never be broken by upstream drift and the build is exactly
  reproducible. (The port was rebased from the earlier `mesa-26.2.0-rc1` candidate onto the
  released `mesa-26.2.0` tag; this is what `scripts/bootstrap-linux-host.sh` fetches.)

## Applying (what `scripts/bootstrap-linux-host.sh` does automatically)

```sh
git clone https://gitlab.freedesktop.org/mesa/mesa.git external/mesa
cd external/mesa
git checkout mesa-26.2.0
git apply /path/to/patches/mesa/phoenix-rpi4-v3d.patch   # -> our exact validated tree
```

## Contents

`phoenix-rpi4-v3d.patch` is `git diff mesa-26.2.0..HEAD` of our port branch — the net change
from the released tag to our validated tree (20 files, ~924 lines): the BCM2711 V3D 4.2 GL +
Vulkan (v3dv) port and its Phoenix winsys integration, including the NPOT-mipmap-generation
fix (`v3d_generate_mipmap` declines NPOT → render fallback; fixes scrambled Quake II model
skins). Now that the base is the released `mesa-26.2.0` tag, the patch is purely our port
commits (no incidental rc1→final upstream folding). Verified to `git apply --check` cleanly
onto the tag.

## License

Mesa is **MIT** (see `docs/license.rst` in the Mesa tree). This patch modifies Mesa and is
therefore under Mesa's MIT license. Only the patch text is redistributed here; the Mesa
source itself is fetched from upstream at the pinned tag.
