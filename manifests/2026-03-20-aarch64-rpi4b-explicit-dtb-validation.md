# Manifest: Pi 4 Explicit-DTB Validation

- Date: `2026-03-20`
- Step: `STEP-0120`
- Status: `completed`

## Goal

- prove or reject explicit DTB-backed kernel handoff as the next Pi 4 QEMU blocker

## Validation Notes

Environment:

- `phoenix-dev`
- copied buildroot:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0120-rpi4b-dtb`
- Pi 4 DTB input used for this step:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0109/_boot/aarch64a53-generic-rpi4b/rpi4b/bcm2711-rpi-4-b.dtb`

Important QEMU constraint discovered during this step:

- VM-local QEMU `10.2.2` `raspi4b` does not support `dumpdtb`
- direct attempt:
  - `qemu-system-aarch64 -machine raspi4b,dumpdtb=...`
- result:
  - `This machine doesn't have an FDT`
- implication:
  - Pi 4 QEMU validation currently needs an explicit external DTB source

Build validation:

- rebuilt:
  - `RPI4B_DTB_PATH=... TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- confirmed output now includes:
  - `_fs/aarch64a53-generic-rpi4b/root/etc/system.dtb`
  - `blob ram0 system.dtb ddr` in the loader payload

## Runtime Result

Pi 4 `raspi4b` `plo.elf` smoke now reaches:

- `Phoenix-RTOS loader v. 1.21 ...`
- `hal: Cortex-A53 Generic`
- `cmd: Executing pre-init script`
- `alias: Setting relative base address to 0x0000000000200000`
- `pl011-tty: started`

Repeated longer runs still do not reach:

- kernel banner on UART
- `pl011-tty: tty0 ready`
- `pl011-tty: console ready`

## Conclusion

- DTB-backed handoff is necessary and immediately improves the Pi 4 QEMU lane
- with an explicit DTB, the Pi 4 lane now reaches the same practical userspace boundary as the generic fast lane:
  - `pl011-tty: started`
- the next fast-lane blocker is no longer missing `system.dtb`

## Selected Next Step

- compare the current Pi 4 and generic post-`pl011-tty: started` boundary and choose the smallest next step toward an interactive console
- the leading candidate is now shared console-readiness work around `pl011-tty` device registration rather than another Pi 4 DTB change
