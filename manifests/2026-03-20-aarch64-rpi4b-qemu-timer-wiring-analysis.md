# Manifest: Pi 4 QEMU Timer Wiring Analysis

- Date: `2026-03-20`
- Step: `STEP-0212`
- Status: `completed`

## Goal

- determine whether the Pi 4 QEMU fast lane actually uses the BCM2711 local
  interrupt controller for the architectural physical timer path

## Sources Reviewed

- local QEMU 10.2.2 source tree:
  - `/home/witoldbolt.guest/src/qemu-10.2.2/hw/arm/bcm2838.c`
  - `/home/witoldbolt.guest/src/qemu-10.2.2/hw/intc/bcm2836_control.c`
- local QEMU 10.2.2 build artifacts:
  - `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64`

## Findings

- `hw/intc/bcm2836_control.c` does model the local interrupt controller and
  includes:
  - `IRQ_CNTPNSIRQ`
  - `REG_TIMERCONTROL`
  - `REG_IRQSRC`
  - `cntpnsirq` GPIO inputs
- but `hw/arm/bcm2838.c` wires the Pi 4 CPU timers differently:
  - `GTIMER_PHYS -> GIC PPI(n, 14)`
  - `GTIMER_VIRT -> GIC PPI(n, 11)`
  - `GTIMER_HYP -> GIC PPI(n, 10)`
  - `GTIMER_SEC -> GIC PPI(n, 13)`
- unlike `hw/arm/bcm2836.c`, the Pi 4 `bcm2838.c` path does not route the CPU
  physical timer through `bcm2836_control` first

## Result

- the local controller route-enable and prescaler experiments are not relevant
  to the current Pi 4 QEMU timer path
- the Pi 4 QEMU fast lane should now pivot back to the direct CPU timer to GIC
  PPI 14 path instead of spending more bounded steps in the local controller

## Next Step

- restore the QEMU fast lane by removing the now-proven-irrelevant local
  controller hooks from the active experiment path, then scope the smallest
  direct GTIMER-to-GIC follow-up
