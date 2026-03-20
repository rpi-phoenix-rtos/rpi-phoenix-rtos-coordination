# Source Artifacts

This file indexes the most important websites, repositories, documents, and source paths for the Phoenix RTOS Raspberry Pi port.

## 1. Phoenix RTOS Official Documentation

- Phoenix build script:
  <https://docs.phoenix-rtos.com/latest/building/script.html>

- Phoenix architecture:
  <https://docs.phoenix-rtos.com/latest/architecture/index.html>

- Phoenix HAL docs:
  <https://docs.phoenix-rtos.com/latest/kernel/hal/index.html>

- Phoenix USB host stack:
  <https://docs.phoenix-rtos.com/latest/usb/usbhost.html>

- Phoenix shell:
  <https://docs.phoenix-rtos.com/latest/utils/psh/index.html>

- Phoenix Linux build page:
  <https://docs.phoenix-rtos.com/latest/building/linux.html>

- Phoenix tests docs:
  <https://docs.phoenix-rtos.com/latest/tests/index.html>

## 2. Phoenix RTOS Upstream Repositories

- Project umbrella:
  <https://github.com/phoenix-rtos>

- Kernel:
  <https://github.com/phoenix-rtos/phoenix-rtos-kernel>

- Loader:
  <https://github.com/phoenix-rtos/plo>

- Devices:
  <https://github.com/phoenix-rtos/phoenix-rtos-devices>

- Filesystems:
  <https://github.com/phoenix-rtos/phoenix-rtos-filesystems>

- Build scripts:
  <https://github.com/phoenix-rtos/phoenix-rtos-build>

- libphoenix:
  <https://github.com/phoenix-rtos/libphoenix>

- Core libraries:
  <https://github.com/phoenix-rtos/phoenix-rtos-corelibs>

- Project/targets:
  <https://github.com/phoenix-rtos/phoenix-rtos-project>

- Tests:
  <https://github.com/phoenix-rtos/phoenix-rtos-tests>

- Host utilities:
  <https://github.com/phoenix-rtos/phoenix-rtos-hostutils>

- lwIP integration:
  <https://github.com/phoenix-rtos/phoenix-rtos-lwip>

- Ports:
  <https://github.com/phoenix-rtos/phoenix-rtos-ports>

- POSIX server:
  <https://github.com/phoenix-rtos/phoenix-rtos-posixsrv>

- USB stack:
  <https://github.com/phoenix-rtos/phoenix-rtos-usb>

- Utilities:
  <https://github.com/phoenix-rtos/phoenix-rtos-utils>

- Documentation:
  <https://github.com/phoenix-rtos/phoenix-rtos-doc>

## 3. Crucial Phoenix Source Paths

### Kernel

- `phoenix-rtos-kernel/hal/aarch64/hal.c`
- `phoenix-rtos-kernel/hal/aarch64/_init.S`
- `phoenix-rtos-kernel/hal/aarch64/dtb.c`
- `phoenix-rtos-kernel/hal/aarch64/aarch64.h`
- `phoenix-rtos-kernel/hal/aarch64/gtimer.c`
- `phoenix-rtos-kernel/hal/aarch64/gtimer.h`
- `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.c`
- `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.h`
- `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `phoenix-rtos-kernel/hal/aarch64/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- `phoenix-rtos-kernel/hal/Makefile`
- `phoenix-rtos-kernel/hal/timer.h`
- `phoenix-rtos-kernel/hal/tlb/tlb.c`
- `phoenix-rtos-kernel/hal/tlb/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/config.h`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/console.c`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/timer.c`
- `phoenix-rtos-kernel/proc/name.c`
- `phoenix-rtos-kernel/proc/msg.c`
- `phoenix-rtos-kernel/syscalls.c`
- `phoenix-rtos-kernel/proc/threads.c`

### Loader

- `plo/hal/aarch64/Makefile`
- `plo/hal/aarch64/generic/_init.S`
- `plo/hal/aarch64/generic/config.h`
- `plo/ld/aarch64a53-generic.ldt`
- `plo/hal/aarch64/zynqmp/hal.c`
- `plo/hal/aarch64/zynqmp/console.c`
- `plo/cmds/script.c`
- `plo/cmds/call.c`
- `plo/cmds/kernel.c`
- `plo/cmds/app.c`
- `plo/cmds/wait.c`
- `plo/phfs/phfs.c`
- `plo/phfs/phfs.h`
- `plo/devices/ram-storage/ramdrv.c`

### Build and target definitions

- `phoenix-rtos-project/.gitmodules`
- `phoenix-rtos-build/build.sh`
- `phoenix-rtos-build/build-core-aarch64a53-zynqmp.sh`
- `phoenix-rtos-build/target/aarch64.mk`
- `phoenix-rtos-build/Makefile.common`
- `phoenix-rtos-project/_targets/aarch64a53/generic/build.project`
- `phoenix-rtos-project/_targets/aarch64a53/generic/preinit.plo.yaml`
- `phoenix-rtos-project/_targets/aarch64a53/generic/user.plo.yaml`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/config.txt`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/user.plo.yaml`
- `phoenix-rtos-project/_targets/aarch64a53/zynqmp/build.project`
- `phoenix-rtos-project/_targets/aarch64a53/zynqmp/preinit.plo.yaml`
- `phoenix-rtos-project/_targets/aarch64a53/zynqmp/user.plo.yaml`
- `phoenix-rtos-project/_projects/aarch64a53-zynqmp-qemu/build.project`
- `phoenix-rtos-project/scripts/aarch64a53-zynqmp-qemu.sh`

### Tests

- `phoenix-rtos-tests/runner.py`
- `phoenix-rtos-tests/trunner/dut.py`
- `phoenix-rtos-tests/trunner/target/emulated.py`
- `phoenix-rtos-tests/trunner/harness/plo.py`
- `phoenix-rtos-tests/trunner/host.py`
- `phoenix-rtos-build/scripts/run_project_tests.py`

### Filesystems and runtime storage

- `phoenix-rtos-filesystems/fat/*`
- `phoenix-rtos-filesystems/ext2/*`
- `phoenix-rtos-filesystems/jffs2/*`
- `phoenix-rtos-filesystems/dummyfs/*`

### Early userspace boot blocker paths

- `libphoenix/unistd/file.c`
  Important because `create_dev()` currently gates `/dev/tty0` and `/dev/console` registration.

- `libphoenix/unistd/sys.c`
  Important because the bounded `pl011-tty` retry path currently reaches `usleep(100000)` here and then blocks inside the kernel `nsleep()` path.

- `phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
  Important because the current fast lane uses raw UART-side diagnostics here to bound the first console-registration blocker.

- `phoenix-rtos-filesystems/dummyfs/srv.c`
  Important because the `devfs` instance is started here with `dummyfs -N devfs -D`, and the next fast diagnostic step targets its non-filesystem namespace registration and `mtLookup` servicing path.

- `phoenix-rtos-kernel/proc/name.c`
  Important because `proc_portLookup()` performs the kernel-side cached lookup and forwards `mtLookup` messages to the registered server port.

- `phoenix-rtos-kernel/proc/msg.c`
  Important because `proc_send()` blocks until the destination server receives and responds, which is the current reason a non-responsive `devfs` lookup can stall the caller.

- `phoenix-rtos-kernel/proc/threads.c`
  Important because `proc_threadNanoSleep()` and `_threads_programWakeup()` now define the next bounded diagnostic target after both fast lanes proved that the first retry path sleeps and never wakes.

- `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.c`
  Important because the next bounded timer diagnostic needs to expose which common AArch64 timer source and IRQ are selected from the DTB before the missing wakeup interrupt should arrive.

- `phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
  Important because `hal_timerRegister()` and `hal_timerSetWakeup()` are now the narrowest common AArch64 timer arming path after `proc/threads.c` proved that sleep enqueue and wakeup programming are reached on the generic lane.

- `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
  Important because the next bounded diagnostic needs to prove whether the selected timer IRQ is actually registered in GICv2 and whether it is ever dispatched before control would reach `threads_timeintr()`.

- `phoenix-rtos-kernel/hal/aarch64/aarch64.h`
  Important because the next bounded experiment is explicit synchronization after architectural timer sysreg writes for both the physical and virtual timer paths.

## 4. Raspberry Pi Official Documentation

- Raspberry Pi configuration and boot documentation:
  <https://www.raspberrypi.com/documentation/computers/config_txt.html>
  Important sections: `initramfs`, `ramfsfile`, `ramfsaddr`, `auto_initramfs`

- Raspberry Pi configuration landing page:
  <https://www.raspberrypi.com/documentation/computers/configuration.html>

- Raspberry Pi device-tree documentation:
  <https://www.raspberrypi.com/documentation/configuration/device-tree>
  Important because it states that the Raspberry Pi firmware loader customizes the DTB before launching the kernel.

- Raspberry Pi device-tree documentation source:
  <https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/configuration/device-tree.adoc>
  Important because it is the source document to consult when DT alias,
  overlay, `dtparam`, or firmware-merged DTB behavior becomes relevant during
  bring-up.

- Raspberry Pi documentation computers folder:
  <https://github.com/raspberrypi/documentation/tree/master/documentation/asciidoc/computers>
  Important because future Pi 4 boot, DT, UART, and configuration debugging
  should consult the original documentation set, not only Linux DTS files.

- Raspberry Pi boot/configuration reference:
  <https://www.raspberrypi.com/documentation/configuration/bootconfig.md>

## 5. External Bare-Metal Reference Repositories

- `sypstraw/rpi4-osdev`:
  <https://github.com/sypstraw/rpi4-osdev>
  Local clone: `/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev`
  Important because it provides a staged Pi 4 bare-metal tutorial with compact
  examples for `_start`, low-peripheral-mode MMIO, mailbox framebuffer access,
  mini-UART wiring, and simple multicore wakeups.

- `rsta2/circle`:
  <https://github.com/rsta2/circle>
  Local clone: `/Users/witoldbolt/phoenix-rpi/external/circle`
  Important because it is a mature Raspberry Pi bare-metal environment with
  Pi 4 AArch64 startup, physical-timer, GIC-400, DTB, mailbox, PCIe, USB, and
  later Pi 5 RP1 support. This is the strongest external bare-metal reference
  for current Pi 4 boot-first work.

- `markCwatson/rpi-os`:
  <https://github.com/markCwatson/rpi-os>
  Local clone: `/Users/witoldbolt/phoenix-rpi/external/rpi-os`
  Important because it is a short Pi 4 hobby kernel with a compact EL3-to-EL1
  handoff, vector table, mini-UART, and scheduler skeleton. It is useful as a
  quick sanity reference, but its timer and interrupt path is still based on
  the legacy system timer and legacy IRQ controller.

- OSDev `Raspberry Pi Bare Bones`:
  <https://wiki.osdev.org/Raspberry_Pi_Bare_Bones>
  Important because it is a compact tertiary reference for AArch64 Pi 3 or 4
  boot entry, DTB handoff in `x0`, current-firmware secondary-core release
  slots, Pi 4 MMIO base selection, and PL011 setup.

## 6. External Reference Source Paths

- `external/rpi4-osdev/part1-bootstrapping/boot.S`
  Important because it shows the simplest Pi 4 `_start` sequence: `mpidr_el1`,
  single-core gating, BSS clear, and C entry.

- `external/rpi4-osdev/part10-multicore/boot.S`
  Important because it explicitly sets `cntfrq_el0 = 54000000` and clears
  `cntvoff_el2`, which is relevant when reasoning about Pi 4 EL handoff and
  generic timer access.

- `external/rpi4-osdev/part4-miniuart/io.c`
  Important because it documents low-peripheral-mode MMIO, GPIO14 or GPIO15
  ALT5 mini-UART setup, and `AUX_UART_CLOCK = 500000000`.

- `external/rpi4-osdev/part13-interrupts/kernel/irq.c`
  Important because it demonstrates the legacy BCM2711 system timer plus legacy
  IRQ controller path and should therefore not be confused with Phoenix’s
  current architectural timer plus GIC debugging lane.

- `external/circle/lib/startup64.S`
  Important because it shows Pi 4 AArch64 entry from an arm stub in EL2,
  explicit `CNTHCTL_EL2` setup, `CNTVOFF_EL2 = 0`, and return to EL1.

- `external/circle/include/circle/bcm2711int.h`
  Important because it defines `ARM_IRQLOCAL0_CNTPNS = GIC_PPI(14)`, confirming
  that the Pi 4 non-secure physical timer IRQ is `30`.

- `external/circle/include/circle/bcm2836.h`
  Important because it defines the Pi 4 local interrupt controller base
  `ARM_LOCAL_BASE = 0xFF800000` and the specific registers
  `ARM_LOCAL_TIMER_INT_CONTROL0` and `ARM_LOCAL_IRQ_PENDING0` used by Circle's
  physical-timer path.

- `external/circle/lib/timer.cpp`
  Important because Circle requires the physical counter on Pi 4 and programs
  `CNTP_CVAL_EL0` plus `CNTP_CTL_EL0` there.

- `external/circle/lib/interruptgic.cpp`
  Important because it is a compact external reference for GIC-400
  initialization in non-secure Pi 4 AArch64 mode.

- `external/circle/lib/interrupt.cpp`
  Important because Circle explicitly enables `ARM_LOCAL_TIMER_INT_CONTROL0`
  bit `1` for `ARM_IRQLOCAL0_CNTPNS` and checks `ARM_LOCAL_IRQ_PENDING0`
  before dispatching the local physical timer path.

- `external/circle/lib/sysinit.cpp`
  Important because Circle writes `ARM_LOCAL_PRESCALER = 39768216U` on Pi 4,
  which is now the next bounded local-block follow-up after the route-enable
  experiment alone failed to change local pending or GIC dispatch.

- local QEMU 10.2.2 source:
  - `/home/witoldbolt.guest/src/qemu-10.2.2/hw/arm/bcm2838.c`
    Important because it wires Pi 4 `GTIMER_PHYS` directly to GIC PPI 14 in
    the `raspi4b` model.
  - `/home/witoldbolt.guest/src/qemu-10.2.2/hw/intc/bcm2836_control.c`
    Important because it proves QEMU does model the BCM2836 local interrupt
    controller, but that block is not on the active Pi 4 physical timer path
    used by `bcm2838.c`.

- `external/circle/lib/bcmmailbox.cpp`
- `external/circle/lib/bcmpropertytags.cpp`
- `external/circle/lib/bcmframebuffer.cpp`
  Important because they are later-stage references for mailbox property calls
  and framebuffer bring-up.

- `external/circle/lib/bcmpciehostbridge.cpp`
- `external/circle/lib/macb.cpp`
  Important because they are later-stage references for Pi 4 PCIe and network
  subsystems.

- `external/rpi-os/src/boot.S`
  Important because it is a compact Pi 4 EL3-to-EL1 handoff example with
  explicit `SCR_EL3`, `HCR_EL2`, and `SPSR_EL3` programming.

- `external/rpi-os/src/irq.S`
  Important because it provides a short vector-table and save-restore example
  for EL1 exception handling.

- `external/rpi-os/src/mini_uart.c`
  Important because it reinforces the same low-peripheral-mode mini-UART setup
  choices seen in other Pi 4 bare-metal examples.

- `external/rpi-os/src/timer.c`
  Important because it is another example of the legacy BCM2711 system timer
  path, which should not be treated as evidence about Phoenix’s current GIC PPI
  issue.

- OSDev `Raspberry Pi Bare Bones` section `Pi 3, 4`
  Important because it explicitly states:
  - Pi 3 and Pi 4 AArch64 boot at `0x80000`
  - `x0` contains the DTB pointer on the primary core
  - recent firmware keeps only core 0 running while secondary cores wait behind
    release slots at `0xE0`, `0xE8`, and `0xF0`

- OSDev `Raspberry Pi Bare Bones` PL011 example
  Important because it demonstrates mailbox-assisted PL011 clock programming on
  Pi 3 or 4 before setting integer and fractional baud divisors.
  Important because it documents `config.txt`, overlays, and runtime `/chosen`
  properties relevant to the firmware-merged DTB.

- Raspberry Pi serial/UART reference:
  <https://www.raspberrypi.com/documentation/configuration/serial.md>
  Important because it documents Pi 4 UART roles and the earlycon MMIO
  addresses for the auxiliary UART and PL011.

- Raspberry Pi remote-access documentation:
  <https://www.raspberrypi.com/documentation/computers/remote-access.html>
  Important section: "Network boot your Raspberry Pi"

- Raspberry Pi 4 boot security whitepaper:
  <https://pip.raspberrypi.com/categories/685-whitepapers-app-notes-compliance-guides/documents/RP-004651-WP/Raspberry-Pi-4-Boot-Security.pdf>

- Raspberry Pi firmware repository boot tree:
  <https://github.com/raspberrypi/firmware/tree/master/boot>
  Useful for:
  - `start4.elf`
  - `fixup4.dat`
  - `start4db.elf`
  - `fixup4db.dat`
  - `start4cd.elf`
  - `fixup4cd.dat`
  - `bcm2711-rpi-4-b.dtb`
  Re-verify:
  - exact filenames required by the current Pi 4 bootloader release
  - whether a specific test baseline should pin the firmware repo commit rather than using the moving `master` branch

- Current validated Pi 4 firmware DTB source for the `raspi4b` QEMU lane:
  - repo commit: `63ad7e7980b030cb4649ecedf2255c9226e5a1e8`
  - path: `boot/bcm2711-rpi-4-b.dtb`
  - observed size: `56373` bytes
  - current important decompiled finding:
    - `memory@0 { reg = <0x00 0x00 0x00>; }`
  - current confirmed structural findings:
    - root:
      - `#address-cells = 2`
      - `#size-cells = 1`
    - `/soc`:
      - `#address-cells = 1`
      - `#size-cells = 1`
      - `ranges = <0x7e000000 0x0 0xfe000000 0x1800000 0x7c000000 0x0 0xfc000000 0x2000000 0x40000000 0x0 0xff800000 0x800000>`
    - `/soc/interrupt-controller@40041000`:
      - `compatible = "arm,gic-400"`
      - `reg = <0x40041000 0x1000 0x40042000 0x2000 0x40044000 0x2000 0x40046000 0x2000>`
  Important because:
  - Raspberry Pi firmware normally customizes this DTB at boot, but direct `qemu-system-aarch64 -M raspi4b` validation currently uses the file without firmware-time customization
  Re-verify:
  - before using this exact commit as a long-lived baseline, because the firmware repository is a moving target

## 5. Raspberry Pi Hardware Documents

- Raspberry Pi 4 Model B official specifications:
  <https://www.raspberrypi.com/products/raspberry-pi-4-model-b/specifications/>

- BCM2711 ARM Peripherals:
  <https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf>

- BCM2712 peripherals / SoC data:
  <https://datasheets.raspberrypi.com/bcm2712/bcm2712-peripherals.pdf>
  Re-verify: URL and document availability can change.

- RP1 Peripherals:
  <https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf>

## 6. Raspberry Pi Linux Reference Tree

- Raspberry Pi Linux:
  <https://github.com/raspberrypi/linux>
  Re-verify:
  - downstream branch tips change over time; as of `2026-03-20`, the active
    branch family includes `rpi-6.12.y`, `rpi-6.19.y`, and `rpi-7.0.y`

### Crucial Pi 4 Linux source paths

- `arch/arm/boot/dts/broadcom/bcm2711.dtsi`
- `arch/arm/boot/dts/broadcom/bcm2711-rpi-ds.dtsi`
- `arch/arm/boot/dts/broadcom/bcm2711-rpi.dtsi`
- `arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts`
- `drivers/mmc/host/sdhci-brcmstb.c`
- `drivers/net/ethernet/broadcom/genet/bcmgenet.c`
- `drivers/pinctrl/bcm/pinctrl-bcm2835.c`
- `drivers/i2c/busses/i2c-bcm2835.c`
- `drivers/spi/spi-bcm2835.c`
- `drivers/mailbox/bcm2835-mailbox.c`
- `drivers/dma/bcm2835-dma.c`
- `drivers/clocksource/arm_arch_timer.c`
- `drivers/clocksource/bcm2835_timer.c`
- `drivers/watchdog/bcm2835_wdt.c`
- `drivers/gpu/drm/vc4/*`
- `drivers/gpu/drm/v3d/*`

### Crucial Pi 5 Linux source paths

- `arch/arm64/boot/dts/broadcom/bcm2712.dtsi`
- `arch/arm64/boot/dts/broadcom/bcm2712-rpi.dtsi`
- `arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts`
- `arch/arm64/boot/dts/broadcom/rp1.dtsi`
- `drivers/mfd/rp1.c`
- `drivers/firmware/rp1-fw.c`
- `drivers/pinctrl/pinctrl-rp1.c`
- `drivers/clk/clk-rp1.c`
- `drivers/mailbox/rp1-mailbox.c`
- `drivers/pwm/pwm-rp1.c`
- `drivers/net/ethernet/cadence/*`
- `drivers/misc/rp1-pio.c`
- `drivers/gpu/drm/rp1/*`
- `drivers/media/platform/raspberrypi/rp1_cfe/*`

## 7. BSD and Other OS References

- FreeBSD hardware support notes:
  <https://www.freebsd.org/releases/14.4R/hardware/>

- FreeBSD source:
  <https://github.com/freebsd/freebsd-src>

- FreeBSD GENET driver:
  `sys/arm64/broadcom/genet/if_genet.c`

- FreeBSD Broadcom config:
  `sys/arm64/conf/std.broadcom`

- NetBSD Raspberry Pi notes:
  <https://wiki.netbsd.org/ports/evbarm/raspberry_pi/>

Important NetBSD findings to remember:

- NetBSD 10 documents Pi 4 support and Pi 5 support via UEFI
- NetBSD still notes significant gaps for Pi 5 and some Pi 4 xHCI/runtime cases

## 8. Emulator References

- Linux ARM architectural timer driver reference:
  <https://gbmc.googlesource.com/linux/%2B/refs/heads/linux-6.1.y/drivers/clocksource/arm_arch_timer.c>
  Useful for timer-source selection policy and generic timer register usage patterns.

- QEMU project homepage and latest releases:
  <https://www.qemu.org/>

- QEMU Raspberry Pi board documentation:
  <https://www.qemu.org/docs/master/system/arm/raspi.html>

- QEMU issue tracker example for `raspi4b` limitations:
  <https://gitlab.com/qemu-project/qemu/-/issues/3013>

- QEMU issue on `raspi4b` DTB dumping:
  <https://gitlab.com/qemu-project/qemu/-/issues/2733>

Re-verify:

- exact QEMU version behavior
- supported `raspi4b` devices

Current local finding to preserve:

- packaged `/usr/bin/qemu-system-aarch64` inside `phoenix-dev` is `8.2.2` and does not list `raspi4b`
- VM-local `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64` is `10.2.2` and does list `raspi4b`
- the first Phoenix Pi 4 `raspi4b` smoke under QEMU `10.2.2` required `-smp 4` and then timed out with no serial output, so the current blocker moved into emulated boot progress rather than QEMU board availability
- current local QEMU `10.2.2` `raspi4b` does not support `dumpdtb`; direct `-machine raspi4b,dumpdtb=...` fails with `This machine doesn't have an FDT`
- current Pi 4 QEMU validation therefore needs an explicit external DTB source
- current Pi 4 QEMU validation also does not include Raspberry Pi firmware DTB customization, so the payload DTB may need explicit QEMU-only fixes such as a non-zero `memory@0/reg` value
- local QEMU gdbstub is now a proven diagnostic path for this port:
  - QEMU docs:
    <https://www.qemu.org/docs/master/system/gdb.html>
  - GDB remote docs:
    <https://sourceware.org/gdb/current/onlinedocs/gdb.html/Connecting.html>
- local `10.2.2` QEMU source path for the current GIC model:
  - `/home/witoldbolt.guest/src/qemu-10.2.2/hw/intc/arm_gic.c`
  - current important fact:
    the CPU-interface read switch exposes `HPPIR` at `0x18` and `ABPR` at
    `0x1c`, but no explicit read case for `0x28` `AHPPIR`
- current decisive pre-map breakpoint result at `_hal_interruptsInit + 64`:
  - generic `virt`:
    - `gicd = 0x08000000`
    - `gicc = 0x08010000`
  - Pi 4 before the bounded `dtb.c` fix:
    - `gicd = 0x0`
    - `gicc = 0x0`
  - Pi 4 after the bounded `dtb.c` fix:
    - `gicd = 0xff841000`
    - `gicc = 0xff842000`

Important Raspberry Pi kernel-source findings to preserve:

- prefer the Raspberry Pi kernel DTS/DTSI files over decompiled DTBs when the question is board intent or which properties are expected to be firmware-filled
- `raspberrypi/linux` `rpi-6.12.y` and `rpi-6.19.y` both contain:
  - `arch/arm/boot/dts/broadcom/bcm2711-rpi.dtsi`
    - `memory@0` comment: `Will be filled by the bootloader`
  - `arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts`
    - `chosen { stdout-path = "serial1:115200n8"; }`
    - comment: `8250 auxiliary UART instead of pl011`
- consequence for Phoenix:
  - bootloader-time DTB customization is a real dependency for faithful Pi 4 DT behavior
  - naive `stdout-path` alias resolution would currently steer Phoenix toward the auxiliary UART, not the existing PL011 console path
- checked on `2026-03-20`:
  - `rpi-6.19.y` and `rpi-7.0.y` are currently identical for:
    - `arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts`
    - `arch/arm/boot/dts/broadcom/bcm2711-rpi.dtsi`
  - use `rpi-6.19.y` or newer as the primary source-DTS reference, and
    re-verify before depending on exact branch contents in later sessions

Raspberry Pi kernel branch pattern checked from the remote on `2026-03-20`:

- the repository exposes rolling `rpi-X.Y.y` heads such as `rpi-6.12.y`, `rpi-6.19.y`, and `rpi-7.0.y`
  Re-verify:
  - before pinning one branch as the long-lived reference baseline, because Raspberry Pi keeps advancing this branch family

## 9. Host and VM Tooling References

- Lima overview:
  <https://lima-vm.io/docs/>

- Lima `vz` VM type:
  <https://lima-vm.io/docs/config/vmtype/vz/>

- Lima network overview:
  <https://lima-vm.io/docs/config/network/>

- Lima VMNet networks:
  <https://lima-vm.io/docs/config/network/vmnet/>

- Lima `limactl sudoers` for `socket_vmnet`:
  <https://lima-vm.io/docs/reference/limactl_sudoers/>

- Docker Desktop for Mac:
  <https://docs.docker.com/desktop/setup/install/mac-install/>

- Docker Desktop virtual machine manager overview:
  <https://docs.docker.com/desktop/features/vmm/>

- Docker CLI binaries note for macOS:
  <https://docs.docker.com/engine/install/binaries/>

- Colima project:
  <https://github.com/abiosoft/colima>

- Tart quick start:
  <https://tart.run/quick-start/>

- Lume overview:
  <https://cua.ai/docs/lume>

## 10. Auxiliary Bare-Metal Reference

- RPi4 OS bare-metal tutorial:
  <https://www.rpi4os.com/>

Useful mainly for:

- early UART bring-up ideas
- mailbox/framebuffer experiments
- rough low-level Raspberry Pi bring-up patterns

Do not treat it as an architectural authority for Phoenix.

## 11. Time-Sensitive Topics Requiring Fresh Browsing

Before implementing features that depend on them, re-check:

- Raspberry Pi EEPROM and bootloader behavior
- Raspberry Pi 4 network boot, TFTP, and `boot.img` behavior
- Lima networking and `socket_vmnet` behavior on current macOS
- Docker Desktop support policy and Apple Silicon behavior if Docker is introduced
- current Pi 4 / Pi 5 firmware options
- QEMU Raspberry Pi emulation status
- Pi 5 RP1 Linux driver state
- Pi 5 BSD support state

## 12. Local Style Anchors Already Observed

These are useful style anchors when writing new code:

- `phoenix-rtos-kernel/hal/aarch64/hal.c`
- `plo/hal/aarch64/zynqmp/hal.c`
- `phoenix-rtos-devices/tty/zynq-uart/zynq-uart.c`
- `phoenix-rtos-build/Makefile.common`

## 13. Current Fast-Lane Diagnostic Paths

These are the current high-signal common AArch64 paths for the early generic and Pi 4 QEMU lanes:

- `phoenix-rtos-kernel/hal/aarch64/aarch64.h`
- `phoenix-rtos-kernel/hal/aarch64/gtimer.h`
- `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.c`
- `phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
- `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `plo/hal/aarch64/generic/_init.S`
- `plo/hal/aarch64/generic/interrupts.c`
- `plo/hal/aarch64/generic/hal.c`
- `plo/hal/aarch64/zynqmp/_init.S`
- `phoenix-rtos-kernel/proc/threads.c`

Current preserved clue:

- as of `STEP-0152`, the selected architectural timer is registered and armed, but no timer IRQ is dispatched on the generic fast lane even after explicit post-write instruction barriers
- as of `STEP-0154`, the generic fast lane reads back the selected timer as armed with `ctl 0x1` and a live non-zero `tval`, so the next bounded clue is GIC-side IRQ state rather than timer programming
- as of `STEP-0156`, the generic fast lane reads the selected timer IRQ back as `grp 0 en 0`, and the existing `plo` generic handoff path exits EL3 to EL1 non-secure, so Group 1 configuration is now the next bounded GIC experiment
- as of `STEP-0158`, the kernel-side attempt to move the selected timer IRQ to Group 1 still leaves the generic fast lane at `grp 0 en 0`, which points the next bounded experiment to generic `plo` EL3 GIC initialization
- as of `STEP-0160`, generic `plo` EL3 GIC initialization is enough to restore timer dispatch and tty registration on the generic fast lane, so the next bounded Pi 4 clue is the loader entry EL on `raspi4b`
- as of `STEP-0162`, both fast lanes enter generic `plo` at `EL3`, and the currently reused Pi 4 DTB input decompiles to a 274-byte stub with only `compatible` and one `memory@0` node
