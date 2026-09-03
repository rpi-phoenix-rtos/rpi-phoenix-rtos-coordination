# #67 vkQuake torch intermittency: is it the banked V3D binner wedge? — driver-side analysis

Date: 2026-09-03
Scope: **static analysis + re-analysis of existing artifacts only.** No build, no Pi cycle (the UART
was in use by a running bench), no writes under `external/`, no source edits.
Companions: [`2026-09-03-quake-torch-regression-archaeology.md`](2026-09-03-quake-torch-regression-archaeology.md)
(history, and the proof that the 2026-08-22 closure was a lavaball),
[`2026-09-03-vkquake-torch-rootcause-candidates.md`](2026-09-03-vkquake-torch-rootcause-candidates.md)
(engine-side analysis; H1/alpha refuted six ways),
[`2026-08-22-q3dm7-binner-stall-analysis.md`](2026-08-22-q3dm7-binner-stall-analysis.md) (the banked wedge).

---

## 0. Answers up front

**Q: Is the #67 torch intermittency the same defect as the banked intermittent CT0/binner wedge?**

**No — the two have incompatible signatures, and the wedge cannot by itself remove one draw from an
otherwise-correct frame.** But this analysis found something the record did not have, and it is
*not* a "no connection" answer either:

> **The torch verdict is perfectly predicted, across all 26 gradeable boots on record, by whether
> the run logged a GPU wedge on a control list at GPU-VA page `0x0acd5xxx` or `0x0acd0xxx`.**
> 23/23 ABSENT boots have such a line. 3/3 PRESENT boots do not. p ≈ 3.8 × 10⁻⁴ (§4), and one of
> the 23 (`vkq-lerp2b-T8`) arrived *after* the prediction was written down — it held.
> Every wedge in every vkQuake run occurs **before the map-load print** — i.e. during
> `Host_Init` + `Cbuf_Execute("map start")`, the one-shot staging/meta-copy upload phase — and
> **not one** wedge occurs during the ~2.5 minutes of steady-state rendering that follows.

So the mechanism is not "the wedge corrupts a frame". It is:

> **A wedge lands on a one-shot GPU *transfer* job during map load. `ioc_submit_cl` drops that job
> and `return 0`s — a mitigation that is correct for a render frame and catastrophic for a
> transfer — and the always-signalled syncobj shim means neither V3DV nor vkQuake can tell. The
> destination buffer keeps its create-time zeros for the rest of the process, so that model's
> vertices decode to a degenerate point and contribute zero fragments in every subsequent frame.**

That is a **silent-failure path in the ported driver**, exactly the class the brief pointed at. The
banked wedge is the *trigger*; the *bug* is `v3d_phoenix_winsys.c:1236-1261`.

Confidence: the statistical separation is strong and the mechanism is coherent with every
measurement below, but the causal link from "VA page `0acd5`" to "flame.mdl's upload job" is
**inferred, not instrumented** (§4.3 states the caveat and §7 gives the experiment that closes it).

**A second, independent factor was found and it answers the "why flame and not zombie" question**
that any dropped-transfer story has to answer: vkQuake stages its mesh uploads with
**alignment = 1**, so each model's copy `srcOffset` is the byte-granular running sum of every prior
upload in that staging batch — and V3DV picks the meta-copy's element size from the GCD of the src
and dst offsets, so an **odd** staging offset forces `item_size = 1` (R8UI) and a raster TLB stride
of `width × 1` (75 bytes for flame's 9600-byte upload). One model in the scene therefore takes a
different, unaligned hardware store path from all the others. That is the **susceptibility**
(deterministic, model-specific); the wedge/drop is the **trigger** (stochastic, per boot). See R2.

---

## 1. New measurement A — the absence is PER-BOOT LATCHED, not per-frame

This had never been measured. Grading every gradeable HDMI tick of every vkQuake boot on record
(`./scripts/check-torch-rois.py --label <label>`, 26 gradeable boots, 7–9 gradeable frames each,
~200 frames):

| | boots | frames per boot | mixed boots |
| --- | --- | --- | --- |
| PRESENT boots | 3 | **all** gradeable frames pass (e.g. `vkq-lerp2b-T6` 9/9, values 281–599 lit px, animating) | — |
| ABSENT boots | 23 | **every** gradeable frame reads **exactly 0** lit px in both ROIs | — |
| **mixed (some frames present, some absent) within one boot** | **0** | | **0** |

There is not a single mixed boot. An ABSENT boot renders ~120 consecutive frames over ~2.5 minutes
with the flame contributing zero pixels in each one, while the lavaball moves, the particles move,
the zombies animate and the wall texture behind the torch stays unbroken.

**Consequence.** The corrupting event happens **once**, before or during the first frame, and its
effect is latched for the process lifetime. Anything per-frame or per-job is excluded as the
*proximate* cause of the missing pixels.

## 2. New measurement B — the flame draw IS issued in ABSENT runs

From the (since-reverted) `VKQ_ALIASTRACE` instrumentation, fork `cd81158`, run
`artifacts/rpi4b-uart/rpi4b-uart-20260903-123413-vkq-trace2.log` — **graded ABSENT**:

```
vkq-alias: progs/flame.mdl   nolerp=1 lerpcvar=2 pose1=1 pose2=1 numposes=6 lerpblend=1.000 blend=0.000 pipe=0 alpha=1.00 atest=0
vkq-alias: progs/v_shot.mdl  nolerp=0 lerpcvar=2 pose1=0 pose2=0 numposes=7 lerpblend=1.000 blend=0.000 pipe=0 alpha=1.00 atest=0
vkq-alias: progs/zombie.mdl  nolerp=0 lerpcvar=2 pose1=195 pose2=195 numposes=198 ...
```

`R_DrawAliasModel` **is** called for `progs/flame.mdl`, on the opaque pipeline, with `alpha=1.00`
and `atest=0`, in a boot where the torches produce zero pixels.

**This refutes candidate R2 of the root-cause doc** ("the static entities never reach the alias draw
list"). The statics reach the draw list. The draw is recorded and submitted; it produces no
fragments.

(The `tris=0` field in that trace is uninformative — it reads 0 for `v_shot.mdl` and `zombie.mdl`
too, and both render. Do not cite it.)

## 3. New measurement C — wedges DO occur under vkQuake, and the raw count predicts nothing

The record's characterisation lists as a gap: *"No wedge has ever been observed under vkQuake/V3DV
specifically."* That is now false. Every vkQuake run on record logs 1–5 `GPU wedged` events
(`vkq-lerp2` logged 146):

| verdict | wedge counts observed |
| --- | --- |
| PRESENT | `vkq-lerp-rep` **0**, `vkq-lerp3` **4**, `vkq-lerp2b-T6` **3** |
| ABSENT | 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5 |

PRESENT occurs with 0 wedges **and** with 4. ABSENT occurs with 1 through 5. **The wedge count is
uncorrelated with the torch verdict.** Anyone reaching for "more GPU trouble ⇒ torches vanish" is
refuted by this table alone.

The vkQuake wedge is also a *different sub-mode* from both recorded classes: `int_sts=0x00000000`,
`ct1cs=0x10/0x18/0x20`, and in several cases `ct1ca` equal to `ct1ea`'s BO base (render never
advanced) or **past** `ct1ea` (`ct1ca=0x0acd5070` vs `ct1ea=0x0acd5065`), with
`RENDERFAULT gpuva=0x00000000 -> NO live BO covers it`. The control lists are **tiny** — RCLs of
0x65/0x7a bytes, BCLs of 0x0e bytes — i.e. single-tile meta-copy/blit jobs, not scene renders. Worth
recording separately; it is not the q3dm7 MODE-A front-end stall.

## 4. New measurement D — the discriminator: a wedge on VA page `0acd5` or `0acd0`

### 4.1 The table

For every vkQuake UART log of 2026-09-03, the set of `ct0ca`/`ct1ca` control-list **base pages** from
the (never rate-limited) `BIN TIMEOUT` / `RENDER TIMEOUT` one-liners, against the torch verdict:

| run | verdict | wedges | wedged CL base pages | `0acd5`/`0acd0`? |
| --- | --- | --- | --- | --- |
| `vkq-lerp-rep` | **PRESENT** | 0 | — | **no** |
| `vkq-lerp3` | **PRESENT** | 4 | aca5 acc7 acc9 ad06 | **no** |
| `vkq-lerp2b-T6` | **PRESENT** | 3 | ac35 acc7 acce | **no** |
| `vkq-mapstart` | ABSENT | 2 | acc7 **acd5** | yes |
| `vkq-showtris` | ABSENT | 4 | aba3 acc7 acce **acd0** | yes |
| `vkq-fix67` | ABSENT | 4 | ab97 acc7 **acd5** acd7 | yes |
| `vkq-nolerp-fix` | ABSENT | 3 | acc7 **acd5** acf1 | yes |
| `vkq-cfg-lerp` | ABSENT | 3 | ab97 acc7 **acd5** | yes |
| `vkq-trace2` | ABSENT | 2 | acc7 **acd5** | yes |
| `vkq-trans1` | ABSENT | 3 | ac64 acc7 **acd5** | yes |
| `vkq-rate-r1` | ABSENT | 4 | acc7 acce **acd5** acd7 | yes |
| `vkq-rate-r2` | ABSENT | 3 | acc7 acc9 **acd5** | yes |
| `vkq-trans12` | ABSENT | 3 | acb3 acc7 **acd5** | yes |
| `vkq-nocache` | ABSENT | 3 | abb3 acc7 **acd5** | yes |
| `vkq-pristine` | ABSENT | 5 | ac95 acc7 acce **acd0** acd7 | yes |
| `vkq-def-d2` | ABSENT | 3 | acc7 **acd5** ace4 | yes |
| `vkq-def-d3` | ABSENT | 3 | acc7 **acd5** ace4 | yes |
| `vkq-def-T1` | ABSENT | 2 | acc7 **acd5** | yes |
| `vkq-def-T2` | ABSENT | 3 | acc7 **acd5** acd7 | yes |
| `vkq-def-T3` | ABSENT | 5 | abc3 abd3 acc7 **acd5** acd7 | yes |
| `vkq-lerp2b-T1` | ABSENT | 2 | acc7 **acd5** | yes |
| `vkq-lerp2b-T2` | ABSENT | 1 | **acd5** | yes |
| `vkq-lerp2b-T3` | ABSENT | 2 | acc7 **acd5** | yes |
| `vkq-lerp2b-T4` | ABSENT | 3 | acc7 acce **acd0** | yes |
| `vkq-lerp2b-T5` | ABSENT | 3 | ac09 acc7 **acd5** | yes |
| `vkq-lerp2b-T7` | ABSENT | 4 | abf0 ac43 acc7 **acd5** | yes |
| `vkq-lerp2b-T8` | ABSENT | 3 | abc5 acc7 **acd5** | yes |

Non-gradeable runs (viewpoint gate; excluded from the statistic, listed for completeness):
`vkq-lerp2` INCONCLUSIVE, 146 wedges, has `acd0`; `vkq-trace` INCONCLUSIVE, has `acd5`;
`vkq-def-d1` INCONCLUSIVE, has `acd5`.

**26/26 perfect separation** (`vkq-lerp2b-T8` was added by the running bench after this table was
first written — a genuine out-of-sample test of the prediction in E1, and it held). Under the null
hypothesis that the verdict is independent of the marker, the probability that the three PRESENT
boots are exactly the three marker-free boots is `1/C(26,3) = 1/2600 ≈ 3.8 × 10⁻⁴`.

Note also `vkq-lerp2b-T4` vs `vkq-lerp2b-T6`: **same binary, same command, same wedge count (3), two
of three wedge addresses identical** — they differ only in whether the third wedge hit `acd0`
(T4, ABSENT) or `ac35` (T6, PRESENT).

### 4.2 All wedges are in the init + map-load window

Line positions in each log (`vkquake: loading 'map start'` is printed **after** `Cbuf_Execute()`
returns, `sources/phoenix-rtos-ports/vkquake/glue/pl_phoenix_main.c:159-161`, so everything before
that print includes `Host_Init` **and the entire map load**):

```
vkq-lerp2b-T5:  381 RENDER TIMEOUT ...  392 RENDER TIMEOUT ...  403 RENDER TIMEOUT ...
                419 vkquake: loading 'map start'
                422 vkquake: init-texture staging flushed + device idle
                423 vkquake: loop 0 enter
```

Same shape in `T1`, `T2`, `T4`, `T6`, `mapstart`, `def-T*`. **Zero wedges after `loop 0 enter`** in
any run. So every wedge in a vkQuake run lands on a job submitted while textures, pipelines and
**model vertex/index buffers** are being uploaded — the one-shot phase — and never on a scene frame.

The glue itself documents that this phase has already produced one silent-upload bug on this port
(`pl_phoenix_main.c:164-172`): *"TEXTURE-STAGING FLUSH (hygiene; HW: textured 2D samples 0 = upload
gap) … The shim only flushes staging per-frame inside `GL_EndRendering`, which starts at frame 1"*.

### 4.2b Why those control lists are tiny: on V3D 4.2 a buffer copy IS a render job

`external/mesa/src/broadcom/vulkan/v3dvx_meta_common.c:1445-1515` — the TFU fast path for
`vkCmdCopyBuffer` is inside `#if V3D_VERSION >= 71`, so on V3D **4.2 it is compiled out**. Every
`vkCmdCopyBuffer` becomes a **CL render job** that loads the source as a raster image into the TLB
and stores it to the destination. One such job per model upload, run once, at map load.

That is exactly what the wedge one-liners show: RCLs of **0x65 / 0x7a bytes** and BCLs of **0x0e
bytes** — single-tile meta-copy control lists, not scene renders (a `start.bsp` scene RCL is orders
of magnitude larger). **The jobs that wedge in a vkQuake run are the upload jobs.**

### 4.3 The caveat, stated plainly

A GPU VA is **not a guaranteed-stable job identifier**. `va_alloc` is first-fit over a hole list
(`v3d_phoenix_winsys.c:499-521`) and a wedge triggers `va_free` of that job's BOs plus a core reset,
so the VA sequence can **diverge after the first drop**. The `0acd5`/`0acd0` marker may therefore be
identifying *which job* wedged, or it may be identifying *how far into the deterministic upload
sequence the first drop occurred*. Both readings point at the same conclusion — a dropped job inside
the one-shot upload phase — but only the §7 experiment distinguishes them, and until it is run the
marker should be treated as a **very strong predictor of unknown mechanism**, not a proven cause.

---

## 5. Characterisation of the banked wedge (from the record)

Terminology hazard first: the torch docs also use "wedge" for the *geometric* shape #67's alias
models collapse into (`2026-09-03-quake-torch-regression-archaeology.md:56`, "a fan/**wedge** in
model space"). That is not a GPU hang. Below, "wedge" = a CLE control thread frozen mid-control-list.

The record supports **four** classes, not two:

| ID | Stage | Signature | Status |
| --- | --- | --- | --- |
| C1 | CT1 render | `fdbgs=0x000350ef` (DEPTHO_FIFO_IP_STALL + INTERPZ + EZTEST backup), `int_sts=0`, `ct1ca` parked near `rcl_end`. Trigger *proven by experiment*: the GPU writing the **live scanout fb** while HVS reads it (3000 depth-heavy frames to DRAM = 0 wedges) | fixed architecturally 2026-06 (render-to-DRAM + triple buffer) |
| C2 | CT1 render | `ct1ca` in a **stale per-tile sublist**, `mmu_ill` set. Exposed only when the extra waited L2T flush ("fix-A") is removed: 1 of 3 boots → 94 RENDER TIMEOUTs | suppressed by timing margin, not root-fixed (`v3d_phoenix_winsys.c:1006-1014`) |
| C3 | CT1 render | **`int_sts=0x00ff0000`** = `V3D_INT_QPU_MASK` bits 27:16 asserted, FRDONE never fires; `ct1ca` frozen within each frame at a consistent sublist offset. Trigger: heavy-fragment deferred lighting (STK) | **root-caused + fixed 2026-08-27** (QPU-int ack; STK render wedges 330 → 0) |
| **C0** | **CT0 binner** | `BIN TIMEOUT`; `int_sts=0`, `int_qpu=0`, `bpoa=bpos=0`, `ovf_armed=0`, `gmpvio=0`, `MMU_VIO_ADDR=0`; `ct0ca` parked inside its own valid BCL BO; `ct0cs=0x20` (bit 5, **undecoded**). MODE A = zero binner progress (`ct0pc=0 bfc=0`) on a malformed 19-byte CL whose BO holds a **stale ASCII string**, `FDBGS=0x07`. MODE B = partial progress then a real MMU violation, `FDBGS=0x47` | **BANKED / owner-attended** |

Facts about C0 that matter for the comparison:

- **Trigger:** `quake3 +devmap q3dm7` (5823 faces) at ~50 % of boots (3/7 post-QPU-fix, so untouched
  by the C3 fix); `q3dm1` (1942 faces) never wedges; Linux renders q3dm7 on the same silicon. Also
  seen on heavy quake1 demo scenes and during STK asset load (`BINFAULT gpuva=0x00000000 → NO live
  BO`, a null-BO ref in the binner CL). Data-dependent and **deterministic within a boot** —
  re-submitting the same frame re-hangs at the same `ct0ca` across GPU resets — intermittent across
  boots. Also **build-layout sensitive** (an inert `r_alias_debug` build produced 194 wedges on
  demo1 where the known-good build produced zero).
- **Granularity: PER-JOB, never per-draw.** Detection is two spin loops in `ioc_submit_cl`
  (bin: `v3d_phoenix_winsys.c:1036-1069`, sampling `ct0ca` every ~160 ms, 5 identical samples ⇒
  frozen, backstop 8 M spins; render: `:1176-1194`, backstop 16 M spins ≈ 2.5 s).
- **Driver response:** bin wedge ⇒ `job_failed`, **skip the render kick** (`:1131-1133`) ⇒
  `job_retry` ⇒ **one true GPU reset and DROP the job, no re-submit** (`:1250-1257`) ⇒
  **`return 0` — success** (`:1261`).
- **Always loud:** `BIN TIMEOUT` / `RENDER TIMEOUT` / `GPU wedged — true reset + drop this frame
  (mitigation; drops=N)` are **not** rate-limited (only the deep sub-dumps are, `bdbg++ < 3` at
  `:1088`, `rdbg++ < 3` at `:1221`). Cost ≥ 0.8 s per event.
- **Observable effect:** one dropped frame + a hitch; at storm rates, 0.8–3 fps. **Correctness is
  not lost** for C0 — "All 7 trials RENDERED (winsys reset recovers every time), 0 faults".
- **The one recorded case of "content missing, rest of the frame correct and stable" is C3 and it is
  PER-PASS**: STK's race rendered *dark* because the CT1 wedges dropped the deferred-lighting jobs
  while geometry/HUD completed. Fixed 2026-08-27 → fully lit.
- **No wedge instance in the record ever presented as "one model missing from an otherwise perfect
  frame".** The one attribution that pointed that way — quakespasm `3d742a3`, *"the V3D mis-fetches a
  single-pose alias VBO once it spans a second page (the wedge path reports `mmu_ill`)"* — rests on a
  register reading that was **retroactively invalidated** on 2026-08-22: `mmu_ill` is the scratch-page
  redirect config the driver *programs*, never a fault VA (in-code note at
  `v3d_phoenix_winsys.c:1095`, `:1225`).
- **The driver's own source says #67 is not a wedge** — `v3d_phoenix_winsys.c:705`: *"The #67 glitch
  is a NON-DETERMINISTIC per-frame geometry loss on complete (**non-wedged**) frames."*
- **Gates:** the only env var in the whole rpi4-v3d tree is `V3D_BIN_CRC`
  (`v3d_phoenix_winsys.c:734`, used `:1165`) — a tile_alloc CRC32 for binner-determinism testing, and
  it is a #67 diagnostic, not a wedge one. Wedge dumps are env-free and unconditional.

---

## 6. Same-or-different verdict

### **DIFFERENT defects. The banked C0 wedge is not the cause of the missing torch pixels.**

Four independent legs, each sufficient:

1. **Granularity.** The wedge path drops a whole **job**. Under V3DV the 31 flame draws share the
   world's render pass and therefore the world's CL job; no job-level event can remove those draws
   and leave the wall, the viewmodel and the lavaball intact. Confirmed against the emission path:
   V3DV has no per-draw failure mode (§8, ranks 3–6 are all whole-job or OOM-gated).
2. **Loudness and cost.** A wedge always prints three headline lines and costs ≥ 0.8 s. The torch
   absence is free and would have to recur silently on ~120 consecutive frames per boot.
3. **Measured non-correlation of the *event rate*** (§3): PRESENT with 0 wedges and with 4; ABSENT
   with 1–5. The record's gap #7 said this correlation had never been measured; it has now, and the
   count is uninformative.
4. **Per-boot latching** (§1). A per-job dropper firing 1–5 times per ~120-frame run cannot hold one
   model off for *every* frame of the run while everything else is perfect.

Pre-empting the obvious counter-argument: **STK's "dark but otherwise stable" frames *were*
wedge-caused** — but that was per-**pass** (a whole lighting job dropped), logged 330 times, and is
the **fixed** C3 class. It is not a counter-example to leg 1.

### But: the wedge is causally implicated as the TRIGGER

§4 makes it very likely that the wedge machinery is nonetheless on the causal path — not by
corrupting a frame, but by **dropping a one-shot transfer job** whose loss is permanent. In that
reading:

- the **defect with the matching signature** is the winsys's silent job-drop
  (`v3d_phoenix_winsys.c:1236-1261`), amplified by the always-signalled syncobj shims;
- the **banked C0/CT1-marginal wedge** is the stochastic input that decides *which* job gets dropped;
- the two are **different bugs**, and fixing the wedge would make the torches appear without fixing
  the silent-drop hazard, while fixing the silent-drop would make the torches appear (loudly, or via
  retry) without fixing the wedge.

That is why "same or different" was the right question and why the answer is "different, but
coupled" — and why closing #67 on a wedge fix would be the seventh false closure.

---

## 7. Ranked driver-side candidates

Ranked by *"would this drop exactly one model's fragments, intermittently, per boot, leaving the
frame otherwise perfect?"*

### R1 ★★★ — A wedged one-shot upload job is dropped and reported as success
`sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c:1236-1261`

```c
job_retry:
	/* the wedge is DATA-dependent — re-submitting the SAME frame re-hangs at the same
	 * ct1ca/ct0ca across true resets (HW-confirmed). */
	...
	fprintf(stderr, "v3d-winsys: GPU wedged — true reset + drop this frame "
	        "(mitigation; drops=%u). ...");
	...
	return 0;                     /* :1261 — caller is never told */
```

Amplifiers that make the loss undetectable to everything above:

- `mesa/v3d_libdrm_shim.c:31-38` — `drmSyncobjWait` returns 0 with `*first_signaled = 0`,
  **ignoring handles and timeout**; `drmSyncobjCreate` hands out the single constant handle `1`
  (`:16-22`). `mesa/v3dv_libdrm_shim.c:52-61`, `:78-86` — `drmSyncobjQuery2` reports
  `points[i] = ~0ull`, `drmSyncobjTimelineWait` returns "all points reached". The header's
  justification (`v3d_libdrm_shim.c:6-8`) is *"submit is synchronous, so fences are always already
  signalled"* — **that premise is exactly what the drop breaks.** So vkQuake's post-staging
  `vkWaitForFences` cannot see it.
- `ioc_wait_bo` → `return 0` unconditionally (`v3d_phoenix_winsys.c:1726-1727`).
- Freshly created BOs are `memset` to zero (`:643`), so a dropped copy leaves the destination
  **all zeros**, not garbage. For an alias VBO that means every vertex decodes to model-space
  `(0,0,0)` → a single point after transform → zero-area triangles → **no fragments, no dark
  silhouette, wall texture untouched** — which is exactly the pixel evidence
  (`docs/misc/torch-archaeology/right-torch-roi-gamma-boost.png`, bare wall, no silhouette).

Why it fits everything: one-shot ⇒ **per-boot latched**; per-copy-**region** ⇒ **one model**
(see the shared-BO constraint below — this must be a *content* failure, and a dropped copy is
exactly that); wedge incidence is stochastic ⇒ **intermittent for a byte-identical binary**; the
flames and `flame2.mdl` are precached adjacently and would share a staging batch ⇒ explains why the
`flame2.mdl` fire-pit flames have been open since 2026-08-22 as a *paired* symptom.

**The shared-BO constraint (and why it kills three otherwise-attractive candidates).** vkQuake
suballocates every alias mesh from a single 16 MiB `mesh_buffer_heap` segment:
`external/vkquake/Quake/gl_mesh.c:538` (`GL_HeapAllocate(mesh_buffer_heap, …)`), `gl_mesh.c:37`,
`:135-136` (`MESH_HEAP_SIZE_MB 16`, page 4096), `external/vkquake/Quake/gl_heap.c:659` (dedicated
allocation only when `size >= segment_size`), `gl_heap.c:213` (one segment = one
`vkAllocateMemory`), and V3DV maps one `VkDeviceMemory` to one BO
(`external/mesa/src/broadcom/vulkan/v3dv_device.c:2232`; per-buffer identity is only
`buffer->mem_offset`, `:2963-2964`). flame 9600 B / flame2 18656 B / v_shot 8832 B / lavaball 400 B
are **all suballocations of one BO**. Therefore a per-model latched defect **must** be either that
model's *bytes* (never written, or written wrong) or that model's *offset arithmetic* — it cannot be
BO liveness, VA aliasing or an MMU mapping fault, because all of those would take `zombie.mdl` and
`v_shot.mdl` down with it, and those render. R1 survives this test (a dropped copy loses one
region's bytes and the BO is `memset` to zero at create, `v3d_phoenix_winsys.c:643`).

Why quakespasm/GL is immune: gallium v3d uploads vertex data by CPU `memcpy` into a mapped BO; there
is no GPU transfer job to drop.

**Not yet proven:** that the wedged `0acd5`/`0acd0` job is the copy job whose destination region is
`flame.mdl`'s vertex allocation. §4.3, and experiment E3.

### R2 ★★★ — Susceptibility: an odd staging offset puts one model's upload on the `item_size = 1` raster path
`external/mesa/src/broadcom/vulkan/v3dvx_meta_common.c:1451-1457` + `:812`, driven by
`external/vkquake/Quake/gl_rmisc.c:758`, `:697`, `:713`, `:761`

```c
/* v3dvx_meta_common.c:1451-1457 */
src_offset += region->srcOffset;
dst_offset += region->dstOffset;
uint32_t item_size = 4;
while (item_size > 1 &&
       (src_offset % item_size != 0 || dst_offset % item_size != 0))
   item_size /= 2;
```

vkQuake stages the upload with **alignment 1** — `R_StagingAllocate(size_to_copy, 1, …)`
(`gl_rmisc.c:758`), where the allocator is literally `q_align(current_offset, 1)` (`:697`) and
`current_offset += size` (`:713`), and `region.srcOffset = staging_offset` (`:761`). So each model's
`srcOffset` is the **byte-granular running sum of every prior upload in that staging batch**. The
destination offsets are 4096-aligned (heap block allocations), so `item_size` is decided by the
source offset alone: an **odd** staging offset forces `item_size = 1` (R8UI) and a raster TLB stride
of `width × item_size` (`v3dvx_meta_common.c:812`) — **75 bytes** for flame's 9600-byte upload
(framebuffer 75×128). Even-but-not-4 forces 2.

This is the missing **"why flame.mdl and not zombie.mdl"** factor: one model in the scene can end up
on a different, byte-strided, unaligned TLB store path from every other model, decided by nothing
but load order. Combined with R1 it gives a clean two-factor account — **R2 = deterministic,
model-specific susceptibility; R1 = stochastic, per-boot trigger.**

**Honest caveat:** no code evidence was found that a 75-byte raster stride or an unaligned raster
base is *invalid*. The CLE address field is a full 32-bit byte address with no shift
(`external/mesa/src/broadcom/cle/v3d_packet.xml:492`, `:529`), so nothing is truncated in the packer,
and `v3dv_image.c:230-260` applies a stride alignment fixup only under `USE_V3D_SIMULATOR`. This is a
testable hypothesis with a strong prior, **not a located defect** — and it is cheap to test (E2b).

### R3 ★★ — Thin, uninterlocked coherency margins around that single upload submit
`v3d_phoenix_winsys.c:962-976` (and the same bracket in `ioc_submit_csd:1654-1659`),
plus `l2t_flush_wait` at `:805-809`

```c
/* #67 ORDERING FIX (2026-07-26): ... SLCACTL has no completion bit on V3D 4.2, and Phoenix's
 * l2t_flush_wait REMOVES the interlock Linux relies on ... Confirmed a RACE (not a producer bug)
 * by a 5-boot HW discriminator: VBO source bytes byte-identical across boots yet torch/monster
 * geometry still mangle-varied. */
c0[CTL_SLCACTL/4] = SLCACTL_INVAL_ALL;
```

`SLCACTL_INVAL_ALL = 0x0f0f0f0f` invalidates TVCCS/TDCCS (vertex/coefficient), UCC and ICC with **no
completion bit**; correctness rests on incidental settle latency before the kick. And
`l2t_flush_wait` — called 8× per CL submit (`:1001`, `:1003`, `:1015`, `:1017`, `:1148`, `:1150`,
`:1259`, plus the TFU/CSD epilogues) — exhausts its 1 M spin budget with **no return value at all**,
so the caller proceeds regardless; its own comment (`:797-804`) says a missed wait makes CT1 fetch a
stale tile list, and `:1006-1014` records that removing the redundant "fix-A" flush regressed to
intermittent wedges in 1 of 3 boots. Margins here are empirically thin.

This is the in-port, HW-confirmed race that produced *this exact symptom on this exact model* under
GL. Ranked below R1/R2 only because as written it is per-job, so on its own it predicts mixed
frames — which §1 excludes. **Applied to the one-shot upload submit it latches**, and then it is
indistinguishable from R1 except in the log (a race produces wrong bytes with no wedge line; a drop
produces zero bytes with a wedge line). §4's perfect correlation with a *logged* wedge favours R1.

### R4 — REFUTED: silent MMU violation redirected to the zeroed scratch page
`v3d_phoenix_winsys.c:842-848` arms `MMU_CTL_PTI_ABORT` + the `MMU_ILLEGAL_ADDR` scratch redirect
(scratch allocated and zeroed `:307-317`), so a faulting access returns zeros and the job completes
with FLDONE/FRDONE, while `MMU_VIO_ADDR`/`MMU_VIO_ID` are read only inside the rate-limited timeout
dumps (`:1092/:1096`, `:1222/:1226`) and `v3d_gpu.c` does not define them at all. **This is a real
and serious blind spot in the port and should be fixed** (read and report VIO on every job, not only
on a timeout) — but as an explanation of *this* symptom it is refuted by the shared-BO constraint:
an unmapped or mis-mapped mesh BO would remove `zombie.mdl` and `v_shot.mdl` too.

### R5 — REFUTED: generation-less BO handles / stale `GEM_CLOSE`
The winsys hazard is real: handles are `slot + 1` with no generation counter
(`v3d_phoenix_winsys.c:489-494`, `:657`), freed slots are reused with the same handle value
(`:546-547`), and `ioc_close_bo` (`:668-690`) `va_free`s + `munmap`s whatever occupies the slot.
**But it cannot fire here, on two independent grounds.** (a) A *repeat* close of the same handle is
safe — `ioc_close_bo` clears `used`/`handle` (`:686-688`) so `bo_find` returns NULL and it returns 0
(`:671`). (b) V3DV cannot emit a stale or duplicate close: `v3dv_bo_free` is refcounted
(`external/mesa/src/broadcom/vulkan/v3dv_bo.c:485-486`, `p_atomic_dec_zero`), `bo_free`
(`:147-165`) memsets the struct before the ioctl using a saved local handle so a double free hits
`p_atomic_dec_zero(0)` and returns with **no ioctl**, and cache eviction (`:448-476`, the 2-second
staleness timer) goes through `bo_remove_from_cache` + `bo_free` exactly once under the cache lock.
Gallium's `v3d_bufmgr.c` is not in the V3DV link closure. And the shared-BO constraint would rule it
out anyway. **Keep the generation counter as a hardening item; do not chase it for #67.**

### R6 — Latent silent-failure sites found en route, judged NOT the cause (record them, fix them)

| site | what is swallowed | why not this bug |
| --- | --- | --- |
| `v3d_phoenix_winsys.c:1620` advertises `DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE = 1` while `phoenix_v3d_ioctl`'s `default:` returns 0 (`:1763-1764`) and **no `DRM_V3D_SUBMIT_CPU` handler exists**. V3DV *does* use it — `external/mesa/src/broadcom/vulkan/v3dv_queue.c:310`, `:544`, `:617`, `:698` (reset-queries, end-query, copy-query-results, timestamp-query, CSD-indirect) | those job types are complete no-ops reported as success | query/timestamp results only; nothing geometric |
| `v3d_gpu.c:847` — the daemon's CSD wait is still the **pre-fix 8 M spin budget** (winsys is 80 M since `1010570`) | the exact black-world bug the winsys was fixed for, reproducible via `/dev/v3d-srv` | vkQuake uses the in-process winsys; and no `CSD TIMEOUT` appears in any log |
| `ioc_submit_tfu:1466-1476` — *"return success so rendering proceeds (a failed upload leaves the image zero)"* | a whole texture stays zero | loud (`TFU TIMEOUT/FAIL`), absent from every log; and a zero *texture* gives a black silhouette, not absent geometry |
| `ioc_submit_csd:1702-1705` | dispatch output lost | loud (`CSD TIMEOUT`), absent from every log |
| binner-overflow pool zeroed once at init (`:341`) but the cursor reset per job (`:938`) without re-zeroing; `INT_OUTOMEM` not in the per-job `INT_CLR` mask (`:1023`, `:1134`) | stale next-block pointers in the spill region; a latched OUTOMEM carried into the next job | `ovf_armed=0` in every observed wedge — overflow never armed in these runs |
| `s->qma == 0` ⇒ CT0QMA/QMS/QTS silently inherited from the previous job (`:1025-1026`) | binner writes tile lists into a possibly-recycled VA | no evidence it fires; V3DV always sets tile state, and the `tile_state == NULL` guard is `v3dv_queue.c:1001-1023` |
| `va_free` drops the hole at `MAX_HOLES` (`:527-529`); `v3dv_bo_free`'s `page_index = size/4096 - 1` underflows to `0xFFFFFFFF` for a 0-byte BO, which the winsys makes reachable by rounding 0 → 1 page and returning success (`:534-540`, `external/mesa/src/broadcom/vulkan/v3dv_bo.c:493`, `:511-525`; `bo_from_cache` has the guard at `:97-100`, the free path does not) | bounded VA leak; out-of-bounds `list_addtail` into a zero-length `size_list` | leak is benign; the underflow is heap corruption, not a clean per-draw drop. **Wants an assert.** Same root cause weakens the Phoenix zero-tile guard at `external/mesa/src/broadcom/vulkan/v3dv_queue.c:1001-1022` (which drops a whole `GPU_CL` job, warning **once** then silent forever) because `v3dv_bo_alloc(0)` no longer returns NULL here |
| `v3d_get_device_info` ignores `os_get_page_size`'s return (`external/mesa/src/broadcom/common/v3d_device_info.c:78-81`) → `devinfo->page_size` would be stack garbage on failure, feeding `align()` in `v3dv_cl.c:94` and `v3d_util.c:309` | a wrong page size everywhere | does not fire: libphoenix implements `_SC_PAGESIZE` (`sources/libphoenix/unistd/conf.c:46-48`). One-line fix anyway |
| `v3dv_cl_ensure_space*` ignore `cl_alloc_bo`'s failure and leave `cl->next` stale (`external/mesa/src/broadcom/vulkan/v3dv_cl.c:155-181`; worst at `v3dv_uniforms.c:498`, no `v3dv_return_if_oom`) | writes past the old CL BO | host-OOM-gated, and `v3dv_cmd_buffer.c:682-686` then destroys the whole job |

**Explicitly refuted by code, do not re-open:**

- **BO-list omission cannot break mapping on this port.** PTEs are written at `ioc_create_bo`
  (`v3d_phoenix_winsys.c:649-652`) into one flat page table, and `ioc_submit_cl` never reads a BO
  handle list — `grep bo_handles v3d_phoenix_winsys.c` returns nothing, and `:1645-1647` says so.
  (Upstream confirms the list is fence-only even on Linux: `v3dv_job_add_bo_unchecked` bumps
  `bo_count` without deduping, `v3dv_cmd_buffer.c:71-77`, and `v3dv_cl.c:116-124` never adds
  `CHAIN_WITH_BRANCH` continuation BOs at all.)
- **`attr.maximum_index` cannot clamp the flame fetch.** `maximum_index = MIN2(0xffffff, c_vb->size /
  stride)` with `c_vb->size = buffer->size - offset`
  (`external/mesa/src/broadcom/vulkan/v3dvx_cmd_buffer.c:2653-2654`, `v3dv_cmd_buffer.c:3536-3539`).
  For flame's xyz binding (stride 12, size `9600 − 1440·pose`) that is `800 − 120·pose`: 800 at pose
  0, 200 at pose 5, against 120 vertices needed — 1.7×–6.7× **generous** at every pose, and it
  ignores `pipeline->va[i].offset` so it errs generous rather than tight. The ST binding at offset
  8640 gives exactly 120. (What the HW does when `maximum_index` < the drawn index is **not
  documented in this tree** — `broadcom/cle/v3d_packet.xml` defines the field, not its overflow
  semantics. Moot here, since it never underflows.)
- **Binner tile-alloc exhaustion does not truncate** — it stalls, and the stall is loud
  (`binner overflow pool EXHAUSTED` → `BIN TIMEOUT`). `ovf_armed=0` in every observed wedge.
- **CPU-cache incoherence is unavailable under Vulkan on this port.** V3DV allocates every BO with
  `flags == 0` (`v3dv_bo.c:242-249`), so the winsys always takes the `MAP_UNCACHED` branch
  (`v3d_phoenix_winsys.c:627-630`).
- **Descriptor / dynamic-offset / uniform-stream tracking has no defect.** Dynamic offsets are
  compared individually and dirty the state (`v3dv_cmd_buffer.c:3935-3943`); the uniform rewrite and
  `GL_SHADER_STATE` re-emit key on the same flags (`:2432-2538`, `:3031-3032`), cleared only after a
  successful emit (`v3dvx_cmd_buffer.c:2707-2711`); all uniform/texture/state BOs are collected
  (`v3dv_uniforms.c:706-728`); and `v3dv_cl_reloc` captures `{bo, offset}` per use (`v3dv_cl.h:83-97`)
  so a later CL-BO switch cannot dangle an earlier draw's address.
- **BO liveness / VA aliasing / MMU mapping** — refuted by the shared-BO constraint under R1, and by
  R5's two independent grounds.

---

## 8. Experiments, cheapest-decisive first

Standard cycle throughout: `--capture-secs 240`, Bash `timeout: 420000`. Grade with
`./scripts/check-torch-rois.py --rate <label>` (never a single frame, never by eye — see #67's
five false closures).

### E1 — free, no new Pi time: test the §4 prediction against the running bench

The prediction is **pre-registered**: *a boot renders the torches iff its UART log contains no
`BIN`/`RENDER TIMEOUT` line whose `ct0ca`/`ct1ca` lies in page `0x0acd5xxx` or `0x0acd0xxx`.*

```
./scripts/check-torch-rois.py --rate vkq-lerp2b
grep -a -o -E "ct[01]ca=0x[0-9a-f]+\[[0-9a-f]+\.\." artifacts/rpi4b-uart/*vkq-lerp2b-T*.log
```

Every new trial the bench produces is a free test. A single counter-example (a PRESENT boot with the
marker, or an ABSENT boot without it) falsifies §4 and sends the hunt back to R2/R3/R5.
**Run this first — everything downstream depends on it.**

### E2 — one cycle, no rebuild: intervene on the wedge rate

If R1 is right, reducing the amount of one-shot GPU upload work should raise the torch rate.
`gl_max_size` and `gl_picmip` both exist in vkQuake (`external/vkquake/Quake/gl_texmgr.c:52-53`,
registered `:717-718`) and shrink the init-time texture uploads without moving the camera, so the
fixed torch ROIs and the `mae` viewpoint gate stay valid:

```
./scripts/test-cycle-bench.sh 6 vkq-picmip -- "vkquake +gl_picmip 3 +r_lerpmodels 2 +map start"
./scripts/check-torch-rois.py --rate vkq-picmip
```

- torch rate rises **and** wedge count falls ⇒ the upload phase is causal (R1/R2/R3 family).
- rate unchanged with the same wedge count ⇒ the intervention was too weak; go to E2b/E3.
- **Also record the wedge addresses** — if the marker pages move with the reduced texture set while
  the correlation holds, §4 is identifying a *job*, not a sequence position.

### E2b — one-line engine change, tests R2 directly (needs a vkQuake relink only, no core rebuild)

`external/vkquake/Quake/gl_rmisc.c:758` — change `R_StagingAllocate(size_to_copy, 1, …)` to
alignment **4**. That forces every mesh upload's `srcOffset` to a multiple of 4, so V3DV's
`item_size` loop (`v3dvx_meta_common.c:1451-1457`) always picks 4 and every model takes the same
`R32UI` raster path with a 4-byte-multiple stride.

```
./scripts/test-cycle-bench.sh 8 vkq-stagealign4 -- "vkquake +map start"
./scripts/check-torch-rois.py --rate vkq-stagealign4
```

- rate → 8/8 ⇒ **R2 confirmed**; the real fix belongs in V3DV (force `item_size = 4` by staging
  through an aligned scratch, or reject the sub-4 raster path on V3D 4.2), and it would retire the
  two quakespasm band-aids too.
- rate unchanged ⇒ R2 refuted as *sufficient*; R1 alone carries it, go to E3.

Cheap companion, same relink: print `staging_offset` and its alignment class at `flame.mdl`'s vertex
upload (`gl_rmisc.c:758-761`) over UART, and check across boots whether it varies at all. If it is
**constant** across boots then R2 cannot be the intermittency source on its own (only the
susceptibility), which is already how §0 frames it — but measuring it removes the assumption.

### E3 — the decisive instrument (needs a GPU-lib rebuild + relink; owner-gated)

One env-gated print in the winsys, modelled on the existing wedge dumps (which are already env-free,
zero-cost-on-happy-path). Add `V3D_JOB_LOG`:

1. In `ioc_submit_cl`, on entry: job serial, `bcl_start`/`bcl_end`, `rcl_start`/`rcl_end`,
   `qma`/`qts`, and the **`bo_handles` list** the submit carries (it is currently discarded —
   `v3d_phoenix_winsys.c` has no `bo_handles` reference at all).
2. In the `job_retry` drop path (`:1250-1257`), print that same identity, so a dropped job names its
   BOs.
3. In `ioc_create_bo`, one line per BO: handle, gpuva, size, flags.

Then one boot with `V3D_JOB_LOG=1` on a marker (ABSENT) run and one on a marker-free (PRESENT) run,
and diff. This answers, in one cycle: *is the dropped job a transfer, and is its destination BO the
one vkQuake bound as `flame.mdl`'s vertex buffer?* Nothing cheaper can answer it, because the
identity of the dropped job is currently not printed anywhere.

**Build note:** this touches `sources/phoenix-rtos-devices`, so it needs `--scope core` and the
`.gpu-libs` relink, and the shader disk cache must be cleared on restage
(`project_v3d_shader_disk_cache` footgun).

### E4 — the candidate fix, once E3 confirms (do not ship before E3)

Make the drop non-silent, and retry transfers. Two independent changes:

1. **Retry once for a non-frame job.** The current no-retry rule is justified for a *frame*
   (`:1237-1240`: re-submitting the same frame re-hangs across resets, HW-confirmed) but a one-shot
   transfer is worth one retry after a true reset — the data is still there and a second attempt
   costs one hitch, versus permanently wrong output.
2. **Return an error the stack can see.** `return -EIO` from a dropped job, and make
   `drmSyncobjWait` propagate it, so V3DV raises `VK_ERROR_DEVICE_LOST` instead of vkQuake believing
   its upload landed. Loud-and-broken beats quiet-and-wrong; this is the same lesson as
   `docs/misc/2026-09-03-csd-wait-cntvct-deadline.md` ("Better to be slow once than wrong quietly").

Validate with `test-cycle-bench.sh 8` + `--rate`, never a single boot.

### E5 — cheap orthogonal check, no rebuild

```
./scripts/test-cycle-bench.sh 3 vkq-e1m1 -- "vkquake +map e1m1"
```

`e1m1` also carries `light_torch_small_walltorch`. Same model, different precache order, different
upload sequence and VA layout. Grade **by eye** (the ROI spec is `start.bsp`-only) purely for
present/absent, and read the wedge addresses. If the torch presence tracks the marker in a second
scene, §4 generalises.

### Explicitly NOT worth running

- `V3D_BIN_CRC=1` as a "layout perturbation" — it CRCs the existing tile_alloc BO and does not shift
  the GPU VA layout, so a null result would be uninformative.
- `+r_showtris 1` — **inert on this port**: the shim hardcodes
  `vulkan_globals.non_solid_fill = false` (`sources/phoenix-rtos-ports/vkquake/glue/pl_phoenix_vk_vid.c:876`),
  so `R_ShowTris` early-returns (`external/vkquake/Quake/gl_rmain.c:763`) and those pipelines were
  never built. The `vkq-showtris` run proves nothing about geometry.
- Any single-boot verdict, of any kind.

---

## 9. What would falsify the §4/R1 reading

State these up front so the next session cannot close #67 on a coincidence:

1. **A PRESENT boot whose log contains an `0acd5`/`0acd0` wedge**, or an **ABSENT boot with none**
   (E1). Either kills the marker.
2. **E3 showing the dropped job is a scene render, not a transfer** — then the latching in §1 needs a
   different explanation and R2/R3/R5 move up.
3. **A boot with zero wedges that still loses the torches.** One such boot would refute R1 outright.
   The only zero-wedge boot on record (`vkq-lerp-rep`) is PRESENT — **n = 1**, so this is currently
   the weakest link in the whole argument, and E1/E2 should hunt specifically for more zero-wedge
   boots. (If zero-wedge boots turn out to be reliably PRESENT, R1 is close to proven; if a
   zero-wedge boot loses the torches, the answer moves to R3 — a silent coherency race on the same
   upload submit, which produces wrong bytes with *no* wedge line.)
4. **E2b (staging alignment 4) making the torches 8/8 reliable** would confirm R2 as the
   susceptibility and, by itself, be a shippable workaround — but it would **not** absolve R1's
   silent drop, which stays a latent hazard for every other one-shot transfer in the system. Fix
   both.

**Already answered, do not re-ask:** `flame.mdl`'s mesh is **not** in its own BO — every alias mesh
suballocates from one 16 MiB `mesh_buffer_heap` segment = one `VkDeviceMemory` = one Phoenix BO
(`external/vkquake/Quake/gl_mesh.c:538`, `gl_heap.c:213`, `:659`,
`external/mesa/src/broadcom/vulkan/v3dv_device.c:2232`). That is what rules out R4/R5 and forces the
explanation to be content- or offset-level.

## 10. Record corrections this analysis makes

- `docs/pi4-hardware-support-matrix.md:94`, `:112` — "an intermittent V3D binner wedge on long GPU
  runs" is cited to `project_vulkan_v3dv_port`, which contains **zero** occurrences of "wedge",
  "BIN TIMEOUT", "RENDER TIMEOUT" or "binner". The real homes are
  `project_quake3_lightmap_uif_xor` (C0), `project_supertuxkart_feasibility` (C3),
  `project_v3d_render_stall_reframe` (C1/C2).
- The characterisation gap *"no wedge has ever been observed under vkQuake/V3DV"* is **false**: every
  vkQuake run logs 1–5, with a distinct sub-mode (§3) — tiny meta-copy control lists, `int_sts=0`,
  `ct1ca` at or past `ct1ea`, `RENDERFAULT gpuva=0x00000000`.
- The gap *"no per-frame/per-draw correlation instrument; that correlation has never been measured"*
  is now partly closed: §1 (per-boot latching), §3 (wedge count uncorrelated), §4 (wedge *address*
  perfectly correlated).
- Root-cause-doc candidate **R2 ("statics never reach the alias draw list") is refuted** by §2.
- The C1 `fdbgs` value is recorded two incompatible ways (`0x000350ef` in
  `docs/done/2026-06-21-render-stall-binner-hang.md:59-66` vs `0x47/0x07` + `errstat=0x1000` in
  `project_v3d_render_stall_reframe`); unreconciled, flagged for whoever picks up C0.
- **`vkCmdCopyBuffer` on V3D 4.2 is a CL render job, not a TFU blit** — the TFU fast path in
  `external/mesa/src/broadcom/vulkan/v3dvx_meta_common.c:1445-1515` is behind
  `#if V3D_VERSION >= 71`. Every buffer upload on this hardware therefore goes through the binner +
  render path and is exposed to the wedge/drop mitigation. Worth stating explicitly wherever the port
  is described as using TFU for uploads.
- The port advertises `DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE = 1` (`v3d_phoenix_winsys.c:1620`,
  `libv3d-client.c:245`) with **no `DRM_V3D_SUBMIT_CPU` handler** — `phoenix_v3d_ioctl`'s `default:`
  returns 0 (`:1763-1764`). V3DV uses that ioctl at four queue sites
  (`external/mesa/src/broadcom/vulkan/v3dv_queue.c:310`, `:544`, `:617`, `:698`), so
  reset-queries / end-query / copy-query-results / timestamp-query / CSD-indirect are silent no-ops
  reported as success. Same shape as the pre-fix `SUBMIT_TFU`/`SUBMIT_CSD` stubs
  (`:1291-1294`, `:1637-1640`). Not the torch bug; fix it before anyone trusts a Vulkan timestamp.
- `v3d_gpu.c:847` — the **daemon's** CSD completion wait is still the pre-fix `8000000u` spin budget
  while the winsys is `80000000u` (since `1010570`). Anything routed through `/dev/v3d-srv`
  reproduces the black-world bug the winsys was fixed for.
