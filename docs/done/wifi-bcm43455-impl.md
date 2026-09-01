# Implementation plan — Pi 4 WiFi (CYW43455 / BCM43455 over SDIO)

> **STATUS (2026-06-26): forward plan, still ACTIVE but PARKED at the #91 firmware-execution
> gate.** Implementation reached fw-download + CR4-release; the firmware does not execute (see
> `2026-06-04-wifi-fw-exec-gate.md`). This plan remains the valid forward reference; no change.

Status: forward plan. Sequenced after MMU/EL drop, GENET ethernet, and a
working in-tree SDIO bus driver. References: forward research at
[`docs/knowledge/wifi-bcm43455.md`](../knowledge/wifi-bcm43455.md), non-Linux
survey at [`docs/knowledge/wifi-bcm43455-non-linux.md`](../knowledge/wifi-bcm43455-non-linux.md).

## 1. Goal and tier ladder

Bring the on-board CYW43455 WLAN radio up on Phoenix-RTOS so an unprivileged
user-space program can associate with a WPA2 access point and exchange ICMP
and DHCP traffic via lwIP.

- **Tier 0 (MUST) — SDIO bus driver.** Phoenix can issue CMD52 single-byte and
  CMD53 multi-block reads/writes to the BCM2711 Arasan host (`mmcnr`) sitting
  at `0xfe300000`, with IRQ delivery (card-IRQ + host-IRQ).
- **Tier 1 (MUST) — chip detection.** SDIO function 1 is enabled, the
  ChipCommon backplane window resolves chip ID `0x4345` rev 6, and the
  driver prints `BCM43455 rev 6 detected on mmcnr` over UART.
- **Tier 2 (MUST) — firmware upload + bring-up.** `brcmfmac43455-sdio.bin`
  and the Pi-4 NVRAM blob are streamed into SOCRAM, the chip's Cortex-M3
  comes out of reset, and BCDC/SDPCM version handshake completes.
- **Tier 3 (MUST) — frame TX/RX.** A WHD-style API (`whd_wifi_scan`,
  `whd_network_send_ethernet_data`) returns beacons from `iovar` scan and
  pushes raw 802.3 frames in both directions.
- **Tier 4 (MUST) — open-AP association.** Driver associates with a fixed
  open SSID at the bench, link goes up, ARP works.
- **Tier 5 (MUST) — WPA2 + DHCP/DNS.** `WLC_SET_PASSPHRASE` +
  `WLC_SET_SSID` to a WPA2-PSK AP; lwIP's DHCP client gets an IPv4 lease and
  pings a known host.
- **Tier 6 (STRETCH) — WPA3-SAE.** Firmware-offloaded SAE if feature flags
  permit; gated on a recent firmware blob and the `brcmfmac.feature_disable`
  workaround documented in the research note.
- **Tier 7 (STRETCH) — Scan/connect IPC.** A tiny `wifictl`-style tool +
  port for runtime SSID/passphrase changes (Tier 4-5 use a hard-coded
  config).

## 2. Phoenix conventions audit

Phoenix already ships partial SDIO infrastructure:

- [`phoenix-rtos-devices/sdio/common/sdio.h`](../../sources/phoenix-rtos-devices/sdio/common/sdio.h)
  — bus-API header with `sdio_init`, `sdio_config`, `sdio_transferDirect`
  (CMD52), `sdio_transferBulk` (CMD53 single/multi-block),
  `sdio_eventRegister`/`sdio_eventEnable` for card-IRQ wakeups. This is the
  contract a WiFi driver consumes.
- [`phoenix-rtos-devices/sdio/imx6ull/imx6ull-sdio.c`](../../sources/phoenix-rtos-devices/sdio/imx6ull/imx6ull-sdio.c)
  — 864-line worked example. Talks to the i.MX6ULL uSDHC at `0x2194000`
  with DMA, an interrupt thread, mutex-locked command issue, and 32-bit
  register access. It is **not** a server (no port, no msg loop) — it is
  a library linked into the driver that owns the SDIO bus. That driver
  becomes the "WiFi server" that registers a port.

There is also a **half-finished WHD port** under
[`phoenix-rtos-lwip/wi-fi/whd/`](../../sources/phoenix-rtos-lwip/wi-fi/whd/),
including `whd_chip.c`, `whd_sdpcm.c`, `whd_cdc_bdc.c`, `whd_resources.c`,
`whd_bus_sdio_protocol.c`, plus a Cypress HAL shim layer in
[`phoenix-rtos-lwip/wi-fi/hal/`](../../sources/phoenix-rtos-lwip/wi-fi/hal/)
(`cyhal_sdio.c` etc.). It compiles for none of the in-tree targets
(`_targets/Makefile.*` lists no Pi 4, no aarch64). Treat it as a salvage
yard: many `whd_*` files are bus-agnostic and reusable, but the bus shim
and `cybsp_wifi.c` were written against ModusToolbox-style HAL, not the
Phoenix `sdio.h` API.

Idiom for a new subsystem in Phoenix: a server binary registers a port,
exports `msg_t`-shaped RPCs, and links against driver libraries
(`libdrv_sdio`-style) for hardware. The WiFi server will look like
`usbsrv` or `phoenix-rtos-usb/usbsrv` in role: owns the SDIO bus
exclusively, runs an event loop, dispatches TX requests from lwIP and
delivers RX frames.

## 3. File-level breakdown

```
sources/phoenix-rtos-devices/sdio/rpi4-sdhci-sdio/
    Makefile                        # builds sdio_rpi4 driver lib
    sdhci-sdio.c                    # Arasan SDHCI v3 + SDIO command engine
    sdhci-sdio.h                    # MMIO layout, IRQ bits, DMA descriptors

sources/phoenix-rtos-devices/sdio/common/
    sdio.h                          # already exists, keep as canonical API
    sdhci-regs.h                    # NEW shared SDHCI v3 register layout

sources/phoenix-rtos-devices/wireless/cyw43455/
    Makefile                        # builds cyw43srv server
    cyw43.c                         # main: msg loop, port registration
    cyw43_chip.c                    # backplane bring-up, SOCRAM, ARM CR4 reset
    cyw43_firmware.c                # blob loader, NVRAM packing, watchdog
    cyw43_wifi.c                    # iovar wrappers, scan/join/leave state
    cyw43_sdpcm.c                   # SDPCM framing (or salvaged whd_sdpcm)
    cyw43_bcdc.c                    # BCDC IOCTL/IOVAR multiplexer
    cyw43_resource.h                # firmware-blob symbol declarations
    cyw43.h                         # client-facing message structs

sources/phoenix-rtos-lwip/drivers/
    cyw43455-netif.c                # NEW lwIP netif glue
    netif-driver.c                  # existing register_netif_driver hook

sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/
    board_config.h                  # add SDHCI_MMCNR_BASE = 0xfe300000,
                                    # SDHCI_MMCNR_IRQ, GPIO34/41 mux for WL_REG_ON
    build.project                   # add cyw43srv to component list
```

The firmware blob ships as a generated C array
(`cyw43_firmware_blob.c` produced from `brcmfmac43455-sdio.bin` via a host
`xxd -i`-style step in the driver `Makefile`). See section 5.

## 4. Bus abstraction

The WiFi driver consumes the existing
[`sdio.h`](../../sources/phoenix-rtos-devices/sdio/common/sdio.h) API:

- `sdio_transferDirect(sdio_read, addr, area, &byte)` — CMD52, single byte,
  used for CCCR/FBR enable, function-select, watermark, address-window
  paging.
- `sdio_transferBulk(dir, blockMode, addr, area, buf, len)` — CMD53,
  byte-mode for short metadata pulls, block-mode (64-byte blocks per
  brcmfmac convention) for firmware download and frame data.
- `sdio_eventRegister(SDIO_EVENT_CARD_IRQ, handler, arg)` +
  `sdio_eventEnable` — host-side hook for the chip's mailbox interrupt.

Compared with reference designs:

- **NetBSD `sdmmc`** exposes `sdmmc_io_read_byte`, `sdmmc_io_write_byte`,
  `sdmmc_io_read_multi_1`, `sdmmc_io_write_multi_1`. One-to-one mapping
  with `sdio_transferDirect`/`sdio_transferBulk`.
- **Pico-SDK `cyw43-driver`** demands `cyw43_read_bytes` and
  `cyw43_write_bytes` callbacks at the integrator level. Trivial wrappers
  over the Phoenix calls; the driver was written with bus-agnosticism as
  an explicit goal.
- **Cypress WHD** uses `whd_bus_sdio_*` ops registered through
  `whd_bus_sdio_attach`. The half-port already in tree (`whd_bus.c`,
  `whd_bus_sdio_protocol.c`) carries this signature; the bus glue would
  fan out to `sdio_transferDirect`/`Bulk`.

Decision: keep the public `sdio.h` unchanged. Add an
**Arasan-specific implementation** under `sdio/rpi4-sdhci-sdio/` that
lives behind that header. Anything peculiar to the Pi 4 (32-bit-only
register access, BCM2711 watermark quirks, F0/F1/F2 enable order with
`WL_REG_ON` GPIO assertion) goes in the implementation file, not the
header.

## 5. Firmware blob distribution

Phoenix has no `/lib/firmware/`. Three options, ranked:

(a) **Embed in the cyw43srv binary as a C array** (recommended). The
    server `Makefile` runs a host pre-build step that converts
    `firmware/brcmfmac43455-sdio.bin` (~600 KB) and the Pi-4 NVRAM
    (`brcmfmac43455-sdio.raspberrypi,4-model-b.txt`, ~3 KB) into linkable
    `const uint8_t cyw43_firmware[] = { ... };`. Pros: deterministic,
    matches Pico-SDK's `cyw43-driver/firmware/` model; the blob is in
    the .text/.rodata of cyw43srv, paid for once at server load — not in
    the kernel. Cons: ~600 KB extra in the cyw43srv ELF; a firmware
    update means rebuilding the server.
(b) **Ship as a separate file on a Phoenix filesystem.** Possible once a
    rootfs (jffs2/littlefs/nfs) is wired up, but Phoenix Pi 4 today loads
    via TFTP and has no persistent FS. Forces a sequencing dependency on
    that work and a `fopen`/`fread` path inside cyw43srv. Defer.
(c) **Embed in the kernel image / syspage.** Bloats the early DRAM
    footprint and entangles a userspace driver's payload with the kernel.
    Reject.

Recommendation: **(a) for tiers 0-5**, revisit (b) when a rootfs is
available so updates do not require a server rebuild.

License: `brcmfmac43455-sdio.bin` is non-free binary-redistributable
(Cypress / Infineon firmware EULA via `firmware-nonfree`). Carry an
explicit `LICENSE.firmware` next to the blob and document the source URL
at [`endlessm/linux-firmware`](https://github.com/endlessm/linux-firmware/blob/master/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt)
and OpenWRT cypress-nvram. Cypress permits redistribution; we cannot
modify the blob.

## 6. Phased delivery

P0–P2 status update 2026-05-26: equivalent work has landed in
`sources/phoenix-rtos-lwip/port/diag-udp.c` as the `'w'/'i'/'e'/'f'`
diag sub-commands (lwip e3ab254, dfa96b1, b247d0e, b87d314). The
code lives in lwip-port rather than the canonical
`sources/phoenix-rtos-devices/sdio/rpi4-sdhci-sdio/` location
because it began as observability scaffolding (UDP on :9999) before
the file layout in section 3 was firmed up. P3 work should migrate
the SDIO + chip code out of diag-udp into a dedicated driver tree
when the chip-init logic stops fitting in a side-process probe.

| Phase | Scope | Success criterion | UART / UDP signature | Status |
|---|---|---|---|---|
| **P0** | Add `WL_REG_ON` GPIO assert, `mmcnr` clock + pinmux, board_config bases | GPIO drives high; SDHCI controller reset clears | diag-udp `'w'` reports `WL_REG_ON=1`, GPFSEL3 = ALT3 | **DONE 2026-05-26** |
| **P1** | CMD0/CMD5/CMD3/CMD7 enumeration, CMD52 R/W to CCCR | RCA assigned, CCCR readable | diag-udp `'e'` reports `RCA=0x0001`, `CCCR ver 0x43` | **DONE 2026-05-26** |
| **P2** | F1 enable, backplane window, ChipCommon ID readback | `chipid==0x4345 rev 6` | diag-udp `'f'` reports `chipid=0x15264345` (chip=0x4345, rev=6, pkg=1 BCM43455c0) | **DONE 2026-05-26** |
| **P3** | SOCRAM init, ARM CR4/CM3 hold, firmware download via CMD53 block writes, NVRAM pack-and-append, ARM release, ready mailbox poll | Chip prints firmware version over BCDC | `cyw43: fw bcm43455c0 7.45.241.x ready` | NEXT (~4 weeks) |
| **P4** | SDPCM/BCDC framing, IOCTL/IOVAR get-set, MAC-from-OTP, scan iovar | Beacons returned from passive scan; MAC matches OTP | `cyw43: mac=dc:a6:32:.. scan: 5 BSSes` | 3 weeks |
| **P5** | Open-AP join: `WLC_SET_INFRA`, `WLC_SET_SSID`, link-event handling | Linkup event from firmware | `cyw43: associated to "ph-test" ch=6` | 2 weeks |
| **P6** | lwIP netif: pbuf TX path, RX queue + thread, MAC + MTU registration | ARP and ICMP work to link-local | `enX: link up, IPv4 169.254.x.x` | 2 weeks |
| **P7** | WPA2-PSK: `WLC_SET_WSEC`, `WSEC_PMK`, `WLC_SET_AUTH`, DHCP via lwIP | Ping a known host on a WPA2 network | `cyw43: WPA2 4-way OK; dhcp lease 192.168.x.y` | 3 weeks |
| **P8** | Stretch: WPA3-SAE, scan IPC, runtime config | SAE association | `cyw43: SAE OK` | open |

Total to Tier 5 closure: ~19 weeks of focused work, before slip.

## 7. Stack architecture decision

Three candidates per the non-Linux survey:

- **Option A — Port NetBSD `bwfm` (~5 kLoC).** Clean-room C, BSD-2,
  proven on Pi 4 with the same Arasan SDHCI we target. Structure
  (`bwfm.c` + `if_bwfm_sdio.c`) maps onto Phoenix's "library + server"
  split. Backplane probing and chip init are correct for CYW43455 rev 6.
- **Option B — Pico-SDK `cyw43-driver`.** Smallest codebase, embedded-
  by-design. But: only ever validated on CYW43439 with gSPI; chip-init
  for 43455 differs (different SOCRAM size, backplane core IDs); we
  would need to write a fresh `cyw43_bus_sdio.c` and validate the LL
  layer against an unsupported chip. Higher integration risk.
- **Option C — Finish the in-tree WHD half-port.** Most code already
  written, but WHD master upstream has dropped CYW43455. Carrying a
  fork against a deprecated chip path is a long-term liability.

**Recommendation: Option A (bwfm).** Concretely:

1. Take the NetBSD files `sys/dev/ic/bwfm.{c,h}`,
   `sys/dev/ic/bwfmreg.h`, `sys/dev/ic/bwfmvar.h`,
   `sys/dev/sdmmc/if_bwfm_sdio.c` as the protocol-layer skeleton.
2. Replace `sys/dev/sdmmc` calls with the Phoenix `sdio.h` API.
3. Replace `if_attach`/`bpf_*`/`net80211` calls with a thin lwIP netif
   shim (section 9). bwfm is FullMAC, so net80211 is mostly a wrapper
   around firmware iovars — most of the surface translates into
   `pbuf_alloc` + `netif->input`.
4. Salvage from the in-tree WHD port: NVRAM image header
   (`wifi_nvram_image.h`), `whd_chip_constants.c` for BCM43455 specifics,
   chip-init constants. Drop the WHD bus and HAL layers.

Keep Option B as an exit option: if bwfm's licensing or integration
proves intractable for upstreaming, the `cyw43_ll.c` layer is permissive
and has a smaller surface to vendor.

## 8. WPA2/WPA3 user API

CYW43455 firmware terminates the EAPOL 4-way handshake on-chip, so the
host-side commands are:

```c
struct cyw43_join_req {
    char    ssid[32];
    uint8_t ssid_len;
    char    passphrase[64];
    uint8_t passphrase_len;
    uint8_t auth;     /* CYW43_AUTH_OPEN | _WPA2_PSK | _WPA3_SAE */
    uint8_t channel;  /* 0 = scan-and-pick */
};
```

Server-side iovar sequence per Cypress / WHD reference: `WLC_DOWN`,
`WLC_SET_INFRA=1`, `WLC_SET_AUTH={0|WPA2|SAE}`,
`WLC_SET_WSEC=AES_ENABLED`, `wsec_pmk` (set 64-byte PMK or pass
passphrase), `WLC_SET_SSID`, `WLC_UP`. lwIP's DHCP client picks up the
moment `NETIF_FLAG_LINK_UP` flips. Tier 5 is reached when this RPC
succeeds against a WPA2-PSK AP and DHCP returns a lease.

For Tier 7 the server would expose this as a Phoenix port message
(`oid_t cyw43_oid; msg.type = mtDevCtl; msg.i.io.op = CYW43_JOIN;`).

## 9. Test strategy

- **First-light (Tier 1).** UART signature
  `BCM43455 rev 6 detected on mmcnr`. Plus a CMD52 read of CCCR offset
  `0x00` returning `0x33` (CCCR/SDIO spec rev 3). Recorded into
  `manifests/<phase>.md` per the rollback discipline in
  [`AGENTS.md`](../../AGENTS.md).
- **Mid (Tier 4).** A bench AP broadcasting open SSID `ph-test` on
  channel 6. Driver runs a scripted join from `init`, prints linkup, and
  responds to ARP for an in-server static IP. Captured by
  `scripts/capture-rpi4b-uart.sh` and summarised by
  `scripts/summarize-rpi4b-uart-log.py`.
- **Final (Tier 5).** WPA2-PSK AP, DHCP server on the bench, ICMP to a
  known host. Pass criterion: 1000 echo requests, 0% loss, RTT < 50 ms
  median (Wi-Fi over a clean test bench should easily clear this).
- **Regression.** Each phase produces a manifest via
  `scripts/snapshot-integration-state.sh`; failures restore via
  `scripts/restore-integration-state.sh <manifest>`.

## 10. Inter-dependencies

Depends on (in order):

1. MMU on, EL drop to EL1 — already landed.
2. Stable lwIP for IPv4 plus DHCP. Existing `phoenix-rtos-lwip` already
   carries DHCP; no new work.
3. **GENET ethernet first**, not for code reuse but for stack-shakedown:
   confirms lwIP and the netif registration path on Pi 4 work before
   adding SDIO complexity to the same loop.
4. SDIO host driver — **new code**, P0-P1 in section 6.
5. DMA + cache coherency on BCM2711 — already exercised by GENET. The
   SDIO controller uses ADMA2 descriptors; reuse the cache-clean +
   IPA->PA logic landed for GENET. Track under TD-04 in
   [`docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`](TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md).
6. GPIO subsystem for `WL_REG_ON` (GPIO34) and 32 kHz LPO routing — Pi 4
   board-side wiring; depends on whatever GPIO driver the GPIO/pinctrl
   forward research lands.

Conflicts:

- **Bluetooth bring-up** uses the same chip. The 43455 BT side comes up
  over UART (`/dev/ttyAMA1`) with its own firmware patch (`BCM4345C0.hcd`).
  Firmware load order: WLAN must be brought up first because both share
  the same `WL_REG_ON` and 32 kHz LPO; once WLAN is in operational state
  the BT firmware can be patched in. Coordinate with the BT plan.
- **BCM2711 SD vs. WLAN clock domain.** Open question §8 from the
  forward research — both controllers share a clock manager. If the
  boot SD path leaves it in a state hostile to mmcnr, P0 will fail
  early. Probe with `scripts/capture-rpi4b-uart.sh` after boot SD
  init and inspect the `mmcnr` clock register before issuing any SDIO
  command.

## 11. Effort estimate

Honest range: **3-6 developer-months** for one engineer to take Tier 0
through Tier 5 against a clean codebase, factoring in:

- ~5 weeks SDIO host driver (P0-P2). The Arasan controller is well-
  documented but BCM2711 quirks (32-bit-only access, watermark register
  layout, ADMA descriptor shape) consume time.
- ~4 weeks chip bring-up + firmware load (P3). This is the highest-risk
  phase: the `HT avail timeout` failure mode that bit OpenBSD's bwfm
  on Pi 3 is a real possibility on Pi 4 if the F2 enable handshake
  is mistimed.
- ~3-5 weeks BCDC / SDPCM + scan (P4).
- ~2 weeks association + lwIP integration (P5-P6).
- ~3 weeks WPA2 + DHCP shakedown, including `wsec_pmk` quirks (P7).
- ~2-4 weeks slack for Pi-specific NVRAM debugging (silent dead-radio
  failure mode if NVRAM fields are wrong) and intermittent regulatory
  / channel issues.

Tier 6-7 (SAE, scan IPC) add another 1-2 months.

## 12. Open questions

- **License fit.** NetBSD bwfm is BSD-2-clause, Pico-SDK
  cyw43-driver is BSD-3-clause; both compatible with Phoenix's
  permissive licence target.
- **NVRAM blob.** Use the Pi-Foundation Pi-4-specific NVRAM
  (`brcmfmac43455-sdio.raspberrypi,4-model-b.txt`) verbatim, sourced
  from openwrt/cypress-nvram. The generic Cypress NVRAM yields
  marginal RF and intermittent disassoc on Pi-4 antennas — confirmed
  by the brcmfmac firmware-loading-by-DT-compatible logic and by
  bench reports.
- **MAC address.** Pi 4 reads the MAC from OTP through a VC4 mailbox
  call. Phoenix has no mailbox client yet. Workaround for tiers 1-5:
  let the firmware fall back to the NVRAM `macaddr` (typically blank)
  or a derived MAC; defer the mailbox-OTP read to a later phase under
  its own TD ticket.
- **OOB IRQ vs in-band.** brcmfmac and bwfm both support an
  out-of-band "host-wake" GPIO IRQ in addition to the SDIO in-band
  interrupt. Pi 4 wires this on GPIO40. In-band is sufficient
  for Tier 5; OOB is an optimisation for low-power that can wait.
- **Concurrency model.** Tier 0-3 fit a single-threaded server; from
  Tier 5 onwards we want a TX thread distinct from the RX/event
  thread to avoid head-of-line blocking on long iovars during heavy
  throughput. Decide concretely at P5 boundary.

---

This plan supersedes nothing currently in tree. It is referenced from
[`docs/inprogress/status.md`](status.md) once any phase becomes the active step
in [`tracking/current-step.md`](../../tracking/current-step.md).
