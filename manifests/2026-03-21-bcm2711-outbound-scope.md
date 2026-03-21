# BCM2711 Outbound And Root-Bridge Scope

Date: `2026-03-21`

## Step

- `STEP-0319` Scope the smallest outbound-window and root-bridge shaping step

## Repositories

- coordination repo

## Summary

- reviewed the remaining gap between the BCM2711 link-state gating step and
  meaningful downstream enumeration
- selected the next smallest slice as:
  one outbound memory window plus root-bridge class-code shaping
- explicitly kept downstream bridge memory-window programming, MSI, xHCI, and
  any claim of real endpoint enumeration out of scope

## Why This Comes Next

- Circle sets outbound translation and root-bridge class shaping immediately
  after link-up on Pi 4, before it treats downstream configuration as useful
- that makes one outbound window plus root-bridge class shaping the smallest
  next step after the sampled link / RC-mode gate
- it moves Phoenix closer to meaningful downstream config-space access without
  pretending that endpoint enumeration is already validated

## References

- `external/circle/include/circle/memorymap64.h`
- `external/circle/lib/bcmpciehostbridge.cpp`
- `sources/phoenix-rtos-devices/pcie/server/pcie.c`

## Validation

- source review only
- no code changes in this step

## Next Logical Step

- implement one BCM2711 outbound window plus root-bridge class shaping behind
  the current link-state gate
