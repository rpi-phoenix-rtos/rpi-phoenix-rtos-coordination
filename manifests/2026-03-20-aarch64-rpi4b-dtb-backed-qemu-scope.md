# Manifest: Pi 4 DTB-Backed QEMU Scope

- Date: `2026-03-20`
- Step: `STEP-0119`
- Status: `completed`

## Goal

- identify the smallest next Pi 4 step after stable loader startup on QEMU `raspi4b`

## Evidence

From the completed `STEP-0118` validation:

- generic `virt` now still reaches:
  - loader startup
  - kernel banner
  - `pl011-tty: started`
- Pi 4 `raspi4b` now reaches only:
  - loader startup
  - `alias: Setting relative base address to 0x0000000000200000`

From the rebuilt artifacts:

- generic QEMU buildroot contains:
  - `_fs/aarch64a53-generic-qemu/root/etc/system.dtb`
- Pi 4 buildroot without an explicit DTB does not contain:
  - `_fs/aarch64a53-generic-rpi4b/root/etc/system.dtb`
- Pi 4 `part_kernel.img` program list in this build therefore contains no `system.dtb` blob entry

From prior Pi 4 project work:

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
  - already supports `RPI4B_DTB_PATH`
  - already copies a supplied DTB into both:
    - the firmware boot tree
    - `/etc/system.dtb` for the loader payload

## Conclusion

- the next smallest Pi 4 blocker is DTB-backed kernel handoff, not another early loader issue
- before adding more loader or kernel instrumentation, the right next step is to validate the existing Pi 4 DTB path on the QEMU lane

## Selected Next Step

- generate a Pi 4 DTB from the current `raspi4b` QEMU model
- rebuild the Pi 4 project with `RPI4B_DTB_PATH` pointing to that DTB
- rerun the `plo.elf` Pi 4 QEMU smoke
- use the result to decide whether the next code step belongs in:
  - Pi 4 project automation
  - generic kernel DTB parsing
  - or Pi 4 MMIO/console handoff
