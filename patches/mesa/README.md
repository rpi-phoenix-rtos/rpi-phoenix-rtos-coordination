# Mesa V3D/GL/Vulkan port — patch

The Phoenix-RTOS Raspberry Pi 4 GPU stack (libGL / libv3d / libv3dv) is built from
**upstream Mesa plus this single patch** — we do NOT vendor a full Mesa fork.

## Base

- Upstream: `https://gitlab.freedesktop.org/mesa/mesa.git`
- Pinned tag: **`mesa-26.2.0-rc1`** — an *immutable* git tag. Because it never moves,
  this patch can never be broken by upstream drift and the build is exactly reproducible.
  (rc1 is a release candidate; when Mesa 26.2.0 **final** ships we will re-base the port
  onto that tag. rc1 is our validated base for now.)

## Applying (what `scripts/bootstrap-linux-host.sh` does automatically)

```sh
git clone https://gitlab.freedesktop.org/mesa/mesa.git external/mesa
cd external/mesa
git checkout mesa-26.2.0-rc1
git apply /path/to/patches/mesa/phoenix-rpi4-v3d.patch   # -> our exact validated tree
```

## Contents

`phoenix-rpi4-v3d.patch` is `git diff mesa-26.2.0-rc1..HEAD` of our port branch — the net
change from the tag to our validated tree (23 files, ~600 lines): the BCM2711 V3D 4.2 GL +
Vulkan (v3dv) port and its Phoenix winsys integration. It also folds in a few incidental
upstream commits that were between rc1 and our fork point (nouveau / Intel anv /
pick_status housekeeping) — harmless to the aarch64 V3D build, included only so the applied
tree byte-matches our validated HEAD. Verified to `git apply --check` cleanly onto the tag.

## License

Mesa is **MIT** (see `docs/license.rst` in the Mesa tree). This patch modifies Mesa and is
therefore under Mesa's MIT license. Only the patch text is redistributed here; the Mesa
source itself is fetched from upstream at the pinned tag.
