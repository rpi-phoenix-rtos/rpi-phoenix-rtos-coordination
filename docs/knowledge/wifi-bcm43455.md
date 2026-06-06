# Forward research: Pi 4 WiFi (CYW43455 / BCM43455 over SDIO)

Status: forward-research only. No code in tree depends on this yet. Goal: scope the work
required to bring Wi-Fi up on Phoenix-RTOS once the kernel hosts a network stack.

## 1. Hardware overview

The Pi 4B's onboard radio is the **Cypress / Infineon CYW43455** (originally Broadcom
BCM43455). Single-chip 1x1 dual-band 802.11ac (Wi-Fi 5) MAC + baseband + radio with
integrated Bluetooth 5. WLAN supports MCS0-MCS9 (up to 256-QAM) in 20/40/80 MHz channels,
~433 Mbps PHY on 5 GHz. SDIO v3.0 for WLAN, separate UART for BT HCI; the Pi uses both.

On BCM2711 the WLAN chip is wired to one of three SD host controllers - **not** the slot
used for the boot SD card. The Pi 4 has `emmc2` (new SDHCI-iproc, boot SD), the legacy
Arasan SDHCI (`mmcnr`, used for WLAN), and the Broadcom `sdhost` (GPIO22-27 alt-funcs;
cannot drive SDIO). The WLAN controller has a different MMIO base from the boot SD
controller; it cannot be driven by reusing the SD-card driver.

Sources: [Infineon CYW43455 datasheet](https://www.infineon.com/assets/row/public/documents/30/49/infineon-cyw43455-datasheet-en.pdf?fileId=8ac78c8c7d0d8da4017d0ee226686889),
[BCM2711 ARM Peripherals](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf),
[Pi 4 WiFi-chip-interface forum thread](https://forums.raspberrypi.com/viewtopic.php?t=327930).

## 2. Linux driver: brcmfmac

The reference open-source driver is `drivers/net/wireless/broadcom/brcm80211/brcmfmac/` in
mainline Linux. It is a **FullMAC** driver (the 802.11 MAC runs on the chip's ARM core in
firmware), in contrast to its sibling `brcmsmac`, which is SoftMAC. Key files:

- `sdio.c` - bus glue: enumerates the SDIO function, talks CMD52/CMD53, performs the firmware
  download, registers RX/TX callbacks. ~4 kLoC.
- `bcmsdh.c` - low-level SDIO helpers (32-bit register reads/writes, watermark setup).
- `chip.c` / `chip.h` - per-chip backplane setup, AXI/SOCRAM bring-up, ARM core reset.
- `firmware.c` - firmware/NVRAM file lookup and load orchestration. Tries device-specific
  paths (e.g. `brcmfmac43455-sdio.raspberrypi,4-model-b.txt`) before falling back to generic.
- `feature.c` - runtime feature discovery: queries the firmware via "iovars" to see which
  capabilities (SAE, P2P, MBSS, MFP, WoWLAN, ...) the loaded firmware actually supports, sets
  flags consumed by the cfg80211 layer.
- `cfg80211.c` - the big one (~8 kLoC). Translates Linux's `cfg80211_ops` (scan, connect,
  disconnect, set_key, change_bss, ...) into firmware iovars. Vendor-command paths live here
  too. This is where wpa_supplicant ultimately lands: nl80211 -> cfg80211 -> brcmf_cfg80211.
- `core.c`, `bcdc.c`, `fwsignal.c` - the BCDC framing layer that wraps every command and data
  frame between host and firmware.

Because the chip is FullMAC, the driver does **not** register a `mac80211` `ieee80211_hw`. It
registers a `wiphy` directly with cfg80211 and exposes plain network devices. Encryption,
beaconing, rate adaptation, A-MPDU and most of management-frame processing all run on the
chip.

Sources: [Linux Wireless brcm80211 docs](https://wireless.docs.kernel.org/en/latest/en/users/drivers/brcm80211.html),
[brcmfmac sdio.c](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c),
[brcmfmac cfg80211.c](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c).

## 3. Firmware blob and NVRAM

The chip boots with no firmware - the host must upload one over SDIO every time. Two files
are needed:

- `brcmfmac43455-sdio.bin` - the firmware image, several hundred KB, downloaded into chip
  SOCRAM.
- `brcmfmac43455-sdio.txt` (NVRAM/calibration) - small text key=value file with PA gains,
  antenna config, MAC address slot, regulatory hints.

On Linux these live in `/lib/firmware/brcm/`. The Pi-flavoured NVRAM (with the model-specific
calibration the foundation ships) lives in
[RPi-Distro/firmware-nonfree](https://github.com/RPi-Distro/firmware-nonfree/tree/buster/brcm).
Mainline `linux-firmware` carries the generic `brcmfmac43455-sdio.bin` and per-board NVRAM
files keyed off the DT compatible string (e.g.
[`brcmfmac43455-sdio.raspberrypi,4-model-b.txt`](https://github.com/endlessm/linux-firmware/blob/master/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt)).
Phoenix will need to redistribute both files alongside the kernel; license is non-free
binary-redistributable.

## 4. SDIO init sequence

`brcmf_sdio_probe()` and friends in `sdio.c` walk roughly this sequence:

1. **SDIO function 1 enable** via CMD52 to CCCR `IO Enable` register; wait for `IO Ready`.
2. **Chip ID read** - CMD53 burst read of the chip-common backplane window; `chip.c` matches
   the ID (0x4345 rev 6 for CYW43455) and selects the firmware filename.
3. **Backplane setup** - bring up the ARM core, enable SOCRAM, configure AXI windows.
4. **Firmware download** - CMD53 block writes stream the .bin into SOCRAM, then a
   trailer/handshake (`brcmf_sdio_download_code_file`, `brcmf_sdio_download_nvram`).
5. **NVRAM upload** - parsed key=value text gets binary-packed and written to SOCRAM tail.
6. **ARM release** - clear reset on the chip's ARM, poll the "ready" mailbox.
7. **BCDC handshake** - exchange protocol version, query firmware version, run feature
   detection iovars (`feature.c`).
8. **Net registration** - register the wiphy with cfg80211 and create `wlan0`. Userspace
   (wpa_supplicant) takes over from here.

Sources: [brcmfmac sdio.c](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c),
[Zerowi bare-metal walkthrough](https://iosoft.blog/2020/03/08/zerowi-part1/) (a useful
reverse-engineered description of the same init dance).

## 5. Phoenix-RTOS path to "working"

A staged plan that produces something testable at each step:

- **Tier 0 - SDIO host + chip detect.** Port a minimal SDHCI driver targeting the Pi 4 WLAN
  controller (NOT the boot SD controller; it's a separate instance with its own MMIO base,
  IRQ, and clock). Implement CMD52 and CMD53 (single-block + multi-block, 32-bit register
  access only on BCM2711 emmc/Arasan). Bring SDIO function 1 up and read the chip ID.
  Deliverable: a kernel log line `BCM43455 rev 6 detected on mmcnr`.
- **Tier 1 - firmware upload.** Add the SOCRAM/backplane window code from `chip.c` and the
  download loop from `sdio.c`. Embed the .bin and .txt in the rootfs. Deliverable: the chip's
  ARM comes out of reset and answers BCDC version queries.
- **Tier 2 - brcmfmac-bus minimum.** Port BCDC framing, the iovar machinery, and enough of
  the `core.c`/`fwsignal.c` data path to send/receive raw 802.11 frames via the firmware.
  Deliverable: `iovar` get/set works; firmware reports MAC address; passive scan returns
  beacons.
- **Tier 3 - open-AP association.** Port the connect/disconnect/scan handlers from
  `cfg80211.c`, but only the open-network path. Provide a tiny CLI that calls the equivalent
  iovars directly. Deliverable: associate with an open SSID, receive a DHCP lease via
  Phoenix's lwIP.
- **Tier 4 - WPA2/WPA3 + DHCP.** Wire up SAE/PSK paths. CYW43455 firmware can offload SAE,
  but only on recent Pi firmware and only if the right feature flag is set; WPA2-PSK works
  with simpler 4-way handshake offload. Phoenix already has lwIP for v4/v6, so DHCP and
  routing fall out for free once `wlan0` carries packets.

Each tier has a clear test target on real hardware (the build/log loop in
`scripts/rebuild-rpi4b-fast.sh` already supports it).

## 6. Phoenix has no mac80211/cfg80211

This is the elephant in the room. Three realistic options:

(a) **Port a subset of cfg80211.** Because brcmfmac is FullMAC only the cfg80211 thin layer
    is needed - not mac80211. Surface is small: scan, connect, disconnect, set_key,
    get_station, plus nl80211-equivalent IPC for a future supplicant. Est 1-3 kLoC.
    Pro: minimises diff against upstream. Con: still need a supplicant.
(b) **Talk to firmware directly via iovars; skip cfg80211.** The firmware can do 4-way
    handshake and SAE if the right iovars are set, so a tiny "connect to SSID with PSK"
    RPC is sufficient. Pro: low hundreds of lines once Tier 1-2 is in. Con: no clean
    supplicant path; every extra feature is one-off.
(c) **Different stack** (ESP-IDF, NuttX, Zephyr). None matches brcmfmac's architecture;
    rewrite from scratch. Not recommended.

Recommended: start with (b) for Tier 3 (proves the driver works), evolve toward (a) before
Tier 4 if Phoenix gains a supplicant port (wpa_supplicant builds against a small POSIX
surface and could be the natural fit).

Sources: [Cypress WHD discussion](https://community.infineon.com/t5/AIROC-Wi-Fi-and-Wi-Fi-Bluetooth/SDIO-WiFi-Driver-for-Murata-Type-1MW-Cypress-CYW43455-for-STM32F4/td-p/268121),
[Infineon WHD repo](https://github.com/Infineon/wifi-host-driver) - WHD is essentially
option (b) productised for RTOS targets and is worth reading even if not used directly.

## 7. Known quirks (Pi-specific)

- DT compatible: `brcm,bcm4329-fmac` under the WLAN SDHCI node. Optional properties used by
  the Pi: `interrupt-parent`/`interrupts`/`interrupt-names = "host-wake"` for OOB IRQ, and
  `brcm,ccode-map` for regulatory translation. Phoenix's DT consumer needs to recognise the
  compatible and produce a probe event for the brcmfmac glue. See the
  [brcm,bcm4329-fmac binding](https://www.kernel.org/doc/Documentation/devicetree/bindings/net/wireless/brcm,bcm4329-fmac.yaml).
- Pi 4 ships its **own** NVRAM (`brcmfmac43455-sdio.raspberrypi,4-model-b.txt`) distinct from
  the generic Cypress one - antenna and PA calibration differ. Using the wrong one yields
  marginal RF (visible as low TX power, frequent disassoc).
- BCM2711 quirk: 32-bit-only register access on the emmc/SDHCI controllers. Any
  `readb`/`readw`/`writeb`/`writew` on the SDHCI block is a hardware fault; SDHCI library
  code must be the BCM-quirks variant (mainline `sdhci-iproc.c` style).
- `wpa_supplicant >= 2.11` exposes a known regression with brcmfmac SAE offload that is
  papered over with `brcmfmac.feature_disable=0x82000` on Linux. Worth knowing if Phoenix
  ever points wpa_supplicant at the chip.

Sources: [brcm,bcm4329-fmac binding](https://www.kernel.org/doc/Documentation/devicetree/bindings/net/wireless/brcm,bcm4329-fmac.yaml),
[bcm2711-rpi-4-b.dts](https://github.com/RobertCNelson/device-tree-rebasing/blob/master/src/arm/bcm2711-rpi-4-b.dts),
[Red Hat brcmfmac+wpa_supplicant bug](https://bugzilla.redhat.com/show_bug.cgi?id=2302577).

## 8. Open questions

- Which SDHCI variant does the Pi 4 use for WLAN exactly - is it the legacy Arasan core or a
  second emmc2-style instance? Confirm against BCM2711 datasheet section on MMC/SD before
  picking a host driver to port.
- Does the WLAN controller share a clock domain with the boot SD controller? If yes, our
  current SD bring-up sequence may already park it in a bad state.
- Phoenix-RTOS networking model: lwIP is in-tree but how raw frames enter it from a driver
  is unclear - is there a netif-style API, or do drivers post buffers to a server port?
- Bluetooth: same chip, separate UART, separate firmware patch (`BCM4345C0.hcd`). Out of
  scope here but lives in the same hardware block - revisit when scoping BT.
- Licensing: `firmware-nonfree` is non-free binary-redistributable. Confirm the downstream
  distribution terms are compatible with whatever Phoenix ships.

---

Word count target: 800-1500. This document is forward research only and does not change any
code or build state.
