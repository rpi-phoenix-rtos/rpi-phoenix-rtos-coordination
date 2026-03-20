# Manifest: Pi 4 GIC Base Provenance Scope

- Date: `2026-03-20`
- Step: `STEP-0218`
- Status: `completed`

## Goal

- select the smallest next follow-up that can test whether Phoenix is using the
  intended GIC distributor and CPU-interface base addresses on the Pi 4 lane

## Evidence Reviewed

Runtime evidence:

- Pi 4 lane:
  - `gtimer: hppir 0`
  - `gtimer: ahppir 524`

Source evidence:

- `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c`
  only treats `interrupt-controller@...` nodes as GIC candidates when they are
  under `amba_apu`
- the same file matches shallow `intc@...` nodes, which fits generic `virt`
- the official Pi 4 DTB contains `/soc/interrupt-controller@40041000`
  with `reg = <0x40041000 0x1000 0x40042000 0x2000 ...>`

## Selected Next Experiment

- add one bounded runtime trace of the GIC base addresses Phoenix actually uses:
  - print the `gicd` and `gicc` addresses returned by `dtb_getGIC()`
  - compare them on generic `virt` and Pi 4 `raspi4b`

## Why This Is The Right Next Step

- it changes one visibility variable only
- it directly tests the new strongest hypothesis
- it can confirm or rule out a DTB-driven wrong-base selection without changing
  any timer or interrupt policy

## Selected Next Step

- implement the bounded GIC base provenance trace and validate it on the
  generic and Pi 4 QEMU lanes
