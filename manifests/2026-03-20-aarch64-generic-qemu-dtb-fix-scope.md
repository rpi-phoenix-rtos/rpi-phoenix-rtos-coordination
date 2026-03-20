# Manifest: Generic AArch64 QEMU DTB Handoff Fix Scope

- Date: `2026-03-20`
- Step: `STEP-0066`
- Result: `completed`

## Scope

- inspect the kernel-stage silence after the generic loader script path started succeeding
- confirm what the AArch64 kernel requires before early console initialization
- choose the smallest DTB handoff fix that works in the current `host project image` lane

## Upstream Repositories

- none

## Findings

- the AArch64 kernel halts in `_hal_init()` if `syspage_progNameResolve("system.dtb")` returns `NULL`
- the generic AArch64 user script does not currently load any `system.dtb` blob before `go!`
- `image_builder.py` resolves `blob /etc/system.dtb ...` against `${PREFIX_ROOTFS}/etc/system.dtb`
- the current generic fast lane does not run a working `fs` build, so the DTB must be made available directly in `${PREFIX_ROOTFS}` during the project build

## Selected Fix

- in the generic QEMU project build, generate a `virt,secure=on,gic-version=2` DTB directly into `${PREFIX_ROOTFS}/etc/system.dtb`
- in the generic AArch64 user script, load that DTB with:
  `blob {{ env.BOOT_DEVICE }} /etc/system.dtb ddr`

## Notes

- this is preferred over broader `plo` DTB-passing redesign because it matches the existing AArch64 kernel contract and stays inside the current validated project/image lane
- if the kernel still stays silent after this DTB handoff fix, the next step should instrument earliest kernel entry or syspage content rather than revisiting the loader-script path

## Selected Next Step

- generate `system.dtb` into `${PREFIX_ROOTFS}` during the generic QEMU project build, load it in `user.plo`, and rerun the smoke command
