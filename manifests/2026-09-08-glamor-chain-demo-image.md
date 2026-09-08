# Integration State: 2026-09-08-glamor-chain-demo-image

## Summary

- Date: 2026-09-08
- Note: SD image carrying the glamor DestroyPixmap chain fix (bug #4). Deltas vs the 21-boot-gated 0ddcee67 image are exactly 4 artifacts, each HW-verified: Xphoenix-glamor-daemon (fix, 3/3 trials 0 faults + desktop pixel-verified), the xlaunch launcher x3 copies (adds the test-only --quit-after flag), python3 and micropython (rebuilt; verified this session). NOT re-gated over SD boot: no SD card in the host reader.
- Generator: scripts/snapshot-integration-state.sh

## Image artifact

| field | value |
| --- | --- |
| path | `artifacts/rpi4b/rpi4b-sd-2part.img` |
| size | 1680867328 bytes |
| sha256 | `486acda56cc9f290cc5104d0db94db247477246e49c6cd72545d6ad0577b2642` |
| rootfs | ext2, 881 MiB volume, 652 MiB used, 179 MiB free |
| variant | `sd` (2-partition: 64 MiB FAT boot + ext2 root) |

Flash with:

```
udisksctl unmount -b /dev/sda1 2>/dev/null || true
sudo dd if=artifacts/rpi4b/rpi4b-sd-2part.img of=/dev/sda bs=4M conv=fsync status=progress
sync
```

### Verification status — read this before flashing

**Verified:** the four artifacts that differ from the previously-gated image
(`0ddcee67…aaaa91e`, 21 boots / 0 faults) are each HW-verified, and the image's copies are
byte-identical to the copies that were tested:

| artifact | evidence |
| --- | --- |
| `bin/Xphoenix-glamor-daemon` | the bug-#4 fix; 4 netboot trials, 0 faults, full X teardown, desktop pixel-verified (wmaker+xterm+xclock+xcalc+xlogo) |
| `bin/startx`, `bin/startx_gpu`, `bin/pl_phoenix_xlaunch` | one launcher binary in 3 copies; adds the test-only `--quit-after` flag, exercised in all 4 trials |
| `bin/python3` | `pycheck` on HW: 3.14.4, zlib/hashlib/json/math/os all OK, no `create_gil` failure |
| `bin/micropython` | `mpcheck` on HW: OK |

Every other file in `/bin` (164) and `/sbin` (19) is **byte-identical** to the gated image —
same file sets, no additions or removals — so the five game engines and their data are
unchanged and carry their existing gate.

⚠️ **NOT verified by booting: the SD path itself.** There was no SD card in the host reader,
so this image was never flashed or booted; the runtime evidence above is netboot on
byte-identical binaries. QEMU cannot substitute — the `raspi4b` lane loads `plo.elf` directly
and does not emulate the VideoCore firmware → FAT handoff.

**Structurally verified instead** (2026-09-08), which bounds the flash risk:

| check | result |
| --- | --- |
| `verify-rpi4b-sdimg.sh` | OK |
| partition table | p1 FAT32 64 MiB bootable @2048; p2 Linux @135168 |
| FAT boot contents | `phoenix-armstub8-rpi4.bin`, `kernel8.img`, `start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb`, `overlays/`, `config.txt`, `loader.disk` — complete |
| `config.txt` | `armstub=`, `kernel=kernel8.img`, `initramfs loader.disk 0x08000000`, `enable_uart=1` |
| `loader.disk` variant | SD (no `nfs;/` syspage entry), syspage carries `bcm2711-emmc;-r;/dev/mmcblk0p2:ext2` |
| root partition | that `mmcblk0p2` is this image's ext2 partition |
| `e2fsck -fn` on p2 | **clean**, all 5 passes, 10496 files, 799583/1033998 blocks |

So the boot chain is structurally sound and the rootfs is fsck-clean; what remains unproven is
only the firmware/EMMC2 handoff on real hardware, which is unchanged code from the image that
booted 21 times.

⚠️ The rootfs volume is 881 MiB here vs ~1.5 GiB in the gated image (the volume is now sized
from content — see `scripts/build-rpi4b-rootfs-ext2.sh`). Free space on the running Pi drops
from ~850 MiB to ~179 MiB. Pass `RPI4B_ROOTFS_BLOCKS=1572864` to restore the old size.

## Repositories

| Repository | Branch | Commit SHA | Remote |
| --- | --- | --- | --- |
| _build | main | c406f3990 (dirty(1)) | https://github.com/houp/phoenix-rpi.git |
| libphoenix | master | 2ec0497 (clean) | https://github.com/phoenix-rtos/libphoenix.git |
| phoenix-rtos-build | master | a80c1fe (clean) | https://github.com/phoenix-rtos/phoenix-rtos-build.git |
| phoenix-rtos-corelibs | master | c863625 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-corelibs.git |
| phoenix-rtos-devices | master | e371967 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-devices.git |
| phoenix-rtos-doc | master | d4419df (clean) | https://github.com/phoenix-rtos/phoenix-rtos-doc.git |
| phoenix-rtos-filesystems | master | be68a10 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-filesystems.git |
| phoenix-rtos-hostutils | master | 49a1fd9 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-hostutils.git |
| phoenix-rtos-kernel | master | 76e0adbc (clean) | https://github.com/phoenix-rtos/phoenix-rtos-kernel.git |
| phoenix-rtos-lwip | master | 492b20b (clean) | https://github.com/phoenix-rtos/phoenix-rtos-lwip.git |
| phoenix-rtos-ports | master | 21bd0ec (clean) | https://github.com/phoenix-rtos/phoenix-rtos-ports.git |
| phoenix-rtos-posixsrv | master | 8a44ce8 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-posixsrv.git |
| phoenix-rtos-project | master | f257a5f (clean) | https://github.com/phoenix-rtos/phoenix-rtos-project.git |
| phoenix-rtos-tests | master | 291708a (clean) | https://github.com/phoenix-rtos/phoenix-rtos-tests.git |
| phoenix-rtos-usb | master | d592025 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-usb.git |
| phoenix-rtos-utils | master | 57cbf33 (clean) | https://github.com/phoenix-rtos/phoenix-rtos-utils.git |
| plo | master | e815446 (clean) | https://github.com/phoenix-rtos/plo.git |

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
_build	c406f39903a3783ed8fb29da39bbf0100c09e532	main
libphoenix	2ec049762775c1f3041d5c7f2aa0a31fc1ce3e1a	master
phoenix-rtos-build	a80c1fe7edd6f4cc4aa7f128c9e5c5caacd3fea6	master
phoenix-rtos-corelibs	c863625b89c9b38ac6a6dde29e06d236de451c7f	master
phoenix-rtos-devices	e371967642f48a7a8286005d0e663e0fa7f6354c	master
phoenix-rtos-doc	d4419dfae5428cb3b8081404c34b12c78c86770d	master
phoenix-rtos-filesystems	be68a10c5f49a7e84eeeec255576994cd2bd2270	master
phoenix-rtos-hostutils	49a1fd996e5745a19cc7ec0b22179bd1e90906cf	master
phoenix-rtos-kernel	76e0adbc71d428d5aa279688ea64072cd0853715	master
phoenix-rtos-lwip	492b20badec9ea1132c7ce47cfabcd9d48a2ee1b	master
phoenix-rtos-ports	21bd0ecec38caeea08f793879b4e4eff48dbc73b	master
phoenix-rtos-posixsrv	8a44ce8eb9851e3bac36e36165e26ca8e879530f	master
phoenix-rtos-project	f257a5f24890d66551ef180f5d4795f0b06baf40	master
phoenix-rtos-tests	291708ac024431123ce46c8a1c1e7f1f1f91248a	master
phoenix-rtos-usb	d592025f0706f3302bea93ec2c629435aa1f125d	master
phoenix-rtos-utils	57cbf332e295b0a85baff0922ba081d2cbdf4bf6	master
plo	e815446b11e78fd2c55186a673e933374621d355	master
```
