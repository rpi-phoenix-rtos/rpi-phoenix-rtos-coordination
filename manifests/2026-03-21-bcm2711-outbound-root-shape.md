# BCM2711 Outbound Window And Root-Bridge Shaping

Date: `2026-03-21`

## Step

- `STEP-0320` Implement the smallest outbound-window and root-bridge shaping step

## Repositories

- `phoenix-rtos-devices` `cab0ca1`
- `phoenix-rtos-project` `ba1a0c3`
- coordination repo

## Summary

- added Pi 4 outbound-window constants to the Pi 4 board config
- added one BCM2711 outbound-window programming helper
- added RC BAR2 sizing/programming and root-bridge class-code shaping
- gated that shaping behind the sampled BCM2711 link / RC-mode state

## Key Files

- `sources/phoenix-rtos-devices/pcie/server/pcie.c`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`

## Design Notes

- the selected outbound window follows the current Circle Pi 4 memory-map
  reference:
  CPU base `0x600000000`, PCIe-side base `0xF8000000`, size `0x04000000`
- root-bridge shaping in this step is intentionally limited to:
  - outbound window 0 programming
  - RC BAR2 programming
  - root-bridge class code `0x060400`
- this still does not claim meaningful downstream endpoint enumeration on real
  hardware

## Validation

Validated in `phoenix-dev`:

Preserved Xilinx compile lane:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
tmpdir=$(mktemp -d ~/phoenix-buildroots/zynq-pcie-outwin.XXXXXX)
./scripts/prepare-buildroot.sh --copy-components "$tmpdir"
cd "$tmpdir"
make -C phoenix-rtos-devices TARGET=aarch64a53-zynqmp-qemu \
  CPPFLAGS="-I$PWD/_projects/aarch64a53-zynqmp-qemu" pcie
```

Touched Pi 4 compile lane and regression build:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
tmpdir=$(mktemp -d ~/phoenix-buildroots/pi4-pcie-outwin.XXXXXX)
./scripts/prepare-buildroot.sh --copy-components "$tmpdir"
cd "$tmpdir"
make -C phoenix-rtos-devices TARGET=aarch64a72-generic-rpi4b \
  PCI_EXPRESS_BCM2711_INDEXED_CFG=y \
  CPPFLAGS="-I$PWD/_projects/aarch64a72-generic-rpi4b" pcie
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

Observed result:

- the preserved Xilinx-targeted `pcie` server still compiles and links
- the Pi 4 targeted `pcie` server still compiles and links
- a fresh full Pi 4 A72 build still succeeds from the same disposable buildroot

## Remaining Gap

- the root bridge still lacks the explicit bridge memory-window programming
  sequence Circle applies before downstream enumeration is treated as
  meaningful

## Next Logical Step

- scope the smallest root-bridge memory-window and downstream-bus exposure step
