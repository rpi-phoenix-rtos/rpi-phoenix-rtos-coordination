# Eth Tier 3 complete — GENET RX one packet (polled)

Hardware: Raspberry Pi 4-B, BCM2711 GENET v5 + BCM54213PE @ 100 Mbps full duplex.
Captured: `artifacts/rpi4b-uart/rpi4b-uart-20260525-06*-tier3-rdma-fix.log`.

## The single root-cause bug

For weeks the symptom was: RX-side init looks fine, TX works on the
wire (verified via host tcpdump), HW is configured (UMAC_CMD=0x17,
RDMA_CTRL=0x00020001, ring 16 enabled, BD addresses programmed) — and
yet the SW-readable `RDMA_RING_PROD_INDEX` stayed at 0 across multiple
inbound frames.

**The bug:** RDMA and TDMA have **mirrored layouts** for their
producer/consumer pair within the per-ring 0x40 register slice:

| Offset | TDMA           | RDMA          |
|-------:|----------------|---------------|
| 0x00   | READ_PTR       | **WRITE_PTR** |
| 0x08   | CONS_INDEX (HW)| **PROD_INDEX (HW)** |
| 0x0C   | PROD_INDEX (SW)| **CONS_INDEX (SW)** |
| 0x2C   | WRITE_PTR      | **READ_PTR**  |

The driver was using `GENET_TDMA_RING_PROD_INDEX = 0x0C` for BOTH
DMA blocks. So the RX poll thread read offset 0x0C — which on the
RDMA side is the SW-writable CONS_INDEX — and waited for it to
advance. It never did, because SW never wrote there. HW was writing
PROD_INDEX at offset 0x08 every time a frame arrived; we just
weren't reading it.

The fix landed in `agent/rpi4-genet` commit `f5f40af` (lwip):

- Add a separate `GENET_RDMA_RING_*` set of macros in
  `bcm-genet-regs.h` with the corrected mirrored layout.
- Switch the RX init writes and poll thread reads to use them.
- TX init / xmit keep the original `GENET_TDMA_RING_*` macros.

A documentation comment box at the top of the per-ring section in
`bcm-genet-regs.h` explains the gotcha so the next reader doesn't
fall into it.

Reference: Linux's `drivers/net/ethernet/broadcom/genet/bcmgenet.h`
splits these as `TDMA_*` vs `RDMA_*` macros for exactly the same
reason. None of the public docs called this out as a footgun, but the
header makes the asymmetry obvious once you look for it.

## How the bug was found

Three rounds of diagnostic prints in `genet_rxPollThread`:

1. **Round 1**: poll PROD/SW_C every 1 s for 10 s. Showed PROD=0
   throughout. Conclusion: HW isn't producing.
2. **Round 2**: also read BD[0].status. Showed it changed from
   0x00000000 to 0x007e7f80 the moment our smoke-TX completed. So HW
   IS doing RX-completion work into BD memory.
3. **Round 3**: read BD[0]'s addr_lo + addr_hi (verify our addresses
   survive) and BD[1].status (verify HW touches more BDs). Showed
   BD[1].status changed to 0x00e27fa0 later in the capture — a real
   226-byte multicast frame.

Round 3's data forced the question: "HW is writing to BDs, but
PROD_INDEX doesn't move — am I reading the right register?"
Cross-checked against Linux's `bcmgenet.h` and the mirrored layout
was right there.

## Other fixes in the Tier 3 commits

Many of these aren't strictly necessary on their own, but they
match Linux + Circle exactly and would have been needed once any of
them hit a real edge case:

- `SYS_PORT_CTRL = PORT_MODE_EXT_GPHY` (required for Pi 4's external
  BCM54213PE — without it, TX descriptors are consumed but no frame
  ever reaches the wire).
- **Do NOT set `DMA_OWN` bit on TX BDs.** It's a RX-only convention
  on GENET; setting it causes TDMA to consume the BD but never push
  the frame out.
- `EXT_RGMII_OOB_CTRL.OOB_DISABLE` MUST be set (force link from the
  RGMII_LINK bit, don't read non-existent OOB pins).
- `RBUF_CTRL |= RBUF_ALIGN_2B | RBUF_64B_EN` (Linux init_umac order).
- `RBUF_CHK_CTRL |= RBUF_RXCHK_EN | RBUF_L3_PARSE_DIS`.
- `RBUF_TBUF_SIZE_CTRL = 1`.
- `RDMA_RING_XON_XOFF_THRESH = (5 << 16) | 16` (Linux defaults). The
  default 0 means "always XOFF".
- `UMAC_MAX_FRAME_LEN = 1536`.
- Linux-style two-phase DMA enable: ring-enable first, then global
  `DMA_EN`.
- Full Linux `bcmgenet_{rdma,tdma}_disable` handshake at init: clear
  enables, poll `DMA_STATUS` until the engine reports stopped.
- Pre-program all 256 BDs of the default queue (BDs [16..255] alias
  back to the 16 unique dmammap'd buffers cyclically) and set
  `END_ADDR = 767`. Setting `END_ADDR` to span only 16 BDs made HW
  set the WRAP bit on the first frame, which appeared to suppress
  PROD_INDEX updates.

## Operational lesson

Test cycles for hardware-side bring-up need ≥180 s capture window
(the inner picocom watchdog starts before Pi power-on, so 30–60 s
of the window is consumed by setup). The Bash tool's default 120 s
timeout will also silently SIGTERM the cycle before lwip prints
reach the log. Both rules are now in `CLAUDE.md` under "Test cycles:
pick `--capture-secs` for what you need, pass Bash `timeout`".

## Files touched this tier

- `sources/phoenix-rtos-lwip/drivers/bcm-genet.c`
- `sources/phoenix-rtos-lwip/drivers/bcm-genet-regs.h`
- `scripts/test-cycle-netboot.sh` (post-cycle health table that
  surfaces the "caller truncation" symptom loud and clear)
- `CLAUDE.md`
- `memory/feedback_test_cycle_timeout.md`
- `memory/project_genet_tier3_open.md` (will be flipped to a
  completion note after manifest)

Integration manifest: `manifests/2026-05-25-eth-tier3-rx-ok.md`.

## Next step

Tier 4 (lwIP integration + DHCP + ICMP). The RX path now needs to
hand frames to `netif->input` instead of just printing them; need to
allocate a `pbuf`, copy the frame body (starting at `buf+64`, length
`status[27:16] - 64`), and call `tcpip_input(pbuf, netif)`. Plus
remove the smoke-test TX (Tier 4's ARP/DHCP traffic will replace it
naturally) and remove the 1 Hz RDMA diagnostic spam.
