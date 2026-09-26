# E2 — What is SuperTuxKart's "~88 % inside submit" made of?

*Pre-registered 2026-09-26, before the first Pi run. Code: winsys instrumentation (default-off,
uncommitted in `phoenix-rtos-devices`), [`tools/gpu-lane/stkprof/`](../../tools/gpu-lane/stkprof/)
(build script, analysis script, README). Research context:
[§1.3 and §6 E2](../research/2026-09-26-gpu-drm-architecture.md).*

## Question

STK renders at ~8.4 fps and is not fill-bound (720p = 1080p,
[2026-09-16 note](../misc/2026-09-16-stk-fps-not-fill-bound.md)). The "~88 % of the frame is CL
submits (8/frame, ~150 ms)" figure comes from the 2026-09-08 era (5.84 fps, 171 ms/frame, before
`scale_rtts_factor=0.75` was seeded), and a note from the 1-fps chrono era claimed the opposite
("~83 % of a ~1000 ms frame on CPU work outside the V3D driver", `scripts/build-rootfs-helpers.sh`
comment). Neither was ever split. [inferred] Both are stale; E2 re-measures from scratch.

What each submit's wall time consists of — **GPU execution** (bin and render spins), **our cache /
TLB maintenance** around the job, or **CPU work between submits** — decides how much of the STK gap
the async multi-queue render server (M1) can close. The research doc explicitly does not predict a
speedup; neither does this file.

## What the submit path does today

All line numbers are `phoenix-rtos-devices` **HEAD `cad97dc`**,
`gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c` (the working tree adds ~300 lines of default-off
instrumentation and shifts them). Everything is **synchronous and polled**: the calling thread
spins on MMIO until the GPU finishes, and `DRM_V3D_WAIT_BO` returns 0 without waiting
(:3575-3576) because nothing is ever in flight when a submit returns. [read]

**Entry** — every DRM ioctl from Mesa (the libdrm shim's `drmIoctl` is an inline forwarding to
`phoenix_v3d_ioctl`; `drmSyncobj*` are no-op stubs in `v3d_libdrm_shim.c`) enters
`phoenix_v3d_ioctl` (:3542), which takes the process-wide `v3d_submit_lock` except for
`GET_PARAM`/`WAIT_BO` (:3550-3557).

**`ioc_submit_cl` (:2399), in order:**

| # | Lines | Step | Waits? |
|---|---|---|---|
| 1 | :2420 | `dsb sy` | barrier |
| 2 | :2438-2525 | entry RCL sanity check (two byte reads of the RCL head/tail; prints only when bad) | no |
| 3 | :2550 | `dsb sy` (Normal-NC BO stores drained before the MMIO kick) | barrier |
| 4 | :2565 | `SLCACTL = INVAL_ALL` — slice caches (TVCCS/TDCCS/UCC/ICC); fire-and-forget, moved to the front by the #67 ordering fix so every later wait is its settle time | no |
| 5 | :2572 → :2272-2279 | `mmu_flush_tlb`: MMUC flush, spin on FLUSHING; TLB clear, spin on CLEARING (1 M-spin caps). **Every submit**, whether or not any PTE changed since the last one | **2 spins** |
| 6 | :2590-2592 | L2T flush: wait-old (`l2t_flush_wait`, :2261-2265), issue `L2TFLS`, wait-new | **2 spins** |
| 7 | :2595-2606 | **"fix-A"**: a second, identical waited L2T flush. Original purpose now served by step 4; removal on 2026-07-26 regressed (1/3 boots, 94 CT1 render timeouts) so it stays as *timing margin* — E10 asks whether it survives async submit | **2 spins** |
| 8 | :2612-2616 | clear `FLDONE|FRDONE|QPU` ints, `PTB_BPOS=0`, program tile-alloc `CT0QMA/QMS`, `CT0QTS`, then **kick CT0** (`CT0QBA`, `CT0QEA`) | — |
| 9 | :2622-2659 | **bin spin**: poll `CTL_INT_STS` for `FLDONE` (8 M-spin cap). **OUTOMEM**: when the binner exhausts tile-alloc it raises `INT_OUTOMEM` and stalls; the loop hands it the persistent 32 MiB overflow pool (`BINOVF_PAGES`, :206) via `PTB_BPOA/BPOS` — the *whole* remaining pool in one chunk (`BINOVF_CHUNK_BYTES`), re-armable, then clears `OUTOMEM`. Every 1 M spins (~160 ms) it samples `ct0ca`; 5 frozen samples (~0.8 s) = wedge | **spin (GPU)** |
| 10 | :2660-2754 | on bin wedge: diagnostic dump, `goto job_retry` | wedge only |
| 11 | :2755-2786 | **bin→render handoff**: clear ints; waited L2T flush (wait-old, issue, wait-new) so CT1 reads complete tile lists; `SLCACTL` again; `bincrc_capture` (no-op unless `V3D_BIN_CRC=1`) | **2 spins** |
| 12 | :2788 | **kick CT1** (`CT1QBA`, `CT1QEA`) | — |
| 13 | :2796-2816 | **render spin**: poll for `FRDONE` (16 M-spin cap); every 1 M spins ack latched QPU ints and sample `ct1ca` (frozen ~0.8 s = wedge) | **spin (GPU)** |
| 14 | :2817-2869, :2870-2988 | on wedge: dump, `reset_reinit_core()` (:2948 — full V3D reset + `apply_core_regs`), **drop the job** | wedge only |
| 15 | :2990-2991 | post: wait-old, then issue `L2TFLS|FLM_CLEAN` **without waiting** — its drain is absorbed by the *next* submit's wait-old at step 6 (or the next TFU/CSD prologue) | 1 spin |
| 16 | :3013-3036 | `DRM_V3D_SUBMIT_CL_FLUSH_CACHE`: counted and logged; the TMUWCF + awaited clean runs only with `V3D_CL_CACHE_CLEAN=1` (default **off** — legacy behaviour, a C1 A/B arm) | off by default |

So one CL job costs, besides the two GPU spins, **four waited L2T flushes, one MMU TLB+PTE-cache
flush, two slice invalidates and two `dsb`s**, all with the CPU spinning. The L2T flush range is the
whole cache (`L2TFLSTA=0`, `L2TFLEND=~0`, `apply_core_regs` :2313-2314). [read]

**`ioc_submit_tfu` (:3079)**: prologue `dsb` + `mmu_flush_tlb` + `SLCACTL` + waited L2T flush
(:3215-3220); program + kick (:3223-3238); spin on `HUB_INT TFUC/TFUF` with a BUSY fallback
(:3247-3256, 8 M cap); a gated readback probe that prints for the first 12 TFUs and every 1024th
(:3257-3366); epilogue TMU write-combiner drain (spin) + waited L2T clean + `SLCACTL`
(:3376-3381).

**`ioc_submit_csd` (:3438)**: `dsb` + `SLCACTL` + `mmu_flush_tlb` + waited L2T flush
(:3445-3450); spin until the CSD unit has no CURRENT dispatch (:3466-3476, reset on timeout); kick
(:3479-3482); spin on `CSDDONE` (:3492-3496, 80 M cap); TMUWCF drain + waited L2T clean + `dsb`
(:3508-3513). [inferred] STK's GL path should issue no CSD jobs; the counter will say.

**Present**: the SDL2 GL glue calls `glFinish()` then `v3d_phoenix_flip(k)` (:1034), whose only
hardware work is `v3d_phoenix_fb_flip` (:1040) — the mailbox `SET_VIRTUAL_OFFSET` through
`rpi4-vcmbox` (~0.15 ms measured earlier). The flipstat/pace prints live in the same function.

**BO allocation** (`ioc_create_bo`, :1374): `mmap(MAP_CONTIGUOUS|MAP_UNCACHED)` or a pool hit,
`va2pa`, one PTE per page. Its time is outside the submit ioctls but inside "the winsys".

**Out of scope**: `libv3dv-phoenix.a` compiles the same source for vkQuake; STK links only
`libv3d-phoenix.a` (+ `libGL-phoenix.a`), so the Vulkan copy is irrelevant here (and, being
default-off, unchanged).

## The instrument

Compiled only with `-DV3D_PHX_SUBMIT_PROFILE`; every hook is `V3D_SP(...)`, empty by default.
Timestamps are `isb; mrs cntvct_el0` (EL0-readable on this port since kernel `9d7f558b`),
frequency from `cntfrq_el0` (printed in the banner; 54 MHz expected → 18.5 ns resolution).

* **Whole ioctls** (in `phoenix_v3d_ioctl`): lock-wait time and handler time per class — CL, TFU,
  CSD, CREATE_BO, GEM_CLOSE, MMAP_BO, unlocked (GET_PARAM/WAIT_BO), other.
* **Phases** (a cursor advanced at fixed points):

  | phase | CL steps | | phase | TFU / CSD |
  |---|---|---|---|---|
  | `pre` | 1-4 | | `tfu pre` | prologue |
  | `tlb` | 5 | | `tfu spin` | kick → done |
  | `l2t` | 6 | | `tfu diag` | readback probe |
  | `fixa` | 7 | | `tfu post` | epilogue |
  | `bin` | 8-9 (kick writes + spin) | | `csd pre` | prologue + not-current wait |
  | `hand` | 11 | | `csd spin` | kick → CSDDONE |
  | `rend` | 12-13 | | `csd post` | clean |
  | `post` | 14-16 | | | |

  plus counters: OUTOMEM hand-outs, wedges, silent spin-cap exits in `l2t_flush_wait` and
  `mmu_flush_tlb` (a timed-out wait returns as if it had succeeded).
* **Frame**: flip-to-flip period (min/max per window), GPU-spin total of the worst frame, the
  mailbox pan time.
* **Gap**: from the end of one GPU-job ioctl (CL/TFU/CSD) to the start of the next — the time the
  GPU sat idle while the CPU built the next job (BO ioctls and flips in between included).
* **Computed CPU**: `cpu = wall − Σioctl − lock − flip − rep`, where `rep` is every instrument and
  flipstat/pace print (timed, so the instrument's own UART cost never lands in `cpu`).

Three lines per flipstat window (`subprof-a`, `-cl`, `-x`), each tagged `t=` and `fr=` so one
UART-corrupted line costs only its window, and raw µs sums so the identities are checkable. Formats
and the build/run recipe: [`tools/gpu-lane/stkprof/README.md`](../../tools/gpu-lane/stkprof/README.md).

**Self-checks built in:** (1) CL phase sum / CL ioctl time ≈ 1 (the phases cover the submit);
(2) counter window / `clock_gettime` window (`cg`) ≈ 1 — two clocks, one period;
(3) `rep` share of the frame is small; (4) the build proves the default object is byte-identical
and the control relink reproduces the shipped binary (see README).

## Pre-registered measurement plan

**Binary and arms.** One binary, `supertuxkart-prof` via `stk-prof`, never the shipped name. Arms
are cache states, not builds:

| arm | trials | how |
|---|---|---|
| prof-warm | 3 (main) | cycle log says `Mesa shader disk cache KEPT` |
| prof-cold | 1-2 | `rm -rf <export>/.mesa-shader-cache` before the cycle (⚠ biases the next C1 trial) |
| shipped-warm | 2 | `stk …` in the same session, interleaved with prof-warm — overhead check only |

Command, every trial: `stk-prof --track=hacienda --numkarts=4 --profile-laps=2` (shipped arm:
`stk …`). One run per boot. The arm is asserted from the log (`stk-prof: DATADIR=` and
`v3d-winsys: SUBMIT PROFILE build … cntfrq=`), never from the command sent.

**Windows.** Gameplay only, per the measure-over-gameplay rule: flipstat windows with fps > 3 (menus
and loading run < 1 fps, the race ~8 — the same threshold `scripts/c1-bench-table.sh` and
`c1-fire-position.sh` use for race start), dropping the first and last window of the race as
transitions. Excluded: any window with a wedge, a spin-cap exit, or a `TIMEOUT` / `GPU wedged` /
`DROPPED job` line (a wedge adds ~0.8 s of false "render"). Target ≥ 10 gameplay windows (~50 s)
per trial. Loading-phase windows are reported but not graded (cold vs warm is mostly a loading
difference; [inferred] the race itself should differ little).

**Numbers, per frame over the gameplay windows** (`e2-summarize.py`):
F frame period; **G** = bin + render + TFU spin + CSD spin; **M** = pre + tlb + l2t + fixA + hand +
post + TFU/CSD pre/post/diag; **C** = computed CPU; **B** = BO and other ioctls; **P** = flip +
lock wait; **R** = instrument. Also: CL submits/frame, mean inter-GPU-job gap, OUTOMEM rate,
Σbin vs Σrender.

**Validity gates (a trial failing any is void, not "a different result"):**
1. CL phase-sum / ioctl in 0.98-1.00; counter / `cg` wall in 0.99-1.01.
2. R < 2 % of F.
3. prof-warm gameplay fps within 5 % of shipped-warm in the same session (else the instrument or a
   libphoenix/archive drift is perturbing the frame — compare `BUILD-INFO.txt`).
4. 0 kernel (EL1) faults; the race reached (gameplay windows exist).
5. ⚠ prof runs are a **different binary layout** — never fold them, or their C1 signatures, into C1
   rate statistics.

## What each outcome means for M1

Upper-bound models (all [inferred]; they assume perfect overlap and no new costs):

* **U1 async submit, CPU ∥ GPU**: the CPU builds job N+1 while the GPU runs job N, and the server
  (or GPU) does the maintenance. Frame ≥ max(C + B + P, G + M). Gain ≤ F / max(…) — at most ×2,
  and only when the two sides are equal.
* **U2 U1 + bin(N+1) ∥ render(N)**: V3D has separate CT0 and CT1 queues and Linux keeps a bin job
  and a render job in flight at once (separate bin/render scheduler queues, `v3d_sched.c`); a multi-queue server can too. GPU side → G − min(Σbin,
  Σrender) + M. This is the lever that still works when STK is fully GPU-bound. (Loose: bin and
  render share the QPUs, so real overlap is less.)
* **U3 no M1 at all**: keep submit synchronous but drop fix-A and flush the TLB only when PTEs
  changed. Frame ≥ F − fixA − tlb.

| outcome (shares of F, gameplay) | reading | for M1 |
|---|---|---|
| **G ≥ 50 %, C ≤ 25 %** (GPU-bound) | the GPU really executes for most of the frame | CPU∥GPU overlap is capped by C (small); the prize is CT0∥CT1 overlap (U2), and render cost itself (scene, passes) — M1 must be multi-queue to matter |
| **M ≥ 25 %** (maintenance-bound) | our waited flushes/TLB clears are the frame | the lever is cache strategy (U3, E10: Linux leaves its L2T flush in flight and lets the binner stall its first CL read on
it — winsys comment at :2551-2564), which needs no M1; M1 must not copy today's per-job sequence |
| **C ≥ 50 %** (CPU-bound) | STK/Mesa CPU work dominates; the GPU idles | M1 will not help STK much; next step is a CPU-side profile (Mesa state emission, uncached BO writes — `tools/v3dmemprobe` — and the `dc civac` path in `v3d_resource.c`) |
| **G + M ≥ 50 % and C ≥ 25 %** (serial mix) | comparable CPU and GPU, strictly serialised | async submit (U1) is the lever, bound F / max(C+B+P, G+M); plus U2 on top where Σbin is a substantial part of G — the branch label never overrides the printed bounds |
| none of the above | — | read the components; no verdict |

The script prints the shares, U1-U3 and the matching branch. A result near a threshold is reported
as such, not rounded into a branch.

**Known limits of the method.** The spins are *wall* time on the CPU, i.e. GPU execution plus poll
latency (one MMIO read, sub-µs) — a fair measure of "GPU busy" for a serial submit, not of GPU
utilisation under overlap. `cpu` also contains page-fault handling, preemption by other processes
and any frame-limiter sleep; it is "not in the winsys", not "STK's own code". A bin spin includes
the binner's OUTOMEM stall while the loop services it (counted separately as `oom`). The
instrument adds ~11 stamps per CL submit (~100 per frame, a few µs at tens of ns each) plus ~600
bytes of UART per 5 s (booked to R).

## Coordination notes

* The winsys edit is **uncommitted** in `phoenix-rtos-devices`, a tree other agents commit to.
  Commit it by path only (`git -C sources/phoenix-rtos-devices add gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c`)
  — a `commit -a` elsewhere would sweep it in unreviewed.
* `build-showcase-apps.sh`'s `archive_fresh()` watches the mesa/ directory, so the next showcase
  build will **rebuild `libv3d-phoenix.a`** (~9 min) because the source is newer than the archive.
  The winsys object comes out byte-identical (PROOF 1/1b), so the archive content — and hence the
  shader-cache fingerprint — should not change; expect the rebuild, do not read it as a change.
* Not yet run on hardware: `cntfrq_el0` at EL0 is inferred readable (CNTKCTL `EL0VCTEN|EL0PCTEN`
  gates it too); the banner prints the raw value and the `cg`/wall check catches a wrong frequency
  in the first window.
* The first window's `fr` is one less than flipstat's (the opening flip opens the window); it is a
  loading window and never graded.

## Result

*(empty until the first trials; append the per-trial table from `e2-summarize.py`, the verdict, and
the decision taken.)*

## Result — trial 1 (prof-warm, build 8, 2026-09-26, `rpi4b-uart-20260926-192136-e2-prof-W1.log`)

68 gameplay windows, 2545 frames, **7.38 fps, 135.4 ms/frame**, 8.0 CL submits/frame; self-checks
pass (phase-sum/ioctl 1.000, counter/wall 1.0000, instrument cost 0.0 %).

| share | ms/frame | % |
|---|---|---|
| **G** GPU waits | 94.56 (bin 3.21, **render 91.35**) | 69.8 |
| **M** cache/TLB maintenance (fix-A 0.02, TLB 0.00) | 0.28 | 0.2 |
| **C** CPU outside the winsys | 40.37 | 29.8 |
| B / P / R | 0.04 / 0.16 / 0.01 | ~0.1 |

**Pre-registered verdict: SERIAL MIX** — CPU and GPU comparable and strictly serial. Upper bounds:
async submit (U1) ×1.43; U1 + bin‖render (U2) ×1.48 (bin is small); dropping fix-A/TLB (U3) ×1.00.
So M1 is worth ≤ ~1.4× for STK, and cache maintenance is **not** a lever.

⚠ **The bigger finding:** the V3D **render phase takes 91 ms per frame** at a measured 500 MHz
V3D clock. Even with perfect CPU/GPU overlap STK would be capped near 11 fps, while Raspberry Pi OS
runs it at 30+ — and STK's fps is known to be resolution-independent, so this is not fill rate. The
render phase itself is ~3× too slow → experiment **E2b** (`E2b-v3d-render-slowness.md`) investigates
tiling (RASTER vs UIF), MMU/cache configuration, per-job flushes, shader build flags and V3D
performance counters. Trials 2–4 (shipped-warm overhead check, two more prof-warm) are running.
