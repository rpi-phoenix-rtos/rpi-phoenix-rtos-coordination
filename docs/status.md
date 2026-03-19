# Status

## Repository State

- Repository purpose: documentation and agent scaffolding for a future Phoenix RTOS Raspberry Pi port
- Implementation state: not started
- Documentation baseline prepared: 2026-03-19

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

## Most Important Technical Findings

- Phoenix has reusable AArch64 support, but it is currently too `zynqmp`-specific in build glue and DTB assumptions.
- Phoenix's AArch64 DTB parser needs generalization for Raspberry Pi DT layouts and standard FDT cell handling.
- Phoenix's AArch64 HAL currently includes generic GICv2 support, but timer/platform selection is too platform-specific.
- Phoenix's existing test runner is already structured for UART-driven DUT automation and can be extended for Raspberry Pi targets.
- Phoenix officially documents Linux build flows and Linux package prerequisites; native macOS builds should not be treated as the primary path.
- On the current host, Homebrew, Xcode, QEMU, `dtc`, `uv`, `expect`, and `jq` are already present; Lima, Docker, Colima, Tart, and common UART helper tools are not yet installed.
- Phoenix upstream style is conservative and review-oriented: file headers, tabs in C, localized `clang-format off/on`, direct control flow, `static const` hardware tables, and warning-clean builds enforced by `-Werror` in `phoenix-rtos-build/Makefile.common`.
- Pi 4 uses BCM2711 with GIC-400, PL011, BCM2711 PCIe, VL805 xHCI over PCIe, GENET Ethernet, and Broadcom SDHCI.
- Pi 5 uses BCM2712 plus RP1, with most I/O behind a PCIe-connected southbridge-like peripheral controller.

## Immediate Next Implementation Milestones

1. Create a generic non-Xilinx AArch64 QEMU target for fast bring-up work.
2. Refactor Phoenix AArch64 support so platform hooks are not `zynqmp`-hardwired.
3. Implement a generic AArch64 FDT parser suitable for Raspberry Pi DTBs.
4. Add a Raspberry Pi 4 `plo` platform with PL011 UART, MMU, GICv2, and a real boot path from Raspberry Pi firmware.
5. Boot the Phoenix kernel on Pi 4 with a minimal RAM-backed rootfs.
6. Add the first multi-repo integration manifest once the upstream repos are cloned locally.

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
