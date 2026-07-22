# Mesa V3D/GL/Vulkan port — patch series

The Phoenix-RTOS Raspberry Pi 4 GPU stack (libGL / libv3d / libv3dv) is built from
**upstream Mesa plus this small patch series** — we do NOT vendor a full Mesa fork.

## Base

- Upstream: `https://gitlab.freedesktop.org/mesa/mesa.git`
- Pinned tag: **`mesa-26.2.0-rc1`** — an *immutable* git tag. Because it never moves,
  these patches can never be broken by upstream drift and the build is exactly
  reproducible. (rc1 is a release candidate; when Mesa 26.2.0 **final** ships we will
  re-base the port onto that tag. rc1 is our validated base for now.)

## Applying

```sh
git clone https://gitlab.freedesktop.org/mesa/mesa.git external/mesa
cd external/mesa
git checkout mesa-26.2.0-rc1
git am /path/to/patches/mesa/*.patch      # applies all 17 in order -> our exact tree
```

`scripts/bootstrap-linux-host.sh` does this automatically.

## Contents (17 patches)

- **Phoenix-RTOS RPi4 port (ours):** the `v3d:` and `v3dv:` / `broadcom/vulkan:` commits
  — the BCM2711 V3D 4.2 GL + Vulkan port and its Phoenix winsys integration.
- **Incidental upstream commits** included only to reproduce our exact validated base
  from the `mesa-26.2.0-rc1` tag (they touch nouveau / Intel anv / pick_status and do
  **not** affect the aarch64 V3D build): `0001` (.pick_status.json), `0006` (nv50_ir_ra),
  `0007` (anv Intel vendor id). Harmless; kept for byte-exact reproducibility.

## License

Mesa is **MIT** (see `docs/license.rst` in the Mesa tree). Our patches modify Mesa and are
therefore under Mesa's MIT license. This directory redistributes only the patch text; the
Mesa source itself is fetched from upstream.
