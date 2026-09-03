# Licensing

This project is a **multi-component** work. Different parts carry different
licenses; be careful to honour each. The short version:

- **Our original code → BSD 3-Clause** ([LICENSE](LICENSE)), matching the
  Phoenix-RTOS upstream convention.
- **Third-party components keep their own upstream licenses** — we do not, and
  cannot, relicense them.
- **Patches we apply to external projects follow that project's license.**

## Component breakdown

| Component | Location | License |
|---|---|---|
| This coordination repo's original code (build/lab scripts, port tooling, docs) | `scripts/`, `tools/` (our `*.sh`/`*.py`/`build.project`-style glue, **except** the GPL showcase ports below), `docs/`, `manifests/` | **BSD 3-Clause** ([LICENSE](LICENSE)) |
| Game-engine framework ports — recipe, Phoenix platform glue and patches (derivative of the GPL engines they build) | `sources/phoenix-rtos-ports/{quakespasm,yquake2,quake3,vkquake}/` (`glue/`, `patches/`, `port.def.sh`) | **GPL-2.0-or-later** — each port declares this in its `port.def.sh` `license=`. These are the recipes that build the engines shipped on the image. Optional, opt-in showcases (`--with-showcase`); the BSD core does not depend on them. |
| SuperTuxKart framework port — recipe + patches | `sources/phoenix-rtos-ports/supertuxkart/` | **GPL-3.0-or-later** (declared in its `port.def.sh`). |
| Superseded ad-hoc Quake / vkQuake platform glue (kept for reference; not what the image builds) | `tools/quakespasm-port/`, `tools/vkquake-port/` | **GPL-2.0-or-later** (each dir's `COPYING`). |
| Quake III game-logic VMs we built from ioquake3 | `assets/quake3-qvm/pak1-ioq3-vms.pk3` (**in this repo**) | **GPL-2.0** — the three QVMs (`vm/{ui,cgame,qagame}.qvm`) are compiled from ioquake3 sources. **No id Software content is involved.** They exist because the free Quake III demo's 1999 QVMs report UI API 3 while the pinned quake3e requires 6. See `assets/quake3-qvm/README.md`. |
| Phoenix-RTOS OS sources (kernel, plo, libphoenix, devices, filesystems, usb, lwip, ports framework, project, …) | `sources/<repo>/` (separate git repos) | Their upstream **Phoenix-RTOS** licenses (BSD 3-Clause / MIT — see each repo's `LICENSE`). Our Pi-4 changes to these are contributed under the **same license as the file/repo they modify.** |
| Our patches to external upstreams | `tools/*/patches/*.patch`, and port changes staged for `sources/phoenix-rtos-ports` | Follow the **patched project's** license (a patch to a GPL/MIT/etc. project is licensed under that project's terms). |
| Vendored third-party source tarballs | `tools/ports/src/*.tar.*` (dillo, fltk, glib, libffi, libiconv, mc, nano, ncurses) | **Each under its own upstream license** (LGPL/GPL/MIT/BSD/… as shipped inside the tarball). Redistributed unmodified; not relicensed. |
| Mesa V3D/GL/Vulkan port | `patches/mesa/phoenix-rpi4-v3d.patch` (a single patch applied atop the immutable upstream tag `mesa-26.2.0`) | **MIT** (Mesa's license, `docs/license.rst` upstream). Only our patch (~924 lines across 20 files, 465 added) is redistributed here; the Mesa base tree is fetched from upstream at build time. Our patches inherit Mesa MIT. |
| Game-engine sources (QuakeSpasm, yQuake2, quake3e, vkQuake) | not in this repo — each port fetches its own **pinned upstream commit-archive** at build time (URL + sha256 in its `port.def.sh`); our engine changes are also published as org forks (`rpi-phoenix-rtos/{quakespasm,yquake2,quake3e,vkquake}`, branch `phoenix-rpi4-port`, cloned into `external/` by `bootstrap-linux-host.sh` as a development convenience) | **GPL-2.0-or-later** (upstream). Our port commits are GPL-compatible. |
| SuperTuxKart source + assets | not in this repo — the port fetches the pinned stk-code 1.4 tarball, and `scripts/stage-game-data.sh` fetches the pinned 1.4 asset package | **GPL-3.0-or-later** (code); the asset roots carry their own upstream (predominantly CC) terms. Fetched at build time, not redistributed here. |
| Raspberry Pi firmware blobs | `.bootblobs/` (fetched from `raspberrypi/firmware`) | Raspberry Pi's firmware license; fetched, not redistributed here. |
| Quake I / II / III game data (`pak0.pak`, `pak0.pk3`, maps) | not in this repo — `scripts/stage-game-data.sh` fetches the **freely-redistributable shareware/demo** paks from pinned URLs at build time and stages them into the image *you* build | id Software's shareware/demo terms — **not redistributed by this repo**. Retail Quake data, if you use your own, remains subject to id Software's retail terms. |

## Notes on the AI-assisted origin

Much of the original first-party code in this repo was produced with AI
assistance during the Pi-4 bring-up. That work is released under BSD 3-Clause
(above). Where that code was written *into* an external project (a patch, or a
change inside a `sources/` upstream repo), it is contributed under **that
project's** license, not BSD, so as not to disturb the upstream's licensing.

## Copyright holder

The `LICENSE` copyright line currently reads *"Witold Bołt and the Phoenix-RTOS
Raspberry Pi 4 port contributors"* — adjust the holder (e.g. to an organisation)
before publishing if that is preferred.
