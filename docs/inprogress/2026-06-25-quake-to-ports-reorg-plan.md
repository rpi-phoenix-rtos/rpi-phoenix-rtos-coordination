# Quake / vkQuake / V3D-demo reorg out of phoenix-rtos-devices (#12, Phase C)

**Goal:** move the capstone *applications* (Quake, vkQuake) and the GPU bring-up
*diagnostics* (v3d-scout/stalltest/tier0 + gl* rungs) **out of**
`phoenix-rtos-devices/misc/` — where they sit as if they were device drivers — into a
proper applications home, leaving only real drivers in devices. This is the user's
explicit "Quake should not be in rtos-devices but in ports" ask.

This plan is **execution-ready and gated**: each step is independently buildable and the
flagship (`rpi4-quake` bundled + boot-launched) is **never** left un-bundled mid-step.

> Status: PLAN ONLY (no edits/builds/boot performed). Read-only investigation done
> 2026-06-25.

---

## 0. The load-bearing constraint (read this first)

Quake/vkQuake are **~19 MB binaries bundled directly into `loader.disk`** and
boot-launched, because the read-based NFS/SD process loader cannot `exec` a binary that
large. This dictates the entire reorg.

**How a program reaches `loader.disk`:** `image_builder.py` resolves each
`app -x <name>` line in `user.plo.yaml` by looking up
**`PREFIX_PROG_STRIPPED/<name>`** (verified: `image_builder.py` `PloCmdApp`, and
`_targets/build.common` `b_mkscript_user` aliases `$name` at `${PREFIX_PROG_STRIPPED}/$path`).
Every component built through **`binary.mk`** (the shared
`$(MAKES_PATH)/binary.mk`, whose strip rule emits `$(PREFIX_PROG_STRIPPED)%`) lands in
that one flat directory, **regardless of which repo's Makefile drove it**. So a program
built from `phoenix-rtos-devices/misc/X` and one built from
`phoenix-rtos-project/_user/X` are *byte-for-byte interchangeable* from `loader.disk`'s
point of view — `app -x rpi4-quake ...` keeps working unchanged after the move. **This
is what makes the reorg safe.**

**Why NOT the literal `phoenix-rtos-ports` repo.** The ports project is a system of
**upstream-tarball recipes**: each port is a `port.def.sh` with `source=` / `sha256` /
`archive_filename` + `p_prepare`/`p_build` hooks that fetch and build a *released
upstream package* (busybox 1.27.2, openssl, etc.), staged via `ports.yaml`. (A port
*can* drop a program into `PREFIX_PROG` — busybox does `cp -va busybox_unstripped
"$PREFIX_PROG"` — so "ports only reach the rootfs" is **not** the disqualifier.) The real
disqualifier is the **recipe model**: Quake is not an upstream tarball. It is a bespoke
coordination-repo link (`tools/quakespasm-port/build-quakespasm-phoenix.py`) against an
`external/` git clone + prebuilt GPU archives, producing a static `.a` that a tiny stub
Makefile links. That shape does not fit `port.def.sh` at all.

**Best-fit home = `phoenix-rtos-project/_user/`.** This is Phoenix's canonical in-tree
*applications* location (it already hosts `hello`, `voxeldemo`, `rotrectangle`,
`serverdemo`). Its `Makefile` builds every subdir via `binary.mk` exactly like
devices/misc, into the same `PREFIX_PROG_STRIPPED`. **Every other Phoenix target already
wires `make -C "_user" all install`** in its `b_build_*` (ia32, imx6ull, imxrt10x/11x,
mcxn94x, gr716, zynqmp, mps3an536, gr765, …) — the aarch64a72/generic target is the
**only one that does not**. Wiring it is small and fully precedented.

> **USER-DECISION FLAG (deviation from the literal ask).** The user said "ports." The
> only mechanism that can (a) host a bespoke libphoenix-linked binary and (b) bundle a
> 19 MB program into `loader.disk` is `phoenix-rtos-project/_user/`, **not**
> `phoenix-rtos-ports`. This plan uses `_user/` and treats it as "the applications
> project" (the spirit of the ask). If the user insists on the literal ports repo, the
> only honest path is a `port.def.sh` whose "source" is the local `external/quakespasm`
> clone — a non-standard, fragile abuse of the recipe model that *still* could not solve
> the loader.disk bundling. Recommend `_user/`; surface this for veto before executing.

---

## 1. What is in `devices/misc/` today — classification

| Component | What it is | Links | Bundled? | Disposition |
|---|---|---|---|---|
| `rpi4-quake` | **Application** (GLQuake capstone, flagship) | `/tmp/lib{quakespasm,GL,v3d}-phoenix.a` | **YES** (`DEFAULT_COMPONENTS`); launch currently commented (see §6) | → `_user/rpi4-quake` |
| `rpi4-vkquake` | **Application** (Vulkan QuakeSpasm) | `/tmp/lib{vkquake,v3dv,v3d}-phoenix.a` | Default-off (swap target); stub `.c` **untracked** | → `_user/rpi4-vkquake` (+ `git add` the stub) |
| `rpi4-v3d-scout` | Bring-up **diagnostic** (V3D MMIO identity dump) | none (raw MMIO) | **Built** in `DEFAULT_COMPONENTS`; launch commented | **DROP** (recommended) |
| `rpi4-v3d-stalltest` | Bring-up **diagnostic** (render-stall repro) | `/tmp/lib{GL,v3d}-phoenix.a` | Not bundled (commented) | **DROP** (recommended) |
| `rpi4-v3dv-tier0` | Bring-up **diagnostic** (Vulkan tier-1 boot) | `/tmp/lib{v3dv,v3d}-phoenix.a` | Not bundled (commented) | **DROP** (recommended) |
| `rpi4-v3d-mesa` | Bring-up **diagnostic** (`v3d_screen_create`) | `/tmp/libv3d-phoenix.a` | Not bundled (commented) | **DROP** (recommended) |
| `rpi4-glclear` | GL **demo** rung (green clear) | `/tmp/lib{GL,v3d}-phoenix.a` | Not bundled (commented) | DROP, or → `_user/` demo (choice, §7) |
| `rpi4-glcube` | GL **demo** rung (spinning cube) | `/tmp/lib{GL,v3d}-phoenix.a` | Not bundled (commented) | DROP, or → `_user/` demo (choice, §7) |
| `rpi4-gldraw` | GL **demo** rung (textured triangle); **untracked** WIP per Phase A | `/tmp/lib{GL,v3d}-phoenix.a` | Not bundled (commented) | DROP, or → `_user/` demo (choice, §7) |

**Stays in `devices/misc/` (these ARE drivers / device servers — do NOT move):**
`rpi4-vcmbox`, `rpi4-thermal`, `rpi4-hwrng`, `rpi4-fb`, `rpi4-gpio`, `rpi4-audio`,
`rpi4-sysinfo` (boot banner), `rpi4-klogd` (klog daemon), `rpi4-ipcprobe` (API probe).
The V3D **power-on** lives in the winsys (`tools/v3d-driver-port/v3d_phoenix_power.c`),
not in devices — nothing GPU-driver-side needs to move.

### Diagnostics disposition — recommend DROP, not relocate

The scout/stalltest/tier0/mesa hypotheses are **resolved** (V3D powers on; GL renders on
HDMI; Vulkan tier-4b renders a real pipeline). Per CLAUDE.md ("Remove diagnostic-only
code whose hypothesis was disproved") and the upstreamability goal, **deleting** them is
cleaner than relocating cruft into a new dev/test dir. They remain fully recoverable from
git history + the integration manifests. The gl* rungs (glclear/cube/gldraw) are the only
showcase-worthy diagnostics; if the showcase (Phase E) wants a small live GL demo, keep
**one** (recommend `rpi4-glcube`) under `_user/` and drop the rest. Present as an explicit
choice (§7); default recommendation = drop all six diagnostics, keep nothing in the GL
demo slot until Phase E decides.

---

## 2. Target architecture (after the reorg)

```
phoenix-rtos-project/_user/
    rpi4-quake/        Makefile + rpi4-quake-stub.c       (GL flagship, bundled)
    rpi4-vkquake/      Makefile + rpi4-vkquake-stub.c     (Vulkan, default-off swap)
    [rpi4-glcube/]     optional, only if Phase E wants a GL demo

phoenix-rtos-devices/misc/
    rpi4-vcmbox, rpi4-thermal, rpi4-hwrng, rpi4-fb,
    rpi4-gpio, rpi4-audio, rpi4-sysinfo, rpi4-klogd, rpi4-ipcprobe   (drivers — unchanged)
    (v3d-scout/stalltest/tier0/mesa/glclear/glcube/gldraw DELETED)

tools/.gpu-libs/   <-- NEW stable home for the prebuilt engine archives
    libquakespasm.a libGL-phoenix.a libv3d-phoenix.a
    libvkquake.a    libv3dv-phoenix.a
    (was /tmp/lib*.a — see §5)
```

Build wiring after the move:
- `_targets/aarch64a72/generic` or `_projects/aarch64a72-generic-rpi4b/build.project`
  gains `make -C "_user" all install` (so `_user` programs land in
  `PREFIX_PROG_STRIPPED`).
- `devices/_targets/Makefile.aarch64a72-generic` **loses** the `rpi4-quake` /
  `rpi4-vkquake` / `rpi4-v3d-scout` `DEFAULT_COMPONENTS` lines (and the commented demo
  lines are removed with the deleted components).
- `user.plo.yaml` launch lines for `rpi4-quake` / `rpi4-vkquake` are **unchanged in
  text** (`app -x rpi4-quake ...` resolves by basename regardless of source repo) — only
  their explanatory comments are updated to point at the new home.

---

## 3. Cross-repo git mechanics (NOT a `git mv`)

`phoenix-rtos-devices` and `phoenix-rtos-project` are **separate sibling repos** (not
submodules). Each move is therefore:
- `git -C <sources>/phoenix-rtos-devices rm misc/<X>/...` + a devices commit, **and**
- create the files under `phoenix-rtos-project/_user/<X>/` + `git -C
  <sources>/phoenix-rtos-project add ...` + a project commit.

Two commits, two repos. Use `git -C <abs-path>` per CLAUDE.md (runs without a prompt;
`add` and `commit` as **separate** calls, no `&&`). `rpi4-vkquake-stub.c` is currently
**untracked** in devices → it is simply created at the new `_user/rpi4-vkquake/` home and
`git add`ed there (nothing to `git rm`).

**Snapshot + rollback span both repos.** After each validated step, record state with
`scripts/snapshot-integration-state.sh` (captures all sibling SHAs into a
`manifests/*.md`). Rollback = `scripts/restore-integration-state.sh <manifest.md>`, never
ad-hoc per-repo `git checkout` (the move is not atomic across repos, so only a manifest
restore is deterministic).

---

## 4. The prebuilt-engine-libs fragility (`/tmp/lib*.a`)

`rpi4-quake` links `/tmp/lib{quakespasm,GL,v3d}-phoenix.a`; `rpi4-vkquake` links
`/tmp/lib{vkquake,v3dv,v3d}-phoenix.a`. These are produced by the coord-repo tools
(`tools/quakespasm-port/build-quakespasm-phoenix.py`,
`tools/vkquake-port/build-vkquake-phoenix.py`, `tools/v3d-driver-port/build-*.py`) and
written to `/tmp`. They get **cleared mid-session** (host `/tmp` reaping), which caused
the two flagship-rebuild failures noted in the cleanup plan.

**Why `/tmp` was used (and why a relative path will NOT work):** the Phoenix build runs
inside `.buildroot/`, a **copy** of the sibling repos made by
`scripts/prepare-buildroot.sh --copy-components`. A path relative to the moved component
would resolve inside that throwaway copy and could not reach the coord-repo tools'
output. `/tmp` was chosen precisely because it is a host-absolute path that survives the
copy. The Makefiles already use the same trick for headers:
`V3D_COORD := /home/houp/phoenix-rpi`.

**Fix = stable absolute home + a regenerate step in the wrapper (NOT in the make):**
1. New dir `tools/.gpu-libs/` in the coordination repo. Point the build scripts'
   output + the component Makefiles at `$(V3D_COORD)/tools/.gpu-libs/lib*.a` instead of
   `/tmp/lib*.a`. (`tools/.gpu-libs/` is gitignored — these are large generated
   artifacts, not source.)
2. In `scripts/rebuild-rpi4b-fast.sh` (which runs in coord context, before the build
   shell, and can invoke the python scripts), add an **"ensure libs exist; rebuild if
   missing or stale"** pre-step that runs the relevant `tools/*/build-*.py` when
   `tools/.gpu-libs/lib*.a` is absent/older than its inputs. This removes the "stale or
   reaped lib" class of failure and makes `--scope core` self-sufficient.
3. In each component Makefile, keep a **loud existence check** (e.g. `$(if $(wildcard
   $(QS_LIB)),,$(error rpi4-quake: $(QS_LIB) missing — run tools/quakespasm-port build))`)
   so a missing lib fails fast with a clear message instead of an obscure link error.

> **FLAG (optional follow-up, not part of the gated move):** fully auto-regenerating the
> libs *from inside the in-buildroot make* (a real Make dependency edge) is a larger
> change — the in-buildroot make would have to shell back out to the coord tools, which
> breaks the buildroot-copy isolation. Do **not** attempt it in this reorg; the
> wrapper-side ensure-step (item 2) is the right scope here.

---

## 5. Execution order — small, gated steps (flagship never broken)

Each step ends with a **gate**: `--scope core` rebuild + verify the flagship is still
**built into `PREFIX_PROG_STRIPPED`**, the artifact the move actually relocates:

```
ls .buildroot/_build/aarch64a72-generic-rpi4b/prog.stripped/rpi4-quake
```

> **Why NOT `loader.disk | grep rpi4-quake` as the gate.** Verified empirically
> (2026-06-25): in the current tree that grep returns **0 matches**, because
> `image_builder.py` bundles a program into `loader.disk` only when an **active (non-
> commented) `app -x` line** in `user.plo.yaml` references it — and the `rpi4-quake`
> launch is currently commented ("TEMP-NO-QUAKE-AUTOSTART"). `DEFAULT_COMPONENTS`
> controls what is *built into `prog.stripped`*, NOT what lands in `loader.disk`. So the
> loader.disk grep is empty *before* any move and cannot detect a regression. The move
> changes **where `rpi4-quake` is built**, i.e. its presence in `prog.stripped/` — that
> is the invariant the gate must check. (`rpi4-quake` IS present in `prog.stripped/`
> today; confirmed.)
>
> Keep `loader.disk | grep rpi4-quake` only as an **end-to-end** confirmation, and run it
> **only after the flagship launch line is restored** (uncomment `app -x rpi4-quake` +
> revert the vkQuake debug swap — see Precondition 0). Until then it is expected to be
> empty and is not a live gate.

Do not proceed past a red gate.

> **PRECONDITION 0 (reconcile with in-flight work).** Two live disablements sit in the
> tree and must be reconciled before the end-to-end (loader.disk) confirmation is
> meaningful:
> 1. The user's **vkQuake HW-test swap** (`devices/_targets/Makefile.aarch64a72-generic`
>    + `user.plo.yaml` dirty — Phase A item) staged for the evening manual test. This
>    reorg must **not stomp** it.
> 2. **The `rpi4-quake` launch line itself is commented** ("TEMP-NO-QUAKE-AUTOSTART"
>    debug state), so the flagship is built but not bundled/launched. The per-step gate
>    (`prog.stripped/rpi4-quake`) does not need it restored; the **end-to-end** check
>    (loader.disk + boot) does.
>
> Before starting: confirm with the user whether the evening vkQuake test has run / the
> swap can be reverted to the flagship config, and whether the `rpi4-quake` autostart
> should be restored. Execute the reorg from the flagship baseline; reapply any
> vkQuake-swap intent as the clean per-app toggle the reorg produces. **Do not run this
> reorg concurrently with any other subagent editing the devices Makefile / plo /
> build-core** (whole-tree-touching, must run solo).

### Step C0 — stable lib home (prep, no component move yet)
1. Create `tools/.gpu-libs/`; gitignore it (coord repo).
2. Repoint `tools/*/build-*.py` outputs + the `/tmp/lib*.a` references in **all**
   GL/VK component Makefiles (rpi4-quake, rpi4-vkquake, and any gl*/v3d* kept) to
   `$(V3D_COORD)/tools/.gpu-libs/`.
3. Add the ensure-libs pre-step to `rebuild-rpi4b-fast.sh` (§4 item 2).
4. **Gate:** `--scope core` clean + `prog.stripped/rpi4-quake` present (Quake still links
   + builds, now from the new lib home). Snapshot.
- *Risk: LOW* (paths only). *Rollback: manifest restore + revert coord script.*

### Step C1 — Quake → `_user` (the flagship; do first)
1. Wire `_user` into the rpi4b build: add `make -C "_user" all install` to the rpi4b
   target's `b_build_project` (in `_projects/aarch64a72-generic-rpi4b/build.project`,
   alongside the existing DTB staging — mirrors the ia32 `b_build_project` pattern).
2. Create `phoenix-rtos-project/_user/rpi4-quake/` = copy of the devices Makefile +
   `rpi4-quake-stub.c` (Makefile body unchanged except the §C0 lib paths). `git -C
   project add`.
3. **Gate A (additive — Quake now built at BOTH homes):** `--scope core` clean +
   `prog.stripped/rpi4-quake` present, AND the `_user` build itself succeeds (it now
   produces `PREFIX_PROG_STRIPPED/rpi4-quake`; mechanism confirmed: `_user/Makefile` →
   `binary.mk` → `PREFIX_PROG_STRIPPED`, identical to devices/misc). Watch for the
   first-time `_user` build failure on this arch (§6) — build `_user` standalone once
   here to isolate it from the Quake move.
4. Only now remove `rpi4-quake` from devices: `git -C devices rm
   misc/rpi4-quake/Makefile misc/rpi4-quake/rpi4-quake-stub.c`; delete the
   `DEFAULT_COMPONENTS += rpi4-quake` line + its comment in
   `_targets/Makefile.aarch64a72-generic`.
5. Update `user.plo.yaml` comments only (launch text unchanged).
6. **Gate B:** `--scope core` clean + `prog.stripped/rpi4-quake` still present (now
   sourced solely from `_user`). Two sibling commits (devices rm; project add).
   Snapshot → manifest.
- *Risk: MEDIUM* (touches devices core Makefile + project build wiring + plo). This is
  **the riskiest step**: if `_user` isn't actually built, Quake silently un-bundles.
  Gate A (build at both homes *before* removing from devices) is the guard. *Rollback:
  manifest restore.*

### Step C2 — vkQuake → `_user`
1. Create `phoenix-rtos-project/_user/rpi4-vkquake/` = the Makefile (§C0 paths) +
   `rpi4-vkquake-stub.c` (commit the previously-untracked stub here). `git -C project
   add`.
2. `git -C devices rm misc/rpi4-vkquake/Makefile` (the untracked stub needs no `rm`);
   remove the (commented) `DEFAULT_COMPONENTS += rpi4-vkquake` line + comment block.
3. Update the vkQuake swap comments in `user.plo.yaml` + the new `_user` Makefile to
   reference the new home.
4. **Gate:** `--scope core` clean + `prog.stripped/rpi4-quake` still present (vkQuake is
   default-off, so the flagship is unchanged; this gate confirms no regression).
   Optionally build the vkQuake swap (uncomment) once to confirm `prog.stripped/rpi4-vkquake`
   links from `_user`, then revert to flagship. Two sibling commits. Snapshot.
- *Risk: LOW–MEDIUM* (default-off; the swap mechanism is the only live edge). *Rollback:
  manifest restore.*

### Step C3 — drop the resolved diagnostics
1. `git -C devices rm` the six diagnostic components:
   `misc/rpi4-v3d-scout/` (incl. `v3d_gen.h`, `v3d_packet_v42_pack.h`),
   `misc/rpi4-v3d-stalltest/`, `misc/rpi4-v3dv-tier0/`, `misc/rpi4-v3d-mesa/`,
   `misc/rpi4-glclear/`, `misc/rpi4-glcube/`, and the **untracked** `misc/rpi4-gldraw/`
   (delete the untracked files directly).
2. In `_targets/Makefile.aarch64a72-generic`: remove the **active**
   `DEFAULT_COMPONENTS += rpi4-v3d-scout` line **and** all the commented demo/diag
   `DEFAULT_COMPONENTS` blocks (rpi4-v3d-mesa, glclear, gldraw, glcube, v3d-stalltest,
   v3dv-tier0). In `user.plo.yaml`: remove the matching commented launch blocks.
3. **Gate:** `--scope core` clean. Note: removing `rpi4-v3d-scout` removes it from
   `prog.stripped/` (it was actually built, only its launch was commented) — this is the
   one diagnostic whose removal changes what is built; scout was never launched, so the
   boot is unaffected. `prog.stripped/rpi4-quake` still present. One devices commit.
   Snapshot.
- *Risk: LOW* (dead/diagnostic removal). The scout image-content change is the only
  non-incidental effect — flag it explicitly in the commit message. *Rollback: git
  history / manifest restore.*
- **If Phase E wants a GL demo:** before C3, copy `rpi4-glcube` to `_user/rpi4-glcube/`
  (default-off, not in any `DEFAULT_COMPONENTS`), then drop the devices copy in C3 with
  the rest. Otherwise drop it too.

### Step C4 — tidy + record
1. Reconcile the Phase A working-tree items this reorg touches: the vkQuake-swap
   revert (now expressed as the clean `_user` per-app toggle), the now-committed
   vkQuake stub, the dropped untracked `rpi4-gldraw`.
2. Update `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` and the cleanup plan's
   Phase C section to mark the moves done; note `tools/.gpu-libs/` as the lib home.
3. Final `--scope core` + boot-bundle gate + a snapshot manifest covering all siblings.

---

## 6. Subtleties & gotchas to carry into execution

- **plo dup-program brick:** a program name may appear only **once** as an `app -x`
  alias in a rendered boot script. The reorg does **not** rename `rpi4-quake` /
  `rpi4-vkquake`, so no alias collision — but verify the rendered `user.plo` after any
  plo edit (per the "plo: no duplicate program" memory).
- **`app -x` text is unchanged:** because resolution is by basename out of
  `PREFIX_PROG_STRIPPED`, moving the source repo requires **no** change to the launch
  lines themselves — only comments. Do not "helpfully" rewrite the launch strings.
- **Stale-core hazard:** the moves touch core (devices) + project. Always rebuild with
  **`--scope core`** (not `auto`) after each committed move and verify with the
  `loader.disk | grep` gate — an `auto` rebuild can ship a stale image lacking the change
  (CLAUDE.md).
- **The Quake launch is currently commented** in `user.plo.yaml`
  ("TEMP-NO-QUAKE-AUTOSTART" debugging), so `rpi4-quake` is **built** (in
  `prog.stripped/`) but **NOT in `loader.disk`** (verified: `loader.disk | grep
  rpi4-quake` = 0 matches today). The per-step gate therefore checks
  `prog.stripped/rpi4-quake`, not loader.disk presence. Do not conflate the two.
- **`_user` builds for aarch64a72 for the FIRST time at C1.** `_user/Makefile` does
  `DEFAULT_COMPONENTS := $(ALL_COMPONENTS)`, so `make -C "_user" all install` compiles
  the stock demos (`hello`, `hellocpp`, `rotrectangle`, `serverdemo`, `voxeldemo`) for
  this target, which has **never** built `_user` before. If any of them fails to compile
  for aarch64a72, **the project stage fails at C1** — a failure that is *not* Quake's
  fault and could be mistaken for a bad move. Likely-failure escape: fix the offending
  demo, or temporarily scope the `_user` build to the rpi4 apps (e.g. a target-specific
  `DEFAULT_COMPONENTS` or `make -C _user rpi4-quake install`). Build `_user` standalone
  once at C1 to surface this before relying on it. Name collisions: none (the stock demo
  names are distinct from any bundled program).
- **Toolchain/flags:** the moved Makefiles keep their exact `LOCAL_LDLIBS`
  (start-group / whole-archive / `-z stack-size=33554432` / `--build-id` /
  `--allow-multiple-definition`). These are load-bearing (documented in the Makefile
  comments) — copy verbatim, change only the lib **paths** (§C0).

---

## 7. Explicit choices for the user / orchestrator

1. **Home = `phoenix-rtos-project/_user/`** (not literal `phoenix-rtos-ports`). Spirit of
   the ask; the only mechanism that bundles 19 MB into loader.disk. **Veto point.**
2. **Diagnostics: DROP all six** (scout/stalltest/tier0/mesa + glclear/gldraw) by default
   — hypotheses resolved, recoverable from git. Alternative: relocate to a `_user/`
   dev-demo group behind default-off. **Recommend drop.**
3. **GL demo for the showcase:** keep **one** rung (recommend `rpi4-glcube`) under
   `_user/` for Phase E, or none until Phase E decides. **Recommend none now; revisit in
   Phase E.**

---

## 8. Files touched (summary)

**phoenix-rtos-devices** (git rm + Makefile edits):
- `misc/rpi4-quake/`, `misc/rpi4-vkquake/Makefile` — removed (moved)
- `misc/rpi4-v3d-scout/`, `misc/rpi4-v3d-stalltest/`, `misc/rpi4-v3dv-tier0/`,
  `misc/rpi4-v3d-mesa/`, `misc/rpi4-glclear/`, `misc/rpi4-glcube/` — removed (dropped)
- `misc/rpi4-gldraw/` — untracked, deleted
- `_targets/Makefile.aarch64a72-generic` — drop the rpi4-quake / rpi4-vkquake /
  rpi4-v3d-scout `DEFAULT_COMPONENTS` lines + the commented demo/diag blocks

**phoenix-rtos-project** (add + build wiring):
- `_user/rpi4-quake/{Makefile,rpi4-quake-stub.c}` — new
- `_user/rpi4-vkquake/{Makefile,rpi4-vkquake-stub.c}` — new (stub committed)
- `_projects/aarch64a72-generic-rpi4b/build.project` — add `make -C "_user" all install`
  to `b_build_project`
- `_projects/aarch64a72-generic-rpi4b/user.plo.yaml` — comment-only updates

**coordination repo:**
- `tools/.gpu-libs/` — new lib home (gitignored)
- `tools/{quakespasm,vkquake,v3d-driver}-port/build-*.py` — output path → `tools/.gpu-libs/`
- `scripts/rebuild-rpi4b-fast.sh` — ensure-libs pre-step
- `.gitignore` — add `tools/.gpu-libs/`
- `manifests/*.md` — one snapshot per validated step
- this plan + `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` / cleanup plan
  Phase C update

---

## 9. Riskiest steps & rollback (recap)

- **Riskiest: C1** (Quake → `_user`) — losing the flagship if `_user` isn't actually
  built (or fails to build for this arch — first `_user` build on aarch64a72, §6). Guard:
  **Gate A builds at both homes before removing from devices**; only remove after
  `prog.stripped/rpi4-quake` is confirmed present from the `_user` build.
- **C0** lib-path repoint — a typo'd path silently breaks the link; the Makefile
  existence-check (§4 item 3) turns that into a fast, clear error.
- **Rollback for every step:** `scripts/restore-integration-state.sh <manifest.md>` (spans
  both siblings); never ad-hoc cross-repo checkout.
