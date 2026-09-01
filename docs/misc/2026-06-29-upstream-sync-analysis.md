# Upstream sync analysis — 2026-06-29

Goal: pull/integrate clean upstream (origin = phoenix-rtos) changes into the
sibling repos without destabilising the working Pi 4 tree (user is actively
HW-testing mc/xedit/WindowMaker; another agent edits WindowMaker; main session
finishes xedit — all in the coord repo `tools/x11-port`, none touching siblings).

Method: `git fetch origin` (fetch-only, no merge) per sibling, then for each
repo with upstream-exclusive commits compute the **file-level overlap** between
the files touched by `HEAD..origin/master` (upstream's new commits) and the
files touched by `origin/master..HEAD` (our local Pi 4 commits). Disjoint file
sets ⇒ a merge cannot produce a textual conflict (conflicts are per-file).
Confirmed each candidate with `git merge-tree` (zero conflict markers) before
the real `git merge --no-edit origin/master`. Build-validated the whole set with
`./scripts/rebuild-rpi4b-fast.sh --scope core` → **GREEN** (image assembled +
verified, SHA256 556e470a…, zero error lines).

NOT run: `git-pull-upstream-all.sh` (it would attempt merges into the two
repos that carry parked WIP). Per-repo `git -C <repo> merge` only.

## Per-repo summary

| repo | upstream-new | our local commits | overlap | decision | action |
|------|-------------:|------------------:|---------|----------|--------|
| phoenix-rtos-corelibs | 0 | 1 | — | current | none (already up to date) |
| phoenix-rtos-filesystems | 0 | 33 | — | current | none |
| phoenix-rtos-usb | 0 | 32 | — | current | none |
| phoenix-rtos-doc | 3 | 0 | none | **MERGED** (FF) | not compiled |
| phoenix-rtos-tests | 7 | 0 | none | **MERGED** (FF) | not compiled |
| phoenix-rtos-hostutils | 1 | 0 | none | **MERGED** (FF) | host-only tool |
| phoenix-rtos-build | 1 | 12 | none | **MERGED** | sparcv8leon-only, inert for aarch64 |
| phoenix-rtos-ports | 3 | 8 | none | **MERGED** | openiked-only, not in Pi 4 image |
| phoenix-rtos-project | 6 | 155 | none | **MERGED** | riscv/openocd/submodule bumps, inert |
| phoenix-rtos-posixsrv | 3 | 1 | none | **MERGED** | tmpfile.c; in image; build-green |
| phoenix-rtos-utils | 2 | 36 | none | **MERGED** | nandtool + psh echo -n; in image; build-green |
| plo | 2 | 74 | none | **MERGED** | ram-storage bugfix; boot-critical but tiny+orthogonal; build-green |
| libphoenix | 26 | 30 | **5 files** | report-only | pwd/stdio overlap with our X11 libc work |
| phoenix-rtos-kernel | 19 | 229 | **6 files** | report-only | hal/aarch64/pmap.c + interrupts collide with Pi 4 reloc work |
| phoenix-rtos-devices | 14 | 294 | none | report-only | parked SD #154 WIP (uncommitted) |
| phoenix-rtos-lwip | 1 | 142 | none | report-only | parked WiFi #91 WIP (uncommitted) |

## Rollback table (deterministic revert per repo)

Since this work could not be boot-tested (main session owns the Pi), if the next
netboot regresses, revert any merged repo with
`git -C sources/<repo> reset --hard <pre-merge-SHA>`:

| repo | pre-merge SHA (rollback to) | post-merge HEAD |
|------|-----------------------------|-----------------|
| phoenix-rtos-doc | 5083598 | deb40ff |
| phoenix-rtos-tests | 63951d7 | d5d4cb1 |
| phoenix-rtos-hostutils | 2a894a3 | aa0c55a |
| phoenix-rtos-build | aad9a50 | 4ddabee |
| phoenix-rtos-ports | 6b07b95 | 205e4a9 |
| phoenix-rtos-project | 77d8fcd | 53bc2cb |
| phoenix-rtos-posixsrv | ef6e39b | ff04a1b |
| phoenix-rtos-utils | ad9e39a | 92d23e0 |
| **plo** (boot-critical) | **f8ed6aa** | **93881db** |

## Merged repos (build-green-validated)

All merges leave the repo `behind-origin=0`, `dirty=0`. Three were
fast-forwards (doc, tests, hostutils); the rest are merge commits (`ort`).

- **phoenix-rtos-doc** → deb40ff. Doc-only (echo -n, license/$PATH fixes). FF.
- **phoenix-rtos-tests** → d5d4cb1. trunner pytest harness, tmpfile() tests, regex test filter. FF. Not compiled into the Pi 4 image.
- **phoenix-rtos-hostutils** → aa0c55a. ctf_to_proto.py fix. Host tool. FF.
- **phoenix-rtos-build** → 4ddabee (merge). Upstream touched only `target/sparcv8leon.mk` (move SPARC kernel bss/data) — orthogonal to aarch64.
- **phoenix-rtos-ports** → 205e4a9 (merge). Upstream touched only openiked (vroute impl, stack-overflow + pfkey_writev fixes). We don't build openiked for the Pi 4.
- **phoenix-rtos-project** → 53bc2cbd (merge). riscv64-gr765/grfpga DTB-from-source, openocd stm32n6 config, submodule-pointer bumps. The `sources/*` siblings are **not** initialised as submodules here (gitlinks uninitialised; build uses the sibling checkouts directly), so the pointer bumps are inert. No `_targets/aarch64*` or `_projects/*rpi4*` touched.
- **phoenix-rtos-posixsrv** → ff04a1b (merge). Upstream rewrote `tmpfile.c` (open error handling, tmp-dir init, msg.oid file-op forwarding bug). Our local commit touched only `special.c`. Disjoint. Recompiled by `core` scope (`tmpfile.o` + rebuilt `posixsrv` binary confirmed post-merge mtime); build green. **Cross-repo caveat:** the tmpfile work is a coordinated fix (issues #1575/#1417) split across posixsrv + libphoenix + tests; we merged the posixsrv + tests halves but held libphoenix (report-only, below), so the tree now runs new-posixsrv + old-libphoenix. Inspected all three posixsrv commits: they are **posixsrv-internal** (error handling, /var/tmp dir creation, oid-forwarding fix, and dropping `asprintf` for a stack `sprintf`) and do **not** depend on any new libphoenix symbol or changed tmpfile() contract — they fix bugs against the existing protocol. Safe to keep decoupled; the libphoenix tmpfile/mktemp half can land later without re-touching posixsrv. `tmpfile()` is on the user's live test path (mc/editors), so the main session should exercise it once.
- **phoenix-rtos-utils** → 92d23e0 (merge). Upstream: `psh/echo` gains `-n`, `nandtool` realpath resolution. Our 36 commits never touched echo.c/nandtool.c. Recompiled; build green. (Note: psh `echo -n` may slightly change rc-script echo behaviour — cosmetic.)
- **plo** → 93881db (merge). Upstream: `devices/ram-storage/ramdrv.c` — overflow-safe `ramdrv_isValidAddress` + implements `ramdrv_write` (was `-ENOSYS`). `ram-storage` IS in `PLO_ALLDEVICES` for aarch64-generic, so this code is compiled into our bootloader, but we never touched ramdrv.c (no conflict) and the change is a self-contained 13-line bugfix that does not touch any Pi 4 HAL. Boot-critical repo, so validated with `--scope core` → green. **Recommend the main session boot-test once before relying on it** (the rebuild only proves it compiles, not that the loader still boots).

## Report-only repos (NOT merged — attended review required)

### phoenix-rtos-kernel — 19 upstream commits, **6-file overlap**, DO NOT auto-merge
HEAD 9c831508 → origin/master 74714478. Overlapping files (upstream-new ∩ our local):
`hal/aarch64/interrupts_gicv2.c`, `hal/aarch64/pmap.c`, `posix/posix.c`,
`proc/threads.c`, `syscalls.c`, `vm/map.c`. Two upstream commits land directly
on aarch64 and are **genuinely interesting fixes we should want**:
- `a7b0cf11 hal/aarch64: fix _pmap_halMapDevice`
- `3b3704fd hal/aarch64: fix hal_strcpy to copy with null byte`
Plus a generic-HAL device-mapping refactor (`a2e25d82 hal/armv7a: add IO mapping
functionality in HAL` and the imx6ull/zynq7000 conversions to
`_pmap_halMapDevice()`), a new `schedInfo` syscall + `include/sched.h` move
(paired with the libphoenix pthread work below), `vm/map: fix shared maps
allocated calculation`, perf/trace fixes, and `posix: return ECHILD from
multithreaded waitpid`.
Risk: `hal/aarch64/pmap.c` is the heart of our `agent/rpi4-program-reloc` work
(known-good tag `known-good/2026-04-19-map-relocation-complete`). The
`_pmap_halMapDevice` and `hal_strcpy` fixes likely apply cleanly to our tree but
MUST be merged attended, on a branch, with a boot-test. The `vm/map` shared-maps
fix and `waitpid ECHILD` are worth cherry-picking on their own.
**Recommendation:** attended. Cherry-pick the two aarch64 fixes + `vm/map` fix +
`waitpid ECHILD` onto a branch, rebuild `--scope core`, boot-test, then decide on
the full merge.

### libphoenix — 26 upstream commits, **5-file overlap**, DO NOT auto-merge
HEAD 2b8dff5 → origin/master 8ff4671. Overlap: `include/pwd.h`,
`include/stdio.h`, `posix/stubs.c`, `stdio/fprintf.c`, `unistd/pwd.c`. We added
pwd/stdio/wide-char functions during the X11 lib port; upstream has now landed
its own `pwd: implement missing functions`, `mktemp/tmpnam/tempnam`,
`stdio: implement gets()`, `stdio: fix fgets()`, `stdio/printf: add error
propagation`, and a large pthread overhaul (attr get/set scope/guardsize/
inheritsched, stackaddr handling, `schedInfo` syscall use — pairs with the
kernel `sched.h` move above). High probability of **duplicate-symbol / signature
conflicts** in pwd.c and the stdio files if merged naively.
**Recommendation:** attended. Reconcile our X11-port pwd/stdio additions against
upstream's (prefer upstream's, drop our now-redundant shims where they match),
then take the pthread + mktemp work. The pthread changes are valuable for any
future threaded port. Must co-merge with the kernel `sched.h`/`schedInfo` commit.

### phoenix-rtos-devices — 14 upstream commits, no overlap, but **parked WIP**
HEAD db4cab8 → origin/master bccac7e. Working tree is **dirty**:
`storage/bcm2711-emmc/sdcard.c`, `storage/bcm2711-emmc/sdstorage_dev.c`
(uncommitted SD #154 work — OFF-LIMITS). Upstream delta is entirely orthogonal
to BCM2711: STM32N6/imxrt pwm/dma/tty/posixsrv-stack, grlib NAND timeout, ublox
GPS receiver refactor. **Zero file overlap** with our Pi 4 work — this would be a
clean merge **once the SD #154 WIP is committed or stashed by the owning
session**. Defer until that WIP is resolved; then it is a safe merge.

### phoenix-rtos-lwip — 1 upstream commit, no overlap, but **parked WIP**
HEAD 39309b9 → origin/master 83dbab9. Working tree dirty: untracked
`port/wifi-fw-43455.{c,h}`, `port/wifi-nvram-43455.{c,h}` (WiFi #91 — OFF-LIMITS).
The single upstream commit `83dbab9 wi-fi/lwip: fix AP network shutdown` touches
only `wi-fi/lwip/cy_lwip.c` (Cypress/Infineon WiFi glue — not our GENET path, and
not the BCM43455 files we have parked). No overlap. **Trivial, safe merge once
the WiFi WIP is committed/stashed.** Untracked files don't block a merge by
themselves, but per project safety rules we do not disturb a repo carrying parked
WIP. Defer to the owning session.

## Repos already current (nothing upstream)

phoenix-rtos-corelibs, phoenix-rtos-filesystems, phoenix-rtos-usb — `HEAD..origin/master` is empty. We carry local Pi 4 commits ahead of upstream but there is nothing new upstream to pull.

## Notable upstream developments to follow

1. **aarch64 HAL fixes in kernel** (`_pmap_halMapDevice`, `hal_strcpy` null-byte) — directly relevant to the Pi 4; pull attended.
2. **pthread overhaul + `schedInfo` syscall** (libphoenix + kernel, paired) — improves POSIX thread attr support; relevant to any threaded port (Quake/X clients). Co-merge the libphoenix + kernel halves together.
3. **`vm/map` shared-maps allocation fix** and **multithreaded `waitpid` ECHILD** (kernel) — general correctness fixes worth cherry-picking.
4. **tmpfile()/mktemp/tmpnam/tempnam, gets(), fgets() fix** across libphoenix + posixsrv + tests — userspace libc maturity; posixsrv half already merged here, libphoenix half pending the attended reconcile.
5. **plo ram-storage now supports write** — minor, already merged.
