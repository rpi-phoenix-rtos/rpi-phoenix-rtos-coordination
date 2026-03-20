# Manifest: Generic AArch64 QEMU `virt` Milestone Scope

- Date: `2026-03-20`
- Step: `STEP-0048`
- Result: `completed`

## Scope

- inspect the existing AArch64 QEMU build, project, and test wiring
- choose the first non-Xilinx generic AArch64 QEMU milestone shape
- select the first small code step that should unlock that milestone

## Findings

- the current `aarch64a53-zynqmp-qemu` runtime script depends on Xilinx QEMU and the `arm-generic-fdt` machine, so it is not the right long-term fast lane for generic AArch64 or Raspberry Pi work
- `phoenix-rtos-build/build.sh` selects core-build entry points by `TARGET_FAMILY` and `TARGET_SUBFAMILY`
- `phoenix-rtos-build/makes/include-target.mk` currently accepts only `aarch64a53-zynqmp` in the AArch64 Cortex-A target class
- `phoenix-rtos-project` target selection requires both `_targets/$TARGET_FAMILY/$TARGET_SUBFAMILY` and `_projects/$TARGET`, so later generic QEMU work will need a real `aarch64a53-generic-qemu` project path
- `phoenix-rtos-tests` emulated targets are thin wrappers around project run scripts, so a later generic QEMU target can follow that pattern once the build/project wiring exists

## Selected Milestone

- first non-Xilinx milestone:
  `aarch64a53-generic-qemu`
- intended shape:
  - generic `aarch64a53-generic` build subfamily
  - standard QEMU `virt` machine
  - reusable PL011 console path
  - later `phoenix-rtos-project/scripts/aarch64a53-generic-qemu.sh`
  - later `phoenix-rtos-tests` emulated target wrapper

## Selected First Code Step

- repository:
  `phoenix-rtos-build`
- step:
  add build-system recognition for `aarch64a53-generic` and a matching `build-core-aarch64a53-generic.sh` entry point

## Why This Step Was Selected

- the build system currently rejects `aarch64a53-generic`, so neither kernel nor project work can be selected cleanly until that gate is opened
- the step stays inside one repo and can be validated without claiming that the full generic target already boots
- project-first or kernel-first alternatives would still remain blocked by the current build-layer target whitelist

## Selected Validation Lane

- `TARGET=aarch64a53-generic make -f phoenix-rtos-build/Makefile.common export-cflags`
- `bash -n phoenix-rtos-build/build-core-aarch64a53-generic.sh`

## Selected Next Step

- add build-system recognition for `aarch64a53-generic` and the matching generic core-build entry point
