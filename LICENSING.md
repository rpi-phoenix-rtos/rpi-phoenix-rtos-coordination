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
| Quake / vkQuake platform glue (derivative of the GPL game engines it links into) | `tools/quakespasm-port/`, `tools/vkquake-port/` | **GPL-2.0-or-later** (each dir's `COPYING`). Optional, opt-in showcases; the BSD core does not depend on them. NOTE: vkQuake itself is WIP/non-functional and is **not published** — the `tools/vkquake-port/` glue is retained but its game engine (`external/vkquake`) is local-only. |
| Phoenix-RTOS OS sources (kernel, plo, libphoenix, devices, filesystems, usb, lwip, ports framework, project, …) | `sources/<repo>/` (separate git repos) | Their upstream **Phoenix-RTOS** licenses (BSD 3-Clause / MIT — see each repo's `LICENSE`). Our Pi-4 changes to these are contributed under the **same license as the file/repo they modify.** |
| Our patches to external upstreams | `tools/*/patches/*.patch`, and port changes staged for `sources/phoenix-rtos-ports` | Follow the **patched project's** license (a patch to a GPL/MIT/etc. project is licensed under that project's terms). |
| Vendored third-party source tarballs | `tools/ports/src/*.tar.*` (dillo, fltk, glib, libffi, libiconv, mc, nano, ncurses) | **Each under its own upstream license** (LGPL/GPL/MIT/BSD/… as shipped inside the tarball). Redistributed unmodified; not relicensed. |
| Mesa V3D/GL/Vulkan port | `patches/mesa/*.patch` (17 patches applied atop the immutable upstream tag `mesa-26.2.0-rc1`) | **MIT** (Mesa's license, `docs/license.rst` upstream). Only our ~600-line patch set is redistributed here; the Mesa base tree is fetched from upstream at build time. Our patches inherit Mesa MIT. |
| QuakeSpasm engine (GLQuake showcase) | published as an org fork `rpi-phoenix-rtos/quakespasm` (fetched by `bootstrap-linux-host.sh`) | **GPL-2.0-or-later** (upstream `LICENSE.txt`). Our port commits are GPL-compatible. |
| vkQuake engine | `external/vkquake` — **NOT published** (WIP/non-functional; local-only) | GPL-2.0; excluded from this release. |
| Raspberry Pi firmware blobs | `.bootblobs/` (fetched from `raspberrypi/firmware`) | Raspberry Pi's firmware license; fetched, not redistributed here. |
| Quake game data (`pak0.pak`, maps) | not in this repo | id Software shareware/retail terms — **not redistributed**; the user supplies it. |

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
