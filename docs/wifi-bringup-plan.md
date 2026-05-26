# WiFi (BCM43455) Bring-Up Plan for Phoenix-RTOS on Pi 4

> **Related**: see `docs/bluetooth-bringup-plan.md` for the BT side of the
> same combo chip. WiFi lands first (higher user-visible value + the
> shared `expgpio` / mailbox / firmware-blob plumbing carries over to BT
> for free).


**Status**: scout / planning (not yet started).
**Owner**: open (task #36).
**Estimated effort**: 6–8 focused iterations spanning multiple sessions.
**Unblocked since**: 2026-05-25 (Ethernet Tier 5c + SMP Phase E landed; no
remaining hard prerequisites).

## Scope summary

Bring the on-board WiFi chip on Raspberry Pi 4 (Cypress / Infineon
BCM43455) up to "associate with an AP, get DHCP, ping over WiFi" under
Phoenix-RTOS. Goal is a second `netif` alongside the GENET Ethernet —
the lwip-port supports `LWIP_WIFI` and already has the Cypress WHD
driver + HAL skeleton in tree.

## What's already in tree

Under `sources/phoenix-rtos-lwip/wi-fi/`:

- **`whd/`** — Cypress Wi-Fi Host Driver (WHD). Full driver: MAC/PHY
  command queue, CDC/BDC framing, event loop, MIMO/security/scan/AP
  modes. ~50 source files. *Already platform-independent.*
- **`hal/`** — Cypress HAL stubs (GPIO, SDIO, USB, SPI, RTOS abs).
  `cyhal_sdio.c` is currently bound to the **i.MX RT USDHC2** —
  uses `USDHC2_ADDR`, `pctl_clk_usdhc2`. Must be split per-platform
  or rewritten for BCM2711.
- **`lwip/`** — lwip↔WHD glue: `cy_network_buffer_lwip.c`,
  `cy_lwip.c`. Reusable.
- **`Makefile.aarch64a72-generic`** — `LWIP_WIFI` build flag exists.
  Currently not enabled for `aarch64a72-generic-rpi4b`.

`include/board_config.h` for the Pi 4 project already exposes
`RPI_MAILBOX_BASE_ADDRESS=0xfe00b880` (used by GENET Tier 5b mailbox
MAC fetch); the same mailbox interface drives GPIO + power tags.

## What's NOT in tree

1. **Pi 4 SDIO host driver.** The Pi 4 has three MMC/SDIO controllers
   in the BCM2711 (confirmed via 2026-05-25 diag-udp 's' scout —
   all three mmap'd successfully and read 0 when idle):
   - **SDHOST** (bcm2835 legacy) at `0xfe202000`. Used for SD card
     on Pi 3; on Pi 4 it can drive the SD card or be unused.
   - **MMC/SDHCI** (Arasan, legacy) at `0xfe300000`. **WiFi BCM43455
     SDIO interface on Pi 4** per Linux DT
     (`arch/arm/boot/dts/bcm2711-rpi-4-b.dts`, the `&mmc` node).
   - **EMMC2** (BCM2711-specific Arasan) at `0xfe340000`. **SD card
     slot on Pi 4** per the `&emmc2` node.
   Target for WiFi bring-up: **MMC/SDHCI at 0xfe300000**. Linux
   reference: `drivers/mmc/host/sdhci-iproc.c` (~600 lines) for
   the SDHCI-compatible Arasan path, plus the standard SDHCI core
   in `drivers/mmc/host/sdhci.c`.

2. **WHD chip ID 43455 support.** `whd_chip_constants.c` enumerates
   the supported chip family — current list: 43012, 43022, 43430,
   43439, 43909, 43907. **43455 must be added.** Upstream WHD
   already supports it; pattern is `case 43455:` rows in each chip-
   constants switch plus a chip-family classifier. Diff size against
   upstream cy-whd: ~30 lines per support case across ~6 switches.

3. **Firmware blobs.** BCM43455 needs:
   - `cyfmac43455-sdio.bin` — radio firmware (~600 KB)
   - `cyfmac43455-sdio.clm_blob` — country/locale tables (~7 KB)
   Source: Infineon's `cy-whd-firmware` repo or
   `raspberrypi/firmware-nonfree` (`brcmfmac43455-sdio.bin` is
   bytewise-identical). Distribution-friendly licensing requires
   Cypress' SLA-text packaged alongside; for Phoenix we put them
   under a `firmware/` directory and mount via dummyfs.

4. **WL_REG_ON GPIO pulse.** Pi 4 boots with BCM43455 power-gated.
   The signal `WL_REG_ON` controls power-up: routed via VideoCore
   to GPIO bank 41 (chip-internal). Driven via mailbox property
   tag `0x00038041` (SET_GPIO_STATE). Power-on sequence:
   `WL_REG_ON=1 → wait 150 ms → WL_HOST_WAKE follows → SDIO ready`.

5. **lwip-port WiFi netif registration.** `port/main.c` has
   `init_wifi()` gated on `LWIP_WIFI` — need to ensure that block
   is fleshed out for the Pi 4 driver (currently the build flag
   isn't even set for the rpi4b target).

## Bring-up tier plan

Matching the pattern that worked for Ethernet (one tier per validated
checkpoint, each ends in a manifest + doc note).

### Tier 0 — environment & blobs

- Fetch `brcmfmac43455-sdio.bin` and `brcmfmac43455-sdio.clm_blob`
  from `raspberrypi/firmware-nonfree`.
- Stage under `manifests/firmware/wifi/` (or similar; outside the
  source repos to keep them clean).
- Decide blob mount path: probably `/lib/firmware/...` via
  dummyfs at boot.
- Output: a scout note recording exact file SHAs and licensing
  pointers.

### Tier 2 — first SDIO command/response (DONE 2026-05-25)

CMD0 (GO_IDLE_STATE) + CMD5 (IO_SEND_OP_COND, arg=0) both complete.
CMD5 returns the BCM43455 OCR:

```
resp[0] = 0x30ffff00
        bit 31    C (Card Ready)         = 0 (first probe with arg=0)
        bits 30:28 NUM_IO_FUNCTIONS      = 3 (F0/F1/F2)
        bit 27    MP (Memory Present)    = 0 (pure SDIO)
        bits 23:0 I/O OCR                = 0xffff00 (2.0V-3.6V)
```

First real SDIO command/response on Phoenix-RTOS/Pi 4. The chip is
addressable and replies correctly via the SDHCI controller. Manifest:
`2026-05-25-wifi-tier2-cmd5-ocr.md`.

Root cause of prior all-zero responses (Tier 2 first cut): SDHCI
command-issue encoding bug. The COMMAND register at offset 0x0E is
the **upper** 16 bits of the 32-bit dword at offset 0x0C, so its
RESPONSE_TYPE bits 1:0 live at dword bits 17:16, not 1:0. The old
code put RESPONSE_TYPE at bits 1:0 (TRANSFER_MODE territory),
producing commands with response-type=0 — the controller correctly
asserted CMD_COMPLETE without sampling the bus. Fix in lwip `868e1f6`.

### Tier 3 — chip enumeration (DONE 2026-05-25)

Standard SDIO bring-up worked first-try via the diag-udp `'e'`
sub-command (lwip `b247d0e`):

```
CMD5(0)    resp=30ffff00   (3 IO funcs, voltage 2.0-3.6V)
CMD5(ocr)  resp=b0ffff00   ready_iters=1  C=1 (chip ready)
CMD3       resp=00010000   RCA=0x0001
CMD7(rca)  resp=00001e00   (select OK, state=CMD)
CMD52(F0r0) resp=00000043  (CCCR rev 0x43 = SDIO 3.00 + CCCR/FBR 1.20)
```

### Tier 4 — CIS + F1 enable + chip-id readback (DONE 2026-05-26)

Single-cycle Tier 4 via the diag-udp `'f'` sub-command (lwip
to-be-committed). All steps succeeded on the first attempt:

```
F0 CIS ptr bytes=ac 10 00  -> 0x0010ac
CIS[0..7]  20 04 d0 02 a6 a9 21 02
TPL_MANFID vendor=0x02d0 device=0xa9a6  (BCM43455)
F1 IOEn pre=0x00  set OK  IORDY iters=1 val=0x02  IOEn post=0x02
F1 SBADDR pre L=00 M=00 H=18  (window already at ChipCommon by default)
F1 backplane[0..3] = 45 43 26 15  chipid=0x15264345
  -> chip_id  = 0x4345  (BCM43455 family)
  -> chip_rev = 6        (BCM43455c0 silicon)
  -> chip_pkg = 1        (WLBGA)
```

**Confirmed**: the Pi 4 board chip is **BCM43455c0** with revision 6
silicon. The SDIO TPL_MANFID device id 0xa9a6 + chip-id 0x4345 +
chip-rev 6 uniquely identify it among the BCM43xx family. This
exactly matches what Linux's `brcmfmac` driver expects for the Pi 4
(`brcmfmac43455-sdio.bin` firmware).

Result file: `artifacts/diag-udp/2026-05-26-wifi-tier4-f.txt`.

### Tier 5+ — see `docs/plans/wifi-bcm43455-impl.md`

With chip family positively identified, the detailed Tier-5+ work
(firmware download, ARM core release, SDPCM/BCDC, IOCTL, scan,
associate, DHCP) follows the existing implementation plan at
[`docs/plans/wifi-bcm43455-impl.md`](plans/wifi-bcm43455-impl.md).

That plan recommends **Option A — porting NetBSD `bwfm`**
(BSD-2 licensed FullMAC driver, already validated against BCM43455
rev 6 silicon on the Pi 4 by NetBSD itself). It explicitly rejects
extending WHD because:

- WHD master upstream **dropped CYW43455 support**; carrying a
  fork against a deprecated chip path is a long-term liability.
- WHD's chip table is Cypress/Infineon-flavoured; 43455 is Broadcom
  silicon and the constants come from a different source tree
  (Linux brcmfmac).
- Phoenix is BSD-3-Clause; bwfm is BSD-2; both are permissive and
  align with the project's upstreamability goal. WHD is Apache 2.0
  — workable but heavier on notice/attribution requirements.

P0/P1/P2 of that impl plan (SDHCI + CCCR enumeration + chip-id
readback) are equivalent to the diag-udp `'w'/'i'/'e'/'f'`
sub-commands that landed today. Continue work from P3 (firmware
download via CMD53 + ARM core release).

The salvageable WHD pieces (NVRAM image header, BCM43455 chip
constants from `whd_chip_constants.c` if any) get folded into the
new bwfm-derived driver per impl-plan section 7.

### Tier 1a — SDHCI register snapshot (DONE 2026-05-25)

Full SDHCI 3.0 register dump via the diag-udp 'c' probe
(lwip `a66c25d`):

```
PRES_STATE   = 0x000f0001   CMD_INHIBIT=1, DAT[3:0]=0xf (lines high)
HOST/PWR     = 0x00000f00   SD_BUS_POWER=on, 3.3V
CLK/RST      = 0x000e8707   int clk en+stable, SD clk en, div=0x87
                            (~926 kHz from 250 MHz base)
INT_STATUS   = 0x00000000   no pending interrupts
INT_STAT_EN  = 0x37ff0033   cmd+xfer complete + most errors masked
CAPS_LO/HI   = 0x00000000   (Arasan quirk; informational)
HOST_CTRL2   = 0x00000000   no UHS modes
VERSION      = 0x99020000   vendor 0x99 + SDHCI Spec 3.0
```

The controller is **fully initialized at boot by VideoCore**. Tier 1
driver does not need to set up clocks, power, or interrupts —
it can proceed directly to bus-side bring-up.

### Tier 1 — SDHCI host driver scaffolding

- New `sources/phoenix-rtos-devices/multi/bcm2711-sdhost/` (or as a
  module under an existing multi). MMIO base `0xfe300000`, IRQ 56
  (per BCM2711 ARM-local).
- Initial scope: enumerate, soft reset, set bus width = 4, set
  clock to 400 kHz (init speed), implement CMD0/CMD3/CMD5/CMD7 +
  one-shot data transfer. NO interrupts yet (polled).
- Validate: SDIO CMD5 to function 0 returns OCR (chip-specific).
  No WHD required at this tier.

### Tier 2 — SDHOST IRQ + DMA

- Convert polled CMD/data path to IRQ-driven on
  `INTERRUPT_SDIO` (BCM2711 SPI 56 ≈ abs IRQ 88).
- DMA: Pi 4 SDHOST has its own DMA engine; alternative is
  PIO-via-FIFO which is simpler. Start with PIO, add DMA in
  Tier 5+ if throughput is insufficient.

### Tier 3 — cyhal_sdio binding

- Split current `cyhal_sdio.c` into `cyhal_sdio_imxrt.c` (existing
  code, gated by an `#ifdef`) and `cyhal_sdio_bcm2711.c` (new). The
  function-set interface (`cyhal_sdio_init`, `cyhal_sdio_send_cmd`,
  `cyhal_sdio_bulk_transfer`) stays the same.
- Implement the BCM2711 backend by calling into the Tier-2 SDHOST
  driver via msg-port or direct linkage.
- Validate: `cyhal_sdio_init()` returns OK; subsequent `send_cmd`
  for CMD52 read of CCCR reads the BCM43455 ID byte sequence.

### Tier 4 — WHD chip-id 43455 plumb

- Add `case 43455:` to each of the chip-constants switches in
  `whd_chip_constants.c`. Cross-check against upstream cy-whd /
  `infineon/wifi-host-driver` repo on GitHub for current values.
- Add firmware path resolution: WHD has a fw-loader callback that
  reads `cyfmac43455-sdio.bin` from disk; wire to a Phoenix-side
  open()/read() of the staged blob.
- Validate: `whd_wifi_on()` succeeds; chip reports its MAC.

### Tier 5 — scan + associate

- Run `whd_wifi_scan()`, collect BSSIDs in range.
- `whd_wifi_join()` with WPA2 PSK to a known test AP (lab needs an
  open or PSK SSID for this step).
- Validate: link-up event arrives via WHD's event handler.

### Tier 6 — lwip integration + DHCP + ping

- Bring up the new netif (`whd → cy_lwip → netif_add`).
- DHCP for the WiFi netif (the regular `dhcp_start()` should
  work since the lwip-side already supports multi-netif DHCP).
- Validate: host on the same SSID can ping the Pi 4 WiFi IP.

### Tier 7+ — productionize

- Throughput measurement.
- Drop the SDHOST PIO path for DMA if throughput < 20 Mbps.
- Power-state hooks (suspend/resume WL_REG_ON).
- Scan-cache + roaming.

## Why this order

Tiers 1-3 are all about getting BCM43455 visible over SDIO. Once a
single CCCR byte reads correctly, the rest of the stack (WHD + lwip)
is mostly upstream Cypress code that the Phoenix port already
contains. Splitting Tier 1/2 from Tier 3 lets us iterate the SDIO
driver in isolation without WHD's added complexity.

Tier 4 is small (~30 lines + firmware-loader hookup) and unblocks all
the WHD code in tree. It's worth doing once SDIO is solid because
otherwise WHD bails out before any radio work begins.

Tiers 5/6 are the visible "WiFi works" moment.

## Risks / unknowns

- **SDHOST vs Arasan ambiguity.** RESOLVED 2026-05-25 via the
  diag-udp 's' scout (lwip `124a99b`): MMC/SDHCI at `0xfe300000` is
  the WiFi target per Pi 4 Linux DT. GPFSEL3 reads 0x0 at boot
  (GPIO 34-39 INPUT), confirming pinctrl hasn't been applied —
  Phoenix WiFi-Tier 1 will set those pins to ALT3 as part of bring-up.
- **VideoCore-owned hardware.** Some Pi 4 board variants leave WL_REG_ON
  under VideoCore firmware control until an explicit mailbox call hands
  it over. Linux's `brcmfmac_pcie.c` doesn't deal with this, but Linux
  for the SDIO path (`bcm2835-mmc` + `brcmfmac-sdio`) does. Sequence
  the WL_REG_ON pulse via the mailbox to be safe.
- **Power-on timing.** BCM43455 has documented minimum hold times for
  WL_REG_ON. Sub-100 ms holds intermittently fail to bring up SDIO.
  150 ms is the Linux default; we'll match.
- **Firmware/CLM mismatches.** Cypress and Infineon ship subtly
  different firmware variants for the same silicon. Use the
  raspberrypi/firmware-nonfree blobs to match the Linux behaviour
  the Pi 4 ecosystem has been validated against.

## Validation tooling already in place

- **Tier 5c diag UDP responder** (`echo q | nc -u -w 1 10.42.0.99 9999`)
  will show the WiFi netif as a second line once it's registered:
  `netif: wl0 rx=... tx=...`. The same `t` query exposes WHD's
  internal threads.
- **HDMI snapshots** during the test cycle show fbcon state — Phoenix's
  WHD has debug printfs that emit to stdout, mostly visible during
  the pre-fbcon window.
- **The existing test cycle script** (`scripts/test-cycle-netboot.sh`)
  needs no changes — WiFi bring-up runs alongside the GENET netif on
  the same image.

## Open task

`#36` (WiFi BCM43455 — parked until Ethernet Tier 4). Unblocked
2026-05-25; description should be updated to point at this plan when
work begins.

## Open questions for next iteration

1. ~~Confirm SDHOST vs Arasan controller assignment on the lab Pi 4B~~ —
   resolved 2026-05-25: SDHCI at `0xfe300000`.
2. Decide on the firmware blob staging path (`/lib/firmware/...`
   under dummyfs vs a dedicated devfs node vs embedded into the
   image). Embedded is simplest for early bring-up.
3. ~~Whether to attempt SDHOST or jump straight to Arasan SDHCI~~ —
   SDHCI confirmed as the WiFi controller; SDHOST is the SD card
   path on Pi 4.
4. ~~Clock enable mechanism.~~ — partially resolved 2026-05-25 (lwip
   `818ac4a`, `b658bc1`). VideoCore mailbox `GET_CLOCK_RATE` for
   EMMC (id=1) returns **250 MHz at boot**; EMMC2 (id=12) returns
   **100 MHz**. Both clocks are already running. SDCard device
   power state (mailbox `GET_POWER_STATE` device_id=0) is also
   already **on (0x1)** at boot, and SET(on+wait) is idempotent.
   Yet SDHCI high-offset register reads (0x40 CAPS, 0xFC VERSION)
   still fault in the original v2 scout. Mechanism unclear —
   candidates:
   - BCM2711 Clock Manager `CM_RESETS` register at `0xfe101000`
     may hold individual peripherals in reset until explicitly
     released. Linux's `bcm2711-clocks.c` lists `BCM2711_CLK_EMMC`
     handled via `bcm2835-cprman.c`.
   - Phoenix mmap repeated against the same physical page (the v2
     scout did 4 mmap'd of overlapping pages in 0xfe300000) — could
     be a transient kernel bug.
   - The "fault" may not be a fault at all — could be a script
     race / nc -w 1 timing artifact. The single-mmap 'c' probe in
     this commit returns data fine.
   ~~Next experiment~~ — RESOLVED 2026-05-25 in lwip `812b0e9`. The
   v2 scout's empty reply was a multi-mmap probe artifact. With a
   single mmap of the 0xfe300000 page, all four reads work:
   `r00=0 caps_lo=0 caps_hi=0 ver=0x99020000`. Non-zero VERSION
   register confirms controller accessibility. CAPS=0 means the
   controller is in soft-reset (expected) — Tier 1 driver will
   issue SOFTWARE_RESET + delay then re-read CAPS.
