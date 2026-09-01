# Was deleting upstream's lwip `wi-fi/` subtree right? (analysis, 2026-09-01)

Owner question: *was the deletion needed? Couldn't we model our WiFi on upstream's
work (approach + code location)? How different is theirs from ours? Are we
reinventing the wheel?*

## Verdict: the deletion was NOT needed. Restore it.

`wi-fi/` is **fully opt-in upstream** — the top-level Makefile compiles it only
under `LWIP_WIFI_BUILD=yes`:

```
ifeq (${LWIP_WIFI_BUILD},yes)
CFLAGS += -Iwi-fi/hal -Iwi-fi/lwip -Iwi-fi/whd
include wi-fi/{hal,whd,lwip}/Makefile
```

So it never entered our build. Deleting it bought **zero** build/size benefit and
cost: (a) ~49 k of the ~49.2 k deletions that make our fork diverge from upstream,
and (b) the loss of a working reference for the exact layer we are about to write.
The recorded reason was release tidiness ("WiFi unsupported, so it doesn't ship"),
not licensing or secrecy — and it is public upstream code regardless. The real EULA
concern is the **firmware blobs**, which are handled separately and correctly
(gitignored `.firmware/` + `scripts/stage-bcm43455-firmware.sh`).

**Action:** in the lwip de-tangle, branch from upstream and do **not** re-delete
`wi-fi/`. Leave it opt-in and unbuilt.

## What upstream has vs what we built

| Layer | Upstream (`wi-fi/`) | Ours |
|---|---|---|
| Chip/bus driver | Cypress **WHD** (77 files) + **cyhal** (22) | from-scratch SDIO → SDPCM/BCDC → ioctl/event path (`devices/wifi/rpi4-wifi`) |
| Chips supported | `chip_resources[]` = 43430, 43439, 0x4373 — **no 43455** | BCM43455 (Pi 4) |
| lwip integration | `wi-fi/lwip/`: `cy_lwip.c` (netif), `cy_network_buffer_lwip.c` (pbuf↔driver buffers), `cybsp_wifi.c`, plus a DHCP *server* for AP mode | **not built yet** — this is the next step |

**Are we reinventing the wheel?**
- **Lower layer: no.** WHD has no 43455 entry (and its chip constants would be wrong:
  `CHIP_RAM_SIZE`/ATCM defaults do not match this part), so it cannot drive the Pi 4
  radio. A custom path was required.
- **Upper layer: yes, we would be** — if the netif were hand-rolled. `cy_lwip.c` is
  the canonical shape and we should follow it.

## Licence constraint (important)

`wi-fi/lwip/cy_lwip.c` header: *"Copyright 2021, Cypress Semiconductor … you may use
this Software only as provided in the license … non-transferable license to copy,
modify, and compile"*. That is a **restrictive vendor licence, not Apache-2.0**.

⇒ **Model the architecture; do not copy the code** into `phoenix-rtos-devices`
(which we intend to keep upstreamable). This matches the owner's own framing
("even if no code reuse — at least the approach and code location").

## The netif design we should copy (structure only)

From `cy_lwip.c` / `cy_network_buffer_lwip.c`, the seam a WiFi driver must present:

1. `wifiinit(netif)` — set `netif->output = etharp_output`,
   `netif->linkoutput = <driver tx>`, HW address from the driver's
   `get_mac_address`, flags `NETIF_FLAG_BROADCAST|ETHARP|LINK_UP`, MTU 1500.
2. `linkoutput(netif, pbuf)` — refuse unless the driver is "ready to transceive"
   (associated **and** keyed), then hand the ethernet frame to the driver.
3. RX: driver thread → allocate a pbuf → `tcpip_input(p, netif)`.
4. Buffer bridge: alloc/free + **room to add/remove headers at the front**
   (`cy_buffer_add_remove_at_front`) — SDPCM/BDC headers get prepended, so the
   RX/TX buffers must reserve front slack.
5. Interface lifecycle: `add_interface` / `network_up` / `remove_interface`.
6. Optional later: multicast (IGMP/MLD) filter hooks.

Mapping to what we already have in `rpi4-wifi`: `get_mac_address` ← `cur_etheraddr`;
`ready_to_transceive` ← join state (`setssid==0 && psksup==6`); TX ← generalise
`diag_wifiDataTx` from "build a DHCP packet" to "send an arbitrary ethernet frame";
RX ← promote the ch2 drain in `diag_wifiRxDhcp` into a thread that delivers frames
upward instead of parsing DHCP itself.

## Performance testing (owner ask) — necessarily two stages

Holding a DHCP lease is a handful of small frames; it is **not** a throughput result.
Real numbers need sockets, i.e. the netif:

1. **Now (optional, cheap):** raw data-path ceiling — blast N frames through the
   driver TX and time it. Bounds anything the netif can reach; no lwip needed.
2. **After the netif:** real TCP/UDP throughput (iperf-style, and NFS-over-WiFi),
   comparable to the wired baseline (~30 MB/s read / ~20 MB/s write over gigabit).
   Report the negotiated PHY rate alongside, as WiFi rate adapts.
