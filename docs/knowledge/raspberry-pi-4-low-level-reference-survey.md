# Raspberry Pi 4 Low-Level Reference Survey

This document consolidates the highest-value low-level Raspberry Pi 4 facts
from the external references that matter most for Phoenix RTOS bring-up.

Purpose:

- reduce repeated re-discovery of basic Pi 4 constants and boot assumptions
- distinguish stable facts from tutorial-specific shortcuts or stale advice
- record which sources are trustworthy enough to drive the next real-hardware
  boot-debug steps

This document complements, rather than replaces:

- `docs/knowledge/raspberry-pi-4.md`
- `docs/knowledge/raspberry-pi-bare-metal-reference-notes.md`
- `docs/knowledge/circle-reference-review.md`

## 1. Source Quality Ranking

Use the following priority order when facts disagree:

1. official Raspberry Pi documentation and the BCM2711 peripherals PDF
2. Raspberry Pi Linux DTS sources
3. U-Boot BCM2711 DTS and historically relevant downstream Linux boot configs
4. Circle's Pi 4 armstub and platform support
5. NuttX Pi 4 porting notes
6. focused bare-metal references with explicit Pi 4 scope
7. blogs, forums, and Stack Overflow

Practical ranking for the sources reviewed in this step:

- Highest signal:
  - BCM2711 peripherals PDF
  - Raspberry Pi Linux DTS files
  - Raspberry Pi firmware boot tree
  - U-Boot `bcm2711.dtsi`
  - Circle
  - NuttX BCM2711 porting case study
- Good but narrower:
  - EDK2 Raspberry Pi 4 platform
  - `rust-raspberrypi-OS-tutorials`
  - `markCwatson/rpi-os`
  - `sypstraw/rpi4-osdev`
- Useful with caution:
  - `rhythm16/rpi4-bare-metal`
    This repo explicitly says it contains errors and should not be treated as a
    primary truth source.
- Supplementary / lower-signal for early boot:
  - Ultibo Pi 4 platform notes
  - CircuitPython broadcom port
  - generic GPIO blog posts
  - OSDev and Raspberry Pi forum threads
  - Stack Overflow answers
  - BOOTBOOT
  - Ultibo Core

## 2. Stable Board Facts

- Board target:
  Raspberry Pi 4 Model B
- SoC:
  BCM2711
- CPU:
  quad-core Cortex-A72, ARMv8-A 64-bit
- Interrupt controller:
  GIC-400
- Serial blocks:
  PL011 plus AUX mini-UART
- On-board Ethernet:
  Broadcom GENET
- USB host path:
  VL805 xHCI behind BCM2711 PCIe

These facts are consistent across official Pi documentation, Linux DTS, Circle,
and the external bare-metal repos.

## 3. Address Map and Alias Facts

### 3.1 Official address-translation model

The BCM2711 peripherals PDF is the anchor source here.

Key facts from the official address-map chapters:

- main peripherals exist in the 35-bit `0x4_7c00_0000` to `0x4_7fff_ffff`
  range
- ARM-local peripherals exist in the 35-bit `0x4_c000_0000` to
  `0x4_ffff_ffff` range
- in low-peripheral mode, those become ARM-visible at:
  - main peripherals: `0xFC00_0000` to `0xFF7F_FFFF`
  - ARM-local peripherals: `0xFF80_0000` to `0xFFFF_FFFF`
- legacy peripheral addresses in the `0x7Enn_nnnn` range are still commonly
  used in documentation and DT, but must be translated to ARM-visible aliases
  before direct CPU access

Implication for Phoenix:

- the build-time or DTS-visible address is not automatically the CPU-visible
  address
- low-level code that bypasses DT translation must use the ARM-visible aliases
- DMA still cares about legacy addresses

### 3.2 Linux DT `ranges` are the practical translation guide

The Raspberry Pi Linux `bcm2711.dtsi` gives the cleanest machine-readable
translation model:

- `0x7e000000 -> 0xfe000000` for common BCM283x peripherals
- `0x7c000000 -> 0xfc000000` for BCM2711-specific peripherals
- `0x40000000 -> 0xff800000` for ARM-local peripherals

U-Boot `arch/arm/dts/bcm2711.dtsi` independently matches the same three
translation ranges, which makes the alias model significantly harder to dismiss
as a Linux-only quirk.

Implication for Phoenix:

- DTB parser logic must honor `/soc` `ranges`
- early board constants used outside the DT parser should match the translated
  ARM-visible aliases, not the raw DTS bus addresses

## 4. Cross-Source Pi 4 Low-Level Constants

The following constants are the most important for early bring-up and are
supported by multiple high-value references.

| Function | DTS / bus address | ARM-visible address | Primary sources | Notes |
| --- | --- | --- | --- | --- |
| Peripheral base | `0x7e000000` | `0xfe000000` | BCM2711 PDF, Linux DTS, `rpi4-osdev`, `rpi-os`, Rust tutorials | Low-peripheral mode assumption used by most Pi 4 bare-metal code |
| GPIO | `0x7e200000` | `0xfe200000` | BCM2711 PDF, Rust tutorials, `rpi4-osdev`, `rpi4-bare-metal` | Used for PL011 pinmux and GPIO tests |
| PL011 UART0 | `0x7e201000` | `0xfe201000` | Linux DTS plus low-peripheral translation, Rust tutorials, Phoenix board config | Main early runtime UART target for Phoenix |
| ARM local control | `0x40000000` | `0xff800000` | Linux DTS, Circle, `rpi4-bare-metal`, `rpi4-osdev` | Timer / local interrupt setup |
| ARM local prescaler | `0x40000008` | `0xff800008` | Linux DTS, Circle, `rpi4-bare-metal`, `rpi4-osdev` | Usually written with `0x80000000` |
| GIC base | `0x40040000` | `0xff840000` | Linux DTS translation, `rpi4-bare-metal`, Circle | Useful summary value |
| GIC distributor | `0x40041000` | `0xff841000` | Linux DTS, Circle, `rpi4-bare-metal`, Rust tutorials | This is the corrected Phoenix Pi 4 value |
| GIC CPU interface | `0x40042000` | `0xff842000` | Linux DTS, Circle, `rpi4-bare-metal`, Rust tutorials | This is the corrected Phoenix Pi 4 value |
| PCIe host bridge | `0x7d500000` | `0xfd500000` | Linux DTS plus `ranges`, current Phoenix board config | Key for VL805/xHCI path |

EDK2's Pi 4 DSC independently matches several of these constants:

- `PcdGicDistributorBase = 0xFF841000`
- `PcdGicInterruptInterfaceBase = 0xFF842000`
- `PcdBcm27xxPciRegBase = 0xfd500000`
- `PcdBcm27xxPciBusMmioAdr = 0xf8000000`
- `PcdBcmGenetRegistersAddress = 0xfd580000`
- `PcdBcm283xRegistersAddress = 0xfe000000`

The consensus is strong on these addresses. The earlier Phoenix use of
`0x40041000` / `0x40042000` in real-hardware code was therefore a genuine
board-config bug, not an alternative valid interpretation.

## 5. Boot Chain and Firmware Handoff Facts

### 5.1 Boot partition contents

The Raspberry Pi firmware boot tree remains the canonical source for the Pi 4
firmware-visible files.

Pi 4-relevant files currently present in `raspberrypi/firmware/boot` include:

- `start4.elf`
- `fixup4.dat`
- `start4db.elf`
- `fixup4db.dat`
- `kernel8.img`
- `bcm2711-rpi-4-b.dtb`

Implication for Phoenix:

- staged Pi 4 images should continue to treat the firmware boot tree as the
  authoritative source of firmware binaries and board DTBs

EDK2's Pi 4 README is also useful as a firmware-facing boot reference because
it stages:

- `enable_gic=1`
- `armstub=RPI_EFI.fd`
- `disable_commandline_tags=2`
- `device_tree_address=0x3e0000`
- `device_tree_end=0x400000`

Implication for Phoenix:

- it is another concrete example that a custom firmware-stage payload on Pi 4
  often pairs an explicit armstub with an explicit DTB staging window
- the low-memory region around `0x3e0000..0x400000` should be treated with care
  in any future radical low-memory diagnostic image or armstub experiment

### 5.2 Historical Pi 4 Linux boot helpers are useful, but not final truth

The historical `sakaki-/bcm2711-kernel` project is useful because it captures
an early Pi 4 64-bit Linux boot shape that explicitly required:

- `enable_gic=1`
- `armstub=armstub8-gic.bin`

It also temporarily clamped memory with `total_mem=1024`.

Implication for Phoenix:

- this is useful evidence that early Pi 4 64-bit kernels sometimes needed an
  explicit GIC-aware armstub pairing
- it should be treated as historical and time-sensitive, not as a current
  canonical firmware contract
- `total_mem=1024` should not be copied into Phoenix unless there is a tightly
  scoped experiment that clearly justifies it
- likewise, NuttX's successful `0x480000` Pi 4 load address is evidence for a
  U-Boot-based kernel placement, not a contradiction of the firmware-native
  `0x80000` bare-metal convention

### 5.3 Normal AArch64 load convention

Across Circle, `rpi-os`, `rpi4-osdev`, and the Rust tutorials, the normal Pi 4
bare-metal AArch64 kernel image convention is:

- firmware loads `kernel8.img`
- image is linked for `0x80000`
- an `armstub` establishes the initial CPU environment

Important examples:

- Circle `boot/config64.txt`
- `rpi4-osdev` `link.ld`
- `rpi-os` `src/linker.ld`
- Rust tutorial linker scripts

Implication for Phoenix:

- Phoenix's current `kernel_address=0x40080000` is a Phoenix `plo` staging
  choice, not a general Raspberry Pi 4 requirement
- if the current Phoenix loader model keeps failing on hardware, reverting to a
  more firmware-native boot shape remains a valid radical experiment

### 5.4 `kernel_old=1` is not a safe default

This is one of the most important stale-tutorial traps.

- `rpi4-osdev` part 10 uses `kernel_old=1` for its special multicore path and
  explains that it changes the load expectation to `0x00000000`
- `rhythm16/rpi4-bare-metal` explicitly warns that `kernel_old=1` broke on
  newer firmware and references discussion of the same issue
- the official Raspberry Pi documentation still describes `kernel_old` as a
  legacy compatibility option that changes the kernel load address to `0x0`

Implication for Phoenix:

- do not cargo-cult `kernel_old=1` into the Pi 4 port
- only use it for a tightly scoped experiment if the exact objective requires
  it and the firmware behavior is re-verified

### 5.5 `arm_peri_high` is real, but easy to misuse

Official Raspberry Pi documentation states:

- `arm_peri_high=1` enables high-peripheral mode on Pi 4
- it is set automatically when a suitable DTB is loaded
- enabling it without a compatible DTB and suitable armstub can break boot

`rhythm16/rpi4-bare-metal` intentionally uses:

- `arm_peri_high=0`
- `armstub=armstub8.bin`
- `enable_gic=1`

Implication for Phoenix:

- early Pi 4 bring-up should not casually toggle peripheral-high mode
- the current Phoenix low-peripheral assumptions remain defensible until there
  is a reason to change them deliberately

Re-verify:

- current firmware defaults around automatic peripheral-high selection
- whether the firmware DTB and custom armstub interact differently after future
  EEPROM updates

### 5.6 Interrupt-controller selection depends on more than one knob

Official Raspberry Pi documentation says:

- `enable_gic=1` is the Pi 4 default
- `enable_gic=0` routes interrupts through the legacy controller instead

Ultibo's Pi 4 platform notes add an important nuance:

- when no suitable DTB is present, current firmware may still select the legacy
  controller
- when a DTB containing a compatible GIC node is present, firmware may enable
  the GIC regardless of `enable_gic=0`
- if software configures the GIC while the legacy controller is actually
  active, the board can hang on the four-color screen with no useful runtime
  output

Implication for Phoenix:

- `enable_gic=1` alone should not be treated as sufficient proof that the GIC
  path is really active
- the staged DTB, firmware-selected DTB, and armstub behavior may all
  participate in the controller-selection result
- if the current black-screen real-board failure persists, a bounded
  controller-selection proof or self-test is justified before widening other
  early-boot hypotheses

## 6. CPU Entry, EL Setup, and SMP Facts

### 6.1 Secondary-core release conventions

There is strong agreement on the Pi 4 secondary-core release locations:

- `0xd8`
- `0xe0`
- `0xe8`
- `0xf0`

Sources:

- Linux `bcm2711.dtsi` `cpu-release-addr`
- U-Boot `bcm2711.dtsi`
- Circle Pi 4 armstub layout
- `rpi4-bare-metal` armstub layout
- OSDev bare-bones article

For AArch64 Linux DT, the CPU nodes use:

- `enable-method = "spin-table"`

Implication for Phoenix:

- the current custom Pi 4 armstub and future SMP bring-up should continue to
  align with the firmware spin-table contract

### 6.2 Timer and local-control setup consensus

The strongest cross-source consensus in the whole survey is here:

- `LOCAL_CONTROL = 0xff800000`
- `LOCAL_PRESCALER = 0xff800008`
- write `0` to local control for the crystal-clock path
- write `0x80000000` to the prescaler
- set `CNTFRQ_EL0 = 54000000`
- clear `CNTVOFF_EL2`

This exact pattern appears in:

- Circle `boot/armstub/armstub8.S`
- `rhythm16/rpi4-bare-metal` `armstub8.S`
- `rpi4-osdev` multicore startup

Implication for Phoenix:

- the current Phoenix Pi 4 armstub is aligned with the consensus on these
  basic timer constants
- future timer debugging should not re-open the `54 MHz` question unless new
  contradictory evidence appears

### 6.3 The current Phoenix armstub is still smaller than the known-working ones

The new survey matters because it sharpens this specific gap.

The current Phoenix Pi 4 armstub now does:

- local timer control and prescaler setup
- `CNTFRQ_EL0`
- `CNTVOFF_EL2`
- `CNTHCTL_EL2`
- `SCR_EL3`
- `CPUECTLR_EL1.SMPEN`
- bounded GIC group-1 setup
- `SCTLR_EL2`
- `SPSR_EL3`

However, the stronger Circle / `rpi4-bare-metal` stubs also include a broader
set of early register preparation, including:

- `ACTLR_EL3`
- `CPTR_EL3`
- `CPACR_EL1`
- `HCR_EL2`
- `SCTLR_EL1`
- `TCR_EL1`
- `MAIR_EL1`
- in the `rpi4-bare-metal` case, `L2CTLR_EL1`

Implication for Phoenix:

- if the current real board still remains black and silent, the next justified
  radical experiment is a fuller Circle-style `setup_more_regs` path, not more
  random address changes

## 7. Interrupt and Timer Identity Facts

### 7.1 Architectural timer

The Linux DT declares:

- `timer { compatible = "arm,armv8-timer"; ... }`

Circle provides the clearest concrete IRQ identity:

- `GIC_PPI(14)` is the Pi 4 non-secure physical timer
- that resolves to IRQ `30`

U-Boot `bcm2711.dtsi` independently confirms the standard ARMv8 timer PPI
ordering:

- `13` secure physical
- `14` non-secure physical
- `11` virtual
- `10` hypervisor

Implication for Phoenix:

- the current Pi 4 preference for the non-secure physical timer remains the
  right path
- hobby examples that use the legacy system timer are not the correct model for
  the current Phoenix timer path

### 7.2 High-value SPI IRQ identities

Circle's `bcm2711int.h` is the most immediately useful compact IRQ table for
future driver bring-up:

- UART: `GIC_SPI(121)`
- GPIO0-3: `GIC_SPI(113..116)`
- I2C: `GIC_SPI(117)`
- SPI: `GIC_SPI(118)`
- Arasan SDIO: `GIC_SPI(126)`
- PCIe host INTA: `GIC_SPI(143)`
- PCIe host MSI: `GIC_SPI(148)`
- xHCI internal: `GIC_SPI(176)`

Implication for Phoenix:

- this header is a good quick cross-check source when the DT parser, board
  constants, and future driver IRQ wiring need a second opinion

## 8. GPIO, UART, and Low-Level I/O Facts

### 8.1 GPIO pull control on BCM2711

The BCM2711 peripherals PDF and the Rust tutorials agree that BCM2711 uses:

- `GPIO_PUP_PDN_CNTRL_REG*`

for internal pull-up/pull-down control.

This is also reflected in:

- `rpi4-osdev` `GPPUPPDN0`
- `rpi4-bare-metal` GPIO definitions

Important caution:

- some older tutorials still use the BCM283x `GPPUD` / `GPPUDCLK` sequence
- `rhythm16/rpi4-bare-metal` explicitly calls this out as a Pi 4 mismatch,
  even though it reports both methods may appear to work in some testing

Implication for Phoenix:

- BCM2711 GPIO and UART pinmux work should prefer the newer pull-control
  registers, not the legacy ones

### 8.2 The Pi 4 activity LED is on GPIO 42

Ultibo's Pi 4 platform notes explicitly state:

- the Pi 4 activity LED is connected to GPIO pin `42`

This is valuable because it gives Phoenix a hardware-visible earliest-entry
proof technique that does not depend on:

- UART
- mailbox/framebuffer success
- GIC delivery

Implication for Phoenix:

- a bounded GPIO42 toggle experiment is now one of the highest-value next
  earliest-entry diagnostics for the real board
- this should be preferred over more speculative framebuffer-only debugging if
  the next armstub experiment still stays black

### 8.3 PL011 versus mini-UART

The sources show two common early-serial patterns:

- PL011 for stable primary UART
- mini-UART for simple tutorial output

Important implications:

- Linux `bcm2711-rpi-4-b.dts` uses `stdout-path = "serial1:115200n8"` because
  the standard Raspberry Pi OS environment often routes the console through the
  auxiliary UART
- many bare-metal tutorials use PL011 explicitly instead because it is a more
  stable early-debug choice
- Rust tutorial Pi 4 bring-up uses `init_uart_clock=48000000` in `config.txt`
  and a Pi 4-specific GPIO/UART driver split
- EDK2's Pi 4 DSC also uses:
  - PL011 clock input `48000000`
  - mini-UART clock `500000000`
  and its README warns that mini-UART serial from the OS may be unreliable
  under CPU throttling

Implication for Phoenix:

- the current Phoenix decision to stay on PL011 for early bring-up remains
  correct
- Linux's `stdout-path` on stock DTBs should not be mistaken for the best
  bare-metal debug choice

## 9. DTB and Firmware-Patching Facts

### 9.1 Build-time DTB is not the same thing as the live firmware-patched DTB

This is one of the most important bring-up facts for Phoenix.

In Linux `bcm2711-rpi.dtsi`:

- `memory@0` is explicitly marked as a bootloader-filled placeholder
- related bootloader configuration and public-key nodes are also described as
  firmware-populated

Implication for Phoenix:

- the static `system.dtb` staged into the image is useful for QEMU and for
  loader-side parsing, but it is not equivalent to the final DTB that Raspberry
  Pi firmware passes at runtime
- any real-hardware bug involving RAM size, placement, or firmware-private
  nodes must account for this difference

### 9.2 DT bus addresses must be translated

Linux `bcm2711.dtsi` is the cleanest proof that the raw DT `reg` values for:

- GIC
- PCIe
- serial
- GPIO
- other SoC peripherals

are not directly CPU-visible addresses until `/soc` `ranges` are applied.

Implication for Phoenix:

- any future DT parser regression that ignores parent `ranges` is likely to
  produce a real-hardware failure even if it appears to work in a simplified
  emulation path

### 9.3 U-Boot confirms the practical Pi 4 platform shape

The current U-Boot `bcm2711.dtsi` independently confirms several Pi 4 facts
already relied on by Phoenix:

- top-level `interrupt-parent = <&gicv2>`
- `/soc` `ranges` matching Linux downstream translation
- `timer { compatible = "arm,armv8-timer"; ... }`
- AArch64 CPUs described as `arm,cortex-a72`
- AArch64 CPUs using `spin-table` release addresses
- PCIe host at `pcie@7d500000`
- GENET at `ethernet@7d580000`

Implication for Phoenix:

- these are now cross-checked against both Linux downstream DTS and U-Boot
  rather than depending on a single tree
- the remaining uncertainty is not the basic board layout, but the exact
  earliest runtime handoff on the real board

## 10. PCIe, USB, and Later Bring-Up Facts

Even though the current blocker is earlier than PCIe or xHCI, the survey also
confirms the future path.

### 10.1 PCIe host

Linux `bcm2711.dtsi` confirms:

- the PCIe host node is `pcie@7d500000`
- the Pi 4 outbound memory window is based on `0xf8000000`
- host interrupts map through GIC SPI lines beginning at `143`

This matches the current Phoenix Pi 4 PCIe assumptions closely.

U-Boot `bcm2711.dtsi` independently confirms the same Pi 4 PCIe node placement
and shows the same outbound-window style rooted at `0xf8000000`, which adds a
second primary-source-style check for the current Phoenix PCIe constants.

EDK2's Pi 4 DSC adds one more useful constant here:

- `PcdBcm27xxPciCpuMmioAdr = 0x600000000`

That is a UEFI-specific mapping choice rather than a bare-metal hardware
constant, but it confirms that EDK2 also treats the outbound window as a
translated CPU-visible PCIe aperture rather than direct device space.

### 10.2 VL805 and USB

Circle remains the strongest non-Linux reference for the actual Pi 4 USB host
sequence:

- initialize the PCIe host bridge
- notify firmware about xHCI reset
- enable the VL805 xHCI path

Implication for Phoenix:

- the existing Phoenix USB keyboard path is correctly layered:
  generic `usbkbd` logic first, BCM2711 PCIe plus xHCI transport next
- EDK2 adds one practical caution for future Phoenix xHCI work:
  its Pi 4 README states that xHCI may only work reliably in pre-OS mode unless
  RAM is limited to 3 GB, because of nonstandard DMA constraints
- that should be treated as a warning that future real-hardware Phoenix xHCI
  failures on larger-memory boards may involve DMA aperture assumptions rather
  than only driver logic

### 10.3 Supplementary later-stage sources

Some of the listed sources are more relevant for later driver work than for the
current earliest boot problem:

- CircuitPython broadcom port
  useful later for GPIO, storage, and general board-support ideas, but it is
  explicitly alpha and is not the cleanest earliest-boot reference
- Ultibo Core
  likely useful later as a broad board-support corpus, but not currently a
  higher-signal early-boot reference than Circle or Linux DTS
- BOOTBOOT
  useful as a generic loader comparison, but less directly relevant to
  Phoenix's native `firmware -> plo -> kernel` target design

## 11. Most Useful Diagnostic Lessons for the Current Boot Failure

The survey points to a shorter list of high-value next experiments.

### 11.1 What is probably not the main problem anymore

- FAT partition layout
- missing Pi 4 firmware files
- raw GIC base aliases in the board config
- the basic `LOCAL_CONTROL` / `LOCAL_PRESCALER` / `CNTFRQ_EL0` constants

Those areas now have strong cross-source support and have already been corrected
or validated.

### 11.2 What remains plausible

1. Phoenix still enters with the wrong earliest CPU-state assumptions for the
   real Pi 4 firmware + armstub handoff.
2. The current custom armstub still does too little compared with the known
   working Pi 4 armstubs from Circle and `rpi4-bare-metal`.
3. The earliest visible diagnostic point is still too late in the path, so the
   board dies before Phoenix can repaint HDMI or expose a clearer signal.
4. A future low-memory diagnostic path could accidentally collide with the
   firmware / DTB staging region if it ignores the same kind of reserved window
   that EDK2 keeps at `0x3e0000..0x400000`.

### 11.3 Best next experiments

The survey makes these the most justified next moves:

1. Add an even earlier Pi 4-specific visible diagnostic path before the current
   `plo` HDMI initialization.
2. Prefer GPIO42 activity-LED proof as the very first hardware-visible signal
   if the next image is still silent.
3. Try a fuller Circle-style / `setup_more_regs` armstub experiment rather than
   more ad hoc address changes.
4. Consider an Ultibo-style bounded controller-selection self-test if the GIC
   versus legacy path remains ambiguous on the real board.
5. Keep NuttX's GPIO-first diagnostic lesson in mind:
   toggle one GPIO or LED line immediately to prove that the first C or
   assembly path is reached, independent of UART availability.

## 12. Key References Used In This Survey

Official and primary:

- BCM2711 peripherals PDF:
  <https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf>
- Raspberry Pi firmware boot tree:
  <https://github.com/raspberrypi/firmware/tree/master/boot>
- Raspberry Pi Linux DTS:
  <https://github.com/raspberrypi/linux/tree/rpi-6.19.y/arch/arm/boot/dts/broadcom>
- Raspberry Pi legacy config options:
  <https://www.raspberrypi.com/documentation/computers/legacy_config_txt.html>

High-value implementation references:

- U-Boot `bcm2711.dtsi`:
  <https://github.com/u-boot/u-boot/blob/master/arch/arm/dts/bcm2711.dtsi>
- EDK2 Raspberry Pi 4 platform:
  <https://github.com/tianocore/edk2-platforms/tree/master/Platform/RaspberryPi/RPi4>
- Circle:
  <https://github.com/rsta2/circle>
- NuttX BCM2711 porting case study:
  <https://nuttx.apache.org/docs/latest/guides/porting-case-studies/bcm2711-rpi4b.html>
- `sakaki-/bcm2711-kernel`:
  <https://github.com/sakaki-/bcm2711-kernel>
- Rust Raspberry Pi OS tutorials:
  <https://github.com/rust-embedded/rust-raspberrypi-OS-tutorials>
- `rhythm16/rpi4-bare-metal`:
  <https://github.com/rhythm16/rpi4-bare-metal>
- `sypstraw/rpi4-osdev`:
  <https://github.com/sypstraw/rpi4-osdev>
- `markCwatson/rpi-os`:
  <https://github.com/markCwatson/rpi-os>

Supplementary / explanatory:

- `rpi4os.com`:
  <https://www.rpi4os.com>
- Code Embedded GPIO blog:
  <https://www.codeembedded.com/blog/raspberry_pi_gpio/>
- OSDev Raspberry Pi forum thread:
  <https://forum.osdev.org/viewtopic.php?t=56115>
- Stack Overflow peripheral-base discussion:
  <https://stackoverflow.com/questions/77205909/raspberry-pi-4-bcm2711-peripheral-base-address-differs-in-documentation-from-har>
- Raspberry Pi forum thread:
  <https://forums.raspberrypi.com/viewtopic.php?t=377875>

Lower-value for the current earliest-boot problem, but still worth remembering:

- Ultibo Pi 4 platform notes:
  <https://ultibo.org/wiki/Unit_PlatformRPi4>
- Ultibo Pi 4 boot notes:
  <https://ultibo.org/wiki/Unit_BootRPi4>
- Ultibo Core:
  <https://github.com/ultibohub/Core/tree/master>
- BOOTBOOT:
  <https://gitlab.com/bztsrc/bootboot>
- CircuitPython broadcom port:
  <https://github.com/adafruit/circuitpython/tree/main/ports/broadcom>

Re-verify:

- exact Raspberry Pi firmware behavior around `arm_peri_high`, automatic DTB
  handling, and current armstub interaction
- whether later Circle or Linux branches change any Pi 4 early-boot constants
  that matter to Phoenix
