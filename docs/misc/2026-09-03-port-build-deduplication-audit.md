# Port-build deduplication audit — every third-party build, and which one ships

**Date:** 2026-09-03
**Status:** ANALYSIS ONLY. No build script, `port.def.sh` or `ports.yaml` was
changed by this document, and nothing here was executed against hardware or
against the buildroot. A full-clean SD build was running while this was written;
every statement below comes from reading the trees, not from a build.
**Purpose:** give the owner's rule ("one build path per port; `tools/` is not a
ports directory", `docs/inprogress/WEEK-2026-W36.md` §OWNER RULE 2026-09-03) a
complete traced ground truth — what exists twice, which copy the image actually
takes, where the two copies have already drifted, and a migration order that is
safe to execute one port at a time.

**Headline numbers:** **81 third-party items** audited across 89 table rows
(the last 8 rows group our own-code `tools/` dirs, which are not third party).
**24 items are dual-personality** — a framework port *and* a legacy `tools/`
script. **39 divergences found** between the two personalities; the worst is
that the legacy `libiconv` "port" is not libiconv at all but a hand-written
identity stub (§4 D1). Classification: **76 `PORT`** (of which 45 are already
framework-only, i.e. nothing to do), **2 `TOOLS-JUSTIFIED`** (the Mesa/V3D GPU
archives; the `--enable-glamor` Xphoenix variant), **11 `HOST-TOOL`**.

---

## 0. The rule being applied

Quoted from the weekly log (owner, 2026-09-03):

- `tools/` is **only** for our own tools needed to build the project which for
  various reasons cannot easily be placed elsewhere. "So we should use `tools`
  folder if no other place makes sense."
- A **normal port** — "we take a versioned artifact from Internet, apply our
  patches and just build" — belongs in `sources/phoenix-rtos-ports` as a
  framework port (`port.def.sh` + `patches/`, registered in a project's
  `ports.yaml`). `nano` is the model case.
- Improvements to the system itself belong in the kernel / devices / libphoenix
  repos.
- The only legitimate reason to keep a hand-written script is a build the
  framework genuinely cannot express, expected to be "very rare and exceptional".
- The thing to eliminate: a port with **two build personalities** — a framework
  port *and* a legacy `tools/**/build-*.sh` — especially where the legacy script
  is the one producing what ships. "We don't need multiple ways to build mv or
  nano or quake or sdl or python."
- To be done "in a controlled, careful, tested way".

Classification vocabulary:

| Class | Meaning |
| --- | --- |
| `PORT` | Plain versioned-artifact + patches build. Belongs in `phoenix-rtos-ports` only; any `tools/` script for it should go. Sub-annotated `(clean)` when there is already no `tools/` script, i.e. nothing to do. |
| `TOOLS-JUSTIFIED` | Must stay in `tools/` because a **named** framework limitation blocks it. The mechanism is stated per item. |
| `HOST-TOOL` | Not a port — our own code, a host-side utility, or a data-staging recipe. Correctly in `tools/`. |

---

## 1. How "which one ships" was determined — the tiebreaker

This is the mechanism behind the whole audit, so it goes first.

With `--with-showcase` (the **default** since 2026-09-03,
`scripts/rebuild-rpi4b-fast.sh:145-148`) the image is produced in this order:

1. `scripts/build-showcase-apps.sh --phase gpu` (`rebuild-rpi4b-fast.sh:565`) —
   builds `tools/.gpu-libs/lib{v3d,GL,v3dv}-phoenix.a` from the host Mesa tree.
2. `phoenix-rtos-build/build.sh host fs core ports project image`
   (`rebuild-rpi4b-fast.sh:350,610`). The **`ports`** stage runs `port_manager`
   over `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml`
   and installs framework-port artifacts into `_fs/<target>/root`.
3. `scripts/build-showcase-apps.sh --phase stage` (`rebuild-rpi4b-fast.sh:637-643`)
   — runs the legacy `tools/**/build-*.sh` steps with
   `SHOWCASE_STAGE_DIR=_fs/<target>/root`, i.e. **into the same tree**.
4. `scripts/build-rpi4b-rootfs-ext2.sh` packs `_fs/<target>/root` into the ext2
   partition; the `nfsroot` variant serves the same tree.

**Therefore: wherever a framework port and a `tools/` script write the same
rootfs path, the `tools/` copy wins, because it is written last.** That single
ordering fact answers "which one ships" for every overlapping item below, and it
is exactly the mechanism that made the `nano` trap invisible (§4 D-nano).

Three secondary rules that the table depends on:

- **`if: false` does not mean "not built".** `port_manager.py:390-393` pops an
  `if: false` port from the *user-requested* candidate set only; it is still
  built when an enabled port pulls it through `depends=`. So `sdl2` is
  `if: false` yet is built and linked into four of the five games, while
  `nano`/`mc`/`ffmpeg` (`if: false`, nothing depends on them) are genuinely not
  built by the framework at all.
- **Libraries do not ship as files, they ship inside consumers.** For a library
  built twice the question is *which consumers link which copy* — that is where
  the libiconv, libffi and zlib splits bite (§4).
- **Two different library prefixes, one shared include path.** Framework ports
  install to `PREFIX_A`/`PREFIX_H` = `.buildroot/_build/<t>/{lib,include}`. The
  legacy `tools/ports` scripts write into
  `.buildroot/_build/<t>/sysroot/{lib,usr/include}` — a different tree, but one
  that is on **every** port's default `--sysroot` search path. So a legacy
  header can shadow a framework one for any port that does not pass
  `-I${PREFIX_H}` first. On a full clean the sysroot is rebuilt before the ports
  stage, so this is an *incremental-build* hazard, not a clean-build one.

Currently invoked legacy steps, exhaustively
(`scripts/build-showcase-apps.sh` phase `stage`):

| line | step | hard/soft |
| --- | --- | --- |
| 491 | `scripts/build-rootfs-helpers.sh` (5 launcher binaries) | hard |
| 499 | `tools/ports/build-libiconv.sh` | hard |
| 500 | `tools/ports/build-libffi.sh` | hard |
| 501 | `tools/ports/build-ncurses.sh` | hard |
| 505 | `tools/x11-port/build-x11-phoenix.sh` | hard |
| 506 | `tools/ports/build-glib2.sh` | hard |
| 519 | `tools/ports/build-nano.sh` | soft |
| 520 | `tools/ports/build-mc.sh` | soft |
| 536 | `tools/x11-port/build-xlaunch.sh` | soft |
| 554 | `tools/x11-port/build-xserver-core.sh --glamor` | soft |
| 555 | `tools/x11-port/build-xfbdev.sh --glamor-daemon` | soft |
| 558 | `tools/x11-port/build-gl-x11-window.sh --daemon` | soft |

Everything else under `tools/**/build-*.sh` is **not invoked by any build path**
(verified: no `.sh`, `.py`, `.yaml` or `Makefile` in the repo calls the legacy
`build-*-phoenix.py` engine recipes, and the migrated X11 app steps were removed
at `build-showcase-apps.sh:525-534`).

**⚠️ Path-level coupling that survives content migration.**
`build-showcase-apps.sh:185-198` (`archive_fresh()`) lists
`tools/v3d-driver-port`, `tools/quakespasm-port` and `tools/vkquake-port` as
freshness inputs and **`die`s if any path is missing** (`:194`). Deleting any of
those three directories breaks **every** `--phase gpu` build — and therefore all
five game ports — until that list is edited first. This guard was added
deliberately on 2026-09-03 to catch exactly this migration.

---

## 2. Master table

`ships`: `FW` = the framework port's artifact reaches the image; `TOOLS` = the
legacy script's artifact reaches the image; `split` = both are built and
different consumers get different copies; `neither` = nothing in the default
image flow builds it.

`fw if:` — the flag in the rpi4b `ports.yaml`; `(dep)` = not listed, pulled
transitively; `(none)` = no framework port exists; `—` = listed without a flag
(defaults to true).

### 2a. Framework-only ports — nothing to do

All of these are already single-personality. No `tools/` script exists for any
of them. Class `PORT (clean)`, ships `FW`, no divergences.

| # | name | fw if: | note |
| --- | --- | --- | --- |
| 1 | busybox | — | |
| 2 | bash | — | |
| 3 | libnfs | — | |
| 4 | lzo | — | |
| 5 | pcre | — | |
| 6 | jansson | — | |
| 7 | lua | — | |
| 8 | sqlite3 | — | |
| 9 | jq | — | |
| 10 | redis | — | |
| 11 | coreutils | — | |
| 12 | coremark | — | |
| 13 | picocom | — | |
| 14 | libevent | — | |
| 15 | mbedtls | — | |
| 16 | openssl (dir `openssl111`, `name="openssl"` 1.1.1a) | — | |
| 17 | micropython | — | |
| 18 | dropbear | — | |
| 19 | curl | — | |
| 20 | lighttpd | — | `tools/stress/net/stage-lighttpd.sh` only stages a stress **config**, does not build |
| 21 | oniguruma | (dep, via jq) | |
| 22 | libogg | (dep, via STK) | |
| 23 | libvorbis | (dep, via STK) | |
| 24 | libsamplerate | (dep, via STK) | |
| 25 | bzip2 | (dep, via python) | legacy `tools/python-port/build.sh` builds its own private 1.0.8 — same version |
| 26 | xz | (dep, via python) | legacy private 5.4.7 — same version |
| 27 | wpa_supplicant | true | registered 2026-09-03 to stop hand-copying it onto the export |
| 28 | sdl2 | false (dep) | `scripts/build-sdl2-port.sh` is a **port_manager driver**, not a second recipe (`:95,100`); `tools/sdl2-port/*.py` are *consumers* of the port's `libSDL2.a`, not producers |
| 29 | yquake2 | true | `tools/yquake2-port/` holds only `quake2-launcher.c`; its README still points at a deleted `.py` |
| 30 | quake3 (engine, quake3e) | true | `tools/quake3-port/` holds only `quake3-launcher.c` + `demos/cap.dm_68`; README stale the same way |
| 31 | supertuxkart | true | `tools/supertuxkart-port/` holds only `stk-launcher.c` (+ a dead `savedir-seed/`) |

### 2b. Ports in the repo but not built for rpi4b

Class `PORT (clean)`, ships `neither`, no `tools/` script, no divergences:
`azure_sdk`, `coreMQTT`, `coremark_pro`, `enet`, `fs_mark`, `heatshrink`,
`llama2`, `lsb_vsx`, `micro-ecc`, `openiked`, `openvpn`, `smolrtsp`, `sscep`,
`wamr` (14 items, #32–45). Registered nowhere in the rpi4b `ports.yaml` and
pulled by no `depends=`. Listed for completeness only.

### 2c. Dual-personality items — the ones the owner's rule is about

| # | name | framework port? | tools script? | which SHIPS | fw if: | class | divergences (§4) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 46 | **libiconv** | yes (GNU 1.18) | `tools/ports/build-libiconv.sh` | **split** | (dep, via dillo/xorg_apps) | PORT | **D1 (worst)** |
| 47 | **libffi** | yes (3.4.6) | `tools/ports/build-libffi.sh` | **split** | (dep, via python) | PORT | D2 |
| 48 | **zlib** | yes (1.2.11) | inside `build-x11-phoenix.sh:210-213` (1.3.1) | **split** | — | PORT | D3, D34 |
| 49 | **libpng** | yes (1.6.40) **and** built again inside `xorg_fonts:91-100` | inside `build-x11-phoenix.sh:219-227` | split (3 copies) | (dep) | PORT | D24, D34 |
| 50 | **libjpeg / jpeg-9e** | `libjpeg` port 3.0.4 **and** `jpeg-9e` inside `xorg_fonts:103-110` | inside `build-x11-phoenix.sh:234-241` | split (3 copies) | (dep) | PORT | D34 |
| 51 | **ncurses** | yes (6.4, `-fPIC`) | `tools/ports/build-ncurses.sh` (6.4, no PIC) | **split** — framework copy only exists after a manual `scripts/build-port.sh ncurses` | (none in yaml; only nano/mc depend, both `if:false`) | PORT | D5, D-hdr, D6b |
| 52 | **glib2** | yes (2.56.4) | `tools/ports/build-glib2.sh` | **TOOLS** (framework glib2 is built by nothing) | (dep of mc only → not built) | PORT | D1 (its `-liconv` is the stub), D32 |
| 53 | **nano** | yes (2.2.6) | `tools/ports/build-nano.sh` | **TOOLS** | **false** | PORT | D-nano (patch trap, fixed today), D10 |
| 54 | **mc** | yes (4.8.31) | `tools/ports/build-mc.sh` | **TOOLS** | **false** | PORT | D6, D11, D1 |
| 55 | **fltk** | yes (1.3.10) | `tools/ports/build-fltk.sh` (**not invoked**) | FW | (dep, via dillo) | PORT | minor: legacy-only link-smoke gate |
| 56 | **dillo** | yes (3.2.0) | `tools/ports/build-dillo.sh` + `build-dillo-dbg.sh` (**not invoked**) | FW | true | PORT | D12 |
| 57 | **python (CPython 3.14.4)** | yes | `tools/python-port/build.sh` (**not invoked**) | FW | **true** (flipped 2026-09-03) | PORT | D7, D9 |
| 58 | **python `_curses`** | **no framework path at all** | `tools/python-port/build-curses.sh` | TOOLS, and only by a manual run | (none) | PORT | **D8**, D3b (clobbers `/bin/python3`) |
| 59 | **ffmpeg** | yes (6.1) | `tools/ffmpeg-port/build-ffmpeg-phoenix.py` | **neither** | **false** | PORT | D30 |
| 60 | **harfbuzz** | yes (2.6.7, CMake, `HB_HAVE_GLIB=OFF`) | `tools/x11-port/build-harfbuzz.sh` (autotools, `--with-glib=yes`, **not invoked**) | FW | (dep, via STK) | PORT | **D22** |
| 61 | **cairo** | inside `xorg_fonts:216-234` (1.16.0) | `tools/x11-port/build-cairo.sh` (1.16.0, **not invoked**) | FW | (dep) | PORT | D26 |
| 62 | **fribidi 1.0.13** | **none** | `tools/x11-port/build-fribidi.sh` (**not invoked**) | neither | (none) | PORT | D22 |
| 63 | **pango 1.42.4** | **none** (explicitly deferred, `xorg_fonts:13-15`) | `tools/x11-port/build-pango.sh` (**not invoked**) | neither | (none) | PORT | **D22** |
| 64 | **xorg_libs** (23 tarballs: xorgproto, libX11, libxcb, libXt, libXaw, pixman, …) | yes | `tools/x11-port/build-x11-phoenix.sh` (**invoked, hard-fail**) | FW for the shipped clients; TOOLS for `/tmp/x11-phoenix` consumers | true | PORT | **D4**, D23, D24, D31 |
| 65 | **xorg_fonts** (freetype, expat, fontconfig, libXft, libfontenc, libXfont2, cairo, libpng, jpeg) | yes | split across `build-x11-phoenix.sh` + `build-wmaker.sh` (fontconfig/expat/libXft) + `build-cairo.sh` | split | true | PORT | D23, D24, D25, D26 |
| 66 | **xorg_server / Xphoenix** | yes (1.20.14, `--disable-glamor`, stripped → `/usr/bin/Xphoenix`) | `build-xserver-core.sh` + `build-xfbdev.sh` (invoked **only** in `--glamor` / `--glamor-daemon` mode) | **FW** for `Xphoenix`; **TOOLS** for `Xphoenix-glamor-daemon` | true | PORT (core) + TOOLS-JUSTIFIED (glamor variant) | **D17**, D18, D19, D24 |
| 67 | **xterm 396** | yes | `tools/x11-port/build-xterm.sh` (**not invoked**) | FW | true | PORT | D20, D24 |
| 68 | **WindowMaker 0.95.9** | yes | `tools/x11-port/build-wmaker.sh` (**not invoked**) | FW | true | PORT | **D13**, **D14**, D15, D16, D24 |
| 69 | **xorg_apps** (xcalc 1.1.2, xclock 1.1.1, xlogo 1.0.7, xedit 1.2.2) | yes | 4 × `tools/x11-port/build-x{calc,clock,logo,edit}.sh` (**not invoked**) | FW | true | PORT | none material (framework is stricter) |
| 70 | **xbill 2.1** | yes (pinned commit + sha256) | `tools/x11-port/build-xbill.sh` (**not invoked**) | FW | true | PORT | **D21** |
| 71 | **quakespasm** | yes (0.97.0, `if:true`) | `tools/quakespasm-port/build-quakespasm{,-sdl,-det}-phoenix.py` (**not invoked**) | FW | true | PORT | D28, D-qs-rawfb |
| 72 | **vkquake** | yes (1.34, `if:true`) | `tools/vkquake-port/build-vkquake-phoenix.py` (**not invoked**) | FW | true | PORT | **D27**, D29 |

### 2d. Third-party builds with no framework port at all

| # | name | framework? | tools? | ships | class | note |
| --- | --- | --- | --- | --- | --- | --- |
| 73 | **Mesa / GL / V3D / V3DV** (`libGL/libv3d/libv3dv-phoenix.a`) | none | `sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/build-{v3d,gl,v3dv}-phoenix.py` driven by `build-showcase-apps.sh:369-419` | TOOLS (`tools/.gpu-libs/`) | **TOOLS-JUSTIFIED** | see §3.1 |
| 74 | **xorg-server `--enable-glamor` variant + glamor shim** | none (`xorg_server` hardcodes `--disable-glamor`, `:73`) | `build-xserver-core.sh --glamor`, `build-xfbdev.sh --glamor-daemon`, `tools/x11-port/glamor-shim/` | TOOLS (`/bin/Xphoenix-glamor-daemon`) | **TOOLS-JUSTIFIED** | see §3.2 |
| 75 | twm 1.0.12, xeyes 1.1.2 | none | `build-x11-phoenix.sh:331-357` (`--with-apps`, off by default) | neither | PORT (or delete) | demo clients superseded by wmaker |
| 76 | ico 1.0.6, oclock 1.0.5, jwm 2.4.6 | none | `build-{ico,oclock,jwm}.sh` (**not invoked**) | neither | PORT (or delete) | demo clients superseded by wmaker |
| 77 | gdk-pixbuf 2.36.12 | none | present only as an extracted tree under `tools/x11-port/src/`; the migration spec marks it "NOT ported" | neither | delete | dead source tree |
| 78 | FreeBSD **teken** (terminal emulator) | none | `tools/ports/teken-src/` — a **second copy** of the vendored tree that lives at `sources/phoenix-rtos-devices/tty/pl011-tty/teken/` | the devices copy ships (compiled into `pl011-tty`) | PORT (or delete the `tools/` copy) | D33b |
| 79 | Quake III **QVM modules** (ioquake3 `game`/`cgame`/`ui` + the LCC bytecode compiler) | none | `tools/quake3-vm/build-quake3-vms.sh` (created 2026-09-03, concurrent work) | data artifact `assets/quake3-qvm/pak1-ioq3-vms.pk3` | **HOST-TOOL** | produces a **data** pk3, not a target binary; the framework has no notion of a bytecode-data artifact. Correctly in `tools/`. |
| 80 | DejaVu TTF fonts | n/a (copied from the host `fonts-dejavu` package) | `scripts/stage-desktop-fonts.sh` + `tools/x11-port/fontconfig/fonts.conf` | staged into the export | HOST-TOOL (data staging) | not a build |
| 81 | Quake/Quake2/Quake3/STK game **data** | n/a (pinned upstream archives) | `scripts/stage-game-data.sh` → `scripts/fetch-quake-data.sh` | staged into the rootfs overlay | HOST-TOOL (data staging) | not a build |

### 2e. Our own code in `tools/` — correctly there

Class `HOST-TOOL` throughout. No framework port applies; these are not ports.

| # | group | contents |
| --- | --- | --- |
| 82 | rootfs helper binaries | `scripts/build-rootfs-helpers.sh` compiles exactly 5: `tools/ram-stage/ram-stage-play.c` → `bin/ram-stage-play`, `tools/yquake2-port/quake2-launcher.c` → `usr/bin/quake2`, `tools/quake3-port/quake3-launcher.c` → `usr/bin/quake3`, `tools/supertuxkart-port/stk-launcher.c` → `bin/stk`, `tools/pty-run/pty-run.c` → `usr/bin/pty-run` |
| 83 | X11 launcher | `tools/x11-port/launcher/pl_phoenix_xlaunch.c` → `pl_phoenix_xlaunch` / `startx` / `startx_gpu` (argv[0]-keyed); `launcher/mouseprobe.c` |
| 84 | GPU X window client | `tools/x11-port/gl_x11_window.c` (+ `--daemon` mode) — in-repo C, links `tools/.gpu-libs/*.a` |
| 85 | V3D/Mesa build harnesses | `tools/v3d-driver-port/{harness_screen_create.c,v3dv_harness.c,triangle_spirv.h,gen-triangle-spirv.py}` — **load-bearing**: compiled by `build-{v3d,v3dv}-phoenix.py` |
| 86 | ffmpeg E4 demo players | `tools/ffmpeg-port/{e4_play.c,e4_x11_play.c,e4_fbshow.c,e4_decode_*.c,e4_fb_blit.h,gen_e4_clip.py}` — explicitly kept in `tools/` by `ffmpeg/port.def.sh:33-36` |
| 87 | SDL2 smoke tests | `tools/sdl2-port/{sdl2-gltest.c,sdl2-audiotest.c,sdl2-audio-videostubs.c}` + their `.py` drivers |
| 88 | python extension recipe | `tools/python-port/build-extension.sh` + `phoenix-py-compat.h` (framework copy exists), `ctypestest.py`, `selftest*.py` |
| 89 | diagnostics / probes / benches (24 dirs) | `axi-pmu`, `bt-probe`, `cnn-mnist`, `coreutils-difftest`, `coreutils-maketest`, `dbg-probe`, `demo-apps`, `dlopen-poc`, `heap-stress`, `hevc-decode`, `hevc-probe`, `kernel-stackov`, `nfs-bench`, `redis-persist-test`, `sd-scratch-test`, `sqlite-lock-test`, `sqlite-wal-test`, `stack-bomb`, `stress`, `ttyprobe`, `v3d-shader-tool`, `wifi-probe`, plus the X11 dev/debug C (`x11-cursor-sweep.c`, `xcolortest.c`, `apps/{xphxdemo,xfontprobe,xcalc-dbg-wrap}.c`) and the mc/dillo debug wrappers |

---

## 3. Per-item notes for the hard cases

### 3.1 Mesa / V3D / GL / V3DV — `TOOLS-JUSTIFIED` (named mechanism)

The three archives `tools/.gpu-libs/lib{v3d,GL,v3dv}-phoenix.a` are ~99 % compiled
Mesa. Their build is **not** "tarball + patches":

- It needs a **host** `meson setup` of `external/mesa` to emit
  `compile_commands.json`, which is the per-file compiler-flag source the
  cross-compile scripts *harvest*; `build-showcase-apps.sh:316-320` and
  `:340-359` then `ninja` ~88 codegen targets individually to materialise
  generated C (nir opcodes, glapi dispatch, v3dv entrypoints).
- A full host `ninja` **cannot** succeed on x86 (`v3d_resource.c` contains
  aarch64 `dc civac` cache asm), so the flow is deliberately best-effort and the
  caller verifies specific outputs (`ninja_mesa_soft()`, `:285-299`).
- The inputs include a 1.2 GB `external/mesa` clone plus a Python/meson/mako
  toolchain in a `uv` venv (`ensure_mesa_pyenv()`, `:226-234`).
- The outputs are **static archives consumed by five other builds via absolute
  path**: `quakespasm/port.def.sh:97`, `yquake2:84`, `quake3:115`, `vkquake:134`,
  `supertuxkart:99`, each with a `b_die` if absent. That is why phase `gpu` must
  precede `build.sh ports` (`build-showcase-apps.sh:18-23`).

The framework's `port.def.sh` model has no way to express "host meson configure →
harvest a compile database → cross-compile a subset → publish an archive other
ports link by absolute path". Relocating the *glue* (not Mesa) into
`phoenix-rtos-devices` was already done on 2026-09-02 (D9 option A) and the
remaining `tools/.gpu-libs/` + `external/mesa` residency is **owner-gated**
(decision D2, `WEEK-2026-W36.md:16,73`). **Do not re-litigate it in this
migration**; treat it as the one accepted wart, and note that portifying it would
remove the absolute-path coupling from all five game ports at once.

What is load-bearing in `tools/v3d-driver-port/` (deleting breaks the archives):
`harness_screen_create.c` (compiled by `build-v3d-phoenix.py:281,288`, linked
`:386-388`), `v3dv_harness.c` (82 KB, `build-v3dv-phoenix.py:247-251,292`),
`triangle_spirv.h` + its generator. Everything else there is a dev probe
(`csd_*.c`, `mlp_gpu.c`, `gl_*_smoke.c`, `texprobe/`, the `gpu-*.sh` drivers) or a
reference duplicate of a UAPI header now vendored in devices.

### 3.2 The X11 stack — the genuinely tangled one

Post-migration the picture is:

- **What ships from the framework:** `xorg_libs`, `xorg_fonts`, `xorg_server`
  (→ stripped `/usr/bin/Xphoenix`), `xterm`, `windowmaker`, `xorg_apps`, `xbill`
  — all `if: true`, all staged by the `ports` stage.
- **Why `build-x11-phoenix.sh` still runs (hard-fail) anyway:** it is now
  essentially *only* a seeder for `/tmp/x11-phoenix`, which the still-ad-hoc
  `glib2` (`build-glib2.sh:46`) and `mc` (`build-mc.sh:65`) read zlib/png/jpeg
  from, plus the glamor server steps (`build-xserver-core.sh:30,85,113`,
  `build-xfbdev.sh:30`), `build-gl-x11-window.sh:40,64`,
  `libmd-phoenix/build.sh:13` and `stage-x11-runtime.sh:37,57,66`.
  `build-showcase-apps.sh:503-510` says exactly this. **So the ad-hoc X11 lib
  stack cannot be deleted until glib2+mc are off it** — that is the single
  dependency that gates the largest cleanup in this audit.
- **What has NO framework equivalent and is the real reason a `tools/` X11
  presence survives:** the glamor / GPU-accelerated X path.
  1. `tools/x11-port/glamor-shim/` — `glamor_phoenix_ctx.c` (in-process V3D GL
     context + `glamor_phx_screen_readback`), `epoxy_shim.c`, the `epoxy/`
     headers.
  2. A **503-line `#ifdef GLAMOR_PHOENIX` block** in `tools/x11-port/ddx/fbdev.c`
     (GL-texture root pixmap, `fbdevGlamorBackScreenPixmap()`, the shadow-RAM
     card cursor). The framework's `xorg_server/files/ddx/fbdev.c` is the
     **2026-08-20 pre-glamor snapshot** (1020 lines vs 1523).
  3. `patches/xorg-server-1.20.14-glamor-{rgba-upload,screen-upload-yflip}.patch`.
  4. The `--glamor` build logic itself: an `--enable-glamor` reconfigure of the
     *same* tarball with `GLAMOR_CFLAGS` pointed at the epoxy shim to bypass
     `PKG_CHECK_MODULES([GLAMOR],[epoxy])`, an `ar d` winsys/power swap for
     `libv3d-client.a`, and a link against `tools/.gpu-libs/*.a`.

  The framework cannot express **two different configurations of one port**
  (`--disable-glamor` Xphoenix *and* `--enable-glamor` Xphoenix-glamor-daemon)
  from a single `port.def.sh` version. That is the named mechanism for
  `TOOLS-JUSTIFIED` on item 74. Everything *else* in `tools/x11-port/` is either
  a migrated duplicate or dev-only.

- **Real source contributions that would be lost if `tools/x11-port/` were
  deleted today** (beyond the glamor set): the XKB keymap **generator**
  (`xkb/gen-builtin-keymap.sh` + `us-pc105.keymap` + `default.xkm`; the framework
  carries only the generated `builtin_keymap.h`); the two WindowMaker patches
  (§4 D13); `fontconfig/fonts.conf` (**live** — read by
  `stage-desktop-fonts.sh:37`, which `sync-netboot-tree.sh:109` runs on every
  restage); `stage-x11-runtime.sh`'s payload (libX11 locale DB, the misc PCF font
  dir, the encodings DB — staged by nothing else, and the script has **no
  automated caller**); and `PROGRESS.md` / `WMAKER-PORT-STATUS.md`.

- Already safely duplicated (byte-identical in both trees, so the `tools/` copy
  is deletable): `ddx/{ddxLoad.c,fbdev_stub.c,hid_evdev_map.h}`,
  `xkb/builtin_keymap.h`, `libmd-phoenix/sha1.[ch]`, the xterm drop-ins,
  `ftw-phoenix/{ftw.c,ftw.h,wmaker-phoenix-compat.h}`, `xbill-phoenix-shim.h`,
  `xedit-phoenix-shim.h`, and 5 of the 11 patches.

### 3.3 SDL2 — not dual-personality; the three "build-*-port.sh" scripts are drivers

`scripts/build-port.sh`, `scripts/build-sdl2-port.sh` and
`scripts/build-xorg-ports.sh` all do the same thing: replicate `build.sh`'s port
environment (`source build.subr` + `build.project`, export `PREFIX_*`, capture
`EXPORT_CFLAGS` from `Makefile.common`), write a one-off `ports.yaml` to
`mktemp`, and end with
`PHOENIX_VER=… python3 ./port_manager.py build <yaml> sources/phoenix-rtos-ports`.
They are **framework drivers**, not second recipes. `build-port.sh` is the
generic one and supersedes the other two (`build-port.sh:6`); `build-xorg-ports.sh`
is itself now stale since the xorg ports flipped `if: true`.

`libSDL2.a` for the shipped games comes from the framework `sdl2` port, pulled by
`depends="sdl2"` on quakespasm/yquake2/quake3/supertuxkart, plus the shared
`sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_gl{ctx,stubs}.c` referenced by
absolute path from each game port. `tools/sdl2-port/*.py` read
`${BUILD}/lib/libSDL2.a` and print "run scripts/build-sdl2-port.sh first" — they
are consumers.

### 3.4 The five game engines

All five are `if: true` and install into `/usr/bin`; the ad-hoc engine archives
(`libquakespasm.a`, `libvkquake.a`) and the `_user/rpi4-{quake,vkquake}`
loader.disk wrappers were deleted 2026-09-03 (D10). **This supersedes
`docs/misc/2026-09-02-game-source-of-truth-audit.md`, whose headline "all four
ports are `if: false` → the ports path builds nothing" is no longer true.**

- **quakespasm**: the framework patch is a verified **superset** (809 lines / 29
  hunks / 12 files vs legacy 349 / 11 / 6; every legacy hunk found at a matching
  anchor), with one deliberate behaviour change — `r_alias.c` gains an
  `r_alias_lerpmode` cvar and flips the default from pose-snap to real 2-pose
  lerp. `platform/pl_phoenix_{stubs,sys}.c` are byte-identical to `glue/`.
  **But** `platform/pl_phoenix_{in,snd,vid}.c` + `sdl-shim/` are the **only copy
  of the raw-framebuffer non-SDL variant** — dead in the build graph, no
  framework home. That needs an owner decision (keep as a documented variant, or
  drop); do not delete it silently. `platform/pl_phoenix_in.c` is additionally
  cited as the HID→event reference by `tools/x11-port/PROGRESS.md:303`.
- **vkquake**: `vkquake_shaders.c` (1.14 MB of committed SPIR-V) and
  `vk_trampolines.c` are **copied into** `vkquake/glue/` byte-identically, not
  referenced from `tools/` — so the port is self-contained, and glslang is not a
  build dep. The generators `gen-vkquake-shaders.py` / `gen-vk-trampolines.py`
  remain the mandated regeneration path (`vkquake/port.def.sh:96-103`) and must
  stay. **Active drift here — see D27.**
- **yquake2 / quake3 / supertuxkart**: never had an ad-hoc engine recipe in
  `tools/`; only launchers, a Q3 reference demo, and a dead STK seed dir. Nothing
  to migrate.

### 3.5 python and `_curses` — the shipped interpreter has an unbuilt module

`python` flipped `if: true` today, so `/bin/python3` and
`/usr/local/lib/python3.14` now come from the framework port. Two things follow:

- **`import curses` has no framework path.** `sources/phoenix-rtos-ports/python/`
  contains only `Python-3.14.4.tar.xz`, `Setup.local`, `config.site`,
  `phoenix-py-compat.h`, `port.def.sh`; grep for `curses` returns nothing, and
  the port does not depend on `ncurses`. `_curses.cpython-314.so` exists only via
  `tools/python-port/build-curses.sh`, which additionally needs the **framework**
  `ncurses` port (only that one is `-fPIC`) — and nothing in the image flow
  builds `ncurses` either. So curses requires two out-of-band manual steps and is
  therefore a regression against the HW-proven 2026-09-01 state.
- **`build-curses.sh` clobbers the shipped interpreter.** `:56` copies
  `/tmp/python-port-build/Python-3.14.4/python` over `$STAGE/bin/python3` and
  `:60` `rsync --delete`s the stdlib. Run after a build, it silently replaces the
  framework binary with an ad-hoc one — the same trap shape as `nano`.

The clean fix is inside the framework port: add `ncurses` to `depends`, add
`_curses` to `Setup.local` (static, which removes the dlopen dependency
altogether) or reproduce the `.so` build in `p_build`, and move `curses_shim.h`
next to `port.def.sh`.

### 3.6 ffmpeg — a framework port that nothing builds, and demos that need the other one

`ffmpeg/port.def.sh` does build `libav{util,codec,format}.a` +
`libsw{scale,resample}.a`, but it is `if: false` and no port depends on it, so
**no image contains it**. Meanwhile the E4 players hardcode the *legacy* output
location: `build-e4-play.sh:18` and `build-e4-x11-play.sh:35-37,55-57` expect
`external/ffmpeg/libav*/libav*.a`, which only
`tools/ffmpeg-port/build-ffmpeg-phoenix.py` produces. The port installs to
`PREFIX_A`/`PREFIX_H` instead. So the `.py` cannot be deleted until the players
are repointed — but the players themselves correctly stay in `tools/`
(`ffmpeg/port.def.sh:33-36` says so).

---

## 4. Divergences found — 34 latent bugs and drifts

Ordered worst first. "Latent" means present in the trees today, whether or not it
has fired.

### D1 — WORST: the legacy `libiconv` is not libiconv

`tools/ports/build-libiconv.sh` does **not build GNU libiconv at all**. It
compiles a self-written **identity stub** (`tools/ports/iconv-stub/iconv.c`, an
ASCII/UTF-8 `memcpy` with SUSv4 pointer/E2BIG semantics) — rationale at
`build-libiconv.sh:9-23`, build at `:46-50` (one `gcc -c` + `ar rcs`, no
`configure`). The framework port is **real GNU libiconv 1.18**
(`libiconv/port.def.sh:8-9`).

Consequences:

- Everything on the legacy chain — ad-hoc `glib2` → shipped `mc`, and the legacy
  `dillo` link — **cannot transcode legacy single-byte codepages** (Latin-1,
  CP1250, KOI8 pages in Dillo; non-UTF-8 filenames in mc). It only looks correct
  because the default traffic is UTF-8/ASCII.
- The stub is planted in the **shared dev sysroot**:
  `build-libiconv.sh:56` writes `$SYSROOT/usr/include/iconv.h` and
  `$SYSROOT/lib/libiconv.a`. Any framework port that does not put
  `-I${PREFIX_H}` ahead of the sysroot include path, or whose link reaches
  `-L$SYSROOT/lib` before `-L${PREFIX_A}`, gets the stub instead of GNU 1.18.
- This is the single strongest argument for the owner's rule in the whole audit:
  two "ports" with the same name are not two builds of one thing, they are two
  *different pieces of software*.

Also: `tools/ports/iconv-phoenix-shim.h` is referenced by nothing anywhere — a
dead relic of an abandoned real-libiconv attempt.

### D2 — libffi version split inside one image

`tools/ports/build-libffi.sh:20` builds **libffi 3.3** (2019) into
`/tmp/phoenix-ffi` + `$SYSROOT/lib`; `libffi/port.def.sh:8` is **3.4.6**. Both
are built in a default showcase image: the framework copy via `python`'s
`depends=`, the legacy copy via `build-showcase-apps.sh:500`. So
`_ctypes` in the shipped `python3` links 3.4.6 while ad-hoc `glib2` → shipped
`mc` links 3.3 — five years of aarch64 fixes apart, and 3.4 changed the
closure / `ffi_prep_cif_var` surface. `python/port.def.sh:142` already asserts
"libffi 3.4 has all of them", which only holds on the framework path;
`tools/python-port/build.sh:141` still documents 3.3.

### D3 — zlib version split, and the old one is what ships

`build-x11-phoenix.sh:210-213` builds **zlib 1.3.1**; the framework `zlib` port
is **1.2.11** (2017). `build-glib2.sh:46,55-56` then copies `libz.a`/`zlib.h`
from `/tmp/x11-phoenix` into the shared sysroot, so 1.3.1 lands there too. The
framework 1.2.11 is what `python`, `curl`, `libpng`, `xorg_fonts` and the games
link. Given the image is to be **published**, 1.2.11 predates
CVE-2018-25032 and CVE-2022-37434; worth a security look independently of this
migration.

### D3b — `build-curses.sh` overwrites the shipped `python3`

See §3.5. `tools/python-port/build-curses.sh:56,60`. Same trap shape as nano.

### D4 — libXt version mismatch

`build-x11-phoenix.sh:280` builds **libXt 1.3.0**; `xorg_libs/port.def.sh:190`
builds **1.3.1**. Only tarball in the legacy `src/` cache is 1.3.0. Xaw-based
apps therefore link a different Xt depending on which side built them.

### D5 — ncurses `-fPIC` only on the framework side

`ncurses/port.def.sh:44` adds `-fPIC` (ports commit `c5812a1`);
`build-ncurses.sh:54` does not. Load-bearing: only the PIC archive can be folded
into `_curses.cpython-314.so`.

### D6 — the framework `mc` port still carries the retired mntent stub

`mc/port.def.sh:65` stages `mc-support/mntent.h` into `${PREFIX_H}` and
`:69,72-73` compiles + archives `mntent-stub.o` into `libmcsupport.a`. The
legacy script **deliberately removed both**, with the failure analysis at
`build-mc.sh:75-89`: libphoenix now implements the whole family (`29f5373`), so
(a) the stub header shadows the real one and lacks `hasmntopt`, and (b)
`mntent-stub.o` multiply-defines `setmntent`/`getmntent` against libphoenix. It
is masked today only because the framework's `mc.cache` adds
`ac_cv_func_hasmntopt=no` (the sole diff between the two caches). Net effect: a
framework-built `mc` gets a stubbed "no mounts" mount table; the legacy-built one
(which ships) gets the real one. **This is the one place where migrating as-is
would be a regression** — fix the port first.

### D6b — ncurses header layout differs, and the difference is load-bearing

Framework mirrors 8 headers from `include/ncurses/` up to the include root
(`ncurses/port.def.sh:55-57`) so `#include <curses.h>` resolves. Legacy copies
into `$SYSROOT/include` *and* `$SYSROOT/include/ncurses/`, producing a nested
`ncurses/ncurses/curses.h` and **no root `curses.h`** — which is why
`build-mc.sh:171` and `build-nano.sh:60` must pass `-I.../include/ncurses`
explicitly. Also: legacy uses `$SYSROOT/include`, while `build-libiconv.sh:56`
and `build-libffi.sh:73` use `$SYSROOT/usr/include` — inconsistent even among
themselves.

### D7 — the legacy `python` `config.site` is missing five forces

`sources/phoenix-rtos-ports/python/config.site:135-145` adds
`ac_cv_func_sched_{getparam,setparam,getscheduler,setscheduler,rr_get_interval}=no`.
Phoenix `<sched.h>` *declares* but does not *define* these, so a compile-only
probe sets `HAVE_*=yes` and `posixmodule.c` emits calls that fail the static link.
`tools/python-port/config.site` lacks them, so the legacy build is the broken
side here. `Setup.local` and `phoenix-py-compat.h` are byte-identical.

### D8 — `_curses` exists on neither the framework nor an automated path

See §3.5. `curses_shim.h`, `build-curses.sh` and `build-extension.sh` are the
only copies of that recipe anywhere.

### D9 — python stdlib staging differs

Framework: `cp -a Lib/. → .../usr/local/lib/python3.14/` — the **whole tree
including `test/`** (`python/port.def.sh:192-193`), and no `--delete`, so files
dropped upstream linger. Legacy `build-curses.sh:60`: `rsync -a --delete
--exclude 'test/' --exclude 'tests/' --exclude '__pycache__/'`. The framework
therefore ships tens of MB of CPython test suite into the rootfs.

### D10 / D11 — stripped vs unstripped

Framework ports strip (`$STRIP -o ${PREFIX_PROG_STRIPPED}/…` then `b_install`).
Legacy scripts `cp` the raw link output: `build-nano.sh:80`, `build-mc.sh:241-246`,
`build-dillo.sh:247-248`, `build-xfbdev.sh:262-280`. Since the legacy copy is the
one that ships for nano and mc, `/bin/{nano,mc}` are unstripped today. (The
framework `python` port deliberately installs **non**-stripped, with a stated
reason: the dlopen extension recipe resolves the Py C-API against `python3`'s
`.symtab`.)

### D12 — dillo: iconv and the missing `dillorc`

Framework link closure includes `-liconv` inside the `--start-group`
(`dillo/port.def.sh:71-76`); the legacy one omits it and relies on ambient
sysroot resolution — i.e. the D1 stub. The legacy patch set is otherwise
**equivalent**: its two guarded inline `sed -i` edits (`dw/selection.cc` strndup
rename, `src/IO/http.c` `uint_t → socklen_t connect_ret_size`, a real 4-byte
stack overwrite on every HTTP connect) match the framework's
`patches/0001-*`/`0002-*` hunk-for-hunk. **But the framework port stages no
`dillorc`**, while `build-dillo.sh:250-251` stages it to `/etc/dillo/dillorc` — a
runtime-defaults regression on the framework path. The legacy header comment at
`:160-165` is also stale (still calls the socklen_t bug unfixed). Framework-only
improvement worth keeping: it prepends `${PREFIX_PORT_INSTALL}/bin` to `PATH` so
configure picks the cross `libpng16-config`, not the host's.

### D13 — WindowMaker: three source fixes exist only in `tools/`, and the framework is what ships

`windowmaker/port.def.sh:64` calls `b_port_apply_patches` but there is **no
`patches/` directory**, so it is a no-op. Legacy applies three things
unconditionally:

1. `WindowMaker-0.95.9-phx-diag.patch` (432 lines). Despite the name it carries
   **two** things (`build-wmaker.sh:272-283`): the `-DPHX_DIAG`-gated markers
   *and* an always-on `"phxfile:"` direct-TTF-file font path in `WINGs/wfont.c` +
   `configuration.c` defaults — the fix for the `WMCreateFont`/`XftFontOpenName`
   startup hang. `phxfile` appears **nowhere** under `sources/`.
2. `WindowMaker-0.95.9-phoenix-getcommandforpid.patch` — a real `threadsinfo()`
   based `GetCommandForPid` replacing `osdep_stub.c`'s warn-and-return-False
   (session save/restore).
3. An inline `perl` edit adding the `#ifndef WMAKER_SHELL` guard to `src/main.c`
   so `-DWMAKER_SHELL` takes effect. The framework deliberately skips this one
   and documents why (`windowmaker:52-56`) — stock wmaker hardcodes `/bin/sh`,
   which is correct on the Pi.

This is the **inverse of the nano trap**: there, `tools/` shipped an unpatched
tarball while the fix sat in the port dir; here the framework ships and the fixes
sit in the `tools/` tree. (1) and (2) are not currently *broken* — the generic
font path is covered by `stage-desktop-fonts.sh` mapping generic families to
DejaVu in `fonts.conf`, and the desktop is HW-proven — but they are unreconciled
and would be lost.

### D14 — WindowMaker staging: the framework installs only the main binary

Legacy `build-wmaker.sh:404-405` copies **all** of `bin/`, i.e. `wmsetbg`,
`wdwrite`, `wmgenmenu`, plus `share/{WindowMaker,WINGs,WPrefs}`,
`etc/WindowMaker`, the defaults-DB font rewrite, DejaVu TTFs, `etc/fonts/fonts.conf`
and `var/cache/fontconfig` (`:407-461`), then runs a pre-flight (0 undefined
symbols; `/bin/sh`, `/share/WindowMaker`, `/etc/WindowMaker`, `/etc/fonts` all
verified baked in). Framework copies only `src/wmaker` + `usr/share/{WindowMaker,
WINGs}` + `etc/WindowMaker` (`windowmaker:125-139`). **No `wmsetbg`, no
`WPrefs`, no fonts, no pre-flight.** Per project memory `wmsetbg` is load-bearing
for the desktop background; today it survives only on the hand-maintained NFS
export, so a pristine re-export loses it.

### D15 / D16 — WindowMaker configure deltas

Legacy `--enable-jpeg` with `-ljpeg` in the closure (`build-wmaker.sh:261,363-373`)
vs framework `--disable-jpeg`, no `-ljpeg` (`windowmaker:105,111-120`) — JPEG
backgrounds/themes work on one side only. Legacy `--prefix=/ --datadir=/share
--bindir=/bin` bakes `/share/WindowMaker`; framework uses `--datarootdir=/usr/share`
→ `/usr/share/WindowMaker`. Legacy also passes `-Wl,-z,stack-size=0x100000`, which
the framework drops — now **redundant**, since
`phoenix-rtos-kernel/hal/aarch64/arch/cpu.h:39` sets `SIZE_USTACK` to 1 MiB.

### D17 — `xorg_server/files/ddx/fbdev.c` is a stale snapshot of a file we own

Framework copy: 1020 lines, mtime 2026-08-20. `tools/x11-port/ddx/fbdev.c`: 1523
lines, mtime 2026-08-27. The 503-line delta is entirely inside
`#ifdef GLAMOR_PHOENIX`, and the framework configures `--disable-glamor` and never
defines it — so the *shipping* Xphoenix is functionally identical. But the
framework tree now holds a **pre-glamor source-of-truth for a file we author**,
and the GPU-accelerated X work exists only under `tools/`. Also: framework
`p_build` copies only `fbdev.c` + `hid_evdev_map.h` (`:92`), so the carried
`files/ddx/fbdev_stub.c` is unused (legacy uses it for a `--stub` de-risk mode).

### D18 — three xorg-server patches applied only by the legacy build

`xorg-server-1.20.14-record-malloc0.patch` (applied at `build-xfbdev.sh:83-88`)
is **obsolete** and the framework documents why (`xorg_server:37-42`: libphoenix
`malloc(0)` is now non-NULL) — so the legacy build applies an unnecessary patch.
The two glamor patches are legacy-only because the framework has no glamor path.

### D19 — Xphoenix install path and strip differ

Framework: stripped → **`/usr/bin/Xphoenix`** (`xorg_server:119-127`). Legacy:
unstripped → **`/bin/Xphoenix`** (`build-xfbdev.sh:262-280`).
`build-showcase-apps.sh:533-534` flags this explicitly, while
`launcher/pl_phoenix_xlaunch.c` was written against `/bin/Xphoenix`. **Check the
launcher's exec path before removing anything that stages `/bin/Xphoenix`.**

### D20 — xterm: `-liconv` and terminfo

Legacy `XCLOSURE` includes `-liconv` (`build-xterm.sh:70`); framework's does not
(`xterm:104`) — notable because `xorg_apps` needed an explicit
`depends="xorg_libs libiconv"` for exactly that reason. The framework port has
passed a clean build, so treat it as an unexplained closure difference rather
than a confirmed break. Separately: legacy stages terminfo entries
(`x/xterm`, `x/xterm-256color`, `v/vt100`) into `/usr/share/terminfo`
(`build-xterm.sh:183-186`); **the framework stages none.**

### D21 — xbill: legacy clones unpinned git HEAD

`build-xbill.sh:46,73` does `git clone --depth 1 https://github.com/alistairmcmillan/Xbill.git`.
Framework pins commit `75f47443…` as a committed tarball with a sha256
(`xbill/port.def.sh:23-27`). The framework side is reproducible; the legacy side
is not. (The bespoke build is otherwise a faithful transplant.)

### D22 — harfbuzz build system change breaks the (legacy-only) pango chain

Legacy: autotools, `--with-freetype=yes --with-glib=yes` (`build-harfbuzz.sh:19-20`).
Framework: CMake, `HB_HAVE_GLIB=OFF` (`harfbuzz/port.def.sh:72-77`) with a
hand-written `harfbuzz.pc`. `build-pango.sh:14-15` records that harfbuzz **must**
be `--with-glib=yes` for `pangofc-shape.c`'s `<hb-glib.h>`. So the framework
harfbuzz as configured **cannot satisfy pango**, and the whole
glib → cairo → harfbuzz+fribidi → pango chain is legacy-only and mutually
incompatible with the framework stack. Nothing ships from it today (pango and
fribidi have no framework port and no invoked legacy step), but it means "migrate
pango later" is not a mechanical move.

### D23 — libXfont2 patch applied only by the legacy build (documented redundant)

`libXfont2-2.0.6-phoenix-fdopen-rt.patch` is applied by
`build-x11-phoenix.sh:256` and absent from `xorg_fonts:139-152`. The patch's own
header records that libphoenix `string2mode()` now accepts `'t'`, so pristine
`fdopen("rt")` works — the framework omission is correct. But `tools/` holds the
only record of the #56 font-path-collapse diagnosis.

### D24 — `-std=gnu17` present on the framework side, absent on the legacy side

Framework adds it to pixman (`xorg_libs:176`), libpng (`xorg_fonts:97`),
xorg-server (`xorg_server:78`), xterm (`xterm:85+`) and wmaker; the corresponding
legacy scripts do not (`build-x11-phoenix.sh:200,223`, `build-xserver-core.sh:158`).
Under gcc-16's C23 default this is exactly the class of breakage that hit glib2.
(For glib2 itself it is *not* a gap — the framework inherits gnu17 from
`Makefile.common:156`, and the legacy script sets it explicitly at
`build-glib2.sh:107`.)

### D25 / D26 — fontconfig and cairo prefix/path deltas

fontconfig `--with-default-fonts`: legacy `/usr/share/fonts`
(`build-wmaker.sh:174`) vs framework `/usr/share/fonts/truetype`
(`xorg_fonts:184`). fontconfig `--prefix`: legacy `/` (bakes target-relative
paths) vs framework `$PREFIX_BUILD` + explicit `--sysconfdir=/etc`. Framework
additionally sets `ac_cv_member_struct_statfs_f_flags=no` + three sibling cache
vars the legacy build does not. cairo: legacy links against the **in-tree,
never-installed** fontconfig (`build-cairo.sh:28,44` →
`$SRC/fontconfig-2.14.2/src/.libs`); framework uses the installed copy.

### D27 — vkquake glue drift: `tools/` is ahead, the shipping copy is behind

`tools/vkquake-port/platform/pl_phoenix_sdlcompat.c` (2026-09-02) **removed** its
local `double copysign(...)`, with a comment stating libphoenix/libm now export it
so a local copy is a duplicate definition. `sources/phoenix-rtos-ports/vkquake/glue/pl_phoenix_sdlcompat.c`
(2026-08-26) **still defines it** — and that is the copy the `if: true` port
compiles. Masked today by `vkquake/port.def.sh:251` `-Wl,--allow-multiple-definition`
plus archive ordering, so the build is not broken. `vkq_phoenix_compat.h` drifts
the same direction (comment only). **This is the only case in the audit where the
`tools/` tree holds a newer fix than the shipping recipe.** Reconcile before
deleting `tools/vkquake-port/platform/`.

### D28 — quakespasm patch: superset, with one deliberate behaviour flip

Framework `patches/0001-quakespasm-phoenix-v3d-single-elf.patch` contains every
legacy hunk at a matching anchor, plus 6 more files. The one divergent anchor
(`r_alias.c`) replaces the unconditional pose-snap with an `r_alias_lerpmode`
cvar and **flips the default to real 2-pose lerp** (#28 no longer reproduces
after the winsys fixes). Legacy behaviour is still reachable via
`r_alias_lerpmode 0`. Functional superset, one intentional change — worth
recording so it is not later mistaken for drift.

### D29 — vkquake patch: framework drops two diagnostic-only hunks

The legacy `gl_screen.c @@-1107` SCR_DrawGUI 2D bisector harness and the
`gl_texmgr.c @@-1212` `fprintf(stderr, "vkq-tex-built: …")` trace are gone from
the framework patch (verified pure diagnostics). Framework adds `host_cmd.c`
(skip auto-demo), `r_alias.c` + `alias_common.inc` (alpha=1 for fb0 scanout) and
`sys_sdl.c` (slurp-to-RAM file layer). Superset of load-bearing changes, not a
textual superset.

### D30 — ffmpeg: incompatible install locations

Framework port → `--libdir=$PREFIX_A --incdir=$PREFIX_H`. Legacy `.py` →
in-tree `external/ffmpeg/libav*/libav*.a` (`build-ffmpeg-phoenix.py:109-111`),
which is exactly what `build-e4-play.sh:18` and `build-e4-x11-play.sh:35-37`
hardcode. Configure lines are near-identical; the port adds
`--disable-autodetect` (a hermeticity fix the legacy `.py` lacks).

### D31 / D32 — caches and mirrors

The two sides use **different distfile caches**: legacy
`$HOME/.phoenix-distfiles/x11` keyed `<nv>.tar.gz`
(`build-x11-phoenix.sh:40,97`), framework `$HOME/.phoenix-distfiles/xorg` keyed
by real basename (`xorg_libs:65`, `xorg_fonts:63`) — so nothing is shared and a
migration re-downloads ~30 tarballs. Legacy also has the richer mirror expander
(`_mirror_urls`, `:72-84`). Separately, the framework `glib2` port lost the
`ftp.gnome.org` fallback mirror the legacy script carries
(`build-glib2.sh:31-33`) — a reproducibility hazard the legacy comment calls out.

### D33 — orphaned staging with no caller

`tools/x11-port/stage-x11-runtime.sh` stages the libX11 locale DB (#51), the full
host misc PCF font dir with upstream `fonts.dir`/`fonts.alias` (#56) and the X11
encodings DB — and **no script in `scripts/` or `tools/` invokes it**. Nothing
else stages those. It also reads from `/tmp/x11-phoenix`, so it silently depends
on the legacy prefix build.

### D33b — two copies of vendored FreeBSD teken

`tools/ports/teken-src/` duplicates
`sources/phoenix-rtos-devices/tty/pl011-tty/teken/`. The devices copy is the one
compiled into `pl011-tty`. No port on either side; the `tools/` copy has no
consumer.

### D34 — non-fatal library steps inside a hard-fail script

`build-x11-phoenix.sh`'s zlib/libpng/jpeg steps end in `|| echo "…: FAIL"`
(`:213,227,241`) — they print FAIL and continue. `build-glib2.sh:46,55-56` copies
zlib out of that prefix with `|| true`. So a missing zlib degrades into a
downstream link error rather than a clear failure, which
`build-showcase-apps.sh:496-498` already warns about.

### D-nano — the trap that started this (fixed today, recorded for the pattern)

`tools/ports/build-nano.sh` built the upstream tarball **unpatched** while the
framework port applied `patches/`, and per §1 the image takes the `tools/` one —
so a fix landing in the port dir would never reach the shipped binary. Fixed
2026-09-03: `build-nano.sh:29,44-49` now applies the framework port's patch dir
(`ports a732d02`, coord `eb0eec1bf`). **The same shape recurs in D3b (python) and
inverted in D13 (wmaker).** Any dual-personality item is a candidate; the check
is mechanical — does the legacy script apply the port's `patches/`, or its own?

### D-hdr — the legacy scripts mutate the shared sysroot

Not a single divergence but the enabling condition for several. Legacy scripts
write headers and archives into `.buildroot/_build/<t>/sysroot/{lib,usr/include}`,
which is on every port's `--sysroot` path: `build-libiconv.sh:56` (`iconv.h`),
`build-libffi.sh:73` (`ffi.h`), `build-ncurses.sh:66-69`, `build-glib2.sh:86,91,97-101`
(`libintl.h`, `arpa/nameser.h`, `resolv.h`, `libresolv.a`), `build-mc.sh:90,117-118`
(`langinfo.h`, `libmcsupport.a`). The already-fixed `mc` `mntent.h` incident
(`WEEK-2026-W36.md:86`) is the canonical example: one port's build mutated a
shared header, so any other port needing mntent silently got a stub. A
documented order-dependence still exists in `build-xterm.sh:134-143` — whether
`libncurses.a` happens to be in the sysroot at configure time changes xterm's
`USE_TERMINFO` decision non-deterministically across machines.

---

## 5. Migration order

### 5.0 Test methodology (the same for every step)

`scripts/compare-rootfs-binaries.sh` proves *build tree == NFS export == SD card*
for 10 fixed paths (the 5 engines, `bin/psh`, `bin/busybox`,
`usr/bin/Xphoenix`, `bin/python3`, `bin/bash`) via `sha256sum` + `debugfs dump`.
It does **not** compare a framework recipe against a legacy one. So per port:

1. **Before** the flip, from the current image: record `sha256`, byte size,
   `aarch64-phoenix-nm -u` (must be empty) and the full sorted `nm` symbol list
   of the legacy-built binary out of `.buildroot/_fs/<target>/root/<path>`, plus
   any `--version` / banner string.
2. Build the framework side standalone: `scripts/build-port.sh <name>` (clean by
   default; `--incremental` to skip re-extract). This is a genuine `port_manager`
   run — the same mechanism `build.sh ports` uses — so a pass here is real
   evidence.
3. Compare: **byte equality is not expected and not the goal** (the legacy copies
   are unstripped, the framework ones stripped, and build paths are embedded).
   The meaningful equivalence checks are: 0 undefined symbols, the defined-symbol
   set is a superset-or-equal, size within a sane band once you account for
   `strip`, and the same linked-library set (`nm | grep` for the marker symbols
   of each dependency — e.g. `libiconv_open` vs the stub's `iconv_open`).
4. Flip `if: true`, remove the legacy step from `build-showcase-apps.sh` **in the
   same commit** (never leave both enabled — that double-builds and stages two
   binaries), rebuild `--with-showcase`, then run
   `scripts/compare-rootfs-binaries.sh` to confirm the export and the card agree,
   and add the binary to that script's `PATHS` list if it belongs there.
5. Boot smoke on the Pi. **Note the export hazard:**
   `scripts/sync-netboot-tree.sh:15` rsyncs **without `--delete`** "so
   hand-staged extras survive", so a stale binary from a removed legacy step
   persists on the NFS export and can mask a failure. Either test against a
   pristine export (`make-pristine-nfs-export.sh`) or against the SD image.
6. Before deleting any `tools/` directory, **edit
   `scripts/build-showcase-apps.sh:185-198` first** — `archive_fresh()` `die`s on
   a missing path for `tools/{v3d-driver-port,quakespasm-port,vkquake-port}`.

### 5.1 Ordered plan

**Tier 0 — fix the framework side first (no flips, no `tools/` deletions).**
These are port-repo-only changes and must land before anything is flipped,
because two of them would otherwise turn a migration into a regression.

| # | change | why first |
| --- | --- | --- |
| 0a | `mc` port: drop the `mntent.h` staging + `mntent-stub.o` + the `ac_cv_func_hasmntopt=no` cache line (D6) | flipping `mc` today ships a worse `mc` than we have |
| 0b | `python` port: add `ncurses` to `depends`, add `_curses` to `Setup.local` (static — no dlopen needed), copy `curses_shim.h` next to `port.def.sh`; exclude `test/`/`tests/` from the stdlib stage (D8, D9) | `python` is *already* `if: true`, so `import curses` is broken in the image right now |
| 0c | `dillo` port: stage `dillorc` → `/etc/dillo/dillorc` (D12) | dillo is already `if: true`; this is a live gap |
| 0d | `windowmaker` port: bring across `wmsetbg`/`wdwrite`/`wmgenmenu` + `share/WPrefs`, and decide on the two legacy patches (D13, D14) | wmaker is already `if: true`; `wmsetbg` currently survives only on the hand-maintained export |
| 0e | `vkquake` port: port the `copysign` removal from `tools/vkquake-port/platform/pl_phoenix_sdlcompat.c` into `glue/` (D27) | the shipping recipe is the stale one |
| 0f | correct the stale `windowmaker/port.def.sh:3-8` + `README.md` "DRAFT / canonical build is tools/" text | it contradicts `ports.yaml:83` |

Each of Tier 0 is verifiable with `scripts/build-port.sh <name>` plus one image
build; **0b and 0d want a Pi cycle** (curses smoke; desktop background).

**Tier 1 — safe, one per Pi cycle, in this order.** Each is a leaf: nothing else
reads its output.

1. **`nano`** — smallest possible flip, and the patch sets are already unified
   (D-nano). Flip `if: true`, delete the `build-showcase-apps.sh:519` step. What
   breaks if the script vanished today: nothing else; `build-nano.sh` is the only
   consumer of `/tmp/phoenix-ncurses` besides mc. Test: `/bin/nano /etc/hostname`
   under `TERM=vt100`. **Do this one first — it is the reference migration.**
2. **`mc`** (after 0a) — flip `if: true`, delete step `:520`. Framework `mc` pulls
   `ncurses` + `glib2` + `libiconv` through `depends=`, which means this single
   flip puts **mc's whole chain** — glib2, ncurses and real GNU libiconv — on
   the framework path for the first time. (libiconv itself is already
   framework-built via `dillo`'s `depends=`; what is new is that *mc* gets the
   real one instead of the stub.) Breakage if the script vanished today: the legacy
   `libmcsupport.a` and `langinfo.h` in the shared sysroot disappear (good), and
   the four diagnostic variants (`mc-ascii`/`mc-dbg`/`mc-guard`) go — delete them
   per the project's diagnostic-code rule rather than migrating. Test: mount
   table shows real mounts (the D6 check), skins render, F-keys work.
3. **`libiconv` + `libffi` + `ncurses` + `glib2` step removal** — once (2) is in,
   these four legacy steps (`build-showcase-apps.sh:499,500,501,506`) have **no
   remaining consumer**, so delete all four in one commit. This is the change
   that kills **D1, D2, D5, D6b and most of D-hdr** at once, and it is the single
   highest-value item in the audit. Then delete `tools/ports/iconv-stub/`,
   `iconv-phoenix-shim.h`, `mc-dbg-instrument.patch`, `mc-support/mc-guard-wrap.c`,
   `mc-repro/`, `teken-src/` (D33b). Test: full `--with-showcase` build + boot;
   verify `nm` on `/bin/mc` and `/bin/dillo` shows GNU libiconv symbols, not the
   stub's.
4. **Retire the dead scripts that ship nothing** (zero-risk, no Pi cycle needed —
   but do it as its own commit so a bisect is clean):
   `tools/ports/build-{fltk,dillo,dillo-dbg}.sh`,
   `tools/x11-port/build-{xterm,wmaker,xcalc,xclock,xlogo,xedit,xbill,cairo,harfbuzz,ico,oclock,jwm,xcalc-dbg,xcolortest,x11-cursor-sweep}.sh`,
   `tools/python-port/build.sh`, the legacy engine `.py` files, and all
   `__pycache__/`. Keep: the `vkquake` generators, `tools/supertuxkart-port/stk-launcher.c`,
   `tools/quake3-port/demos/cap.dm_68` (used by `scripts/quake3-host-capture.sh:38`),
   the E4 players, the SDL2 smoke tests, `fontconfig/fonts.conf` (live). Delete
   `tools/supertuxkart-port/savedir-seed/` (dead — `stk-launcher.c:51,70` embeds
   the XML as string literals). Fix the two stale READMEs.

**Tier 2 — needs care; not one-per-cycle.**

5. **`ffmpeg`** — repoint `build-e4-play.sh` / `build-e4-x11-play.sh` at
   `PREFIX_A`/`PREFIX_H` (D30), then flip `ffmpeg` `if: true` (or give the E4
   players a `depends`-style prerequisite), then delete
   `build-ffmpeg-phoenix.py`. Two moving parts in different trees; do the repoint
   and prove the players still build **before** touching the flag.
6. **The ad-hoc X11 lib stack (`build-x11-phoenix.sh`)** — deletable only *after*
   step 3, because glib2 and mc are its last two real consumers. Even then, four
   other things still read `/tmp/x11-phoenix`: the glamor server steps,
   `build-gl-x11-window.sh`, `libmd-phoenix/build.sh` and
   `stage-x11-runtime.sh`. So the sequence is: (a) repoint
   `libmd-phoenix/build.sh` and `build-gl-x11-window.sh` at `PREFIX_A`/`PREFIX_H`;
   (b) decide what to do with `stage-x11-runtime.sh` (it has no caller — either
   wire it in properly or fold its payload into `xorg_fonts`/a staging script and
   delete it, D33); (c) repoint the glamor steps; (d) then remove step `:505` and
   the script. Also resolve D4 (libXt 1.3.0 vs 1.3.1) and D3 (zlib 1.3.1 vs
   1.2.11) as part of this — the legacy prefix is where the second zlib comes
   from.
7. **The glamor / GPU-X path** — the only genuinely `TOOLS-JUSTIFIED` X11 piece
   (§3.2). Two options, both owner-decisions, neither a mechanical migration:
   either add a second framework port (`xorg_server_glamor`, same tarball,
   different configure + the glamor patches + the shim), or accept a permanent
   `tools/` residency and at minimum **reconcile
   `xorg_server/files/ddx/fbdev.c` with the newer `tools/` copy** so the
   source-of-truth for a file we author is not a stale snapshot (D17). Do the
   reconcile regardless of which option wins.
8. **pango / fribidi / gdk-pixbuf** — currently ship nothing and are blocked on
   D22 (the framework harfbuzz is `HB_HAVE_GLIB=OFF` and cannot satisfy pango).
   Either delete these three legacy scripts as dead, or treat "pango on Phoenix"
   as a new port project with a `HB_HAVE_GLIB=ON` harfbuzz variant. Not part of a
   deduplication pass.
9. **Mesa / V3D / GL / V3DV** — `TOOLS-JUSTIFIED`, owner-gated (D2/D9). Out of
   scope for this migration by prior decision.

### 5.2 What would break if a `tools/` script were deleted today — quick reference

| script | breaks immediately |
| --- | --- |
| `build-libiconv.sh` | `build-glib2.sh` (`-liconv` unresolved) → mc, and any framework port that was silently resolving iconv from the sysroot |
| `build-libffi.sh` | `build-glib2.sh` gmodule/gobject |
| `build-ncurses.sh` | `build-nano.sh:34` and `build-mc.sh:73` self-invoke it; both fail |
| `build-glib2.sh` | `build-mc.sh:72` self-invokes it; mc fails |
| `build-nano.sh` / `build-mc.sh` | `/bin/nano` / `/bin/mc` disappear from the image (framework copies are `if: false`) |
| `build-x11-phoenix.sh` | glib2, mc, the glamor steps, `build-gl-x11-window.sh`, `libmd-phoenix/build.sh`, `stage-x11-runtime.sh` — all lose `/tmp/x11-phoenix` |
| `build-xserver-core.sh --glamor` / `build-xfbdev.sh --glamor-daemon` | `/bin/Xphoenix-glamor-daemon` (concurrent-GPU X desktop) disappears; nothing else provides it |
| `build-gl-x11-window.sh` | `/bin/gl-x11-window-daemon` disappears |
| `build-xlaunch.sh` | `startx` / `startx_gpu` disappear; psh cannot start X |
| `build-rootfs-helpers.sh` | `/usr/bin/{quake2,quake3,pty-run}`, `/bin/{stk,ram-stage-play}` — three of five games lose their entry point |
| any of `tools/{v3d-driver-port,quakespasm-port,vkquake-port}` (the **directory**) | **every `--phase gpu` build dies** at `build-showcase-apps.sh:194` — edit that list first |
| `build-ffmpeg-phoenix.py` | `build-e4-play.sh`, `build-e4-x11-play.sh` |
| everything else under `tools/**/build-*.sh` | nothing — not invoked by any build path |

---

## 6. Open questions / things I could not determine from the trees

- **Whether the framework `xterm`'s missing `-liconv` (D20) is actually safe.**
  The port has passed a clean build, which is evidence but not proof for every
  locale path. Not resolvable by reading.
- **Whether the shipped framework `wmaker` needs the `phxfile:` font path
  (D13.1).** It works today, and the plausible reason is that
  `stage-desktop-fonts.sh`'s generic-family→DejaVu aliases avoid the
  `FcFontMatch` scan the patch was written to bypass. But nothing in the port
  records that reasoning, so this is inference, not a traced fact.
- **The three-way libpng/jpeg duplication (rows 49-50).** `libpng` exists as a
  standalone framework port *and* is built again inside `xorg_fonts`, *and* again
  by the legacy prefix. Same for jpeg (`libjpeg` 3.0.4 port vs `jpeg-9e` inside
  `xorg_fonts`). Which copy a given consumer links depends on `-L` order and was
  not traced per consumer.
- **`ncurses`' effective state in a clean image.** The framework port is
  registered nowhere and depended on only by `nano`/`mc` (`if: false`), so
  `$PREFIX_A/libncurses.a` exists only after a manual `scripts/build-port.sh
  ncurses`. Whether the full-clean build currently running produced it is not
  determinable from the trees; check after it finishes.
- **The full-clean build running while this was written is the live proof for
  every `if: true` port.** Its outcome postdates this audit; treat any
  "build-proven" claim below `ports.yaml`'s own comments as of that build, not of
  this document.
