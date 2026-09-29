# GPU migration P3 — the first GPU stack removed (branches, not merged)

Owner directive 2026-09-28 ([MIGRATION §3a](MIGRATION.md)), phase **P3**: delete the old GPU stack so the
DRM-shaped stack (rpi4-v3d-async + rpi4-kms + shmsrv, libdrm_phoenix, mesa_drm, sdl2_kmsdrm,
xorg_server_drm, Wayland) is the only one; owner rules 2026-09-30: one binary / port / version per
program, ship only the best current version. Done on branches `gpu/p3-remove` (pushed to `publish`,
**not merged, not built, no Pi cycle**). Evidence tags as in MIGRATION: **[built]** cross compile / link
/ static check, **[read]**, **[inferred]**.

## 1. Branches

| Repo | Branch | Commits (on) | What |
|---|---|---|---|
| devices | `gpu/p3-remove` | `643a65e` (on master `e364c97`) | `gpu/rpi4-v3d` deleted; component list without the knob; `rpi4-fb` not a component |
| ports | `gpu/p3-remove` | `43212b6`, `b4ca974`, `73a1502`, `a4f3e08` (on master `8d16491`) | 4 ports deleted, 3 old game ports → engine providers, libmd + SPIR-V moved, one Xorg binary; `mesa_drm` + `sdl2_kmsdrm` byte-identical to master |
| ports | `gpu/p3-games-link` | `f22e649` (on `gpu/p3-remove`) | the three `*_drm` game ports read the providers' new interface (§3) |
| project | `gpu/p3-remove` | `ad609b1` (on master `e73e2e0`) | `ports.yaml` / `user.plo.yaml` without the knob and the old entries |
| coord | `gpu/p3-remove` | `23a7fc090` + the doc commit (on main `5fd9d59ed`) | scripts, tools, TD register, LICENSING, this doc |

**Merge together**, devices + ports + project + coord (the knob and the old ports disappear in all
of them at once). `gpu/p3-remove` of ports is **not resolvable on its own**: the `*_drm` game
recipes on it are master's (the coordinator asked that they stay untouched: the desktop-apps
agent restructures them on `feat/desktop-apps-ports`), and master's still `depends="... sdl2 ..."` and
read the old game ports' old-stack link line. Merge ONE of: `gpu/p3-games-link` (proven below), or a
`feat/desktop-apps-ports` whose game ports consume the §3 interface or compile the engines themselves.

## 2. Deleted

| Area | What | Lines |
|---|---|---|
| devices | `gpu/rpi4-v3d/`: in-process winsys + Mesa glue (`v3d_phoenix_winsys.c` 3 936, libdrm/v3dv shims, stubs, compat header), Mesa fork build scripts (`build-{gl,v3d,v3dv}-phoenix.py`, source lists), the `rpi4-v3d` daemon (`/dev/v3d-srv`, `v3d_gpu.c`), `libv3d-client`, its uapi copy | −13 811 (36 files) |
| devices | `_targets/Makefile.aarch64a72-generic`: `RPI4B_GPU_LEGACY_ON`, `rpi4-v3d`, `rpi4-fb` components, stale comments | −33 / +7 |
| ports | `sdl2` (the `/dev/fb0` SDL video backend, `glue/sdl_phoenix_glctx.c`, `sdl_phoenix_glstubs.c`) | −2 308 |
| ports | `quakespasm` (`quakespasm_drm` has had identical copies of its patch + glue) | −1 677 |
| ports | `vkquake` (the fb0 Vulkan shim, `vk_trampolines.c`, sdl-shim, patch 0001/0002); `glue/vkquake_shaders.c` **moved** to `vkquake_drm/glue/` | −~13 400 (+ the 2.2 MB SPIR-V file moved) |
| ports | `xorg_server` (kdrive `Xphoenix`, fbdev DDX, builtin keymap copy); `files/libmd/sha1.{c,h}` **moved** to `xorg_server_drm/glue/libmd/` | −3 033 |
| ports | `yquake2`, `quake3`, `supertuxkart`: their old-stack link (Mesa fork archives from `tools/.gpu-libs`, the ports `libSDL2.a`, the SDL-GL glue, `external/mesa` headers), `iuse=rootfs`, TD-24 markers | −311 / +152 |
| ports | `xorg_server_drm`: builds libmd itself (no `xorg_server` dependency); the second 20 MB copy `/bin/Xorg-drm-noshim` gone (startx-drm runs `/bin/Xorg-drm`); the dead old-lane string guard | port.def.sh −26 / +24 |
| project | `ports.yaml`: knob header, `sdl2`, `xorg_server` and the 5 legacy game entries, 13 `if:` knob lines; `user.plo.yaml`: 2 `rpi4-fb` launches, 6 knob conditions | −210 / +20 |
| coord scripts | knob + GPU-archive phase (`rebuild-rpi4b-fast.sh`), `build-showcase-apps.sh` (646 → 101 lines), legacy branches of `build-rootfs-helpers.sh`, `check-rootfs-complete.sh`, `verify-sd-image-contents.sh`; `syntax-check-v3d.sh`, `build-sdl2-port.sh` deleted; 12 others reworded (see the commit) | −1 288 / +225 |
| coord tools | `tools/quakespasm-port` (3 214), `tools/vkquake-port` except `gen-vkquake-shaders.py` (20 387), `tools/v3d-driver-port` (9 077), `tools/sdl2-port` (546), `tools/fbprobe`, `tools/v3dmemprobe` (old-winsys diagnostic), `tools/.gpu-libs`, `tools/x11-port` kdrive DDX / glamor shim / `gl_x11_window.c` / xlaunch / their build + cycle scripts / 9 xorg-server patches (6 469) | −39 997 |
| coord | `patches/mesa/` (the Mesa fork patch + README); the Mesa clone in `bootstrap-linux-host.sh` | −1 735 |

## 3. TD-25: engine providers, not a fold (why, and the interface)

**Choice:** `yquake2`, `quake3` and `supertuxkart` stay as **object providers** — compile the engine
against `sdl2_kmsdrm` + `mesa_drm` headers, link nothing, install nothing — and the linking ports read an
explicit interface instead of scraping the old link line out of `build.log`:

* `yquake2`, `quake3`: `port-sources/<port>/engine-link.sh` defines `ENGINE_OBJS` (link order),
  `ENGINE_LINK_FLAGS` (the provider's `${CFLAGS} ${LDFLAGS}`; the consumer cannot re-derive them:
  `nl_setup` unsets CC/CFLAGS/LDFLAGS) and `ENGINE_LINK_TAIL` (`-lstdc++ -lm`, 4 MiB stack).
* `supertuxkart`: its CMake tree + `link.txt`, now configured against `sdl2_kmsdrm`'s `libSDL2.a`
  (a configure-inputs stamp forces the reconfigure of an existing tree, whose cache names the old SDL).

**Why not moving the compile into the `*_drm` ports** (the other option; implemented first, then
reverted): (a) the coordinator reserved the `*_drm` game recipes for the desktop-apps agent; (b) one
compile, several link variants is exactly what the windowed (`-wl`, M8) games need — they relink the same
objects today (`tools/gpu-lane/sdl2-wl/gamewl/relink-sdl-gl-game-wl.sh` parses the old `build.log` line,
so **it breaks at P3 either way** and should read `engine-link.sh`); (c) STK's CMake build (21 patches,
toolchain file, 12 dependencies) stays in one place.

**Proofs [built]** (none of them writes `.buildroot`):
* new include set (`-I<sdl2_kmsdrm>/include[/SDL2]` first, `-I<mesa_drm>/src-include` for
  `external/mesa/include`) recompiled from the shipped trees: **yquake2 137/137, quake3 167/167 objects
  code- and data-identical** (`objdump -d` + `.rodata/.data`) to the shipped ones;
* `gpu/p3-games-link` harness (the new `p_build` bodies + subr run against the image buildroot's inputs,
  manifests built from the shipped objects, outputs in `/tmp`): **`yquake2-drm`, `quake3e-drm`,
  `supertuxkart-drm` (stripped) and `quake2-drm`, `quake3-drm`, `stk-drm` byte-identical to the shipped
  P1 builds**; every positive proof passes (nm -u 0, swap wrap in the engine path, submit-first, no
  first-stack symbol/string, no duplicate symbol);
* lost: the control relinks and inverse controls (they needed the old stack). STK's SDL/GL TUs were not
  recompiled here (its `compile_commands.json` covers shaderc only) — first-build risk 1.

## 4. Kept on purpose

| What | Why |
|---|---|
| `devices/video/rpi4-fb` (source) | TD-27: `hevc-play` (hand-built) still writes `/dev/fb0`; porting it to a KMS dumb buffer is untestable without a Pi cycle. Not a component, not started. `check-gpu-stack-image.sh` keeps its `allow_fb0` note |
| `tools/gpu-lane/*` (all) | superseded by the ports but read by `sdl2-wl`, `video-player`, `check-gpu-lane-ports-sync.sh`; deletion candidates in §7 |
| `tools/{yquake2,quake3,supertuxkart}-port` launcher sources | canonical of the ports' vendored launchers (sync check) and read by `sdl2-wl`; `quake3-port/demos/cap.dm_68` by `quake3-host-capture.sh` |
| `tools/vkquake-port/gen-vkquake-shaders.py` | regenerates `vkquake_drm/glue/vkquake_shaders.c` (default output retargeted) |
| `tools/x11-port` non-GPU parts | `fontconfig/fonts.conf` (`stage-desktop-fonts.sh`), `xkb/` (xorg_server_drm patch 0003 provenance), app scripts + `build-x11-phoenix.sh` (unused by the build; §7) |
| forbidden-string guards naming `Xphoenix`, `phxgl`, `/dev/fb0` in `weston`, `wayland`, `labwc_desktop`, the game subr | they catch a stale shared-prefix archive or header coming back (the shared prefix still holds the deleted `sdl2`'s headers/archive on existing buildroots) |
| `check-gpu-stack-image.sh` check 3 (names the deleted programs) | nothing deletes stale files from the persistent staging tree / export |
| external game forks in `bootstrap` | patch sources of `game-port-patch.sh`; `external/vkquake` = the SPIR-V generator's Shaders/ |

## 5. Static checks

* **grep** (`gpu-libs|libGL-phoenix|libv3d-phoenix|sdl_phoenix_glctx|rpi4-v3d([^-a-z]|$)|Xphoenix|RPI4B_GPU_LEGACY`;
  `rpi4-v3d\b` would also match `rpi4-v3d-async`): project 0; devices Makefiles 0; coord scripts 3 hits
  (`check-gpu-stack-image.sh` absence list; `make-pristine-nfs-export.sh` junk-name pattern `^rpi4-v3d$`);
  ports `gpu/p3-remove`: master's `supertuxkart_drm`, `yquake2_drm` comment, `relink-sdl-gl-game.subr`
  (untouched by instruction → 0 on `gpu/p3-games-link`), `quakespasm_drm` comment (desktop-apps agent's
  file), guards in `labwc_desktop`/`weston`/`wayland`, history comments in `zlib`/`xorg_fonts`.
* `bash -n` every changed script; `py_compile` the two Python ones; `port_manager validate`: 92 ports
  (was 96; −4). `build-port.sh --dry --yaml` of the new `ports.yaml` (scratch buildroot): `gpu/p3-remove`
  alone **unsatisfiable** (see §1); with `gpu/p3-games-link` **74 ports**, none of the deleted ones.
* `user.plo.yaml`: `diff-boot-variants.py --verbose --order rpi4-vcmbox,posixsrv,rpi4-v3d-async,rpi4-kms,shmsrv,psh`,
  3 variants × both log modes: rendered scripts **identical to master's default**; `RPI4B_GPU_LEGACY=1`
  no longer changes anything. Devices component list (`make` of the target file): identical for any knob value.
* `sha1.c` compiles clean (`-Wall`, project flags). `check-gpu-lane-ports-sync.sh`: 161 files / 43 mappings
  identical. `check-rootfs-complete.sh` and `check-gpu-stack-image.sh` on today's P1 rootfs + loader: COMPLETE / PASS.
* No C file of devices changed (deletion + Makefile only), so `syntax-check.sh` had nothing to check.

## 6. Names that are not plain (one program, one name — for P4 / the owner)

| Now | Plain | References that change |
|---|---|---|
| games `quakespasm-drm`/`qs-drm`, `yquake2-drm`/`quake2-drm`, `quake3e-drm`/`quake3-drm`, `vkquake-drm`/`vkq-drm`, `supertuxkart-drm`/`stk-drm` (+ plain-name copies, TD-26) | `quakespasm`, `quake2`(+engine `yquake2`), `quake3`(+`quake3e`), `vkquake`, `stk`(+`supertuxkart`) | desktop-apps agent's restructure; `game-res.c`, check scripts, gate `run-showcase-gate-drm.sh`, `<app>: new GPU lane` banners + `flipstat` tags (gate greps), `stage-game-data.sh` untouched |
| `Xorg-drm`, `startx-drm`, `/etc/X11/xorg-drm.conf`, `eglx11-demo-x` | `Xorg`, `startx` (wrapper exists), `xorg.conf`, `eglx11-demo` | `xorg_server_drm` port + `glue/pi/startx-drm` (+ tools copy), `startx`/`startx_gpu` wrappers, `check-*` scripts, `rebuild-rpi4b-fast.sh` showcase marker, `grade-x-desktop-video.py`, xfce/labwc session scripts if they start X |
| `rpi4-v3d-async` (`/dev/v3d-async`) | `rpi4-v3d` (free now) | devices dir + Makefile, `user.plo.yaml` (2 lines), `libdrm_phoenix` `V3DA_DEV_NAME`, `startx-drm --servers`, `rpi4-sysinfo`, check scripts, gate ready-lines |
| `vkcube-drm` | `vkcube` | `vkcube_drm` port, check lists |
| `thunar-wl`, `gdbus-wl`, `/usr/lib/xfce-demo`, `/etc/xdg/*-demo` | `thunar`, `gdbus`, … | `xfce_wayland` (TD-26), sessions |
| ports `*_drm`, `sdl2_kmsdrm`, `mesa_drm`, `libdrm_phoenix`, `xorg_server_drm` | `sdl2`, `mesa`, `libdrm`, `xorg_server` (freed by P3), game ports | every `depends=`, `ports.yaml`, `PORT_DEP_*`, `versioned-ports/` paths, sync-check maps |

## 7. Leftover checklist (temporary / testing / superseded) — for the coordinator

- [x] old GPU stack, knob, kdrive `Xphoenix`, xlaunch, glamor daemon, gl-x11-window, fbprobe, v3dmemprobe (this pass)
- [x] `Xorg-drm-noshim` duplicate (this pass)
- [ ] **`hevc-play`** → KMS dumb buffer, then delete `video/rpi4-fb` (TD-27); until then delete the stale `bin/hevc-play` from rootfs/export (it cannot run: no `/dev/fb0`)
- [ ] stale files in the persistent `_fs` tree / export from earlier builds: `bin/Xorg-drm-noshim`, `bin/v3dmemprobe`, `bin/fbprobe`, `.mesa-shader-cache/`; a pristine export clears the export
- [ ] `tools/gpu-lane` scaffolding superseded by ports (delete once `sdl2-wl`/`video-player` are ports and the sync checks are retargeted): `sdl2-drm/` (incl. `build-*-drm.sh`, gamedrm/vkqdrm/stkdrm sources = the ports' sync masters), `mesa-drm/`, `vulkan-drm/`, `libdrm-phoenix/`, `x11-drm/`, `xorg-drm/`, `weston-drm/` (incl. `shmsrv`), `labwc-drm/`, `dbus/`, `v3d-async/` + `kms/` (devices is canonical since P1), `stkprof/` (patches the deleted winsys), `e7-drm-build/`, `pace/`, `pollwake/`, `portdeath/`, `m9-res` (keep `game-res.c` or move it), probes `kmsprobe/`, `exportprobe/`, `ipcprobe/`, `e2c/`
- [ ] `tools/x11-port` ad-hoc X11 lib stack + app scripts (superseded by `xorg_libs`, `xorg_apps`, `xbill`, `xterm`, `windowmaker` ports; unused by the build) — keep `fontconfig/`, `xkb/`
- [ ] `tools/ffmpeg-port` E4 demos (`e4_fbshow`, `e4_play` write `/dev/fb0`; `e4_x11_play` built against the deleted X pieces) — superseded by the video player ports
- [ ] `tools/v3d-shader-tool` (built on the old Mesa fork) — check, then delete
- [ ] probes in the image: keep `drmprobe`, `kmscube`, `vkcube-drm` (the GPU stack's smoke tests, seconds each, used by the gates) and `eglx11-demo-x` (the X desktop's GL window); owner decision for `pwmwrite`, `pwmdma`, `armtrials` (audio DMA-stall probes, TD-23), `thermal-soak`, `mtstress`, `gtk3-demo`/widget-factory (rootfs growth), `serverdemo`/`voxeldemo`
- [ ] transitional code whose purpose is gone: `vkqdrm` libphoenix bridges (`ipv6_mreq`, `<execinfo.h>`) and the ports' `ipv6_mreq` copies — libphoenix has both now; vkQuake patch 0005 (until G5 `SUBMIT_CPU`); `builder_string="… new GPU lane …"` and the `new GPU lane` banners (P4)
- [ ] `.claude/settings.json`: dead allowlist entry `./scripts/syntax-check-v3d.sh` (owner: permission file)
- [ ] best measured defaults: unchanged by P3 and correct — servers `-r 1 -m serial -i` / `-G -p 96 -C` (`mig-x`, `m7l-session2`), X = Xorg-drm + modesetting/glamor (`xorg-drm.conf`), vkQuake patches 0001–0010 (`mig-vkq-h` 44.38 fps). Still open: `gpu_mem=128` + plo's triple-height firmware fb (sized for the deleted pan-flip present; MIGRATION §4 item 11 — measure, then shrink)
- [ ] docs (P4): KNOWN-ISSUES rows of the old stack, MIGRATION/README/BUILD wording

## 8. Risks for the first build of these branches

1. **STK's first CMake configure against `sdl2_kmsdrm` + `mesa_drm` headers** (never run). The stamp
   forces a full reconfigure + recompile of STK (tens of minutes [inferred]). Failure = a compile error in the log; the
   object-identity proof covers yquake2/quake3 only.
2. **`xorg_server_drm` reconfigure**: the libmd sources entered its deps stamp → one full meson
   reconfigure + rebuild; `find_library('md')` must now resolve from `-L<deps-prefix>/lib` (was the shared
   prefix, where the deleted `xorg_server` port put it).
3. **Stale shared-prefix leftovers** of the deleted ports on the image buildroot: `include/SDL2/`,
   `lib/libSDL2.a`, `lib/pkgconfig/sdl2.pc`, `lib/libmd.a`, `include/sha1.h`, and `port-sources/{sdl2,
   quakespasm,vkquake,xorg_server}-*` (~290 MB). The recipes put the new SDL headers first, so they are
   harmless, but remove them (and the port-sources dirs, for disk) before the build:
   `rm -rf .buildroot/_build/aarch64a72-generic-rpi4b/{include/SDL2,lib/libSDL2.a,lib/libSDL2main.a,lib/pkgconfig/sdl2.pc,lib/libmd.a,include/sha1.h} .buildroot/_build/aarch64a72-generic-rpi4b/port-sources/{sdl2,quakespasm,vkquake,xorg_server}-*`
   (the Docker `--no-cache` gate cannot see this class; the incremental build can).
4. **Recipe digests** (port_manager hashes every file of a port's directory; a change cleans the port
   and all its dependents): rebuilt on `gpu/p3-remove` = the 3 game providers, `vkquake_drm`,
   `xorg_server_drm`; `mesa_drm` and `sdl2_kmsdrm` were kept byte-identical so **Mesa is not rebuilt**.
   `gpu/p3-games-link` changes `sdl2_kmsdrm/gamedrm/` → SDL (2 builds) + the five SDL games rebuild.
5. **Merge coupling** (§1): P3 needs `gpu/p3-games-link` or an equivalent `feat/desktop-apps-ports`; the
   `sdl2-wl` windowed-game relinks (`relink-sdl-gl-game-wl.sh`, `build-stk-wl.sh`) must read
   `engine-link.sh` / the new `link.txt` (no old SDL token, no glue objects, no control relink), and
   `sdl2-wl/build.sh` (quakespasm-wl) reads the deleted `ports/quakespasm/{<commit>.tar.gz,patches,glue}`
   and `ports/sdl2`: use `quakespasm_drm/{patches,glue}` (identical copies), its own tarball download,
   and `sdl2_kmsdrm`'s SDL tarball.
6. Build with `--scope core --with-ports --with-showcase` (the devices component list changed), then
   `check-gpu-stack-image.sh` on the rootfs and on a pristine export, then the showcase gate.
7. `bootstrap` no longer clones Mesa: `tools/gpu-lane/mesa-drm/build.sh` needs an `external/mesa` by hand
   on a fresh host (it is scaffolding; §7).
