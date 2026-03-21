# BCM2711 Link State Gating

Date: `2026-03-21`

## Step

- `STEP-0318` Implement the smallest BCM2711 link-bring-up slice

## Repositories

- `phoenix-rtos-devices` `e7ece12`
- coordination repo

## Summary

- added the next bounded BCM2711 link step:
  `PERST` release, 100 ms settle wait, link-up sampling, and RC-mode sampling
- stored that sampled state in the BCM2711 backend context
- used the sampled state to gate downstream bus config-space accesses while
  still allowing root-complex config-space access on bus `0`

## Key Files

- `sources/phoenix-rtos-devices/pcie/server/pcie.c`

## Design Notes

- this step does not claim that the link is working on real hardware
- it does make the backend behavior more faithful to the expected BCM2711
  sequence:
  downstream accesses now explicitly return all ones and ignore writes when the
  sampled link or RC-mode state is not ready
- the step remains intentionally narrower than outbound-window setup,
  root-bridge class-code shaping, or downstream enumeration

## Validation

Validated in `phoenix-dev`:

Preserved Xilinx compile lane:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
tmpdir=$(mktemp -d ~/phoenix-buildroots/zynq-pcie-link.XXXXXX)
./scripts/prepare-buildroot.sh --copy-components "$tmpdir"
cd "$tmpdir"
make -C phoenix-rtos-devices TARGET=aarch64a53-zynqmp-qemu \
  CPPFLAGS="-I$PWD/_projects/aarch64a53-zynqmp-qemu" pcie
```

Touched Pi 4 compile lane and regression build:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
tmpdir=$(mktemp -d ~/phoenix-buildroots/pi4-pcie-link.XXXXXX)
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

- the BCM2711 backend still lacks outbound-window setup and root-bridge shaping
  needed before downstream enumeration can be treated as meaningful

## Next Logical Step

- scope the smallest outbound-window and root-bridge shaping slice after the
  new link-state gating step
