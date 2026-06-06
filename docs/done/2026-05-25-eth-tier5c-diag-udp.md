# Eth Tier 5c — net-routed observability via UDP diag responder (2026-05-25)

Resolves `TD-Eth-Stats`. Pi 4 boots go silent on the pl011 UART once
`fbcon` takes over `/dev/console`, which has been kept the project's
runtime debug surface to one HDMI screen + the early kernel klog. With
the GENET driver running we have a much better channel — this commit
makes Phoenix's lwip-port answer a 1-byte UDP probe with a machine-
parseable text dump of per-netif state.

## What landed (lwip `b261265`)

`port/diag-udp.c` — a single source file, ~140 lines:

- `init_diag_udp()` is called from `port/main.c` after `create_netif`,
  gated on at least one netif up. It schedules a `tcpip_callback` that
  runs `udp_new()` + `udp_bind(IP_ANY_TYPE, 9999)` + `udp_recv` on the
  tcpip thread. No separate thread, no socket fd — uses lwip raw API
  (`LWIP_TCPIP_CORE_LOCKING` is on for this port).
- `diag_udp_recv` is invoked per inbound datagram. It frees the
  request pbuf, allocates a 1400-byte reply pbuf, walks `netif_list`
  with `NETIF_FOREACH`, and writes a line per interface:
  - `netif: <name><num> <driver-stats-string>\n` when the driver
    exposes a `stats` callback.
  - `netif: <name><num> (no per-driver stats)\n` otherwise.
- A `gettime()` snapshot at init lets the responder emit `uptime_ms:`
  on every reply.

`include/netif-driver.h` gains an optional `stats(netif, buf, cap)`
field on `netif_driver_t`. NULL-safe — drivers that don't care leave
it out.

`drivers/bcm-genet.c` implements `genet_stats`: emits all four
already-existing counters (`rx_pkts_seen`, `rx_pkts_dropped`,
`tx_pkts`, `tx_timeouts`) plus link state (`up`/`speed`/`duplex`) and
MAC source (`mailbox` vs `fallback`). Wired into `genet_drv` via
`.stats = genet_stats`.

## The loopback trap

First build crashed lwip's tcpip thread on the first probe: 100% ping
loss followed even though the netif had been pinging fine before the
probe. Root cause: `netif_driver(netif)` casts `netif*` to
`netif_alloc*` and reads `((netif_alloc*)netif)->drv`. This relies on
the caller having allocated the netif through `create_netif`, which
wraps it in `struct netif_alloc`. The lwip-created loopback netif has
no such wrapper, so the `drv` read came from memory past the netif
struct — a wild pointer — and dereferencing `drv->stats` SIGSEGV'd
the tcpip thread, wedging all later traffic.

Fix: gate the `netif_driver()` call on `(n->flags & NETIF_FLAG_ETHARP)
!= 0`. Phoenix netif-drivers set that flag (via `genet_netifInit`
adding it during init), the loopback doesn't.

This is a generally-useful pattern: any future code that iterates
`netif_list` and wants to reach Phoenix's `netif_driver_t` MUST gate
on `NETIF_FLAG_ETHARP` or carry its own list of registered netifs.

## Validation

```
$ scripts/test-cycle-netboot.sh --label diag-stats-fix --capture-secs 180

# cold probe at uptime ~20s
$ echo q | nc -u -w 1 10.42.0.99 9999
PHX-DIAG/1
netif: en1 rx=4 rx_drop=0 tx=6 tx_timeout=0 link=1/100Mbps/full mac_src=mailbox
netif: lo0 (no per-driver stats)
uptime_ms: 19985
.

$ ping -c 100 -i 0.01 -q 10.42.0.99   # 100/100, 0% loss

# post-flood probe at uptime ~22s
$ echo q | nc -u -w 1 10.42.0.99 9999
PHX-DIAG/1
netif: en1 rx=105 rx_drop=0 tx=107 tx_timeout=0 link=1/100Mbps/full mac_src=mailbox
netif: lo0 (no per-driver stats)
uptime_ms: 22200
.
```

`Δrx = Δtx = +101 = 100 pings + 1 cold probe`. Counters are live. No
regression on ping path (0% loss alongside diag UDP traffic).

The reported `link=1/100Mbps/full` is the lab's USB-Ethernet bridge
negotiating at 100 Mbps; the GENET + BCM54213PE PHY is gigabit-capable
and the bridge is the limiter.

## Why this matters

- **Unblocks SMP Phase E** if extended to read per-CPU tick counters
  (currently the gating issue is post-fbcon stdout silence; a future
  iteration can wire the responder to a Phoenix /proc-style stats
  source).
- **Validates the lwip stack at the application layer** — first time
  we exercise lwip's raw API + the tcpip thread end-to-end at the
  app level on this port. The diag-udp callback is the smallest
  realistic lwip server in the codebase.
- **Demonstrates the `stats` callback** as the right abstraction for
  any future netif driver (WiFi, etc.) to surface counters.

## Followups (small)

- The reply pbuf is `PBUF_RAM` allocated to 1400 bytes and shrunk via
  `pbuf_realloc`. At more than 5–6 netifs the format could overflow;
  a wrap check would be sensible. Two netifs today, no concern.
- The probe is unauthenticated. Fine for a lab-host development
  environment; would need an access-control gate for an upstream
  release.

## Manifest

`manifests/2026-05-25-eth-tier5c-diag-udp.md`. lwip head `b261265` on
`agent/rpi4-genet`. All other siblings unchanged from the Tier 5b
snapshot.
