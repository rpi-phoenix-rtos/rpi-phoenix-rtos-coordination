# rpi4b → standard rc.psh + RAM-rootfs conversion plan (2026-05-31)

User-authorized direction: align the Pi 4 boot with the standard Phoenix init model
(`psh -i /etc/rc.psh` + a real `/` rootfs) to fix device-node visibility (#123) and
enable busybox/userspace (#118+). Rollback point: `manifests/2026-05-31-usb-stack-done-busybox-builds.md`.

## Corrected diagnosis (strong source evidence — supersedes earlier theories)

Earlier theories (posixsrv per-process /dev view; "devfs-as-root") are **REFUTED**:
- The kernel namespace is **global, not per-process**: `rootOid`/`devfsOid`/`dcache` are
  kernel-wide; `lookup("devfs")` short-circuits to the global `devfsOid` for ALL processes
  (`phoenix-rtos-kernel/proc/name.c:109-171,236-268`). So devfs nodes are already globally
  reachable by name — there is no per-process namespace to fix.
- `dummyfs;-N;devfs;-D` registers a port literally named `devfs` (not `/`)
  (`phoenix-rtos-filesystems/dummyfs/srv.c:187-190,246-257`). On standard targets `/` is
  registered by a storage/FS server (pc-ata `atasrv.c:687`, etc.); rpi4b uses dummyfs-root as
  `/` + devfs bound at `/dev` — the SAME runtime shape as standard targets.

**Real root cause = startup RACE.** The kernel spawns all syspage programs concurrently
(`kernel/main.c:67-126`; plo `app` line order is NOT exec order, `docs/knowledge-status.md:117`).
`create_dev` retries `lookup("devfs")` 3×100ms (`libphoenix/unistd/file.c`), but **`bind devfs
/dev` does NOT retry** — on a lookup miss it returns -ENOENT and exits (`phoenix-rtos-utils/psh/
bind/bind.c:42-60`). When `bind` loses the race, `/dev` never gets the `atDev` mount attr, so
create_dev nodes (live in devfs, global) are unreachable via the `/dev` path, while posixsrv's
`mkdir /dev/posix` etc. land in the root dummyfs's `/dev` and ARE visible. This exactly matches:
diag123 `access("/dev/kbd0")=ENOENT` even in-process, `access("/dev")=0`, kbd0+pl011-tty missing,
framework `symlink("/dev/usb-...")` visible. The standard `rc.psh` model fixes it via `W`
(wait-for-exit) sequencing of `bind` before the device servers.

## Rootfs delivery (no new driver): rofs RAM image
rpi4b has no block driver (SD/EMMC2 = #119). Use **rofs** (`phoenix-rtos-filesystems/rofs/`),
which mmaps a read-only image from a physical address (`rofs/rofs.c:212`, MAP_PHYSMEM), built by
the `mkrofs` hostutil (`phoenix-rtos-build/build.sh:241`), loaded into RAM via a plo `blob` line
(like the existing `/etc/system.dtb` blob; `plo/cmds/blob.c:99-109`). ONE code edit needed:
rofs's `portRegister("/")` root path is `#if 0`'d (`rofs/srv.c:123-134`) — enable it for the
`address:/` mountpoint case.

## Phased plan (each step independently bootable; ONE boot per step — netboot is flaky)

**Phase 0 — confirm the race (cheap, 1 boot, no rootfs).** Make `bind devfs /dev` win the race
and check a *different* process sees the nodes. NOTE: verify whether a plo `wait` between `app`
lines actually affects the KERNEL's concurrent spawn (it may not — apps are packed to syspage and
spawned together). If plo `wait` is ineffective, the cleaner decisive test is to **patch
`bind.c` to retry the lookups 3×100ms like create_dev** (also a good upstreamable robustness fix),
rebuild, boot, then from psh `ls -l /dev` — if kbd0/pl011-tty become visible to psh, the race is
confirmed. If still missing, STOP and re-investigate create_dev's nodevfs/portRegister fallback
before the rootfs work.

**Phase 1 — build + load the RAM rootfs (old launch model intact, safe).**
1. New `_projects/aarch64a72-generic-rpi4b/rootfs-overlay/etc/rc.psh` (model on ia32/zynqmp):
   `W /bin/bind devfs /dev` ; `W /sbin/dummyfs -m /tmp -D` ; `X /bin/posixsrv` ;
   `X /sbin/usb --bridge-only` ; `X /sbin/lwip genet:0xFD580000:189:190:PHY:bcm54213pe:0.1:irq:MAC` ;
   `T /dev/console` ; `X /bin/psh`. (usb bridge-only MUST precede lwip — embedded USB ordering,
   memory:pi4-xhci-crcr-stale-after-hcrst.)
2. In `build.project` `b_image_project`: run `mkrofs` over `$PREFIX_ROOTFS` → `rootfs.rofs`
   (mirror `_targets/riscv64/generic/build.project:53-66`); pack as a blob in user.plo.yaml.
   Verify busybox + servers (psh/posixsrv/dummyfs/lwip/usb/pl011-tty) are in PREFIX_ROOTFS
   /bin,/sbin. Watch loader.disk size vs the 0x08000000 window (`build.project:225-229`).
3. Add `blob ... rootfs.rofs ddr` to user.plo.yaml WITHOUT launching rofs; boot once; confirm
   it appears in `main: Starting syspage programs:` and the old boot (USB/GENET) still works.

**Phase 2 — rofs as `/` + cutover (highest risk).**
3. Enable rofs root-register (`rofs/srv.c:123-134` `#if 0` → portRegister("/") for `addr:/`);
   plumb the blob's physical address as the rofs `address` arg.
4. Rewrite user.plo.yaml to standard shape: `kernel` ; `blob /etc/system.dtb` ; `blob rootfs.rofs` ;
   `app rofs;<addr>:/` ; `app dummyfs;-N;devfs;-D` ; `app pl011-tty` ; `app psh;-i;/etc/rc.psh` ;
   `go!`. Drop dummyfs-root, mkdir/dev, bind, posixsrv, usb, lwip plo lines (now in rc.psh) +
   the `fastlane_stageRootDummyfs`/`fastlane_stagePshApplets` shims. KEEP: DTB blob+RPI4B_HAVE_DTB,
   armstub, kernel8 reloc, firmware staging, config.txt.
   **USB/GENET regression gate:** verify bridge-only→lwip order in rc.psh + enumeration matches
   known-good. Verify `ls -l /dev` from psh shows console/tty/kbd0 (the #123 fix) + busybox runs.

## Risk: Step-4 cutover highest (root provider + init + USB ordering at once). Phases 0–1 de-risk
each piece. rofs wrong-address = `/` fails + psh root lookup hangs. busybox rootfs may overflow the
RAM window — trim busybox_config if so. Full source citations in the planning-agent output
(this session's transcript).
