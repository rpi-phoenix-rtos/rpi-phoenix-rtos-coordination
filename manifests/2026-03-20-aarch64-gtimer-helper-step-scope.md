# Manifest: First Source-Agnostic AArch64 Timer Helper Step Scope

- Date: `2026-03-20`
- Step: `STEP-0027`
- Result: `completed`

## Scope

- inspect the current AArch64 architectural timer helpers and DTB source-selection API after `STEP-0026`
- choose the first source-agnostic helper shape for physical-versus-virtual timer operations
- select the smallest exact touched-file set for that helper step

## Result

- selected helper shape:
  add a common AArch64 `gtimer` helper layer that hides the physical-versus-virtual timer sysreg split behind one source-keyed API
- selected responsibilities for the first helper slice:
  - source-keyed counter reads
  - source-keyed control-register access
  - source-keyed timer programming
  - source-to-string helper for later diagnostics
- selected exact file set:
  - `phoenix-rtos-kernel/hal/aarch64/Makefile`
  - `phoenix-rtos-kernel/hal/aarch64/gtimer.h`
  - `phoenix-rtos-kernel/hal/aarch64/gtimer.c`
- selected validation:
  keep the existing `aarch64a53-zynqmp-qemu` build green in `phoenix-dev`; the new helper file should be compiled in that lane even though the ZynqMP timer backend will continue to own runtime behavior

## Why This Was Selected

- The remaining backend duplication is no longer about scheduler wakeup routing but about repeatedly branching on physical versus virtual timer choice.
- A small helper layer removes that split without forcing the full generic backend into the current target.
- Compiling the helper unconditionally keeps it build-validated immediately, instead of creating dead unbuilt code.

## Selected Next Step

- implement the first common AArch64 `gtimer` helper layer in `hal/aarch64/gtimer.[ch]` and compile it in the current AArch64 build
