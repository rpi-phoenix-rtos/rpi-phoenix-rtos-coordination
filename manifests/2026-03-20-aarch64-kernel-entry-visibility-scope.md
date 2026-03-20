# Manifest: Earliest Generic AArch64 Kernel-Entry Visibility Scope

- Date: `2026-03-20`
- Step: `STEP-0176`
- Status: `completed`

## Goal

- select the smallest visibility-only kernel change that can show whether the Pi 4 lane reaches generic AArch64 kernel `_start` after the single loader-side `A3` transfer marker

## Changes

No code changes.

## Review Basis

Reviewed:

- `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`
- `sources/phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/board_config.h`
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/board_config.h`
- `sources/phoenix-rtos-build/Makefile.common`

## Findings

- the first generic AArch64 kernel instruction stream is `hal/aarch64/_init.S`, so that file is the correct smallest boundary for post-loader kernel-entry visibility
- project-local `board_config.h` is already injected first on the include path through `phoenix-rtos-build/Makefile.common`, so a generic kernel config header can safely consume project-specific UART base overrides without new build glue
- both the generic QEMU and Pi 4 project board configs already define `PL011_TTY_BASE`, which is sufficient for a tiny raw PL011 marker before the normal DTB-driven console setup exists
- the chosen step can remain visibility-only if it adds:
  - a board-overridable early UART base definition in generic AArch64 kernel config
  - a tiny raw PL011 `putc` macro in `hal/aarch64/_init.S`
  - one marker at kernel `_start`

## Conclusion

- the next bounded implementation step should add a raw earliest-entry marker in `phoenix-rtos-kernel/hal/aarch64/_init.S`
- the marker should be emitted before the first substantial kernel initialization work so the Pi 4 lane can be divided cleanly into:
  - no kernel entry
  - or visible kernel entry followed by a later early-init failure
- the step should stay limited to generic AArch64 kernel config and `_init.S`

## Selected Next Step

- implement the earliest generic AArch64 kernel-entry visibility marker and rerun both QEMU lanes
