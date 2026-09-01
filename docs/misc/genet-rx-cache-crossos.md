# GENET RX DMA buffer memory + cache maintenance: Phoenix vs Linux / NetBSD / FreeBSD

Read-only cross-OS comparison of how the Broadcom GENET (BCM2711 / Pi 4) RX path
manages DMA buffer memory and D-cache coherency. Goal: test the hypothesis that
Phoenix-RTOS's **uncached** RX pool is the design outlier behind the ~8.5 MB/s
gigabit throughput ceiling, and audit Phoenix's optional cacheable-RX path
(`GENET_RX_CACHEABLE=1`) against the three classic ARM cacheable-DMA-RX bugs.

Sources examined (local flat copies + upstream paths for durability):
- **Phoenix**: `sources/phoenix-rtos-lwip/drivers/bcm-genet.c`, `bcm-genet-regs.h`
- **Linux**: `external/linux/drivers/net/ethernet/broadcom/genet/bcmgenet.c`
  (upstream `drivers/net/ethernet/broadcom/genet/bcmgenet.c`)
- **FreeBSD**: `external/bsd-genet/freebsd_if_genet.c`
  (upstream `sys/arm64/broadcom/genet/if_genet.c`)
- **NetBSD**: `external/bsd-genet/netbsd_bcmgenet.c`
  (upstream `sys/arch/arm/broadcom/bcmgenet.c`)

---

## 1. Per-OS RX comparison table

| Property | Linux | FreeBSD | NetBSD | Phoenix (default `=0`) | Phoenix (`GENET_RX_CACHEABLE=1`) |
|---|---|---|---|---|---|
| **RX buffer memory** | **Cacheable** (skb from `__netdev_alloc_skb`, streaming DMA) | **Cacheable** (mbuf cluster `m_getcl`, streaming DMA) | **Cacheable** (mbuf cluster, streaming DMA) | **Uncached** (`dmammap`) | **Cacheable** (`dmammap_cached`) |
| **Device non-coherent?** | Yes — genet DT node has **no** `dma-coherent` (bcm2711.dtsi:587-594), so `dma_map/unmap` do real cache ops | Yes (busdma non-coherent tag) | Yes (busdma non-coherent tag) | n/a (uncached ⇒ no maintenance) | assumed non-coherent |
| **sync_for_device (before arming BD)** | `dma_map_single(...DMA_FROM_DEVICE)` in `bcmgenet_rx_refill` (2267-2268) | `bus_dmamap_sync(...PREREAD)` in `gen_mapbuf_rx` (1563) | `bus_dmamap_sync(...PREREAD)` in `genet_setup_rxbuf` (275-277) | none | `genet_dcacheCleanInvalRx` clean+inval before arming (init 752; re-arm 1039) |
| **sync_for_cpu (after DMA, before read)** | `dma_unmap_single(...DMA_FROM_DEVICE)` via `bcmgenet_free_rx_cb` (1902-1903), then read `skb->data` (2349) | `bus_dmamap_sync(...POSTREAD)` in `gen_rxintr` (1390-1391) before `mtod` read (1397) | `bus_dmamap_sync(...POSTREAD)` in `genet_rxintr` (752-755) before enqueue (779) | none | `genet_dcacheCleanInvalRx` clean+inval at 1013, **after** PROD read + barrier, **before** any frame read |
| **Invalidate is pure or clean+inval?** | pure invalidate (DMA_FROM_DEVICE unmap) | pure invalidate (POSTREAD) | pure invalidate (POSTREAD) | n/a | **clean+invalidate** (`dc civac`) — forced: `dc ivac` traps at EL0 (bcm-genet.c:254-274) |
| **Descriptor status word read from** | DMA'd 64B status block in `skb->data` after unmap (2349-2350) | DMA'd 64B statusblock via `mtod` after POSTREAD (1396-1398) | **MMIO** desc reg `GENET_RX_DESC_STATUS` (716) — no cache concern | MMIO desc reg (925) | MMIO desc reg (925) — no cache concern |
| **RX buffer size / slot** | `RX_BUF_LENGTH` 2048 (bcmgenet.c:53) | `MCLBYTES` 2048 | `MCLBYTES` 2048 (CTASSERT :61) | `GENET_MAX_FRAME` 2048 (regs.h:337) | 2048, one whole slot per op |
| **Buffer alignment vs cache line** | skb data cacheline-aligned (`ARCH_KMALLOC_MINALIGN` / `SKB_DATA_ALIGN`); map/unmap whole buffer | DMA tag align **4** (if_genet.c:642); `m_adj(ETHER_ALIGN)` **before** map (1543) ⇒ DMA starts mid-line, but cluster is exclusively owned | RBUF_ALIGN_2B set (:420), DMA to cluster base, `m_adj(ETHER_ALIGN)` **after** RX (777) | pool page-aligned, slots at `i*2048` ⇒ 64B-aligned + full-line | same: every slot 2048B, page-aligned pool ⇒ line-aligned + whole-line padded |
| **2-byte RX prefix (ETH_PAD / align)** | `NET_IP_ALIGN`; 64B RSB prefix in-buffer | `ETHER_ALIGN`=2; 64B statusblock in-buffer | `ETHER_ALIGN`=2; RBUF_ALIGN_2B | `RBUF_ALIGN_2B` on, `RBUF_64B_EN` off ⇒ `GENET_RX_STATUS_PREFIX`=2 (regs.h:335, bcm-genet.c:825) | same 2-byte prefix |
| **Ordering barrier around PROD/ownership** | inside `dma_unmap_single` | inside `bus_dmamap_sync` | inside `bus_dmamap_sync` | explicit `dmb ld` after PROD read (920) | `dmb ld` (920) + `dsb sy` bracketing civac loop (286-290) |

**Hypothesis confirmed.** All three mature OSes DMA RX payload into **cacheable**
memory and use streaming-DMA cache maintenance (invalidate on completion, clean
before hand-off). **None** uses uncached/coherent RX buffers. Phoenix's default
uncached pool (`GENET_RX_CACHEABLE=0`, bcm-genet.c:96-107, allocated at 719) is
the outlier: it eliminates cache maintenance but forces the entire TCP/IP receive
chain to touch every payload byte uncached, which is consistent with the observed
~8 MB/s app-drain ceiling noted in the flag comment (bcm-genet.c:101-102).

---

## 2. Correct cache-maintenance sequence Phoenix SHOULD use for cacheable RX

Phoenix **already implements** the textbook sequence under `GENET_RX_CACHEABLE=1`.
For the record, the correct streaming-DMA-RX ordering is:

1. **Before arming a BD** (`sync_for_device`): clean (+invalidate) the slot's
   cache lines so no dirty CPU line can later write back over the frame the
   device is about to DMA. Phoenix: `genet_dcacheCleanInvalRx` at init
   (bcm-genet.c:752) and on re-arm of a spare (1039).
2. **Detect DMA completion via the producer index** read from MMIO
   (bcm-genet.c:905), then a **load-load barrier** so payload reads cannot be
   hoisted ahead of the PROD observation. Phoenix: `dmb ld` (920).
3. **Before the CPU reads the frame** (`sync_for_cpu`): invalidate the slot's
   lines so the CPU sees device-DMA'd RAM, not a stale/speculatively-filled line.
   Phoenix: `genet_dcacheCleanInvalRx` at 1013, correctly placed after step 2 and
   before every read of the buffer.
4. **Invariant that keeps step 3 safe:** no CPU write to a slot may occur between
   arming its BD and its `sync_for_cpu`. Phoenix upholds this — the only CPU write
   to a slot is the 2-byte ETH pad at 1029-1030, which happens *after* the
   invalidate at 1013.

EL0 constraint (bcm-genet.c:254-274): pure `dc ivac` is EL1-only and **traps at
EL0**, so Phoenix must use `dc civac` (clean+invalidate) at both sync points. Using
clean+invalidate for `sync_for_cpu` (where Linux/BSD use pure invalidate) is safe
here **only because** invariant (4) guarantees no dirty line exists at that point —
so the "clean" is a no-op and cannot write a stale line back over DMA'd data. This
should stay documented; it is a genuine deviation from the classic rule that is
valid solely under this driver's write discipline.

**Reframed alignment invariant (from the FreeBSD evidence):** the property mature
drivers actually maintain is *"no CPU-written data shares a cache line with any
DMA'd region,"* achieved by exclusive whole-buffer ownership — **not** strict
start-of-buffer line alignment. FreeBSD's DMA target even starts mid-cache-line
(align 4, `m_adj` before map) yet is correct because each cluster is owned
exclusively. Phoenix satisfies this invariant *more strictly* (every 2048B slot is
itself line-aligned and whole-line padded), so it has more headroom than the BSDs,
not less.

---

## 3. Verdict: does Phoenix's cacheable path have bugs (a)/(b)/(c)?

**(a) Invalidate must be AFTER DMA completion and BEFORE CPU read (not
invalidate-only-at-recycle): NOT PRESENT / correct.**
`genet_dcacheCleanInvalRx` for `sync_for_cpu` is at bcm-genet.c:1013, which runs
after the PROD read (905) and the `dmb ld` barrier (920) and before any read of the
frame (zero-copy hand-up at 1025 or copy fallback at 1072). It is a genuine
per-frame `sync_for_cpu`, not an invalidate deferred to buffer recycle. Correct.

**(b) 64B line alignment + full-line padding (no line shared with CPU-written
data): NOT PRESENT / correct by construction.**
The pool is a single page-aligned `dmammap_cached` (bcm-genet.c:717); each slot is
`GENET_MAX_FRAME`=2048 bytes at offset `i*2048` (739-740), so every slot is
64B-aligned and an exact multiple of cache lines. No two slots share a line, and a
whole-slot `civac` never reaches a neighbour (comment 271-274). The one CPU write
(2-byte pad, 1029-1030) lands in the first line of the *same* slot the device DMA'd,
shared with no other owner, and is cleaned again before that buffer is re-armed
(1039). Correct.

**(c) 2-byte RX prefix vs line alignment: NOT PRESENT / correct.**
`GENET_RX_STATUS_PREFIX`=2 (RBUF_ALIGN_2B on, RBUF_64B_EN off; regs.h:335,
bcm-genet.c:825). The device DMAs the frame from slot offset 0 (2-byte pad then L2
header at +2); the CPU later zeroes those 2 pad bytes for lwip's ETH_PAD_SIZE=2 at
1029-1030. Those bytes are in the same slot/line the device wrote and shared with no
other buffer, so there is no cross-buffer false-sharing. A DMA'd in-buffer prefix in
cacheable memory is fine when the sync covers it — FreeBSD relies on exactly this by
reading its 64B statusblock from the mapped buffer after POSTREAD
(freebsd_if_genet.c:1396-1398). Correct.

### Real flaws surfaced (not (a)/(b)/(c), but worth fixing/noting)

1. **`GENET_RXFRAME_LOG=1` + `GENET_RX_CACHEABLE=1` reads stale bytes.** The diag
   block (bcm-genet.c:952-1006) reads frame bytes (`f[12]`, checksum walks) *before*
   the `sync_for_cpu` invalidate at 1013. Under both flags on, those reads hit
   pre-invalidate (stale) cache — the diagnostic itself would be wrong and could
   misdiagnose "corruption." Move the diag after 1013, or gate it off when cacheable.
2. **Copy-fallback path (1065-1087) has no explicit `sync_for_device` on the
   retained buffer.** It is safe *only* because that path never CPU-writes the slot
   (`pbuf_take_at` reads it at 1072; the pad bytes go to the freshly-allocated pbuf,
   not the DMA slot) and the next drain's `civac` at 1013 handles staleness. State
   this invariant in the code: anyone adding an in-place fixup or debug `memset`
   there would create a dirty line and corrupt the next DMA silently.

### Open question (labeled speculation — NOT a finding)

The flag comment (bcm-genet.c:97-106) keeps cacheable RX off because it reportedly
"corrupts the GPU framebuffer under load." That symptom is **not** explained by bugs
(a)/(b)/(c), which are all avoided above. Hypotheses to check before re-enabling,
none yet confirmed:
- **Attribute-mismatched aliasing**: whether `dmammap_cached`'s returned PA is also
  mapped Normal-cacheable-writeback identically everywhere, or whether a second
  (e.g. kernel/uncached) mapping of the same PA creates a mismatched-attribute alias
  (architecturally UNPREDICTABLE on ARMv8).
- **Scanout overlap**: whether the cacheable RX pool PA can overlap the GPU scanout
  high-mem region — the driver already prints the pool PA span for exactly this
  check (bcm-genet.c:732-736); verify it never intersects the plo framebuffer
  triple-buffer range.

Verifying the `dmammap_cached` mapping attributes and PA placement is the next step
before flipping `GENET_RX_CACHEABLE` to 1 on the netboot data path.
