# 2026-04-17 - Pi 4 early-kernel MMU cross-check against public references

## Trigger

Before spending more real-board retries on the current `A2 / KLM / X1 / X2 / X3`
stall, the Pi 4 early-kernel MMU path was re-checked against public references
to rule out missing well-known arm64 or BCM2711 bring-up requirements.

## Sources checked

- Linux arm64 early boot:
  <https://github.com/torvalds/linux/blob/master/arch/arm64/kernel/head.S>
- Raspberry Pi forum higher-half / TTBR1 bring-up thread:
  <https://forums.raspberrypi.com/viewtopic.php?t=322671>
- Raspberry Pi forum generic TTBR0 / TTBR1 discussion:
  <https://forums.raspberrypi.com/viewtopic.php?t=227139>
- Circle:
  <https://github.com/rsta2/circle>
- NuttX BCM2711 notes:
  <https://nuttx.apache.org/docs/latest/platforms/arm64/bcm2711/index.html>
  <https://nuttx.apache.org/docs/12.9.0/guides/porting-case-studies/bcm2711-rpi4b.html>
- BCM2711 peripherals PDF:
  <https://datasheets.raspberrypi.org/bcm2711/bcm2711-peripherals.pdf>

## Main conclusions

- The current Phoenix change that builds `TTBR1` before MMU-on and removes the
  late runtime `TCR_EL1.EPD1` toggle is aligned with the mainstream arm64 boot
  model. It is not the suspicious part anymore.

- The strongest remaining publicly documented gap is page-table cache
  maintenance for tables populated while the MMU is off.

  Linux explicitly documents and implements this because speculative cache lines
  can otherwise leave stale page-table contents visible to the page-table
  walker.

- The Raspberry Pi forum TTBR1 bring-up discussions independently reinforce the
  same general model:
  - keep execution identity-mapped through MMU enable
  - branch explicitly to the higher-half mapping only after that
  - do not confuse high virtual link addresses with high physical placement

- Circle, NuttX, EDK2, and U-Boot did not reveal a special BCM2711-only MMU
  rule that would invalidate the current Phoenix TTBR1-from-start direction.

## Practical implication

Before the next real-board retry, the strongest next code change should be:

- add an explicit cache-maintenance pass for the early TTBR0 / TTBR1 tables
  before MMU-on, in the same problem area where Linux performs page-table
  clean/invalidate work

This is currently a stronger, better-sourced next move than:

- more probe churn
- more blind hardware retries
- reverting the TTBR1-from-start structure without a better primary-source
  reason

## Scope of this step

- documentation and research only
- no new source code change was applied in this step
