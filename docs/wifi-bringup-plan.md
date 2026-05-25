# WiFi (BCM43455) Bring-Up Plan for Phoenix-RTOS on Pi 4

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

1. **Pi 4 SDIO host driver.** The Pi 4 has two MMC/SDIO controllers:
   - **EMMC2** (Arasan SDHCI) at `0xfe340000`. On stock Pi 4 boards
     this is wired to the SD card slot.
   - **SDHOST** (bcm2835-style controller) at `0xfe300000`. Wired to
     the WiFi BCM43455 in 4-bit SDIO mode.
   Linux reference: `drivers/mmc/host/bcm2835.c` (~1500 lines) for
   the SDHOST controller (legacy bcm2835 path), or
   `drivers/mmc/host/sdhci-iproc.c` for the SDHCI path. Either is
   tractable; SDHOST is simpler / Pi-specific and is what Linux
   uses by default for WiFi on Pi 4.

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

### Tier 1 — SDHOST host driver scaffolding

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

- **SDHOST vs Arasan ambiguity.** The Pi 4 firmware can switch which
  controller drives WiFi vs SD card via DT pinmux. The default for
  Linux is SDHOST→WiFi, but if Phoenix's plo or armstub configures
  pinmux differently, we may inherit Arasan→WiFi. Determine this
  via the live DT or by reading GPIO ALT functions for bank 34-39
  before starting Tier 1. **Recommendation**: scout this first with
  a small one-shot binary that dumps `GPFSEL3/4` and the SDHOST/
  Arasan controller register zero (presence indicator).
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

1. Confirm SDHOST vs Arasan controller assignment on the lab Pi 4B.
   One-shot Pi-side binary that prints `GPFSEL` and the SDHOST
   register-zero would close this in a single test cycle.
2. Decide on the firmware blob staging path (`/lib/firmware/...`
   under dummyfs vs a dedicated devfs node vs embedded into the
   image). Embedded is simplest for early bring-up.
3. Whether to attempt SDHOST or jump straight to Arasan SDHCI. Linux
   uses SDHOST by default for Pi 4 WiFi, but SDHCI is more standard
   and the Phoenix codebase doesn't have either yet.
