# Manifest: A72 Pi 4 Runtime Validation Scope

- Date: `2026-03-20`
- Step: `STEP-0184`
- Status: `completed`

## Goal

- define the smallest runtime validation step for the new `aarch64a72-generic-rpi4b` scaffold

## Review Basis

Reviewed:

- `tracking/current-step.md`
- `manifests/2026-03-20-aarch64-rpi4b-a72-scaffold.md`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`

## Findings

- the new A72 scaffold is intentionally structural and does not yet change kernel or loader runtime behavior
- the next highest-signal question is whether the A72 build identity changes the current Pi 4 QEMU boundary at all
- the strongest comparable runtime lane is:
  `qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G`
  with the official firmware DTB and the staged `kernel8.img`
- the smallest safe runtime step is therefore validation-only, not another code change

## Conclusion

- the next step should rebuild `TARGET=aarch64a72-generic-rpi4b`, run the Pi 4 QEMU smoke with the official DTB, record the earliest visible boundary, and compare it with the existing A53 diagnostic lane

## Selected Next Step

- validate the `aarch64a72-generic-rpi4b` Pi 4 QEMU lane and record whether the early-boot boundary moves
