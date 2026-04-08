# 2026-04-08 Pi 4 Armstub Rebuild

## Scope

Close the next smallest real-hardware handoff step after the earlier
firmware-default placement experiment turned out not to be internally coherent:

- restore the last coherent high-DDR `plo` placement for the Pi 4 A72 path
- add a Pi-4-specific `armstub8-rpi4`-style handoff stub to the boot image
- stage that stub through `config.txt`
- rebuild and revalidate the generic and Pi 4 QEMU lanes
- export a fresh Pi 4 SD-card image for the next real board retry

## Trigger

The earlier low-placement experiment from
`2026-04-08-pi4-firmware-placement-rebuild.md` was disproved by the next QEMU
retest:

- Pi 4 `raspi4b` no longer reached the old runtime band
- `plo` failed with:
  `Cannot allocate memory for 'phoenix-aarch64a72-generic.elf'`

That failure showed the low `ADDR_PLO=0x200000` experiment was incompatible
with the current Phoenix `plo` memory map, which still assumes the kernel image
is loaded inside the `0x40000000..0x7fffffff` DDR window described by the
generic `preinit.plo.yaml` map.

## Code Changes

### `plo`

File:

- `/Users/witoldbolt/phoenix-rpi/sources/plo/ld/aarch64a72-generic.ldt`

Commit:

- `cb0975a`

Change:

- restore the Pi 4 A72 `plo` linker script to the existing generic high-DDR
  placement by reusing `aarch64a53-generic.ldt`

Result:

- the Pi 4 QEMU lane no longer regresses inside `plo`

### `phoenix-rtos-project`

Files:

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`
- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`

Changes:

- restore:
  - `kernel_address=0x40080000`
  - `boot_load_flags=0x1`
- add:
  - `armstub=phoenix-armstub8-rpi4.bin`
  - `disable_splash=1`
- build and stage a Pi-4-specific `armstub8-rpi4`-style AArch64 stub into the
  boot tree
- keep the existing `loader.disk` handoff model unchanged

Commit:

- `e6a6fe5`

### Coordination Repo Helpers

Files:

- `/Users/witoldbolt/phoenix-rpi/scripts/assemble-rpi4b-bootfs.sh`
- `/Users/witoldbolt/phoenix-rpi/scripts/export-rpi4b-sdimg.sh`
- `/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh`

Changes:

- copy `phoenix-armstub8-rpi4.bin` into the assembled Pi 4 bootfs tree
- export the VM-local SD image with `limactl copy --backend=scp` instead of a
  brittle raw `cat` pipe
- update the expected host-image SHA-256 to the refreshed artifact

## Validation

### Rebuild

Passed in `phoenix-dev` after regenerating the Pi 4 DTB:

- `TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

### QEMU

Passed:

- `./scripts/qemu-shell-smoke.sh generic`
- `./scripts/qemu-shell-smoke.sh rpi4b`
- `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`

### Artifact Validation

Passed:

- `./scripts/assemble-rpi4b-bootfs.sh`
- `./scripts/assemble-rpi4b-bootfs-img.sh`
- `./scripts/assemble-rpi4b-sdimg.sh`
- `./scripts/export-rpi4b-sdimg.sh`
- `./scripts/verify-rpi4b-sdimg.sh`

The rebuilt FAT and SD images now both contain:

- `config.txt`
- `kernel8.img`
- `loader.disk`
- `phoenix-armstub8-rpi4.bin`
- `bcm2711-rpi-4-b.dtb`
- required Pi 4 firmware files

## Refreshed Artifact

Exported host-visible image:

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`

Size:

- `69206016`

SHA-256:

- `9fc6dd1b5c6a5da81aa62c980f5abbc68f165183a0efa084881cb81202d38e24`

## Result

The earlier low-placement hypothesis is now closed as false and cleaned up from
the active image path. The refreshed Pi 4 SD image keeps the last coherent
Phoenix loader placement and adds a Pi-4-specific firmware handoff stub, which
is the next justified real-device retry.
