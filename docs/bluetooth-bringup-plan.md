# Bluetooth (BCM43455) Bring-Up Plan for Phoenix-RTOS on Pi 4

**Status**: scout / planning (not yet started).
**Owner**: open.
**Estimated effort**: 4–6 iterations for Tier 0–2 (raw HCI controller
usable). Tier 3 (host stack) depends on choice (defer / NimBLE / BTstack)
and may add another 3–8 iterations.
**Unblocked since**: 2026-05-25, conditional on WiFi Tier 1 landing the
mailbox `SET_GPIO_STATE` plumbing (shared with `expgpio`).

## Scope summary

Bring the BT side of the BCM43455 combo chip up under Phoenix-RTOS to
the point where Phoenix can issue HCI commands and parse HCI events
over UART. The BT side is a **standard UART HCI controller framed in
H4** — much simpler than the WiFi SDIO path. Once the radio is alive,
choose a host stack (or defer and ship as a raw `/dev/hci0`).

## Why this comes after WiFi

Three reasons:

1. **Shared prerequisite: expgpio via mailbox.** BT_REG_ON sits on the
   firmware-driven GPIO expander (`expgpio[0]`), not on a native
   BCM2711 GPIO. WiFi's WL_REG_ON is on the same expander. The mailbox
   `SET_GPIO_STATE` (tag `0x00038041`) helper we write for WiFi
   Tier 1c is the same code path BT needs. Doing WiFi first means BT
   inherits a tested helper.
2. **Shared prerequisite: firmware blob loading.** Both need a
   ~30–600 KB blob from `linux-firmware/brcm/` mounted somewhere in
   Phoenix's namespace. Solving the staging path once (e.g. embedded
   into the image vs. dummyfs `/lib/firmware/` vs. TFTP pull) for
   WiFi unblocks BT for free.
3. **WiFi has higher user-visible value** for an RTOS demo. The
   community's "bring up Pi 4 from bare metal" tutorials all stop at
   Ethernet → SDIO → WiFi; BT is invariably deferred. Phoenix-RTOS
   should follow the same priority.

## What's already in tree

- **PL011 driver**: yes, Phoenix's console uses PL011 already. UART0
  (`0xfe201000`) is wired to BT on Pi 4. Phoenix's `config.txt`
  currently uses `dtoverlay=miniuart-bt`, which swaps **the console
  to the mini-UART** and **reserves UART0 for BT**. So the pinmux
  side is already done by VideoCore firmware — we don't need to set
  GPFSEL3 ourselves.
- **Mailbox property channel**: yes — used by GENET Tier 5b
  (`GET_BOARD_MAC`), Tier 1a (`GET_CLOCK_RATE`,
  `GET_POWER_STATE`/`SET_POWER_STATE`), thermal probe
  (`GET_TEMPERATURE` + `GET_THROTTLED`), and the watchdog reboot
  (no mailbox, MMIO PM block). Adding `SET_GPIO_STATE` (tag
  `0x00038041`) is one more call against the same transport.
- **lwip + Ethernet**: lets us push the firmware blob via TFTP or
  similar during development without needing SD/EMMC storage.

## What's NOT in tree

1. **HCI H4 framing layer.** ~200 LoC: a byte sink that demultiplexes
   the packet-type prefixes (0x01 command, 0x02 ACL, 0x04 event), a
   "send HCI command + await Command Complete with matching opcode"
   helper, and a small ring buffer for events.
2. **Broadcom Patch RAM uploader.** Walks a `.hcd` blob calling
   vendor `WRITE_RAM` (0xFC4C) per record, then `LAUNCH_RAM` (0xFC4E)
   at the end. ~100 LoC plus the `.hcd` blob itself.
3. **Vendor baud-rate switch.** VSC `0xFC18` `UPDATE_BAUDRATE` sets
   3 Mbaud after patchram. Trivial; needs a `tcdrain`-equivalent
   on Phoenix's PL011 driver before the host-side baud change.
4. **expgpio mailbox driver.** ~30 LoC to call
   `SET_GPIO_STATE`(expgpio[0], 1) for BT_REG_ON.
   Shared with WiFi for WL_REG_ON sequencing.
5. **Host stack** (host-side Bluetooth protocol: L2CAP / GATT / SMP /
   etc.). Deferred — see §"Host stack" below.

## Bring-up tier plan

Matching the WiFi plan's pattern: one tier per validated checkpoint,
each ends in a manifest + doc note.

### Tier 0 — UART0 + BT_REG_ON

- Verify the current `config.txt` indeed reserves UART0 for BT
  (`dtoverlay=miniuart-bt` is already set per `docs/rpi4-os-
  development-guide.md` §"Phoenix config.txt knobs"). Confirm by
  reading GPFSEL3 via the diag-udp 'g' command: pins 30-33 should be
  in ALT3 (function 7) for UART0 RTS/CTS/TXD/RXD. If they're INPUT,
  add `enable_uart=1` adjustments.
- Implement `mailbox_set_gpio_state(pin, state)` in the lwip-port
  (or, better, in a small new userspace helper). Reuses the existing
  property-channel transport from `bcm-genet.c`'s `genet_mboxGetMac`.
- Raise BT_REG_ON via that call.
- Open UART0 at 115200 8N1 RTS/CTS.
- Loopback test: send `HCI_RESET` H4 packet `01 03 0c 00`, expect
  a `0x0e` Command Complete event back. No firmware needed — the
  unpatched ROM responds to RESET.
- Output: scout note recording the chip's `READ_LOCAL_VERSION`
  response so we know exactly which `.hcd` matches (BCM4345C0 vs
  BCM4345C5, etc.).

### Tier 1 — H4 framing + HCI helpers

- New file `sources/phoenix-rtos-utils/btctl/btctl.c` (or whatever
  Phoenix's "small daemon" location is): an H4 byte sink + an HCI
  command/response framework. Roughly:
  - State machine demuxing packet-type bytes `0x01`/`0x02`/`0x04`.
  - `hci_send_cmd(opcode, params, len)` blocking on Command Complete
    with matching opcode.
  - Background read thread feeding the response framework.
- Wire it to UART0 via Phoenix's existing PL011-tty msg-port driver
  (open `/dev/pl011` and read/write directly, or open a new
  `/dev/hci0` chardev backed by the daemon).
- Validate: `HCI_RESET`, `READ_LOCAL_VERSION_INFORMATION` (`0x1001`),
  `READ_LOCAL_NAME` (`0x0c14`) all succeed and return parseable
  payloads.

### Tier 2 — Patch RAM (`.hcd`) loader + baud switch

- Decide the firmware-blob staging path (probably embed into the
  image initially via `phoenix-rtos-build` blob include, switch to
  `/lib/firmware/brcm/BCM4345C0.hcd` via dummyfs once that
  infrastructure is in place).
- Implement the patchram sequence:
  1. `HCI_Reset` (0x0c03)
  2. VSC `Download_Minidriver` (0xfc2e)
  3. Loop `Write_RAM` (0xfc4c) records from the `.hcd`
  4. `Launch_RAM` (0xfc4e)
  5. Settle ~50 ms
  6. `HCI_Reset` again
  7. VSC `Update_Baudrate` (0xfc18) at 3 Mbaud
  8. Reconfigure UART to 3 Mbaud after a tcdrain-equivalent
- Validate: `READ_LOCAL_VERSION_INFORMATION` post-patch reports
  the patched LMP subversion (matches a known-good Pi 4 BT
  baseline) and `READ_BD_ADDR` returns the actual chip MAC (not
  `00:00:00:00:00:00`).
- This tier produces a **usable HCI controller**. Any host stack
  can layer on top.

### Tier 3 — Host stack (choice deferred)

Three options, ranked by realism:

A. **Defer.** Ship Tier 0–2 only. Expose `/dev/hci0` as a raw H4
   chardev. External host (x86 laptop) runs BlueZ pointing at the Pi
   over USB-over-IP or a TCP tunnel. Smallest delivery; useful for
   radio bring-up CI but no production user-facing BT.

B. **Port Apache NimBLE host.** Apache-2.0, ~30k LoC, BLE-only (no
   Classic). Designed RTOS-agnostic. Phoenix would need to provide
   a tiny porting layer (mutex/semaphore/timer wrappers, OS event
   loop hook). Useful if the project's BT use cases are all LE.

C. **Port BTstack `port/raspi`.** Dual-mode (Classic + LE), proven
   on Pi 3-class BCM4345 silicon. Licence is non-commercial-only;
   commercial licensing required for shipping product, which gates
   the public release. Pi 4-specific entry doesn't exist upstream
   but Pi 3B+ entry is close enough that adaptation is straightforward.

Recommendation: **defer (option A) until WiFi is done**, then
revisit with the question "do we need BT at all for the Phoenix-RTOS
Pi 4 demo?". If the answer is yes-and-LE-only, NimBLE. If
yes-and-Classic-also, BTstack with the licensing caveat noted.

### Tier 4+ — optional

- Coexistence parameter post-patch (vendor coex tuning command —
  required on Pi 4 firmware r177+ for stable BT when Wi-Fi is
  active).
- HCI socket exposure (`/dev/hci0` as the Linux-style HCI socket
  ABI) for any future port of `bluetoothctl` or similar.
- Power-state hooks (BT_REG_ON gating during suspend).

## Risks / unknowns

- **`expgpio` schematic cross-check.** The Pi 4 reduced schematic
  shows the expander; the exact register layout for the I2C-side
  control may be undocumented outside Linux's `pinctrl-rpi-exp-gpio`.
  Confirm via VideoCore mailbox first (which abstracts this) before
  considering a direct I2C bridge driver.
- **`.hcd` redistribution licence.** `BCM4345C0.hcd` lives under
  Cypress's EULA via `linux-firmware`. Check before bundling into a
  public Phoenix image. TFTP-loading during dev avoids the question
  but kicks it to deployment.
- **PL011 driver ownership.** Phoenix currently runs PL011 as the
  console driver in some configurations. If `dtoverlay=miniuart-bt`
  is active, console is on mini-UART (separate driver) and PL011 is
  free — confirm via test cycle on the current image.
- **Pi 4 BT firmware variant.** Two variants exist in the wild
  (BCM4345C0 / BCM4345C5). `READ_LOCAL_VERSION` on the lab board
  will tell us which `.hcd` to bundle.

## Validation tooling already in place

- **Tier 5c diag UDP responder.** Once BT is up and Phoenix has a
  thread reading HCI events, that thread's cpuTime + state is
  visible via the `t` probe like any other Phoenix thread.
- **Watchdog reboot.** Speeds up the patchram→reset cycle if we
  ever wedge the controller during development.

## Sequencing relative to other work

This plan is **not currently a task in the loop**. WiFi Tier 1c is
the next concrete Pi 4 driver step. Once WiFi is at the point where
the `SET_GPIO_STATE` mailbox helper + the firmware-blob staging path
are landed (target: WiFi Tier 2-3 ish), BT Tier 0 becomes a 1-iteration
add-on. The rest follows.

For now, **document the plan and move on**. The relevant Phoenix
research briefs already in tree:

- `docs/research/bluetooth-bcm43455.md` (Linux reference)
- `docs/research/bluetooth-bcm43455-non-linux.md` (BTstack / NimBLE
  / FreeBSD / NetBSD / InternalBlue / bare-metal-Pi survey)

Read both before starting Tier 0.
