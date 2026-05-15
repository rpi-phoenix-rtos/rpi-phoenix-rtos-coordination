# Bluetooth (BCM43455 over UART HCI) — Phoenix-RTOS Pi 4 implementation plan

Owner-of-record: this document. Research input:
[`docs/research/bluetooth-bcm43455.md`](../research/bluetooth-bcm43455.md),
[`docs/research/bluetooth-bcm43455-non-linux.md`](../research/bluetooth-bcm43455-non-linux.md).
Coordination repo path discipline: this plan does not commit code; it sets the
sequencing and file layout for work in `phoenix-rtos-devices` and
`phoenix-rtos-ports`.

## 1. Goal and tier ladder

End goal: a Phoenix-RTOS Pi 4 image that brings the on-board CYW/BCM43455
Bluetooth controller out of reset, loads its Patch RAM (`.hcd`) over UART HCI
H4, and exposes either a raw `/dev/hci0` or a usable LE host stack. Wi-Fi
(SDIO) is out of scope. Six tiers, each independently testable and
rollback-able via a `manifests/*.md` snapshot.

- **Tier 0 — UART routing.** Phoenix debug console stays on mini UART;
  PL011 becomes the BT HCI link owned by a new HCI server (see §2). Exit:
  Phoenix boots, mini UART carries klog, PL011 enumerates as a second tty.
- **Tier 1 — BT_REG_ON via firmware mailbox / expgpio.** Implement the VC4
  property-tag client, raise WL_REG_ON (`expgpio[1]`) then BT_REG_ON
  (`expgpio[0]`) with the ~700 µs LPO settle. Exit: BT silicon is clocked
  (verified indirectly by Tier 2).
- **Tier 2 — First HCI_RESET round trip.** H4 framer plus a "send command,
  await Command Complete" helper. Send `01 03 0C 00` at 115 200 8N1 CTS/RTS;
  expect `04 0E 04 01 03 0C 00`. Exit: that exact event is logged.
- **Tier 3 — `.hcd` patchram + baud switch.** `DOWNLOAD_MINIDRIVER`
  (`0xFC2E`), stream `WRITE_RAM` (`0xFC4C`) records, `LAUNCH_RAM` (`0xFC4E`),
  settle, `UPDATE_BAUDRATE` (`0xFC18`), re-reset. Exit: post-patch
  `READ_LOCAL_VERSION` returns a new LMP subversion.
- **Tier 4 — Minimal LE host.** BTstack or nimBLE host-only. Exit:
  `gap_le_scan` reports the address of a known BLE peer within 10 s.
- **Tier 5 — Full GAP / SMP / L2CAP / GATT.** LE pairing, GATT server with
  one read+notify characteristic, GATT client. Exit: interop run against
  `bluetoothctl` and `nRF Connect` in both roles.

Tiers 0-3 are "BT silicon works"; Tier 4 is "Phoenix does something with
BT"; Tier 5 is "production-credible".

## 2. UART assignment decision

Pi 4 has six PL011s (`uart0`, `uart2`-`uart5`) plus mini UART (`uart1`). Per
`bcm2711-rpi-4-b.dts` and research [§11](../research/bluetooth-bcm43455-non-linux.md):

- `uart0` is the only PL011 internally wired to BCM43455 BT (GPIO 30-33,
  never on the header). Flow control is mandatory above 921 600 baud.
- `uart1` (mini UART) is the default 40-pin debug header (GPIO 14/15);
  baud tracks `core_freq`, hence `core_freq=250` in `config.txt`.
- `uart2`-`uart5` are header-routable via overlays, unused on stock wiring.

**Today.** [`config.txt`](../../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt)
already sets `dtoverlay=miniuart-bt`, which swaps the firmware-side console
to mini UART and reserves PL011 for BT — the right mux for our target. The
Phoenix console is therefore already on mini UART even though the driver is
named [`pl011-tty.c`](../../../sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c)
(it parameterises `PL011_TTY_BASE`); confirm against `board_config.h` before
Tier 0.

**Tier 0 change.** Ship a second `pl011-tty` instance bound to PL011 MMIO
`0xFE201000` at 115 200 8N1 with RTS/CTS muxed (alt-0 on GPIO 30-33). The
HCI transport (Tier 2) opens this tty rather than writing to the controller
directly, keeping the H4 framer testable in userspace. PL011 is
register-compatible across instances, so the driver only needs a per-instance
base-address array. The mini UART debug console stays put.

Alternative — running BT on a header-routed `uart2` while keeping PL011 for
debug — is rejected: it would require external wiring and defeats the
on-board BT. Document only.

## 3. Mailbox + expgpio (precondition for Tier 1)

`BT_REG_ON` and `WL_REG_ON` sit on `expgpio`, accessed only through the VC4
firmware mailbox (research [§10](../research/bluetooth-bcm43455-non-linux.md)).
Phoenix has no mailbox driver yet — see `TD-15-mboxprobe` in
[`sources/phoenix-rtos-kernel/main.c`](../../../sources/phoenix-rtos-kernel/main.c)
where the buffer is reserved but the property-tag client is unwritten. This
driver is **the** blocker for BT Tier 1.

Required property tags:

- `0x00038041` `set_gpio_state` — `[u32 gpio_id, u32 state]`. BT pin is
  expgpio base + 0; WL is base + 1. (Confirm Pi 4 expgpio base offset
  against firmware docs — open question.)
- `0x00038043` `set_gpio_config` — direction/pull, belt-and-braces; firmware
  already configures these as outputs at boot on Pi 4.
- `0x00040002` `set_power_state` — domain enable; likely a no-op on Pi 4 but
  document for parity.

Driver size: ~300 LoC (single property-buffer page with cache-coherent
attributes, `MBOX_WRITE`/`MBOX_READ` ring at `0xFE00B880`, polling). The
same driver unblocks GPU framebuffer, SD power domains, and Wi-Fi — cost is
amortised across tracks.

**Sequencing.** WL_REG_ON must precede BT_REG_ON or the combo PMU
sequencer leaves BT invalid. Then wait ~700 µs (two 32.768 kHz LPO ticks)
before any HCI command. Code as `usleep(1000)`; do not skip.

## 4. File-level breakdown

New tree under `sources/phoenix-rtos-devices/bluetooth/`:

- `bluetooth/Makefile` — top-level `obj-y += hci-uart/` once Tier 2 lands.
- `bluetooth/hci-uart/Makefile` — links the server.
- `bluetooth/hci-uart/hci-uart.c` — main entry, opens the second PL011 tty,
  wires the H4 framer to libtty, exposes `/dev/hci0` (raw H4 byte device for
  Tier 0–3 demo) and a `msg`-based command/event API for Tier 4+ host stacks.
- `bluetooth/hci-uart/hci-uart.h` — public types: `hci_cmd_t`, `hci_evt_t`,
  packet-type enums.
- `bluetooth/hci-uart/h4-protocol.c` — byte-level demuxer: `0x01` cmd, `0x02`
  ACL, `0x03` SCO, `0x04` evt, plus the matching encoder.
- `bluetooth/hci-uart/bcm-vendor.c` — BCM-specific glue: VSC opcodes
  (`0xFC2E`, `0xFC4C`, `0xFC4E`, `0xFC18`, `0xFC27`), `bcm_patchram_load()`,
  `bcm_set_baudrate()`, `bcm_setup()`. Mirrors the structure of
  `chipset/bcm/btstack_chipset_bcm_download_firmware.c` from BTstack but
  written against Phoenix's libtty and msg API. Around 400 LoC.
- `bluetooth/hci-uart/bcm-firmware-blob.S` — `.incbin` of `BCM4345C0.hcd`,
  exposed as `_bcm_hcd_start` / `_bcm_hcd_end`. ~52 KB.
- `bluetooth/hci-uart/expgpio.c` — thin caller of the mailbox driver (lives
  in the kernel or a separate `mbox` server) that knows the BT/WL pin numbers
  and the LPO settling delay.

Stack layer (Tier 4+), one of:

- `sources/phoenix-rtos-ports/btstack/` — a Phoenix port of BTstack.
  Subdirectories: `phoenix-port/` (the HAL adapter implementing
  `btstack_uart_block_t`, `btstack_run_loop_t`, and `hal_time_ms`),
  `chipset-bcm/` (a thinned copy of upstream's `chipset/bcm/`), and an
  `examples/` folder for `gap_le_scan`, `le_counter`, etc.
- *or* `sources/phoenix-rtos-ports/nimble/` — a port of Apache Mynewt nimBLE
  (host-only build). Adds a hand-written BCM init shim because nimBLE has no
  vendor patching.

Userspace tooling: none required if `bcm-vendor.c` runs inside the HCI server
on startup (the recommended path). A `btattach`-equivalent is only needed if
we later want a userspace patchram for development; defer.

Project integration:

- [`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`](../../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml)
  gains a `hci-uart` server entry once Tier 2 is stable.
- [`config.txt`](../../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt)
  already correct; no change.

## 5. Stack choice — BTstack vs nimBLE vs hand-roll

| Candidate | License | Classic | LE | BCM patchram in-tree | Pi-family port | Effort |
|-----------|---------|---------|----|----------------------|----------------|--------|
| **BTstack** | non-commercial unless paid | yes | yes | yes | yes (`port/raspi`) | low (1-2 mo Tier 4) |
| **nimBLE** | Apache 2.0 | no | yes | no | no | medium (port BCM shim) |
| **hand-roll** | own | no | partial | n/a | n/a | high, fragile |

**Recommendation: nimBLE for the public release; BTstack as an internal
reference during bring-up.** BTstack is the fastest path to a working radio
and the only candidate with a Pi-family `port/raspi`, but its non-commercial
licence makes a public release a blocker (§9). Use BTstack only as a
documented reference for the BCM patchram record walker; write `bcm-vendor.c`
clean-room against the public protocol description (research §8 +
InternalBlue). Then bring nimBLE in for Tier 4. `bcm-vendor.c` is
host-stack-agnostic and stays put.

If Classic BT (RFCOMM, A2DP, HID-over-Classic) becomes a requirement, the
plan changes: license BTstack commercially or revisit FreeBSD
`netgraph/bluetooth`. Do not incur that cost speculatively.

## 6. Phased delivery

**Phase A — Mailbox + expgpio (precondition).** Lands the VC4 property-tag
client and a `set_gpio_state` helper. Same driver is needed by the GPU and
Wi-Fi work, so this is not BT-specific. Exit: probe BT_LED / WL_LED states by
reading back, manifest snapshot, status-doc note. UART signature: a
debug-console line "expgpio: BT_ON=1 WL_ON=1" after kernel init.

**Phase B — Tier 0 + Tier 2 (radio comes up, HCI_RESET round trip).** Add
the second `pl011-tty` instance, write `hci-uart.c` and `h4-protocol.c`, run
HCI_RESET against the bare ROM. Exit: log line "hci0: HCI_RESET ok (status=0)".

**Phase C — Tier 3 (.hcd patchram + 3 Mbaud).** Land `bcm-vendor.c`,
`bcm-firmware-blob.S`, embed `BCM4345C0.hcd`. Do the launch, settle, rebaud,
re-reset. Exit: log line "hci0: patchram ok, lmp_subver=<new>, baud=3000000".

**Phase D — Tier 4 (LE host, scan-only).** Bring in the chosen host stack
(nimBLE preferred). Run `gap_le_scan` for 10 s. Exit: log line "hci0: saw
peer aa:bb:cc:dd:ee:ff (RSSI=-NN)" matching a known phone.

**Phase E — Tier 5 (full GAP / SMP / L2CAP / GATT).** Pairing with a
laptop/phone, GATT server demo, GATT client demo. Multi-week.

Each phase ends with a `manifests/*.md` snapshot via
[`scripts/snapshot-integration-state.sh`](../../scripts/snapshot-integration-state.sh)
so we can roll back deterministically per the rollback discipline in
[`AGENTS.md`](../../AGENTS.md).

## 7. Test strategy

- **Tier 0.** Two USB-TTL captures (mini UART + PL011) via
  [`scripts/capture-rpi4b-uart.sh`](../../scripts/capture-rpi4b-uart.sh) plus
  [`summarize-rpi4b-uart-log.py`](../../scripts/summarize-rpi4b-uart-log.py).
  Pass: Phoenix banner is on mini UART, PL011 is silent until Tier 2.
- **Tier 1.** No direct UART signature. Verified indirectly via Tier 2.
  Optional: logic analyser on PL011 RX or a probe on the BT_LED line.
- **Tier 2.** Exact bytes `04 0E 04 01 03 0C 00` on PL011 RX within 50 ms of
  host TX `01 03 0C 00`. The HCI server hex-dumps all H4 traffic.
- **Tier 3.** `READ_LOCAL_VERSION_INFORMATION` (`0x1001`) before patchram
  returns the bare-ROM LMP subversion; after `LAUNCH_RAM` + post-launch
  reset it returns a different subversion. Capture both constants from a
  Linux-side one-shot to populate the pass criterion.
- **Tier 4.** Known BLE peripheral within 1 m (phone advertising or
  CR2032 iBeacon). 10 s scan. Pass: at least one `ADV_IND` matching the
  peripheral's MAC, RSSI -40 to -70 dBm.
- **Tier 5.** Manual interop with `bluetoothctl` (Linux) and `nRF Connect`
  (phone), both roles. No CI in first cut.

The UART summariser should grow a `--bt-mode` switch that recognises
HCI_RESET and `LAUNCH_RAM` markers.

## 8. Inter-dependencies

**Hard depends on:**

- VC4 firmware mailbox driver (NEW). Also needed by GPU, SD power domains,
  and Wi-Fi. **Land first.**
- A second `pl011-tty` instance (small extension to
  [`pl011-tty.c`](../../../sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c)).
- BCM2711 GPIO pinmux for GPIO 30-33 alt-0. No-op if firmware already
  mux'd via `miniuart-bt`; otherwise a one-shot in the HCI server.

**Conflicts with:**

- **Wi-Fi (combo chip).** Wi-Fi will also drive WL_REG_ON; ownership must
  be a shared `expgpio` service with one server and multiple clients, not
  competing direct callers. Land the arbiter before the second consumer.
- **Debug-console UART routing.** If anyone reverts `dtoverlay=miniuart-bt`
  or breaks the second `pl011-tty` instance, BT loses its UART or klog
  vanishes. Add a runtime assertion in the HCI server: if its PL011 base
  matches the active klog tty, refuse to start.

**Independent of:** cache / MMU work, networking stack (HCI is its own byte
channel), display beyond sharing the mailbox.

## 9. License gate

BTstack is dual-licensed: free **for non-commercial use only**, with a paid
commercial licence available from BlueKitchen GmbH. A public Phoenix-RTOS
release containing BTstack — even just the chipset adapter — is a **release
blocker** unless a commercial licence is obtained. Mark this in
[`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`](../TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
the moment any BTstack file enters the tree, with an `LIC-BT-01` tag.

The Apache 2.0 nimBLE alternative removes the gate but loses Classic BT and
forces us to maintain `bcm-vendor.c` ourselves. That is acceptable: the BCM
patchram protocol is documented externally (research [§8](../research/bluetooth-bcm43455-non-linux.md)
and InternalBlue) so a clean-room implementation is feasible.

The `BCM4345C0.hcd` blob itself ships under Cypress's redistribution terms via
the Linux `linux-firmware` package; redistribution in a binary image is
permitted but the EULA must be reproduced. Add to the publish-map.

## 10. Effort estimate

Working backwards from the milestones, in engineer-weeks of focused work
(not calendar time; the bring-up runs in parallel with other tracks):

- Phase A (mailbox + expgpio) — 1.5 weeks. Largely shared with other tracks;
  attribute 0.5 to BT.
- Phase B (Tier 0 + Tier 2) — 2 weeks.
- Phase C (Tier 3 patchram) — 2 weeks. Most of the BT-specific risk is here.
- Phase D (Tier 4 LE scan) — 3 weeks for nimBLE port + integration; 1.5
  weeks for BTstack if we accept the licence terms internally.
- Phase E (Tier 5 full host) — 6-10 weeks depending on how much of nimBLE's
  pairing/SMP work-out-of-the-box on Phoenix.

Total: **roughly 4-5 months of one-engineer time** to reach Tier 4, plus
another 1-2 months to Tier 5. Multi-month is the correct framing; this is
new-frontier work for bare-metal Pi 4 (no public BT bring-up exists, per
research §7).

## 11. Open questions

- **Exact LMP subversion** of the bare BCM43455 ROM and post-patch — needs a
  one-shot Linux capture on the bring-up Pi 4 to populate Tier 3's pass
  criterion.
- **Which `.hcd` matches our board revision** — `BCM4345C0.hcd` is the most
  likely hit, but `BCM4345C5.hcd` ships in some `linux-firmware` snapshots.
  Decide via `READ_LOCAL_VERSION` on first hardware contact.
- **Does Phoenix's tty layer expose a `tcdrain` equivalent** for the
  `UPDATE_BAUDRATE` race? If not, add a libtty TX-fifo-flush primitive before
  Tier 3 lands.
- **Cypress redistribution EULA** — confirm the exact wording we must ship
  alongside the firmware blob in the Phoenix image.
- **Mailbox property-buffer cache attributes** — do we need uncached or
  inner-shareable-cacheable on the page that holds the property buffer? This
  intersects with the TD-04 cache-coherency class problem.
- **expgpio numbering on Pi 4 vs Pi 3** — research cites GPIO 128 / 129 / 45
  for different Pi families; double-check Pi 4 with the firmware mailbox tag
  documentation before Phase A.
- **BTstack non-commercial use during in-house bring-up** — get an explicit
  go/no-go from licensing before any BTstack source enters the tree, even
  privately.
- **Coexistence VSC for FW r177+** — newer `.hcd` files require a vendor
  coex tuning command after patchram, else BT stutters when Wi-Fi is active.
  Capture the constant and add it to `bcm-vendor.c` once Wi-Fi is also up.
- **Sleep-mode VSC `0xFC27`** — needed for power but not for first bring-up;
  schedule as a Tier 5 hardening item, not a Tier 3 blocker.
