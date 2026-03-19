# Phoenix RTOS Raspberry Pi Port: Implementation Dossier

## Scope

This document is the main technical blueprint for a full Phoenix RTOS port to Raspberry Pi hardware, starting with Raspberry Pi 4 and extending later to Raspberry Pi 5.

It is based on:

- Phoenix RTOS documentation and upstream source repositories
- Raspberry Pi official boot/configuration documentation
- Raspberry Pi BCM2711, BCM2712, and RP1 peripheral documentation
- Raspberry Pi Linux upstream/source references
- FreeBSD and NetBSD Raspberry Pi support references

## Working Rules

The implementation must stay phase-gated and commit-oriented.

Rules:

1. Only advance one narrow step at a time.
2. Every step must define explicit success criteria before code is written.
3. A step is only considered complete after validation on the strongest relevant test lane.
4. After each successful step, commit the changes in every touched upstream repository.
5. After cross-repository steps, update the coordination repository to record the exact integration state and test outcome.
6. Prefer multi-repo coordination through manifests and documentation in this repository, not by collapsing Phoenix into a synthetic monorepo.
7. Keep every code step small, readable, warning-clean, and stylistically aligned with neighboring Phoenix sources.

## 1. Core Architecture Decision

The target design should preserve the native Phoenix execution model rather than replacing it with a foreign boot stack.

Target chain:

1. Raspberry Pi firmware loads a Phoenix-provided image from the first FAT partition.
2. `plo` performs platform bring-up required by Phoenix.
3. `plo` constructs or passes the Phoenix syspage inputs.
4. Phoenix kernel starts.
5. User-space servers and drivers register devices and filesystems.

### Why this is the right design

- Phoenix is built around `plo`, syspage preparation, user-space device drivers, and the existing build/test pipeline.
- A UEFI-first design would hide important platform details and complicate later native support.
- A firmware-assisted early stage is acceptable, but only as a stepping stone.

### Acceptable transitional compromises

- Using Raspberry Pi firmware to provide the DTB and initial binary loading.
- Using firmware-preserved RP1 state during the earliest Pi 5 debug phase.
- Using QEMU for common AArch64 iteration even when it does not model every Raspberry Pi peripheral.

### Unacceptable end states

- Permanent dependence on UEFI for normal boot.
- Permanent dependence on Linux firmware state for core low-level initialization.
- A port that only boots in emulation or only boots via manual developer intervention.

## 2. What Exists Already in Phoenix

### 2.1 AArch64 kernel support

Important upstream Phoenix kernel paths:

- `phoenix-rtos-kernel/hal/aarch64/hal.c`
- `phoenix-rtos-kernel/hal/aarch64/dtb.c`
- `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `phoenix-rtos-kernel/hal/aarch64/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/*`

Key finding:

- Phoenix already has common AArch64 code and generic GICv2 interrupt logic.
- The platform selection in `hal/aarch64/Makefile` is still driven by `zynqmp`.
- The current DTB parser is not generic enough for Raspberry Pi device tree structure.

### 2.2 `plo`

Important upstream loader paths:

- `plo/hal/aarch64/Makefile`
- `plo/hal/aarch64/zynqmp/*`
- `plo/cmds/*`
- `plo/phfs/*`
- `plo/devices/*`

Key finding:

- `plo` already provides a real Phoenix-native loader and script system.
- `plo` AArch64 support is also currently `zynqmp`-oriented.
- `plo` has enough structure to host a Raspberry Pi-specific UART/storage boot path.

### 2.3 Phoenix build and target system

Important upstream build paths:

- `phoenix-rtos-build/build.sh`
- `phoenix-rtos-build/build-core-aarch64a53-zynqmp.sh`
- `phoenix-rtos-build/target/aarch64.mk`
- `phoenix-rtos-project/_targets/aarch64a53/zynqmp/*`

Key finding:

- Phoenix's target system is already organized around target families and target-specific scripts/YAML.
- A Raspberry Pi 4 target can follow the same pattern once the AArch64 support is generalized.

### 2.4 Phoenix tests

Important upstream test paths:

- `phoenix-rtos-tests/runner.py`
- `phoenix-rtos-tests/trunner/dut.py`
- `phoenix-rtos-tests/trunner/target/*`
- `phoenix-rtos-tests/trunner/harness/plo.py`
- `phoenix-rtos-tests/trunner/host.py`
- `phoenix-rtos-build/scripts/run_project_tests.py`

Key finding:

- Phoenix already has a UART/QEMU-oriented automated test harness using `pexpect`, serial, host abstractions, and harness composition.
- This is the right place to integrate Raspberry Pi targets and hardware lab control.

## 3. Major Gaps To Close

## 3.1 Generalize AArch64 support

Before any clean Raspberry Pi port can exist, Phoenix needs:

- generic AArch64 platform selection
- generic DTB/FDT parsing
- generic ARM architectural timer support
- generic SMP hooks
- cleaner separation between common AArch64 and board-specific logic

### Required refactors

1. Split common AArch64 logic from `zynqmp`-specific code in the kernel and `plo`.
2. Introduce a platform directory for `rpi4` later `rpi5`.
3. Make the build system able to include a non-Xilinx AArch64 board family cleanly.

## 3.2 Generalize DTB handling

Current Phoenix DTB parsing assumptions are too narrow:

- it looks for ZynqMP-style node shapes
- it assumes fixed address and size cell widths in some places
- it focuses on `amba_apu`, GIC, and `serial@` nodes in a Zynq-centric layout

### Required DTB capabilities for Raspberry Pi

- parse `/memory@*`
- parse CPU nodes and their enable methods
- parse GIC node
- parse serial nodes
- parse mailbox
- parse SDHCI/MMC
- parse PCIe
- parse Ethernet nodes
- parse power/reset/clock relationships as needed for later drivers

### Raspberry Pi-specific DT facts that matter

- Pi 4 uses BCM2711 DT layouts rooted in Broadcom `bcm2711.dtsi`
- Pi 4 secondary CPUs use `spin-table`
- Pi 5 CPUs use `psci`
- Pi 5 adds RP1 as a PCIe-connected endpoint-like peripheral complex described in `rp1.dtsi`

## 3.3 Add an ARM architectural timer implementation

Phoenix AArch64 currently relies on platform-specific timer initialization rather than a clean generic ARMv8 architectural timer path.

For Raspberry Pi, a generic `arm,armv8-timer` path should be the default kernel timer implementation unless a later board-specific reason appears.

## 3.4 Add missing drivers

Phoenix does not currently have the required Raspberry Pi driver coverage.

Most important missing classes:

- PL011 UART
- BCM2711 / Broadcom SDHCI
- BCM2711 PCIe root complex
- xHCI HCD
- BCM2711 GENET Ethernet
- BCM2835/2711 pinctrl and GPIO
- BCM2835 I2C
- BCM2835 SPI
- watchdog / thermal / RNG support
- Pi 5 RP1 wrapper and RP1 low-speed peripherals

## 4. Phase Plan

## Phase 0: Build, documentation, and lab prep

Deliverables:

- repo-local knowledge base
- standard build container spec
- pinned upstream reference versions
- hardware-lab design for autonomous testing

Tasks:

1. Keep this documentation current.
2. Standardize on Linux build hosts first.
3. Define an image format for Raspberry Pi firmware partitioning.
4. Define the local multi-repo clone and manifest strategy.
5. Choose the first real-device flashing/update workflow.
6. Define the preferred long-run Pi 4 network-boot test loop and the fallback SD or USB recovery path.
7. Define quality gates for code style, warnings, and reviewability before feature work starts.
8. Prepare CI jobs that can at least build and assemble images without hardware.

Success criteria:

- reproducible host setup
- repeatable image generation
- explicit per-step commit and manifest discipline
- explicit per-step code-quality discipline
- clear artifact naming and locations

## Phase 1: Generic AArch64 cleanup

Deliverables:

- generic AArch64 common layer suitable for more than ZynqMP
- generic DTB parser improvements
- generic timer implementation

Tasks:

1. Refactor kernel AArch64 Makefiles and platform hooks.
2. Refactor `plo` AArch64 Makefiles and platform hooks.
3. Introduce an upstream-QEMU generic AArch64 target.
4. Add DTB parser tests where possible.

Success criteria:

- AArch64 code no longer assumes `zynqmp`
- a non-Xilinx AArch64 target can be built cleanly

## Phase 2: Raspberry Pi 4 `plo` bring-up

Goal:

- get a Phoenix-native loader path running on real Pi 4 hardware

Tasks:

1. Create a Raspberry Pi 4 `plo` platform directory.
2. Implement low-level start, exception vectors, MMU tables, cache control, and UART output.
3. Use Raspberry Pi firmware to load `plo` and provide the DTB.
4. Add a minimal `config.txt`/firmware image recipe.

First success criteria:

- boot banner on UART
- DTB visible to `plo`
- stable return to prompt or scripted hand-off behavior

## Phase 3: Raspberry Pi 4 kernel boot

Goal:

- `plo` loads the Phoenix kernel and a minimal root setup

Tasks:

1. Add a Raspberry Pi 4 kernel platform directory.
2. Enable GICv2, exceptions, timer, and console.
3. Boot single-core first.
4. Start with RAM-backed rootfs and minimal services.

Success criteria:

- Phoenix shell on UART
- stable interrupt/timer path
- repeated reboot cycles

## Phase 4: Pi 4 SMP and persistent storage

Tasks:

1. Implement `spin-table` secondary CPU bring-up based on DT data.
2. Add `plo` raw SDHCI block loading.
3. Add kernel/user-space block driver path for persistent rootfs.
4. Use Phoenix `ext2` or `fat` where appropriate.

Success criteria:

- multi-core stable boot
- persistent filesystem boot
- non-RAM test image support

## Phase 5: Pi 4 low-speed I/O

Tasks:

1. PL011 tty driver
2. GPIO/pinctrl
3. I2C
4. SPI
5. PWM
6. optional simple mailbox framebuffer for diagnostics

Success criteria:

- external device interaction via GPIO/I2C/SPI
- scripted peripheral smoke tests

## Phase 6: Pi 4 networking

Tasks:

1. Port `bcm2711-genet-v5`
2. Integrate with Phoenix network stack
3. Add regression tests for DHCP, ping, TCP, link drop/recovery

Success criteria:

- reliable Ethernet
- network-enabled remote automation and artifact transfer

## Phase 7: Pi 4 PCIe and USB host

Tasks:

1. Port Broadcom PCIe host bridge
2. Add MSI support as needed
3. Add a generic xHCI HCD to Phoenix
4. Validate VL805 behind PCIe on Pi 4
5. Add USB storage and simple USB device coverage

Success criteria:

- USB keyboard/storage enumeration
- USB storage mount and transfer tests
- repeated hotplug tests

## Phase 8: Pi 4 polish and reliability

Tasks:

1. Watchdog
2. thermal sensor / throttling visibility
3. RNG
4. soak testing
5. build/test image matrix

Success criteria:

- automated nightly regression on real hardware
- stable enough baseline to begin Pi 5

## Phase 9: Pi 5 bootstrap

Goal:

- get a minimal Phoenix path on Pi 5 while keeping the design honest about RP1 complexity

Tasks:

1. Reuse as much common AArch64 work as possible.
2. Start with `plo`, UART, single-core kernel boot.
3. Use Pi 5 firmware knobs only as temporary debugging aids.
4. Plan for RP1 as a proper native subsystem, not a magical black box.

## Phase 10: Pi 5 RP1 native support

Tasks:

1. RP1 PCIe/MFD wrapper
2. RP1 MSI-X / interrupt routing
3. RP1 clocks/resets/mailbox/firmware interface as needed
4. RP1 UART/I2C/SPI/GPIO/PWM/DMA/MMC
5. RP1 Ethernet
6. RP1 xHCI
7. RP1 display/camera blocks later

Success criteria:

- Pi 5 low-speed I/O and Ethernet/USB work natively

## 5. Driver Porting Priorities

### Highest priority for Pi 4

1. PL011 UART
2. generic ARMv8 timer
3. GICv2
4. SDHCI
5. GPIO/pinctrl
6. GENET Ethernet
7. Broadcom PCIe
8. xHCI

### Lower priority for Pi 4

- framebuffer / display
- audio
- camera
- GPU acceleration
- Wi-Fi / Bluetooth

### Highest priority for Pi 5

1. basic boot and UART
2. PCIe path to RP1
3. RP1 wrapper
4. RP1 GPIO/UART/I2C/SPI/MMC
5. RP1 Ethernet
6. RP1 xHCI

## 6. Filesystem Strategy

Use the first FAT partition only for Raspberry Pi firmware boot assets.

Recommended runtime storage progression:

1. RAM-backed rootfs for first kernel boot
2. SD-backed persistent rootfs using Phoenix `ext2` or `fat`
3. JFFS2 only where raw flash semantics are actually needed

Why:

- Raspberry Pi boards boot naturally from a firmware-managed FAT partition.
- Phoenix already has `fat`, `ext2`, `jffs2`, `dummyfs`, and `meterfs`.
- Persistent SD storage is easier to automate and reason about than introducing raw-flash assumptions on a board whose normal path is SD/USB/NVMe.

## 7. Emulation Strategy

### What QEMU should be used for

- common AArch64 boot path validation
- MMU and exception debugging
- early kernel scheduler sanity
- DTB parser iteration
- CI smoke boots

### What QEMU should not be trusted for

- complete Raspberry Pi peripheral validation
- final interrupt behavior for all peripherals
- USB host correctness
- Ethernet correctness
- full-system confidence

### Practical recommendation

Maintain two emulator lanes:

1. generic AArch64 `virt` lane for fast common work
2. Raspberry Pi-specific QEMU lane for limited board-shape checks

Real hardware remains the authority.

## 8. Real Hardware Automation Strategy

The target is an AI-friendly feedback loop:

1. build image
2. update boot media automatically
3. power-cycle DUT automatically
4. capture UART automatically
5. run smoke tests automatically
6. collect logs automatically

Recommended lab components:

- Linux host controller
- USB-UART adapter
- relay- or smart-PDU-based power switching
- SD card update mechanism or external boot media
- optional SD mux for higher automation

For Pi 4 specifically, automated recovery and provisioning may also benefit from Raspberry Pi bootloader features such as programmable `RPIBOOT` GPIO behavior, but this must be re-verified on the exact target firmware revision used.

## 9. Main Risks

### Risk: spending too long on Pi 5 too early

Mitigation:

- keep Pi 5 behind a hard gate until Pi 4 is operational

### Risk: designing around QEMU quirks

Mitigation:

- gate all meaningful bring-up claims on real-hardware results

### Risk: leaving AArch64 support structurally Zynq-specific

Mitigation:

- insist on common-layer refactors before driver sprawl

### Risk: over-reliance on firmware-preserved hardware state

Mitigation:

- mark every such dependency transitional and track removal work explicitly

### Risk: knowledge loss across long sessions

Mitigation:

- update docs and source-artifact index continuously

## 10. Recommended Working Sequence For Future Agents

When implementation begins, a future agent should usually proceed in this order:

1. read `docs/status.md`
2. read this dossier
3. read the relevant platform document
4. read `docs/testing-automation.md`
5. inspect the current workspace and upstream trees
6. make the smallest change that improves boot or testability
7. validate in generic emulation first if possible
8. validate on real hardware
9. update docs before ending the session
