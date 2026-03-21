# Pi 4 USB Transport Scope

Date: `2026-03-21`

## Step

- `STEP-0311` Scope the first Pi 4 USB transport milestone after the keyboard bridge

## Repositories

- coordination repo

## Summary

- reviewed the current Phoenix PCIe server shape against the Pi 4 USB target
  and the already indexed Circle BCM2711 host-bridge path
- fixed the next real transport milestone as BCM2711 PCIe root-complex
  bring-up plus config-space enumeration
- explicitly rejected an early jump to xHCI as too wide for the current step

## Key Findings

- Pi 4 USB keyboard input is still blocked by transport, not by higher-level
  keyboard handling:
  Phoenix now has both a generic `usbkbd` class driver and a `pl011-tty`
  `/dev/kbd0` bridge
- the current Phoenix PCIe server scan logic was still ECAM-only in shape
- Circle's BCM2711 host-bridge code uses indexed config-space access through
  `PCIE_EXT_CFG_INDEX` and `PCIE_EXT_CFG_DATA`, not a plain directly mapped
  ECAM window
- the first enabling code move is therefore a small server-local config-space
  abstraction, not a full host-bridge implementation

## References

- `external/circle/lib/bcmpciehostbridge.cpp`
- `external/circle/include/circle/bcm2711.h`
- `external/circle/include/circle/memorymap64.h`
- `sources/phoenix-rtos-devices/pcie/server/pcie.c`
- `sources/phoenix-rtos-devices/pcie/README.md`

## Validation

- source review only
- no code changes in this step

## Next Logical Step

- implement the smallest server-local PCIe config-space abstraction that keeps
  the current Xilinx ECAM path working while making a later BCM2711 backend
  possible
