# GPIO + pinctrl on BCM2711 — Forward-Research Brief

Scope: what Phoenix-RTOS needs to know to implement GPIO + pin-mux on
the Raspberry Pi 4. Planning doc, not a porting recipe.

## 1. Hardware overview

The BCM2711 GPIO controller is a direct descendant of the BCM2835 block:
54 GPIO lines, organised as two banks (0..27 and 28..53), with the same
register layout as earlier Pi SoCs plus one new addition for pull
configuration.

Per the BCM2711 ARM Peripherals manual, the GPIO block is addressable in
two views. In the legacy 32-bit "VC" view (the addresses literally printed
in the datasheet) the GPIO base is `0x7E200000`. The ARM does not see that
address; instead it sees the same block at `0xFE200000` in **low-peripheral
mode** (the default the VPU sets up at boot) or at `0x4_7E20_0000` in
**high-peripheral mode**. Phoenix's bring-up uses low-peri, matching the
current `armv8r52` HAL constants. See section 5 for the gotcha.

Register map (offsets from the GPIO base; widths are 32-bit), preserving
the BCM2835 names ([BCM2711 ARM Peripherals datasheet, ch. 5][1]):

| Offset | Name | Function |
|--------|------|----------|
| 0x00..0x14 | GPFSEL0..5 | Function select, 3 bits/pin (10 pins per reg, last reg uses 4) |
| 0x1C, 0x20 | GPSET0..1 | Atomic set-output-high (write 1 to set bit) |
| 0x28, 0x2C | GPCLR0..1 | Atomic set-output-low |
| 0x34, 0x38 | GPLEV0..1 | Pin level (read) |
| 0x40, 0x44 | GPEDS0..1 | Event-detect status; W1C |
| 0x4C, 0x50 | GPREN0..1 | Rising-edge detect enable |
| 0x58, 0x5C | GPFEN0..1 | Falling-edge detect enable |
| 0x64, 0x68 | GPHEN0..1 | High-level detect enable |
| 0x70, 0x74 | GPLEN0..1 | Low-level detect enable |
| 0x7C, 0x80 | GPAREN0..1 | Async rising-edge |
| 0x88, 0x8C | GPAFEN0..1 | Async falling-edge |
| 0x94 | GPPUD | **Legacy** pull control — present but RAZ/WI on BCM2711 |
| 0x98, 0x9C | GPPUDCLK0..1 | **Legacy** pull clock — RAZ/WI on BCM2711 |
| 0xE4..0xF0 | GPIO_PUP_PDN_CNTRL_REG0..3 | **NEW on BCM2711**, 2 bits/pin |

The new `GPIO_PUP_PDN_CNTRL_REGn` registers replace the BCM2835
GPPUD/GPPUDCLK strobe dance. Encoding per pin: `00`=none, `01`=pull-up,
`10`=pull-down, `11`=reserved. Each register covers 16 pins. REG0 is at
`0x7E2000E4` legacy / `0xFE2000E4` ARM low-peri — confirmed by the
[U-Boot pinctrl-bcm283x patch series][2].

## 2. Linux driver structure

Linux splits the responsibility cleanly. `drivers/pinctrl/bcm/pinctrl-bcm2835.c`
([upstream source][3]) is the **only** driver — it implements both the
pinctrl ops (function select, pull config, group/function tables) and the
`gpio_chip` ops in one file. The probe path supports three `compatible`
strings: `brcm,bcm2835-gpio`, `brcm,bcm7211-gpio`, and the BCM2711-specific
`brcm,bcm2711-gpio`. The compatible value is what gates the new pull
register code path.

The driver registers one `struct bcm2835_pinctrl` carrying a
`pinctrl_dev *`, a `gpio_chip`, a `pinctrl_gpio_range`, and a
`gpio_irq_chip`. GPIO chip ops: `bcm2835_gpio_request` / `_free`,
`_direction_input` / `_output` (writes GPFSELn), `_get` (reads GPLEVn),
`_set` (writes GPSETn/GPCLRn — no RMW, the dual-register pair is the
point), `_get_direction` (decodes GPFSELn). The IRQ chip wires GPEDSn
(W1C) and uses GPRENn/GPFENn/GPHENn/GPLENn (sync) plus GPAREN/GPAFEN
(async) for trigger config.

A standalone `gpio-bcm2835` driver name existed historically; in current
upstream the combined `pinctrl-bcm2835` is canonical.

## 3. Device-tree bindings

Binding doc: `Documentation/devicetree/bindings/pinctrl/brcm,bcm2835-gpio.yaml`
(text version still mirrored at [kernel.org][4]).

Per-node properties on a pin-config subnode:
- `brcm,pins` — array of GPIO IDs (0..53)
- `brcm,function` — integer: `0`=GPIO_IN, `1`=GPIO_OUT, `2`=ALT5, `3`=ALT4,
  `4`=ALT0, `5`=ALT1, `6`=ALT2, `7`=ALT3 (note the deliberately weird
  ordering — it matches the GPFSEL bit encoding)
- `brcm,pull` — `0`=none, `1`=down, `2`=up

The newer generic bindings (`bias-disable`, `bias-pull-up`,
`bias-pull-down`, `output-high`, `output-low`) are also accepted and
preferred for new DTs.

Consumers reference a pinctrl state with the standard `pinctrl-names =
"default";` / `pinctrl-0 = <&my_pins>;` pair. Userspace access in Linux is
via `/dev/gpiochipN` and libgpiod (`gpioget`, `gpioset`, `gpiomon`).

## 4. Header-pin mapping

The 40-pin header on Pi 2/3/4/5 exposes 26 user-controllable BCM GPIOs
plus 8 grounds, 4 power rails, and 2 HAT-ID pins (GPIO0/1). Mapping is
identical across Pi 2/3/4; canonical reference at [pinout.xyz][5] and
the [Raspberry Pi GPIO docs][6]. Highlights:

- UART0 (TXD/RXD): GPIO14/15 on header pins 8/10
- I²C-1 (SDA/SCL): GPIO2/3 on pins 3/5
- SPI-0: GPIO10/9/11 (MOSI/MISO/SCLK), CE0/CE1 = GPIO8/7
- HAT EEPROM (reserved): GPIO0/1 on pins 27/28

GPIOs 28..45 exist on the SoC but are not on the header — routed to SD
card, Ethernet PHY, ID pins, and the LED expander (section 7). GPIOs
46..53 are not pinned out at all on Pi 4.

## 5. Pi 4-specific quirks

**Pull configuration.** The legacy GPPUD/GPPUDCLK strobe sequence is gone
— those registers read zero and silently drop writes on BCM2711. All
pull config must go through `GPIO_PUP_PDN_CNTRL_REGn`. BCM2835-style
code looks like it succeeded but leaves the pull network unchanged.
See [linux-gpio patchwork 1563776607][7].

**Interrupt routing.** Earlier Pis routed the four GPIO bank interrupts
through the BCM "ARMC" interrupt controller. On BCM2711 they go through
the GIC-400 as SPIs 113, 114, 115, 116 — confirmed by
[`bcm2711.dtsi`][8] in the rpi linux tree. Phoenix's `armv8r52` HAL
already deals with the GIC for the timer, so wiring GPIO IRQs is mostly a
matter of registering the right SPI numbers; no new controller driver
needed.

**Peripheral base.** Low-peripheral vs high-peripheral mode shifts every
MMIO base, including the GPIO block. Phoenix's existing constants assume
low-peri (`0xFE000000`-class). If we ever flip `arm_peri_high=1` in
`config.txt` we have to rebase every peripheral table together.

## 6. Phoenix-RTOS path to "working"

Tier 0 — **MMIO toggle, no abstraction.** Map one page covering
`0xFE200000`, write GPFSELn for one pin (typically the ACT LED — see next
section), then bang GPSET/GPCLR in a loop. No driver, no DT. Used as a
"are we alive on the bus?" oscilloscope check. Depends only on the HAL's
existing `dev_mmio` mapping path.

Tier 1 — **fsel API.** A small `gpio_setalt(pin, fn)` plus `gpio_get`,
`gpio_set`, `gpio_clear`. Lives in `hal/armv8r52/` next to the UART code.
Enables ALT0 routing for SPI/I²C/PWM later. Depends on the page-cache
fix from TD-04 already landing.

Tier 2 — **Pull configuration via GPIO_PUP_PDN_CNTRL.** Implemented as
`gpio_setpull(pin, none|up|down)`. Strict BCM2711-only — refuse to compile
on bcm2835/2837. No state machine, just RMW the right register; that's
the whole point of the new block.

Tier 3 — **GPIO IRQ.** Wire bank-0 and bank-1 GPEDS through the GIC SPIs
to the Phoenix IRQ subsystem. Reuses the existing GIC distributor code
that already handles the timer. Per-pin trigger configuration via
GPRENn / GPFENn / GPHENn / GPLENn. Async-edge variants (GPAREN/GPAFEN)
deferred — they sample on a 32 kHz clock and most users want the sync
version anyway. Depends on the IRQ subsystem reaching steady state on Pi 4.

Tier 4 — **libgpiod-style ABI.** A `/dev/gpiochip0` character-device
emulation in Phoenix userspace, exposing the same `GPIO_GET_LINE_INFO_IOCTL`
/ `GPIO_GET_LINEHANDLE_IOCTL` shape. This is the only tier where we
have a real chance of running upstream gpiod consumers unchanged.
Depends on Phoenix's chardev infrastructure and the IRQ tier.

## 7. ACT LED on Pi 4

The Pi 4 schematic ([`raspberry-pi-4-reduced-schematics.pdf`][9])
shows the green "ACT" LED is **not** wired to a 40-pin BCM GPIO. It is
behind a secondary I²C-attached GPIO expander that the VPU firmware
drives. Pi 3B did the same thing.

"GPIO 42 = ACT" refers to a *virtual* expander pin index honoured by
firmware when you set `dtoverlay=act-led,gpio=42` — it is not a BCM2711
SoC GPIO and toggling GPFSEL4/GPSET1 will not blink the on-board LED.
Pi 2/3+ used GPIO 47 directly; Pi 4 broke that.

For a Phoenix Tier-0 blink: either (a) cooperate with firmware via the
mailbox like `dtoverlay=act-led`, or (b) toggle a header pin (e.g.
GPIO 21 / pin 40) with an external LED. Tier 0 should pick (b).

## 8. Open questions

- Does the GIC reset state route SPI 113..116 to a specific CPU at all,
  or do we need explicit `GICD_ITARGETSRn` programming the way we did
  for the timer? Verify on hardware before designing the IRQ tier.
- Do we want the libgpiod ABI to live in the kernel chardev path or in
  a userspace server? The latter is more Phoenix-idiomatic but adds an
  IPC hop per pin toggle.
- Is there value in keeping the legacy GPPUD path compiled for shared
  Pi 3 / Pi 4 binaries, or do we hard-fork pull-config per SoC family?
  Linux keeps both via `compatible`; we may not need to.
- ALT-function tables: the BCM2711 datasheet ALT0..ALT5 columns are not
  byte-identical to BCM2835 (a few peripherals moved). We need to copy
  the table verbatim — do not paraphrase from memory.

## Sources

- [1] [BCM2711 ARM Peripherals datasheet (PDF)](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf)
- [2] [u-boot ML: pinctrl bcm283x BCM2711 pull-up/down patches](http://www.mail-archive.com/u-boot@lists.denx.de/msg558605.html)
- [3] [linux pinctrl-bcm2835.c (master)](https://github.com/torvalds/linux/blob/master/drivers/pinctrl/bcm/pinctrl-bcm2835.c)
- [4] [Documentation/devicetree/bindings/pinctrl/brcm,bcm2835-gpio.txt (kernel.org)](https://www.kernel.org/doc/Documentation/devicetree/bindings/pinctrl/brcm,bcm2835-gpio.txt)
- [5] [Raspberry Pi GPIO pinout reference](https://pinout.xyz/)
- [6] [Raspberry Pi GPIO documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio-and-the-40-pin-header)
- [7] [linux-gpio: BCM2711 pull-up support patch (patchwork)](https://patchwork.ozlabs.org/project/linux-gpio/patch/1563776607-8368-3-git-send-email-wahrenst@gmx.net/)
- [8] [bcm2711.dtsi (raspberrypi/linux rpi-6.6.y)](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm/boot/dts/broadcom/bcm2711.dtsi)
- [9] [Raspberry Pi 4 reduced schematics (PDF)](https://datasheets.raspberrypi.com/rpi4/raspberry-pi-4-reduced-schematics.pdf)
- [Pi forum: ACT LED on Pi 4 / GPIO expander discussion](https://forums.raspberrypi.com/viewtopic.php?t=287974)
- [Pi forum: GPIO base address obtained in the kernel](https://forums.raspberrypi.com/viewtopic.php?t=283456)
- [rpi4-osdev: low-peripheral mode notes](https://www.rpi4os.com/part4-miniuart/)
