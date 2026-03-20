# Manifest: Pi 4 Firmware-File Staging Scope

- Date: `2026-03-20`
- Step: `STEP-0112`
- Status: `completed`

## Goal

- define the smallest next step that makes the Pi 4 staged boot tree capable of becoming a self-contained firmware partition once the operator supplies Raspberry Pi firmware files

## Source Findings

From the current Pi 4 staged boot tree:

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
  - currently stages:
    - `config.txt`
    - `kernel8.img`
    - `loader.disk`
    - optional `bcm2711-rpi-4-b.dtb`
  - does not stage Raspberry Pi firmware files

From the current project-local Pi 4 directory:

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/`
  - does not contain `start4.elf`, `fixup4.dat`, or an equivalent staged firmware directory

From the current docs:

- `docs/platforms/raspberry-pi-4.md`
  - already states that the first FAT partition contains Raspberry Pi firmware files, `config.txt`, DTB, and the Phoenix loader image
- `docs/manual-operator-instructions.md`
  - documented DTB and staged Pi 4 artifacts, but did not yet explicitly call out a required firmware-file source

## Selected Next Step

- keep the change project-local to `phoenix-rtos-project`
- add an optional operator-supplied firmware directory input for the Pi 4 project, similar in spirit to the existing optional DTB input
- when supplied, copy the required Raspberry Pi firmware files into `_boot/aarch64a53-generic-rpi4b/rpi4b/`
- keep ordinary no-hardware builds green if firmware files are not supplied
- update the operator runbook with the exact manual requirement discovered here

## Planned Validation For The Next Step

- build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- artifact inspection:
  - stage a synthetic firmware directory with placeholder files
  - verify the placeholder firmware files are copied into `_boot/aarch64a53-generic-rpi4b/rpi4b/`
- compatibility check:
  - default no-firmware builds must remain green if the staging remains optional
