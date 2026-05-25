# Ethernet Tier 5 — IRQ-driven RX + diagnostic cleanup (2026-05-25)

Productionization step for the BCM2711 GENET v5 driver. Tier 4 left RX
on a 10 ms polling thread with a per-frame diagnostic printf; Tier 5
moves RX to interrupt-driven completion and removes the diagnostic
noise that was load-bearing for Tier 3/4 debug.

## What changed in `drivers/bcm-genet.c`

1. **RX completion** — `genet_irqHandler` (interrupt context) reads
   `INTRL2_0_CPU_STAT` masked by `INTRL2_0_CPU_MASK_STAT`, masks the
   pending bits, clears them in `INTRL2_0_CPU_CLEAR`, stores the
   event mask in `state->irq_events`, and returns 1 to wake
   `state->irq_cond`. `genet_irqThread` is a `condWait` loop that
   calls the renamed `genet_drainRxRing` (same body as the old
   `genet_rxPollThread`) when `INTRL2_0_RX_DMA_DONE` is in the mask,
   then re-unmasks the serviced bits via `INTRL2_0_CPU_MASK_CLEAR`.
2. **IRQ registration** — `interrupt(state->irq_general,
   genet_irqHandler, state, state->irq_cond, &state->irq_handle)`.
   The plo config string already supplied `189:190` (= GIC SPI
   157:158 — INTRL2_0 / INTRL2_1 — under the GICv2 backend's
   `SPI_FIRST_IRQID=32` convention); the prior `irq_general` /
   `irq_ring` state fields were reserved for exactly this moment.
   Init masks all bits in both INTRL2_0 and INTRL2_1 before
   registering (Linux's `init_intrl2_set_mask`), then unmasks only
   `INTRL2_0_RX_DMA_DONE`.
3. **Per-RX printf removed.** The Tier 3/4 `RX#N len=… dst=…` line
   was load-bearing during RX bring-up but, at 1 Gbps ICMP load,
   each pbuf was paying a `printf` round-trip into the kernel klog
   path — this was the single largest contributor to ping RTT.
4. **`genet_dhcpStartCb` cleanup.** Dropped the netif-pointer +
   linkoutput-pointer debug prints and the `etharp_gratuitous` return
   code log. The static-IP assignment + a single one-line banner
   remain; the gratuitous ARP call is kept (it populates host caches
   on Tier 5b future-DHCP-failure modes too).
5. **Stats counters.** Added `tx_pkts`, `tx_timeouts` (incremented
   from `genet_linkOutput`'s success / timeout paths) alongside the
   existing `rx_pkts_seen` / `rx_pkts_dropped`. No periodic heartbeat
   yet — they're for gdb inspection or a future stats syscall.
6. **Dead locals removed.** `last_bd0_status` and `ticks` in the old
   poll thread were debug-era state never used in production.

## What did NOT change

- **TX completion stays polled** inside `genet_linkOutput`. The path
  is single-slot synchronous (one in-flight frame at a time); at
  1 Gbps a 1518 B frame drains in ~12 µs, well below the IRQ + cond
  + thread-wake round-trip. An IRQ-driven TX completion makes sense
  only with a free-queue ring deeper than 1 — that's a separate
  productionization step.
- **Link state stays on the 1 Hz MDIO poll thread.** The BCM54213PE
  PHY has an `INT_B` interrupt output, but on the Pi 4 board it is
  not routed to a GIC SPI — Linux and U-Boot both poll MDIO. We
  would not change that without a board variant exposing the line.
- **PROMISC stays on** in `UMAC_CMD`. The fallback MAC
  (`02:b8:27:eb:00:01`) is locally administered, so the unicast
  filter path can't be exercised end-to-end until `TODO(TD-Eth-MAC)`
  (mailbox tag `0x10003`) is plumbed.

## Validation

`scripts/test-cycle-netboot.sh --label tier5-irq-rx --capture-secs 360`
ran end-to-end on the lab Pi 4B. The UART log doesn't capture the
lwip userspace prints (post-`fbcon: ok` UART silence, `TD-19`) but
the driver effect is conclusive on the wire:

```
$ ping -c 5 -i 0.5 10.42.0.99
PING 10.42.0.99 (10.42.0.99) 56(84) bytes of data.
64 bytes from 10.42.0.99: icmp_seq=1 ttl=255 time=0.612 ms
64 bytes from 10.42.0.99: icmp_seq=2 ttl=255 time=1.13 ms
64 bytes from 10.42.0.99: icmp_seq=3 ttl=255 time=0.810 ms
64 bytes from 10.42.0.99: icmp_seq=4 ttl=255 time=1.17 ms
64 bytes from 10.42.0.99: icmp_seq=5 ttl=255 time=0.851 ms
5 packets transmitted, 5 received, 0% packet loss
rtt min/avg/max/mdev = 0.612/0.916/1.173/0.210 ms
```

Comparison with Tier 4 (same hardware, same network path):

| Tier            | min     | avg     | max     | notes                       |
| --------------- | ------- | ------- | ------- | --------------------------- |
| Tier 4 polled   | 3.72 ms | 7.38 ms | 16.8 ms | per-RX printf, 10 ms cadence|
| Tier 5 IRQ      | 0.61 ms | 0.92 ms | 1.17 ms | clean, IRQ-driven           |

~8× lower mean RTT, ~14× lower min RTT.

## Manifest

`manifests/2026-05-25-eth-tier5-irq-rx.md`. `phoenix-rtos-lwip` head
`789be33` on branch `agent/rpi4-genet`. All other siblings unchanged
from the Tier 4 snapshot.

## Followups (Tier 5b candidates)

- Autonomous DHCP. On this lwip-port, `dhcp_start` resets the netif
  IP to 0.0.0.0 before the DISCOVER reaches the wire. Needs a
  lwip-port walkthrough rather than a GENET-driver change.
- Real MAC source. Plumb the VideoCore mailbox `GET_BOARD_MAC`
  (tag `0x10003`) so we can drop `CMD_PROMISC` and exercise the
  unicast filter.
- Stats surfacing. The `rx_pkts_*` / `tx_pkts` / `tx_timeouts`
  counters need a route to userspace — either a Phoenix-side stats
  syscall, an `/dev` node, or a SIGUSR1-triggered driver dump.
- IRQ-driven TX completion when a multi-slot TX queue lands.
- Link-state IRQ if a future Pi 4 variant routes the PHY `INT_B`
  to a GIC SPI.

## Followups that became `TODO(TD-…)` markers

The temporary-fixes registry now points at:

- `TD-Eth-DHCP` — static IP fallback.
- `TD-Eth-MAC` — promisc + fallback MAC until VideoCore mailbox is wired.
- `TD-Eth-LinkIRQ` — MDIO poll instead of PHY INT_B.
- `TD-Eth-Stats` — counters exist but aren't surfaced.
