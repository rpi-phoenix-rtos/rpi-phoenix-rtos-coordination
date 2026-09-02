# Game source-of-truth audit — `external/<game>` fork vs `ports/<game>` patch

Date: 2026-09-02. Audit only: no build inputs changed, no Pi cycle run, nothing pushed.
Scope: the four ported games (QuakeSpasm, vkQuake, yQuake2, Quake III/quake3e).

## Answer

1. All four ports recipes are `if: false` (`ports.yaml:159–204`) and nothing depends on them — **the ports path builds nothing in any image: netboot, sd, or clean Docker.** Only the fork path is wired.
2. **QuakeSpasm** — ships as `rpi4-quake` from the **fork** everywhere; patch is a **strict subset** and carries every named fix (#67, #26, #68, `r_quadparticles 0`, `r_oldwater 1`). No functional gap.
3. **vkQuake** — ships as `rpi4-vkquake` from the **fork** (opt-in `--with-vkquake`); patch is a **strict subset**, fork-only content is 2 UART traces. No functional gap.
4. **quake3e** — `/usr/bin/quake3e` on the NFS root is **fork-built, hand-carried**; no build produces it. Patch is a **strict subset** (fork-only = `tr_init.c` capture harness).
5. **yQuake2 — the one dangerous case.** `/usr/bin/yquake2` is **ports-built, hand-carried**. The patch's `vid.c VID_HasRenderer` gl3/gles3 acceptance exists **nowhere in the fork**; the fork's `gl3_image.c` V3D mipmap-skip fix exists **nowhere in the patch**. Neither tree has both.
6. That yQuake2 split is **one day old**: patch hunk from ports `d01a6ab` (2026-08-27 03:31), fork narrowing from `d0c064f9` (**2026-09-02 20:47, today**) — active drift, not history.
7. **Pinned tarball commit == fork merge-base with upstream for all four.** Zero base drift.
8. **Nothing the clean Docker build ships is unverified** — it ships the fork-built GLQuake we HDMI-verify. The exposure is the *future* `if: true` flip.
9. Recommendation: **(A) fork canonical + generate the ports patch from the fork delta + a `--check` that fails the build**, uniformly for all four; reconciling yQuake2 (patch→fork) is the prerequisite.

---

## 1. Which path produces the binary that reaches the Pi

### The ports path is inert

`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml`:

| port | line | gate |
|---|---|---|
| `quakespasm` | 159–160 | `if: false` |
| `yquake2` | 175–176 | `if: false` |
| `quake3` | 190–191 | `if: false` |
| `vkquake` | 203–204 | `if: false` |

No other port declares any of these in `depends`, so the resolver never pulls them. The only way
to build them is `scripts/build-port.sh <name>` by hand (`scripts/build-port.sh:14–18`: "Ports not yet
listed in a target ports.yaml (`if: false` or absent) are skipped by the normal `build.sh ports` stage;
this helper builds them regardless.").

### The fork path is what the build wires

- `scripts/build-showcase-apps.sh:337–342` builds `tools/.gpu-libs/libquakespasm.a` from
  `external/quakespasm/Quake` via `tools/quakespasm-port/build-quakespasm-phoenix.py`.
- `scripts/build-showcase-apps.sh:381–407` builds `tools/.gpu-libs/libvkquake.a` from
  `external/vkquake` via `tools/vkquake-port/build-vkquake-phoenix.py` (only under `--with-vkquake`).
- `_user/rpi4-quake/Makefile:41` links `$(GPU_LIBS)/libquakespasm.a`;
  `_user/rpi4-vkquake/Makefile:43` links `$(GPU_LIBS)/libvkquake.a`.
- `grep -rn quake scripts/build-showcase-apps.sh` returns **no** `yquake2` / `quake3e` reference.
- `external/` clone refs: `scripts/bootstrap-linux-host.sh:157–163` — all four of our forks are cloned
  at branch **`phoenix-rpi4-port`** (mesa alone is pinned upstream + a patch).

Local fork checkouts are exactly the published tips (`git -C external/<g> rev-list --left-right --count
HEAD...publish/phoenix-rpi4-port` → `0 0` for all four), so "fork tree" below == what a fresh clone gets.

### (a) netboot / nfsroot (the daily test path)

Root is the NFS export `/srv/phoenix-rpi4-nfs`, rebuilt by `scripts/make-pristine-nfs-export.sh`
from `.buildroot/_fs/<target>/root` **plus a hand-copy overlay**. That script's own comments
(lines 29–32) say the Q2/Q3 engines are "not in the standard build" and copies them forward from the
previous export:

```
for b in usr/bin/yquake2 usr/bin/quake3e usr/bin/quake2 usr/bin/quake3; do
```

Observed on `/srv/phoenix-rpi4-nfs` (2026-09-02):

| artifact | size / date | path that built it | evidence |
|---|---|---|---|
| `usr/bin/rpi4-quake` | 17.6 MB, Aug 26 | **fork** | `grep -a -c mg_tga` = 1, `r_alias_lerpmode` = 1, `scr_capture_host` = 1 (all fork-only) |
| `bin/quakespasm`, `bin/quakespasm-sdl` | 23.3 / 25.0 MB | **fork** | same three markers = 3 each |
| `usr/bin/rpi4-vkquake` | 12.9 MB, Aug 26 | **fork** | `grep -a -c "vkvid: shmod"` = 2 (fork-only diag) |
| `bin/vkquake` | 22.3 MB, Aug 22 | **fork** | `"vkvid: shmod"` = 2 |
| `usr/bin/yquake2` | 19.1 MB, **Aug 27 03:25** | **ports** | `grep -a -c scr_capture` = **0**, i.e. the fork's `gl1_sdl.c` capture harness is absent — and that harness landed on the fork in `ea5d7aef` / `c6cbed43`, both **2026-08-22**, five days *before* this binary was built, so a fork build would necessarily contain it. Decisive. |
| `usr/bin/quake3e` | 28.2 MB, Aug 22 | **fork** | `grep -a -c scr_capture` = **1**; that string exists only in the fork's `tr_init.c` — `grep -rn scr_capture sources/phoenix-rtos-ports/quake3/` returns nothing |
| `usr/bin/quake2`, `usr/bin/quake3` | 708 KB / 149 KB | neither — small launchers | `tools/yquake2-port/quake2-launcher.c`, `tools/quake3-port/quake3-launcher.c`; no script builds them today |

**Marker rejected (recorded so it is not reused):** a `gles3` string count in the yquake2 binary does
**not** discriminate. Pristine upstream `src/client/vid/vid.c` already contains 5 `gles3` literals
(its renderer-fallback chain, lines 537–552: `Com_Printf("Retrying with gles3...")`,
`Cvar_Set("vid_renderer", "gles3")`), and the fork tree has them too. Only the `scr_capture` marker
above is load-bearing.

**Which renderer the staged yquake2 actually is:** the **gl3 / GLES3** variant, not `gl1`.
`grep -a -c` on `/srv/phoenix-rpi4-nfs/usr/bin/yquake2`: `'version 320 es'` = 1, `ref_gl3` = 3,
`'OpenGL ES'` = 10, `glGenerateMipmap` = 2, `ref_gl1` = 0, `YQ2_GL3_MIPMAP` = **0**. So the Quake II
binary currently on the NFS root is the ports-built `YQ2_RENDERER=gl3` build **without** the fork's
V3D mipmap-skip fix — see §3 for why that matters. (Its mtime, Aug 27 03:25, sits 6 minutes before
ports commit `d01a6ab` "yquake2: gl3 GLES3 renderer variant (YQ2_RENDERER=gl3) — runs on V3D",
2026-08-27 03:31, i.e. it is that session's build.)

At the time of first inspection, `.buildroot/_fs/aarch64a72-generic-rpi4b/root/usr/bin` contained
**only** `rpi4-quake` (17,801,472 B, Aug 28 00:28) and `rpi4-vkquake`; `root/bin` had no `quake*` entry.
(That tree was wiped mid-audit by a concurrent rebuild — `_boot/` timestamps moved to Sep 2 21:43 —
so the observation is quoted as of first read, not re-verifiable now.)

`loader.disk` bundling: `scripts/rebuild-rpi4b-fast.sh:459–465` bundles `rpi4-quake` only when
`tools/.gpu-libs/libquakespasm.a` exists (i.e. the fork build ran);
`user.plo.yaml:249–250` launches it for non-`nfsroot` variants under `RPI4B_WITH_SHOWCASE=1`.
`user.plo.yaml:251–259` shows `rpi4-vkquake` is built but its launch line is commented out (the two swap).

### (b) `sd` image

`scripts/rebuild-rpi4b-fast.sh:517–534`: the sd variant's ext2 root is populated from
`_fs/<target>/root` by `scripts/build-rpi4b-rootfs-ext2.sh`; `PREFIX_ROOTFS="$PREFIX_FS/root/"`
(`phoenix-rtos-build/build.sh:41`). The showcase `--phase stage` step
(`rebuild-rpi4b-fast.sh:509–515`) stages X11/ports apps but **no** game binaries
(`build-showcase-apps.sh:449–526` — the games arrive via `_user`, not staging).

Direct check of the exported sd image `artifacts/rpi4b/rpi4b-sd-2part.img` (Sep 1 19:34):

```
mg_tga: 1        scr_capture_host: 1    rpi4-quake: 1
'Yamagi Quake II': 0   'Quake3e': 0     gles3: 0
```

⇒ sd ships the **fork-built** GLQuake and **no** Quake II / Quake III at all.

### (c) authoritative clean Docker build

`Dockerfile:39` `ARG REPO_BASE=https://github.com/rpi-phoenix-rtos`; `:55` `ARG BUILD_FLAGS=--with-showcase
--with-ports`; `:73` runs `bootstrap-linux-host.sh` (which clones the four forks at `phoenix-rpi4-port`,
lines 157–163); `:92` runs `./scripts/rebuild-rpi4b-fast.sh --variant "${BUILD_VARIANT}" ${BUILD_FLAGS}`.
`--with-ports` runs the `build.sh ports` stage, which honours `ports.yaml` — where all four games are
`if: false`. So the Docker build takes the **fork** path for QuakeSpasm (and vkQuake only if
`--with-vkquake` is added), and builds **no** yQuake2 / quake3e.

Corroborated on the last Docker-built image on disk, `artifacts/rpi4b/docker-sd-complete.img`
(875 MB, Jul 23):

```
scr_capture_host: 2   rpi4-quake: 3   QuakeSpasm: 7
YQ2_GL3_MIPMAP: 0     yq2cap: 0       'Yamagi Quake II': 0   'Quake3e': 0
mg_tga: 0   r_alias_lerpmode: 0     <- image predates those fork commits (Jul 28 / later)
```

The `scr_capture_host` hit proves the fork's capture harness is compiled in, i.e. fork-built GLQuake.

### Does anything get built by BOTH?

No — not in any configuration reachable from a build script. For each game exactly one path runs:
fork for quakespasm/vkquake (automatically), and for yquake2/quake3 **neither** path runs
automatically; their binaries only exist because someone ran `build-port.sh` (yquake2) or a
now-deleted `tools/` script (quake3e) by hand and `make-pristine-nfs-export.sh` keeps copying the
result forward.

**Stale documentation found (reported, not edited):** `scripts/bootstrap-linux-host.sh:113–116` claims
the `yquake2`/`quake3e` forks were added so "a fresh clone can build them at all". That is no longer
true — `tools/yquake2-port/` and `tools/quake3-port/` now hold only the launcher `.c` + `COPYING`
(their engine build scripts were deleted on migration; see each `README.md:3–8`), and
`build-showcase-apps.sh` never mentions them. A fresh clone gets two `external/` trees that nothing
builds from.

---

## 2. Is each shipped patch a strict subset of the fork delta?

Method (script kept at `/home/houp/.claude/jobs/c8f1289c/tmp/subset-check.sh`):

1. `sha256sum` the four cached tarballs against the `port.def.sh` pins — **all four match**
   (`c540596d…`, `8b913a17…`, `e811f2b6…`, `2c8d8fdd…`).
2. `tar -xzf` each tarball into a scratch dir; `patch -p1 -d <up> -i <shipped patch>` — all four
   apply cleanly (exit 0, no fuzz, no rejects).
3. `git -C external/<g> archive HEAD` → `tar -xf` (avoids working-tree litter).
4. `patch -R -p1 --dry-run -d <fork> -i <shipped patch>` — reverse-applying the shipped patch onto the
   fork tree. Success proves every hunk's *post*-state is present verbatim in the fork ⇒ subset.
5. `diff -rq <patched-upstream> <fork>` to enumerate what the fork has beyond the patch.

### Base drift: none

| game | `port.def.sh` pin | `git merge-base HEAD <upstream>` | equal? |
|---|---|---|---|
| quakespasm | `6baceeac…` (`sezero/quakespasm`, line 18) | `6baceeac…` | ✅ |
| vkquake | `9be3a5ad…` (`Novum/vkQuake`, line 19) | `9be3a5ad…` | ✅ |
| yquake2 | `e27fdcce…` (`yquake2/yquake2`, line 19) | `e27fdcce…` | ✅ |
| quake3 | `62398290…` (`ec-/quake3e`, line 21) | `62398290…` | ✅ |

The pinned tarball is exactly the fork's fork-point in all four cases.

### Diffstat comparison (confirms the numbers in the brief)

| game | shipped patch | fork delta vs merge-base |
|---|---|---|
| quakespasm | 7 files, +97/−17 | 12 files, +522/−17 (24 commits) |
| vkquake | 9 files, +175/−24 | 9 files, +202/−24 (22 commits) |
| yquake2 | 4 files, +24/−79 | 7 files, +254/−81 (4 commits) |
| quake3 | 4 files, +42/−10 | 5 files, +231/−10 (2 commits) |

### QuakeSpasm — **strict subset** ✅

Reverse-apply: all 7 files OK (`r_alias.c` hunks 1–3 succeeded with fuzz 2 / offset ≤21 — context
shifted by the fork's later additions; the changed lines themselves match, and the
patched-upstream↔fork diff for that file is **additions only**, confirming subset).

(i) In the FORK but not shipped:

| file | fork-only content | purpose |
|---|---|---|
| `Quake/gl_screen.c` | +195: `scr_capture` / `scr_capture_max` / `scr_capture_dir` / `scr_capture_host` / `scr_capture_port` cvars, `scr_captureConnect()`, `phxgl_capture_gl()` readback, PNG/TGA writer | visual-regression capture harness (Pi→host TCP frame sink, `scripts/quake-capture-sink.py`) |
| `Quake/gl_rmain.c` | +178: `r_mg_models[]`, `R_ModelGallery_f`, `R_ModelGallery_DrawTag`, `r_mg_active/_shown_idx/_fullbright`, `mg_tga` | #67 model-gallery test harness (block-code tagged frames) |
| `Quake/glquake.h` | +8 | declarations for the above, marked `/* #67 … REMOVE after */` |
| `Quake/gl_rmisc.c` | +4 | registers `mg` command, `mg_tga`, `r_alias_lerpmode` |
| `Quake/r_alias.c` | +26 beyond the patch | `r_alias_lerpmode` bisect cvar (0 snap / 1 lerp / 2 blend0; **default 1 == the patch's behaviour**) + `r_mg_fullbright` uniform override |
| `.gitignore` | +14 | build artefacts |

All six are test-harness / bisect-diagnostic; none changes default rendering behaviour.

(ii) In the SHIPPED PATCH but not in the fork: **none.**

### vkQuake — **strict subset** ✅

Reverse-apply: all 9 files OK (`gl_rmisc.c` hunks 2–12 with offset 5–6 / fuzz 2; the
patched-upstream↔fork diff for that file is additions only).

(i) Fork-only:

| file | fork-only content | purpose |
|---|---|---|
| `Quake/gl_screen.c` | +21: edge-triggered `Sys_Printf("vkvid: SCR_UpdateScreen gate -> %d …")` block, marked `TODO(vkquake-port)` | bring-up frame-gate trace |
| `Quake/gl_rmisc.c` | +6: two `Sys_Printf("vkvid: shmod …")` traces around `vkCreateShaderModule`, marked `TODO(vkquake-port)` | shader-module bring-up trace |

(ii) In the shipped patch but not in the fork: **none.**

### quake3e — **strict subset** ✅

Reverse-apply: all 4 files OK, no fuzz. `diff -rq` shows only `code/renderer/tr_init.c` differing.

(i) Fork-only: `code/renderer/tr_init.c` +189 — the deterministic visual-regression capture hook
(`scr_capture*` cvars via `ri.Cvar_Get`, `phxgl_capture_gl()`, TGA-over-TCP sink; the reference demo
is `tools/quake3-port/demos/cap.dm_68`, driven by `scripts/quake3-host-capture.sh`).
`port.def.sh:61–64` already documents this as deliberately excluded.

(ii) In the shipped patch but not in the fork: **none.**

### yQuake2 — **NOT a subset** ⚠️

Reverse-apply: 3 of 4 files OK; **`src/client/vid/vid.c` hunk #1 FAILED at 332.**

(ii) **In the SHIPPED PATCH but NOT in the fork — the dangerous category:**

`sources/phoenix-rtos-ports/yquake2/patches/0001-single-elf-static-link.patch`, `vid.c`
`VID_HasRenderer()`. Patched-upstream vs fork:

```
 	/* Phoenix single-ELF port: the renderer is statically linked, there is
-	 * no ref_*.so file to stat. GetRefAPI link-resolves to the ONE compiled-in
-	 * renderer (gl1 or gl3/gles3), so accept those names; …
-	return (strcmp(renderer, "gl1") == 0 || strcmp(renderer, "gl3") == 0 || strcmp(renderer, "gles3") == 0) ? true : false;
+	 * no ref_*.so file to stat. Report the compiled-in renderer (gl1) as
+	 * present; …
+	return (strcmp(renderer, "gl1") == 0) ? true : false;
```

(`-` = shipped-patch result, `+` = fork.) The gl3/gles3 broadening exists **only** in the patch file in
`sources/phoenix-rtos-ports`; it appears in no commit on `external/yquake2`
(`phoenix-rpi4-port` = `d0c064f9`, 4 commits from the merge-base). It is a real, load-bearing change:
`yquake2/port.def.sh:104–111, 171–194` adds an opt-in `YQ2_RENDERER=gl3` build (GLES3 TU list,
`-DYQ2_GL3_GLES3 -DYQ2_GL3_GLES`), and without this hunk `VID_HasRenderer("gles3")` would refuse it.
**Timeline — this divergence is one day old, and it tells you which tree is real for yQuake2:**

| when | where | what |
|---|---|---|
| 2026-08-22 00:28 / 01:10 | fork `ea5d7aef`, `c6cbed43` | gl1 capture harness (`gl1_sdl.c`, `gl1_main.c`) |
| 2026-08-26 14:25 | ports `8954e79` | `yquake2` framework port added (patch v1) |
| **2026-08-27 03:31** | **ports `d01a6ab`** | "gl3 GLES3 renderer variant (`YQ2_RENDERER=gl3`) — **runs on V3D**" — this is the commit that **introduced the gl3/gles3 acceptance into the patch** |
| 2026-08-27 07:32 | fork `b7fad67a` | "gl3: default mipmap generation off on the Phoenix/V3D port" — the fix, committed **only to the fork**, 4 h after the ports gl3 variant |
| **2026-09-02 20:47** | **fork `d0c064f9`** | "Phoenix single-ELF port: drop duplicate symbols and the .so renderer probe" — rewrote `VID_HasRenderer` from pristine upstream's `Sys_IsFile` probe straight to **gl1-only**, i.e. the narrowing landed **today** |

Two consequences worth stating plainly:

- The fork never *had* the gl3/gles3 acceptance and then lost it — `d0c064f9` replaced pristine
  upstream's `.so`-stat implementation with a narrower one than the patch already shipped. So this is
  new divergence created today, not an old one.
- Before `d0c064f9` (today), `external/yquake2` carried **no** single-ELF changes at all — its `vid.c`
  still stat'ed `ref_*.so`, which on Phoenix returns false for every renderer. The fork tree was
  therefore **not independently buildable as the single ELF** during the whole period the gl3 work
  happened. For yQuake2, unlike quakespasm/vkquake, **the ports patch is the artifact that was
  actually built and run**, and the fork is a recently-reconstructed upstream-form record of it. That
  is why the reconciliation direction in §4 is patch→fork for this game.

(i) In the FORK but not shipped:

| file | fork-only content | purpose |
|---|---|---|
| `src/client/refresh/gl1/gl1_sdl.c` | +206: `scr_capture*` cvars, `yq2cap_scene_rendered` gate, TGA/TCP sink | visual-regression capture harness |
| `src/client/refresh/gl1/gl1_main.c` | +3: sets `yq2cap_scene_rendered = 1` in `RI_RenderFrame` | hook for the above |
| `src/client/refresh/gl3/gl3_image.c` | +23/−2: `YQ2_GL3_MIPMAP` env gate — **skips `glGenerateMipmap` by default** on the V3D port | **functional fix, not a harness.** Comment: "on the ported Mesa V3D each mip level is a separate TFU copy + L2T cache flush, so a map's texture load takes many minutes and the 3D view never appears. With mipmaps off the load completes in seconds and the full textured 3D map renders (HW-verified on RPi4)" |

**The asymmetry is the finding:** the patch *advertises* gl3/gles3 as available; the fork *fixes* gl3 so
it is usable. Neither tree has both. A ports-built `YQ2_RENDERER=gl3` binary would pass
`VID_HasRenderer("gles3")` and then take minutes per map load. This is latent (the port defaults to
`gl1`, `port.def.sh:189`) but it is exactly the class of divergence the audit was asked to find.

---

## 3. Does anything differ functionally between what we HDMI-verify and what a clean build ships?

**No — today.** Both the daily netboot run and the clean Docker build ship the *same* fork-built
`rpi4-quake` / `rpi4-vkquake`. The clean-build gate does not ship any ports-built game
(all four `if: false`), so there is no unverified game in the clean image.

### QuakeSpasm named fixes — all in the shipped patch

`grep -n` on `patches/0001-quakespasm-phoenix-v3d-single-elf.patch`:

| fix | fork commits | in shipped patch? | patch evidence |
|---|---|---|---|
| #67 alias-model collapse / single-pose de-alias | `0a900c7`, `4ef0a42`, `3d742a3` | ✅ | `:17` `// #67: one pose block per pose…` (gl_mesh.c), `:145` `/* #67 single-pose DE-ALIAS…`, `:169` `/* #67 SINGLE-POSE FIX (2026-07-27)…` (r_alias.c) |
| #26 LAN play (FIONREAD → FIONBIO+PEEK) | `4abb324` | ✅ | `:98–117` net_udp.c hunk, `…task #26` |
| #68 direct connect, skip discovery slist | `c90c9b9` | ✅ | `:70–76` net_main.c, `/* Phoenix #68 fix: … goto JustDoIt; */` |
| `r_quadparticles` default 0 | `55b479e` | ✅ | `:200/:207` `"r_quadparticles","1"` → `"0"` |
| `r_oldwater` default 1 | `ff17470` | ✅ | `:48/:57` `"r_oldwater","0"` → `"1"` |
| streaming element VBO for brush surfaces | `5f02adb` | ✅ | `:223` r_world.c hunk |
| #28 alias lerp restore / lit-term floor | `8fdede9`, `5e3ec37`, `90da546` | ✅ (net effect) | fork default `r_alias_lerpmode 1` == the patch's unconditional real 2-pose lerp |
| capture harness, model gallery, `r_alias_lerpmode`, `r_alias_debug` | `8bb6366`, `e3f5633`, `c533d16`, `9a54f7d`, `268e2d8`, `e0109fc`, `2706871`, `332f7f2`, `f12d397`/`394342e` (added then reverted), `785eaea` | ❌ fork-only | diagnostics — intentionally excluded |

⇒ A ports-built `quakespasm` would render the same as the HDMI-verified `rpi4-quake`, minus the
capture/gallery commands. **No functional gap.**

### vkQuake fixes — all in the shipped patch

The 22 fork commits from `9be3a5ad..HEAD` include the functional set: `d3e329c` alias alpha=1
(invisible torches), `53c87ed`/`0d8dc54` liquid base texture + compute warp, `81fe88a` back-face
culling re-enable, `f4d923e` degenerate `vkCmdCopyBufferToImage` extent (#29), `cabcd35`
mmap-backed shader-module allocator, `45ede2c` exposed basic-pipeline builders, `b9dfe94`
demo-playback file fix, `51ddfc4`/`2c893c2` workaround removals. Reverse-apply succeeded on all 9
files and the only patched↔fork differences are the two `TODO(vkquake-port)` `Sys_Printf` traces
⇒ every functional fix is in the shipped patch. **No functional gap.**

### quake3e

Patch is a strict subset; fork-only = the `tr_init.c` capture harness. **No functional gap.**
(The `/srv` binary we HDMI-verified is nonetheless the *fork*-built one — 28.2 MB vs the
"18.3 MiB stripped" the port build reports at `ports.yaml:186` — so the ports-built quake3e
has never itself been HDMI-verified. The source delta says it should behave identically.)

### yQuake2 — the one real gap

Fork-only `gl3_image.c` mipmap-skip is a **functional, HW-verified** fix absent from the shipped
patch; the shipped patch's gl3/gles3 acceptance is absent from the fork. For the `gl1` default
(`port.def.sh:189`) the two trees are byte-equivalent, so a gl1 build has no gap.

**But the gap is not merely latent.** The Quake II binary actually staged on the NFS root is the
**gl3/GLES3** build (`'version 320 es'`=1, `ref_gl3`=3, `ref_gl1`=0) **without** the mipmap-skip fix
(`YQ2_GL3_MIPMAP`=0, `glGenerateMipmap`=2) — see §1(a). By the fork's own comment on `b7fad67a`, that
configuration takes "many minutes" per map load on V3D. So the Quake II we would run today from the
NFS root is a build the fork says needs a fix it does not contain.

**Unclear / not determined:**
- Whether that staged gl3 binary has ever been run to a rendered 3D frame on HW. Ports commit
  `d01a6ab` (6 minutes after the binary's mtime) claims "runs on V3D"; the fork commit 4 hours later
  says the same configuration never reaches the 3D view without the mipmap fix. Both cannot be true
  of the same code, and no Pi cycle was permitted for this audit. **Unresolved.**
- Where the fork's "HW-verified on RPi4" mipmap-skip was verified. Given the fork tree was not
  single-ELF buildable until today (§2), it was most likely verified in an out-of-tree/ports build
  and only committed to the fork. **Not established from the repos.**

---

## 4. Recommendation

Owner constraints taken as binding: **(1)** GitHub must clearly show what we changed vs the upstream
project state; **(2)** upstream must stay trackable in future.

Both constraints favour keeping the forks. A patch file inside `phoenix-rtos-ports` shows the delta
only as a diff blob with no upstream ancestry, no per-change commit message, no `git log`, and no way
to rebase onto a newer upstream. Option (C) — pointing the recipe at our own fork tarball — satisfies
neither: it destroys the "pinned pristine upstream + visible patch" property that makes the ports
recipe auditable, and gives GitHub no fork-point to diff against.

**Chosen for all four games: (A) — fork canonical, ports patch GENERATED from the fork delta,
with a staleness check that fails the build.**

Rationale (uniform on purpose — four different policies is how this drift happened):

- The fork is already the de-facto source of truth for the two games that actually ship
  (quakespasm, vkquake), and it is the only representation that satisfies both owner constraints.
- The pin already equals the fork's merge-base for all four, so generation is mechanical:
  `git -C external/<g> diff <merge-base> <branch>` is *exactly* the ports patch, modulo the
  deliberate harness exclusions.
- Generation makes the exclusions explicit and reviewable instead of implicit and drifting.

### Minimal concrete change

1. **Add `scripts/gen-game-port-patch.sh <game>`** (new file, ~40 lines) that:
   - resolves `merge-base` of `external/<g>` `phoenix-rpi4-port` against the upstream remote and
     asserts it equals `commit=` in `sources/phoenix-rtos-ports/<port>/port.def.sh` (fails loudly on drift);
   - emits `git diff <merge-base> <branch> -- <paths>` with an **explicit exclude list** for the
     harness files, written next to the patch as `<port>/patches/EXCLUDE` so the exclusions are
     reviewable data, not script trivia. Initial contents:
     - quakespasm: `Quake/gl_screen.c Quake/gl_rmain.c Quake/glquake.h .gitignore` + the
       `gl_rmisc.c`/`r_alias.c` harness hunks (those two files need hunk-level filtering — see step 3);
     - vkquake: the two `TODO(vkquake-port)` trace hunks;
     - yquake2: `src/client/refresh/gl1/gl1_sdl.c src/client/refresh/gl1/gl1_main.c`;
     - quake3: `code/renderer/tr_init.c`.
   - `--check` mode: regenerate to a temp file and `diff` against the committed patch; non-zero exit
     if they differ.
2. **Wire `--check` into the build**: call it from `scripts/rebuild-rpi4b-fast.sh` (next to the existing
   showcase freshness checks around line 147–159) and from `scripts/publication-audit.sh`, so a fork
   commit that never made it into the patch fails a build instead of silently diverging.
3. **Resolve the yQuake2 divergence first** — it is a prerequisite for (1), since generation would
   otherwise *delete* the gl3/gles3 acceptance. Two commits on `external/yquake2`
   `phoenix-rpi4-port`, then regenerate the patch:
   - port the shipped patch's `VID_HasRenderer` gl3/gles3 broadening **into the fork** — direction is
     patch→fork here because for yQuake2 the patch is the tree that was actually built and run
     (§2 timeline); do not let generation silently narrow it to gl1;
   - keep `gl3_image.c`'s mipmap-skip and let it flow **into** the generated patch (it is a functional
     V3D fix, not a harness — the current exclusion is accidental, not deliberate);
   - then rebuild + HDMI-verify the gl3 binary once, since the currently staged one has neither half
     (§3) and its behaviour is unresolved.
4. **Hunk-level exclusion** for quakespasm `gl_rmisc.c` / `r_alias.c` and vkquake's two trace blocks:
   cheapest robust approach is to move each harness block behind
   `#ifdef PHX_CAPTURE_HARNESS` in the fork, and have the generator drop hunks whose added lines are
   entirely inside such a guard. That also makes the harness compile-optional in the fork itself,
   which is what `glquake.h`'s own `/* … REMOVE after */` marker is asking for.
5. **Housekeeping (separate, trivial):**
   - fix `scripts/bootstrap-linux-host.sh:113–116` — the yquake2/quake3e forks are not buildable from a
     fresh clone; either restore a build path or say plainly that the ports recipe is the build path;
   - decide the yquake2/quake3e ship story. Right now `make-pristine-nfs-export.sh` copies two
     engines forward that no build produces, one fork-built and one ports-built. Either flip
     `yquake2` / `quake3` to `if: true` in `ports.yaml` (making the ports path real, HDMI-verify once,
     and drop the hand-copy), or drop them from the export. Carrying hand-staged engines indefinitely
     is what made `quake3e` un-attributable until this audit.

**Where this recommendation is uncertain:** whether the owner wants the ports recipes to remain
"pristine upstream tarball + patch" at all for the two games that no image builds
(yquake2, quake3). If the answer is that those two games should ship from the ports framework, then
step 5's `if: true` flip must come with a real HDMI verification of the ports-built binaries — which
this audit could not do (no Pi cycle allowed; UART locked).

---

## Commands run (all read-only)

```
grep -rn "quakespasm\|vkquake\|yquake2\|quake3" scripts/build-showcase-apps.sh
cat sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml
grep -n ... sources/phoenix-rtos-ports/{quakespasm,vkquake,yquake2,quake3}/port.def.sh
git -C external/<g> {branch -a, log -1, remote -v, merge-base, diff --stat, archive}
git -C external/yquake2 log --format='%ci %h %s' e27fdcce..HEAD
git -C external/yquake2 show d0c064f9 -- src/client/vid/vid.c
git -C sources/phoenix-rtos-ports log --format='%ci %h %s' -- yquake2/patches/0001-single-elf-static-link.patch
git -C /home/houp/phoenix-rpi apply --stat sources/phoenix-rtos-ports/<g>/patches/*.patch
sha256sum sources/phoenix-rtos-ports/<g>/<commit>.tar.gz
bash /home/houp/.claude/jobs/c8f1289c/tmp/subset-check.sh     # extract + patch + reverse-apply + diff -rq
diff -u <patched-upstream>/<file> <fork>/<file>
grep -a -c '<fork-only-marker>' <binary|image>
```

Scratch trees (patched-upstream + fork exports, all four games) are under
`/home/houp/.claude/jobs/c8f1289c/tmp/` for re-inspection.
