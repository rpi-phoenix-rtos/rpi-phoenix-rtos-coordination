# Eth Tier 2 complete — GENET TX one packet (polled)

Hardware: Raspberry Pi 4-B, BCM2711 GENET v5 + BCM54213PE @ 100Mbps full.

## Tier 2 deliverable

UART captured on the third test cycle this evening:

```
lwip: genet@fd580000: TX ring 16 ready (BD 0..255, buf 0000000000026000 phys=0x032ca000)
lwip: genet@fd580000: link up: 100 Mbps full-duplex
lwip: genet@fd580000: smoke-test TX (60 B grat-ARP) result=0
```

`result=0` is `ERR_OK` from `genet_linkOutput`. That means:

1. `pbuf_copy_partial` linearised the smoke-test frame into the
   dmammap'd slot.
2. The BD at index 0 was written with `SOP|EOP|OWN|TX_CRC | (60<<16)`.
3. `TDMA_RING_PROD_INDEX` was bumped to 1.
4. The hardware advanced `TDMA_RING_CONS_INDEX` to 1 within the 100ms
   polling window — i.e. the TDMA engine consumed the descriptor and
   pushed the frame out of the GMII pipeline.

## Implementation notes worth keeping

### TDMA register layout, finalized

Two important things in `bcm-genet-regs.h` that the Tier 0 scout
document didn't have at the right level of detail:

- BDs are at GENET MMIO **offset 0x4000**, 256 × 12 bytes = 0xC00.
- Per-TX-ring control regs at **0x4C00**, 17 × 0x40 = 0x440 slice.
- Global TDMA control regs at **0x5040** (`RING_CFG`, `CTRL`, `STATUS`,
  `SCB_BURST_SIZE`).

The default-queue selection bit pattern in `DMA_CTRL` is
`(1 << (RBUF_EN_LSB + DEFAULT_RING))` — i.e. for ring 16 it's
`1 << 17 = 0x20000`. The `TDMA_EN` bit is at position 0.

### DMA buffer source

Phoenix lwip already had `pktmem` + `dmammap`. `dmammap(GENET_MAX_FRAME)`
returns:

- One page (4 KiB), MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNCACHED | MAP_CONTIGUOUS.
- `va2pa()` gives the physical address — Phoenix maps low-RAM
  contiguous, so we get an address well under 4 GiB (`0x032ca000` in
  the test run), which keeps BCM2711's GENET DMA happy.

### What we're not doing yet

- **Multi-slot ring**: Tier 2 reuses BD index 0 → 255 → 0 with a SINGLE
  payload buffer. That's safe under the polled `linkOutput` because the
  function doesn't return until `cons_index == prod_index`, so the buffer
  is never racy. Tier 5 will widen this for IRQ-driven TX with a
  per-BD payload pool.

- **Real MAC**: still using the locally-administered fallback
  `02:b8:27:eb:00:01`. The bootloader briefly programs the real MAC
  (`dc:a6:32:3c:dd:f1` per the netboot DHCP log) into UMAC_MAC0/MAC1,
  then issues `GENET STOP: 0` which clears those registers before
  kernel handoff. Tier 4's DHCP path will need either VideoCore mailbox
  tag 0x10003 or fdt parsing.

- **UMAC_CMD.RX_EN**: stays off until Tier 3's RDMA ring is live.
  Setting RX_EN without a place to put frames overflows the internal
  RBUF and trips status bits we don't yet check.

### Smoke test caveat

The synthesized 60-byte gratuitous-ARP fires once per first link-up
(`tx_smoke_test_done` flag). Marked `TODO(TD-Eth-Smoke)` for removal
once Tier 4 has DHCP and ARP exercising TX naturally. Without it,
Tier 2 can't be validated standalone because the lwip stack is passive
on an unconfigured netif.

## Files changed this tier

- `sources/phoenix-rtos-lwip/drivers/bcm-genet.c` — TDMA init, TX path,
  speed programming, smoke-test (+213 LoC, mostly TX).
- `sources/phoenix-rtos-lwip/drivers/bcm-genet-regs.h` — TDMA register
  layout + BD status bit definitions.

Integration manifest: `manifests/2026-05-24-eth-tier2-tx-ok.md`.
