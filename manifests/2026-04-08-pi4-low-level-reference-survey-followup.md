# 2026-04-08 Pi 4 Low-Level Reference Survey Follow-up

## Scope

Extend the existing Pi 4 low-level reference survey with the additional
BCM2711, Pi 4 boot, and bare-metal sources identified after the first real
hardware tests.

## Sources reviewed in this pass

- U-Boot BCM2711 DTS:
  `external/u-boot/arch/arm/dts/bcm2711.dtsi`
  Local clone commit: `f0000b4a57e9edf8ff8454b9056d767466dff57f`
- historical Pi 4 Linux notes:
  `external/bcm2711-kernel/README.md`
  Local clone commit: `4e6916f`
- Ultibo platform notes:
  <https://ultibo.org/wiki/Unit_PlatformRPi4>
- Ultibo boot notes:
  <https://ultibo.org/wiki/Unit_BootRPi4>
- NuttX BCM2711 case study:
  <https://nuttx.apache.org/docs/latest/guides/porting-case-studies/bcm2711-rpi4b.html>
- targeted official / web follow-up on:
  - `enable_gic`
  - `arm_peri_high`
  - Pi 4 bare-metal armstub expectations

## Durable findings folded into the knowledge base

- U-Boot independently matches the Linux downstream Pi 4 `ranges` model:
  - `0x7e000000 -> 0xfe000000`
  - `0x7c000000 -> 0xfc000000`
  - `0x40000000 -> 0xff800000`
- U-Boot independently matches:
  - Cortex-A72 CPU description
  - `spin-table` secondary release addresses
  - ARMv8 timer PPI ordering
  - Pi 4 PCIe and GENET node placement
- Ultibo documents two especially useful Pi 4 debugging facts:
  - activity LED is on GPIO `42`
  - the effective interrupt-controller path can depend on DTB plus firmware
    state, not only the `enable_gic` config value
- NuttX's `0x480000` load address is now explicitly categorized as a
  U-Boot-specific kernel placement, not a contradiction of the firmware-native
  `0x80000` bare-metal convention
- historical `sakaki-/bcm2711-kernel` notes are now preserved as
  time-sensitive evidence that early 64-bit Pi 4 Linux often paired
  `enable_gic=1` with `armstub8-gic.bin`

## Practical outcome

The next justified earliest-entry Pi 4 hardware experiment should be chosen
from this narrower set:

1. GPIO42 activity-LED proof
2. fuller Circle-style armstub register setup
3. bounded controller-selection self-test if GIC-vs-legacy mode is still
   uncertain

## Files updated

- `docs/raspberry-pi-4-low-level-reference-survey.md`
- `docs/source-artifacts.md`
- `docs/platforms/raspberry-pi-4.md`
- `docs/status.md`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Validation

- documentation-only step
- local source inspection completed for the newly cloned U-Boot and
  `bcm2711-kernel` reference trees
- targeted web research completed for the supplementary Pi 4 boot questions
