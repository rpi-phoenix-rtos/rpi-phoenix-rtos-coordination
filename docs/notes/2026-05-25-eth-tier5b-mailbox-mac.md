# Eth Tier 5b — real MAC from VideoCore mailbox (2026-05-25)

Resolves `TD-Eth-MAC` and `TD-Eth-Promisc`. Up to Tier 5 the genet
driver fell back to a deterministic locally-administered MAC
(`02:b8:27:eb:00:01`) because Pi 4 firmware doesn't push the board
MAC into `UMAC_MAC0/MAC1` (unlike Pi 3). PROMISC was kept on so
the unicast filter wouldn't drop traffic destined for the real
board MAC. This commit plumbs the BCM2835 mailbox property
`GET_BOARD_MAC` (tag `0x10003`) directly inside `bcm-genet.c`,
removing both workarounds together.

## What changed (lwip `79bd607`)

`drivers/bcm-genet.c`:

- New `genet_mboxGetMac(uint8_t out[6])` static helper, gated on
  `#if defined(RPI_MAILBOX_BASE_ADDRESS)`. The function:
  - Page-aligns the mailbox base (Pi 4 maps it at `0xfe00b880`, which
    is not page-aligned itself), maps via `physmmap()` for
    `MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM`.
  - Allocates a `dmammap()` page for the property message buffer
    (the VC4 mailbox requires a 16-byte-aligned, uncached, coherent
    DRAM address — `dmammap` is page-aligned + uncached + contiguous,
    which trivially satisfies all three).
  - Builds the property packet for `GET_BOARD_MAC` (32 bytes total,
    8 u32 words: size, REQUEST, tag, val-buf-size=8, req/resp=0,
    val[0..3]=0, val[4..7]=0, END).
  - Writes `(msg_pa & ~0xF) | channel(8)` to `VC_MBOX_WRITE`, polls
    `VC_MBOX_STATUS.EMPTY` clear + `VC_MBOX_READ` for the matching
    request token.
  - On `msg[1] == 0x80000000` (response success), copies bytes
    20..25 (= `&msg[5]` as `uint8_t*`) into `out[0..5]`.

- New `genet_state_t.mac_is_fallback` (bool). Used by
  `genet_macSetSpeed` to set `CMD_PROMISC` only on the fallback
  path; the mailbox path clears it.

- New MAC source priority in `genet_netifInit`:
  1. `genet_readMac` (UMAC_MAC0/MAC1 — empty on Pi 4)
  2. `genet_mboxGetMac` — primary path on Pi 4
  3. LAA fallback `02:b8:27:eb:00:01` + PROMISC on (only if mailbox
     also fails)

- `RPI_MAILBOX_BASE_ADDRESS` comes from `board_config.h`, which is
  already on the include path (greth.c uses it; lwip-port Makefile
  exposes it for all drivers).

## Validation

`scripts/test-cycle-netboot.sh --label tier5b-mbox-mac --capture-secs 180`
plus a host-side ping + ARP dump:

```
$ ping -c 5 10.42.0.99
5 packets transmitted, 5 received, 0% packet loss
rtt min/avg/max/mdev = 0.663/1.003/1.422/0.295 ms

$ arp -n 10.42.0.99
10.42.0.99    ether    dc:a6:32:3c:dd:f1    C    enx00e04c68013a
```

`dc:a6:32:*` is the Raspberry Pi Trading Ltd OUI — the firmware
returned the actual board MAC for the lab Pi 4B. RTT envelope
unchanged from Tier 5 (PROMISC was already off effectively on the
crossover bridge, so no traffic disappeared into broadcast noise).

## Followup: portability

`genet_mboxGetMac` is gated on `RPI_MAILBOX_BASE_ADDRESS`. On non-Pi
targets the gate evaluates false and the stub returns `-ENOTSUP`, so
the driver falls through to the LAA path. No build-time impact on
other GENET-using boards (none in tree today, but the upstreaming
target is to make this driver work across BCM2711-family boards).

## Remaining TD-Eth-*

- `TD-Eth-DHCP` — autonomous DHCP exchange (lwip-port internals,
  not driver). Static IP `10.42.0.99/24` still applied on first
  link-up.
- `TD-Eth-LinkIRQ` — PHY `INT_B` line not routed to GIC SPI on Pi 4;
  driver stays on 1 Hz MDIO link poll.
- `TD-Eth-Stats` — `rx_pkts_*` / `tx_pkts` / `tx_timeouts` counters
  not surfaced outside the driver yet.

## Manifest

`manifests/2026-05-25-eth-tier5b-mailbox-mac.md`. lwip head `79bd607`
on `agent/rpi4-genet`. All other siblings unchanged from the Tier 5
snapshot.
