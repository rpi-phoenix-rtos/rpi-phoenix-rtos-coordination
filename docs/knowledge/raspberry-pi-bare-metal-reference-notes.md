# Raspberry Pi Bare-Metal Reference Notes

This document captures the most useful findings from external Raspberry Pi
bare-metal repositories that are not part of the Phoenix RTOS codebase but are
worth consulting during the Pi 4 port.

## Reviewed Repositories

### `sypstraw/rpi4-osdev`

- Repository:
  <https://github.com/sypstraw/rpi4-osdev>
- Local clone:
  `/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev`
- Reviewed commit:
  `c9a2a1429ae7cf0f6d1632c3f4a92fb8457e6c07`
- Nature:
  staged tutorial repo for a simple bare-metal Pi 4 OS

### `rsta2/circle`

- Repository:
  <https://github.com/rsta2/circle>
- Local clone:
  `/Users/witoldbolt/phoenix-rpi/external/circle`
- Reviewed commit:
  `5d819ab24f9a6c53ebab3525558051826b39d757`
- Nature:
  mature bare-metal environment with wide Pi 4 and Pi 5 subsystem support

### `markCwatson/rpi-os`

- Repository:
  <https://github.com/markCwatson/rpi-os>
- Local clone:
  `/Users/witoldbolt/phoenix-rpi/external/rpi-os`
- Reviewed commit:
  `ecc9418d88adb3fc4c6a30e3182b2e029c2bd90f`
- Nature:
  compact hobby Pi 4 kernel with simple EL handoff, mini-UART, scheduler, and
  legacy timer or IRQ examples

### OSDev: `Raspberry Pi Bare Bones`

- Article:
  <https://wiki.osdev.org/Raspberry_Pi_Bare_Bones>
- Nature:
  compact cross-platform OSDev article covering Raspberry Pi AArch32 and AArch64
  bare-bones boot patterns

## Immediate Pi 4 Boot-First Findings

### Boot address and firmware expectations

- `rpi4-osdev` uses the standard AArch64 firmware load address `0x80000` in
  [`part1-bootstrapping/link.ld`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part1-bootstrapping/link.ld)
  and starts with a simple `_start` sequence in
  [`part1-bootstrapping/boot.S`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part1-bootstrapping/boot.S).
- The tutorial switches to `kernel_old=1` only for its special multicore path,
  which moves the kernel expectation to address `0x00000` and is explicitly
  documented in
  [`part10-multicore/README.md`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part10-multicore/README.md).
- Circle keeps Pi 4 AArch64 at `kernel_address=0x80000` in
  [`boot/config64.txt`](/Users/witoldbolt/phoenix-rpi/external/circle/boot/config64.txt)
  and uses a Pi 4-specific arm stub plus a Pi 4-specific kernel filename:
  `armstub8-rpi4.bin` and `kernel8-rpi4.img`.
- The OSDev article also states that Pi 3 and Pi 4 AArch64 firmware normally
  loads `kernel8.img` at `0x80000`, not `0x8000`, and that the boot code is
  entered from `armstub8`.

Implication for Phoenix:

- the current Phoenix Pi 4 `kernel_address=0x40080000` loader staging is a
  Phoenix-specific `plo` design choice, not a general Pi 4 requirement
- the simpler external projects still confirm that `0x80000` remains the normal
  firmware-native AArch64 kernel load convention on Pi 4

### Secondary-core containment and bring-up

- `rpi4-osdev` starts by gating non-boot CPUs using `mpidr_el1` plus `wfe` in
  [`part1-bootstrapping/boot.S`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part1-bootstrapping/boot.S).
- Its multicore experiment later uses spin locations embedded in the image at
  offsets `0xd8` through `0xf0` in
  [`part10-multicore/boot.S`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part10-multicore/boot.S)
  and wakes secondary CPUs with `sev`.
- Circle documents a more structured multicore model in
  [`doc/multicore.txt`](/Users/witoldbolt/phoenix-rpi/external/circle/doc/multicore.txt):
  core 0 handles peripheral IRQs, while additional cores are assigned explicit
  tasks and synchronized with spin locks rather than ad hoc polling.
- The OSDev article states that with recent firmware on Pi 3 and Pi 4, only the
  primary core runs and secondaries wait in a spin loop until a function
  address is written to:
  - `0xE0` for core 1
  - `0xE8` for core 2
  - `0xF0` for core 3
  It also notes that in AArch64 `x0` contains a 32-bit pointer to the DTB on
  the primary core.

Implication for Phoenix:

- `rpi4-osdev` is useful as a minimal sanity check for early CPU containment
- Circle is a better reference for sustained multicore design
- Phoenix should continue its own loader-side secondary-core containment and not
  copy the tutorial’s image-local spin-table layout directly

### EL transition and generic timer setup

- `rpi4-osdev` only becomes interesting for timer setup in its multicore stage.
  There it explicitly:
  - programs the local timer control block at `0xff800000`
  - sets the local prescaler at `0xff800008`
  - writes `cntfrq_el0 = 54000000`
  - clears `cntvoff_el2`
  in
  [`part10-multicore/boot.S`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part10-multicore/boot.S)
- Circle’s
  [`lib/startup64.S`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/startup64.S)
  is the stronger reference for a reusable Pi 4 AArch64 handoff. It enters from
  EL2 and explicitly:
  - enables EL1 timer access by setting the low two bits in `CNTHCTL_EL2`
  - clears `CNTVOFF_EL2`
  - configures `HCR_EL2`
  - returns to EL1 with interrupts masked until later initialization

Implication for Phoenix:

- Circle directly reinforces that Pi 4 AArch64 should use the CPU internal
  physical timer path, not a board-specific legacy timer workaround
- the Phoenix fast lane already proved the timer expires locally; Circle
  supports the conclusion that the remaining problem is on the PPI or GIC side,
  not in basic timer source selection

### Interrupt controller and timer IRQ identity

- `rpi4-osdev` interrupt work in
  [`part13-interrupts/kernel/kernel.h`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part13-interrupts/kernel/kernel.h)
  and
  [`part13-interrupts/kernel/irq.c`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part13-interrupts/kernel/irq.c)
  uses the BCM2711 legacy system timer plus legacy IRQ controller registers,
  not the ARM architectural timer plus GIC path.
- Circle’s
  [`include/circle/bcm2711int.h`](/Users/witoldbolt/phoenix-rpi/external/circle/include/circle/bcm2711int.h)
  defines:
  - `GIC_PPI(n) = 16 + n`
  - `ARM_IRQLOCAL0_CNTPNS = GIC_PPI(14)`
  which resolves to IRQ `30`
- Circle’s
  [`lib/timer.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/timer.cpp)
  requires `USE_PHYSICAL_COUNTER` on Pi 4 and programs `CNTP_CVAL_EL0` plus
  `CNTP_CTL_EL0`
- Circle’s
  [`lib/interruptgic.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/interruptgic.cpp)
  initializes the GIC distributor and CPU interface in non-secure mode for
  ordinary IRQ delivery

Implication for Phoenix:

- Circle confirms that Phoenix choosing the non-secure physical timer on Pi 4
  and mapping it to IRQ `30` is correct
- `rpi4-osdev` part 13 should not be used as a model for the current Phoenix
  timer or GIC work
- the next Phoenix diagnostic step should stay on the timer-to-GIC PPI seam
  rather than revisiting timer source identity

### Peripheral base and MMIO assumptions

- `rpi4-osdev` consistently uses `PERIPHERAL_BASE = 0xFE000000` in low
  peripheral mode, documented in
  [`part4-miniuart/README.md`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part4-miniuart/README.md)
  and implemented in
  [`part4-miniuart/io.c`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part4-miniuart/io.c).
- `rpi-os` repeats the same assumption in
  [`include/arm/base.h`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/include/arm/base.h).
- The OSDev article’s Pi 3 and 4 C example also selects `MMIO_BASE = 0xFE000000`
  for Pi 4 and explicitly calls out the board-dependent MMIO base.
- The tutorial explicitly warns that this differs from some published BCM2711
  documentation because the Pi 4 boots in low peripheral mode by default.

Implication for Phoenix:

- this matches why Phoenix had to stop relying on generic-QEMU MMIO defaults for
  Pi 4
- low-peripheral-mode assumptions remain valid for firmware-booted Pi 4 code
  until or unless Phoenix deliberately changes that mode later

## Later-Stage Reference Value

### UART and early console

- `rpi4-osdev` demonstrates mini-UART on GPIO14 and GPIO15 with ALT5 and
  `core_freq_min=500` in the tutorial narrative around
  [`part4-miniuart/README.md`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part4-miniuart/README.md)
  and
  [`part4-miniuart/io.c`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part4-miniuart/io.c).
- Circle contains fuller UART infrastructure under
  [`lib/serial.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/serial.cpp)
  and related headers, making it the better long-term reference.
- The OSDev article is worth remembering for one PL011-specific detail: on Pi 3
  and Pi 4 it demonstrates using the mailbox property interface to set the
  UART clock so that the baud rate becomes deterministic before programming the
  PL011 divisors.

### Mailbox and framebuffer

- `rpi4-osdev` gives a compact tutorial-quality example of the VideoCore
  mailbox property interface and framebuffer allocation in
  [`part5-framebuffer/README.md`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part5-framebuffer/README.md).
- Circle provides production-oriented implementations in
  [`lib/bcmmailbox.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/bcmmailbox.cpp),
  [`lib/bcmpropertytags.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/bcmpropertytags.cpp),
  and
  [`lib/bcmframebuffer.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/bcmframebuffer.cpp).

### Networking, PCIe, and USB

- `rpi4-osdev` networking uses an external ENC28J60 over SPI in
  [`part14-spi-ethernet/README.md`](/Users/witoldbolt/phoenix-rpi/external/rpi4-osdev/part14-spi-ethernet/README.md),
  which is not a reference for Pi 4’s on-board GENET Ethernet.
- Circle is much more relevant for later Phoenix work because it contains:
  - Broadcom PCIe host bridge code in
    [`lib/bcmpciehostbridge.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/bcmpciehostbridge.cpp)
  - network-device infrastructure in
    [`lib/netdevice.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/netdevice.cpp)
  - MACB-based Ethernet support in
    [`lib/macb.cpp`](/Users/witoldbolt/phoenix-rpi/external/circle/lib/macb.cpp)
  - broad USB host stacks and QEMU notes in
    [`doc/qemu.txt`](/Users/witoldbolt/phoenix-rpi/external/circle/doc/qemu.txt)

## Recommended Usage

Use `rpi4-osdev` for:

- staged early-boot sanity checks
- minimal AArch64 single-core start patterns
- low-peripheral-mode MMIO examples
- mailbox or framebuffer concept refreshers

Use Circle for:

- Pi 4 AArch64 EL2 to EL1 startup details
- architectural timer and IRQ identity on BCM2711
- GIC-400 handling
- multi-core policy
- later GPIO, mailbox, framebuffer, PCIe, xHCI, and network references

Do not use `rpi4-osdev` part 13 as a model for the current Phoenix timer bug:

- it uses the BCM2711 legacy system timer and legacy IRQ controller
- Phoenix is already on the architectural timer plus GIC path

Use `rpi-os` for:

- a very small AArch64 Pi 4 boot skeleton in
  [`src/boot.S`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/src/boot.S)
- a simple EL3 to EL1 transition using
  [`include/arm/sysregs.h`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/include/arm/sysregs.h)
- compact vector-table examples in
  [`src/irq.S`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/src/irq.S)
- low-peripheral-mode and mini-UART examples in
  [`include/arm/base.h`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/include/arm/base.h)
  and
  [`src/mini_uart.c`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/src/mini_uart.c)

Do not use `rpi-os` as a reference for the current Phoenix timer issue:

- it boots from `0x80000`, gates secondary CPUs via `mpidr_el1` plus `wfe`, and
  uses low peripheral mode, which is useful
- but its timer and IRQ work in
  [`src/timer.c`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/src/timer.c)
  and
  [`src/irq.c`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/src/irq.c)
  is again based on the BCM2711 legacy system timer plus legacy IRQ controller
- unlike Circle’s Pi 4 AArch64 startup, it does not explicitly configure
  `CNTHCTL_EL2` or use the architectural physical timer path that Phoenix is
  currently debugging

## Additional Notes From `rpi-os`

- `rpi-os` uses the standard AArch64 firmware load address `0x80000` in
  [`src/linker.ld`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/src/linker.ld)
  and with `.org 0x80000` in
  [`src/boot.S`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/src/boot.S).
- Its boot path assumes transfer from an external `armstub8` and performs an
  `EL3 -> EL1h` return with `SCR_EL3`, `HCR_EL2`, and `SPSR_EL3` setup, but it
  is intentionally minimal and does not establish the fuller timer-access state
  visible in Circle’s `startup64.S`.
- Its mini-UART path again uses:
  - GPIO14 and GPIO15
  - ALT5
  - `AUX_UART_CLOCK = 500000000`
  - low peripheral mode at `0xFE000000`
  in
  [`src/mini_uart.c`](/Users/witoldbolt/phoenix-rpi/external/rpi-os/src/mini_uart.c)
- This repo is therefore useful as a short-form Pi 4 “sanity mirror”, but not
  as the primary reference for Phoenix architectural timer, GIC, DTB, or
  peripheral-driver work.

Use the OSDev article for:

- quick AArch64 boot-entry reminders
- DTB handoff reminders (`x0` on the primary core)
- current-firmware secondary-core release-slot reminders
- MMIO base and PL011 initialization cross-checks

Do not use the OSDev article as the primary reference for Phoenix Pi 4
architectural-timer debugging:

- it is intentionally minimal
- it is not a Pi 4-specific deep dive into GIC-400 behavior
- Circle remains the better external source for the current timer and PPI seam

## Re-verify

- Re-verify both repository heads and any cited commit hashes before depending
  on them in a later long-running implementation session.
