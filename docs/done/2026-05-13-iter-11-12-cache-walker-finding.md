# 2026-05-13 — iter-11/12 cache-enable investigation: walker uses a separate path

## Headline finding

The kernel can read `PMAP_COMMON_KERNEL_TTL3[0]` and `[1]` correctly via
regular `ldr` AFTER `SCTLR.M=1` and `SCTLR.C=1` have been turned on —
the values come out as the descriptors we wrote pre-MMU
(`0x0000000000400703` / `0x0000000000401703` with `pkernel = 0x400000`).
**But the page-table walker still fails with translation-fault-L3 at
`FAR = 0xffffffffc0001890` on the very next high-VA literal load.**

This means the data the kernel sees through its data cache differs from
what the walker sees through whatever path it uses. The PT entry IS in
RAM, IS in the D-cache (we read it), and the walker still reports
"invalid". That's a property of Cortex-A72's translation-walker
hierarchy, not a software hazard we can patch around with `dc ivac` /
`dc civac` / ordering changes.

Image that produced this output: `b597c1f7…1e743807`. UART excerpt
(see `artifacts/rpi4b-uart/rpi4b-uart-20260513-214217-netboot-iter-12-diag-ttl3.log`):

```
X5
T0:0000000000400703
T1:0000000000401703
N
EX=0000000000000004
ESR=0000000096000007
ELR=ffffffffc000077c
FAR=ffffffffc0001890
```

## What iter-7 through iter-12 each tried

| Iter | Change | Image SHA | Result |
|------|--------|-----------|--------|
| 7  | M=1 then C=1 deferred until AFTER kernel PT writes + ivac of PT region | `7dea6c3c…b15e5ae5` | `ESR=0x96000007` `FAR=c0001890` (L3 fault TTL3[1]) |
| 8  | iter-7 + `NC_BLOCK_ATTRS` → Cacheable WBWA | `4c4e2bbe…21a041d3` | Same fault |
| 9  | iter-8 + plo teardown `civac` → `ivac` | `064c10b2…3cc188cb` | Same fault |
| 10 | C=1 immediately after M=1, before any PT writes (cacheable blocks shared between writer and walker) | `7d6f6b1e…b4cb912d` | Same fault |
| 10a | iter-10 + dump TTL3[1] hex via low-PA UART | `3fc6e3ad…d476c9e8` | `ESR=0x96000005` `FAR=fe201018` (L1 fault TTBR0[3] — the low-PA UART block) — even SCRATCH_TT entries become invisible to the walker |
| 11 | All kernel TTL2/TTL3 writes moved BEFORE `SCTLR.M=1` (Linux `__cpu_setup` pattern), C=1 immediately after M | `5569d0c4…7e03810` | Same fault |
| 12 | iter-11 + ivac BOTH before pre-MMU PT writes AND after, just before MMU enable | `ab661501…30124e59` | Same fault |
| 12-diag | iter-12 + post-MMU dump of TTL3[0..1] | `b597c1f7…1e743807` | Read T0/T1 cleanly via ldr; walker still faults |

## Interpretation

Cortex-A72 has a **translation-walker cache** (a small fully-associative
cache inside the MMU walker) plus the unified L2. Walks happen through
both. When SCTLR.C is enabled, walks read PT entries through what ARM
ARM calls the "translation regime cacheability" controlled by TCR
IRGN/ORGN — but the walker may have its own internal state machine that
is **not** invalidated by a plain `dc ivac`, only by `tlbi`.

Our `tlbi vmalle1is` runs before C=1 enable. Walks immediately after
should be cache-cold. The first walk for a new VA should miss in the
walker's cache, miss in L1-D, hit in L2 (where we have our writes via
the cacheable-data write path), and pull the entry. Yet it doesn't.

Possible deeper causes we haven't yet pinned:
1. Walker reads through a **physical** alias that bypasses our cache
   writes (which were Device-nGnRnE pre-MMU, going to DRAM only). If
   the walker reads through the D-cache "from the other side" and the
   line was speculatively filled with zeros earlier, the cache returns
   zero rather than fetching from DRAM. Even though our regular ldr
   sees correct value because... it might see a different cache line
   alias.
2. An A72 erratum specific to PT walks across an SCTLR.C transition
   that we haven't identified. We applied 1319367; there may be more.
3. The TCR field `SH1`/`ORGN1`/`IRGN1` interacts with `SCTLR.SED` or
   other bits in a way we haven't fully traced.

## Where work was paused

* Working baseline image (no kernel cache enable): committed in
  `manifests/2026-05-13-armstub-1319367-final.md`. Boots reliably to
  `(psh)%`.
* Worktree contains the iter-12 kernel diff + iter-9 plo diff but
  these don't currently boot through to user-space — they hit the
  fault documented above. To return to a bootable state:
  ```
  cd sources/phoenix-rtos-kernel && git checkout hal/aarch64/_init.S
  cd ../plo && git checkout hal/aarch64/generic/hal.c
  ```
  Then `./scripts/rebuild-rpi4b-fast.sh` will rebuild the baseline.

## Possible directions for a future session

1. **Read more A72 documentation**: specifically the
   "PoCP / Point of Coherency for Page-table walks" discussion in the
   Cortex-A72 TRM and any errata around translation-walker caches.
2. **Compare with rust-raspberrypi-OS-tutorials ch15/16** — they enable
   M+C+I together on the same hardware. Their `__cpu_setup` ordering,
   barrier choices, and TCR shareability/cacheability bits are the
   reference data point.
3. **Try TCR `SH1/IRGN1/ORGN1 = 0` (Non-Cacheable walks)**: forces the
   walker to always read PT from DRAM, bypassing any cache view. Slow
   but should be architecturally robust.
4. **Try writing each PT entry through `str` AND following each `str`
   with `dsb sy` + `dc cvac`** to push the line all the way to PoC.
   This is non-Linux-canonical but might placate A72's walker cache.
5. **Move SCTLR.M and SCTLR.C into a single MSR write inside a
   tightly-controlled critical section** (only `nop`s between SCTLR
   prep and write, no UART prints) so no speculative walker activity
   happens during the transition.

## Bottom line

Cache enable is still parked. The locked-in baseline (1319367 armstub,
plo M-only, kernel M-only, 4 GB RAM, HDMI to psh, SMP smoke) is the
current shipping configuration. USB+keyboard would be unblocked
mostly by getting cache running (perf) — though strictly speaking the
USB/PCIe enumeration is a separate issue worth investigating
independently of cache.
