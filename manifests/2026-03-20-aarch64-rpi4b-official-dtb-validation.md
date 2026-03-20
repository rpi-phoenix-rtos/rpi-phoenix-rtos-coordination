# Manifest: Pi 4 QEMU Validation With an Official Firmware DTB

- Date: `2026-03-20`
- Step: `STEP-0164`
- Status: `completed`

## Goal

- determine whether replacing the current 274-byte stub Pi 4 DTB with an official Raspberry Pi firmware DTB changes the `raspi4b` QEMU lane boundary

## External Source

- Repository: `https://github.com/raspberrypi/firmware`
- Commit: `63ad7e7980b030cb4649ecedf2255c9226e5a1e8`
- DTB path: `boot/bcm2711-rpi-4-b.dtb`
- DTB size:
  - `56373` bytes
  - `file` reports:
    - `Device Tree Blob version 17, size=56373, boot CPU=0, string block size=4901, DT structure block size=51400`

## Changes

No code changes.

The validation replaced the previous stub DTB input with the official Raspberry Pi firmware DTB via:

- `RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb`

## Validation

Environment:

- `phoenix-dev`
- copied buildroot:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0164`
- QEMU `10.2.2`

Build validation:

- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Runtime validation:

1. Pi 4 `raspi4b`

   - now shows only:
     - `hal: entry EL3`
     - `Phoenix-RTOS loader v. 1.21`
     - `hal: Cortex-A53 Generic`
     - `cmd: Executing pre-init script`
     - `alias: Setting relative base address to 0x0000000000200000`
   - and then times out without reaching the previous later boundary:
     - no `pl011-tty: started`
     - no `pl011-tty: register tty0`
     - no `pl011-tty: tty0 lookup retry`

Generated Pi 4 scripts from the validated build:

- pre-init script:
  - `map ddr 0x40000000 0x7fffffff rwx`
  - `phfs ram0 4.0 raw`
  - `alias -b 0x200000`
  - `alias user.plo 0x200000 0x1000`
  - `call ram0 user.plo dabaabad`
- generated `user.plo` begins with:
  - `kernel ram0`
  - `blob ram0 system.dtb ddr`
  - `app ram0 -x dummyfs;-N;devfs;-D ddr ddr`
  - `app ram0 -x pl011-tty ddr ddr`
  - `app ram0 -x psh ddr ddr`
  - `go!`

## Conclusion

- the official firmware DTB materially changes the Pi 4 QEMU boundary
- the previous 274-byte stub DTB was masking an earlier loader-side blocker
- the current Pi 4 lane now stalls inside the pre-init `call ram0 user.plo` path or immediately after it starts executing the user script
- the next bounded step should add filtered visibility in the loader call / script-execution path so the Pi 4 lane can be split into:
  - cannot open `user.plo`
  - cannot read `user.plo`
  - begins executing `user.plo` but stalls on a specific command such as `kernel ram0`

## Selected Next Step

- scope the smallest Pi 4 loader user-script visibility step
