# BCM43455 Bluetooth on Pi 4 — non-Linux research (Round 2)

Round 1 surveyed Linux's `hci_bcm` / BlueZ path. Round 2 deliberately excludes Linux and looks at every other primary source we can lean on for a Phoenix-RTOS port. Where Linux is mentioned below, it's only because the citation lives in the Raspberry Pi kernel device tree (the schematic-of-record).

## 1. BTstack (BlueKitchen GmbH)

BTstack is the leading non-Linux embedded Bluetooth stack: dual-mode (Classic + LE), portable to bare metal and most RTOSes, with explicit support for several Broadcom controllers. License is "free for non-commercial use" with paid commercial licensing — a real gating concern for a publicly-released RTOS port, but legally usable for development.

The repository ships ~40 ports under `port/` ([github.com/bluekitchen/btstack/tree/master/port](https://github.com/bluekitchen/btstack/tree/master/port)). The relevant one is `port/raspi`. Per its README, the port officially targets **Pi 3 B/B+, Pi 3 A/A+, and Pi Zero W** — there is **no Pi 4 entry**, but the architecture is identical: BCM4345C0 (Pi 3B+/Zero 2 W) is essentially the same family as BCM43455 (Pi 4), and the upload path is the same `bcm` chipset module. The port handles three transport variants:

- Pi 3 A/B (no flow control): H5 at 921 600 baud
- Pi 3 A+/B+ (flow control): H4 at 921 600 baud
- Pi Zero W: H4 at 921 600 baud

It also documents the GPIO sequencing for `BT_REG_EN` (called `BT_ON` in our schematic): GPIO 128 / 129 / 45 depending on model. The port walks through the firmware-only-on-cold-boot optimisation: when flow control is wired the controller keeps state across resets, so the .hcd is uploaded once.

The `chipset/bcm/` folder ([github.com/bluekitchen/btstack/tree/master/chipset/bcm](https://github.com/bluekitchen/btstack/tree/master/chipset/bcm)) contains:
- `btstack_chipset_bcm.c/h` — the chipset adapter
- `btstack_chipset_bcm_download_firmware.c/h` — the .hcd uploader
- `convert_hcd.py` — converts a `.hcd` blob into a C array for embedded targets that lack a filesystem

The uploader uses `HCI Reset` (`0x03 0x0c`) and `Read Local Version` (`0x01 0x10`) to identify the chip, then iterates the `.hcd` records via `chipset->next_command()`. The opcode constants live in the chipset adapter rather than the download module. The protocol itself — `Download_Minidriver` (`0xFC2E`), `Write_RAM` (`0xFC4C`), `Launch_RAM` (`0xFC4E`) — is documented externally (see §8 below) and matches what InternalBlue and broadcom-bt-firmware decode.

For Phoenix this is the most realistic candidate: it already speaks H4, it already speaks Broadcom Patch RAM, it already has a Pi-family port that bring-up engineers test against real BCM4345-class silicon, and the `raspi/` port is small enough (~5 source files plus README) to fork into a `phoenix-rpi4` sibling.

## 2. Zephyr Bluetooth

Zephyr's BT subsystem is split between `subsys/bluetooth/host/` (host stack — GAP, GATT, SMP, L2CAP) and `drivers/bluetooth/hci/` (transport adapters). The HCI driver list ([github.com/zephyrproject-rtos/zephyr/tree/main/drivers/bluetooth/hci](https://github.com/zephyrproject-rtos/zephyr/tree/main/drivers/bluetooth/hci)) includes `h4.c`, `h5.c`, `spi.c`, `userchan.c`, plus vendor blobs for NXP, ST, Silabs, Infineon (Cypress), and ESP32. There is **no Broadcom-specific HCI driver** — Zephyr expects the controller to be Bluetooth-spec-compliant and pushes vendor patching up into application code. That makes Zephyr's H4 driver a reasonable reference for transport framing but useless for the .hcd upload step. The host stack itself is BLE-first (Classic support is feature-gated and significantly less mature) and the licence is Apache-2.0, which is friendlier than BTstack's.

## 3. MyNewt nimBLE

Apache nimBLE ([github.com/apache/mynewt-nimble](https://github.com/apache/mynewt-nimble)) is the mature open BLE stack — both host and controller, Bluetooth 5.4. License is Apache 2.0. **It is BLE-only**: no BR/EDR, no RFCOMM, no SDP. It targets Nordic nRF51/52/5340 and Renesas DA1469x as integrated controller-plus-host on the same MCU. It can also run host-only over UART HCI to an external controller via the `blehci` companion app, but there's no built-in support for Broadcom .hcd patching. For Phoenix on a BCM43455 that means nimBLE is host-side viable but we'd still have to author the BCM init script ourselves. And we'd lose Classic BT entirely (HID over Classic, A2DP, RFCOMM serial bridges all out of scope).

## 4. FreeBSD netgraph BT

FreeBSD's `sys/netgraph/bluetooth/` ([github.com/freebsd/freebsd-src/tree/main/sys/netgraph/bluetooth](https://github.com/freebsd/freebsd-src/tree/main/sys/netgraph/bluetooth)) is a complete classic BT stack built on the netgraph framework: HCI, L2CAP, RFCOMM, sockets, and drivers (USB/UART). It is **Classic BT only — no LE**, last meaningfully extended in the 2000s. License is BSD-2-Clause. Architecturally interesting (clean transport/protocol/socket separation; we could mirror the layering) but stale; the codebase has no concept of LL Privacy, extended advertising, or any post-4.0 feature. Not a runtime candidate, but a useful reference for how to lay out classic-only kernel modules.

## 5. NetBSD netbt

NetBSD's `sys/netbt/` ([github.com/NetBSD/src/tree/trunk/sys/netbt](https://github.com/NetBSD/src/tree/trunk/sys/netbt)) is a similar-vintage Classic BT stack (HCI, L2CAP, RFCOMM, SCO) plus an `hci_le.h` header indicating LE-aware HCI parsing — but no full LE host. Drivers under `sys/dev/bluetooth/` cover bthub/btuart-style generic transports, not Broadcom specifically. Same era, same limitations as FreeBSD.

## 6. Bluedroid / Fluoride (Android)

Android's `system/bt/` (bluedroid → fluoride → "Gabeldorsche") was originally seeded from Broadcom's reference stack and is therefore the closest open mirror of the BCM vendor command catalogue. The vendor library interface (`bt_vendor.h`) documents how an OEM HAL signals firmware-config, baud-rate-change, and chip-init phases — that contract was lifted directly from what the BCM4329/4334/4345 family expects. License Apache 2.0. The downside: it's enormous, deeply Android-coupled (binder, JNI, AOSP build), and not portable to a small RTOS as-is. Cite it for vendor-command semantics and SCO routing assumptions, not as a port candidate.

## 7. Pi 4 bare-metal BT

`rsta2/circle` ([github.com/rsta2/circle](https://github.com/rsta2/circle)) — the most polished bare-metal C++ environment for Pi — explicitly lists Bluetooth under **Not supported** in its features table. That alone tells us nobody in the bare-metal Pi community has done the legwork; if Circle (which has full USB, GPU, audio) hasn't shipped BT, nobody else has either. `dwelch67/raspberrypi`, `LdB-ECM/Raspberry-Pi`, and rpi4-osdev tutorials similarly skip BT. There is no public bare-metal BCM43455 BT bring-up to copy from. This means whoever ports BTstack to Phoenix's Pi 4 is doing first-of-its-kind work on the Pi 4 specifically, which is a useful framing for time estimates.

## 8. The .hcd Patch RAM format

The `.hcd` ("Host Controller Download") format is Broadcom proprietary. Each record is `[opcode:2][length:1][payload:length]` where payload for a `Write_RAM` carries `[address:4][bytes...]`. The loader sequence per BTstack and InternalBlue ([github.com/seemoo-lab/internalblue](https://github.com/seemoo-lab/internalblue)) is:

1. `HCI_Reset` (0x0C03)
2. `Download_Minidriver` (0xFC2E) — controller jumps into the bootloader stub
3. Loop of `Write_RAM` (0xFC4C) records from the .hcd
4. `Launch_RAM` (0xFC4E) with the entry address — controller restarts running patched firmware
5. ~50 ms settle, then UART rebaud + final `HCI_Reset`

InternalBlue's notes confirm chipset-specific quirks (e.g. "`Launch_RAM` crashes if another HCI command arrives within 6 s on BCM4358A3"). For Phoenix we can copy BTstack's record walker verbatim and feed it the Pi 4 firmware blob (`BCM4345C0.hcd` from `linux-firmware/brcm/`).

## 9. InternalBlue

InternalBlue ([github.com/seemoo-lab/internalblue](https://github.com/seemoo-lab/internalblue)) is a Bluetooth firmware research framework for Broadcom and Cypress chips — MIT licensed, primarily Python, runs over an HCI socket. It exposes `read_memory`, `write_memory`, `launch_ram`, instruction-level patching, and reverse-engineered firmware symbol tables. For our purposes it's two things: (a) a working reference implementation of every vendor opcode we'll need, in idiomatic Python that's easy to read; (b) a debugging tool we can point at the BCM43455 once we have basic HCI working, to verify our patches landed.

## 10. GPIO BT_REG_ON / BT_HOST_WAKE / BT_DEV_WAKE on Pi 4

The Pi 4 bluetooth control GPIO is **not directly on the SoC** — it sits on the `expgpio` GPIO expander (a 74AHC595-style shift register driven by firmware). From `bcm2711-rpi-4-b.dts` ([github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts)):

```
&bt { shutdown-gpios = <&expgpio 0 GPIO_ACTIVE_HIGH>; };
gpio-line-names = "BT_ON", "WL_ON", ...;
```

So `BT_REG_ON` is `expgpio[0]`. There is **no** dedicated `BT_HOST_WAKE` / `BT_DEV_WAKE` line wired to the SoC on Pi 4 — wake routing is handled in-band over the UART (eHCILL on H4 with flow control). Phoenix needs to drive the firmware mailbox to control `expgpio[0]`, since there's no MMIO path to that pin.

## 11. UART routing on Pi 4

UART0 (PL011, the "real" UART) is wired to BT on GPIO 32/33 (TX/RX) plus GPIO 30/31 (CTS/RTS). UART1 (mini-UART) is the default debug console on GPIO 14/15. Three relevant `config.txt` knobs:

- `dtoverlay=disable-bt` — frees UART0 for general use, leaves BT silicon powered down. **This is what the Phoenix bring-up has used so far.**
- `dtoverlay=miniuart-bt` — swaps: BT keeps UART0, debug console moves to mini-UART. Mini-UART baud tracks core clock so `core_freq=250` is required.
- `enable_uart=1` — enables the chosen debug UART.

For BT enablement on Phoenix we'd remove `disable-bt`, add `miniuart-bt` (or accept losing the debug console), and own UART0 in the kernel.

## 12. Synthesis — recommended path

Three options, ranked:

**A. Port BTstack `port/raspi` to Phoenix (recommended).** This is the only candidate that already (i) handles Broadcom .hcd upload, (ii) speaks H4 with flow control, (iii) has been validated on a Pi-family BCM4345-class part. Effort: write a Phoenix UART HAL adapter implementing BTstack's `btstack_uart_block_t` interface, plus a one-shot expgpio mailbox call to assert `BT_ON`. The BTstack non-commercial licence is a real concern for the public release; we should reach out to BlueKitchen early about licensing, or pick option B.

**B. Build a thin host on top of nimBLE (BLE-only Phoenix).** Apache 2.0, clean code, but we'd write the BCM .hcd uploader ourselves (porting BTstack's `chipset/bcm/` is plausible — the uploader and the host stack are decoupled in BTstack precisely so you can do this). Loses all Classic BT functionality.

**C. Defer.** BT is rarely on the critical path for an RTOS bring-up demo. Wi-Fi via SDIO + brcmfmac-equivalent is far more user-visible. Recommendation: defer until kernel has SDIO + Wi-Fi working, then revisit. Option A becomes much easier once the firmware-loading scaffolding (file IO, GPIO mailbox) already exists for Wi-Fi.

For the current Pi 4 bring-up plan: **defer BT, but pick BTstack as the eventual target and avoid any architecture choice that would foreclose it** (specifically, keep UART0 ownership flexible in the kernel, and make sure the firmware-mailbox driver for Wi-Fi exposes generic `expgpio` set/get rather than Wi-Fi-specific helpers).

Sources:
- [BTstack repository](https://github.com/bluekitchen/btstack)
- [BTstack ports list](https://github.com/bluekitchen/btstack/tree/master/port)
- [BTstack BCM chipset adapter](https://github.com/bluekitchen/btstack/tree/master/chipset/bcm)
- [Zephyr HCI drivers](https://github.com/zephyrproject-rtos/zephyr/tree/main/drivers/bluetooth/hci)
- [Apache MyNewt nimBLE](https://github.com/apache/mynewt-nimble)
- [FreeBSD netgraph bluetooth](https://github.com/freebsd/freebsd-src/tree/main/sys/netgraph/bluetooth)
- [NetBSD netbt](https://github.com/NetBSD/src/tree/trunk/sys/netbt)
- [InternalBlue](https://github.com/seemoo-lab/internalblue)
- [Circle bare-metal Pi (no BT)](https://github.com/rsta2/circle)
- [bcm2711-rpi-4-b.dts](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts)
- [BCM43455 NVRAM for Pi 4](https://github.com/openwrt/cypress-nvram/blob/master/brcmfmac43455-sdio.raspberrypi,4-model-b.txt)
- [Pi 4 BT serdev probe issue](https://github.com/raspberrypi/linux/issues/3260)
