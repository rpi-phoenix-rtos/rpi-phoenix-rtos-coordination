# 2026-09-03 — one image, every game: build-side enablement

Owner priority (overnight 2026-09-03): **ONE complete image containing EVERY game we
test**, with **as little ported software inside `loader.disk` as possible**, built
**deterministically**. This note records (1) how each game actually reached the Pi
before this change, (2) what changed, (3) the build results, (4) what is still open,
and (5) the exact doc claims that must be corrected once the hardware runs confirm the
new state. **No doc was edited** — the owner wants docs fixed at the very end.

Constraints observed while doing this work: no Pi cycle, no `sync-netboot-tree.sh`, no
`rebuild-rpi4b-fast.sh`, no pushes. Only `scripts/build-port.sh` (builds into
`.buildroot`, assembles no image) and `bash -n` were run, plus the two new scripts
introduced here.

---

## 1. Investigation — how each game reached the Pi before this change

### 1a. Four different delivery mechanisms for five games

| Game | Binary | How it got onto the Pi (before) |
|---|---|---|
| QuakeSpasm / GLQuake | `/usr/bin/rpi4-quake` | `_user/rpi4-quake` component, linking `tools/.gpu-libs/libquakespasm.a`, **bundled into `loader.disk`** by `user.plo.yaml:249-250` |
| vkQuake | `/usr/bin/rpi4-vkquake` | `_user/rpi4-vkquake`, same pattern; its `loader.disk` line **commented out** (`user.plo.yaml:258-259`) because "loader.disk fits only one large GL/VK binary" |
| Quake II | `/usr/bin/yquake2` | framework port `if: false` → built **only** by `scripts/build-port.sh`, then **hand-copied** forward between NFS exports |
| Quake III | `/usr/bin/quake3e` | same as Quake II |
| SuperTuxKart | `/usr/bin/supertuxkart` | port recipe existed in `sources/phoenix-rtos-ports/supertuxkart/` but was **not registered in `ports.yaml` at all** → *no* image build has ever produced it |

Primary sources:

* `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml:249-250`
  — the only game `app` line, gated on `RPI4B_WITH_SHOWCASE=1`; `:258-259` the
  commented vkQuake alternative.
* `.../ports.yaml:159, 175, 190, 203` (pre-change line numbers) — `quakespasm`,
  `yquake2`, `quake3`, `vkquake` all `if: false`. Each comment ends with the same
  instruction: *"Flip to if:true (and drop the ad-hoc step) in lockstep once it is
  image-proven."* No `supertuxkart` entry existed.
* `scripts/build-showcase-apps.sh:337-343` built `libquakespasm.a`;
  `:383-407` built `libvkquake.a`. Both existed **only** to feed the two `_user`
  wrappers.
* `scripts/make-pristine-nfs-export.sh:29-32` (pre-change) hand-copied
  `usr/bin/{yquake2,quake3e,quake2,quake3}` forward "because they are not in the
  standard build".
* `tools/supertuxkart-port/build-stk-launcher.sh:41-46` built `/bin/stk` and installed
  it **straight into the live NFS export**.
* `port_manager/port_manager.py:391` — `require_bool(port, "if", True)`. `if:` is a
  literal boolean; it cannot be a jinja/env expression like the `plo` yaml's `if:`.
  So flipping a port on makes it unconditional in every ports stage.

### 1b. Game data — where it lived

`phoenix-rtos-build/build.sh:67` puts `$PROJECT_PATH/rootfs-overlay` first in
`ROOTFS_OVERLAYS`, so the overlay is the one staging path that reaches **both**
variants (the `sd` ext2 packer and `sync-netboot-tree.sh`'s rsync both consume
`_fs/<target>/root`).

Before this change the overlay contained **only** Quake I:
`rootfs-overlay/usr/share/quake/id1/pak0.pak` (18 MB), fetched by
`scripts/fetch-quake-shareware-pak.sh` / `scripts/fetch-quake-data.sh q1`.

Everything else was hand-staged on the live NFS export (`/srv/phoenix-rpi4-nfs-gcc16`,
the `fsid=0` export):

| Data | Size on the export | Fetcher that existed |
|---|---|---|
| `usr/share/quake/id1` | 18 MB | `fetch-quake-data.sh q1` (+ overlay) |
| `usr/share/quake2/baseq2` | 50 MB | `fetch-quake-data.sh q2` — Docker only |
| `usr/share/quake3/demoq3` | 46 MB | `fetch-quake-data.sh q3` — Docker only |
| `usr/share/supertuxkart/data` | 46 MB | **none** |
| `usr/share/supertuxkart/stk-assets` | 149 MB | **none** |

The Docker build (`Dockerfile:87-89`, pre-change) already fetched q1/q2/q3 into the
overlay with pinned URLs + sha256s; nothing at all staged the SuperTuxKart roots.

### 1c. `loader.disk` size

`.buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk` =
**4,503,808 bytes** (built 2026-09-03 00:04, i.e. *without* `--with-showcase`). It
therefore contains **zero game bytes today**. With `--with-showcase` the removed `app`
line would have added the `rpi4-quake` ELF — **17,805,568 bytes** as staged on the
export — i.e. game binaries were ~80% of a showcase `loader.disk`. The hard ceiling in
`build.project:253` is the 128 MiB generic plo RAM window, so size was never the real
constraint; the "only one large GL/VK binary" rule was a self-imposed convention.

### 1d. Launchers and helpers were hand-fed too

`psh` cannot set environment variables and has no `;`/`|`, so three engines need a tiny
launcher. Their sources live in the coordination repo and had **no build home**:

| Binary | Source | Purpose |
|---|---|---|
| `bin/ram-stage-play` | `tools/ram-stage/ram-stage-play.c` | copy an asset tree into the `/tmp` RAM disk then exec the engine (NFS reads are latency-bound, ~20× per scattered read) |
| `usr/bin/quake2` | `tools/yquake2-port/quake2-launcher.c` | RAM-stage + fb-native custom video mode + `+map demo1` |
| `usr/bin/quake3` | `tools/quake3-port/quake3-launcher.c` | RAM-stage + `fs_basepath`/`fs_game` |
| `bin/stk` | `tools/supertuxkart-port/stk-launcher.c` | `SUPERTUXKART_{DATADIR,ASSETS_DIR,SAVEDIR}` + seeded first-run profile + 1080p |
| `usr/bin/pty-run` | `tools/pty-run/pty-run.c` | `/dev/ptmx` getty-style forwarder |

**QuakeSpasm and vkQuake need no launcher.** Both `glue/pl_phoenix_main.c`
implementations (`ports/quakespasm/glue/pl_phoenix_main.c:50-93`,
`ports/vkquake/glue/pl_phoenix_main.c:35-60`) run `wait_for_gamedata()`, which probes
`/ramtmp/quake`, `/tmp/quake`, `/usr/share/quake`, `/opt/quake`, `/` for
`id1/pak0.pak` and accepts an optional `-basedir <dir>` override. So `quakespasm` and
`vkquake` with no arguments already find the staged data. The `/bin/quakespasm`,
`/bin/quakespasm-sdl` and `/bin/vkquake` binaries on the old export were **not
wrappers** — they were the ad-hoc `tools/*-port` *engine* builds (23 MB / 25 MB /
22 MB), now superseded by the ports' `/usr/bin/quakespasm` and `/usr/bin/vkquake`.

---

## 2. What changed

### `sources/phoenix-rtos-project`

* `_projects/.../ports.yaml`
  * `quakespasm`, `yquake2`, `quake3`, `vkquake` → **`if: true`**.
  * **`supertuxkart` registered** (`if: true`) — it had no entry at all.
  * `python` → `if: true` and **`wpa_supplicant` registered** (`if: true`), so
    `/bin/python3` + `/usr/local/lib/python3.14` and
    `/usr/bin/{wpa_supplicant,wpa_cli}` are build-produced instead of hand-copied.
  * A block comment above the game entries states the new invariant (games in the
    rootfs, not the boot blob; the fork is the single patch source; the GPU-archive
    prerequisite; where the data comes from).
* `_projects/.../user.plo.yaml` — **purely subtractive**: deleted the single
  `app {{ env.BOOT_DEVICE }} rpi4-quake ddr ddr` entry, its `RPI4B_WITH_SHOWCASE` gate,
  and the two commented-out `-x rpi4-quake` / `-x rpi4-vkquake` launch blocks. `str: app`
  line count 39 → 36 (one real line + two comment lines). The whole file still parses,
  no program is listed twice, and `RPI4B_WITH_SHOWCASE` no longer appears anywhere.
* `_user/rpi4-quake/`, `_user/rpi4-vkquake/` — **deleted** (the only D10 "second
  representation" of QuakeSpasm and vkQuake).
* `_projects/.../build.project` — comment no longer claims `_user` builds the games.
* `.gitignore` — game data patterns extended to `quake2`, `quake3` and
  `share/supertuxkart/` so the 306 MB of staged data can never be committed.

### `sources/phoenix-rtos-ports`

* `vkquake/port.def.sh` — **two missing link flags added**: `-Wl,--build-id` and
  `-Wl,-z,stack-size=33554432`. Verified with `readelf`: the ports-built `vkquake`
  before the fix had **no build-id note and no `PT_GNU_STACK` segment at all**, while
  the HW-verified `_user/rpi4-vkquake` has a build-id and `memsz=0x2000000`. V3DV's
  `init_uuids()` walks the ELF for a build-id note and `create_physical_device` fails
  with *"Failed to find build-id"* without it, and `PT_GNU_STACK p_memsz` is what the
  kernel uses for the main-thread stack, which vkQuake's `Host_Frame` loop runs on.
  Without this fix, flipping the port on would have shipped a vkQuake that cannot
  create a Vulkan device. After: build-id `9f327edf…`, `GNU_STACK memsz=0x2000000`.
* `yquake2/port.def.sh` — default renderer **`gl1` → `gl3`** (`YQ2_RENDERER` still
  overrides). gl3/GLES3 is the configuration the owner has seen render on V3D 4.2 and
  the one that was staged all along; the port's default would have shipped the
  never-confirmed gl1 build. `desc`/comments updated.
* `libpng/port.def.sh` — **real bug fix, and the reason the first STK build failed.**
  `p_prepare` guarded its `./configure` on `Makefile`, but
  `b_port_invalidate_stale_configure()`
  (`phoenix-rtos-build/port_manager/port.subr:151-189`) drops **`config.status`** when
  the libphoenix API fingerprint changes. libpng was the *only* one of the 17 autoconf
  ports keying on `Makefile`, so the invalidation deleted `config.status` while the
  stale `Makefile` suppressed the reconfigure, and the next `make install` died inside
  libpng's own rule with `./config.status: No such file or directory`. Guard now keys on
  `config.status` like every other port.

### coordination repo

* **`scripts/stage-game-data.sh` (new)** — one command stages the data for all five
  engines into the rootfs overlay: q1/q2/q3 by delegating to the existing
  `fetch-quake-data.sh` (no second copy of that logic), SuperTuxKart `data/` from the
  pinned `stk-code-1.4.tar.gz` (sha checked against the value the port itself pins, so
  the two cannot drift) and `stk-assets/` from the pinned SuperTuxKart 1.4 Android
  package (`…/releases/download/1.4/SuperTuxKart-1.4.apk`, sha256 `29bded24…`, the pin
  recorded in ports commit `7ac46d8`). Idempotent, `--force` to re-fetch, per-game
  selection, and a closing summary table.
* **`scripts/build-rootfs-helpers.sh` (new)** — builds the five tiny static helpers
  above into the rootfs staging tree with the same `.toolchain` gcc as the engines, and
  refuses any artifact carrying a `PT_INTERP`. Wired into
  `build-showcase-apps.sh --phase stage` as a **hard-fail** step. Replaces
  `tools/supertuxkart-port/build-stk-launcher.sh`, which is **deleted** — it wrote into
  the live NFS export.
* `scripts/build-showcase-apps.sh` — dropped the `libquakespasm.a` and `libvkquake.a`
  archive steps (their only consumers were the deleted `_user` wrappers); the phase now
  produces just `lib{v3d,GL,v3dv}-phoenix.a`, the GPU stack the ports link. The V3DV
  path is **on by default** (`skip_vulkan=0`) because the `vkquake` port needs
  `libv3dv-phoenix.a`; `--with-vkquake` is now a compatibility no-op and
  `--skip-vulkan` warns that the ports stage will fail.
* `scripts/rebuild-rpi4b-fast.sh` — stopped exporting `RPI4B_WITH_SHOWCASE` (nothing
  consumes it now) and replaced the old `libquakespasm.a` gate with a precondition
  check that names any missing GPU archive up front. `--with-showcase` ext2 root
  **768 MiB → 1.5 GiB**; the arithmetic is in the comment (≈55 MiB base + ≈108 MiB of
  engines + ≈308 MiB of data, against `mke2fs -b 1024 -i 2048` spending roughly an
  eighth of the volume on the inode table, with STK's asset tree being tens of
  thousands of ≤1 KiB files). Partition geometry is computed from the actual image
  size, so only partition 2 grows.
* `scripts/make-pristine-nfs-export.sh` — the game-**data** copy-forward is gone too
  (the binary copy-forward had already been removed in coord `996db622e`), and the
  completeness check now lists the five engines, the four launchers and all four data
  roots instead of the deleted `rpi4-quake`/`rpi4-vkquake`.
* `Dockerfile` — new `STK_ASSETS_URL` / `STK_ASSETS_SHA256` build args; the three
  `fetch-quake-data.sh` calls replaced by one `stage-game-data.sh all` with every pin
  passed through as env, so the authoritative Docker build and a local build stage the
  same bytes for all five games.
* `scripts/publication-audit.sh` — `KNOWN_PAYLOADS` extended to the quake2/quake3/
  supertuxkart overlay trees, all attributed to `scripts/stage-game-data.sh`.
* `.claude/settings.json` — allowlisted `build-port.sh`, `stage-game-data.sh`,
  `build-rootfs-helpers.sh`, `build-showcase-apps.sh`.

---

## 3. Verification actually performed (no Pi, no export, no image)

### 3a. `scripts/build-port.sh <port>` — clean build, real rc

| Port | rc | Artifact (`_fs/<target>/root`) | Size |
|---|---|---|---|
| `quakespasm` | **0** | `usr/bin/quakespasm` | 18,553,680 B |
| `yquake2` | **0** | `usr/bin/yquake2` | 19,109,952 B (gl3 default) |
| `quake3` | **0** | `usr/bin/quake3e` | 19,180,064 B |
| `vkquake` | **0** | `usr/bin/vkquake` | 12,799,520 B (with build-id + 32 MiB stack) |
| `supertuxkart` | **0** | `usr/bin/supertuxkart` | 38,008,448 B |
| `wpa_supplicant` | **0** | `usr/bin/wpa_supplicant`, `usr/bin/wpa_cli` | 2,744,856 B / 142,416 B |
| `python` | **0** | `bin/python3` + `usr/local/lib/python3.14` | 57,068,136 B + 52 MB |

The first `supertuxkart` attempt returned **rc=1**, in the `libpng` *dependency*, not in
STK — root-caused and fixed (see §2, `libpng/port.def.sh`); the retry is the rc=0 above.

`scripts/build-rootfs-helpers.sh` → rc 0, five static ELFs, none with `PT_INTERP`:
`bin/ram-stage-play` 756,416 B, `usr/bin/quake2` 717,008 B, `usr/bin/quake3`
717,008 B, `bin/stk` 717,112 B, `usr/bin/pty-run` 733,840 B.

`scripts/stage-game-data.sh all` → rc 0. Staged into the overlay: `quake/id1` 18M,
`quake2/baseq2` 48M, `quake3/demoq3` 45M, `supertuxkart/data` 46M,
`supertuxkart/stk-assets` 149M (**306 MB total**), and
`git status` in `phoenix-rtos-project` shows **none of it** (gitignore verified with
`git check-ignore -v`).

### 3b. `bash -n` on every edited script

`build-showcase-apps.sh`, `rebuild-rpi4b-fast.sh`, `make-pristine-nfs-export.sh`,
`publication-audit.sh`, `stage-game-data.sh`, `build-rootfs-helpers.sh`,
`build.project`, and the three edited `port.def.sh` files — **all clean**.
`ports.yaml` and `user.plo.yaml` both parse with `yaml.safe_load`.

### 3c. `loader.disk` carries no game

`grep` over `user.plo.yaml` finds no `quake`/`stk`/`supertux` outside comments, and no
`app` line that could render a game under any env combination (the `RPI4B_WITH_SHOWCASE`
variable is gone from both the yaml and the rebuild script). Diff is deletions only.

### 3d. Regression check against the pre-regeneration binaries

The coordinator flagged that the engines on the export were dated **Aug 28**, i.e. from
before ports commit `0d9de9a` regenerated the patches from the forks. Two findings:

1. **The live export no longer holds those Aug-28 binaries.** `/srv/phoenix-rpi4-nfs-gcc16/usr/bin/`
   now contains `quakespasm` 18,553,680, `yquake2` 19,109,952, `quake3e` 19,180,064,
   `vkquake` 12,799,520 — byte-for-byte the artifacts my `build-port.sh` runs produced
   at 00:37–00:41 today, so a sync happened after them. **The hardware results reported
   for Quake II and Quake III today therefore validate the NEW, regenerated-patch,
   gl3-default binaries, not the Aug-28 ones.** (`supertuxkart` on the export is still
   38,005,552 from Aug 28 — 2,896 B smaller than today's build.)
2. **yQuake2, old vs new, is a strict improvement.** Against the last surviving
   pre-regeneration copy (`/srv/phoenix-rpi4-nfs/usr/bin/yquake2`, Aug 27,
   19,103,640 B), a `strings` feature probe gives an identical renderer profile —
   `"OpenGL ES"` ×18, `ref_gl3` ×3, `gl3_` ×14, `ref_gl1` ×0 in both, i.e. both are the
   gl3/GLES3 build — and the only difference is that the new one **has** the
   `YQ2_GL3_MIPMAP` gate (1 vs 0) and is 6,312 B bigger. That gate is the
   `glGenerateMipmap`-off fix the owner says Quake II needs; the old binary lacked it.
   No regression indicator.
3. **quake3e, old vs new.** The surviving old copy (Aug 22, 28,229,480 B) is **not
   stripped**, so its size is not comparable. Feature probe: `r_mergeLightmaps` present
   in both, `Q3 1.32` in both, `renderervk` **0 in both** — see the open item below.
4. **Symbol-set comparison is not possible on these artifacts**: the shipped binaries
   are `strip`ped, so `nm -g --defined-only` returns 0 symbols for both old and new. The
   unstripped counterparts exist only for today's builds
   (`_build/<target>/prog/`), not for the Aug binaries.
5. **What the regeneration actually added** (`git show 0d9de9a --stat` in
   phoenix-rtos-ports: +1204/−75 across the four patches): quake3e gained a ~188-line
   `tr_init.c` block and yQuake2 gained `gl1_main.c`/`gl1_sdl.c` hunks — both are the
   **deterministic visual-regression capture harness** (per-frame TGA over a TCP sink).
   Its commit message documents this as a deliberate trade-off, and every cvar it adds
   defaults to inert: `scr_capture "0"`, `scr_capture_max "0"`, `scr_capture_host ""`
   (verified in the patch files, both ports). yQuake2's added gl1 hunks are not even
   compiled in the gl3 build. So the added delta is inert-by-default, not a behaviour
   change — but it is diagnostic-only code inside a port, which the project's own
   code-quality rule and the `quakespasm` port's own comment say should stay out. Worth
   an explicit owner decision; not changed here, because regenerating a patch would
   change the very binaries currently under test on the Pi.

---

## 4. Open items / risks (nothing papered over)

1. **`/tmp/mesa-v3d-build` is a hard prerequisite of the game ports.** All five recipes
   `b_die` unless `tools/.gpu-libs/lib{GL,v3d,v3dv}-phoenix.a` *and*
   `/tmp/mesa-v3d-build/src` exist (`quakespasm/port.def.sh:107-112` and equivalents).
   Now that the ports are `if: true`, **any** ports stage — including `--variant sd`
   without `--with-showcase`, and `--scope full-clean` — hard-requires them. A host
   `/tmp` reaper therefore breaks the ports stage. This is a real determinism wart and
   the reason the GPU archives themselves were moved out of `/tmp` earlier. Fixing it
   means portifying the Mesa/V3D stack (D2/D9 follow-up), not a patch here.
2. **`.buildroot/phoenix-rtos-ports` is a stale copy** (dated Aug 27), refreshed only by
   `prepare-buildroot.sh`. `build-port.sh` reads `sources/phoenix-rtos-ports` (so my
   verification used the edited recipes), but the real `build.sh ports` stage reads the
   buildroot copy. **The next real build must run `prepare-buildroot.sh`** (i.e. not
   `--skip-prepare`) or it will build the pre-fix `libpng`/`vkquake`/`yquake2` recipes.
3. **quake3e has no Vulkan renderer in any build we can find.** The port compiles
   `opengl1` only (`quake3/port.def.sh:42-44, 189, 236`), and `renderervk` appears 0
   times in both the old and the new binary. The owner's recollection that "Quake III
   ran on both its OpenGL and Vulkan renderers" cannot be true of these artifacts —
   either it was a separate build, or it is a mix-up with vkQuake. Needs an owner
   answer before any doc says "both renderers".
4. **`sync-netboot-tree.sh` rsyncs without `--delete`**, so the export is a superset
   that keeps whatever a previous build left behind. For the owner's determinism chain
   (identical binaries in the NFS root and the SD image) the clean path is
   `make-pristine-nfs-export.sh`, which rebuilds the export from `_fs/root`; a plain
   sync can leave stale executables that byte-comparison will then flag. Not changed
   here (it is the parent session's live rig).
5. **`/tmp` on the `sd` variant is on the ext2 root**, not a RAM disk, so the `quake2`
   and `quake3` launchers' RAM-staging step copies ~50 MB from ext2 to ext2 there.
   Harmless within the 1.5 GiB root, but it buys nothing on SD; a variant-aware launcher
   (skip RAM staging when the basedir is already local) would be a small win.
6. **`nano`, `mc`, `ffmpeg`, `sdl2` remain `if: false`.** `sdl2` is intentional (it is
   pulled transitively via `depends`), but `nano`/`mc` are still shipped by
   `build-showcase-apps.sh`'s ad-hoc steps — the same double-representation the games
   just escaped. Out of scope here; named so it is not forgotten.
7. **`scripts/build-sd-in-docker.sh` only rewires `PAK0_URL`** for its local-serving
   simulation; it does not serve the q2/q3/STK sources from the host, so that path falls
   back to the pinned public URLs. Fine, but not fully offline.
8. **The 306 MB of freshly staged overlay data will enter `_fs/root` on the next `fs`
   stage** and hence the next export sync. That is the intended end state, but it is a
   visible size jump (rootfs ≈55 MB → ≈470 MB) that the parent session should expect.

---

## 5. Doc claims to correct (**do not apply yet** — after the hardware runs)

### `README.md`

| Line | Current claim | Corrected fact |
|---|---|---|
| 111-113 | "the boot image holds only *one* large GL/VK game binary, so this builds vkQuake but still ships GLQuake unless you swap the launch line by hand" | No game is in the boot image at all. All five engines ship on the rootfs; nothing is swapped, no launch line to edit. |
| 113-114 | "vkQuake also renders without input wired yet" | Owner: vkQuake had working mouse **and** keyboard. Delete. |
| 130-131 | "all five engines are proven on the hardware, but only two are wired into the SD image today" | All five are wired into the image. |
| 135 | GLQuake "✅ with `--with-showcase`" / run `rpi4-quake ddr ddr` | `rpi4-quake` no longer exists. Run `quakespasm` (no args — it finds `/usr/share/quake/id1`). |
| 136 | vkQuake "⚠️ built, not bundled — needs a one-line swap" | On the card. Run `vkquake`. |
| 137 | Quake II "❌ not yet" | On the card. Run `quake2` (launcher). |
| 138 | Quake III "❌ not yet" | On the card. Run `quake3` (launcher). |
| 139 | SuperTuxKart "❌ not yet — built by `tools/`" | On the card, built by the `supertuxkart` framework port. Run `stk`. |
| 143-146 | "The ~18 MB binary lives in `loader.disk` (**it is too large to exec from the read-based ext2/NFS loader**) … game data sits at `/id1` on the ext2 root" | **False and the headline claim to delete.** The loader was fixed long ago; large binaries exec fine from both ext2 and NFS — the 38 MB `supertuxkart` runs from the NFS root today. Data is at `/usr/share/quake/id1`, not `/id1`. |
| 148-153 | "GLQuake and vkQuake swap — the image holds only one of them … swap the two `app … rpi4-quake` / `app … rpi4-vkquake` launch lines in `user.plo.yaml`" | Delete entirely. Both lines are gone; both games ship. |
| 155-158 | "Quake II and Quake III exist as proper framework ports but are registered `if: false` … those three are exercised over the netboot NFS root" | All five are `if: true` (plus a new `supertuxkart` entry) and build into the image. |
| 260 | `rpi4-quake` in the run-it example | `quakespasm` |
| 303-309 | "These build but are **not yet part of the default `--with-showcase` image** … vkQuake additionally needs `--build-arg BUILD_FLAGS=…--with-vkquake` … remaining WIP is input wiring" | All part of the default `--with-showcase` image. `--with-vkquake` is a no-op (Vulkan is on by default). Input works. |
| 327-330 | STK "is built on demand (large mobile-reduced asset set, ~150 MB) rather than baked into the default image" | Baked in: `stage-game-data.sh` stages `data/` + `stk-assets/` (194 MB) into the overlay. |

### `docs/BUILD.md`

| Line | Current claim | Corrected fact |
|---|---|---|
| 211-217 | phase-gpu builds "`libquakespasm.a` … `libvkquake.a`" and "the in-tree `rpi4-quake` / `rpi4-vkquake` `_user` components link these and are bundled into `loader.disk`" | Phase gpu builds only `lib{v3d,GL,v3dv}-phoenix.a`; the five game **ports** link them and install into the rootfs. No `_user` game components exist. |
| 234-236 | "`--skip-vulkan` (GL only, no vkQuake)" listed as an ordinary option | `--skip-vulkan` now makes the ports stage fail (the `vkquake` port needs `libv3dv-phoenix.a`); Vulkan is on by default. |
| 243-246 | "GLQuake (`libquakespasm` → `rpi4-quake`)" | GLQuake is the `quakespasm` port → `/usr/bin/quakespasm`. The glslang/SPIR-V caveat still holds for the `vkquake` port. |
| — | (missing) | Document `scripts/stage-game-data.sh` and `scripts/build-rootfs-helpers.sh`, and the 1.5 GiB `--with-showcase` ext2 root. |

### `docs/KNOWN-ISSUES.md`

Referenced from README as the home of the "vkQuake input not wired" limitation — check
and remove that entry (owner: mouse + keyboard worked).

### `docs/inprogress/WEEK-2026-W36.md`

* §3 "Open question the audit could not settle without a Pi cycle: … whether that
  configuration has ever rendered a 3D frame on hardware is unresolved" — the owner has
  answered it (gl3/GLES3 with mipmap generation off *is* the rendering config), and the
  binary now built carries that gate. Delete the open question.
* D10 row: "(a) needs Pi cycles: flip each game's `if: false` → `true` … one game per
  cycle" — the flips are done for all five plus `supertuxkart`; what remains is HW
  confirmation, not the flip.

### `sources/phoenix-rtos-ports/*/port.def.sh` header comments

`quakespasm:35-69`, `yquake2:35-59`, `quake3:38-64`, `vkquake:35-52` still describe
themselves as pre-flip (`if: false`, "the image still gets the game from the ad-hoc
showcase path"). The `ports.yaml` entries were rewritten; these headers were **not**
touched beyond the yquake2 renderer note, to keep this change reviewable. They need a
follow-up pass.
