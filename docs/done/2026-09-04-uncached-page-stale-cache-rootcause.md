# Stale dirty cache lines on uncached pages — root cause of #67, and a system-wide DMA hazard

**Status: FIXED** (kernel `5d8645f6`). Written 2026-09-04, after the bug survived five
prior "fixes".

## The defect, in one paragraph

Phoenix hands out `MAP_CONTIGUOUS` pages that may have been used by another process
through a **cached** mapping. `_pmap_destroy()` — the process-teardown path — releases
those pages **without any cache maintenance**: its only siblings, `_pmap_enter()` and the
unmap path, both call `_pmap_cacheOpBeforeChange()`, but teardown calls nothing. So dirty
lines survive into the free pool. When the page is next handed to an **uncached** mapping
(a DMA buffer), those lines are still in L1/L2 and are evicted **later**, landing on top of
whatever the new owner wrote through its uncached mapping.

## Why it looked like a GPU bug for months

The visible symptom was V3D "wedging": the GPU stopping mid-frame with an unrecognised
opcode. What actually happened is that Mesa wrote a correct control list into a BO, stale
lines from the page's previous owner were evicted over parts of it, and the GPU then
executed a **half-written list** and stopped at the first invalid byte. The hardware
behaved correctly throughout.

## The fingerprint (what finally identified it)

- Corruption present **at submit**, before the GPU is kicked — so not a GPU fault.
- Damage at **64-byte granularity** — a cache line.
- Damage in **both directions**: one list had a correct terminator and a zeroed head;
  another a correct head and a missing tail. No "emission stopped" story allows both.
- Content was either **zeros** or the **previous owner's texels**.
- The producer was provably innocent: `MMAP_BO` handed Mesa the correct pointer
  (`nmaps == 1`), and the job referenced exactly **one** control-list BO.

## What was excluded first, by measurement

GPU misbehaviour · closed-BO address/VA reuse (a "recently closed BO overlaps this one"
report fired **zero** times across a full bench) · BO handle recycling (fixed anyway; **no
measurable effect**) · CPU-cacheable BOs (V3DV never requests them) · stale MMU TLB
(flushed every submit) · allocation failure (no winsys failure logged in any corrupt boot)
· neighbour overrun (the truncation boundary is invariant; an overrun's would not be) ·
control list split across BOs (the job carries one CL BO) · sub-word stores (CL BOs are
**Normal-NC**, not Device — `hal/aarch64/pmap.c:461-470`).

## The fix

`_pmap_cacheOpAfterChange()` now cleans+invalidates whenever a page is mapped
`MAIR_IDX_NONCACHED` — the data-cache counterpart of the instruction-cache handling
already in that hook. Restricted to Normal-NC (real RAM mapped uncached); Device mappings
hold no lines. Cost is one page flush per uncached mapping, and those are rare (DMA
buffers), versus flushing every page at teardown.

## Results

| | before | after |
|---|---|---|
| #67 torches (pass rate) | **0/15** | **7/7 (100%)** |
| corrupt control lists | ~12 per 8 boots | **0** |
| render timeouts | 19 per 8 boots | **0** |
| yQuake2 binner wedges | ~1 per run | **0** |
| presents | 3,600–3,780 | 3,660–3,870 |

A local `dc civac` in the V3D winsys reproduced the same result and was **removed** once
the kernel fix landed, so no second copy is silently load-bearing.

## What this means beyond V3D — NOT yet measured

Every uncached DMA mapping has the same exposure: **SD, genet, USB, WiFi**. A long history
of intermittent, address-dependent, "HW-marginal" faults in those subsystems is consistent
with this bug class. That is a **prediction from the mechanism, not a measurement** — the
specific historical flakes have not been re-run against the fix. Also worth testing: the
gallium GL `cacheable BO` mode, disabled years ago because it "hangs the GPU/render after
the first cacheable readback" (`v3d_resource.c`), which may be this defect from the other
side.

## Method notes (why five earlier fixes failed, and this one did not)

- Grade a **pass RATE with a run-control** (presents > 0), never a screenshot. A "zero
  drops" boot and a boot that never ran the game look identical otherwise.
- Instrument rather than guess — and then **audit the instrument**. The entry-time check
  was initially reading *before* the submit `dsb`, which could have manufactured a phantom.
- **Beware silent-empty checks.** Four times in one night a tool returned nothing and it
  read like a clean pass: a probe before a barrier, `objdump` not on `PATH`, a git compare
  against a nonexistent `origin/master`, and `ls … | awk … || echo ABSENT` where `||` bound
  to `awk`. Use `[ -f ]` and explicit exit codes.
- Every earlier fix targeted **BO identity** (handles, addresses, VAs, mappings). The
  buffer was always correct; its **contents** were being eaten afterwards.
