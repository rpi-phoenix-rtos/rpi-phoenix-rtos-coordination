# Two-front fixes: NFS exec-EIO + GPU torch/monster race (2026-07-26, in progress)

User mandate: fully fix (1) the torch/monster #67 render glitch and (2) the NFS
boot/exec instability — deterministically, correctly (no band-aids), autonomously.

## NFS exec-over-NFS -EIO — FIXED (committed)

- Root cause (kernel `vm/object.c:232`, `object_fetchCluster`): the exec demand-page
  force path opens each ELF-metadata cluster with `proc_open`; that open **fabricated
  `-EIO`** on any transient and was **not retried**, while its sibling `proc_read`
  already retries transients 25× (nfs_ops.c). One transient blip on the cold open
  aborted the whole ~17 MB exec (`exec ... failed (err=-5)`, ~1/10 nfsroot boots).
- Fix (kernel `c25ed0cb`): never fabricate `-EIO` — propagate `proc_open`'s real
  errno (4a) — and give this one open a bounded, backed-off re-drive matching the
  read path (4b), logging each attempt's real errno. Inert on SD.
- Repro learning: the failure is the **first cold exec right after takeover** (nfs-fs
  warming), ~1/10 boots — NOT accelerable per-boot (a 30×-cold-exec/boot synthetic
  loop gave 0/~75 because the pre-exec lookups warm nfs-fs first). So validation is
  **passive**: every subsequent netboot execs quake and exercises the fix; the
  per-attempt log captures the real errno the first time any boot hits the transient.
- Validation so far: 5 clean torch-baseline boots + earlier boots, all quake-OK, 0
  re-drives fired yet (consistent with ~1/10). Ongoing passive validation.

## GPU torch/monster #67 glitch — mechanism CONFIRMED = consumer render race

Two research subagents (2026-07-26) + a HW discriminator settled the mechanism:

- **Linux uses the same cache ops on V3D 4.2** (`v3d_flush_l3`/`v3d_invalidate_l2c`
  are no-ops ≥4.1/3.3; `v3d_invalidate_caches` = bare L2TFLS + SLCACTL 0x0f0f0f0f),
  byte-identical to the Phoenix winsys. So the fix is NOT a missing cache op. And
  Linux's own comment says the L2T flush needs no completion wait (HW self-stalls) —
  so **"fix-A" (the waited-L2T-flush before the CT0 kick) works only as injected
  LATENCY, not a coherency primitive.**
- **Producer path is deterministic** (VBO is Normal-NC, stable-VA, direct uncached
  memcpy, `dsb sy`-drained, memset-init; kernel flushes the cached alias on
  cacheability change). 
- **HW discriminator (5 fresh cold boots, r_dynamic 0, fixed-timestep demo):**
  - VBO **source** CRC (QVBO) byte-identical across all 5 boots, all 41 models.
  - Yet the door/torch frames still **mangle-vary cross-boot**: F0090 3.0%, F0100
    3.5%, F0110 3.1%, F0120 5.1%, the monster frame F0140 5.5%, F0070 3.2% (3–4
    distinct renders / 5 boots; >30/255 per-pixel). (F0000 26.8% = console warmup,
    discounted.)
  - **Identical input → non-deterministic output = a GPU render RACE, not a data/
    producer bug.** Confirms the fire-and-forget SLCACTL slice-cache invalidate
    (TVCCS/TDCCS, no completion bit on V3D 4.2) is raced by the binner's coordinate-
    shader vertex fetch.
- Size/latency clue: fix-A's latency fixed large models (guns) but not tiny ones
  (torches) — a wrong-DRAM producer bug would not be latency-fixable at all, so this
  independently supports the race.

### RESOLVED via ordering fix (coord 457a650) — supersedes the "deferred" conclusion
The Linux-ordering study (docs/inprogress/2026-07-26-gpu-linux-ordering-analysis.md)
found the difference: Linux leaves the L2T flush **in flight** so the binner
hardware-stalls its first CL read on it, flooring the fire-and-forget SLCACTL
settle window. Phoenix's pre-bin `l2t_flush_wait` REMOVED that interlock, so
SLCACTL got only ~5 MMIO writes of settle before the CT0 kick → the coordinate-
shader vertex fetch raced it. **Fix: issue SLCACTL as the FIRST op after the submit
`dsb`, before mmu_flush_tlb + all the L2T waits — every existing per-submit spin-wait
then becomes free settle latency (zero added fps cost).**

**VALIDATED (HW):**
- `r_dynamic 0`: torch/monster cross-boot variance **3–5.5% → 0.0%** (byte-identical
  across 5 fresh boots), vs baseline (fix-A only) which still mangle-varied.
- `r_dynamic 1` (user's actual setting): worst non-warmup cross-boot diff **0.4%**
  (F0120, sub-perceptual), all else 0.0%, over 5 fresh boots.
- Full-res HDMI (real rpi4-quake, ordering fix): wall torches render as **correct
  flames** (vs pre-fix mangled spikes), monsters/viewmodel/world correct, 0 faults.
- ~10 total fix boots, 0 regressions/wedges; fps unchanged (uses existing waits).

The earlier "deferred, needs a slice-invalidate completion primitive" conclusion in
the 2026-07-24 localization doc is **superseded** — no new HW primitive was needed;
the fix was ORDERING. fix-A retained as belt-and-suspenders latency margin (its
removal is an untested fps optimization, explicitly deferred).

## Harness
- DET quake (`build-quakespasm-det.py`, `external/quakespasm-det`): QVBO source CRC
  (gl_mesh.c), 192×108 full-frame dump every 10th frame (gl_screen.c) → cross-boot
  torch scorer, + `QDET_EXECPROBE` marker-gated exit-at-main for the NFS exec loop
  (compiled only into DET via `-DQDET_EXECPROBE`, never the ship build).
- Torch scorer: 5-boot cross-boot per-frame distinct-count + >30/255 pixel-diff %.

---

## Follow-ups (2026-07-26 pm)

### fix-A removal — ATTEMPTED, REGRESSED, REVERTED
Tried removing fix-A (the extra waited-L2T-flush) since the SLCACTL ordering fix
now settles the slice invalidate. HW result: **1 of 3 boots hit 94 CT1 RENDER
TIMEOUTs** (ct1ca wedged in a stale per-tile sublist, mmu_ill set) — the marginal
binner→render tile-list wedge. So fix-A also provides timing margin that suppresses
that SEPARATE render-side wedge, not just the SLCACTL settle. Reverted (coord
3567f2f); fix-A stays. Its removal is NOT a safe fps optimization.

### NFS exec-EIO — root of the `-34` refined; re-drive extended
The fix-A-removal boots caught the ~1/10 NFS transient (orthogonal to fix-A), and
the unmask (4a) paid off: the real errno is **`-34` = ERANGE**, and it **persisted
through all 8 re-drive attempts (~1.9 s) then still failed** — so the committed
re-drive deadline was too short.
- Root: `-34` is libnfs's **catch-all default** (`research/libnfs/nfs4/nfs4.c:188`)
  for an NFSv4 status it doesn't map (NFS4ERR_DELAY/GRACE map to -EIO, so it's a
  different, unmapped status). The fresh client hits it on its **first OPEN** right
  after mount — the mount's GETATTR/FSINFO don't establish NFSv4 OPEN state, so the
  first OPEN transiently fails during establishment, clearing in a few seconds
  (a manual re-run always works).
- Fix (kernel, extends c25ed0cb): change the object.c re-drive from 8 tries/~1.9 s
  to a **~10 s deadline** with ramped backoff. It exits the instant the window
  clears (typically ~2-3 s, not a fixed wait), recovering the exec. Still bounded
  and targeted at the one uncovered exec open; each attempt logs the real errno.
- Cleaner root options for later: fix libnfs's ERANGE catch-all to distinguish the
  actual (retryable) status, or warm up the NFSv4 client's OPEN state at takeover
  before "/" goes live (needs libnfs debug logging of the raw NFS4ERR to pick the
  precise status). Deferred — the re-drive extension recovers it now.
