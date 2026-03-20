# Status

## Repository State

- Repository purpose: documentation and agent scaffolding for a future Phoenix RTOS Raspberry Pi port
- Implementation state: Phase 1 common AArch64 cleanup started; first upstream build-glue step completed
- Documentation baseline prepared: 2026-03-19

## Implementation Readiness

Documentation readiness:

- ready

Tracking readiness:

- ready

Execution readiness on the current workstation:

- ready for implementation bootstrap and validated host-side Phoenix builds

Known remaining start-gate tasks before the first implementation step:

- none

Completed start-gate tasks:

- missing host prerequisite tools installed on the current workstation
- the initial Phoenix upstream repositories cloned into `sources/`
- first baseline integration manifest created under `manifests/`
- `phoenix-dev` Linux VM created and verified
- the documented Linux package baseline installed and verified inside `phoenix-dev`
- the full current `phoenix-rtos-project/.gitmodules` repo set cloned as sibling repos under `sources/`
- the local sibling-clone buildroot workflow has been defined and automated with `scripts/prepare-buildroot.sh`
- one clean upstream `host-generic-pc` build completed successfully inside `phoenix-dev`

Start-gate status:

- cleared for the first implementation steps

## Strategic Decisions Already Made

- First real target is Raspberry Pi 4 Model B.
- Raspberry Pi 5 is a second-stage target after Pi 4 stabilization.
- Final target architecture should preserve Phoenix's normal boot chain:
  `Raspberry Pi firmware -> plo -> syspage -> kernel -> user-space servers/drivers`
- Pi 4 bring-up should begin with a minimal single-core UART-booting system.
- The implementation must advance in narrow, explicitly validated steps rather than broad multi-subsystem pushes.
- Every successful implementation step must end with git commits in each touched upstream repository plus a coordination-repo state update.
- QEMU is a fast gate, not a replacement for real hardware.
- Pi 4 network boot is a preferred later-stage real-hardware deployment path for fast iteration once bootloader setup and DHCP/TFTP infrastructure are ready; SD or USB media remains the fallback and recovery path.
- This project runs on a macOS Apple Silicon workstation. The recommended execution model is macOS host for coordination and hardware control, plus a Linux arm64 VM as the primary Phoenix build and emulation environment.
- Future code must favor upstreamability: small diffs, Phoenix-native style, warning-clean builds, and no gratuitous reformatting.
- The workflow now supports explicitly authorized unattended sessions, but only under the step, validation, commit, and stop-condition rules documented in `docs/unattended-agent-mode.md`.

## Most Important Technical Findings

- Phoenix has reusable AArch64 support, but it is currently too `zynqmp`-specific in build glue and DTB assumptions.
- Phoenix's AArch64 DTB parser needs generalization for Raspberry Pi DT layouts and standard FDT cell handling.
- Phoenix's AArch64 HAL currently includes generic GICv2 support, but timer/platform selection is too platform-specific.
- Phoenix's existing test runner is already structured for UART-driven DUT automation and can be extended for Raspberry Pi targets.
- Phoenix officially documents Linux build flows and Linux package prerequisites; native macOS builds should not be treated as the primary path.
- On the current host, Homebrew, Xcode, QEMU, `dtc`, `uv`, `expect`, `jq`, `limactl`, `yq`, `socat`, `picocom`, `mtools`, and `socket_vmnet` are present, and the `phoenix-dev` Ubuntu 24.04 VM now has the documented package baseline installed.
- `phoenix-rtos-project` expects a populated multi-repo tree via its submodule paths. The full current `.gitmodules` repo set is now cloned under `sources/`, and the sibling-clone workflow is now handled through the disposable buildroot prepared by `scripts/prepare-buildroot.sh`.
- In the current Lima setup, the shared workspace path is effectively read-only from inside the Linux guest, so disposable buildroots should fall back to VM-local storage such as `~/phoenix-buildroots/phoenix-rtos-project`.
- The first clean upstream baseline build is now verified with `TARGET=host-generic-pc ./phoenix-rtos-build/build.sh clean host core fs test project image` inside the disposable buildroot, producing artifacts under `_build/host-generic-pc`, `_fs/host-generic-pc/root`, and `_boot/host-generic-pc`.
- The `aarch64-phoenix` toolchain is now installed and verified in `phoenix-dev` at `/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix`, with sysroot `/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/aarch64-phoenix`.
- The AArch64 toolchain build requires more than the baseline Phoenix package set; the currently confirmed extra VM packages are `bison`, `flex`, `libgmp-dev`, `libmpfr-dev`, `libmpc-dev`, `libisl-dev`, and `zlib1g-dev`.
- The current AArch64/libphoenix flow still generates files inside component source trees, so the linked buildroot is not sufficient for current toolchain or AArch64-target validation in the read-only Lima mount; use `scripts/prepare-buildroot.sh --copy-components` and the VM-local copied buildroot at `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy` for those lanes.
- The first upstream AArch64 cleanup step is now complete: `phoenix-rtos-kernel` and `plo` no longer hardwire top-level AArch64 platform selection through a literal `zynqmp` substring check, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with the new selection path.
- Copied buildroots exclude `.git`, so some builds may emit harmless version-probe noise such as `fatal: not a git repository`; treat the overall build exit status and produced artifacts as authoritative.
- Local `qemu-system-aarch64` in `phoenix-dev` provides the standard `virt` machine, and its DTB exposes root-level `pl011@...`, `intc@...`, `arm,armv8-timer`, and PSCI/HVC nodes; the first non-Xilinx QEMU follow-up should therefore start with kernel DTB parser recognition of those node names rather than with target metadata alone.
- The kernel DTB parser now recognizes shallow `pl011@...` and `intc@...` nodes, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- Local `virt` inspection also confirmed that the GIC `reg` property uses 16-byte tuples, so the next narrow generic-QEMU follow-up should stay in `hal/aarch64/dtb.c` and generalize interrupt-controller `reg` decoding before broader AArch64 platform work.
- The kernel DTB parser now decodes both the existing 12-byte GIC `reg` tuples and the 16-byte tuples used by local QEMU `virt`, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- There is still no reusable PL011 or ARM architectural timer implementation in the current Phoenix AArch64 tree, so the next smallest preparatory step is to expose root-level `timer` node interrupt metadata from the DTB parser before adding any runtime generic timer code.
- The AArch64 DTB API now exposes architectural timer interrupt metadata from the root-level `timer` node, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- The DTB preparation series is now far enough that the next step must introduce runtime code or new target/build structure, so the next active step is a bounded planning step to choose the smallest safe runtime follow-up.
- That runtime planning step is now complete: the next selected change is to remove the hard `TIMER_IRQ_ID` dependency from common AArch64 GICv2 code by moving timer IRQ knowledge behind the timer HAL API.
- The common AArch64 GICv2 code now queries timer IRQ identity through the timer HAL API instead of using the `TIMER_IRQ_ID` macro directly, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- Reusable AArch64 architectural timer sysreg helpers now exist in `hal/aarch64/aarch64.h`, and the existing `aarch64a53-zynqmp-qemu` build still succeeds with that change.
- Phoenix upstream style is conservative and review-oriented: file headers, tabs in C, localized `clang-format off/on`, direct control flow, `static const` hardware tables, and warning-clean builds enforced by `-Werror` in `phoenix-rtos-build/Makefile.common`.
- Pi 4 uses BCM2711 with GIC-400, PL011, BCM2711 PCIe, VL805 xHCI over PCIe, GENET Ethernet, and Broadcom SDHCI.
- Pi 5 uses BCM2712 plus RP1, with most I/O behind a PCIe-connected southbridge-like peripheral controller.

## Immediate Next Implementation Milestones

1. Define the first common AArch64 generic timer backend step.
2. Implement that selected generic timer backend step in one narrow patch.
3. Implement a generic AArch64 FDT parser suitable for Raspberry Pi DTBs.
4. Add a Raspberry Pi 4 `plo` platform with PL011 UART, MMU, GICv2, and a real boot path from Raspberry Pi firmware.
5. Boot the Phoenix kernel on Pi 4 with a minimal RAM-backed rootfs.

## Pi 4 Success Criteria for "Phase 1"

- Stable boot from Raspberry Pi firmware into `plo`
- Stable `plo` UART console
- Stable `plo -> kernel` transfer
- Kernel MMU, exception, interrupt, and timer paths working
- Single-core shell on UART
- Reliable reboot

## Pi 4 Success Criteria for "Developer Complete"

- SD boot and persistent rootfs
- UART, GPIO, I2C, SPI, PWM
- Ethernet
- PCIe host bridge
- xHCI USB host
- USB mass storage
- Watchdog, thermal, RNG
- Reproducible build/test automation against real hardware

## Pi 5 Entry Gate

Do not start full Pi 5 enablement until Pi 4 has:

- stable boot
- stable storage
- stable Ethernet
- stable USB host
- a working real-device regression loop

## Re-Verify Before Depending On

- Raspberry Pi EEPROM/config behavior
- QEMU `raspi4b` peripheral completeness
- exact network boot and `boot.img` behavior on the current Raspberry Pi bootloader release
- Lima `socket_vmnet` behavior on the exact macOS and Lima versions in use when bridged lab networking is enabled
- Pi 5 debug/bootloader options such as `enable_rp1_uart`, `pciex4_reset`, `os_check`
- Linux and BSD support state for Pi 5 Ethernet and RP1 peripherals
