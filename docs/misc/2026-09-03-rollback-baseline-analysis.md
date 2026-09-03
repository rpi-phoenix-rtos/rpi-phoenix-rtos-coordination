# Rollback baseline: isolated workspace, verification, and commit-risk analysis

Date: 2026-09-03
Author: agent (subagent of the interactive session; parent owns the UART/HW)
Workspace: `/home/houp/phoenix-rpi-rollback`
Requested baseline: `manifests/2026-08-28-gcc16-promoted-default.md`

This document is written into the live repo; **nothing else in the live tree was
modified.** `sources/**`, `.buildroot`, `tools/.gpu-libs`, `.toolchain` and
`/srv/phoenix-rpi4-nfs*` were left untouched (see "Isolation" for how that is
enforced, not merely intended).

---

## 0. TL;DR — the three things worth acting on

1. **The manifest is not the known-good showcase state.** It records coord
   (`_build`) at `9e53c97` — but the manifest *file itself* was only committed in
   the very next coord commit `659cb631f`, and the "all 5 games + X desktop"
   demo was verified 35 coord commits later, at **`15dc40787`
   ("docs: mark Sunday demo showcase COMPLETE (all 5 games + X desktop, fresh
   build)", 2026-08-28 02:21)**. Five of the commits in that gap are the fixes
   that *made* the demo work — including `f1854f2c7` "x11-port: build with
   `-std=gnu17` (fix libXt under gcc-16/C23)", committed **21 minutes after**
   `9e53c97`. A literal manifest rollback therefore rolls the coord repo back
   *behind* the X11 fixes and cannot reproduce the showcase.
   → The workspace uses **sibling SHAs = manifest exactly, coord = `15dc40787`.**
2. **Rolling back is not uniformly a win.** Two post-baseline changes are real
   fixes that the baseline lacks: the V3D CSD spin budget (8M → 80M, needed by
   V3DV's large compute dispatches) and the `gl_stubs.c` `strtok_r` removal
   (which is *coupled* to libphoenix `7a1cd73`). See §5 for the bisect ordering
   constraint this creates — **you cannot mix baseline GPU glue with post-09-01
   libphoenix.**
3. **The "known-good demo" never existed as a single reproducible build.** At
   the demo-verified coord tree, `tools/` has an *engine* build recipe for only
   **two** of the five games:

   | `tools/<port>/` | contents at `15dc40787` | so the demo binary came from |
   | --- | --- | --- |
   | `quakespasm-port` | `build-quakespasm{,-sdl,-det}-phoenix.py` + patch | the image build (`_user/rpi4-quake`, inside `loader.disk`) |
   | `vkquake-port` | `build-vkquake-phoenix.py` + shaders/trampolines + patch | the image build (`_user/rpi4-vkquake`) |
   | `yquake2-port` | `quake2-launcher.c`, `README` — **no engine recipe** | `scripts/build-port.sh yquake2` run by hand (`if: false`), **hand-copied to the NFS export** |
   | `quake3-port` | `quake3-launcher.c`, `demos/`, `README` — **no engine recipe** | ditto |
   | `supertuxkart-port` | `stk-launcher.c`, `savedir-seed` — **no engine recipe** | ditto; STK was not even *registered* in `ports.yaml` |

   The demo therefore = one built image **plus** three hand-built binaries
   **plus** hand-staged game data (the baseline rpi4b `rootfs-overlay` holds
   exactly one file, `etc/rc.psh`) **plus** hand-staged fonts, all layered onto
   the NFS export. This is precisely the practice the owner objected to, and it
   is why "roll back and it works again" cannot be literally true for Q2/Q3/STK:
   there is no baseline build that produces them. The rollback build below
   certifies GLQuake + vkQuake + the X desktop; the other three have to be
   compared as *ports*, not as image outputs.
4. **The baseline was already not reproducible from a clean tree** in two places
   we can name from its own later fix commits: the glamor X server
   (`d0b151111`: libglamor.a "had always passed before because an old ad-hoc
   `--enable-glamor` reconfigure had left the archive lying in
   `tools/x11-port/src/`") and the `.toolchain` libphoenix bundle (§2, hazard H2).
   Part of what "broke over the last few days" is the clean rebuild *exposing*
   pre-existing hidden state, not new regressions.

---

## 1. The workspace

```
/home/houp/phoenix-rpi-rollback
├── (coord repo)                     git worktree of /home/houp/phoenix-rpi @ 15dc40787
├── sources/<16 repos>               git worktrees of each sibling @ manifest SHA
│   └── phoenix-rtos-lwip/lib-lwip   git worktree of the lwIP submodule @ 58e89121a
├── external/{mesa,quakespasm,vkquake,quake3e,yquake2}
│                                    git worktrees @ last pre-baseline commit
├── external/{linux,u-boot,barebox,rpi-eeprom,ffmpeg,bsd-genet,quakespasm-det}
│                                    SYMLINKS to the live trees (research-only)
├── .toolchain                       PRIVATE COPY (292 MB) — see hazard H2
├── .venv .firmware .bootblobs       SYMLINKS (read-only inputs)
├── .buildroot                       the workspace's OWN buildroot
├── .tmp                             bind-mounted over /tmp during the build
├── .hostcache/phoenix-distfiles     private copy of ~/.phoenix-distfiles (87 MB)
├── .decoy-srv                       bind-mounted over /srv during the build
└── .rollback-run/                   runner script + build logs
```

All git worktrees, no re-clones. Total ≈ 1.5 GB + buildroot.

### Isolation (enforced, not assumed)

The baseline `tools/{ports,x11-port}/*.sh` scripts hardcode roughly **150
distinct `/tmp/...` prefixes** (`/tmp/mesa-v3d-build`, `/tmp/x11-phoenix`,
`/tmp/qsobj`, `/tmp/wmaker-deps`, …) and none of them honours `TMPDIR`. Running
them alongside the live tree's concurrent builds is mutual corruption. The build
therefore runs inside a private mount namespace:

```
sudo -n unshare -m \
  mount --bind <W>/.tmp                          /tmp
  mount --bind <W>/.hostcache/phoenix-distfiles  ~/.phoenix-distfiles
  mount --bind <W>/.decoy-srv                    /srv
  setpriv --reuid=houp --regid=houp --clear-groups   # privileges dropped again
```

(Unprivileged user namespaces are blocked on this host —
`kernel.apparmor_restrict_unprivileged_userns=1` — hence `sudo unshare -m` plus
an immediate `setpriv` drop rather than `unshare -Ur`.)

The `/srv` bind is belt-and-braces: nothing in the *build* path resolves the
live export (`stage-desktop-fonts.sh` is reached only from
`sync-netboot-tree.sh`, a test-cycle script, and `build-wmaker.sh` is no longer
called from `phase_stage`), but `build-wmaker.sh` and `stage-desktop-fonts.sh`
both default to the `fsid=0` export from `/etc/exports` if `SHOWCASE_STAGE_DIR`
is unset, so the bind makes the constraint unbreakable.

### What is symlinked, and where a symlink is a lie

| Path | Mode | Is it a true rollback? |
| --- | --- | --- |
| `.toolchain` | **private copy** | Yes — but only after the §2/H2 fix. The gcc-16.2 compiler *is* the baseline (the manifest is the gcc-16 promotion), but its bundled `aarch64-phoenix/aarch64-phoenix/lib/{libphoenix,libc,libm}.a` is hand-maintained and carries **post-baseline** libphoenix. Symlinking it broke the build *and* would have let the build overwrite the live bundle. |
| `.venv` | symlink | Yes. Host-side Python build deps (PyYAML/jinja2/resolvelib/rich) only; version-insensitive. |
| `.firmware` | symlink | Yes. Non-free BCM43455 blobs, read-only; the generated C lands in the workspace lwip worktree. |
| `.bootblobs` | symlink | Yes. Pinned Pi firmware (`start4.elf` etc.), read-only. |
| `~/.phoenix-distfiles` | private copy | Yes. Pinned x.org tarballs; a copy avoids concurrent-download races. |
| `sources/phoenix-rtos-ports/*/…tar.*` | copied from live | Yes. Every tarball is size+sha256-verified by `port_prepare.sh`; content-addressed, so a copy is identical by construction. |
| `external/{linux,u-boot,barebox,rpi-eeprom,ffmpeg,bsd-genet}` | symlink | **N/A — not build inputs.** Research/lab-only per `bootstrap-linux-host.sh`'s `EXTERNAL_DEPS`. |
| `external/quakespasm-det` | symlink | **NOT a true rollback, and unknowable.** It is not a git repo at all, so its historical state cannot be recovered. It is a determinism-harness variant, not on the shipped-game path, so the exposure is low — but flag it. |

---

## 2. Two hazards found while building (both would have silently faked a rollback)

**H1 — lwIP submodule.** `git worktree add` does not populate submodules.
Phase A died immediately with `Makefile:23: lib-lwip/src/Filelists.mk: No such
file or directory`. Fixed by a worktree of the submodule repo at `58e89121a`
(identical at baseline and HEAD, so no version question). `phoenix-rtos-project`'s
own submodules are deliberately left uninitialised — exactly as in the live tree,
because `prepare-buildroot.sh --copy-components` copies each component from
`sources/`.

**H2 — the `.toolchain` libphoenix bundle.** The first attempt (with `.toolchain`
symlinked to the live tree) failed at the GLQuake link:

```
=== compile: 67/67 TUs OK ===
=== archived 67 objs -> …/tools/.gpu-libs/libquakespasm.a ===
=== LINK FAILED: 0 undefined symbols ===
--- other link errors ---
  collect2: error: ld returned 1 exit status
```

("0 undefined symbols" with an unexplained `collect2` error is the baseline
tooling truncating its own failure output — coord `a2958b6c5` "tools: never
truncate build failure output (11 port build scripts)" is the fix, and it is
**post-baseline**, so the baseline cannot tell you why its own links fail.)

Root cause: `tools/quakespasm-port/build-quakespasm-phoenix.py` links with **no
`--sysroot`**, so it picks up `.toolchain/aarch64-phoenix/aarch64-phoenix/lib/`.
That bundle is hand-maintained (`build-x11-phoenix.sh`'s `sync_toolchain_libc`
`cp`s into it) and the live copy is dated 2026-09-01, exporting `strtok_r`,
`memccpy`, `stpncpy`, `psignal` — i.e. libphoenix `7a1cd73`, *post-baseline*.
Baseline `gl_stubs.c` still defines its own `strtok_r`, so the link dies on a
duplicate definition (which is precisely the failure coord `91d2bd245` "fix: drop
libc stubs libphoenix now implements (breaks a from-scratch build)" describes).

Two consequences:

* the symlink made the "rollback" a **mixed** tree (baseline sources, current libc);
* `sync_toolchain_libc` would have **written the workspace's libphoenix into the
  live `.toolchain`** during `phase_stage` — a hard-constraint violation avoided
  only because the build failed first.

The workspace now has a private `.toolchain` copy, and the build runs in **two
phases** so the bundle is a baseline artefact before any ad-hoc GPU/game link:

```
A) rebuild-rpi4b-fast.sh --scope full-clean --variant nfsroot            (no showcase)
B) cp _build/<target>/sysroot/lib/{libphoenix,libc,libm}.a  ->  .toolchain bundle
C) rebuild-rpi4b-fast.sh --scope auto --variant nfsroot --with-showcase --with-vkquake
```

This is not "fixing forward": it reconstructs an input the baseline workflow
required a human to have synced by hand.

---

## 3. Verification table

Sibling SHAs are the manifest's, verbatim. Coord is the demo-verified commit
(§0.1). `DIRT` counts tracked modifications only.

| Repository | Expected | Actual | Match | Dirt |
| --- | --- | --- | --- | --- |
| `_build` (coord) | `9e53c97` (manifest) | **`15dc40787`** | **deliberate deviation — see §0.1 / §4** | 0 tracked |
| libphoenix | 9b7fe3a | 9b7fe3a | OK | 0 |
| phoenix-rtos-build | 857a5e8 | 857a5e8 | OK | 0 |
| phoenix-rtos-corelibs | c863625 | c863625 | OK | 0 |
| phoenix-rtos-devices | 5be4655 | 5be4655 | OK | 0 |
| phoenix-rtos-doc | d4419df | d4419df | OK | 0 |
| phoenix-rtos-filesystems | b017513 | b017513 | OK | 0 |
| phoenix-rtos-hostutils | 49a1fd9 | 49a1fd9 | OK | 0 |
| phoenix-rtos-kernel | 043cde80 | 043cde80 | OK | 0 |
| phoenix-rtos-lwip | bf34d89 | bf34d89 | OK | 0 |
| phoenix-rtos-lwip/lib-lwip | (submodule) | 58e89121a | OK (identical at HEAD) | 0 |
| phoenix-rtos-ports | 7a8549d | 7a8549d | OK | 0 |
| phoenix-rtos-posixsrv | 8a44ce8 | 8a44ce8 | OK | 0 |
| phoenix-rtos-project | 84a36fb | 84a36fb | OK | 0 |
| phoenix-rtos-tests | 90117b1 | 90117b1 | OK | 0 |
| phoenix-rtos-usb | d592025 | d592025 | OK | 0 |
| phoenix-rtos-utils | a585fae | a585fae | OK | 0 |
| plo | e815446 | e815446 | OK | 0 |

Externals (no manifest SHA exists for these; chosen as the last commit dated on
or before 2026-08-28, i.e. the state that produced the demo):

| Tree | Chosen | Date | Rationale |
| --- | --- | --- | --- |
| `external/mesa` | `e4be1163240` | 2026-08-25 | branch tip `phoenix-v3d-port-26.2`, pre-baseline. **Deliberately excludes 2 uncommitted files in the live tree** (`M src/broadcom/meson.build`, `?? src/broadcom/compiler/v3d_shader_dump.c`) — post-baseline debug work sitting directly in the V3D build path. Symlinking would have imported it. |
| `external/quakespasm` | `c90c9b9` | 2026-08-10 | last commit before `785eaea` (2026-09-01, "follow the phxgl_* rename") |
| `external/vkquake` | `d3e329c` | 2026-08-04 | == live HEAD, clean; unchanged since baseline |
| `external/quake3e` | `fc343078` | 2026-08-22 | last commit before `acdf34a7` (2026-09-02) |
| `external/yquake2` | `b7fad67a` | 2026-08-27 | last commit before `d0c064f9`/`ee181885` (2026-09-02) |

**Why the manifest sibling SHAs are still correct for coord `15dc40787`.** No
manifest exists at `15dc40787`, so this needs proof rather than assumption. Two
post-baseline sibling commits carry *committer* dates that look earlier than the
manifest (`devices 97c19b9` 2026-08-27 10:38, `ports 0cb6187` 2026-08-27 12:18)
— but both are **upstream** commits that reached local `master` only through the
2026-09-01 merges:

```
devices  6752847  parents b242f55 97c19b9   2026-09-01 21:50:30  Merge remote-tracking branch 'origin/master'
ports    c335352  parents c5812a1 0cb6187   2026-09-01 21:50:34  Merge remote-tracking branch 'origin/master'
```

So at demo time (2026-08-28 02:21) local `devices` was `5be4655` and local
`ports` was `7a8549d` — exactly the manifest values. Every other sibling's first
post-baseline commit is dated 2026-08-28 09:49 or later. The sibling set in this
workspace **is** the demo-time set.

> `bootstrap-linux-host.sh`'s `EXTERNAL_DEPS` pins quakespasm at
> `4abb3249…` (the `phoenix-pin` branch, 2026-06-26) — but that is the
> *fresh-clone* reproducibility path ("already present … leaving as-is"), not the
> dev-host state that produced the demos. Date selection is the right choice
> here; the pins are noted so the discrepancy is on the record.

### Caveat: the manifest snapshot was DIRTY

The manifest records `_build 9e53c97 (dirty(9))`, `phoenix-rtos-lwip bf34d89
(dirty(4))`, `phoenix-rtos-devices 5be4655 (dirty(2))`, `phoenix-rtos-project
84a36fb (dirty(1))`. **A SHA rollback cannot restore uncommitted work.** If the
baseline build or boot misbehaves, "missing dirt" is as plausible a cause as
"post-baseline commit". The devices/project/lwip dirt most likely landed in
their first post-baseline commits, all of which are unrelated to the GPU/game
path (`97c19b9` lis2mdl units; `0281848` lwip wifi netif token) — but this is
inference, not proof.

---

## 4. The build

**Command actually run** (inside the namespace, as `houp`):

```
cd /home/houp/phoenix-rpi-rollback
./scripts/rebuild-rpi4b-fast.sh --scope full-clean --variant nfsroot \
    --buildroot /home/houp/phoenix-rpi-rollback/.buildroot         # phase A
# phase B: sync sysroot libphoenix/libc/libm into the private .toolchain bundle
./scripts/rebuild-rpi4b-fast.sh --scope auto --variant nfsroot \
    --with-showcase --with-vkquake \
    --buildroot /home/houp/phoenix-rpi-rollback/.buildroot         # phase C
```

Stage list resolved by the baseline script: `clean host fs core ports project
image` (phase A, forced full-clean), and the same list again in phase C
(baseline `rebuild-rpi4b-fast.sh:334` forces it for any `--with-showcase`).
Runner + logs: `.rollback-run/run-baseline-build.sh`, `.rollback-run/build.log`,
`.rollback-run/build.rc`.

### Attempt log (verbatim outcomes)

| Attempt | Coord | Outcome |
| --- | --- | --- |
| 1 | `9e53c97`, `.toolchain` symlinked | **rc=1** — GPU archives built fine (`libv3d-phoenix.a` 411 objs / 18227 KiB, `libGL-phoenix.a` 325 objs / 17425 KiB, harness link PASS), then `build-quakespasm-phoenix.py`: `=== LINK FAILED: 0 undefined symbols === / collect2: error: ld returned 1 exit status`. Root-caused to hazard H2. Log: `.rollback-run/build-coord-9e53c97-ABORTED.log` |
| 2 | `15dc40787`, private `.toolchain` | **rc=2** — `phoenix-rtos-lwip`: `Makefile:23: lib-lwip/src/Filelists.mk: No such file or directory / make: *** No rule to make target 'lib-lwip/src/Filelists.mk'.  Stop.` Root-caused to hazard H1. Log: `.rollback-run/build-attempt2-lwip-submodule.log` |
| 3 | `15dc40787`, private `.toolchain`, submodule populated | **rc = 0** — see §4.1 |

### 4.1 Result of the definitive run — **the baseline BUILDS, rc = 0**

```
PHASE_A_RC=0        (clean host fs core ports project image, no showcase)
                    ok: no strtok_r in the bundle        <- phase B sync verified
PHASE_C_RC=0        (gpu archives + games + X11 stage + image)
BUILD RC=0
Verification: OK
Exported SHA256: c2aca41071c51dcfe353661b31f639ae9b2494c2df51c76caab5e128b1043443
```

Wall time ≈ 33 min (07:19 → 07:52), helped by the pre-seeded port tarballs and
distfile cache. `.rollback-run/build.log` (≈70 k lines) is the full record.

Every GPU/game link succeeded:

```
[archive] tools/.gpu-libs/libv3d-phoenix.a   (411 objs, 18227 KiB)
[link] PASS -> /tmp/v3dphx-harness          (11134 KiB)
[archive] tools/.gpu-libs/libGL-phoenix.a   (325 objs, 17425 KiB)
=== archived 67 objs -> tools/.gpu-libs/libquakespasm.a ===
=== LINK OK -> /tmp/quakespasm-phoenix ===
[archive] tools/.gpu-libs/libv3dv-phoenix.a (119 objs, 5300 KiB)
[link] PASS -> /tmp/v3dvphx-harness         (12396 KiB)
=== archived 83 objs -> tools/.gpu-libs/libvkquake.a ===
=== LINK OK -> /tmp/vkquake-phoenix ===
```

**Three soft failures** (`run_step_soft`, recorded and continued — they do not
affect rc):

```
[WARN] PHASE stage finished with 3 soft failure(s):
  - port app: nano
  - port app: mc
  - X11: Xphoenix-glamor-daemon (concurrent-GPU X)
```

* `Xphoenix-glamor-daemon` failed **exactly as predicted** below:
  `=== configuring xorg-server-1.20.14 (kdrive core, aarch64-phoenix, glamor=0) ===`
  then `missing libglamor.a — configure core with --enable-glamor first`.
  → **The baseline cannot build the GPU-accelerated X server from a clean tree.**
  Coord `d0b151111` (2026-09-03) is the fix and is *newer* than this baseline.
  The software `Xphoenix` did build (6 028 696 B) and `startx`/`startx_gpu` are
  staged, so plain X is fine; only the glamor-accelerated daemon is missing.
* `nano` and `mc` failed in their ad-hoc autoconf builds
  (`make[2]: *** [Makefile:314: global.o] Error 1` and
  `make[3]: *** [Makefile:570: mountlist.lo] Error 1`). Both are pre-existing
  ad-hoc-port breakage at the baseline, unrelated to the GPU/game path, and the
  baseline's truncated-output problem means the real cause is not in the log.

**So: the baseline builds, cleanly, in an isolated workspace — with three known
soft gaps, one of which (glamor X) is a genuine baseline reproducibility hole.**

### Predicted failures in the baseline, with citations

These are named up-front so a failure is not mistaken for a workspace artefact:

* **glamor X server, `run_step_soft` → soft failure.** `build-xfbdev.sh
  --glamor-daemon` hard-checks `missing libglamor.a — configure core with
  --enable-glamor first` (`build-xfbdev.sh:133`), and baseline `phase_stage`
  calls `build-x11-phoenix.sh` *without* `--glamor`. Coord `d0b151111`
  (2026-09-03) is the fix, and its own message says the check "had always passed
  before because an old ad-hoc `--enable-glamor` reconfigure had left the archive
  lying in `tools/x11-port/src/`". A fresh worktree has no such archive.
  → **The baseline's GPU-accelerated X is not reproducible from a clean tree.**
* **Truncated failure output.** Any port-script failure will print a 3-line tail
  and hide the cause; coord `a2958b6c5` is the fix. Expect to re-run a failing
  step by hand.
* **A `python` clean-build langinfo gap** was recorded as deferred in coord
  `75359a61a` (2026-08-28 00:38) — i.e. known-broken *at* the demo. It cannot
  fail *this* build: `python` is `if: false` at baseline, so the ports stage
  never builds it (it was flipped on only in project `e4c5cc7`, 2026-09-03).
  Noted because it is evidence that the demo's `python3` was also hand-staged.

Verified pre-flight for hazard H2 (recorded so the phase-B sync is auditable):
in both the built sysroot and the toolchain bundle, `libc.a`, `libm.a`,
`libg.a`, `libpthread.a` and `libubsan.a` are **symlinks to `libphoenix.a`**, so
the phase-B `cp` of all three resolves to the one real archive. Before the sync
the private bundle held the 09-01 libphoenix (9 641 960 B, exporting
`strtok_r`/`memccpy`/`stpncpy`/`psignal`); phase A's core stage produced the
baseline one (9 501 406 B). Residual known deviation: the bundle's `crt0.o` and
`libstdc++.a` are the toolchain's own (2026-08-23) rather than the baseline
sysroot's — that is the same arrangement the baseline dev host had, so it is
faithful, but it is a place where the ad-hoc game links differ from the
framework ones (`--sysroot`).

---

## 5. Commit-risk analysis: what changed since the baseline

### 5.0 Correcting the commit count

The task brief said "93 commits (devices +14, ports +11, project +10, build +5,
kernel +30, libphoenix +23)". The real figures, measured against the manifest
SHAs:

| Repo | Commits since baseline | In brief? |
| --- | --- | --- |
| phoenix-rtos-kernel | 30 | yes |
| phoenix-rtos-tests | **26** | **no** |
| libphoenix | 23 | yes |
| phoenix-rtos-devices | 14 | yes |
| phoenix-rtos-ports | 11 | yes |
| phoenix-rtos-project | 10 | yes |
| phoenix-rtos-filesystems | **5** | **no** — nfs-fs, i.e. the NFS *root* path |
| phoenix-rtos-utils | **5** | **no** — psh/ntpclient, i.e. the boot path |
| phoenix-rtos-build | 5 | yes |
| phoenix-rtos-lwip | see below | — |
| corelibs, doc, hostutils, posixsrv, usb, plo | 0 | — |
| **coord repo** | **336** (from `15dc40787`) / 362 (from `9e53c97`) | **not in brief at all** |

Total sibling commits ≈ **129**, not 93 — and the coord repo, which owns every
`tools/` build recipe and every showcase script, contributes 336 more. The three
missing repos matter: `filesystems` is the NFS root the netboot demo mounts, and
`utils` changed how psh starts (`829601d`, `f40cafa`, `5d19cda`).

**lwIP is not "+506".** `git rev-list --left-right --count bf34d89...HEAD` gives
`175 506`: the baseline and HEAD share no ancestry, because the fork was
re-based onto real upstream on 2026-09-01 (the "de-tangle"). Only the **top 14**
commits (`c82be84`…`fcb4311`, all dated 09-01/09-02) are post-baseline *work*;
everything below is upstream history that existed in a different shape before.
The honest measure is `git diff --stat bf34d89 HEAD` → **131 files, +49 921 /
−70**, and virtually all of it is the restored `wi-fi/whd/` Cypress subtree
(~46 k lines) plus the genet/netif commits. Net risk to the GPU/game path:
**near zero**; net risk to boot/network: the netif and `/dev/ipstats` changes.

### 5.1 Risk table — GPU / game path

| Rank | Change | Commits | What it could plausibly have broken | Cheapest test |
| --- | --- | --- | --- | --- |
| **1** | **Ports patch regeneration** (`scripts/game-port-patch.sh --regen`) | ports `0d9de9a` (+1204/−75 across the 4 game patches), coord `af65210a1` | The four game patches were *hand-written subsets*; they are now `git diff <pinned tarball>..fork-HEAD`, i.e. **the fork tip verbatim**. That pulls in the post-baseline fork commits (`785eaea` phxgl_* rename, `acdf34a7` platform/`send()`/RWX-VM/GL-types, `d0c064f9`+`ee181885` yquake2 single-ELF + gl3/gles3 accept) **and** the whole visual-regression capture harness (65 / 32 / 39 capture-related lines in the quakespasm / quake3e / yquake2 patches respectively; vkquake 0). Claimed inert (`scr_capture` default 0, `#ifdef QSS_PHOENIX`, `VKQ_TEXDBG`) — but "inert by default" has to be verified, not asserted, and yquake2's renderer default *changed* to gl3/GLES3 (`622465a`). | `git -C sources/phoenix-rtos-ports show 0d9de9a -- '*/patches/*'` and diff the *engine* hunks against the old hand-written patch (`git show 0d9de9a^:quakespasm/patches/...`). Then build one game port standalone (`scripts/build-port.sh quakespasm`) and `nm`/`strings` it for `Q3Cap_Stream`/TGA symbols. No Pi needed. |
| **2** | **`if: false` → `if: true` port flips + games out of `loader.disk`** | project `e4c5cc7`, coord `1e5a4a8ba` | The single largest behavioural change. Games moved from a `_user/rpi4-quake` blob **inside `loader.disk`** to ordinary rootfs binaries in `/usr/bin`, and `python`+`wpa_supplicant` were flipped on at the same time. Everything about how a game is found, exec'd, and how big `loader.disk` is, changed at once. Also flipped: STK was **never registered** before, so no image had ever built it. | Compare `loader.disk` size and the `_user` component list baseline-vs-now (§6). Then, on HW, `ls -l /usr/bin` and exec one game — an exec failure here is a rootfs/exec issue, not a renderer issue, and separates cleanly. |
| **3** | **D9: V3D/Mesa glue relocated into `phoenix-rtos-devices`** | devices `b0c9cdb` (+5136/−7), ports `25a1b62`, project `bddef83`, coord `1b70e315e`, `d56abcb58` | **This was not a pure move.** Comparing baseline `tools/v3d-driver-port/*` against `devices/gpu/rpi4-v3d/mesa/*`: identical — `v3d_phoenix_stubs.c`, `v3d_phoenix_power.c`, `v3d-core-sources.txt`, `v3d-aux-sources.txt`, `phoenix_mesa_compat.h`, `v3d_libdrm_shim.c`, `vk_icd_link.c`; **differs** — `v3d_phoenix_winsys.c`, `gl_stubs.c`, `build-gl-phoenix.py`, `build-v3d-phoenix.py`, `build-v3dv-phoenix.py`, `resolve-syms.py`, `v3dv_libdrm_shim.c`. Functional deltas hiding in the "move": (a) CSD spin budget `8000000u → 80000000u` (see rank 4); (b) `strtok_r` stub deleted from `gl_stubs.c` (see rank 5); (c) `ROOT` is now `PHOENIX_RPI_ROOT` or `Path(PORT).parents[4]` — correct for `<root>/sources/…` and, by luck, for `<root>/.buildroot/…`, but **wrong for any buildroot outside the repo root**; (d) `HARNESS_DIR` still defaults into the coord repo's `tools/v3d-driver-port`, so the "relocated" devices build still depends on the coord tree; (e) `libvcmbox.{c,h}` de-duplicated to `devices/misc/rpi4-vcmbox` — the canonical copy, but a *different* file than the one the demo archives linked. | `diff -r` the two glue trees (already done — reproduce with the file list above). Then `cmp` the archives: build `libv3d-phoenix.a`/`libGL-phoenix.a` from each tree and compare `nm --defined-only` output. Object-count/size deltas are visible without any Pi. |
| **4** | **`v3d_phoenix_winsys.c` CSD spin budget 8M → 80M** | inside devices `b0c9cdb` | Post-baseline commit message: V3DV lightmap dispatches (CFG0 ~`0x00570000` vs `0x00010000` for the probe) exhausted the 8M budget "hundreds of times per run, each time returning with `num_completed=0` — so the caller proceeded without its lightmaps and the world rendered black". **Do not read this as "the baseline has the black-lightmap bug"**: coord `c5968445f` (2026-08-28 01:32) records vkQuake rendering correctly on the baseline. So the wedge is *environment-sensitive* (timing / clock / concurrent load / a different Mesa build) and appeared in the post-D9 era. It is a prime bisect-forward candidate, and the 10× raise may be masking a different root cause. | Revert just that one hunk in the current tree and run vkQuake — one Pi cycle. If black lightmaps return, the budget is load-bearing *now* but was not *then*; that difference is the bug. |
| **5** | **`gl_stubs.c` `strtok_r` removal ↔ libphoenix `7a1cd73`** | devices `b0c9cdb`, coord `91d2bd245`, libphoenix `7a1cd73` | **A bisect-ordering constraint, not a regression.** libphoenix now exports `strtok_r` (from both `libphoenix.a` and `libm.a`); the baseline stub therefore collides. **Baseline GPU glue + post-09-01 libphoenix = `multiple definition of strtok_r` at every game link** (this is exactly how attempt 1 failed, §2/H2). Same applies to `memccpy`, `stpncpy`, `psignal` (libphoenix `7a1cd73`) and to anything else `libphoenix` closed. → **When bisecting, libphoenix and the GPU glue must move together.** Also: the `.toolchain` bundle is a *build input* for these links; re-sync it at every bisect step or you are testing a mixed tree. | `aarch64-phoenix-nm .toolchain/…/lib/libphoenix.a \| grep -w 'T strtok_r'` before every game link. If present, the glue must be the post-baseline one. |
| **6** | **X11 / glamor build** | coord `d0b151111`, `f9004c567`, `a2b4b64b6`, and (pre-demo, i.e. *needed*) `f1854f2c7`, `26f6033c9`, `803469872`, `c21698b4e`, `f8febb060` | Two directions. *Losing* the five pre-demo fixes (they are AFTER the manifest's coord SHA) breaks the desktop: libXt won't build under gcc-16/C23, `xlaunch` races the server, fonts are non-reproducible, twm instead of wmaker, `wmsetbg` staged to the wrong export → black root. *Gaining* `d0b151111` fixed a genuine clean-build hole (glamor never configured). So the X desktop needs coord ≥ `15dc40787` **and** ideally `d0b151111`. | Nothing about X needs the games. Run `phase_stage`'s X11 steps alone in the workspace and check `Xphoenix-glamor-daemon` links; then `startx_gpu` on HW. |
| **7** | **`port_manager` invalidation semantics** | build `5868ef9`, `4361efc`, `f44fd69`, `49ca648`, `f05ded2` (all 2026-09-02/03) | Five commits in ~3 hours changing *when a port is re-configured/re-patched*: patch markers keyed on content not filename, re-configure on libc API change, clear every configure marker, don't skip invalidation when only `config.status` is missing, never treat a port's Makefile as configure output. These change which ports get rebuilt and therefore **which stale objects survive a rebuild** — plausible cause of "worked before, broken now" without any source change. High blast radius, low visibility. | Build the same port twice, once per `port_manager` version, and `cmp` the installed binary. Purely host-side. |
| **8** | **Kernel `posix`/`vm` hardening (24 of the 30 kernel commits, all 09-02)** | kernel `1ff99ec4`…`cc9a3544` | A dense burst of zone poisoning, free-list validation, construction-reference tracking, fd-sweep lock scoping and uninitialised-memory fixes. Any of them can change timing or fail an operation that previously succeeded loosely — and the games are the heaviest users of mmap/AF_UNIX/fd churn. `3dd747e0`/`b49268e5`/`7fb8a317` also change *fault reporting*, so a game symptom may look different rather than be different. | Bisect only if a game fails in a way that looks like memory/fd behaviour. Cheap discriminator: run the same game with `--variant nfsroot` on baseline vs current *kernel only* (everything else current). |
| **9** | **NFS-root behaviour** | filesystems `694ff6d`…`cd30ad2`, ports `c40f233`/`c76fcc4` | The demo boots its root over NFS, and STK's asset tree is tens of thousands of small files. `509aaa8` (unlink-while-open), `fc2f62b` (readlink truncation), `cd30ad2` (FIFOs/device nodes) and libnfs `c40f233` (OPEN-with-create taking the EXCLUSIVE4 path) all change what the root filesystem does under exactly that load. | `ls -lR /usr/share/supertuxkart | wc -l` + time a game's asset load on HW. Separable from rendering. |
| **10** | **psh / boot clock churn** | utils `cbfec4b`…`5d19cda`, project `9deec9c`→`a3fb926`→`4ccd01c` (added then reverted then completed) | An add-and-revert cycle around setting the wall clock at boot, plus changes to how psh waits for its init script. If a game or the desktop fails to start *at all*, suspect this before suspecting the renderer. | `uart-summary.sh` on any boot log: does `(psh)%` appear, and how long after `lwip`? |
| **7b** | **NFS-export staging changed** (the brief's "changed the NFS export staging") | coord `f9004c567`, `1e5a4a8ba` (`make-pristine-nfs-export.sh`, `sync-netboot-tree.sh`) | **Read this one before blaming any code.** `f9004c567`'s own message: `make-pristine-nfs-export.sh` hardcoded `/srv/phoenix-rpi4-nfs` while the **served** root is `/srv/phoenix-rpi4-nfs-gcc16`, so "the pristine tree was being swapped into a directory nothing mounts — the Pi kept booting the stale export and the 'clean export' step proved nothing." And `sync-netboot-tree.sh` is **additive only** (no deletion path), so the served export accumulated hand-staged binaries indefinitely, plus a root-owned Mesa shader disk cache that "no script referenced and which renders green speckle that reads as a GPU wedge". Consequence: for an unknown window, HW results were produced against a tree that was neither the baseline nor the current build. Several of the "breakages" of the last few days may be *this*, surfacing as the staging was finally made honest. | `ls -l --time-style=+%F /srv/phoenix-rpi4-nfs-gcc16/usr/bin/` — any game binary older than the last build is hand-staged residue. Then `make-pristine-nfs-export.sh` + a single boot. Cheap, and it isolates "stale export" from "bad build" before anything else is bisected. **Checked 2026-09-03 07:xx: the served export is currently consistent** (every binary in `/srv/phoenix-rpi4-nfs-gcc16/{bin,usr/bin}` is dated 2026-09-03 05:13–06:51 and byte-matches the current `_fs` tree), so this is a *historical* confound for results from earlier in the week, not a live one. |
| — | **WiFi (devices ×10, lwip netif)** | devices `be7c62a`…`4e8a7e9`, lwip `24aba77`/`2118d0e` | Large but on an orthogonal subsystem. Only realistic coupling: `2118d0e` "stop stealing the default route" — a wrong default route breaks the NFS root, hence everything. | Check the routing table / that NFS root mounts. |
| — | **tests (+26)** | tests `890f1e1`…`83eda31` | No shipped-binary risk (test binaries are staged only with `--with-tests`). Listed for completeness because the brief omitted them. | n/a |

### 5.2 The specific items the brief asked about, answered directly

* **D9 V3D/Mesa move** — see rank 3. **Not a pure relocation**: 7 of 14 compared
  files differ, two of them functionally (CSD spin budget, `strtok_r`), plus a
  path-derivation change and a lingering dependency on the coord repo's
  `tools/v3d-driver-port` for the link harness.
* **`scripts/game-port-patch.sh` patch regeneration** — see rank 1. The
  regenerated patches carry the **fork tip**, including the capture harness and
  the post-baseline engine commits. The commit message itself admits the old
  patch and the fork had *diverged in both directions* (the patch accepted
  gl3/gles3 that the fork rejected; the fork had a V3D mipmap-skip fix the patch
  lacked), so neither pre-existing representation was what the demo binaries
  were built from — which means **the demo-verified game binaries correspond to
  neither the old patch nor the new one**. That is the single most important
  thing to hold in mind when comparing "then" and "now".
* **`if: false` → `if: true` flips** — see rank 2. Flipped on 2026-09-03:
  `python`, `wpa_supplicant`, `quakespasm`, `yquake2`, `quake3`, `vkquake`, and
  `supertuxkart` (newly *registered*, never previously built by any image).
* **`v3d_phoenix_winsys.c` / `libvcmbox`** — winsys: rank 4 (one hunk, the CSD
  spin budget). `libvcmbox`: no functional change, but the *file* moved from
  `tools/v3d-driver-port/libvcmbox.c` to
  `devices/misc/rpi4-vcmbox/libvcmbox.c` and is now the single canonical copy
  linked into `libv3d-phoenix.a`. Worth a `cmp` of the two copies as part of
  rank 3's archive comparison.
* **X11 / glamor build** — see rank 6, and §0.1: the *pre*-demo X11 fixes are
  the reason the manifest's coord SHA is the wrong rollback point.

### 5.3 Suggested bisect order (cheapest discriminator first, no Pi where possible)

1. Fix the frame: use coord `15dc40787` + manifest siblings as "then" (this
   workspace), current tree as "now". Do **not** use coord `9e53c97`.
2. Host-only: rebuild each game port under old vs new `port_manager` (rank 7)
   and compare binaries. Eliminates or convicts the build framework for free.
3. Host-only: diff the *engine* hunks of the regenerated patches against the
   old hand-written ones (rank 1). Anything in there is a candidate.
4. Host-only: build the GPU archives from both glue trees and compare
   `nm --defined-only` (rank 3). Remember rank 5 — move libphoenix with it.
5. Only then go to HW, and split the question first: does the binary **exec**
   (rank 2 / rank 9 / rank 10), before asking whether it **renders** (rank 3/4).

---

## 6. Artefact comparison (for the parent to diff against the current tree)

### 6.1 The CURRENT tree, measured (read-only, for the diff)

`/home/houp/phoenix-rpi/.buildroot`, as of 2026-09-03 05:19–06:50:

| Artefact | Bytes |
| --- | --- |
| `_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk` | **4 504 016** |
| `_fs/…/root/usr/bin/quakespasm` | 18 553 680 |
| `_fs/…/root/usr/bin/yquake2` | 19 109 952 |
| `_fs/…/root/usr/bin/quake3e` | 19 180 064 |
| `_fs/…/root/usr/bin/vkquake` | 12 799 520 |
| `_fs/…/root/usr/bin/supertuxkart` | 38 474 984 |
| `_fs/…/root/bin/python3` | 57 053 120 |
| `_fs/…/root/usr/bin/Xphoenix` | 6 028 952 |
| `tools/.gpu-libs/libGL-phoenix.a` | 17 842 988 |
| `tools/.gpu-libs/libv3d-phoenix.a` | 18 664 900 |
| `tools/.gpu-libs/libv3dv-phoenix.a` | 5 427 660 |

> **The current tree is a moving target.** The parent session rebuilt during this
> work: coord HEAD went `fa01e6a90` → `bbd447eca` and `loader.disk` changed again
> at 07:46 (to **4 187 504** B, still with no NFS-takeover line, i.e. still not
> the `nfsroot` variant). Re-measure the "current" column before drawing
> conclusions from it, and rebuild the current tree as `--variant nfsroot` first.
> (Isolation held throughout: the live `.buildroot`, `tools/.gpu-libs`, the
> `.toolchain` bundle, `/srv/phoenix-rpi4-nfs-gcc16` and the host `/tmp/mesa-v3d-build`
> / `/tmp/x11-phoenix` prefixes all still carry their pre-07:19 or
> parent-session mtimes — none was written by this build.)

**Early GPU-archive signal.** Attempt 1 (baseline glue, before it hit the
`strtok_r` link wall) archived `libv3d-phoenix.a` at **411 objs / 18227 KiB** and
`libGL-phoenix.a` at **325 objs / 17425 KiB`. The current archives are
18 664 900 B = 18227 KiB and 17 842 988 B = 17425 KiB — **the same size to the
KiB.** That is consistent with §5.1 rank 3: the D9 "move" did not change which
Mesa objects go into the archives, only the harness/diagnostics plus the one CSD
spin-budget constant. So the GPU archives are a *low*-probability source of the
game breakage, and rank 1 (patch regeneration) / rank 2 (the port flips) should
be investigated first.

### 6.2 The baseline

Measured from the rc=0 build. Variant: **nfsroot**.

| Artefact | Baseline (bytes) | Current (bytes) | Note |
| --- | --- | --- | --- |
| `_boot/…/rpi4b/loader.disk` | **4 437 248** | 4 504 016 | **NOT apples-to-apples** — see the variant caveat below |
| exported `artifacts/rpi4b/rpi4b-sd.img` | 69 206 016 (sha256 `c2aca410…`) | 69 206 016 (sha256 `327f4cde…`) | FAT-only netboot image; `verify-rpi4b-sdimg.sh` → `Verification: OK` |
| `_fs/…/root/usr/bin/rpi4-quake` (stripped) | **17 801 472** | — (renamed) | GLQuake; current tree ships it as `usr/bin/quakespasm` = 18 553 680 |
| `_fs/…/root/usr/bin/rpi4-vkquake` (stripped) | **12 888 216** | — (renamed) | current tree: `usr/bin/vkquake` = 12 799 520 |
| unstripped `prog/rpi4-quake` | 22 454 136 | — | |
| unstripped `prog/rpi4-vkquake` | 19 163 128 | — | |
| `usr/bin/yquake2` | **absent** | 19 109 952 | no baseline recipe (see §0.3) |
| `usr/bin/quake3e` | **absent** | 19 180 064 | no baseline recipe |
| `usr/bin/supertuxkart` | **absent** | 38 474 984 | not even registered at baseline |
| `bin/python3` | **absent** | 57 053 120 | `if: false` at baseline |
| `usr/bin/Xphoenix` | 6 028 696 | 6 028 952 | −256 B; both software (non-glamor) |
| `bin/Xphoenix-glamor-daemon` | **absent** (soft fail) | present | the glamor hole, §4.1 |
| `tools/.gpu-libs/libGL-phoenix.a` | 17 843 394 (325 objs) | 17 842 988 | Δ = +406 B |
| `tools/.gpu-libs/libv3d-phoenix.a` | 18 664 868 (411 objs) | 18 664 900 | Δ = −32 B |
| `tools/.gpu-libs/libv3dv-phoenix.a` | 5 428 108 (119 objs) | 5 427 660 | Δ = +448 B |
| `tools/.gpu-libs/libquakespasm.a` | 5 702 742 (67 objs) | **absent** | no longer produced (game is a framework port now) |
| `tools/.gpu-libs/libvkquake.a` | 18 924 286 (83 objs) | **absent** | ditto |
| `tools/.gpu-libs/gl-x11-window-daemon` | 22 740 400 | 22 755 176 | |

**Variant caveat on `loader.disk`.** The baseline image here is `nfsroot`; the
current tree's `_boot` was last built as **`sd`** (an `rpi4b-sd-2part.img` of
1 680 867 328 B is present, and its `loader.disk` has no
`app … -x nfs;/;10.42.0.1;/;v4;takeover` line, which the baseline's does). The
plo program sets differ accordingly:

```
baseline (nfsroot): dummyfs lwip nfs-fs pl011-tty posixsrv psh
                    rpi4-audio rpi4-fb rpi4-gpio rpi4-hwrng rpi4-nfsfs
                    rpi4-thermal rpi4-vcmbox
current  (sd):      dummyfs lwip        pl011-tty posixsrv psh
                    rpi4-audio rpi4-fb rpi4-gpio rpi4-hwrng rpi4-sysinfo
                    rpi4-thermal rpi4-vcmbox rpi4-wifi
```

The missing `nfs-fs` in the current blob is the **`sd` variant gating it out**,
not a regression — but it means the parent must rebuild the current tree as
`--variant nfsroot` before any loader.disk / boot comparison is meaningful.

**Correction to an earlier assumption.** The baseline script prints
`Showcase: bundling rpi4-quake into loader.disk`, but `strings loader.disk |
grep -c rpi4-quake` = **0**: the `app rpi4-quake` plo line is gated to the `sd`
variant, so even at the baseline the `nfsroot` image ships GLQuake as an
ordinary rootfs binary at `/usr/bin/rpi4-quake`. The script's message is stale.
The real rank-2 change is therefore narrower than the commit messages suggest —
for `nfsroot` it is a **rename plus three new engines**, not a relocation out of
`loader.disk`.

**And the GPU archives are effectively unchanged.** All three have **identical
object counts** (411 / 325 / 119) and differ in size by **under 450 bytes** on
5–19 MB archives. (Size caveat: the "current" column was measured against the
06:48–06:50 `tools/.gpu-libs`; the parent rebuilt at 07:46, so re-measure before
relying on the byte deltas. The object-count identity is the robust part.) Whatever broke the games, it is almost certainly **not** the Mesa/V3D
archive content — reinforcing the §5.3 ordering: look at the regenerated port
patches (rank 1) and the port/staging machinery (ranks 2, 7, 7b) first.

---

## 7. Reproducing / continuing this work

```
# state of the definitive run
cat /home/houp/phoenix-rpi-rollback/.rollback-run/build.rc
tail -100 /home/houp/phoenix-rpi-rollback/.rollback-run/build.log

# re-run it (idempotent; wipe .buildroot first for a true clean)
/home/houp/phoenix-rpi-rollback/.rollback-run/run-baseline-build.sh

# tear the workspace down (removes the worktree registrations too)
git -C /home/houp/phoenix-rpi worktree remove --force /home/houp/phoenix-rpi-rollback
for r in /home/houp/phoenix-rpi/sources/*; do git -C "$r" worktree prune; done
for r in /home/houp/phoenix-rpi/external/*; do git -C "$r" worktree prune 2>/dev/null; done
git -C /home/houp/phoenix-rpi/sources/phoenix-rtos-lwip/lib-lwip worktree prune 2>/dev/null
rm -rf /home/houp/phoenix-rpi-rollback     # last: the tree itself
```

The workspace holds `git worktree` registrations in the live repos'
`.git/worktrees/` directories. Those are metadata only — no live working tree
was checked out, moved, or modified — but they should be pruned when the
workspace is no longer needed.
