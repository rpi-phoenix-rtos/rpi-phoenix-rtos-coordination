# Game engine forks: upstream sync plan (analysis only)

**Date:** 2026-09-03 · **Scope:** analysis, no merging. Requested by the owner ("for all the
quakes it would also make sense to check what updates came in into the upstream projects and if
possible sync with upstream").

Measured read-only against freshly fetched upstream refs. All four `external/` working trees were
left untouched (a build was compiling out of them). `./scripts/game-port-patch.sh --check` was
**OK for all four** at the time of measurement, so every number below is against a tree whose
shipped patch equals its fork delta.

## 0. The contract a sync has to preserve

`scripts/game-port-patch.sh` generates each port's patch as `git diff <pin>..HEAD` in the fork,
where `<pin>` is `commit="<sha>"` in `sources/phoenix-rtos-ports/<port>/port.def.sh`. The pin must
be an **ancestor of the fork HEAD**. Consequences for any sync:

- **Merge upstream into the fork branch; do not rebase.** All four fork HEADs are already
  published as `refs/heads/phoenix-rpi4-port` on the `publish` remote (verified via `ls-remote`), so
  history must not be rewritten. A merge commit makes the new upstream tip an ancestor of HEAD, so
  the pin contract still holds and `diff pin..HEAD` still contains *only our delta*.
- **Upstream-added binaries are harmless** after a merge: they land in the new base, not in the
  diff. (This matters for vkQuake, whose pending upstream commits add `Windows/SDL3/lib64/*.dll`
  etc.; `game-port-patch.sh` refuses to emit a `GIT binary patch`.)
- **Bumping the pin is a three-part edit** in `port.def.sh`: `commit`, plus `size` and `sha256` of
  the new `https://github.com/<proj>/archive/<sha>.tar.gz`, then `game-port-patch.sh --regen`.
- **Glue-replaced TUs are a silent drift surface, not a conflict surface.** Each port swaps some
  upstream TUs for `glue/pl_phoenix_*`. Upstream changes there never conflict *and never reach our
  binary*. Example: quakespasm's `2a70c79 Increase default heap size to 384 MB` edits
  `Quake/main_sdl.c`, which our port replaces with `glue/pl_phoenix_main.c` — invisible to us
  either way. Reviewing those files by hand is a separate, per-sync chore.

## 1. Drift summary

| Fork | Upstream ref measured | Pinned base | Our commits ahead | Upstream commits behind | Our delta | Conflict surface |
|---|---|---|---|---|---|---|
| `external/quake3e` | `origin/main` @ `3f996601` | `62398290` | 2 | **11** (11 non-merge) | 5 files, +231/−10 | **0 files** |
| `external/quakespasm` | `origin/master` @ `f5fe178` | `6baceeac` | 24 | **12** (12 non-merge) | 12 files, +522/−17 | **1 file** (orthogonal) |
| `external/yquake2` | `origin/master` @ `a9e88f6c` | `e27fdcce` | 5 | **85** (76 non-merge) | 7 files, +258/−81 | **3 files** (2 trivial, 1 orthogonal) |
| `external/vkquake` | `origin/master` @ `44732b9` | `9be3a5ad` | 22 | **216** (216 non-merge) | 9 files, +202/−24 | **8 of 9 files** (3 hard) |

In every case `merge-base(HEAD, upstream) == pin`, i.e. the fork has not merged upstream since it
was pinned, and the pin is an ancestor of both sides. The reported 2026-09-02 numbers (12 / 212 /
85 / 11) were accurate except vkQuake, now 216.

---

## 2. quake3e (Quake3e, `ec-/quake3e`) — 11 behind, zero conflict

**Our delta** (`62398290..acdf34a7`): `code/qcommon/huffman.c`, `code/qcommon/q_platform.h`,
`code/qcommon/vm_aarch64.c`, `code/renderer/qgl.h`, `code/renderer/tr_init.c`.

**Upstream changed** (22 files): `code/asm/common_x64.asm`, `code/client/{cl_avi.c,cl_main.c,client.h}`,
`code/qcommon/{cm_local.h,cm_patch.c,cm_patch.h,cm_polylib.c,cm_polylib.h,cm_trace.c,common.c,cvar.c,files.c,qcommon.h,vm.c,vm_local.h,vm_optimize.h,vm_x86.c}`,
`code/server/sv_init.c`, `code/win32/{win_main.c,win_snd.c,win_wndproc.c}`.

**Intersection: empty.** Upstream's work this window is x86 JIT, Win32 and collision-model
precision; ours is aarch64 JIT + platform block + renderer capture. A merge should be textually
clean.

**Valuable to us:**
- `6efde175` *CM: fixed precision for curve mesh collisions — this fixes x86 32-64 bit
  mispredictions on round corners* and `aa66449d` *CM: fixed another tiny precision loss/misprediction
  on q3dm12* — cross-architecture float determinism in the collision model. Directly relevant to
  aarch64 (and to `external/quakespasm-det`-style determinism work).
- `3e4c1fb5` *CM: fix root of MAX_PATCH_PLANES error, reduce definition MAX_PATCH_PLANES to the
  original value* — a real out-of-bounds/error-path fix; also lowers a static allocation.
- `d0b2021e` *vm: explicitly mark all jump targets with unsafe bit … allocate initial memory with
  RW permissions instead of RWX* — touches `vm.c`/`vm_local.h`, i.e. the same W^X policy area our
  `vm_aarch64.c` RWX-mmap workaround lives in. Read it before merging: it may either simplify our
  workaround or (if the RW-then-RX handoff is generalized) re-break it, since Phoenix `mprotect`
  cannot *add* `PROT_EXEC`.
- `49a2b433` *FS: keep qvm-referenced paks always locked / reduce simultaneously opened file handle
  limit to 250* — fewer concurrent handles is friendly to our NFS root.

**Risky for us:** nothing. `f8372fd8`/`0255f926`/`e4ba4cf3` are x86-only JIT work (`vm_x86.c`,
`common_x64.asm`) and cannot affect an aarch64 build; the Win32 commits are unreachable.

---

## 3. quakespasm (QuakeSpasm, `sezero/quakespasm`) — 12 behind, 1 orthogonal file

**Our delta** (`6baceeac..785eaea`): `.gitignore`, `Quake/{gl_mesh.c,gl_rmain.c,gl_rmisc.c,gl_screen.c,gl_warp.c,glquake.h,net_main.c,net_udp.c,r_alias.c,r_part.c,r_world.c}`.

**Upstream changed:** `Quake/{common.c,common.h,console.c,gl_model.c,gl_screen.c,gl_sky.c,main_sdl.c}`,
plus docs (`Quakespasm.{html,txt}`, `Linux/sgml/`), vendored SDL headers under `MacOSX/` and
`Windows/`, and `Misc/` housekeeping.

**Intersection: `Quake/gl_screen.c` only — orthogonal.**
Upstream's only change there is `38935b4` *Word-wrap centerprints if scr_usekfont is on*: one line
in `SCR_CenterPrint`, old line 146. Our hunks in that file are at old lines 25, 97, 407, 833, 1069,
1111 (the capture harness: includes, cvars, `SCR_Init` registration, the capture body, and two
`SCR_UpdateScreen` hooks). No hunk overlap, no shared symbol — a 3-way merge should apply both.

**Valuable to us:**
- `62993a6` *Enforce NUL terminator for BSP entity lump* and `04aee88` *Fix potential buffer overrun
  in condump command* — plain memory-safety fixes; cheap and worth having on a target where a stray
  overrun shows up as a Data Abort.
- `f6b1396` *gl_sky: include malloc.h for alloca() on MSVC too* — portability housekeeping, no effect
  for us but harmless.

**Risky for us:**
- `2a70c79` *Increase default heap size to 384 MB* — a 256→384 MB single `malloc` at start-up. It
  lives in `Quake/main_sdl.c`, which our port replaces with `glue/pl_phoenix_main.c`, so it does
  **not** reach us; note it only so nobody "helpfully" mirrors it into the glue.
- `d27b64c` *updated some SDL headers* — vendored `MacOSX/`/`Windows/` SDL copies; irrelevant, we
  build against the ported SDL2. It is most of the merge's file churn and can be accepted blind.

---

## 4. yquake2 (yQuake2, `yquake2/yquake2`) — 85 behind, 3 files, biggest payoff

**Fork state note:** `external/yquake2` is on a **detached HEAD** at `ee181885`; the local `master`
branch still sits on the pin. The commit is published (`publish/phoenix-rpi4-port` == `ee181885`),
so nothing is at risk, but step 0 of this sync is putting HEAD back on a named local branch.

**Our delta** (`e27fdcce..ee181885`): `src/client/cl_image.c`, `src/client/refresh/files/common.c`,
`src/client/refresh/gl1/gl1_main.c`, `src/client/refresh/gl1/gl1_sdl.c`,
`src/client/refresh/gl3/gl3_image.c`, `src/client/vid/vid.c`, `src/game/g_main.c`.

**Upstream changed:** 58 files — mostly `src/client/menu/menu.c`, `src/common/*` (a new
`strlist_t`/`FS_ListFilesx` API), the whole `src/client/refresh/gl3/` renderer, and Windows build
tooling.

**Intersection (3 files):**

| File | Upstream did | Verdict |
|---|---|---|
| `src/client/refresh/gl1/gl1_sdl.c` | `3771c7b9` collapses `gl1_discardfb`/`gl1_lightmapcopies` into one `gl_config.tilerendering` flag and **deletes the line `extern cvar_t *gl1_discardfb;`** | **Trivial conflict.** That deleted line is the context anchor of our ~200-line capture-harness insertion. Re-anchor the insertion; our code does not use the cvar. |
| `src/client/refresh/gl1/gl1_main.c` | Same commit plus `2b17da55` (scope fixes) rework `R_Register`/`R_Clear`/`RI_Init`; upstream's nearest hunk starts at old line 1219, immediately after `RI_RenderFrame`'s opening | **Trivial conflict at worst.** Our change is 3 lines *inside* `RI_RenderFrame` at old line 1212 (`yq2cap_scene_rendered = 1`). Adjacent, not overlapping. |
| `src/client/refresh/gl3/gl3_image.c` | Heavy rework (+333): `afee6e3c`/`bc301408`/`a40a0621` port the GL1 scrap texture to GL3 and make it 32-bit; `ac188b65` removes redundant `glTexParameteri` calls | **Orthogonal.** Upstream's hunks jump from old line 162 to old line 254; our `YQ2_GL3_MIPMAP` gate in `GL3_Upload32` sits at old line 212, inside the untouched gap. Re-verify after merge because the scrap path *calls* `GL3_Upload32(…, false)`. |

Everything else of ours (`vid.c` renderer list, `g_main.c`/`files/common.c` single-ELF de-duplication,
`cl_image.c`) is in files upstream did not touch.

**Valuable to us — this is the richest of the four:**
- **`9b891e59` *GLES3: Use glInvalidateFramebuffer()*** — the single most valuable pending commit
  across all four forks. Guarded by `YQ2_GL3_GLES`, i.e. exactly the renderer this port ships
  (`port.def.sh`: "ref_gl3/GLES3 by default"). It tells a **tile-based** GPU to drop
  color/depth/stencil at end-of-frame instead of resolving them to memory — V3D 4.2 is precisely
  that architecture, and tile store bandwidth is our known bottleneck. Small (4 files, +56/−21),
  self-contained, and cherry-pickable ahead of the full sync.
- `3bb54a62` *Batch 3D drawcalls (except for MD2 models)* (+558/−224 across gl3) with
  `84e1b2bd` *GL3: Batch drawTexturedRectangle() calls by texture*, `2e9138a4`, `6bebceb6`,
  `70357012` (`gl3_show_draw_stats`) — draw-call batching, the other half of the V3D win. Named
  runner-up; bigger and riskier than `9b891e59`, so land it as part of the merge, not as a pick.
- `b0ca130c` *GLES3: Use "highp" float precision in shaders* — GLES3 shader-precision correctness on
  a driver where mediump is genuinely lower precision.
- `afee6e3c`/`bc301408`/`a40a0621` (scrap texture in GL3, font in scrap) — fewer texture objects and
  fewer uploads, which directly reduces the per-texture TFU-copy cost that forced our
  mipmap-off workaround.
- `2873c455` *Get rid of gl3_usebigvbo* — removes a VBO-sizing mode; adjacent to the streaming-VBO
  class of bug we hit in quakespasm on V3D.
- `271560b2`, `575d77db`, `3771c7b9` — GLES/tile-rendering cvar and message cleanups; harmless and
  they reduce our conflict surface for next time.
- `e8bca839` *GL3: Implement gl_showtris* and `d7163fa2` (`DG_dynarr.h` errors now `Com_Error`) —
  useful debugging affordances.

**Risky for us:**
- `3f1fa575`/`2e2ea880`/`8cb48410`/`69d4982f`/`c228e24e` — a **filesystem-API refactor**
  (`FS_ListFiles`/`FS_ListMods` → `FS_ListFilesx`/`strlist_t`, `FS_FileExists` → macro over
  `FS_LoadFile2`). Our single-ELF patch de-duplicates symbols in `src/client/refresh/files/common.c`
  and `src/game/g_main.c`; these commits do not touch those files, but they change the FS symbol
  set the single ELF links, so a **link-time** break is the thing to watch, not a merge conflict.
- The `gl3` batching series rewrites `gl3_main.c`/`gl3_surf.c`/`header/local.h` wholesale. Any
  latent V3D-specific assumption in the old path has to be re-validated on HW — this sync needs a
  real HDMI run, not just a build.
- ~14 Windows build-env commits (w64devkit, release scripts, CI matrix) — pure churn, accept blind.

---

## 5. vkquake (vkQuake, `Novum/vkQuake`) — 216 behind; this is a re-port, not a merge

**Our delta** (`9be3a5ad..d3e329c`): `Quake/{gl_rmisc.c,gl_screen.c,gl_texmgr.c,glquake.h,host_cmd.c,r_alias.c,sv_main.c,sys_sdl.c}`, `Shaders/alias_common.inc`.

**Upstream changed:** ~450 files. Highlights: SDL3 becomes the default backend (`in_sdl.c` split into
`in_sdl2.c`/`in_sdl3.c`, `Windows/SDL2/` deleted, `Windows/SDL3/` added), mimalloc bumped to 3.4.5
(+ new `theap.c`, `threadlocal.c`), a moment-based OIT renderer (`r_oit 2`, `Shaders/mboit*`), the
shader set restructured (per-variant `.frag` files deleted in favour of generated variants), new
`json.c`/`jsmn.h`/`steam.c`, and a reworked `Quake/Makefile*`/`meson.build`.

**Intersection: 8 of our 9 files.**

| File | Ours | Upstream | Verdict |
|---|---|---|---|
| `Quake/gl_rmisc.c` | de-static `R_CreateShaderModules`/`R_DestroyShaderModules`/`R_InitVertexAttributes`/`R_CreateBasicPipelines`; mmap-backed `pAllocator` for shader modules; `cullMode=NONE` for 2D pipelines; skip `VK_NULL_HANDLE` render-pass variants | rewritten (+2326/−…): MBOIT pipelines, ~9 new `alias_mboit_*` shader modules, OIT selection deduplicated | **Hard conflict.** Every one of our anchor points moved. All four functions still exist and are still `static` upstream, and the `CREATE_SHADER_MODULE (alias_*)` block is still there — so each of our edits still *has* a home, but they must be re-applied by hand. |
| `Quake/sys_sdl.c` | rewrite `Sys_FileOpenRead` to slurp the file into RAM and `fclose` immediately (NFS perf **and** the demo-playback fix: libphoenix cannot serve two concurrent streams on one file), `owns_memory` free-on-close, bounded memory read | rewritten (+286): `SDL_Mutex`-protected handle table, per-worker handle array, `file_path`, `eof_condition`; `81e740c` *Sys_FileRead: fix EOF trigger criteria for memory-based file*, `d91558b` *cleaner EOF management vs duplicated handles*, `f89839d` free the slot when open fails | **Hard conflict, partly superseded.** Upstream now owns the same area and its EOF work overlaps our `avail` clamp; `d91558b`'s "duplicated handles" framing is the same bug class we hit. Re-apply only the slurp + `owns_memory` on top of the new `allocHandle`/`freeHandle`, and drop whatever upstream now does correctly. |
| `Quake/host_cmd.c` | skip the auto-demo world-load in `Host_Startdemos_f` (old line 2637) so `Host_Init` completes before any world render | `@@ -2637` — upstream modifies **the same line range** (plus ~360 lines of new `Host_Maps_f`/filelist/give work) | **Hard conflict** (a one-hunk one, and our hunk is bring-up scaffolding flagged `TODO(vkquake-port)`; consider whether it is still needed at all). |
| `Quake/glquake.h` | four prototypes for the de-static'd helpers | +204 (MBOIT, new declarations) | Soft conflict; re-add the block. |
| `Quake/gl_screen.c` | drop `GL_SetCanvas`/`R_BindPipeline` from `SCR_DrawGUI` (the fb0 vid shim owns them); edge-triggered `SCR_UpdateScreen` gate trace | +394 (draw_pic/xbr pipelines, GUI changes) | Soft conflict; both of our hunks are in functions upstream also edited, so re-verify semantics, not just text. |
| `Quake/gl_texmgr.c` | `#29` fix: re-derive each copy region's `imageExtent` from the `glt` dims (degenerate `vkCmdCopyBufferToImage` extent) | +114, hunks at old 286/297/429/448/701/716/1486/1609 | **Orthogonal** — our hunk is at old line 1229 (`TexMgr_LoadImage32`), inside an untouched gap. Still confirm the underlying degenerate-extent bug is not fixed upstream, and drop ours if it is. |
| `Quake/sv_main.c` | guard `SV_LocalSound` against a NULL client (start-up NULL-deref) | +256, hunks at old 744…3322 but none at 1381 | **Orthogonal.** Worth pushing upstream — it is a genuine engine bug, not a Phoenix quirk. |
| `Quake/r_alias.c` | set UBO flag `0x10` for opaque draws | +377/−… (MD5 skinning/animation rework, `0edfa71`) | **Semantically clear, textually conflicting.** Verified upstream still only uses flags `0x1`/`0x2`/`0x4`, so bit `0x10` is still free. Re-apply near the new flag block. |
| `Shaders/alias_common.inc` | `if ((ubo.flags & 0x10u) != 0u) result.a = 1.0;` | **untouched** | **No conflict.** |

**Beyond the diff, a vkQuake sync is a re-port**, because the port does not use upstream's build:
`sources/phoenix-rtos-ports/vkquake/port.def.sh` hand-transcribes the TU list from
`external/vkquake/meson.build`'s `srcs`, and vendors `glue/vkquake_shaders.c` (the embedded SPIR-V
`*_spv` arrays) plus `glue/vk_trampolines.c`. Syncing therefore also means:

1. Re-transcribing the TU list: at minimum `json.c`, `steam.c`, `in_sdl2.c`, `mimalloc/theap.c`,
   `mimalloc/threadlocal.c` are new; `in_sdl.c` is gone.
2. **Regenerating all embedded SPIR-V** for a substantially new shader set (mboit, draw_pic/xbr,
   consolidated `screen_effects.comp`, `skinning.inc`). Upstream's `meson.build` now errors unless
   `spirv-opt` supports `--canonicalize-ids` and requires Vulkan SDK ≥ 1.4.341 — a **build-host**
   requirement only. Checked: `application_info.apiVersion` upstream is still
   `VK_MAKE_VERSION(1,1,0)` (or 1.0), so this does **not** raise the runtime API level demanded of
   V3DV. The vkQuake sync is *hard*, not *blocked*.
3. Absorbing **mimalloc 3.4.5** — the allocator, on Phoenix, is exactly where our `>4 KiB` shader
   module malloc fault lived. `ebe02b5` (mimalloc POPCNT misdetection) and `1d037e2` (drop a
   gcc-diagnostics hack) show it is churning.
4. Deciding on **SDL3**. `e1abec5` *SDL3 by default (#933)* is the risk; `meson.build` still has an
   SDL2 fallback path (`use_sdl3` disabled → `dependency('sdl2')`), and our port links its own
   `glue/pl_phoenix_sdlcompat.c` rather than libSDL2, so this is survivable — but the input/audio
   TU split has to be re-decided.

**Also valuable / notable upstream:** `92b0185` *Do not crash on missing Q_PIC*, `51ae00c` *Fix
portable user file locations*, `9819437` *port Ironwail's `Sys_fopen`, unify content roots*,
`0edfa71` *Fix MD5 animation compatibility and skinning*. **Risky:** `b1b9c2f` (MBOIT renderer),
`e1abec5` (SDL3), `1b43315` (mimalloc 3.4.5), `414ee7c` (Vulkan SDK ≥ 1.4.341), and the deletion of
`Shaders/compile.sh`/`compile.bat`.

---

## 6. Recommended sync order

1. **quake3e** — zero-file intersection and 11 commits; the merge should be mechanical, and it
   carries real cross-arch collision-precision fixes. Read `d0b2021e` against our RWX
   `vm_aarch64.c` workaround before merging.
2. **quakespasm** — 12 commits, one intersecting file whose hunks provably do not overlap ours;
   most of the churn is vendored SDL headers we don't build.
3. **yquake2** — 85 commits and three conflicts (two trivial, one orthogonal), and by far the best
   payoff: tile-GPU framebuffer invalidation, GL3 draw-call batching, GLES3 `highp`, scrap textures.
   Budget a real HDMI verification run because the gl3 renderer is substantially rewritten. Start by
   moving HEAD off detached onto a named branch.
4. **vkquake** — last, and as its own scheduled step, not a merge: 216 commits, 8 of our 9 files
   conflict (3 hard), plus a TU-list re-transcription, a full SPIR-V regeneration needing a newer
   Vulkan SDK on the build host, a mimalloc major bump, and the SDL3-by-default decision.

**Per-sync checklist:** merge (never rebase) → resolve → bump `commit`/`size`/`sha256` in
`port.def.sh` → `game-port-patch.sh --regen` → `--check` clean → rebuild → HDMI-verify the game →
review the glue-replaced TUs for upstream fixes that will never reach us → push the fork branch to
`publish` → record the integration state.

**Cheap win available before any of this:** cherry-pick yquake2 `9b891e59`
(*GLES3: Use glInvalidateFramebuffer()*, 4 files, +56/−21, `YQ2_GL3_GLES`-guarded) onto the yquake2
fork on its own. It is the highest value-to-risk commit found in this analysis.
