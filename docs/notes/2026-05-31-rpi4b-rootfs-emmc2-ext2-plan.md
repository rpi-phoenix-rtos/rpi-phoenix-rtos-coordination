# rpi4b rootfs: best-practice EMMC2-SD + ext2 (supersedes the rofs-RAM idea)

User guidance (2026-05-31): follow Phoenix's existing patterns for CAPABLE targets
(x86/x86_64-like) — a real block device + real on-disk filesystem mounted as `/` —
rather than inventing a bespoke RAM-rootfs/rofs-blob scheme. Source research confirms
the idiomatic path. This supersedes the rofs approach in
`2026-05-31-rpi4b-rcpsh-rootfs-conversion-plan.md` (whose #123 fix is already done).

## Two things to keep separate (both already correct on rpi4b)
- **Program binaries** live in RAM/syspage on EVERY Phoenix target (PLO `app ... ddr ddr`),
  incl. x86. rpi4b already does this. KEEP.
- **What serves `/`**: either the blessed DISKLESS root (`dummyfs` with no mountpoint ->
  `portRegister(port,"/")`, dummyfs/srv.c:235-245; exposes syspage bins under /syspage,
  srv.c:48-70) — used by zedboard/zturn/upstream-aarch64a72-generic AND current rpi4b
  (dummyfs-root) — OR a block-device on-disk FS (capable targets). The rpi4b RAM root is
  idiomatic, NOT a hack; keep it as the interim/bring-up root until the SD driver lands.

## Capable-target on-disk rootfs pattern (the template to mirror)
All block storage servers use `phoenix-rtos-corelibs/libstorage` (storage_init /
storage_registerfs / storage_mountfs) + a filesystem lib (libext2 =
phoenix-rtos-filesystems/ext2), then `portRegister(oid.port,"/",&oid)`.
- **Runtime/driver template = zynq7000-sdcard** (`storage/zynq7000-sdcard/sdstorage_srv.c`):
  storage_init(:382) + registerfs("ext2",libext2_storage_mount,:394) + mountRootFs
  (:308-328 -> storage_mountfs -> portRegister("/") :327); root device/fs from `-r
  <dev>:<fsname>` arg (:280-289). (NB: no in-tree project actually boots root from this
  driver — it's the driver model, not an image-build model.)
- **Image-build template = ia32-generic** (`_targets/ia32/generic/build.project:121-144`):
  `genext2fs -b <kb> -i 2048 -d "$PREFIX_ROOTFS" rootfs.ext2` bakes the whole rootfs-overlay
  (incl. /etc/rc.psh, /bin/*, busybox) into the ext2 image; MBR hand-built; image_builder.py
  stitches partitions per `nvm.yaml` (rootfs at offs, type ext2). pc-ata launched from
  user.plo.yaml BEFORE psh registers "/" (atasrv.c:655-690), so rc.psh (on the ext2 image)
  only mounts `dummyfs -m /tmp` + `devfs /dev`, NOT `/`.

## Recommended path (= tasks #119 then #120)
**#119 — BCM2711 EMMC2 SD-card block driver.** New server under
`phoenix-rtos-devices/storage/` (e.g. bcm2711-emmc): mirror sdstorage_srv.c's libstorage +
libext2 + portRegister("/") + `-r /dev/mmcblk0p2:ext2`. The CONTROLLER register layer is new
BCM2711 EMMC2 (Arasan) work — NOT derivable from the BCM43455 WiFi SDIO (different controller;
WiFi SDIO is PIO-not-DMA, [[project-wifi-pi4-sdio-pio-not-dma]]).
**#120 — ext2 rootfs on SD + mount /.** Build steps:
1. Install `genext2fs` on the host (NOT present; only mkfs.ext2 is). Prereq.
2. rpi4b build.project: `genext2fs -b <size> -i 2048 -d "$PREFIX_ROOTFS" part_rootfs.ext2`
   (copy ia32 build.project:121-124). PREFIX_ROOTFS already = full tree (psh + ~40 applets,
   servers, /etc; busybox when built --with-ports).
3. `scripts/assemble-rpi4b-sdimg.sh`: add a 2nd MBR partition (sfdisk `type=83`) after the FAT
   boot part (currently sector 2048, type c, +2048 tail at :49-52) and `dd` the ext2 image in.
4. Add a rootfs partition to a nvm.yaml (model ia32 nvm.yaml:13-17).
5. user.plo.yaml: `app ... -x bcm2711-emmc;-r;/dev/mmcblk0p2:ext2 ddr ddr` BEFORE psh; then
   `psh -i /etc/rc.psh`. rootfs-overlay/etc/rc.psh mounts dummyfs /tmp + devfs /dev (ia32
   rootfs-overlay/etc/rc.psh model), NOT /.
6. KEEP dummyfs-root as interim root until the EMMC driver is proven (can run both, flip when
   stable). Netboot of kernel+syspage stays — ORTHOGONAL to the rootfs mount.

## Dropped: rofs-RAM-blob. busybox (#118) runs from the ext2-on-SD rootfs (proper way), not a
RAM rofs. (mkrofs/rofs remains a legit read-only-image tool but no target uses it as root.)

## Gotchas
- EMMC2 != BCM43455 WiFi SDIO (different controller). Non-coherent A72 DMA -> explicit cache
  maintenance / non-cacheable DMA buffers (same class as USB DMA-write-loss/SError) — likely
  source of silent block corruption; budget for it. genext2fs missing on host.
- Risk/sequencing: USB stack is done+stable; #123 fixed. #119 (EMMC2 driver) is the bulk of the
  remaining userspace effort and is a substantial NEW driver — consider whether to ship the USB
  milestone first. Rollback: manifest 2026-05-31-usb-stack-done-busybox-builds.md.
