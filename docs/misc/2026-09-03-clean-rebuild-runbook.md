# Clean-rebuild runbook (2026-09-03)

**Purpose.** After the upstream merges, fork re-arrangement, patch regeneration and
build-script changes of the last days, rebuild *everything* from source with **no**
reuse of objects, archives, port workdirs, staged rootfs or caches — then verify the
nfsroot rootfs and the SD image were cut from the same tree.

**Audience.** The operator running the real build. This document is the single
ordered procedure. The reasoning behind each individual wipe and guard lives in the
comments of the scripts themselves; the two commits that introduced them are
`f9004c5` ("scripts: close the stale-artifact holes in the build/staging machinery")
and `a2b4b64` ("full-clean also wipes the tools/ caches"), whose messages carry the
per-fix rationale.

**Status of the timings below: ESTIMATES.** They are inferred from tree sizes
(`_build/<target>` is 5.1 GB, `port-sources/` 4.2 GB across 52 port workdirs,
SuperTuxKart alone is 192 MB of source) and from the shape of past clean builds. No
step below has been timed end-to-end for this exact stage list. Treat them as
order-of-magnitude, not as a watchdog budget.

---

## 0. Preconditions

```
cd /home/houp/phoenix-rpi
./scripts/git-siblings.sh status          # every repo must be CLEAN
```

Everything you want in the image must be **committed**. Two independent reasons:

* `scripts/build-sd-in-docker.sh` clones committed state only (it now refuses to run
  on a dirty tree — `ALLOW_DIRTY=1` overrides).
* `rebuild-rpi4b-fast.sh --scope auto` keys its stage selection off sibling dirt;
  `--scope full-clean` (used here) does not, but a dirty tree makes the build
  unreproducible either way.

Also confirm no other build is running against `.buildroot` — check for a recent
mtime on `.buildroot/_build/aarch64a72-generic-rpi4b/prepare.log`. Two concurrent
builds in one buildroot corrupt each other silently.

Disk: the wipe below frees ~5.4 GB and the rebuild re-consumes it plus the Docker
path's own layers. Have **≥ 40 GB** free.

---

## 1. Wipe everything reusable

### 1a. What `--scope full-clean` now does for you

As of commit `f9004c5`, `./scripts/rebuild-rpi4b-fast.sh --scope full-clean` performs
both halves:

| Wiped by `build.sh clean` (build.sh:186-189) | Wiped by the new coord-repo block (rebuild-rpi4b-fast.sh) |
|---|---|
| `.buildroot/_build/aarch64a72-generic-rpi4b` (incl. `sysroot`, `port-sources`, `.port_state`, `versioned-ports`, `.stamp_sysroot`, `prog`, `lib`, `include`) | `.buildroot/_boot/host-generic-pc` |
| `.buildroot/_build/host-generic-pc` | `tools/.gpu-libs/*` (archives + stray ELFs) |
| `.buildroot/_fs/aarch64a72-generic-rpi4b` | `/tmp/{mesa-v3d-build,mesa-v3dv-build,mesa-pyenv}` |
| `.buildroot/_boot/aarch64a72-generic-rpi4b` | `/tmp/{x11-phoenix,wmaker-deps}` |
| | `/tmp/phoenix-{iconv,ffi,ncurses,glib,mc}`, `/tmp/{fltk,dillo}-phoenix` |
| | `/tmp/python-port-build`, `/tmp/{qsobj,qsobj-det,qsobj-sdl,vkqobj}` |
| | `/tmp/{sdl2test-obj,sdl2audio-obj,gl-smoke-build}` + the loose `.o`/`.a` |
| | **the extracted trees under `tools/ports/src/` and `tools/x11-port/src/`** |
| | `tools/v3d-driver-port/.build-csd-daemon` |

The `tools/{ports,x11-port}/src/` entry is the one that is easy to get wrong.
Wiping the `/tmp` prefixes alone is **not** enough: every `config.status`, every
"already patched" stamp (`.dillo-tls-mode`, `.mc-guard-configured`,
`.phoenix-glamor-enabled`) and — critically — the 25 xorg-server core archives live
in those source trees, not in `/tmp`. `build-xserver-core.sh`'s `core_built()` checks
the archives *in the src tree*, so with `/tmp` cleared and `src/` kept it
early-returns on last month's archives and `Xphoenix` links against them.

Only the extracted **directories** are removed; the downloaded tarballs sitting next
to them are kept, so this costs a re-extract, not a re-download.

Plus: `--scope full-clean` now passes `--force` to the GPU phase, so the Mesa host
trees are re-`meson setup`'d and all three GPU archives are rebuilt unconditionally.

`prepare-buildroot.sh` (run automatically at the start of every invocation) then
rsyncs `sources/phoenix-rtos-project` → `.buildroot` **with `--delete`**, refreshing
every component copy from `sources/` and removing anything deleted upstream. That
also wipes `_fs/<target>/root` on every run — see the ordering warning in §2.

### 1b. What you must still wipe by hand (needs `sudo`, or is outside the build)

```
# Mesa shader disk cache on the served NFS export. Root-owned; no script referenced
# it before today. A cache written by a previous engine build renders GREEN SPECKLE
# over a valid frame, which reads as a GPU wedge.
sudo rm -rf /srv/phoenix-rpi4-nfs-gcc16/.mesa-shader-cache

# Optional: the old export backups, if you are short on disk.
#   /srv/phoenix-rpi4-nfs           (superseded, no longer the fsid=0 export)
#   /srv/phoenix-rpi4-nfs.PREV-cruft
```

### 1c. What is deliberately NOT wiped

* **The ports tarball cache** (`sources/phoenix-rtos-ports/*/*.tar.*`, ~392 MB).
  Every tarball is size+sha256-verified against the recipe on *cold* extract
  (`port_prepare.sh:72-98`), and `port-sources/` is gone with `_build`, so each port
  re-extracts and re-patches from a verified archive. Deleting it only costs a
  network round-trip and adds an x.org/CDN outage as a failure mode (that is exactly
  what killed the session-206 clean build; see `scripts/clean-rebuild-resume-when-xorg-up.sh`).
  Delete it only if you specifically suspect a tarball.
  Caveat: the download guard is `[ ! -f "${PREFIX_PORT}/${filename}" ]`
  (`port.subr:41`) — **filename-only**. If a recipe changed its `source=` URL but
  kept `archive_filename`, the old file is reused; the sha256 check then catches it
  and fails the build loudly. That is acceptable (loud), not silent.
* **`.toolchain/`.** The gcc-16.2 cross toolchain is settled and rebuilding it costs
  ~1 h with no expected change. But note §5 — its bundled libc is hand-maintained.
* **`external/mesa`.** A pinned upstream checkout plus tracked patches; a wipe forces
  a multi-GB re-clone. `scripts/bootstrap-linux-host.sh --pinned` re-pins it if you
  suspect drift.
* **The x.org distfile cache** (`~/.phoenix-distfiles/x11`). Keeping it is what makes
  the `src/` tree wipe cheap and offline. It has **no checksums anywhere** — nothing
  in `build-x11-phoenix.sh` verifies a tarball — so `--scope full-clean` now warns
  about any file under 10 KB (a CDN outage serves 95-byte stubs). Heed that warning;
  delete the named files and re-run.

**Estimated wall clock for §1: < 1 minute** (it is only `rm -rf`), except that the
first `--scope full-clean` invocation folds the wipe into the build, so you will not
see it as a separate step.

---

## 2. Build the tree from scratch

### THE ORDERING RULE THAT WILL BITE YOU

`rebuild-rpi4b-fast.sh` runs `prepare-buildroot.sh` on **every** invocation unless
you pass `--skip-prepare`, and that rsync **deletes `_fs/<target>/root`**
(`sources/phoenix-rtos-project/_fs/` contains only `root-skel/`, so `--delete`
removes everything else under `_fs`).

Consequence: **a second `rebuild-rpi4b-fast.sh` run destroys the rootfs the first one
staged**, unless it either (a) re-runs the whole `fs core ports project` stage list,
or (b) is given `--skip-prepare`. This is the mechanism behind the whole problem the
owner hit: the staging tree got gutted, only part of it was re-populated, and
`sync-netboot-tree.sh` (additive, no `--delete`) topped the export back up so the
Pi kept running months-old binaries that nothing on the host could rebuild.

So: **one big invocation, then only `--skip-prepare` follow-ups.**

### 2a. Single full clean build (produces core + ports + GPU + rootfs + SD image)

```
cd /home/houp/phoenix-rpi
./scripts/rebuild-rpi4b-fast.sh \
    --scope full-clean \
    --variant sd \
    --with-showcase --with-ports --with-tests \
    2>&1 | tee artifacts/clean-rebuild-2026-09-03.log
```

Why this exact command:

* `--scope full-clean` → `build.sh clean host fs core ports project image` **plus**
  the out-of-buildroot wipe of §1a **plus** `--force` on the GPU archives.
* `--variant sd` → also runs `build-rpi4b-rootfs-ext2.sh`, so you get
  `_boot/<target>/part_rootfs.ext2` and `artifacts/rpi4b/rpi4b-sd-2part.img`.
  Do the **sd** variant first: its rootfs tree is the superset you then re-use for
  the NFS export in step 2b (the only rootfs difference is that `nfsroot` also
  installs `nfs`; see §4).
* `--with-showcase` → GPU phase before `build.sh` (the five game ports link
  `tools/.gpu-libs/lib{GL,v3d,v3dv}-phoenix.a` by absolute path and `b_die` without
  them), then the X11/ports staging phase after.
* `--with-ports` is implied by the full-clean stage list but harmless and explicit.
* `--with-tests` builds `phoenix-rtos-tests` so the libc suites are on the image.

**Estimated wall clock: 4–8 h.** Rough shape (estimates, not measured):
toolchain not rebuilt; host tools + core ≈ 15–25 min; GPU/Mesa phase (`--force`,
two `meson setup` + three cross-compile archive builds) ≈ 40–70 min; **ports stage
dominates** — 52 workdirs re-extracted, re-patched, re-configured, re-built, with
SuperTuxKart, python, ffmpeg, xorg_server and the four game engines the long poles
≈ 2–4 h; X11/ports staging phase ≈ 1–2 h (it is longer than you may remember: the
`tools/{ports,x11-port}/src/` wipe means the ~30-package ad-hoc X11 lib stack,
glib2, ncurses, libffi, libiconv, nano and mc all re-extract, re-configure and
rebuild from scratch instead of skipping on an existing `/tmp/x11-phoenix`);
ext2 + image assembly ≈ 5 min.

Watch for, and stop on:

* `archive_fresh: freshness input missing:` — a path in `build-showcase-apps.sh`
  went stale. This now dies instead of silently reusing an archive.
* `MISSING GPU archives after the gpu phase:` — the ports stage is about to fail.
* `PHASE stage finished with N soft failure(s):` — X11 apps / nano / mc are the
  soft tier. Read the list; a soft failure means that binary is **absent** from the
  rootfs, which is the honest outcome now that nothing is copied forward.
* Any `port ... FAILED` in the ports stage — do not proceed; a missing engine now
  really means a missing engine.

Check after:

```
ls -la artifacts/rpi4b/rpi4b-sd-2part.img
ls -la .buildroot/_boot/aarch64a72-generic-rpi4b/part_rootfs.ext2
ls -la tools/.gpu-libs/*.a                     # 3 archives, all dated today
ls .buildroot/_fs/aarch64a72-generic-rpi4b/root/usr/bin | grep -E 'quakespasm|yquake2|quake3e|vkquake|supertuxkart'
```

All five engines must be present **and dated today**. A date older than today means
something was reused — stop and find out what.

### 2b. Re-cut the boot artifacts for the nfsroot variant

Only the plo boot script and the `nfs` server differ between variants; the rootfs
tree is shared. `--skip-prepare` is **mandatory** here (see the ordering rule).

```
./scripts/rebuild-rpi4b-fast.sh \
    --scope project --variant nfsroot --skip-prepare \
    2>&1 | tee -a artifacts/clean-rebuild-2026-09-03.log
```

This runs `build.sh project image`: it rebuilds `nfs` (`build.project:113-116`),
re-renders `user.plo.yaml` with `RPI4B_VARIANT=nfsroot`, rebuilds `loader.disk`, and
assembles the 1-partition FAT `rpi4b-sd.img` used for netboot. It **adds** `nfs` to
`_fs/root` and touches nothing else there.

**Estimated wall clock: 3–8 min.**

Check after:

```
strings .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk | grep -c nfs
ls -la .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk    # dated today
```

---

## 3. Produce the nfsroot rootfs (pristine NFS export)

```
./scripts/make-pristine-nfs-export.sh
```

As of `f9004c5` this resolves the target export from the `fsid=0` line in
`/etc/exports` (currently `/srv/phoenix-rpi4-nfs-gcc16`) or `RPI4B_NFS_EXPORT`, and
**dies rather than guessing**. It previously hardcoded `/srv/phoenix-rpi4-nfs`, so
the pristine tree was swapped into a directory nothing mounts and the Pi went on
booting the stale export.

It builds the new tree **only** from `_fs/<target>/root` — there is no binary and no
game-data copy-forward any more. It prints a junk check and a completeness check,
then swaps: old export → `<export>.PREV-cruft`, new → `<export>`.

**Estimated wall clock: 2–5 min** (it is a ~500 MB local rsync plus two `mv`s).

Check after:

* The completeness list must show `OK` for **all** of: `bin/psh`, `bin/busybox`,
  `usr/bin/Xphoenix`, `bin/xterm`, `bin/wmaker`, the five engines, `bin/ram-stage-play`,
  `usr/bin/quake2`, `usr/bin/quake3`, `bin/stk`, and the four game-data roots.
  **Any `MISS` is a build bug now, not a staging bug** — do not hand-copy it.
* The junk check should print `(clean — no junk at root)`.
* `sudo ls -d /srv/phoenix-rpi4-nfs-gcc16/.mesa-shader-cache` must fail (the cache
  does not survive a pristine rebuild). If it exists, you swapped the wrong dir.

Do **not** run `sync-netboot-tree.sh` before the pristine export; it is additive and
would re-import nothing useful. Note that `netboot-server-up.sh:44` calls it
automatically — that is fine *after* the pristine swap (same source tree), but never
treat it as a way to clean the export.

---

## 4. Produce the SD image

Already produced by step 2a as `artifacts/rpi4b/rpi4b-sd-2part.img` (exported,
sha256-printed and verified by `verify-rpi4b-sdimg.sh` in the same run).

If you need to regenerate it *after* step 2b (which re-cut `loader.disk` for
nfsroot), you must re-run the sd variant — and because of the ordering rule that
means a full `--variant sd --scope project --skip-prepare` pass followed by the
ext2 packer:

```
./scripts/rebuild-rpi4b-fast.sh --scope project --variant sd --skip-prepare
```

**Estimated wall clock: 5–10 min** (project+image+ext2 pack of a ~1.5 GiB volume).

**Recommendation: don't.** Do step 2a (sd) → export the image → step 2b (nfsroot) →
pristine export, in that order, exactly once. Flipping back and forth invites
exactly the "which loader.disk is on the card?" confusion this whole exercise is
about.

---

## 5. Compare the two rootfs images

Both the SD ext2 root and the NFS export are cut from the same
`_fs/aarch64a72-generic-rpi4b/root`, so the shared binaries must be **byte-identical**.
The ext2 image can be read without root using `debugfs`.

```
cd /home/houp/phoenix-rpi
E2=.buildroot/_boot/aarch64a72-generic-rpi4b/part_rootfs.ext2
EXP=/srv/phoenix-rpi4-nfs-gcc16
FS=.buildroot/_fs/aarch64a72-generic-rpi4b/root
mkdir -p /tmp/e2cmp
```

For each binary you care about — do at least `bin/psh`, `bin/busybox`,
`usr/bin/Xphoenix`, and all five engines:

```
debugfs -R "dump /usr/bin/quakespasm /tmp/e2cmp/quakespasm" "$E2" 2>/dev/null
sha256sum /tmp/e2cmp/quakespasm "$EXP/usr/bin/quakespasm" "$FS/usr/bin/quakespasm"
```

All three hashes must match. A whole-tree check (slower, but conclusive) is:

```
diff -r --brief --no-dereference \
    --exclude=dev --exclude=proc --exclude=tmp --exclude=mnt --exclude=var \
    "$FS" "$EXP"
```

**Expected differences — everything else is a bug:**

* `sbin/nfs` (or wherever the `nfs` server installs) exists only after step 2b; if
  you compare before it, `$FS` and `$EXP` may differ by that one file.
* `/dev`, `/proc`, `/tmp`, `/mnt` — recreated empty by `make-pristine-nfs-export.sh`
  and excluded from the sync.
* `/etc/fonts` + the fontconfig cache, staged into the export only by
  `stage-desktop-fonts.sh` via `sync-netboot-tree.sh`. Present on the export, absent
  from `$FS`. **This is a real gap** (see §6) — note it, don't paper over it.
* File ownership: the export sync uses `--no-owner --no-group`.

**Estimated wall clock: 2–10 min** depending on whether you do the whole-tree diff.

For the boot half, compare what is actually on the card against what the build
produced:

```
sha256sum artifacts/rpi4b/rpi4b-sd-2part.img
cmp .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk \
    .buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/loader.disk
```

(The second is the TFTP tree the netboot server serves; it must match the boot
directory the image was assembled from.)

---

## 6. Known residual risks — read before you interpret a failure

Ordered by how likely they are to bite during *this* build.

0. **THE ONE HOLE `--scope full-clean` CANNOT CLOSE: the GPU archives are compiled
   against the toolchain's bundled libphoenix headers, one generation behind.**
   `sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/build-{v3d,gl,v3dv}-phoenix.py`
   invoke `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc` with **no
   `--sysroot`** (`build-v3d-phoenix.py:47`), so ~400 Mesa objects resolve libc
   headers out of `.toolchain/aarch64-phoenix/aarch64-phoenix/include` — the
   hand-maintained bundle, whose only recent contents are two headers somebody
   copied by hand on Sep 1. And the ordering forces it: the gpu phase must run
   *before* `build.sh` (the five game ports `b_die` without the archives), so at
   the moment the archives are built the fresh libphoenix does not exist yet.
   The five game ports then link those archives against the *new* libphoenix.

   This was not introduced by anything in this cleanup — it is how the GPU stack has
   always been built — and it is not fixable from the wrapper (you cannot interleave
   the gpu phase into `build.sh`'s stage list, and adding `--sysroot` to the Mesa
   compile is a ~400-object change nobody has validated). **Mitigation, if this
   integration window changed libphoenix headers or ABI** — the owner's pending
   TD-21 syscall-order revert is exactly such a change — after the first pass
   completes, re-run:

   ```
   ./scripts/build-showcase-apps.sh --phase gpu --force
   ./scripts/build-port.sh quakespasm yquake2 quake3 vkquake supertuxkart
   ```

   so the archives *and* the engines that link them are both built against the
   final libphoenix. The Docker path (§7) is immune: it builds the toolchain from
   the current libphoenix, so its bundle is never stale.

   `archive_fresh()` now takes the built sysroot's `libphoenix.a` as a freshness
   input, so on an *incremental* build a libphoenix rebuild at least invalidates the
   archives. It cannot help on the first pass of a full clean, when no sysroot
   exists yet.

1. **A regenerated port patch does not rebuild its port.** `port.subr:68-74` keys
   re-application on `<basename>.applied` marker files, with no content hash;
   `build_layer.py:245` literally says `# TODO: rebuild on changed patches`. This is
   *neutralised* by `--scope full-clean` (which deletes `port-sources/` wholesale),
   which is precisely why this runbook insists on `full-clean` rather than a
   `--scope core` "clean enough" build. If you later iterate on one port, use
   `scripts/build-port.sh <port>` (clean by default) — never a bare rebuild.
2. **The ports stage is the long pole and it is network-dependent.** x.org's tarball
   CDN has served truncated stubs before and killed a full clean build mid-flight
   (session ~206). If it dies there, the cached tarballs from earlier ports are
   fine; resume with the same `--scope full-clean`? **No** — that would re-nuke
   everything. Resume with `--scope auto --skip-prepare --with-showcase --with-ports
   --with-tests`, which reuses the cleanly-built core and retries only the failed
   ports. Say plainly in the report that the build was resumed, not single-shot.
3. **Soft failures are silent-ish.** `build-showcase-apps.sh` phase `stage` records
   nano/mc/X11-app failures and continues. With the copy-forward removed, a soft
   failure now means the binary is genuinely missing from the export. Read the
   `PHASE stage finished with N soft failure(s)` list; check it against the
   completeness list in §3.
4. **Fontconfig / TTF are staged only into the export**, by
   `stage-desktop-fonts.sh` from `sync-netboot-tree.sh:61`. The SD ext2 root never
   gets them, so the X desktop has no Xft fonts on an SD boot. This is a genuine
   asymmetry, not a staleness bug, and it is out of scope for this runbook — but it
   will show up in the §5 diff and should not be "fixed" by copying files.
5. **`.toolchain/aarch64-phoenix/aarch64-phoenix/{lib,include}` is hand-maintained.**
   `libphoenix.a` there is a `cp` performed by `sync_toolchain_libc`
   (`tools/x11-port/build-x11-phoenix.sh:317-324`), and `include/` holds exactly two
   headers somebody copied by hand (`signal.h`, `string.h`, dated Sep 1). Anything
   compiled **without** `--sysroot` links that bundle. `build-rootfs-helpers.sh` no
   longer does (fixed in `f9004c5`); the `tools/ports/*` and `tools/x11-port/*`
   scripts do pass `--sysroot`, and additionally re-sync the bundle. If you add a new
   ad-hoc compile step, pass `--sysroot=.buildroot/_build/<target>/sysroot/`.
6. **`versioned-ports/` entries are keyed by name+version only** and nothing ever
   prunes one (`candidates.py:174-183`; only `build.sh clean`'s wipe of `_build`
   removes them). `full-clean` handles it; an incremental build never would.
7. **`_boot/host-generic-pc` survives `build.sh clean`** (it wipes `_boot/$TARGET`
   only). The new coord-repo block removes it; if you ever run `build.sh clean`
   directly instead of through `rebuild-rpi4b-fast.sh`, it will not be removed.
8. **The Mesa shader disk cache has no build-id.** Nothing on the host can tell a
   valid blob from a stale one. If the Pi renders green speckle *over an otherwise
   valid frame*, that is a stale blob, not a GPU wedge:
   `sudo rm -rf /srv/phoenix-rpi4-nfs-gcc16/.mesa-shader-cache` and re-run.
9. **Silent patch application in the ad-hoc X scripts.** `build-x11-phoenix.sh:44-50`
   (`apply_patches`) discards both the output and the exit status of `patch` and
   `return 0`s unconditionally; `build-xfbdev.sh:86`, `build-xserver-core.sh:178,183`
   and `build-xedit.sh:73-75` do the same with `|| true`. A rejected hunk is
   invisible and the build proceeds against unpatched source. The `src/` tree wipe
   makes the patches *re-apply* (so a reject becomes reproducible), but it does not
   make it *loud*. If an X behaviour regresses after this build, suspect a silently
   rejected patch first. The correct pattern already exists in the same directory —
   `build-wmaker.sh:284-300` dry-runs then hard-fails — and is the fix if this bites.
10. **The "stale-binary trap" in the small X app scripts.** Seven of them guard the
    build with `if [ ! -x "$XDIR/$APP" ]`, so an existing binary is re-staged rather
    than relinked after a libphoenix change. Two scripts fix it by deleting the
    binary first (`build-xedit.sh:107-112`, `build-mc.sh:203-206`). This is
    neutralised by the `src/` wipe, and most of those apps are framework ports now —
    but never trust an incremental run of `tools/x11-port/build-<app>.sh`.
11. **`build-x11-phoenix.sh` degrades package failures to `echo`** (`:204`, `:213`,
    `:227`, `:241`, `:257`, `:303`, `:340`, `:356`) and `build-xserver-core.sh:186-189`
    tolerates a failed `make` if the archives happen to exist. Read the log for
    `FAIL` lines even when the script exits 0.
12. **HW test still has to happen.** None of the above proves the image boots. Use
    the `rpi4-run` skill / `scripts/test-cycle-netboot.sh`; the runbook stops at
    "artifacts produced and cross-checked".

### Fixed today, but worth knowing they existed

* `tools/python-port/build-curses.sh` wrote `python3` + the whole stdlib **directly
  into the live export** (`/srv/phoenix-rpi4-nfs-gcc16`) with an `rsync` that had no
  `--delete`. It now stages into `_fs/<target>/root` like everything else, which also
  means python3 reaches the SD image for the first time. It is **not** in any
  automatic stage list — run it by hand if you want curses support:
  `SHOWCASE_STAGE_DIR=.buildroot/_fs/aarch64a72-generic-rpi4b/root tools/python-port/build-curses.sh`
  (needs `/tmp/python-port-build`, i.e. run `tools/python-port/build.sh` first — and
  the full-clean wipe removes that tree, so budget ~30–50 min for CPython).
* `tools/x11-port/stage-x11-runtime.sh` hardcoded the pre-rename export path and its
  `cp -a` of the locale DB nested a second copy on every run rather than replacing it.
* There are **three** different NFS-export defaults across `tools/`
  (`/srv/phoenix-rpi4-nfs`, `/srv/phoenix-rpi4-nfs-gcc16`, and a runtime `fsid=0`
  lookup in `build-wmaker.sh:43-44`). Several `tools/ports/build-dillo*.sh` paths
  still `mkdir -p` and write into `/srv/phoenix-rpi4-nfs` unguarded. They are not on
  the showcase stage list any more (dillo is a framework port), but do not invoke
  them by hand during a clean-build validation.

---

## 7. Optional: the authoritative clean build (Docker)

The reproducibility gate is the Docker path, which starts from a blank Ubuntu, clones
every repo, builds the toolchain and produces the SD image with **zero** host state:

```
./scripts/build-sd-in-docker.sh
```

It now refuses to start with a dirty coord/sibling tree (the build clones committed
state only). **Estimated wall clock: 6–10 h** (it also builds the cross toolchain and
re-downloads every port tarball and all game data). Run it only when you want the
release-grade answer; the local `--scope full-clean` path above is what you use to
find and fix breakage first.
