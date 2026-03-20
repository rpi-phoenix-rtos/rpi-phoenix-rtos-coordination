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
- `phoenix-rtos-kernel/hal/Makefile`
- `phoenix-rtos-kernel/hal/timer.h`
- `phoenix-rtos-kernel/hal/tlb/tlb.c`
- `phoenix-rtos-kernel/hal/tlb/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/config.h`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/console.c`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/timer.c`
- `phoenix-rtos-kernel/proc/threads.c`

### Loader

- `plo/hal/aarch64/Makefile`
- `plo/hal/aarch64/generic/_init.S`
- `plo/hal/aarch64/generic/config.h`
- `plo/hal/aarch64/zynqmp/hal.c`
- `plo/hal/aarch64/zynqmp/console.c`
- `plo/cmds/script.c`
- `plo/cmds/kernel.c`
- `plo/cmds/app.c`
- `plo/cmds/wait.c`
- `plo/phfs/phfs.h`

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

## 4. Raspberry Pi Official Documentation

- Raspberry Pi configuration and boot documentation:
  <https://www.raspberrypi.com/documentation/computers/config_txt.html>
  Important sections: `initramfs`, `ramfsfile`, `ramfsaddr`, `auto_initramfs`

- Raspberry Pi configuration landing page:
  <https://www.raspberrypi.com/documentation/computers/configuration.html>

- Raspberry Pi remote-access documentation:
  <https://www.raspberrypi.com/documentation/computers/remote-access.html>
  Important section: "Network boot your Raspberry Pi"

- Raspberry Pi 4 boot security whitepaper:
  <https://pip.raspberrypi.com/categories/685-whitepapers-app-notes-compliance-guides/documents/RP-004651-WP/Raspberry-Pi-4-Boot-Security.pdf>

## 5. Raspberry Pi Hardware Documents

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

### Crucial Pi 4 Linux source paths

- `arch/arm/boot/dts/broadcom/bcm2711.dtsi`
- `arch/arm/boot/dts/broadcom/bcm2711-rpi-ds.dtsi`
- `arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b.dts`
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

- QEMU issue tracker example for `raspi4b` limitations:
  <https://gitlab.com/qemu-project/qemu/-/issues/3013>

- QEMU issue on `raspi4b` DTB dumping:
  <https://gitlab.com/qemu-project/qemu/-/issues/2733>

Re-verify:

- exact QEMU version behavior
- supported `raspi4b` devices

Current local finding to preserve:

- `qemu-system-aarch64 -machine help` inside `phoenix-dev` currently lists `raspi0`, `raspi1ap`, `raspi2b`, `raspi3ap`, and `raspi3b`, but not `raspi4b`
- on this workstation, pre-hardware Pi 4 validation must therefore stay on generic `virt` plus Pi 4 artifact inspection until either real hardware is used or a newer QEMU build provides `raspi4b`

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
