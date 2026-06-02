# Post-USB roadmap: real user-space (posixsrv → ports → SD rootfs)

Date: 2026-05-30. Synthesis of two research subagents. **Priority: do this AFTER USB
is finished.** WiFi (task #91) is deprioritized — with Ethernet (working) + USB (in
progress) we have enough connectivity; WiFi can wait. Goal of this roadmap: run real
software (busybox, eventually micropython/lighttpd) on the Pi 4 and ultimately boot
into a persistent writable filesystem.

Tracked as tasks #117–#120 (see below). All claims verified against the local tree by
the subagents; file:line cited there.

## Current state that shapes the plan
- The rpi4b boot is **syspage-only**: every user program is loaded by plo from
  `loader.disk` as an `app … ddr ddr` line in
  `_projects/aarch64a72-generic-rpi4b/user.plo.yaml`. There is **no mounted rootfs**;
  dummyfs gives an in-RAM `/`. The image disk contents are derived directly from the
  plo `app` directives (`image_builder.py`), so adding an `app` line both launches a
  program AND bundles it.
- The FAT boot partition is **read-only** (`fat/libfat.c:97` → `-EROFS`) and there is
  **no BCM2711 SD/EMMC2 block driver** in `phoenix-rtos-devices/storage/` — so the SD
  card cannot yet be a writable runtime filesystem.
- `poll()`/`select()` are kernel-native (work today); pipes / `/dev/null` / ptys are
  NOT present today (need posixsrv).

## Phase A — launch posixsrv (1-line, unblocks everything) — task #117
posixsrv is already built + staged but not launched. Add to `user.plo.yaml`, after
`bind devfs /dev` and before `psh`:
```
  - app {{ env.BOOT_DEVICE }} -x posixsrv ddr ddr
```
Provides: anonymous pipes (`pipe()` is `-ENOSYS` without it), `/dev/null|zero|urandom|
full`, pseudo-terminals, `tmpfile`, event queues. Prerequisite for the ports.
Ordering matters (needs dummyfs-root + devfs bound first). Low-risk, no new kernel work.

**DONE 2026-05-31** (phoenix-rtos-project 9b45b97). Launched, HW-verified stable.
Validation: `ls /dev` at `(psh)%` lists posixsrv's nodes (`event posix pts`); and
psh now takes interactive terminal input (before posixsrv it printed "failed to take
terminal input" — it had no pty; posixsrv's `/dev/pts` provides one). NOTE: the
originally-planned `ls | cat` pipe smoke test does NOT work, but for an unrelated
reason — *psh's shell parser doesn't implement the `|` operator* (it passes `|` and
`cat` as literal argv to `ls`), independent of posixsrv's `pipe()` syscall. Use the
`/dev`-nodes + interactive-input checks above as the posixsrv smoke test instead.

## Phase B — busybox in a RAM-loaded rootfs (no new driver) — task #118
First real port. busybox is arch-generic and the right first target (micropython has
NO aarch64 branch yet; lighttpd needs pcre+openssl and is heavier — both later).
Steps: add `_projects/aarch64a72-generic-rpi4b/ports.yaml` (`ports: [ {name: busybox} ]`);
Pi4 busybox config + `BUSYBOX_CONFIG` export in the project `build.project`; a
host `genext2fs`/`mkrofs` step in `b_image_project` to build a small rootfs image from
`PREFIX_ROOTFS`; load it to DDR via a `blob` line (like the DTB) + mount via the
`phoenix-rtos-filesystems/ext2` server; switch `psh` to `psh -i /etc/rc.psh` with a
Pi4 `rootfs-overlay/etc/rc.psh` that mounts a writable `dummyfs /tmp` and runs busybox.
Result: interactive busybox shell, run programs, create/edit/delete files in `/tmp`
(ephemeral until power-off). Model: `_targets/ia32/generic/build.project` (ext2
rootfs) + `_projects/armv7a7-imx6ull-evk` (ARM rootfs-mount + rc.psh).

## Phase C — BCM2711 EMMC2 SD block driver — task #119
The blocker for a PERSISTENT writable filesystem. Write an SDHCI-style block server in
`phoenix-rtos-devices/storage/` for the BCM2711 EMMC2 (Arasan) controller. Model:
`storage/zynq7000-sdcard`. Expect the same non-coherent-A72 DMA/cache-coherency class
of problems the USB/PCIe work fought. NOTE: this is a DIFFERENT controller from the
BCM43455 WiFi SDIO instance — do not conflate.

## Phase D — persistent SD rootfs — task #120
With the EMMC2 driver: add a second SD partition (ext2 or littlefs — both writable;
FAT is read-only so unsuitable) after the FAT boot partition (extend
`scripts/assemble-rpi4b-sdimg.sh`); launch the EMMC2 driver from `user.plo.yaml`; mount
the partition r/w at `/`; run busybox/lighttpd from it. This is the "boot from SD into
a writable FS that persists across reboot" end goal.

## Risks
- Image-size budget: loader window is 32 MB (~28 MB free after kernel); fine for
  busybox (~1 MB) + small rootfs, tighter for lighttpd+openssl+pcre.
- Missing/partial syscalls: trim busybox applets to those that work; posixsrv covers
  the common POSIX gaps.
- Phase C/D DMA coherency on the BCM2711 fabric (recurring Pi 4 theme).
