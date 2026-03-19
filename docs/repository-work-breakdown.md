# Repository Work Breakdown

This document translates the architecture plan into concrete upstream Phoenix repositories, likely files, and expected change order.

Use it to decide where a task belongs before editing code.

Before editing, also apply the rules in `docs/code-quality-and-upstreaming.md`.

## 1. Repository Roles

### `phoenix-rtos-kernel`

Owns:

- AArch64 HAL
- exceptions, interrupt controller, timer, MMU, SMP bring-up
- DTB parsing used by kernel boot
- syspage-facing kernel platform integration

Primary paths already identified:

- `hal/aarch64/hal.c`
- `hal/aarch64/dtb.c`
- `hal/aarch64/interrupts_gicv2.c`
- `hal/aarch64/Makefile`
- `hal/aarch64/zynqmp/*`

Expected Raspberry Pi work:

1. split generic AArch64 logic from `zynqmp`
2. add generic timer support based on `arm,armv8-timer`
3. generalize DTB parsing
4. add `rpi4` kernel platform directory
5. later add `rpi5` platform directory
6. add SMP support for Pi 4 spin-table release
7. later add PSCI path needed by Pi 5

### `plo`

Owns:

- earliest Phoenix-native platform bring-up
- console before kernel start
- boot commands and image loading
- syspage inputs/handoff preparation

Primary paths already identified:

- `hal/aarch64/Makefile`
- `hal/aarch64/zynqmp/*`
- `cmds/script.c`
- `cmds/kernel.c`
- `cmds/app.c`
- `devices/*`

Expected Raspberry Pi work:

1. split generic AArch64 logic from `zynqmp`
2. add `rpi4` low-level bring-up with PL011
3. parse firmware-provided DTB
4. load Phoenix kernel from a simple block or raw image path
5. later add Pi 5 bootstrap support
6. optionally add improved boot scripts for automated recovery/testing

### `phoenix-rtos-devices`

Owns:

- user-space device drivers and servers
- block, tty, GPIO, network, USB, and other peripheral-facing runtime code

Expected Raspberry Pi work:

- PL011 runtime tty driver
- SDHCI or block-storage drivers for runtime access
- BCM2835 or BCM2711 GPIO and pinctrl server
- BCM2835 I2C and SPI drivers
- BCM2711 GENET Ethernet driver
- generic PCIe support if implemented in devices layer
- generic xHCI HCD if placed outside the kernel
- watchdog, thermal, RNG, PWM
- later RP1 wrapper plus Pi 5 low-speed drivers and Ethernet/xHCI integration

### `phoenix-rtos-filesystems`

Owns:

- runtime filesystems and block-backed storage formats

Relevant current options:

- `fat/*`
- `ext2/*`
- `dummyfs/*`
- `jffs2/*`

Expected Raspberry Pi work:

1. start with `dummyfs` for early kernel bring-up
2. move to persistent block-backed `ext2` or `fat`
3. ensure the runtime filesystem remains separate from the firmware boot FAT partition

### `phoenix-rtos-build`

Owns:

- build orchestration
- per-target build scripts
- image construction glue
- test runner invocation glue

Primary paths already identified:

- `build.sh`
- `build-core-aarch64a53-zynqmp.sh`
- `target/aarch64.mk`
- `scripts/run_project_tests.py`

Expected Raspberry Pi work:

1. add Raspberry Pi-aware AArch64 target definitions
2. add image assembly helpers for firmware FAT plus Phoenix runtime partitions
3. add QEMU `virt` lane
4. later add Pi 4 and Pi 5 hardware-targeted build recipes

### `phoenix-rtos-project`

Owns:

- target composition
- target config fragments
- `plo` preinit and user configuration YAML

Primary paths already identified:

- `_targets/aarch64a53/zynqmp/build.project`
- `_targets/aarch64a53/zynqmp/preinit.plo.yaml`
- `_targets/aarch64a53/zynqmp/user.plo.yaml`

Expected Raspberry Pi work:

1. create new target family or board target layout for Pi 4
2. define `plo` scripts and build composition for early boot
3. later add Pi 5 target
4. keep board-specific config minimal and push reusable logic into common layers

### `phoenix-rtos-tests`

Owns:

- UART and emulator test automation
- target abstractions
- DUT and host orchestration

Primary paths already identified:

- `runner.py`
- `trunner/dut.py`
- `trunner/target/*`
- `trunner/harness/plo.py`
- `trunner/host.py`

Expected Raspberry Pi work:

1. add generic AArch64 `virt` target
2. add Pi 4 DUT target
3. encode boot prompt, timeouts, flashing, and reboot semantics
4. add hardware-lab control hooks
5. later add Pi 5 DUT target

## 2. Recommended Change Order Across Repositories

Do not attack all repositories at once.

Recommended order:

1. `phoenix-rtos-kernel`
2. `plo`
3. `phoenix-rtos-build`
4. `phoenix-rtos-project`
5. `phoenix-rtos-tests`
6. `phoenix-rtos-devices`
7. `phoenix-rtos-filesystems`

Reasoning:

- the kernel and `plo` must first stop assuming `zynqmp`
- build and project glue must then be able to assemble and boot a Raspberry Pi image
- tests should come online as early as possible to prevent blind bring-up
- runtime device drivers matter after the boot path is real

## 3. First Concrete Milestone By Repository

### Milestone A: generic AArch64 cleanup

Main repos:

- `phoenix-rtos-kernel`
- `plo`
- `phoenix-rtos-build`

Expected results:

- non-`zynqmp` AArch64 target builds
- generic DTB parser path exists
- generic timer path exists or is clearly isolated

### Milestone B: Pi 4 `plo` boots

Main repos:

- `plo`
- `phoenix-rtos-build`
- `phoenix-rtos-project`
- `phoenix-rtos-tests`

Expected results:

- firmware boots `plo`
- UART output appears
- DTB is parsed
- image handoff path exists

### Milestone C: Pi 4 kernel boots

Main repos:

- `phoenix-rtos-kernel`
- `plo`
- `phoenix-rtos-project`
- `phoenix-rtos-tests`

Expected results:

- kernel banner
- shell on UART
- repeated reboot success

### Milestone D: Pi 4 persistent storage and low-speed I/O

Main repos:

- `phoenix-rtos-devices`
- `phoenix-rtos-filesystems`
- `phoenix-rtos-project`
- `phoenix-rtos-tests`

Expected results:

- block device access
- persistent rootfs
- GPIO, I2C, SPI, PWM tests

### Milestone E: Pi 4 networking and USB

Main repos:

- `phoenix-rtos-devices`
- `phoenix-rtos-kernel` if PCIe primitives land there
- `phoenix-rtos-tests`

Expected results:

- GENET works
- PCIe root complex works
- xHCI works
- USB storage enumeration works

### Milestone F: Pi 5 bootstrap

Main repos:

- `phoenix-rtos-kernel`
- `plo`
- `phoenix-rtos-build`
- `phoenix-rtos-project`
- `phoenix-rtos-tests`

Expected results:

- Pi 5 `plo` boot
- UART
- single-core kernel

### Milestone G: Pi 5 RP1 native support

Main repos:

- `phoenix-rtos-devices`
- `phoenix-rtos-kernel`
- `phoenix-rtos-tests`

Expected results:

- RP1-backed GPIO/UART/I2C/SPI/MMC
- RP1 Ethernet
- RP1 xHCI

## 4. File-Level Watchlist

These upstream files are high-value references and should be checked before writing corresponding Phoenix code:

### Phoenix

- `phoenix-rtos-kernel/hal/aarch64/dtb.c`
- `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `plo/hal/aarch64/Makefile`
- `phoenix-rtos-build/target/aarch64.mk`
- `phoenix-rtos-project/_targets/aarch64a53/zynqmp/preinit.plo.yaml`
- `phoenix-rtos-tests/trunner/harness/plo.py`

### Raspberry Pi Linux

- `arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b.dts`
- `arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts`
- `arch/arm64/boot/dts/broadcom/rp1.dtsi`
- `drivers/mmc/host/sdhci-brcmstb.c`
- `drivers/net/ethernet/broadcom/genet/bcmgenet.c`
- `drivers/pinctrl/bcm/pinctrl-bcm2835.c`
- `drivers/mfd/rp1.c`
- `drivers/firmware/rp1-fw.c`

### BSD

- `freebsd-src/sys/arm64/broadcom/genet/if_genet.c`

## 5. When To Split Work Into Separate Sessions

Open a dedicated session when the change set crosses one of these boundaries:

- common AArch64 refactor versus board-specific bring-up
- boot-path work versus runtime device-driver work
- emulator-lane work versus real-hardware automation work
- Pi 4 work versus Pi 5 work

This reduces context sprawl and makes documentation updates more reliable.
