# Gigabit NFS/eth performance — decision brief (2026-08-26)

**For:** owner (Witold). **Status:** gigabit fully working + bit-exact + 2.4× faster and **shipped**; all *bounded* optimizations exhausted. The remaining gap to Linux parity needs a **multi-week TCP-stack rearchitecture** — your call on whether/how to invest. This brief has the data and options.

## Where we are (all HW-verified on the real gigabit link, same host/switch/NIC)

| Path | Throughput | % of gigabit line |
|---|---|---|
| **Linux on this Pi4 (NFS read, 1500 MTU)** | **112 MB/s** | 95% |
| Phoenix raw TCP into RAM (lwiperf, no NFS/socket-copy) | 37.5 MB/s | 32% |
| **Phoenix NFS read (dd 128 MB)** — shipped | **18.9 MB/s** | 16% |
| Phoenix NFS baseline (session start) | 7.86 MB/s | 7% |

Session delivered **2.4× NFS / 2.6× raw** via three root-caused, validated fixes:
1. `LWIP_TCPIP_CORE_LOCKING_INPUT=1` — removed the per-packet tcpip-mailbox handoff (~94 µs/frame).
2. `LWIP_CHKSUM_ALGORITHM=3` — wider reads over the (uncached) pbuf; recovered the checksum cost.
3. `GENET_RX_CACHEABLE=1` — cacheable RX pool (Linux/BSD design); bit-exact + GPU+net gates.

## The gap is entirely Phoenix software, and it's per-frame processing efficiency

Linux hits **112 MB/s at standard 1500 MTU** on the same hardware ⇒ the NIC/switch/host/Pi fully support line-rate; the 6× gap is 100% Phoenix stack software. Decomposition:
- **Not the wire / not the window / not loss** (bit-exact, drop=0, window full, RTT 0.4 ms).
- **Not frame size** — jumbo ruled out (the switch drops 9000-MTU frames), and Linux doesn't need it anyway.
- **It is per-frame CPU cost on single threads:** Phoenix's tcpip thread processes RX at ~32 µs/frame (~26k fps → 37.5 MB/s raw). Linux does ~80k fps via NAPI-style batching + multi-core + an optimized hot path. NFS then loses another ~1.8× (37.5→21 MB/s stream) to the socket-recv path (tcpip→app cross-thread pbuf handoff + recv-copy + libnfs RPC parse), which is drain/processing-bound (steady, not backpressure — bigger window/recvmbox won't help).

## Options

| # | Scope | Expected result | Effort | Risk |
|---|---|---|---|---|
| **A** | **Accept 2.4× (18.9 MB/s), stop here** | netboot fully works, 2.4× faster, bit-exact | 0 | none |
| **B** | **Socket-recv path optimization** (cut the 37.5→21 NFS loss: reduce the per-segment tcpip→app handoff + copy; possibly move NFS read onto the raw/callback API or batch recv) | NFS → toward the 37.5 raw ceiling (~2× more, ~37 MB/s) | ~days (bounded-ish; needs socket-recv profiling first, then targeted lwip/nfs-fs work) | medium (touches the socket + nfs-fs read path) |
| **C** | **Raise the raw-TCP ceiling toward Linux** (NAPI-style RX batching to amortize per-frame cost, and/or multi-core RX — lwip's `LWIP_TCPIP_CORE_LOCKING` serializes ALL processing on one thread, so multi-core needs a multi-queue/lockless redesign) | raw → toward 112; NFS follows (with B) | **multi-week** | high (core lwip threading rearchitecture; the just-validated stack is single-threaded by design) |

Note: **B is capped by the raw ceiling (37.5)** — to actually approach Linux's 112 you need **C**. B alone gets NFS to ~37 (still 33% of line); B+C gets toward parity.

## Recommendation

- If NFS-over-netboot at **18.9 MB/s** is adequate for the workflow (it's 2.4× faster and fully correct/stable), **Option A** — declare gigabit done and free the focus for other master-plan items.
- If you want a meaningful further step without a multi-week commitment, **Option B** (~2× more, ~37 MB/s) is the bounded next lever; I'd start by profiling where the socket-recv 21 MB/s goes (copy vs handoff vs libnfs) via a raw socket-recv sink, then target the biggest chunk.
- **Option C** (Linux parity) is a genuine multi-week TCP-stack project (NAPI batching + multi-core lwip). Worth it only if line-rate NFS is a hard requirement; I'd want your go-ahead and preferred approach before starting, given the risk to the working stack.

Awaiting your call. Everything above is committed; the deployed image is the validated 2.4× cacheable build (Option-A state). Harnesses for re-measurement: `$CLAUDE_JOB_DIR/tmp/{ss-measure,iperf-measure,linux-bench}.sh`; lwiperf/RXSTATS/RXPROF diag build flags in the lwip Makefile.
