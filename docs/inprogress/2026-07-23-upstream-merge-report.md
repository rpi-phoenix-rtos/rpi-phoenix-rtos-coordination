# Upstream-merge tracking pass (2026-07-23, autonomous)

Task: pull + merge canonical upstream (origin = phoenix-rtos) into the sibling
forks, rebuild, basic netboot test. Ran AFTER the clean-build release gate went
green (see 2026-07-23-with-ports-clean-build-grind.md).

Rollback anchor for the pre-merge green state: `manifests/2026-07-23-clean-build-green.md`.

## What was behind upstream (read-only assessment)
Only 6 forks behind; the highest-risk forks (kernel, lwip) were already current:
- libphoenix behind 5, devices behind 22, project behind 8, ports behind 1,
  tests behind 2, utils behind 2. (build/corelibs/doc/filesystems/hostutils/
  kernel/lwip/posixsrv/usb/plo: behind 0.)

## Merge result (`scripts/git-pull-upstream-all.sh`)
15 / 16 repos merged origin/master -> master CLEAN (helper aborts conflicts
non-destructively, pushes nothing). Forks with NEW local merge commits:
- libphoenix   -> 4ded12f  (5 upstream commits; on top of cbrt 2457b17)
- phoenix-rtos-devices -> bff0e89  (22 upstream commits)
- phoenix-rtos-ports   -> ba1dd8e  (1)
- phoenix-rtos-tests   -> 19b11eb  (2)
- phoenix-rtos-utils   -> 5e9e3c9  (2; auto-merge in psh/pshapp/pshapp.c)
(Others were already up to date — no new merge.)

CONFLICT (1): phoenix-rtos-project. ALL conflicts are SUBMODULE GITLINK conflicts
(libphoenix, devices, filesystems, kernel, tests, usb, utils, plo) — upstream
advanced the gitlinks to upstream SHAs while we point at our fork tips. No source
conflicts. Merge was ABORTED (project restored to e4d2236). This is tractable
(resolve each gitlink to our current fork HEAD) but is exactly the gitlink-bump
work being HELD for the user + not needed for the build (peer clones, not project
submodules, are what rebuild-rpi4b-fast.sh uses).

## Validation
- Rebuild `--variant netboot --scope core`: GREEN, loader.disk built, image verified.
  Recompiled the merged devices/libphoenix + psh (pshapp.c) — no build regression.
- Netboot boot test (rpi4b-uart-...-netboot-upstream-merge.log): reached `psh prompt`,
  `genet link up`, `netif has IP` (10.42.0.12), **0 faults**. The trailing
  `nfs-smoke: FAIL open /etc/hostname (NFS4ERR_NOENT)` is a known NFS-export-content
  quirk (export lacks that file), NOT a merge regression.

## HELD FOR USER (not done autonomously — needs coordination)
1. **Push the merged forks** to the rpi-phoenix-rtos org (re-publishing upstream
   merges = new work). Forks: libphoenix, devices, ports, tests, utils. NOT pushed.
2. **Resolve phoenix-rtos-project** submodule-gitlink conflict (repoint each gitlink
   to the merged fork HEAD) + push, once the forks above are pushed.
3. **Optional deeper validation before push:** a full `--with-showcase --with-ports`
   rebuild of the merged tree (this pass validated core via --scope core; the ports/
   tests merges (1+2 commits) were not rebuilt with --with-ports). devices merged 22
   upstream commits — a basic netboot exercises kernel/genet/uart/libphoenix but not
   every driver (USB/SD/etc.), so consider a fuller soak before relying on it.

Local merge commits are recoverable/discardable; nothing was pushed. To discard and
return to the pre-merge green state, restore from the rollback manifest above.
