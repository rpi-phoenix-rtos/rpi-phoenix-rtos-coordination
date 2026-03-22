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
  Important because the generic AArch64 path needed corrected syspage offsets
  once `hal_syspage_t` grew a `graphmode` payload for the Pi 4 HDMI text lane.
- `phoenix-rtos-kernel/hal/aarch64/dtb.c`
- `phoenix-rtos-kernel/hal/aarch64/aarch64.h`
- `phoenix-rtos-kernel/hal/aarch64/gtimer.c`
- `phoenix-rtos-kernel/hal/aarch64/gtimer.h`
- `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.c`
- `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.h`
- `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `phoenix-rtos-kernel/hal/aarch64/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- `phoenix-rtos-kernel/hal/aarch64/generic/generic.c`
- `phoenix-rtos-kernel/hal/Makefile`
- `phoenix-rtos-kernel/hal/timer.h`
- `phoenix-rtos-kernel/hal/tlb/tlb.c`
- `phoenix-rtos-kernel/hal/tlb/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/config.h`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/console.c`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/timer.c`
- `phoenix-rtos-kernel/proc/name.c`
- `phoenix-rtos-kernel/proc/msg.c`
- `phoenix-rtos-kernel/posix/posix.c`
- `phoenix-rtos-kernel/syscalls.c`
- `phoenix-rtos-kernel/proc/threads.c`

### Loader

- `plo/hal/aarch64/Makefile`
- `plo/hal/aarch64/generic/_init.S`
- `plo/hal/aarch64/generic/config.h`
- `plo/plo.c`
  Important because the current generic loader call order is
  `hal_init() -> hal_customInit() -> syspage_init()`, which means generic Pi 4
  framebuffer metadata must be published after `syspage_init()` rather than
  from the earlier `video_init()` path.
- `plo/ld/aarch64a53-generic.ldt`
- `plo/hal/aarch64/zynqmp/hal.c`
- `plo/hal/aarch64/zynqmp/console.c`
- `plo/syspage.c`
- `plo/syspage.h`
  Important because the current generic Pi 4 framebuffer path now uses a
  pointer-based `syspage_graphmodeSet()` helper after a bounded gdbstub session
  proved that packed `graphmode_t` by-value passing was unsafe on the active
  AArch64 path.
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
- `phoenix-rtos-project/_targets/build.common`
  Important because `b_mkscript_user()` currently aliases each `app` payload by
  `basename(path)`, which matters when the same binary must be staged twice
  under different boot aliases.
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/config.txt`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/user.plo.yaml`
- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/build.project`
- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/user.plo.yaml`
- `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
- `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`
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

- `libphoenix/unistd/dir.c`
  Important because `resolve_path()` and `safe_lookup()` are now part of the
  active `/dev/console` investigation path.

- `libphoenix/unistd/sys.c`
  Important because the bounded `pl011-tty` retry path currently reaches `usleep(100000)` here and then blocks inside the kernel `nsleep()` path.

- `phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
  Important because the current fast lane uses raw UART-side diagnostics here to bound the first console-registration blocker.
  It is also now the place where the optional `/dev/kbd0` bridge feeds cooked
  USB-keyboard bytes into the shared `libtty` path for Pi 4.

- `phoenix-rtos-devices/tty/usbkbd/usbkbd.c`
  Important because it is now the first generic Phoenix USB HID boot-keyboard
  class-driver foundation.

- `phoenix-rtos-devices/tty/usbkbd/srv.c`
  Important because it provides the process-driver wrapper for the same USB
  keyboard logic.

- `phoenix-rtos-devices/tty/usbkbd/Makefile`
  Important because it exposes both `libusbdrv-usbkbd` and `usbkbd`, matching
  Phoenix's existing USB class-driver structure.

- `phoenix-rtos-devices/pcie/server/pcie.c`
  Important because the platform-agnostic PCIe scan path now uses a small
  server-local config-space backend interface instead of hardcoding direct ECAM
  access throughout the scan logic. This is now the seam for the first BCM2711
  indexed-config backend as well as the later host-bridge initialization work.
  It now also contains the first BCM2711 backend-local reset and `MISC_CTRL`
  preparation hook.
  It also now contains the first BCM2711 link-state gating step:
  `PERST` release, link/RC-mode sampling, and downstream-access gating.
  It also now contains the first outbound-window and root-bridge shaping step:
  outbound window 0 programming, RC BAR2 programming, and root-bridge class
  shaping behind the sampled link-state gate.
  It also now contains the first bridge-exposure step:
  root-bridge cache-line, bus-number, memory-window, and command programming on
  bus `0` behind the sampled link-state gate.

- `phoenix-rtos-devices/pcie/server/Makefile`
  Important because it now carries the backend-selection build flag for the
  BCM2711 indexed-config path without forcing that backend onto other PCIe
  targets.

- `phoenix-rtos-devices/usb/xhci/xhci.c`
  Important because it now contains the compile-valid Pi 4 xHCI discovery and
  early-controller path.
  It now maps MMIO, validates the capability header, performs the first
  bounded controller reset, validates 4K page support and non-zero port count,
  and now also extracts the next controller-shape facts needed before later
  host-operation work:
  max slots, max scratchpad-buffer count, and context size.
  It now also extracts the doorbell and runtime-register offsets needed before
  later interrupter or ring design.
  It now also extracts the pre-interrupt controller limits needed before later
  event-ring work:
  max interrupters, interrupt moderation scale, and maximum ERST size.
  It now also extracts the remaining structural memory-layout capability bits:
  64-bit addressing support, scratchpad-restore support, and maximum primary
  stream array size.
  It now also extracts the first operational memory-layout register state:
  `CRCR` and `DCBAAP`, with minimal reserved-bit and non-`AC64` sanity checks.
  It now also allocates the first controller-owned memory objects:
  a 4K-aligned `DCBAA` page and a 64K-aligned first command-ring block.
  It now also performs the first bounded controller register-programming step:
  writes `DCBAAP`, `CRCR`, and `CONFIG`, then reads them back and validates the
  programmed state.
  The current code intentionally remains pre-root-hub, pre-ring, and
  pre-enumeration.

- `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
  Important because the current Pi 4 A72 project now opts into the
  `PL011_TTY_KBD_PATH` bridge policy without forcing that behavior on every
  PL011 target.
  It also now carries the BCM2711 PCIe host-base and host-window size
  constants used by the first indexed-config backend step.
  It also now carries the first outbound-window constants derived from the
  current Circle Pi 4 memory-map reference.

- `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
  Important because the Pi 4 A72 project now exports
  `PCI_EXPRESS_BCM2711_INDEXED_CFG=y`, which is the first build-level backend
  selection hook for BCM2711 PCIe work.
  It also now carries the documented Pi 4 QEMU DTB build knobs:
  `RPI4B_DTB_PATH` and `RPI4B_QEMU_MEMORY_SIZE`, which are required for the
  current passing Pi 4 `raspi4b` shell and HDMI smokes.

- `phoenix-rtos-filesystems/dummyfs/srv.c`
  Important because the `devfs` instance is started here with `dummyfs -N devfs -D`, and the next fast diagnostic step targets its non-filesystem namespace registration and `mtLookup` servicing path.

- `phoenix-rtos-devices/tty/pc-tty/ttypc_fbcon.c`
  Important because it already provides a Phoenix-native framebuffer text
  renderer that can likely be reused for Pi 4 HDMI text output once generic
  AArch64 `platformctl` exposes framebuffer geometry.

- `phoenix-rtos-devices/tty/pc-tty/ttypc_fbfont.h`
  Important because it already carries a bundled 8x16 bitmap font under a
  permissive retained license, which is likely sufficient for the first Pi 4
  HDMI text-console milestone.

- `phoenix-rtos-devices/tty/pc-tty/ttypc_vga.c`
  Important because it shows how the existing framebuffer renderer is integrated
  with Phoenix terminal state today, helping isolate the minimal reusable text
  drawing pieces from the IA32-specific VGA and PS/2 logic.

- `phoenix-rtos-kernel/proc/name.c`
  Important because `proc_portLookup()` performs the kernel-side cached lookup and forwards `mtLookup` messages to the registered server port.

- `phoenix-rtos-kernel/proc/msg.c`
  Important because `proc_send()` blocks until the destination server receives and responds, which is the current reason a non-responsive `devfs` lookup can stall the caller.

- `phoenix-rtos-kernel/posix/posix.c`
  Important because `open()` currently reaches `posix_open()`, and
  `posix_open()` uses `proc_lookup()` directly rather than `syscalls_lookup()`.

- `phoenix-rtos-kernel/proc/threads.c`
  Important because `proc_threadNanoSleep()` and `_threads_programWakeup()` now define the next bounded diagnostic target after both fast lanes proved that the first retry path sleeps and never wakes.

- `phoenix-rtos-utils/psh/psh.c`
  Important because `main()` waits on `lookup("/")`, dispatches into `pshapp`,
  and now provides the current shell-side visibility markers.

- `phoenix-rtos-utils/psh/pshapp/pshapp.c`
  Important because `psh_run()` carried the bounded retry-policy fix for the
  Pi 4 `/dev/console` startup race, and both fast lanes now reach
  `psh: tty ready` again.

- `phoenix-rtos-kernel/syscalls.c`
  Important because stale false-hypothesis `create_dev` probes were removed
  here once the Pi 4 shell startup race was solved, restoring clean generic and
  Pi 4 shell-smoke output.

- `phoenix-rtos-devices/usb/xhci/phy-aarch64a72-generic.c`
  Important because the current Pi 4 xHCI discovery stub still advertises
  `.irq = 0`, which makes the next realistic bounded bring-up step a polled
  command path rather than interrupt-enable work.

- `phoenix-rtos-usb/usb/usb.c`
  Important because `/sbin/usb` still exits if `hcd_init()` returns `NULL`, so
  staging the USB host service on the live Pi 4 image remains premature until
  the xHCI path can survive initialization and provide at least the first
  roothub contract.

- `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.c`
  Important because the next bounded timer diagnostic needs to expose which common AArch64 timer source and IRQ are selected from the DTB before the missing wakeup interrupt should arrive.

- `phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
  Important because `hal_timerRegister()` and `hal_timerSetWakeup()` are now the narrowest common AArch64 timer arming path after `proc/threads.c` proved that sleep enqueue and wakeup programming are reached on the generic lane.

- `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
  Important because the next bounded diagnostic needs to prove whether the selected timer IRQ is actually registered in GICv2 and whether it is ever dispatched before control would reach `threads_timeintr()`.

- `phoenix-rtos-kernel/hal/aarch64/aarch64.h`
  Important because the next bounded experiment is explicit synchronization after architectural timer sysreg writes for both the physical and virtual timer paths.

- `phoenix-rtos-kernel/include/arch/aarch64/generic/generic.h`
  Important because the current generic AArch64 `platformctl_t` only exposes
  reboot control, and the next HDMI-text step likely starts by adding a
  reusable `pctl_graphmode` query there.

- `phoenix-rtos-kernel/include/arch/ia32/ia32.h`
  Important because it is the current reference definition for `pctl_graphmode`
  and the framebuffer geometry contract used by the existing Phoenix fbcon path.

- Re-verify:
  the `phoenix-dev` VM currently has `aarch64-phoenix` installed but still
  lacks `i386-pc-phoenix`, so IA32 EHCI-based USB-host validation remains
  environment-limited unless that second toolchain is added.

## 4. Raspberry Pi Official Documentation

- Raspberry Pi configuration and boot documentation:
  <https://www.raspberrypi.com/documentation/computers/config_txt.html>
  Important sections: `initramfs`, `ramfsfile`, `ramfsaddr`, `auto_initramfs`

- Raspberry Pi legacy `config.txt` options:
  <https://www.raspberrypi.com/documentation/computers/legacy_config_txt.html>
  Important because the current early bare-metal-style Pi 4 HDMI staging still
  depends on firmware-era HDMI options such as `hdmi_force_hotplug` and
  `disable_overscan`.

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
  Current review focus:
  - `docs/circle-reference-review.md`
  - early Pi 4 mailbox/framebuffer sequencing
  - later Pi 4 PCIe plus xHCI plus HID keyboard sequencing

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

- NetBSD `wsfont` / `wscons` references:
  - <https://github.com/NetBSD/src/tree/trunk/sys/dev/wsfont>
  - <https://man.bsd.lv/NetBSD-8.0/wsfont.4>
  - <https://man.netbsd.org/wsfontload.8>
  Important because they are a plausible later-stage reference for a simple
  framebuffer text console font source. Current conclusion:
  - Phoenix does not need the full `wsfont` infrastructure to benefit
  - Phoenix already has a local font-shaped rendering interface in
    `phoenix-rtos-corelibs/libgraph`
  - the best likely reuse is a small permissively licensed bitmap font asset or
    encoding reference, not a wholesale port of NetBSD wsfont management

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
  Important because they show the exact property-mailbox contract Circle uses:
  coherent low-memory request buffer, bus-address submission, barriers, and
  minimal framebuffer allocation tags.

- `external/circle/lib/screen.cpp`
- `external/circle/include/circle/screen.h`
  Important because they show Circle's layering from framebuffer allocation to
  text-console rendering, which is a useful later reference for Phoenix runtime
  framebuffer console work.

- `external/circle/include/circle/usb/usbhcidevice.h`
- `external/circle/lib/usb/xhcidevice.cpp`
- `external/circle/lib/usb/usbdevicefactory.cpp`
- `external/circle/lib/usb/usbkeyboard.cpp`
- `external/circle/lib/input/keyboardbehaviour.cpp`
- `external/circle/lib/input/keyboardbuffer.cpp`
  Important because they prove that Pi 4 USB keyboard support in Circle sits on
  top of PCIe plus xHCI, then HID keyboard handling, then cooked input
  behavior. This is the main reason USB keyboard remains a later Phoenix
  milestone than current HDMI-visible bring-up.

- `external/circle/lib/bcmpciehostbridge.cpp`
- `external/circle/lib/macb.cpp`
  Important because they are later-stage references for Pi 4 PCIe and network
  subsystems.

- `external/circle/include/circle/bcmpciehostbridge.h`
- `external/circle/include/circle/bcm2711.h`
- `external/circle/include/circle/memorymap64.h`
  Important because they give the key BCM2711 host-bridge constants and memory
  window assumptions behind Circle's PCIe implementation. They are now the
  main external references for the first Phoenix BCM2711 indexed config-space
  backend slice.
  In particular, Circle's `pcie_map_conf()` and `cfg_index()` logic are the
  direct reference for:
  root-complex slot-0 handling on bus 0 and indexed downstream config-space
  access through `PCIE_EXT_CFG_INDEX` and `PCIE_EXT_CFG_DATA`.
  The same file also provides the current reference sequence for:
  bridge reset handling, SerDes IDDQ clear, revision read, and early
  `PCIE_MISC_MISC_CTRL` preparation before outbound-window setup and link-up.
  It is also the reference for:
  `PERST` release, 100 ms settle wait, link-up checks, and RC-mode checks
  before downstream enumeration is treated as meaningful.
  It is also the current reference for:
  outbound window 0 programming and root-bridge class-code shaping on Pi 4
  once sampled link state is acceptable.
  It is also the current reference for:
  bridge-side cache-line, bus-number, memory-window, and command programming in
  `enable_bridge()` before downstream devices are treated as meaningfully
  exposable.

- `phoenix-rtos-corelibs/libgraph/graph.h`
- `phoenix-rtos-corelibs/libgraph/graph.c`
- `phoenix-rtos-tests/gfx/font.h`
  Important because Phoenix already has a `graph_font_t` abstraction and
  `graph_print()` path for bitmap glyph rendering. This means a later Pi 4 HDMI
  text console likely needs a framebuffer console wrapper and a suitable font
  asset more than it needs a brand-new font subsystem.

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
  - current late-userspace proof point:
    bounded gdbstub inspection on the generic fast lane proved that
    `psh_ttyopen("/dev/console")` reaches libphoenix `open()`, `stat()` returns
    `-1`, `resolve_path()` returns `NULL`, and `sys_open()` is never reached
  - current tighter late-userspace proof point:
    a second bounded gdbstub pass proved both the `stat()` and direct `open()`
    `resolve_path()` calls fail at intermediate `/dev` lookup with
    `errno = ENOENT`, before the `console` leaf is reached
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

Important Phoenix filesystem / shell findings to preserve:

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c:48`
  `fetch_modules()` only auto-populates `/syspage` in the root dummyfs
  instance
- `sources/phoenix-rtos-filesystems/dummyfs/srv.c:245`
  `dummyfs;-N;devfs;-D` registers `devfs` in the non-filesystem namespace; it
  does not make `/dev` appear in the root filesystem by itself
- established Phoenix project overlays commonly use:
  `W /bin/bind devfs /dev`
  in `rootfs-overlay/etc/rc.psh`
- the current fast-lane generic and Pi 4 projects do not yet stage an
  equivalent pre-shell `/dev` bind path
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/build.project`
  and
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
  now stage `psh` aliases for `mkdir` and `bind` for pre-shell startup use
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/user.plo.yaml`
  and
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`
  now run `mkdir;/dev` and `bind;devfs;/dev` before the final `psh` app
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
- `plo/hal/aarch64/generic/video.c`
- `plo/hal/aarch64/zynqmp/_init.S`
- `phoenix-rtos-kernel/proc/threads.c`

Current Pi 4 HDMI / mailbox reference paths:

- local QEMU source:
  - `/home/witoldbolt.guest/src/qemu-10.2.2/hw/misc/bcm2835_property.c`
  - `/home/witoldbolt.guest/src/qemu-10.2.2/hw/display/bcm2835_fb.c`
  - `/home/witoldbolt.guest/src/qemu-10.2.2/hw/arm/raspi.c`
- local external reference repos:
  - `external/rpi4-osdev/part5-framebuffer/mb.c`
  - `external/rpi4-osdev/part5-framebuffer/fb.c`
  - `external/circle/lib/bcmmailbox.cpp`
  - `external/circle/lib/bcmframebuffer.cpp`

Important current conclusion:

- on the Pi 4 A72 fast lane, the mailbox/property request buffer must currently
  live in low physical memory for the early `plo` framebuffer path to work
  under `raspi4b` QEMU
- for the first real Pi 4 no-UART HDMI trial, the current firmware staging also
  intentionally enables:
  - `hdmi_force_hotplug=1`
  - `disable_overscan=1`
  based on the current official Raspberry Pi legacy `config.txt` HDMI
  documentation

Current BCM2711 PCIe reference note:

- Circle `external/circle/lib/bcmpciehostbridge.cpp` `enable_bridge()` still
  provided one bounded bridge-local follow-up after the earlier Phoenix
  bridge-exposure step:
  - `PCI_BRIDGE_CONTROL` parity enable
  - `BRCM_PCIE_CAP_REGS + PCI_EXP_RTCTL` CRS software visibility enable
- that capability-state slice is now mirrored in the Phoenix BCM2711 backend
  before any direct downstream endpoint readback is attempted
- the next runtime-relevant integration point is no longer only inside the PCIe
  server:
  the Pi 4 image path now stages `pcie` through
  `phoenix-rtos-devices/_targets/Makefile.aarch64a72-generic` and
  `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`

Current Pi 4 xHCI fast-path reference note:

- Circle's Pi 4 xHCI path uses these bounded downstream assumptions:
  - `XHCI_PCIE_BUS = 1`
  - `XHCI_PCIE_SLOT = 0`
  - `XHCI_PCIE_FUNC = 0`
  - `XHCI_PCI_CLASS_CODE = 0x0c0330`
  - MMIO through the outbound PCIe window
- the first Phoenix Pi 4 xHCI runtime slice now mirrors only the earliest part
  of that model:
  map the fixed MMIO window and validate `CAPLENGTH` / `HCIVERSION` before any
  reset or enumeration logic
- the next Phoenix xHCI runtime slice now also mirrors Circle's first
  controller-readiness step:
  derive the operational-register base from `CAPLENGTH`, wait for `USBSTS.CNR`,
  assert `USBCMD.HCRST`, and wait for reset completion before still failing
  cleanly
- the next Phoenix xHCI readiness slice now also mirrors Circle's immediate
  post-reset checks:
  validate `OP_PAGESIZE` support for 4K pages and extract the basic max-port
  count from `HCSPARAMS1`
- Circle also issues firmware property tag `PROPTAG_NOTIFY_XHCI_RESET`
  `0x00030058` after the PCIe reset path and before enabling the device
- Phoenix now records the fixed BDF/class/MMIO assumptions in the Pi 4 board
  config, and the Pi 4 `pcie` server now also mirrors Circle's firmware
  reset-notify placement in a bounded way before enabling the fixed VL805
  endpoint
- a new useful implementation constraint is now explicit:
  user-space Phoenix code can call `va2pa()` via
  `libphoenix/include/sys/mman.h`, so a firmware-visible mailbox/property
  buffer is feasible in a later Pi 4 user-space helper if that becomes the
  cleanest place for `PROPTAG_NOTIFY_XHCI_RESET`
- `phoenix-rtos-usb/usb/usb.c` also needed one generic AArch64 portability fix
  before the A72 USB host binary could compile cleanly:
  pass the message-thread port value through `uintptr_t`, not `int`
- the A72 build flow now also mirrors the IA32-style USB build structure in a
  bounded way:
  build `libusb`, build the A72 USB device pieces, then build the USB host
  binary against `libusbxhci` and `libusbdrv-usbkbd`
- the first bounded xHCI roothub contract is now also in the Phoenix tree:
  `phoenix-rtos-devices/usb/xhci/xhci.c` now mirrors the EHCI roothub shape
  just enough for Phoenix enumeration:
  - root-hub device/config/string/hub descriptors
  - `usb_isRoothub(pipe->dev)` control-request handling
  - `PORTSC` to `usb_port_status_t` mapping
  - minimal `POWER` / `RESET` / change-bit clear handling
- the most useful external xHCI roothub bitfield reference so far is still
  Circle:
  - `external/circle/include/circle/usb/xhci.h`
  - `external/circle/lib/usb/xhcirootport.cpp`
  especially for `PORTSC`:
  - `CCS`
  - `PED`
  - `PP`
  - `PR`
  - `CSC`
  - `PEC`
  - `OCC`
  - `PRC`
- the next concrete Pi 4 USB blocker is now narrower:
  `xhci_init()` still returns `-ENOSYS` after the internal controller tests,
  so `/sbin/usb` is still not stageable even though roothub requests now exist
- that blocker is now partially cleared:
  `xhci_init()` now survives the current internal controller sequence, so the
  next missing xHCI piece is no longer basic controller lifetime
- the next xHCI blocker is now specifically root-hub status delivery on the
  current Pi 4 path:
  the discovery stub still reports `irq = 0`, so port-change notification will
  need either a temporary polling path or a later real interrupt path before
  live `usb` staging can react to keyboard plug events
- that temporary bridge is now also in the tree:
  `phoenix-rtos-devices/usb/xhci/xhci.c` now starts a small status thread after
  successful init and completes the pending root-hub interrupt transfer when
  `xhci_getHubStatus()` reports change bits
- the next xHCI blocker is now past the roothub:
  child-device enumeration still has no non-roothub transfer path, so the next
  seam is the first real xHCI device-enumeration step after root-hub status
  delivery
- that first post-roothub child-device seam is now also in the tree:
  `phoenix-rtos-devices/usb/xhci/xhci.c` now has:
  - a small reusable internal command-execution helper
  - command-completion slot-ID extraction
  - a bounded `Enable Slot` command path
- the next concrete xHCI blocker is now narrower:
  Phoenix still lacks the first `Address Device` step and the endpoint-0
  control/data path that must follow it

Current preserved clue:

- as of `STEP-0152`, the selected architectural timer is registered and armed, but no timer IRQ is dispatched on the generic fast lane even after explicit post-write instruction barriers
- as of `STEP-0154`, the generic fast lane reads back the selected timer as armed with `ctl 0x1` and a live non-zero `tval`, so the next bounded clue is GIC-side IRQ state rather than timer programming
- as of `STEP-0156`, the generic fast lane reads the selected timer IRQ back as `grp 0 en 0`, and the existing `plo` generic handoff path exits EL3 to EL1 non-secure, so Group 1 configuration is now the next bounded GIC experiment
- as of `STEP-0158`, the kernel-side attempt to move the selected timer IRQ to Group 1 still leaves the generic fast lane at `grp 0 en 0`, which points the next bounded experiment to generic `plo` EL3 GIC initialization
- as of `STEP-0160`, generic `plo` EL3 GIC initialization is enough to restore timer dispatch and tty registration on the generic fast lane, so the next bounded Pi 4 clue is the loader entry EL on `raspi4b`
- as of `STEP-0162`, both fast lanes enter generic `plo` at `EL3`, and the currently reused Pi 4 DTB input decompiles to a 274-byte stub with only `compatible` and one `memory@0` node
