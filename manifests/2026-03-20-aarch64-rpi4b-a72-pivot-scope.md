# Manifest: Pi 4 Cortex-A72 Targeting Pivot Scope

- Date: `2026-03-20`
- Step: `STEP-0180`
- Status: `completed`

## Goal

- define the first bounded implementation step that moves Pi 4 bring-up away from the temporary `aarch64a53` identity and toward a real Cortex-A72-capable generic target path

## Changes

No code changes.

## Review Basis

Reviewed:

- `sources/phoenix-rtos-project/_targets/aarch64a53/generic/build.project`
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
- `sources/phoenix-rtos-kernel/hal/aarch64/exceptions.c`
- `sources/plo/hal/aarch64/generic/config.h`
- Raspberry Pi 4 board facts already captured in `docs/platforms/raspberry-pi-4.md`

## Findings

- Pi 4 is BCM2711 with a quad-core Cortex-A72 CPU, so `aarch64a53-generic-rpi4b` is only a temporary diagnostic target and should not remain the long-term basis
- there is currently no `aarch64a72` target family anywhere in the local `phoenix-rtos-project/_targets` tree
- generic loader config still hardcodes:
  - `PATH_KERNEL "phoenix-aarch64a53-generic.elf"`
  - `ld/aarch64a53-generic.ldt`
- the generic kernel and loader still contain `__TARGET_AARCH64A53` conditionals, so a future A72 target family needs an enabling step before Pi 4 can stop inheriting A53-specific build identity

## Conclusion

- the first bounded implementation step toward proper Pi 4 CPU targeting should not yet be a full A72 port
- the smallest enabling step is to generalize generic artifact naming and linker-script selection away from the literal `aarch64a53` names so a second generic target family can exist without breaking the current QEMU lane
- after that groundwork, the next step can add the first `aarch64a72-generic` target scaffold and a Pi 4 project variant that builds against it

## Selected Next Step

- generalize generic loader artifact naming and linker-script selection so the first `aarch64a72-generic` target scaffold can be introduced cleanly
