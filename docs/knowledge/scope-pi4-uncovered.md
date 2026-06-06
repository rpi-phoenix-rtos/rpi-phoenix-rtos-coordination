# Phoenix-RTOS on Raspberry Pi 4 — Uncovered Scope

> Gap analysis of everything the Pi 4 port still needs that is **not** covered by the
> 14 research briefs and 8 implementation plans currently landed or in flight.
> The orchestrator already owns: EL2→EL1 drop, MMU+cache+SMP, VL805 xHCI, GPU/VC6,
> GENET, BCM43455 WiFi, BCM43455 Bluetooth, GPIO/pinctrl, RTC+thermal+watchdog+
> power-off (incl. the firmware-mailbox driver). This document scopes the rest.

Date: 2026-05-06. Worktree branch: `claude/dazzling-joliot-cd9889`.
Sources cited inline. `Re-verify:` notes where firmware/QEMU/upstream
behavior may have shifted.

---

## 1. Executive summary

### 1.1 Scope diagram

```
                       ┌──────────────────────────────────────────┐
                       │           PHOENIX ON PI 4 v1.0           │
                       └──────────────────────────────────────────┘
                                          │
        ┌─────────────────────┬──────────┴────────────┬─────────────────────┐
        │                     │                       │                     │
   COVERED ELSEWHERE     CRITICAL UNCOVERED      IMPORTANT UNCOVERED   OUT OF SCOPE v1.0
        │                     │                       │                     │
   • EL2→EL1             • SD / EMMC2 SDHCI       • I²C / SPI         • CSI-2 camera
   • MMU/cache/SMP       • FAT16/32 fs            • PWM / I²S audio    • DSI display
   • xHCI (VL805)        • Generic Timer audit    • OTP / serial / MAC• HDMI audio
   • GPU/VC6             • DMA framework          • DTB-driven config • Pi 5 / RP1
   • GENET               • GIC-400 hardening      • Power-off paths   • Secure boot
   • WiFi / BT           • Filesystem story       • Compute Module 4  • USB-PD
   • GPIO / pinctrl      • USB mass storage       • DTB variants by   • Update mech
   • RTC / thermal /     • Boot media path        •   RAM size        • Pi 400
     mailbox / watchdog    (SD vs USB vs net)     • Test infra (CI,   • Crash-dump
                                                    QEMU raspi4b,       framework
                                                    soak harness)     • V3D 3D engine
```

### 1.2 The shape of the gap

The covered set is dominated by **bring-up plumbing** (EL drop, MMU, GIC,
console, USB host) and **named flagship subsystems** (display, network,
WiFi, BT). What is left is a mix of:

1. **Storage**: the SDHCI/EMMC2 driver and a real on-disk filesystem.
   Without these, every boot is a TFTP+ramdisk affair forever — fine
   for bring-up, fatal for "v1.0 port".
2. **Sub-platform plumbing**: DMA framework, I²C/SPI, PWM, I²S, audio.
   These are individually small but collectively define what
   "Phoenix on Pi 4" can do beyond the kernel banner.
3. **Identity & config**: OTP/serial/MAC reading, full DTB consumption,
   handling 1/2/4/8 GB RAM variants without per-binary kernel rebuilds.
4. **Userspace**: aarch64 coreutils, libphoenix coverage on this CPU,
   psh on the real console. We have psh smoke-tested under QEMU; we do
   not have a tested userspace on real hardware.
5. **Test infrastructure beyond the netboot loop**: QEMU `raspi4b`
   parity, automated CI, soak/regression harness, hardware lab control.
6. **Pi 4 hardware-revision matrix**: official Pi 4B is 1/2/4/8 GB plus
   the Compute Module 4 plus the Pi 400. Each is a slightly different
   board. Phoenix needs to choose explicitly which it supports for v1.0.

### 1.3 Verdict

A "production-grade Pi 4 port" requires **roughly 14 more major work
items** to land beyond the in-flight set, of which **6 are MUST**, **5
are SHOULD**, and **3 are NICE**. Total estimated effort beyond the
already-planned work: **18–28 developer-weeks** for a single engineer,
or **~3–5 calendar months** at the current cadence. See §4 for the
breakdown.

---

## 2. Gap-by-gap detail

Each item: **What / Why / Tier / Effort / Deps / Open questions**.

### 2.1 SD card SDHCI (EMMC2) controller

**What.** BCM2711 has a dedicated EMMC2 SDHCI controller for the SD slot
at physical address `0x7e340000` (legacy view) / `0xfe340000` (ARM view),
register space `0x100` bytes, 32-bit-only register access, supports DDR50.
A second SDHCI instance (legacy `emmc`, plus an SDIO-only block) services
the BCM43455 WiFi/BT module. Reference:
[BCM2711 ARM Peripherals datasheet](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf);
[Linux mmc: sdhci-iproc patch](https://patchwork.kernel.org/project/linux-arm-kernel/patch/1563776607-8368-1-git-send-email-wahrenst@gmx.net/);
[OSDev raspi4 page](https://wiki.osdev.org/Raspberry_Pi_4).
The Linux compatible string is `brcm,bcm2711-emmc2` and the IP follows the
SDHCI spec, so a standard SDHCI driver plus a small Pi-4 quirk shim is the
realistic implementation shape.

**Why Phoenix needs it.** Without SD storage, every Pi 4 boot is netboot
or USB. Netboot is fine for the lab; for a deployable port, SD-card
boot from cold power is a hard requirement (the Pi's own firmware
expects the boot partition on SD by default). It is also the path to
swap, persistent logs, and a real rootfs.

**Tier.** **MUST** for v1.0.

**Effort.** 3–4 dev-weeks. SDHCI is a large but well-understood IP;
Phoenix already has two SDHCI-shaped drivers in
`phoenix-rtos-devices/storage/zynq7000-sdcard/` and the `sdio/` subtree.
Most of the work is porting/reusing one of those plus adding the BCM2711
quirks (32-bit access, clock manager wiring, DMA path).

**Deps.** Blocks: FAT/ext filesystem on SD, swap, persistent rootfs,
cold-boot from SD without firmware-staged ramdisk. Blocked by:
clock manager (peripheral clock gating), DMA framework if we want
DMA-mode transfers (PIO mode is acceptable for v1.0).

**Open questions.**
- Do we ship PIO-only first, then add DMA? (Recommend yes.)
- Reuse `zynq7000-sdcard` or pull a portable SDHCI core from the Linux
  driver tree? Phoenix policy favors the existing driver.
- Where does block I/O live in Phoenix's userspace device server model?

### 2.2 Filesystem stack: FAT, ext-something, plus Phoenix-native

**What.** Phoenix already has `dummyfs`, `ext2`, `fat`, `jffs2`,
`littlefs`, `meterfs`, `rofs` in `phoenix-rtos-filesystems/`. So the
gap is **integration on aarch64-rpi4b**, not new code: build the
filesystem servers for this target, wire them into the Pi 4 image
manifest, and validate with the SD driver above.

**Why Phoenix needs it.** Pi firmware *requires* the boot partition to
be FAT16/FAT32 — `start4.elf`, `bootcode.bin`, `config.txt`, the kernel
image and DTB all live there and the VC4 firmware refuses any other
filesystem. ext2 (or a Phoenix-native fs) is needed for the rootfs
once we are past the dummyfs ramdisk model.

**Tier.** **MUST** for v1.0 (FAT for boot partition is non-negotiable;
ext2 or equivalent for rootfs is highly desirable).

**Effort.** 1–2 dev-weeks for build/integration; the code already
exists. Add ~1 week if a write-path bug surfaces in the existing FAT
or ext2 driver on aarch64.

**Deps.** Blocks: real cold-boot flow, persistent userspace state.
Blocked by: §2.1 (SDHCI), block-device server abstraction.

**Open questions.**
- Read-only ext2 first, then add write? Phoenix's existing ext2 is
  read-write but its aarch64 maturity is unverified by this team.
- Endianness assumptions in `littlefs` / `jffs2` on aarch64?

### 2.3 BCM2711 DMA framework

**What.** BCM2711 has 16 DMA channels: channels 0–6 are full BCM2835
"normal" channels, 7–10 are "lite" (reduced features), and **11–14 are
new 40-bit channels** that can address the full 35-bit physical map and
issue write bursts; channel 11 specifically services the PCIe interface.
Channel 15 is reserved for VPU. Reference:
[BCM2711 peripherals datasheet, §4](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf);
[Linux dmaengine BCM2711 40-bit DMA RFC series](https://lore.kernel.org/linux-arm-kernel/1640606743-10993-1-git-send-email-stefan.wahren@i2se.com/).
Phoenix has no general-purpose DMA framework today — `phoenix-rtos-devices/dma/`
contains only `grdmac2`, `imx6ull-sdma`, `imxrt-edma` (per-target shims).

**Why Phoenix needs it.** Without DMA, every byte through audio (I²S /
PWM), SD card, GENET, and the camera is a CPU copy. PIO is acceptable
for SD card and console; it is not viable for sustained audio playback
or gigabit Ethernet line rate. Crucially the **GPU mailbox driver path
to VC6 buffers** uses contiguous physical memory that DMA-aware code
needs to allocate and flush correctly even if DMA channels themselves
are not used by Phoenix kernel code directly.

**Tier.** **SHOULD** for v1.0 — Phoenix can ship without DMA-mode
peripherals, but performance ceiling is low.

**Effort.** 4–6 dev-weeks. Designing a clean Phoenix-wide DMA API
(allocation, channel reservation, scatter-gather, bounce buffers for
the >32-bit case) is the bulk of the work; the BCM2711 channel-driver
on top of it is ~1 week.

**Deps.** Blocks: high-throughput SD, GENET line rate, audio. Blocked
by: cache maintenance API (covered by MMU/cache plan), physical-memory
allocator with alignment + DMA-zone constraints.

**Open questions.**
- Should DMA framework live in libstorage, libio, or a new libdma?
- 40-bit channels need a dma_addr_t-shaped abstraction Phoenix doesn't
  have. Compromise: only use channels 0–6 in v1.0, defer 40-bit to v1.1.

### 2.4 Generic Timer audit / hardening

**What.** ARMv8 Generic Timer (CNTFRQ_EL0, CNTV/CNTP_*) is what Phoenix
uses on aarch64 today, including on Pi 4. Status doc says the kernel
reaches `_hal_init` but timer subsystem behaves differently between
QEMU and real hardware (per the iter-7/8 cache-coherency lesson). This
isn't a "missing driver" gap; it is a **validation and hardening gap**.

**Why Phoenix needs it.** Scheduler ticks, sleep, watchdog feed, CCN
ordering — all hang off this clock. If the timer regresses on Pi 4 once
SMP4 is on, every scheduling decision is wrong.

**Tier.** **MUST** — but mostly a verification task, not implementation.

**Effort.** 1 dev-week for an SMP4 timer verification step
(per-CPU CNTV interrupts, frequency cross-check via mailbox tag
0x00030047 vs CNTFRQ_EL0).

**Deps.** SMP enablement (already planned), GIC-400 PPI routing
(already in flight as part of MMU/cache/SMP plan).

**Open questions.**
- Does Phoenix need the BCM2835 *system timer* (separate IP at
  `0x7e003000`) at all, or is the ARM Generic Timer sufficient? Pi
  firmware uses the system timer for early boot; the kernel does not
  have to. Recommend: **OUT-OF-SCOPE for v1.0** unless a need surfaces.

### 2.5 Clock manager (CM_*) — peripheral clock gating

**What.** BCM2711 has a Clock Manager block at `0x7e101000` that gates
peripheral clocks (UART, I²C, SPI, PWM, EMMC, etc.). On hardware reset,
the firmware turns on most of what `start4.elf` needs and leaves the
rest in whatever state the bootloader configured. Reference:
[BCM2711 peripherals datasheet, §6 General Purpose Clocks](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf).

**Why Phoenix needs it.** Without explicit clock control, attempting to
use a peripheral whose clock the firmware did not pre-enable (PWM, I²S,
secondary SPI/I²C) returns silence. The mailbox interface offers an
alternative: tag `0x00030001` (set clock state) and `0x00030002`
(set clock rate) ask the firmware to do it. That works and is what
Linux uses on Pi 4 for many clocks.

**Tier.** **SHOULD** for v1.0 — needed for any peripheral beyond the
firmware default set.

**Effort.** 1–2 dev-weeks if we go via mailbox (bulk of code already
exists in the in-flight rtc-thermal-power plan); 3 weeks if we drive
CM_ registers directly. Recommend mailbox path.

**Deps.** Blocked by: mailbox driver (in flight under the rtc-thermal
plan). Blocks: PWM, I²S, additional I²C/SPI buses.

### 2.6 I²C buses (BCM2835 BSC controllers)

**What.** Pi 4 exposes multiple BSC I²C controllers (BSC0/1/3/4/5/6 on
BCM2711). Each has 8 32-bit registers (Control, Status, DataLength,
SlaveAddress, FIFO, ClockDiv, DataDelay, ClockStretchTimeout) per
[BCM2835 ARM Peripherals datasheet §3](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf)
which the BCM2711 inherits. Phoenix already has
`phoenix-rtos-devices/i2c/imx6ull/` and `i2c/zynq/` — same userspace
device-server shape, different IP.

**Why Phoenix needs it.** The on-board PMIC, EEPROM, the official Pi
touchscreen, and the vast majority of HATs use I²C. Without I²C the
Pi reduces to a netboot UART terminal.

**Tier.** **SHOULD** for v1.0. (Some boards/HATs don't need it; many
do.)

**Effort.** 1–2 dev-weeks. The IP is small. Likely reuse the
`i2c/imx6ull` skeleton.

**Deps.** GPIO/pinctrl (in flight), clock manager (§2.5), the chosen
DMA framework if we want FIFO-DMA mode (PIO is fine for v1.0).

**Open questions.**
- Do we expose all six BSCs or only the headered BSC1 (`/dev/i2c-1`)?
  Recommend: BSC1 only for v1.0.

### 2.7 SPI buses

**What.** BCM2711 exposes 5 full SPI controllers (SPI0/3/4/5/6) plus 2
"mini" auxiliary SPI controllers (SPI1/2). SPI0 is on the standard 40-pin
header. Reference:
[BCM2711 peripherals datasheet §10 SPI](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf).
Phoenix has SPI drivers for imx6ull-ecspi/qspi, zynq, gr716; nothing for
the BCM SPI block.

**Why Phoenix needs it.** Display HATs, sensor breakouts, SD-over-SPI
fallback (we don't need this), etc. Lower priority than I²C.

**Tier.** **NICE** for v1.0.

**Effort.** 1.5 dev-weeks for SPI0 only, plus ~3 days per additional
controller.

**Deps.** GPIO/pinctrl, clock manager.

### 2.8 PWM (BCM2835 PWM block)

**What.** BCM2711 has two PWM controllers (PWM0 / PWM1). PWM0 maps to
GPIO12/13/18/19 with a few alt functions; **PWM1 channels 0/1 are wired
to GPIO40/41 and feed the analogue audio circuit (3.5 mm jack)**.
Each controller has a 16×32 FIFO and a DMA mode. Per
[the BCM2711 forum thread](https://forums.raspberrypi.com/viewtopic.php?t=295059),
Pi 4 ARM-side bases are `0xfe20c000` (PWM0) and `0xfe20c800` (PWM1).

**Why Phoenix needs it.** PWM is the fan controller (Pi 4B uses a
GPIO-PWM-driven cooling fan in many cases), the analogue audio path
(headphone jack), and a common servo/LED interface for HATs.

**Tier.** **NICE** for v1.0 (analogue audio is OUT-OF-SCOPE; servo
/ fan-control are nice-to-have).

**Effort.** 1 dev-week.

**Deps.** GPIO/pinctrl, clock manager. DMA only if we want FIFO mode
for audio.

### 2.9 I²S / PCM audio

**What.** BCM2711 has the BCM2835-inherited I²S/PCM block on
GPIO18-21. Stereo only; max ~48 kHz typical. Reference:
[STICKY: The I2S sound thread](https://forums.raspberrypi.com/viewtopic.php?t=8496).

**Why Phoenix needs it.** It does not, for v1.0. Phoenix has no audio
stack and no real-time audio user. Out of scope unless a deployment
target appears.

**Tier.** **OUT-OF-SCOPE for v1.0**. Document so future work has a
starting point.

**Effort.** N/A (deferred). When done, ~2 dev-weeks driver + 2–4 weeks
for whatever audio framework Phoenix ends up adopting.

### 2.10 HDMI audio path

**What.** Audio over HDMI goes through the VC6 (covered) plus a separate
HDMI-audio configuration that is owned almost entirely by the firmware.
Linux drives it via `vc4-hdmi`.

**Tier.** **OUT-OF-SCOPE for v1.0**.

### 2.11 CSI-2 camera and DSI display

**What.** Two MIPI CSI-2 ports for Pi camera modules and two MIPI DSI
ports for the Pi touchscreen / DSI panels. These hang off the
ISP/Unicam blocks in BCM2711 and are firmware-mediated for many use
cases.

**Tier.** **OUT-OF-SCOPE for v1.0** — these are large standalone
subsystems whose absence does not block any defined v1.0 use case.

### 2.12 GIC-400 hardening for Pi 4 specifics

**What.** Pi 4 routes its 192 SPIs through the GIC-400 at
`0xff841000` (distributor) / `0xff842000` (CPU interface), per the
[BCM2711 dts](https://github.com/raspberrypi/linux/blob/rpi-5.10.y/arch/arm/boot/dts/bcm2711.dtsi).
Phoenix already has a working GIC v2 driver (used during the iter-7/8
struggles); the gap is **Pi-4-specific quirks**: hyp-mode-vs-EL1
register access decisions baked in by the EL2→EL1 drop, secondary-
CPU GIC-CPU-interface init, plus avoiding the now-deprecated BCM2835
ARMC interrupt controller (Pi 0–3 only — *not present in routing on
Pi 4*).

**Tier.** **MUST** — the kernel cannot reach userspace reliably
without solid IRQ delivery on all 4 cores.

**Effort.** Likely already mostly done as part of the MMU/cache/SMP
plan. Allow 1 dev-week of buffer for SMP4 IRQ-routing surprises.

**Deps.** Already integrated with the in-flight SMP plan.

### 2.13 Mailbox-property: OTP, serial, MAC reading

**What.** The VideoCore mailbox property interface
([wiki](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface))
exposes:
- Tag `0x00010004` get_board_serial → 64-bit serial.
- Tag `0x00010003` get_board_revision → identifies Pi 4B vs CM4 vs Pi 400 and RAM size.
- Tag `0x00030054` get_mac_address → 6-byte MAC for the on-board GENET.
- Tag `0x00030021` get_clocks → list of available clock IDs.

The rtc-thermal-power plan already includes the mailbox driver. The
gap is **adding the identity-read tags** and exposing them to userspace
(e.g. `/proc/cpuinfo`-like surface, or a Phoenix-native mechanism).

**Why Phoenix needs it.** GENET initialization needs the MAC. WiFi
initialization needs the WiFi MAC (derived per the OUI rules in the
[changing-MAC-addresses whitepaper](https://pip-assets.raspberrypi.com/categories/685-app-notes-guides-whitepapers/documents/RP-003474-WP-3-Changing%20MAC%20addresses.pdf)).
Telemetry needs the serial. Multi-board variant logic needs the revision.

**Tier.** **MUST** for v1.0.

**Effort.** 0.5 dev-weeks on top of the existing mailbox driver.

**Deps.** Mailbox driver from the rtc-thermal plan.

**Open questions.**
- WiFi MAC: derive from serial or fetch via separate firmware path
  (BCM43455 NVRAM)? The covered WiFi research will answer this.

### 2.14 DTB consumption: from minimal to driver-aware

**What.** Today Phoenix uses the DTB minimally — to find memory size
and the UART. A "production" port should consume DT to discover device
register bases, IRQ numbers, clock parents, GPIO pinmux, and so on,
the way Linux does.

**Why Phoenix needs it.** Multiple Pi 4 SKUs (1/2/4/8 GB; CM4 with
different on-board peripherals; Pi 400) ship with DTB variants. Without
DT-driven config, every variant needs a per-binary kernel rebuild —
operationally untenable.

**Tier.** **SHOULD** for v1.0; lean on a thin "DT discovery" layer
rather than full Linux-style of_device_id binding.

**Effort.** 3–4 dev-weeks.

**Deps.** None (libfdt-style parsing is mostly self-contained).
Blocks: clean handling of the multi-SKU matrix (§2.16).

**Open questions.**
- How much of Linux's `compatible` matching do we replicate vs hardcode
  Pi-4-specific tables? Recommend: hardcode for v1.0; design for
  extensibility.

### 2.15 RAM-size and SKU matrix (1 / 2 / 4 / 8 GB; CM4; Pi 400)

**What.** Pi 4B ships in 1, 2, 4, 8 GB variants. Pi 400 is functionally
a Pi 4 in a keyboard. CM4 is the same SoC on a denser form factor with
optional eMMC and PCIe brought out. Each has DTB differences (memory
size, USB-C OTG vs host, presence of Ethernet PHY, etc.).

**Why Phoenix needs it.** Picking only one SKU and shipping is fine
for bring-up but not for "v1.0". The decision is partly product
(which SKUs do we sell support for) and partly engineering (how much
extra work is each).

**Tier.**
- **Pi 4B 4 GB**: **MUST** — current dev target.
- **Pi 4B 1/2/8 GB**: **SHOULD** — same SoC, only RAM map differs;
  cost is in DT ingestion.
- **Pi 400**: **NICE** — keyboard form factor, same SoC.
- **CM4**: **OUT-OF-SCOPE for v1.0** — eMMC and PCIe expose new
  surface area.

**Effort.** 1 dev-week per SKU once DT consumption (§2.14) is in.

**Deps.** §2.14 (DT-driven config), §2.13 (revision tag), §2.1 (SDHCI
for the variants that have eMMC).

### 2.16 Boot media flow: SD vs USB vs network

**What.** Pi 4 boot order is configurable via the EEPROM
`BOOT_ORDER`. Default is SD, then USB, then network. Phoenix
already has network boot working in the lab. SD boot needs §2.1+§2.2.
USB boot needs §2.1 (firmware loads off USB Mass Storage *before*
Phoenix runs, but Phoenix then needs to mount its rootfs from there
which means USB MSC class driver, which is mentioned in the USB plan
but not detailed there).

**Why Phoenix needs it.**
- SD boot is the only end-user-friendly path.
- USB boot is needed for Pi 4 deployments without an SD slot (rare).
- Netboot is dev-only.

**Tier.**
- SD boot: **MUST** for v1.0.
- USB boot: **NICE**.
- Netboot: already DONE — keep it; document state.

**Effort.** Bundled with §2.1 (SDHCI) + §2.2 (FAT) + ~0.5 weeks of
boot-flow integration testing.

### 2.17 USB Mass Storage class

**What.** USB MSC class on top of the in-flight xHCI/USB stack. The
USB plan covers HID keyboard but only mentions MSC.

**Why Phoenix needs it.** Pi 4 users routinely boot from USB SSD/USB
flash. Without MSC, the in-flight USB stack only enumerates HID
devices.

**Tier.** **SHOULD** for v1.0 (MUST if SD-card boot slips).

**Effort.** 2 dev-weeks. Phoenix has `phoenix-rtos-devices/storage/umass`
already — porting + xHCI integration.

**Deps.** xHCI plan (in flight); §2.2 (FAT).

### 2.18 USB Hub class

**What.** xHCI plan likely targets root hub + one device. Real-world
Pi 4 users plug in hubs, then keyboard + mouse + storage. USB hub class
driver is needed for that.

**Tier.** **SHOULD** for v1.0.

**Effort.** 1.5 dev-weeks.

**Deps.** xHCI plan.

### 2.19 USB-C power and over-current detection

**What.** Pi 4 uses USB-C for power but does **not** implement USB-PD;
it expects 5 V / 3 A from a dumb USB-C supply. The PMIC reports
under-voltage via a GPIO; firmware reads it. Phoenix can read the
"undervoltage / throttling" status via mailbox tag `0x00030047`
get_throttled.

**Why Phoenix needs it.** Telemetry, log warnings to UART when the
operator's power supply is marginal.

**Tier.** **NICE** for v1.0.

**Effort.** 0.5 dev-weeks.

**Deps.** Mailbox driver.

### 2.20 Userspace: aarch64 coreutils, libphoenix coverage, psh on hardware

**What.** Phoenix has `libphoenix`, `phoenix-rtos-corelibs`, and a
psh shell. QEMU smoke tests ran through psh. We do not yet have a
documented "Phoenix ran psh on real Pi 4 hardware and accepted a
keystroke from a USB keyboard". Subgoals:
- aarch64 build coverage of all libphoenix subdirs
  (`net`, `pthread`, `signal`, `regex`, `wchar`, `time`, `sys`,
  `posix` already exist on the source tree but aarch64 maturity needs
  a sweep).
- A minimal coreutils set: `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`,
  `mount`, `dd`, `ifconfig`-equivalent.
- psh interactive on PL011 with line editing.

**Tier.** **MUST** for v1.0.

**Effort.** 3–4 dev-weeks (mostly verification + small bug fixes).

**Deps.** §2.1+§2.2 (rootfs), USB plan (HID keyboard), GENET (for
network commands).

### 2.21 Test infrastructure: QEMU raspi4b parity, CI, soak

**What.** Three sub-gaps:
1. **QEMU `raspi4b` parity.** Per
   [QEMU raspi documentation](https://www.qemu.org/docs/master/system/arm/raspi.html),
   raspi4b in QEMU supports interrupt controller, DMA, CPRMAN, system
   timer, GPIO, PL011/AUX UARTs, RNG, framebuffer, USB host, SD/MMC,
   thermal, mailbox, SPI, I²C, PWM, PCIe root port, GENET. **PCIe and
   the new VL805 USB are not supported** — QEMU only provides the old
   USB controller. The probe-parity rule means we should at minimum
   know which Phoenix subsystems can be QEMU-tested vs hardware-only.
2. **CI for the Pi 4 port.** Today this is a manual netboot loop. A
   self-hosted runner (Linux build VM + a Pi 4 in the lab + a power
   relay) would let every commit trip the full boot test.
3. **Soak / regression suite beyond first-boot.** Hours-long uptime,
   IRQ-rate stress, USB hot-plug, SD-card I/O loop.

**Tier.** **MUST** (CI). **SHOULD** (QEMU parity catalogue, soak).

**Effort.** 2 dev-weeks for CI; 1 week for QEMU parity catalogue;
2–3 weeks for an initial soak harness.

**Deps.** None for the CI work; soak depends on §2.1, §2.20.

### 2.22 Crash-dump / panic recovery

**What.** A mechanism to capture register state + stack on a kernel
panic and persist it to (a) UART, (b) a reserved memory region, or
(c) SD card. Phoenix has minimal panic UART output today.

**Tier.** **NICE** for v1.0 — UART panic output (which we have) is
enough for early production.

**Effort.** 1 dev-week (UART-only enrichment); 3+ weeks for
persistent dump-to-disk.

### 2.23 Watchdog feed in long-running services

**What.** Pi 4 has a watchdog in the PM (power management) block at
`0x7e100000`. The rtc-thermal-power plan covers the **driver**. The
gap is **userland integration**: psh / device servers need to feed
the watchdog; failure to feed should reboot.

**Tier.** **SHOULD** for v1.0.

**Effort.** 1 dev-week.

**Deps.** Watchdog driver from rtc-thermal-power plan.

### 2.24 Secure boot

**What.** Pi 4 supports a limited form of EEPROM-signed boot but not
full secure boot of the kernel image. Linux distributions typically
do not enable secure boot on Pi 4.

**Tier.** **OUT-OF-SCOPE for v1.0**.

### 2.25 Update mechanism (firmware + kernel + filesystem)

**What.** A/B updates, EEPROM update flow (`rpi-eeprom-update`-style),
kernel image rollback.

**Tier.** **OUT-OF-SCOPE for v1.0**. Document as a v1.x roadmap item.

### 2.26 Pi 5 / RP1 readiness

**What.** Pi 5 moves all GPIO/UART/I²C/SPI/USB/Ethernet/SDIO into the
RP1 chiplet behind a PCIe 2.0 ×4 link. Per
[Raspberry Pi I/O controllers documentation](https://www.raspberrypi.com/documentation/computers/io-controllers.html)
and [the RP1 announcement](https://www.raspberrypi.com/news/rp1-the-silicon-controlling-raspberry-pi-5-i-o-designed-here-at-raspberry-pi/),
RP1 owns: GPIO, MIPI, USB, Ethernet, analogue TV/audio, SDIO, UARTs,
SPIs. Reusable from Pi 4 work: AArch64 plumbing, MMU/cache, GIC, ARM
Generic Timer, mailbox/firmware patterns, GPU/VC6 (mostly), GENET (the
Pi 5 actually keeps GENET-shaped Ethernet but behind RP1).
**Not reusable**: I²C/SPI/UART register-level drivers — RP1 has
*different* IP for these. So our §2.6/§2.7/§2.8 work is Pi-4-specific.

**Tier.** **OUT-OF-SCOPE for v1.0** by project rule. Document the
gap so the Pi 5 plan starts informed.

---

## 3. Critical-path analysis

The dependency DAG for the **MUST** items, ignoring already-in-flight
work:

```
MMU/cache/SMP (in flight) ──► GIC-400 hardening (§2.12) ──► Generic Timer audit (§2.4)
                                       │
mailbox driver (in flight) ──► OTP/serial/MAC reading (§2.13)
                                       │
                                       ▼
clock manager via mailbox (§2.5) ──► (peripheral clocks unlocked)
                                       │
                                       ▼
SDHCI / EMMC2 (§2.1) ──► FAT + ext fs integration (§2.2) ──► SD boot (§2.16)
                                       │
                                       ▼
                          userspace on real hardware (§2.20)
                                       │
                                       ▼
                                   CI loop (§2.21)
```

Critical path: **SDHCI → FAT → SD boot → real-hw userspace → CI**.
Everything else can land in parallel once GIC and mailbox are solid.

The **SHOULD/NICE** items mostly attach to the clock-manager-unlocked
node — I²C, SPI, PWM, watchdog feeding all light up once peripheral
clocks are programmable.

---

## 4. Total-effort estimate

### MUSTs (must land for v1.0 to ship)

| Item                                  | Effort       |
|---------------------------------------|--------------|
| §2.1  SDHCI / EMMC2                   | 3–4 weeks    |
| §2.2  FAT + ext2 integration          | 1–2 weeks    |
| §2.4  Generic Timer audit             | 1 week       |
| §2.12 GIC-400 hardening (buffer)      | 1 week       |
| §2.13 OTP / serial / MAC read         | 0.5 week     |
| §2.16 SD boot integration             | bundled      |
| §2.20 Userspace on real hardware      | 3–4 weeks    |
| §2.21 CI loop                         | 2 weeks      |
| **Subtotal MUSTs**                    | **11.5–14.5 weeks** |

### SHOULDs (should land, defensible to defer one minor)

| Item                                  | Effort       |
|---------------------------------------|--------------|
| §2.3  DMA framework                   | 4–6 weeks    |
| §2.5  Clock manager via mailbox       | 1–2 weeks    |
| §2.6  I²C (BSC1)                      | 1–2 weeks    |
| §2.14 DTB consumption layer           | 3–4 weeks    |
| §2.15 RAM-size + Pi 400 SKU matrix    | 2 weeks      |
| §2.17 USB Mass Storage class          | 2 weeks      |
| §2.18 USB Hub class                   | 1.5 weeks    |
| §2.21 QEMU parity catalogue + soak    | 3–4 weeks    |
| §2.23 Watchdog userland feed          | 1 week       |
| **Subtotal SHOULDs**                  | **18.5–25.5 weeks** |

### NICEs

| Item                                  | Effort       |
|---------------------------------------|--------------|
| §2.7  SPI (SPI0)                      | 1.5 weeks    |
| §2.8  PWM                             | 1 week       |
| §2.19 USB-C power telemetry           | 0.5 week     |
| §2.22 Crash-dump (UART-only)          | 1 week       |
| **Subtotal NICEs**                    | **4 weeks**  |

### Grand total

- **MUSTs only**: ~12–15 dev-weeks ≈ **3 calendar months** at current pace.
- **MUSTs + SHOULDs**: ~30–40 dev-weeks ≈ **6–9 calendar months**.
- **All tiers**: ~35–45 dev-weeks ≈ **8–11 calendar months**.

These estimates assume one full-time engineer. They do not include
debug churn for newly-discovered cache-coherency-class problems on
Cortex-A72 of the kind that consumed 2026-04 (TD-04). Add 20–40 %
contingency.

---

## 5. Explicit out-of-scope decisions for v1.0

The Pi 4 v1.0 port **explicitly will not** include:

1. **CSI-2 camera and DSI display.** Large standalone subsystems with
   no defined v1.0 use case. (§2.11)
2. **HDMI audio path.** Firmware-mediated, low value. (§2.10)
3. **I²S audio + the surrounding audio framework.** Phoenix has no
   audio stack. (§2.9)
4. **Compute Module 4 and Pi 5.** Different boards / SoC. (§2.15, §2.26)
5. **Secure boot.** Pi 4 can't really do it; Linux doesn't bother. (§2.24)
6. **Field update / A/B partitions.** Roadmap item for v1.x. (§2.25)
7. **V3D 3D rendering.** Display framebuffer is in scope (covered
   under GPU/VC6); 3D acceleration is not.
8. **PCIe-as-a-bus** (other than the on-board VL805 endpoint). Pi 4 has
   one PCIe lane brought out only on CM4; v1.0 does not target CM4.
9. **40-bit DMA channels (11–14).** v1.0 ships with channels 0–6 only
   if DMA is in. (§2.3)
10. **Persistent crash-dump to disk.** UART panic output is enough.
    (§2.22)
11. **BCM2835 system timer.** The ARM Generic Timer is sufficient. (§2.4)
12. **Old BCM2835 ARMC interrupt controller path.** Pi 4 routes
    everything through the GIC-400 — no fallback driver needed.
13. **Multiple I²C / SPI buses beyond the one on the 40-pin header.**

---

## 6. Recommended next research / plan agents to spawn

Spawn order is by criticality and dependency:

1. **`docs/research/sdhci-emmc2.md` + `-non-linux.md`** — the BCM2711
   EMMC2 SDHCI controller. Cite the Linux iproc driver, SDHCI spec
   sections, BCM2711 datasheet §3, and Phoenix's existing
   `zynq7000-sdcard` for pattern. **Highest priority** — gates SD
   boot, FAT integration, and userspace-on-real-hw.
2. **`docs/plans/sdhci-emmc2-impl.md`** — implementation plan once
   the research lands. Decide PIO-only vs DMA, server location,
   block-device API.
3. **`docs/research/dtb-consumption.md`** — design a thin DT-driven
   discovery layer: how Phoenix should ingest DTB beyond the current
   memory-size + UART use, and what the boundary is between "firmware
   gives us the DTB" and "kernel consumes it" vs userspace device
   servers.
4. **`docs/research/pi4-filesystem-integration.md`** — how to wire
   `phoenix-rtos-filesystems/{fat, ext2}` into the Pi 4 image, mount
   strategy, root-on-SD vs ramdisk-then-pivot.
5. **`docs/research/pi4-dma-framework.md`** — design a Phoenix-wide
   DMA API. Survey what exists (`grdmac2`, `imx6ull-sdma`,
   `imxrt-edma`) and propose a generalization.
6. **`docs/research/userspace-on-pi4.md`** — current state of psh,
   coreutils, libphoenix on aarch64; what is verified on QEMU
   `aarch64-generic` vs needs verification on real Pi 4.
7. **`docs/plans/i2c-bsc1-impl.md`** — small but unblocks HATs and
   the touchscreen.
8. **`docs/research/pi4-ci.md`** — the lab automation story:
   self-hosted runner, hardware control, the bridge between `gh`
   workflows and `scripts/test-cycle-netboot.sh`.

Lower priority (defer until the above are mid-flight): SPI, PWM,
USB MSC, USB Hub, OTP-tag wiring, watchdog userland.

---

## Sources

- [BCM2711 ARM Peripherals datasheet](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf)
- [BCM2835 ARM Peripherals datasheet](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf)
- [Linux raspberrypi/linux bcm2711.dtsi](https://github.com/raspberrypi/linux/blob/rpi-5.10.y/arch/arm/boot/dts/bcm2711.dtsi)
- [Linux mmc: sdhci-iproc BCM2711 emmc2 patch](https://patchwork.kernel.org/project/linux-arm-kernel/patch/1563776607-8368-1-git-send-email-wahrenst@gmx.net/)
- [Linux dmaengine BCM2711 40-bit DMA RFC](https://lore.kernel.org/linux-arm-kernel/1640606743-10993-1-git-send-email-stefan.wahren@i2se.com/)
- [QEMU raspi machine documentation](https://www.qemu.org/docs/master/system/arm/raspi.html)
- [Mailbox property interface wiki](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface)
- [Changing MAC addresses on Raspberry Pi (white paper)](https://pip-assets.raspberrypi.com/categories/685-app-notes-guides-whitepapers/documents/RP-003474-WP-3-Changing%20MAC%20addresses.pdf)
- [Raspberry Pi I/O controllers documentation](https://www.raspberrypi.com/documentation/computers/io-controllers.html)
- [RP1 announcement](https://www.raspberrypi.com/news/rp1-the-silicon-controlling-raspberry-pi-5-i-o-designed-here-at-raspberry-pi/)
- [OSDev Raspberry Pi 4 page](https://wiki.osdev.org/Raspberry_Pi_4)
- [RTEMS Raspberry Pi 4B BSP user manual](https://docs.rtems.org/docs/main/user/bsps/aarch64/raspberrypi4.html)
- [Phoenix-RTOS filesystems repo](https://github.com/phoenix-rtos/phoenix-rtos-filesystems)
- [Phoenix-RTOS documentation index](https://docs.phoenix-rtos.com/latest/index.html)
- Phoenix-RTOS source observation (this repo's sibling clones):
  `phoenix-rtos-devices/{storage,sdio,i2c,spi,dma,usb,...}`,
  `phoenix-rtos-filesystems/{fat,ext2,jffs2,littlefs,...}`,
  `libphoenix/`, `phoenix-rtos-corelibs/`.

`Re-verify:` Pi 4 EEPROM defaults, QEMU `raspi4b` peripheral coverage,
Phoenix-RTOS filesystem aarch64 status, RP1 PCIe exposure on Pi 5 —
all may shift between sessions.
