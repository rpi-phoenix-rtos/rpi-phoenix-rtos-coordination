# Forward-research brief: Bluetooth on BCM43455 (Pi 4, BT side, UART HCI H4)

Scope: what Phoenix-RTOS needs to bring up the Bluetooth side of the
CYW/BCM43455 combo chip on a Raspberry Pi 4B. Wi-Fi (SDIO/brcmfmac) is out of
scope; UART-attached HCI controller only.

## 1. Hardware overview

The BCM43455 (rebranded by Infineon/Cypress as CYW43455) is a combo chip whose
**Bluetooth side is a standard HCI controller over UART, framed in H4**. On the
Pi 4B the BT UART link is wired to the SoC-internal **PL011 UART0**, not the
mini UART. The mini UART (`uart1`) is the 40-pin header GPIO14/15 console.
`bcm2711-rpi-4-b.dts` reflects this: `uart0` contains a `bluetooth` child node
compatible `brcm,bcm43438-bt`. (Sources:
[raspberrypi/documentation uart.adoc][rpi-uart-doc],
[bcm2711-rpi-4-b.dts (rpi-5.10.y)][rpi-dts-510].)

Internal routing uses **GPIO32 (TXD0) / GPIO33 (RXD0) / GPIO30 (CTS0) / GPIO31
(RTS0)** — pinmuxed inside the SoC, never on the header. RTS/CTS hardware flow
control is mandatory at 3 Mbaud; DT sets `uart-has-rtscts` and `max-speed =
<2000000>`.

GPIO control lines into the combo chip:

- **BT_REG_ON** — main BT regulator enable. Low = held in reset; high =
  released. On Pi 4 this is `&expgpio 0` (a GPIO expander, not a BCM2711 native
  GPIO), as `shutdown-gpios = <&expgpio 0 GPIO_ACTIVE_HIGH>`. WL_REG_ON is the
  matching Wi-Fi enable on the same expander; both should be raised.
- **BT_HOST_WAKE** — chip to host: "I have data". Optional for first bring-up.
- **BT_DEV_WAKE** — host to chip: assert before HCI to defeat deep sleep.

Sequencing per the CYW43455 datasheet: a stable **32.768 kHz LPO clock** must
be present at power-on and at least two LPO ticks must elapse before BT_REG_ON
is raised (typically ~700 us settling). (Source:
[Infineon CYW43455 datasheet][cyw43455-ds].)

Vendor-specific HCI baud-rate switch (post-patch):

1. Open UART at the reset baud (115200 8N1 RTS/CTS).
2. Send VSC `0xfc18` `UPDATE_BAUDRATE` with the new rate as little-endian u32
   (preceded by two reserved bytes). The chip ACKs at 115200, then both sides
   switch.
3. For rates >3 Mbps, send `0xfc45` first to set the UART clock source.

(Constants from `hci_bcm.c`/`btbcm.c`.) (Source: [hci_bcm.c][hci-bcm].)

## 2. Linux driver layout

Three files, clean separation:

- **`hci_uart.c`** — legacy line-discipline transport. Userspace (`btattach`,
  `hciattach`) opens the UART and ioctls `TIOCSETD` to bind `N_HCI`. Generic
  H4/H5/3-wire framing lives here. ([Kconfig: BT_HCIUART][bt-kconfig])
- **`hci_serdev.c`** — modern serdev-bus replacement. UART is a `serdev_device`
  in DT; the kernel binds a child driver directly. No userspace daemon. Exposes
  `hci_uart_register_device` for vendor drivers. ([hci_serdev.c][hci-serdev])
- **`hci_bcm.c`** — Broadcom protocol/probe. Registers as both a serdev driver
  (DT path) and an `hci_uart_proto` (line-discipline path). Owns the GPIOs
  (`device-wakeup-gpios`, `host-wakeup-gpios`, `shutdown-gpios`), regulators,
  runtime-PM, and the vendor sequence (`bcm_setup`, `bcm_set_baudrate`,
  `bcm_gpio_set_power`). Delegates firmware patching to `btbcm.c`. (Source:
  [hci_bcm.c][hci-bcm].)

Serdev makes "GPIO + UART = single hci device" coherent: DT declares the
bluetooth node as a child of the uart node, hci_bcm grabs GPIOs by name and the
UART via the parent serdev, then calls `hci_uart_register_device` for a single
`hci0` with no userspace help.

## 3. Firmware blob — `BCM43455.hcd`

Format: a flat concatenation of HCI commands in H4 layout (opcode + length +
payload), tightly packed, no headers or checksums. Most are vendor-specific
`WRITE_RAM` (`0xfc4c`) streaming patch content into SRAM; a few are config
writes; the file ends with `LAUNCH_RAM` (`0xfc4e`) to jump into the patched
ROM. The patch lives in volatile RAM and must be reloaded after every
BT_REG_ON power cycle. (Sources:
[naehrdine: Unpatching the unpatchable][naehrdine],
[InternalBlue, arxiv 1905.00631][internalblue].)

Linux looks under `/lib/firmware/brcm/`. `btbcm_initialize()` builds a
candidate filename list from the controller's LMP subversion and hw_name
(queried via `READ_LOCAL_VERSION` and a Broadcom VSC), trying patterns like
`brcm/BCM<hw_name>.<board>.hcd` and `brcm/BCM<hw_name>.hcd`, and uses the first
that loads. For a Pi 4 the hit is typically `brcm/BCM4345C0.hcd`. (Sources:
[btbcm patchram patch (de Goede)][btbcm-multi-fw],
[broadcom-bt-firmware][bt-firmware-repo].)

## 4. Init sequence (cite hci_bcm.c)

```
host                                 BCM43455
 |  GPIO BT_REG_ON = HIGH               |
 |  (≥2 LPO ticks, ~700us)              |
 |  open UART at 115200 8N1 RTSCTS      |
 |                                      |
 |  HCI_RESET (0x0c03) ---------------->|
 |  <----------- Command Complete       |
 |                                      |
 |  VSC DOWNLOAD_MINIDRIVER (0xfc2e) -->|
 |  <----------- Command Complete       |
 |  (chip enters Patch RAM mode)        |
 |                                      |
 |  for each chunk in BCM4345C0.hcd:    |
 |    WRITE_RAM (0xfc4c) ------------->|
 |    <--------- Command Complete       |
 |                                      |
 |  LAUNCH_RAM (0xfc4e) -------------->|
 |  <----------- Command Complete       |
 |  (chip resets into patched firmware) |
 |                                      |
 |  HCI_RESET ------------------------->|
 |  VSC UPDATE_BAUDRATE (0xfc18) ----->|
 |  switch UART to 3 Mbaud              |
 |  (optional VSC 0xfc45 first if >3M)  |
 |                                      |
 |  controller is operational           |
```

In the kernel: `bcm_open` (regulators, GPIO power-up, register serdev),
`btbcm_initialize` (READ_LOCAL_VERSION, filename selection, patchram loop,
post-patch reset), `bcm_set_baudrate` (the VSC), `bcm_setup` (PCM, sleep mode
VSC `0xfc27`, autosuspend). (Source: [hci_bcm.c][hci-bcm].)

A reference userspace impl is **`brcm_patchram_plus`** — opens the tty, sends
RESET, DOWNLOAD_MINIDRIVER, streams the .hcd, switches baud, then attaches the
line discipline. Useful as a small single-file porting reference. (Source:
[brcm-patchram-plus][brcm-patchram-plus].)

## 5. Phoenix-RTOS path to "working"

**Tier 0 — GPIO + UART transport.** Bring up `expgpio` (its own driver on the
BCM2711 I2C/MMIO expander); raise BT_REG_ON. Bring up PL011 `uart0` at 115200
8N1 RTS/CTS, alt-fn pinmux for GPIO30..33. Loopback test: send `HCI_RESET` H4
packet `01 03 0c 00` and parse the 0x0e Command Complete. Deps: existing PL011
driver, pinmux config, expander driver. No firmware needed — unpatched ROM
responds to RESET.

**Tier 1 — H4 framing + HCI command/event plumbing.** ~200 LoC: a byte sink
demuxing packet-type prefixes (0x01 cmd, 0x02 ACL, 0x04 evt) and a "send HCI
command, await Command Complete with matching opcode" helper. No external deps.

**Tier 2 — Patch RAM (.hcd) loader + baud switch.** Embed `BCM4345C0.hcd` as a
linker blob (or tftp during dev). Implement §4. Port `btbcm_patchram()` and
`bcm_set_baudrate()` — both ~50 lines, no kernel-isms beyond `__hci_cmd_sync()`
(replace). After this tier the chip is a usable HCI controller for any stack.

**Tier 3 — Minimal LE host.** See §6.

## 6. Phoenix has no BlueZ — realistic options

BlueZ is GPL/Linux-specific. Three pragmatic paths:

1. **Defer.** Ship Tier 0–2 only. Expose `/dev/hci0` as a raw H4 byte device;
   let an external host (x86 box) run the stack. Smallest delivery; useful for
   radio bring-up CI.
2. **Port a minimal LE stack.** Two candidates:
   - **Zephyr `subsys/bluetooth/host`** — clean layering, Apache-2.0, ~30k LoC
     total but ~5k for the LE-only happy path. ([Zephyr HCI UART][zephyr-hciuart])
   - **Apache Mynewt NimBLE** — Apache-2.0, more self-contained, designed to be
     RTOS-agnostic. Probably the best fit.
3. **Hand-roll GAP+L2CAP.** Only viable for "one fixed peer, pre-paired". ~1500
   LoC; quirks will bite. Not recommended unless scope is very tight (e.g. an
   iBeacon advertiser).

## 7. Known quirks

- **WL_REG_ON before BT_REG_ON.** The combo PMU shares a sequencer; some FWs
  misbehave if BT_REG_ON is raised while WL_REG_ON is low. Raise both.
  ([bluez-firmware issue 13][bluez-fw-issue])
- **Coexistence parameter required from FW r177+.** Newer .hcd files expect a
  vendor coex tuning command post-patch; without it, BT stutters when Wi-Fi is
  active. ([LibreELEC PR #13][libreelec-pr13])
- **Hardware flow control is not optional** above ~921600. RTS/CTS must be
  muxed (GPIO30/31); without it the chip drops bytes during patchram streaming
  and LAUNCH_RAM never completes.
- **Baud-rate race on UPDATE_BAUDRATE.** The chip ACKs at the old rate then
  switches; drain host TX, sleep ~1 ms, then reconfigure. `bcm_set_baudrate`
  does this via serdev.
- **`expgpio` is on a separate I2C bus**, not BCM2711 GPIO0..53. There's no
  wire on the SoC native GPIO MMIO for BT_REG_ON.
- **Soft reset after patchram is mandatory.** LAUNCH_RAM resets the controller;
  any HCI sent before the post-launch RESET is silently dropped.

## 8. Open questions

- Exact `expgpio` register layout for Phoenix's I2C driver — schematic
  cross-check against the Pi 4 reduced schematic and whatever VC-firmware
  helper sets it up before kernel handoff.
- Which `.hcd` matches our board revision (BCM4345C0 vs BCM4345C5)?
  `READ_LOCAL_VERSION` on a known-good Pi 4 will answer.
- Whether Phoenix's UART driver exposes a `tcdrain`-equivalent flush, needed
  for the baud switch.
- License posture for redistributing `BCM4345C0.hcd` (Cypress EULA via
  `linux-firmware` terms) — verify before bundling vs tftp-loading.
- Whether to skip Tier 3 and ship Tier 2 + an x86 host-side harness as the
  "BT works" milestone.

[rpi-uart-doc]: https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/configuration/uart.adoc
[rpi-dts-510]: https://github.com/raspberrypi/linux/blob/rpi-5.10.y/arch/arm/boot/dts/bcm2711-rpi-4-b.dts
[hci-bcm]: https://github.com/torvalds/linux/blob/master/drivers/bluetooth/hci_bcm.c
[hci-serdev]: https://github.com/torvalds/linux/blob/master/drivers/bluetooth/hci_serdev.c
[bt-kconfig]: https://github.com/torvalds/linux/blob/master/drivers/bluetooth/Kconfig
[btbcm-multi-fw]: https://patchwork.kernel.org/project/bluetooth/patch/20200417171532.448053-7-hdegoede@redhat.com/
[bt-firmware-repo]: https://github.com/winterheart/broadcom-bt-firmware
[naehrdine]: https://naehrdine.blogspot.com/2021/01/broadcom-bluetooth-unpatching.html
[internalblue]: https://arxiv.org/pdf/1905.00631
[brcm-patchram-plus]: https://github.com/jschen-cse/brcm_patchram_plus
[cyw43455-ds]: https://www.infineon.com/assets/row/public/documents/30/49/infineon-cyw43455-datasheet-en.pdf?fileId=8ac78c8c7d0d8da4017d0ee226686889
[zephyr-hciuart]: https://docs.zephyrproject.org/latest/samples/bluetooth/hci_uart/README.html
[bluez-fw-issue]: https://github.com/RPi-Distro/bluez-firmware/issues/13
[libreelec-pr13]: https://github.com/LibreELEC/brcmfmac_sdio-firmware-rpi/pull/13
