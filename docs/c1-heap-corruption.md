# C1 — the stray `0x8000000x` write (heap corruption)

The long-form record for issue **C1** in [KNOWN-ISSUES.md](KNOWN-ISSUES.md). The register keeps one
short row; everything learned about the bug lives here.

**Status (2026-09-26):** open, root cause unknown — but now **reproducible on demand**, which it
had never been. Current as of the `c1cc` series and build 2 of the pacing instrument.

---

## 1. At a glance

| | |
|---|---|
| **What** | A 32-bit store of `0x80000000` or `0x80000001` lands at **offset +4 of a page** — in a malloc heap header, a chunk header, or (once) a kernel tree node. |
| **Worst effect** | Halts the kernel (EL1 Data Abort, `c1pfn1`); crashes the app (STK in malloc's own bin tree); or, most often, a heap whose every later `free()` leaks silently. |
| **Where it shows** | Almost only SuperTuxKart (STK 27/552 historically; quake3 1/84; none in quake2/quakespasm/vkquake). |
| **How to make it happen** | **Clear the Mesa shader cache before the run** — see §3. Cold cache: fired in 4 of 5 trials. Warm cache: 0 of 5. |
| **Leading picture** | A **stale physical pointer into a recycled contiguous block** — a device (DMA master) writing back to memory it no longer owns, which malloc has since received. Not proven. |
| **Next instruments** | Build 3: `hpalo/hpahi/hpalast` (is the victim band special?) and `p4age` (when did the write happen?). Then, if the band is special: a kernel-side log of every `MAP_CONTIGUOUS` allocation. |

---

## 2. The signature

**The invariant — the only thing to quote without qualification:** a **32-bit store of
`0x80000000` or `0x80000001` at page + 4.** Three detectors see it, two of them independent of the
poison probe, so "+4" is measured rather than an artefact of where a probe happens to look:

- **Header path** — the high half of `heap->size` (`heap+4`; heaps are page-aligned). Reported as
  `hsize = 0x80000001_0000d000`, `hlo32 = 0xd000`, `hhi32 = 0x80000001`. The low half survives, so
  the heap still reads as a legal `0xd000` heap.
- **Poison path** — a poison word written at page+4 of a freed chunk reads back wrong:
  `p4off = 4`, `p4want = <address-keyed poison>`, `p4got = 0x80000001`. The neighbouring words are
  intact (`p4w=` dump: `0, 0x80000001, 0, 0, 0`) — **one** word, not a structure overwrite.
- **Kernel** — `c1pfn1` took an EL1 abort in `lib_idtreeAugment` (`lib/idtree.c:36`) with
  `far = 0x80000001c4718610`; disassembly shows the corrupted `parent` pointer's high half sitting
  at exactly page+4 of a page-aligned `resource_t`.
- **STK crash** — `far = 0x800000010c845168`: high half replaced, low half intact.

**The value is not constant** (`0x80000000` and `0x80000001`, even within one run), which reads
like a status or lock word with a flag in the MSB rather than a marker. `0x8000000x` is the classic
**OWN/VALID bit of a DMA descriptor status word**, and also the VideoCore property-mailbox
**success / parse-error** response codes.

**The victim size class** is `0xd000` (13 pages) in every attributed case tonight; earlier records
also show `0x2000`. The claims "always `0xd000`" and "always a whole `0x5000` heap" were both
refuted historically.

**The victim physical band.** Twelve distinct victim physical pages, from six logs and two
detectors, all lie in **`0x080db000`–`0x085d4000`** — i.e. **0.86–5.83 MiB above `RAM_ADDR`**
(`0x08000000`, the base of managed RAM). ⚠ Not yet controlled: see §6.

---

## 3. How to reproduce it — the shader-cache knob

**C1's rate is set by whether SuperTuxKart starts with a cold Mesa shader cache.**

`scripts/sync-netboot-tree.sh` runs when every test cycle starts. It clears the Mesa shader disk
cache **iff the GPU driver archives changed** (it fingerprints `libv3d/libGL/libv3dv-phoenix.a`),
and prints `cleared` or `KEPT` into the cycle's driver log. So historically only **the first trial
after a GPU-affecting build** ran cold.

```bash
sudo -n rm -rf /srv/phoenix-rpi4-nfs-gcc16/.mesa-shader-cache    # root-owned: the Pi writes it
# ...then start the cycle as usual
```

| cache at start | trials | race starts | fired |
|---|---|---|---|
| **cleared** | 5 (`c1pace1`, `c1idleI1`, `c1ccC1–C3`) | ~82–84 s ("late") | **4** |
| **kept** | 5 (`c1idleB1`, `K1`, `I2`, `c1ccW1`, `W2`) | ~70–72 s ("early") | **0** |

Cache → mode is **10/10**. The paired `c1cc` series (cleared vs kept, alternating, one binary)
gave COLD 3/3 late + 2/3 fired and warm 2/2 early + 0/2 fired.

**Why it plausibly matters — the heap counts.** Every cold trial plateaus at **~8130–8330 heaps**,
every warm one at **~6500–6780**: recompiling shaders pushes **~1500 extra heap creations (~25 %)**
through the allocator, all in the startup window where the fire lands.

⚠ **Two traps in using the knob:**
- When the cache directory is **absent**, `sync-netboot-tree.sh` prints **nothing** about it (its
  whole message sits inside `if [ -d $shader_cache ]`). Grade the arm from your own `cleared`
  marker, not from the sync's message, or the coldest trial reads as "unknown".
- The cache state is **host-side** — it appears in no UART log. Grade with
  `scripts/c1-idle-table.sh <prefix> <series-driver-log>`; the second argument is required.

---

## 4. What is established

| finding | evidence | strength |
|---|---|---|
| **The cold-cache knob** | §3; 10/10 cache→mode, 4/5 vs 0/5 fired | strong |
| **Fires land ~90 s after the first rendered frame**, not at a frame count and not at race start | 33 archived fires; across the bimodal race-start split the frame count differs 1.9× while elapsed time agrees to 3 %; fire-time-on-race-start slope −0.21 ± 0.38 (rejects race-tracking, p ≈ 0.005); reproduced out-of-sample (88.9 s, 98.6 s, 113.8 s) | strong — but see the caveat below |
| **The fire coincides with the END of heap growth** | pre-registered flatten test passed **3/3**: 4245.8 → 0.0, 2391.0 → 0.0, 4719.5 → 0.3 kB/s; heap creation stops at t ≈ 79–88 s and the fire follows within ~10–25 s | strong for coincidence; causation open |
| **Closed-BO pages are enriched among victims, not exclusive** | 1 of 5 attributed victims sat on a page the V3D driver had closed, against **~2 %** of all heaps (`hbo`/`hbop` = 153–163 of 7920–8331) — ~12× — but P(≥1 of 5 \| 2 %) = 0.096 | suggestive, **n = 5** |
| **Within-run control** | `c1ccC2`: two victims minutes apart, one on a closed-BO page (`p4bopa = 1`), one not (`hbopa = 0`) | one run |
| **Recycling starts long before the fire window** | first pool-declined unmap, first physical-frame reuse and first GPU-VA reuse all at **8.6 s**; first heap on a closed-BO page at **20.8 s** | strong |

⚠ **The "~90 s" is a DETECTION time.** The poison is written when a chunk is freed and read only
when the allocator next walks it — and when heap growth stops, the allocator starts reusing free
chunks, i.e. starts *checking* them. "The fire lands at the plateau" and "checking begins at the
plateau" are currently the same observation. `p4age` (build 3) separates them.

---

## 5. Eliminated, refuted and retracted — do not re-walk

**Eliminated:**
- **Double BO ownership.**
- **The allocator corrupting itself** — host harness: 400 seeds / 7.45 bn chunks single-threaded,
  161 M allocs / 16 threads, 0 violations; the `live[]` ring logic, 200 seeds / 244 763
  mmap-munmap cycles, 0 violations.
- **ralloc `get_gc_block_header`** — a positive control proved the function is reached while C1 is
  rampant and the check stayed silent.
- **The in-process V3D mailbox path, on a firing run** (`c1coin`): victim `hpa = 0x8112000` matched
  none of 20 request-buffer PAs, and the driver's unbounded "timed out AFTER the doorbell" message
  fired 0 times — the only path that returns a page while the firmware still owns it.
- **"The victims are inside the `loader.disk` initramfs"** — `loader.disk` is loaded at
  `0x08000000`, but across every recorded build it has been 4.22–4.62 MiB and two victims sit at
  4.89 and 5.83 MiB above that base.

**Refuted beliefs that shaped earlier work — do not rely on them:**
- ⛔ **"C1 cannot be studied by adding instrumentation."** Disproved by a positive result: an
  instrumented build fired 2/23, tracking baseline exactly. It was a **schedule artefact** (below).
- ⛔ **"The 4-in-10 baseline."** Not in the archive. The measured ~11 % pooled rate is itself a
  **mixture** of ~7 % (warm cache) and ~50–80 % (cold cache), so any power calculation or A/B null
  built on a single rate is mis-stated.
- ⛔ **"`V3D_KEEP_CLOSED_BO=1` suppresses C1, p = 0.019" — the old "one solid fact".** Over the full
  archive it is 0/20, but 18 of those 20 ran with a warm cache. Expected under no effect, using each
  mode's own rate: 2.37 → **P(0) = 0.093, not significant.** Do not run it as an arm unless blocked
  on cache state.
- ⛔ **"The idle gap before a trial sets the rate"** (5.18×, p = 1.6 × 10⁻⁶ in the archive). A real
  association but a **proxy**: long gaps are builds, builds change the GPU driver, the driver
  change clears the cache. Refuted as the cause by a pre-registered trial — `c1idleI2`, 660 s idle
  with a warm cache, came back early and clean.
- ⛔ **"The closed-BO route is eliminated"** — recorded after three negatives, retracted by the
  fourth fire (`p4bopa = 1`). Quote "enriched, n = 5", never "eliminated" or "the cause".
- ⛔ **"The victims are scattered."** Measured over virtual pages on a small sample; physically
  they cluster (§2).

**Explanations of old results:**
- The **25-trial "drought"** of 2026-09-26 was 25 warm-cache trials at ~7 %; ordinary.
- **Instrumented A/Bs looked like suppression** because a build takes ~20 min, so the first trial
  on a freshly built instrumented binary always ran cold, while its control series ran warm.

---

## 6. Open hypotheses and the next instruments

**Working picture (not proven):** BOs are `MAP_CONTIGUOUS`, and a `0xd000` heap is 13 pages — both
are multi-page allocations competing for the same high-order pool, which a buddy allocator
(`vm/page.c`) serves from low memory. That explains the **low physical band** *and* the **BO
enrichment** without the BO being the writer: the writer would be a **stale physical pointer into
a recycled contiguous block**, and a closed BO is merely the one previous owner we happen to track.

**What would move it:**

| question | instrument | status |
|---|---|---|
| Is the 0.86–5.83 MiB band special, or just where *late* heaps land? (`capa` only sees the first 64 heaps — startup, at 58–77 MiB.) | `hpalo` / `hpahi` / `hpalast` on the pace line | **build 3** |
| When did the write happen, versus when was it seen? | `p4age` / `p4tick` — poison-write tick per page, reported at the break | **build 3** |
| Who owned the victim page before malloc, when it was not a V3D BO? | kernel-side log of every `MAP_CONTIGUOUS` allocation's physical range | not started — waits on the band answer |
| Does the late heap burst matter? `c1ccC3` alone had a second burst at t ≈ 290 s in which ~1 new heap in 3 landed on a just-closed BO page (2.4 % at startup). | re-observe with the knob | one event; may explain the archive's late fatal cluster |
| Which DMA masters are live during a run? | PA-logging for genet, ADMA2/eMMC, HVS, rpivid | not started |

**Kept as an open line, weakened:** the `+4` / `0x8000000x` coincidence with VideoCore mailbox
response codes. The *location* argument for a stale firmware pointer is gone (§5), the
coincidence of offset and value is not.

---

## 7. Measurement traps specific to C1

- **Report counts are not event counts.** A corrupted heap is re-reported on every later `free()`
  from it: one run showed `SIG = 552` from **4** distinct heaps. Count distinct victims (`VICT`,
  distinct `p4page` / `hpa`).
- **Grade the cache arm from the cycle's output, not from the label** — or a failed `rm` reads as
  a clean cold trial.
- **A still-running trial has 0 frames.** Do not call it void; the graders now report `RUNNING`.
- **`p4off = 4` on a narrow build is tautological** — `C1_P4_WIDE` is a compile-time define; the
  default build probes only +4. The wide arm (4 offsets) ran 16 trials and never fired.
- **`hbo = 0` is only a negative while the 512-entry closed-BO ring has not wrapped.** With the BO
  pool off (the default), `munc + munp` is the total close count; above 512, zero means "not among
  the last 512 closes". `c1-pace-read.sh` prints the verdict.
- **Three weak symbols can silently read as "nothing":** the pace line prints `heaps=?` /
  `hbo=?` when libphoenix's `malloc_c1Pacing` / `malloc_c1HeapBoHits` did not resolve (stale libc),
  rather than a plausible zero. Treat `?` as a broken instrument.
- **The UART flips ~1.3 % of lines.** A flipped digit still parses (`heapkb 470812 → 47469`), so the
  readers drop non-monotonic pace lines; `sort -u` once invented a second victim address — use
  majority vote.
- **Gate a winsys change on the GPU app, not `loader.disk`**, against the buildroot copy (the NFS
  export is only synced when a cycle starts), and on a string unique to *that* build. `bin/stk` is
  an 872 KB launcher; the real binary is `usr/bin/supertuxkart`.

---

## 8. Tools

| tool | use |
|---|---|
| `scripts/c1-idle-table.sh <prefix> <driver-log>` | per-trial arm, **cache state**, mode, frames, fires; guards for void arms |
| `scripts/c1-pace-read.sh <label>` | heap-growth curve, flatten test, recycling onsets, the fire on every axis |
| `scripts/c1-bench-table.sh <prefix>` | series summary with schedule MODE and per-mode stratification |
| `scripts/c1-fire-position.sh` | archive-wide fire timing (frames, since-first-frame, since-race) |
| `scripts/c1-report.sh`, `scripts/c1-victim-pa-table.sh` | per-run PA correlation; victim physical pages across runs |
| `tools/malloc-harness/` | host harness for `malloc_dl.c`, incl. the pacing-counter cross-check |
| `v3d-winsys: pace` line | `t / frames / boc / bore / var / munc / munp / pool / hbo / hbop / heaps / heapkb` (+ `hpalo/hpahi/hpalast` from build 3) |

---

## 9. Records

- Weekly log, night of 2026-09-25/26: [inprogress/WEEK-2026-W39.md](inprogress/WEEK-2026-W39.md) §0
- Hunt night 2026-09-25: [misc/2026-09-25-c1-hunt-night.md](misc/2026-09-25-c1-hunt-night.md)
- Everything before: [misc/2026-09-25-c1-dossier.md](misc/2026-09-25-c1-dossier.md)
- Rollback point for the pacing instrument: [../manifests/2026-09-26-c1-pacing-instrument.md](../manifests/2026-09-26-c1-pacing-instrument.md)
- The KNOWN-ISSUES cell as it stood before this document replaced it (24 000 characters, verbatim):
  [done/c1-known-issues-cell-2026-09-26.md](done/c1-known-issues-cell-2026-09-26.md)
