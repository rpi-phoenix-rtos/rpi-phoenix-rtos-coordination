# Making WiFi & Bluetooth first-class in Phoenix-RTOS — design

**Task T-WIFI-BT** (owner directive 2026-08-10: "think on how to integrate WiFi and BT with
Phoenix — config files, CLI utils, support for different BT devices. Maybe separate subprojects
(like phoenix-rtos-usb) or additions to -devices/-tools. Make these two new big features
first-class citizens"). This is the design deliverable; implementation is staged below. All
integration points are grounded in current code (file:line).

## Starting point

Both BCM43455 radios already work, but only as one-shot `tools/` probes (mmap MMIO + `printf`
telemetry, run once from psh, no OS integration):
- **WiFi** (`tools/wifi-probe/`, 2666 lines): SDIO enum → firmware boot → BCDC ioctl API → CLM
  blob → escan → **scans 16 real APs** ([[project_wifi_fw_exec_gate_91]]).
- **BT** (`tools/bt-probe/`, 462 lines): mini-UART H4 → patchram (323/323) → real BD_ADDR → HCI
  Inquiry ([[project_bluetooth_bringup]]).

"First-class" = each becomes a normal Phoenix subsystem: a resident driver/server exposing a
device node, driven by CLI utilities, configured from files at boot, using the same patterns as
the existing ethernet/tty/gpio subsystems — not a probe you run by hand.

## The four integration surfaces (how Phoenix already does this)

1. **Network drivers** are lwip `netif_driver_t` structs (`phoenix-rtos-lwip/include/netif-driver.h:31-42`:
   `init`/`state_sz`/`name`/`media`/`stats`) registered via `register_netif_driver()`
   (`drivers/netif-driver.c:45`) from a C constructor — genet does exactly this
   (`drivers/bcm-genet.c:1447-1459`). The lwip server's command line **is** the interface table:
   each `argv` is a `"driver:cfg"` spec fed to `create_netif()` (`port/main.c:140-147`;
   `netif-driver.c:118-185`). DHCP-vs-static is per-driver (genet compiles in `dhcp_start`,
   `bcm-genet.c:1008`); static is a post-boot `ifconfig`/`SIOCSIFFLAGS` action
   (`port/sockets.c:381-403`). **The vestigial WHD `init_wifi()` slot (`port/main.c:136`,
   `LWIP_WIFI_BUILD=no` everywhere, `wi-fi/` absent) must NOT be reused.**
2. **Character-device drivers** are userspace servers: mmap MMIO, `portCreate`, `create_dev(&oid,
   "name")` → `/dev/name`, then an `msgRecv` loop over `mtOpen/mtRead/mtWrite/mtGetAttr/mtDevCtl`
   with `ioctl_unpack`/`ioctl_setResponse`. Canonical minimal example: the Pi's own
   `phoenix-rtos-devices/gpio/rpi4-gpio/rpi4-gpio.c:144-234`.
3. **CLI utilities** are psh applets: `phoenix-rtos-utils/psh/<name>/`, a `psh_appentry_t`
   (`psh/psh.h:21-26`) registered by a constructor calling `psh_registerapp` (`psh/psh.c:49`);
   `ifconfig` is the template (`psh/ifconfig/ifconfig.c:670`). Each is both a psh builtin **and**
   `/bin/<name>`.
4. **Boot config** has two mechanisms: (a) plo syspage args (`user.plo.yaml` `app ... -x prog;args`
   — what rpi4b uses today; program args only, **no secrets**); (b) a BSD-style `rc`/`rc.subr` +
   `/etc/rc.conf.d/*` already in the tree (`phoenix-rtos-project/_fs/root-skel/etc/`, with
   `rc.d/network.sh` honoring `network_<if>_mode=dhcp|static`) but **not wired into rpi4b** (its
   `rootfs-overlay/etc/rc.psh` is a minimal hand-rolled script). First-class config means adopting
   (b).

## Recommended architecture

### WiFi = lwip netif driver + `/dev/wifi` control node + `wifi` applet

WiFi is IP networking, so it belongs in the lwip stack exactly where genet is. Add a
brcmfmac-style `netif_driver_t` (`.name = "brcmfmac"`) in `phoenix-rtos-lwip/drivers/` registered
like genet, so it inherits `create_netif`, DHCP, sockets, and `ifconfig` for free. The
802.11-specific control that doesn't fit the ethernet netif (scan, join/WPA2, RSSI) is exposed via
a small `/dev/wifi` node created inside the lwip server (`port/devs.c` `create_dev` pattern) so it
shares the driver's live SDIO/chip state, driven by a new `wifi` psh applet (open `/dev/wifi`,
issue `WIFICTL_SCAN`/`WIFICTL_JOIN` ioctls — same shape as `ifconfig` driving `SIOCxIF*`).

- **Driver body** = the probe's reusable core, extracted into modules (see reuse table): SDIO
  transport, firmware/NVRAM/CLM download, BCDC ioctl API, escan. Structural template =
  `genet_netifInit` + `genet_linkPollThread` (`bcm-genet.c:1262`/`1075`): init → associate →
  link-up → `dhcp_start`.
- **cfg string** mirrors genet's (`brcmfmac:<sdhci-mmio>:<irq>:...`).
- **Locations:** driver `phoenix-rtos-lwip/drivers/brcmfmac*.c`; control node
  `phoenix-rtos-lwip/port/devs.c`; applet `phoenix-rtos-utils/psh/wifi/`.

### Bluetooth = standalone `/dev/hci0` server + `btctl` applet

BT has no place in the IP stack; it is a classic character device. Make it its own server
following `rpi4-gpio.c`, exposing `/dev/hci0` as a raw **H4 HCI byte stream** (write = HCI packet
out, read = next HCI event) so any future host stack (BTstack/NimBLE) or a `btctl` applet speaks
standard HCI. The server runs power-on + patchram at startup, then serves the byte stream.

- **"Support for different BT devices"** (owner) = two axes: (1) different *controllers* — keep the
  chip/transport behind a small vtable (`hci_transport_t` = open/send/recv) so the BCM43455
  mini-UART backend is one implementation and a future USB-HCI or PL011 controller is another,
  selected by cfg; (2) different *remote devices* — that is the host stack's job (pairing,
  profiles), which `/dev/hci0` enables without baking policy into the driver.
- **Locations:** new `phoenix-rtos-devices/bt/rpi4-hci/` (subproject-style dir, like the owner's
  "similar to phoenix-rtos-usb" suggestion — start as a devices/ subdir, promote to a top-level
  `phoenix-rtos-bluetooth` repo later if it grows a host stack); applet
  `phoenix-rtos-utils/psh/btctl/`; the .hcd firmware stays gitignored (Cypress EULA).

### Config (both) — adopt the rc machinery

Switch rpi4b from the minimal `rc.psh` to sourcing the in-tree `rc`/`rc.subr`, then add:
- `rc.conf.d/wifi`: `wifi_enabled`, `wifi_ssid`, `wifi_psk`, `wifi_mode=dhcp|static` (+ static
  addr vars, reusing `network.sh`'s scheme) → `rc.d/wifi.sh` runs the `wifi` applet then hands the
  netif to the existing `network.sh`.
- `rc.conf.d/bluetooth`: `bt_enabled`, `bt_hcd=<path>`, `bt_name` → `rc.d/bluetooth.sh` starts the
  `/dev/hci0` server. **PSKs live here (a root-only fs file), never in plo syspage args.**

## Probe → driver reuse

| WiFi module (from `wifi-probe.c`) | probe fns | reuse |
|---|---|---|
| `sdio.c` (SDHCI + CMD52/53) | `diag_sdhci*`/`diag_sdioCmd5x` :226-858 | verbatim core |
| `fwload.c` (backplane + fw/NVRAM/CLM dl) | `diag_bp*`/`diag_cr4RamSize`/`diag_eromWalk`/`diag_readShared`/`diag_clmLoad` | core |
| `bcdc.c` (ioctl/iovar + RX demux) | `diag_bcdcCmd`/`diag_iovar`/`diag_f2RecvFrame` :1215-1457 | core |
| `scan.c` (escan + event parse; +join/WPA2 TBD) | `diag_wifiScan` :1502-1714 | core |
| blobs | `wifi-{fw,nvram}-43455.*`, `clm-43455.h` | data |
| drop | `diag_format_sdio_fwrelease` :1714-2630 (telemetry; its call *order* = the driver init reference) + `main` | — |

~55% (~1450 lines) is reusable driver core. **BT** (`bt-probe.c`): `h4-uart.c` (`aux_init`/`aux_putc`/`aux_getc`
:161-198 + GPIO routing), `hci.c` (`hci_cmd` :198), `patchram.c` (`bt_patchram` :233) — ~100 lines core + the .hcd blob; the rest is `main`/telemetry.

## Phased plan (smallest increments first)

**BT first** (more self-contained, no lwip coupling, HW-provable in one cycle):
1. `phoenix-rtos-devices/bt/rpi4-hci/`: wrap `h4-uart.c`+`patchram.c`+`hci.c` in the `rpi4-gpio.c`
   server skeleton — `main` mmaps AUX+GPIO, powers on + patchrams at startup, `create_dev("hci0")`,
   msg loop (`mtWrite`=send HCI packet, `mtRead`=next event). Wire into the rpi4b build + plo.
2. `psh/btctl/`: open `/dev/hci0`, issue `HCI_Inquiry`, print events — the acceptance test
   (mirrors the probe's `bt_inquiry`). **HW gate:** netboot, `btctl scan` prints Inquiry Complete.
3. Transport vtable + `rc.conf.d/bluetooth`.

**WiFi next** (bigger; lwip integration):
1. Extract `sdio.c`+`fwload.c`; a `netif_driver_t` whose `init` boots BCM43455 firmware and
   registers the netif; prove it links into the lwip server (no scan yet).
2. Add `bcdc.c`+`scan.c` behind `/dev/wifi` + a `wifi scan` applet.
3. Join/WPA2 → the driver reaches link-up → the existing `dhcp_start` gives an IP (join is the
   current WiFi NEXT per memory; real-network association stays a 1-step owner-triggered follow-up
   for credential/consent reasons — see [[project_wifi_fw_exec_gate_91]] hard constraints).
4. `rc.conf.d/wifi` + switch rpi4b to `rc`/`rc.subr`.

## Open decisions (flag for owner / later)

- **`/dev/wifi` inside the lwip server vs a standalone wifi server.** Recommend inside lwip (shares
  SDIO+chip state with the netif; avoids a second owner of the SDHCI). Trade-off: grows the lwip
  server. Revisit if WiFi wants its own lifecycle.
- **BT transport:** keep the probe's self-contained mini-UART bit-bang for the first cut vs routing
  through a real `phoenix-rtos-devices/tty` driver. Recommend self-contained first (unblocks
  `/dev/hci0`), refactor to a shared tty later.
- **Subproject vs devices/ subdir:** start both as subdirs (`devices/bt/`, `lwip/drivers/`); promote
  BT to a top-level `phoenix-rtos-bluetooth` repo only if a host stack (BTstack/NimBLE, license
  TBD) lands.
- **rc adoption** touches the rpi4b boot path — do it behind a boot-verify (it changes what runs at
  startup); keep the minimal `rc.psh` as a fallback until the rc path is proven.

## Status (updated 2026-08-10)

Increments delivered since this design was written:
- **BT increment 1+2 DONE:** `phoenix-rtos-devices/bt/rpi4-hci/` resident server exposes
  `/dev/hci0` (raw H4 HCI); `btctl scan` (HCI Inquiry) is the acceptance CLI. HW-validated.
- **WiFi increments 1-2 DONE (as a standalone `/dev/wifi` server, not yet the lwip netif):**
  `phoenix-rtos-devices/wifi/rpi4-wifi/` resident server + `wifi scan` (escan → APs) + open-network
  `wifi join <ssid>`. HW-validated (16 APs).
- **Config files STARTED (2026-08-10, devices `454d449`, pushed org):** `wifi up` reads
  `/etc/wifi.conf` (`ssid=`, INI-lite, forward-compatible `psk=`) and joins the configured SSID.
  Build-verified; the full join is gated on an open AP in range.

Remaining first-class work (all previously flagged; each gated as noted):
1. **psh-applet conversion** — move `wifi`/`btctl` from standalone `/bin` tools to
   `phoenix-rtos-utils/psh/{wifi,btctl}/` applets (needs the utils build + a boot to verify).
2. **Boot integration** — start `rpi4-hci`+`rpi4-wifi` as guarded userspace-rc background services
   (NOT plo syspage — brick risk [[feedback_plo_no_duplicate_program]]); adopt `rc`/`rc.subr` +
   `rc.conf.d/{wifi,bluetooth}`. Behind a boot-verify + rollback (attended-class).
3. **WiFi → lwip netif** — fold the standalone `/dev/wifi` driver into a `brcmfmac` `netif_driver_t`
   so it inherits `create_netif`/DHCP/sockets/`ifconfig` (the §"Recommended architecture" target).
4. **WPA2 join** (EAPOL 4-way + key iovars) — makes `/etc/wifi.conf`'s `psk=` live; owner-gated
   (needs a live AP + credentials; do not scrape the host PSK) [[project_wifi_fw_exec_gate_91]].
5. **BT: richer verbs** (`btctl info` = Read_BD_ADDR/version; `connect <addr>`) + a transport vtable
   for "different BT devices."

Next tractable + verifiable-unattended piece: the psh-applet conversion (build-verifiable) or
`btctl info` (HW-verifiable — reads the real BD_ADDR). Boot-integration + WPA2 stay attended/gated.

[[project_bluetooth_bringup]] [[project_wifi_fw_exec_gate_91]] [[project_pi4_gpio_device]] (rpi4-gpio server template)
