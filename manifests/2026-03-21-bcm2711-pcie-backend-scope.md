# BCM2711 PCIe Backend Scope

Date: `2026-03-21`

## Step

- `STEP-0313` Scope the first BCM2711 PCIe config-space backend step

## Repositories

- coordination repo

## Summary

- selected the first BCM2711-specific backend slice after the server-local
  cfg-access refactor
- fixed that slice as a compile-oriented indexed config-space backend, not full
  host-bridge bring-up
- kept link training, outbound windows, DMA ranges, MSI, and xHCI explicitly
  out of scope

## Selected Next Step

- add a BCM2711-specific indexed config-space backend behind the current
  `pcie_cfgio_t` interface
- select it through Pi 4 project/build settings
- keep the implementation limited to:
  - host-bridge MMIO mapping
  - root-complex slot-0 special handling on bus 0
  - `PCIE_EXT_CFG_INDEX` / `PCIE_EXT_CFG_DATA` based config-space reads/writes

## Why This Is The Smallest Useful Move

- it is the first real Pi 4 transport code slice that uses the already-landed
  abstraction
- it creates a concrete BCM2711 backend seam without pretending that the link
  is trained or that downstream devices are reachable yet
- it keeps validation in the compile-only lane that is actually available today

## References

- `external/circle/lib/bcmpciehostbridge.cpp`
- `external/circle/include/circle/bcmpciehostbridge.h`
- `external/circle/include/circle/bcm2711.h`
- `sources/phoenix-rtos-devices/pcie/server/pcie.c`

## Validation

- source review only
- no code changes in this step

## Next Logical Step

- implement the compile-only BCM2711 indexed config-space backend and validate
  it on the touched Pi 4 and Xilinx compile lanes
