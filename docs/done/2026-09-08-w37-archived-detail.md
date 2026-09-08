# Week 2026-W37 — archived detail (moved out of the weekly log 2026-09-08)

The weekly log had grown to 812 lines; the owner asks for it SHORT. Everything below is
**resolved or superseded** and was moved here verbatim. Current state stays in
`docs/inprogress/WEEK-2026-W37.md`.


---

<!-- archived: the four bugs (#1-#4), in full -->

## 3. IN PROGRESS — the four bugs

**#4 — the KERNEL fault is FIXED and HW-verified.** Root cause, decoded exactly from the
second crash: `0xffffffff00786f62` is `"box\0"` preceded by `"/dev"` + `"/vcm"` — the string
**`/dev/vcmbox`**, copied by `hal_strcpy` into `proc_portLookup`'s `char pstack[16]`
**kernel-stack** buffer (`proc/name.c:339`). Laid at `thread_t+96` it covers `idlinkage.id`,
the padding and the low half of `process`, leaves the high half at `0xffffffff`, and never
touches `magic` at +336 — which is why my own guard could not fire: the victim is a live,
correctly-stamped thread whose memory somebody else's **stack** is writing.

The writer is a vfork child. `process_vforkThread` lends the child the **parent's** kernel
stack; my first commit stopped the *child's* `thread_destroy` freeing that borrowed stack,
but not the mirror case — an async death of the **parent** frees the stack while the child
still runs on it, and since every kernel stack is its own kmalloc zone the block is
unmapped and its pages recycle straight into the next 512-byte zone full of `thread_t`s.
Fix: mark the lender for the duration of the borrow and skip the free (kernel `6d8f40a5`;
manifest `manifests/2026-09-07-kernel-vfork-kstack-fix.md`).

**HW result: the EL1 Data Abort that reproduced twice is gone.** What remains in the same
teardown is a **userspace (EL0)** fault — `pc` inside a user binary, one branch to
`0xffffffc8` — i.e. wmaker's own SIGTERM path crashing. Separate bug, far less severe, not
yet triaged. Three real kernel lifetime bugs were fixed along the way (`execkstack` never
initialised, the child-side borrow, the parent-side borrow) plus magic-stamp guards on
`thread_t`/`process_t`.

**#1 — NOT REPRODUCED by a sound measurement. I was wrong twice; here is the corrected
picture.**

Two probes settle it. After the drag burst the client asks for `700x480`, then polls the
server; then it asks for a *different* size (`701x481`) and polls again — that second ask
directly tests the "WM thinks it is already there" early-out:

```
PROBE1 round 0..2  after 0 polls  server=700x480  APPLIED     (3/3)
PROBE2 round 0..2  asked=701x481  server=701x481  APPLIED     (3/3)
SETTLE round 0..2  server=701x481 client=701x481 frame=701x512  CONSISTENT
```

Both sizes are applied within the first 200 ms poll, and once measured on a **quiescent**
window the server and the client **agree**, with the frame equal to the client plus
decorations. **WindowMaker, the X server and the V3D path all resize correctly.**

⚠️ **Retractions.** My last two entries on #1 were both wrong, for two different
measurement mistakes:
1. *"Real, roughly 1 in 3"* — graded by eye from HDMI frames. With 3 s drags and 12 s holds
   back-to-back, a periodic grab lands mid-burst; those frames were **transients**, not
   settled states.
2. *"Deterministic, 10/10, localised to WindowMaker"* — the geometry was read **immediately
   after `XResizeWindow`**, racing the round-trip, so it printed the *pre-resize* size. The
   `633x415` I treated as evidence was simply the drag's last size, not yet replaced. Moving
   the read to after the hold makes the mismatch vanish.

**Where that leaves #1:** the owner's report is **unexplained but not reproduced**. What is
now verified is that resize *correctness* is fine. What a user watching a live drag sees is
a different matter: this stack has no compositing and presents by GPU readback, so
intermediate frames during a fast drag are expected and will look like artefacts. That is a
fidelity/latency characteristic, not a geometry bug — and it is the most likely thing the
owner actually saw. Worth asking him whether the artefacts persisted *after* he let go of
the mouse; if they did, this needs a different hypothesis entirely.

**#2 — FIXED and HW-verified.** Root cause is a line of **our own** code:
`external/mesa/src/mesa/state_tracker/st_atom_framebuffer.c:137` forces `Y_0_TOP` for
**any** framebuffer `>= 1024x768`. It is a **size** gate, not a scanout gate — it exists
because our in-process context never gets a window-system framebuffer, so `FlipY` is false
everywhere. Quake II's underwater post-process FBO is 1920x1080, trips the gate, and
upstream's compensating flip then lands on top of ours.

**Proved on hardware, not inferred.** A temporary `gl3_forceunderwater` cvar made it
reproducible without swimming, and the size gate predicts an exact discriminator:
- `viewsize 100` (FBO 1920x1080, gate trips) → view **upside down** ✔ observed
- `viewsize 70` (FBO 1344x756, height < 768, gate misses) → view **upright** ✔ observed

Fix (fork `83235581`, ports `a196466`): un-flip the underwater blit under `#ifdef
__phoenix__` in `gl3_draw.c`. HW after: underwater **upright** at full viewsize with the
water warp visible, normal path unchanged, 0 faults.

⚠️ **This is a documented STOPGAP, not the end of the story.** It is a conditional
correction on a conditional bug: at `viewsize <= 71` the FBO drops under the gate and the
fix over-corrects. The real fix is to declare the scanout FBO `FlipY` — our driver does
report **`GL_MESA_framebuffer_flip_y`** — and delete the size gate, after which this hunk
AND glamor's three hand-rolled flips (`PHX_READBACK_FLIP_Y` + the two `glamor_transfer.c`
hunks, which exist for the same reason: the X screen pixmap is big, offscreen pixmaps are
small) all come out together. That needs one build plus a re-verification pass over five
games and the X desktop, so it is queued, not rushed.

⚠️ **TD:** the `gl3_forceunderwater` cvar is diagnostic-only and still in the fork
(default 0, inert). Remove it when the structural fix lands — it is the tool that will
verify it.

**★★★★ #3 — ROOT-CAUSED AND FIXED IN THE DRIVER.** Not a workaround: Quake III now
renders with **multitexture ENABLED** (stock default), 0 dropped draws, 0 faults.

**The bug:** `u_vbuf_translate_begin()` failing makes its caller `goto out`, which **skips
`pipe->draw_vbo()` entirely** — the draw is dropped with no trace, because
`debug_warn_once()` is inert in release builds. So world surfaces simply never reached the
GPU, which is why the GL state was provably correct while the screen was black.

**How it was found:** made the failure report itself, and it named the trigger exactly:

```
u_vbuf: TRANSLATE FAILED - DRAW DROPPED unroll=1 incompat_vb=0x0 incompat_elem=0x0
        misaligned=0x0 nelem=3
```

`incompatible_vb_mask`, `incompatible_elem_mask` and `misaligned` are all **zero** — the
only reason u_vbuf entered the translate path at all was the **index-unrolling
optimisation**. Multitexture triggers it because it adds a second texcoord vertex element.

**The fix** (`patches/mesa/phoenix-rpi4-v3d.patch`, mesa `491ad782`→`u_vbuf.c`): decline
index unrolling on Phoenix — it is a pure upload-ratio heuristic, so declining it is always
correct and the draw takes the ordinary indexed path — **and keep the failure loud**, so
this class can never fail silently again. The engine-side `r_ext_multitexture` workaround
is **reverted** (fork `5d2f0f40`, ports `edd3359`); manifest
`manifests/2026-09-07-mesa-uvbuf-dropped-draw-fix.md`.

This fixes a **silent-draw-drop class affecting every GL app**, not just Quake III — and it
is the same path our own earlier note blamed for a "quake3 crash mid-combat".

⚠️ **Still open upstream-wise:** *why* `translate_generic` cannot build that key here
(aarch64 has no `translate_sse`, so it is the only path). Declining the optimisation avoids
it; understanding it would allow re-enabling unrolling.
✅ **Diagnostics removed** — `PHXDIAG` (quake3e `23d284ff`) and `gl3_forceunderwater`
(yquake2 `9c2e6bab`) are gone from the forks and the shipped binaries; patches regenerated
(ports `bcaeedf`), `game-port-patch.sh --check` clean on all four.

**★ COHERENT REBUILD + REGRESSION SWEEP (the Mesa fix touches every GL app, so this was
mandatory, not optional).** Only Quake III had been relinked against the fixed driver — the
other games and the X server still carried the old one, i.e. the export was a mixed build.
Rebuilt `--scope core --with-showcase --with-ports`, confirmed each GL binary now carries
the fix, regenerated the pristine export and cleared the shader cache. All re-verified on
HW, **0 faults and 0 dropped draws each**:

| consumer | result |
|---|---|
| `startx_gpu deskapps` (glamor X desktop) | ✅ full desktop, no regression |
| QuakeSpasm | ✅ renders |
| Quake II | ✅ renders (and the #2 underwater fix survived the rebuild) |
| Quake III | ✅ renders **with multitexture ENABLED** — the bug is gone |
| SuperTuxKart | ✅ UI renders; still NFS-load-bound as before (unchanged) |
| vkQuake | n/a — Vulkan path, does not link libGL, unaffected by the change |

**Reproducers now live in the repo** (`tests/pi-repro/`) instead of being hand-staged onto
the NFS export, so they survive an export refresh: `wmexit.sh` (#4), `sigterm-child.sh`
(the X-free control), `xresize*.sh` (#1).

**GAME RE-VERIFICATION after the rebuild — DONE.** The export was regenerated, so every
engine binary was new and none had been run. All five now checked over netboot, **0 faults
each**:

| game | command | result |
|---|---|---|
| QuakeSpasm | `quakespasm +map start` | ✅ demo-quality (start map, torches, HUD) |
| Quake II | `quake2 +map demo1` | ✅ demo-quality, right way up, GLES3 refresher |
| Quake III | `quake3 +devmap q3dm1` | ✅ demo-quality (textures, lightmaps, HUD, weapon) |
| vkQuake | `vkquake +map start` | ✅ demo-quality (Vulkan path) |
| SuperTuxKart | `stk` | ⚠️ UI + asset load render correctly and **progress** (kart icons appear, tips rotate), but the track is **not reached within ~5 min** over NFS |

**STK is the one demo risk.** It is not broken — it is NFS-load-bound; the earlier
in-game proof was from SD boot. For a screen recording, run STK from the SD card (which is
the owner-blocked flash step) or accept a long lead-in.

Useful by-product: Quake II's own `GL_EXTENSIONS` dump confirms **`GL_MESA_framebuffer_flip_y`
is supported** by our driver, so the structural fix for #2 (declare the scanout FBO FlipY
and delete the size gate) is available rather than hypothetical.


---

<!-- archived: resolved work + superseded images + earlier upstream sweeps -->

## 4c. RESOLVED: NFS fast path + the STK stack overflow

Both root-caused and fixed. **Full record, including the wrong turns and retractions:**
[docs/done/2026-09-07-nfs-fast-path-and-stk-stack-overflow.md](../done/2026-09-07-nfs-fast-path-and-stk-stack-overflow.md).

**1. Path resolution was quadratic** (`phoenix-rtos-filesystems`, pushed). A depth-*d* `stat()`
made `d(d+1)/2 + 2d + 1` NFS round trips to describe *d* paths. Fixed with a 100 ms positive
attribute cache plus reuse of the libnfs directory snapshot across a scan (it had been
re-listing the whole directory *per entry*). HW-measured, same tool and sample throughout:

| per file | before | after |
|---|---|---|
| `stat` | 46 781 us | **5 587 -> 2 765 us** (8.4x) |
| `open` | 48 972 us | **4 698 us** (10.2x) |
| `readdir` | 38 936 us | **277 us** (142x) |
| 150-file walk | 20 918 ms | **1 848 ms** (11.3x) |

End-to-end: 243 STK asset files / 3 MB over NFS **32 s -> 3 s**. This was the answer to the
owner's "look at the asset loading code path" — it was never NFS throughput (25 MB/s).

**2. STK's crash was not nfs-fs** (`phoenix-rtos-ports 4eed9f0`, patch 0011). `char pcm[44100]`
on the stack in `music_ogg.cpp:329`, on the SFX thread — which `std::thread` creates with a
NULL attr, so it gets libphoenix's default **4 KiB** stack and **`guardsize = 0`** (no guard
page). `ov_read` wrote 44 KiB from ~40 KiB *below* the thread's own stack, silently smashing
neighbouring thread stacks (jump to a garbage LR) or heap chunk headers (faults inside malloc)
— one bug, two symptoms. Proven byte-exact: the kernel's dump of the faulting stack is
verbatim decoded `menutheme.ogg` PCM, and two crashes' corrupt return addresses sit exactly
`m_buffer_size` apart in that stream.

**Fix verified as effective, and it exposed a SECOND overflow of the same class.** With patch
0011 in, STK still faults 2/2 — but the stack window is now *completely different data*: heap
pointers followed by **sequential 32-bit integers** (0,1,2,3,4,7,5,6,8,9,10,11,12,13…), with
`pc = 0x0000000d0000000c`, i.e. literally the pair (12,13). None of those quads appear anywhere
in the PCM stream. So the audio overflow is genuinely gone and a different writer — a 32-bit
index/element array, this time plausibly generated rather than mesh data — is now hitting the
same 4 KiB thread stack (victim: thread 69 of `supertuxkart`).

### ★★★ GUARD PAGE BY DEFAULT — the class fix (`libphoenix 02ab4e0`, rebuilding)

Two overflows on default thread stacks, and fixing the first exposed the second: the right
target is the **class**, not the instances. `pthread_attr_default` had `guardsize = 0`, so
`pthread_create` skipped its `mprotect` and every default stack was a bare `mmap` with live
memory directly below. Now **one guard page by default**, so an overrun faults at the offending
store instead of silently landing in a neighbour's stack or the heap.

Two details that make it safe everywhere rather than just here:
- the `mprotect` is now **best effort** — a guard page is hardening, not a functional
  requirement, and it is unavailable on the NOMMU targets, where failing `pthread_create` over
  it would be a real regression; those continue unguarded exactly as before;
- an **explicit `guardsize = 0` is still honoured**, so a caller that genuinely wants no guard
  is not overridden.

Test added (`phoenix-rtos-tests adcceba`): asserts the default attr carries a nonzero guard, and
that an explicit 0 survives. Asserted on the *attribute* — a real overflow kills the process, so
a unit test could not report it — and nonzero rather than a fixed size, since page size is
per-arch.

**Verified on HW, and it worked exactly as designed.** Unit tests pass
(`test_pthread_guard`: 2 Tests, 0 Failures). Then STK: **one** fault instead of a cascade,
`esr=0x92000047` (a *write* translation fault), `far=0x0902cfe0` in a guard page — and it fired
**before** the kart-mesh stage (kartDirt=0) rather than minutes later somewhere unrelated. So
`pc` is the offending store, and it decodes:

**`alloc_small` (`jmemmgr.c`) from `jpeg_make_d_derived_tbl` — libjpeg**, building Huffman
decode tables for STK's `.jpg` textures.

**That reframes overflow #2: it is not a pathological buffer, it is 4 KiB simply not being
enough stack for ordinary library code.** Overflow #1 was a genuine STK bug (a 44 KiB frame),
correctly fixed in the port. #2 is libjpeg behaving normally on a stack no real code can live
in — a **libphoenix default problem**, and the case for raising the default is now evidence-based
rather than stylistic.

The sequencing paid off: had I raised the stacksize first it would have masked this, and I would
have "fixed" STK without learning that the default is unusable. Guard page diagnosed; stacksize
now mitigates, with the guard still catching genuine overruns.

**Default stacksize raised (`libphoenix`, rebuilding):** new `PTHREAD_STACK_DEFAULT`, set to
**256 KiB for aarch64**. An arch that does not define its own keeps `PTHREAD_STACK_MIN`, so the
memory-constrained NOMMU targets are untouched — the point is the MMU targets, where a 4 KiB
default is indefensible, not spending memory where it is scarce. The guard page stays: a bigger
default raises the ceiling, but a genuine overrun must still fault at the store.

Tests extended (`phoenix-rtos-tests`): a **64 KiB floor** on the default stacksize, so a
regression to "the POSIX minimum is the default" is caught in the suite rather than by a crash
inside some port; plus a thread created with the default actually **running an 8 KiB frame**,
since a stacksize the allocator cannot satisfy would surface as a `pthread_create` failure, not
a wrong attribute.

### ★★★★★ STK NOW RUNS CLEAN — 3/3 on the full advanced pipeline

`stkboth` + `stkboth2-T1/T2`: **0 faults, 34 shaders (full advanced pipeline), kartDirt x5 as
the last line** — the known-good "reached the menu, rendering silently" signature, in every
trial. Compare: **0/5** clean before the fixes, and 2/3 with the `--disable-dynamic-lights`
workaround I correctly refused to ship.

Benched to a rate rather than declared on one clean run — that single-run mistake is exactly
what the `--disable-dynamic-lights` episode cost earlier.

Unit tests also pass on HW: `test_pthread_guard` **4 Tests, 0 Failures** (guard present,
explicit 0 honoured, 64 KiB floor, and a default thread really running an 8 KiB frame).

Three fixes, one chain: STK's 44 KiB stack buffer (port patch 0011) + libphoenix's guard page +
libphoenix's usable default stacksize. **STK is now a candidate 6th demo component**, pending a
full re-gate of the demo set on this build.

**All four sibling repos pushed** (fast-forward, no force): `libphoenix 5fa3847..bad2009`,
`phoenix-rtos-tests 5982203..291708a`, `phoenix-rtos-kernel 6d8f40a5..76e0adbc`,
`phoenix-rtos-ports b36c396..4eed9f0`.

### ★★★★★ RE-GATE COMPLETE — 6/6 components, 18 boots, 0 faults

| component | trials | evidence |
|---|---|---|
| GPU X desktop | ✅ 3/3 | glamor up, 5 clients every trial |
| Quake III | ✅ 3/3 | q3dm1 + bots |
| QuakeSpasm | ✅ 3/3 | pak + `Host_Init` |
| Quake II | ✅ 3/3 | `demo1` + GLES3 refresher |
| vkQuake | ✅ 3/3 | Vulkan + `Host_Init` |
| **SuperTuxKart** | ✅ **3/3** | **NEW 6th component** — 34 shaders, full advanced pipeline |

All on the build carrying the three core changes the previous image never had: the pthread
**guard page**, the **256 KiB default stack**, and the kernel **user-fault stack window**. Those
are exactly the changes most likely to disturb long-running GPU/X11 work through an extra
`mprotect` per thread and a shifted memory layout — 18 clean boots says they do not.

**SuperTuxKart joins the demo set**, which is the first time it has been gate-eligible: it was
"⚠️ not benched" in the original gate and crashed 0/5 before the three fixes.

**Blast radius checked before shipping, not after.** A 64x bigger default could have broken the
existing 1000-thread churn regression test — it does not: that test sets its stacksize
explicitly (64 KiB burst / 256 KiB churn) and holds at most 8 threads concurrently. Phoenix's
own servers use `beginthread` with static stacks, so only pthread users (the ports) see the new
default. Incidentally that test already chose **256 KiB** by hand, which is independent support
for the value picked here.

**Kept from the hunt:** a bounded stack-window dump on user faults
(`hal/aarch64/exceptions.c`) — it is what made this findable, is inert on healthy runs
(desktop 2/2, Q3 2/2, 0 spurious dumps), and needs gating before it ships.


### ★★★ DELIVERABLE: DRIVABLE demo image — 6 components + working input

`artifacts/rpi4b/rpi4b-sd-2part.img`, SHA256 `0ddcee676f46ac57f607e7f7bd9f26b5e575fa0621863adc9b6428a0baaaa91e`.
Manifest `manifests/2026-09-07-drivable-demo-image.md`. **Contents gate: PASSES — safe to
flash.** Netboot `loader.disk` restored (`nfs;/` confirmed).

**This is the build to record with.** Gated **6/6 components x 3 trials = 21 boots, 0 faults**
(desktop 6/6, STK 3/3, Q3 3/3, QuakeSpasm 3/3, Q2 3/3, vkQuake 3/3) **and** it is the first
image with reliable keyboard + mouse: 6/6 boots with full USB enumeration (4 devices) and both
input devices reported active by X, where previously **1 boot in 3 had no input at all**.

Everything from the six-component image plus the **xHCI Disable-Slot recovery** fix.

**★ PIXEL-VERIFIED, not just log-verified.** The gate criteria are log-based (glamor up, map
loaded, shaders compiled) and cannot show what is on screen — which is the only thing that
matters for a recording. `scripts/check-hdmi-content.py` (new) grades the last HDMI tick of
each gated run from captures already on disk, no Pi time:

| component | non-black | colours | verdict |
|---|---|---|---|
| SuperTuxKart | 98.0% | 19 745 | **CONTENT** |
| GPU X desktop | 99.2% | ~1 100 | **CONTENT** (flat desktop colours) |
| Quake III | 58–64% | 17–26 k | **CONTENT** |
| vkQuake | 52–54% | ~10 k | **CONTENT** |
| QuakeSpasm | 50.6% | ~9 k | **CONTENT** |
| Quake II | 14.7% | ~6 k | threshold flagged it — **actually correct**, see below |

**#67 vkQuake torches — 9/9 runs present** on the shipped build (`xgvq-T1/T2`, `xgvqb-T1`,
`torch1-T1/T2`, `torch2-T1/T2`, `torch3-T1/T2`), **91/91 at-reference frames lit**, both archway
ROIs, mae 3.4–3.8. Meets the n>=8 standing rule for #67 — no screenshot-based claim.

**What 9/9 does and does not support.** It bounds the rate; it does not prove a fix. At the
historically reported ~15% per-boot rate, P(9 clean) ~ 23%; at 20%, ~13%. So the defect could
still be live and rarer than before. What it does say: **for the purpose of a recording the
torches are reliable enough** — and if a take is unlucky, re-running costs one boot. Not
claiming #67 closed.

Two frames inspected by eye rather than trusted to the metric:
- **SuperTuxKart**: full main menu — logo with gradient, checkered-flag background, all five
  mode icons with correct artwork, complete bottom toolbar, crisp text. Demo-quality.
- **Quake II**: fully textured scene, weapon model + hand, HUD (100 + health cross + weapon
  icon), lightmaps, reflective floor, lit banner. **Correct** — `demo1` is simply a dark indoor
  map, so my 25%-non-black threshold was wrong for it, not the render. Recorded so the next
  reader does not chase it.

### ★★ (superseded) SIX-COMPONENT SD image

`artifacts/rpi4b/rpi4b-sd-2part.img`, SHA256 `9cfcfef3a5a61520722d2140d6bea196f9f53c15cb9212f08ff14465e30b33ba`.
Manifest `manifests/2026-09-07-six-component-demo-image.md`.
**Contents gate: PASSES — safe to flash.**

**Gated 6/6 components x 3 trials = 18 boots, 0 faults**, plus post-merge spot checks on the
two most fragile (STK and the desktop) after the final rebuild. Netboot `loader.disk` restored
afterwards (`nfs;/` confirmed).

Carries: the nfs-fs fast path (stat 8.4x, open 10.2x, readdir 142x, 243-file asset read
32 s -> 3 s), STK's ogg stack-buffer fix, libphoenix's **pthread guard page** and **256 KiB
default stack**, the kernel **user-fault stack window**, upstream quake3e `f694bbbc`, and
upstream libphoenix `067ea81` (pushed after boot-test: `bad2009..2ec0497`).

### ★ (superseded) DELIVERABLE: 5-component SD image cut 2026-09-07

`artifacts/rpi4b/rpi4b-sd-2part.img`, 1 680 867 328 B,
SHA256 `6fb4c8f78f8aa4a03f77318a17e7842e9d2899586dace99744586620fb2eb365`.
Manifest `manifests/2026-09-07-nfs-fast-path-sd-image.md`.
**Contents gate: PASSES — safe to flash** (`verify-sd-image-contents.sh`), including the
positive/negative markers (v3d submit mutex present in vkquake; yquake2 free of
`gl3_discardfb`).

Carries: the nfs-fs fast path (stat 8.4x, open 10.2x, readdir 142x, 243-file asset read
32 s -> 3 s), the audit fixes, upstream quake3e `f694bbbc`, and the stk-launcher
`setenv overwrite=0` fix.

⚠️ **Be precise about what is verified on which build, because they are not the same build:**
- **5/5 demo gate at 3 trials each** (desktop, Q3, QuakeSpasm, Q2, vkQuake; 18 boots, 1
  unrelated fault) ran on the **pre-merge** build.
- The image adds the quake3e merge (Q3 only), the nfs-fs audit fixes (core), and the launcher
  fix (STK only). **Q3 was re-verified on it**; the other four were not re-run, though the
  build has since had **5 further clean boots** (q3sweep, ramdiag, stktmp x3 — 0 faults
  besides STK's own crash).
- ✅ **RE-BENCHED ON THIS EXACT IMAGE: 5/5 components x 3 trials = 15 boots, 0 faults.**

  | component | trials | evidence |
  |---|---|---|
  | GPU X desktop | 3/3 | glamor up, 5 clients, every trial (`imgdesk-T1`, `imgdesk2-T1/T2`) |
  | Quake III | 3/3 | q3dm1 + bots (`q3sweep`, `imgq3-T1/T2`) |
  | QuakeSpasm | 3/3 | pak + `Host_Init` (`imgqs-T1/T2`, `imgqs3-T1`) |
  | Quake II | 3/3 | `demo1` + GLES3 refresher (`imgq2-T1/T2`, `imgq2b-T1`) |
  | vkQuake | 3/3 | Vulkan + `Host_Init` (`imgvq-T1/T2`, `imgvqb-T1`) |

  So the image is gated on its **own** build, not inherited from a parent — which matters,
  because it carries the quake3e merge, the nfs-fs audit fixes and the launcher change that
  the earlier 5/5 never saw. **0 malloc/ntpclient faults in these 15 boots** (the earlier lone
  fault was 1 in 18 on the parent build).

Netboot `loader.disk` restored afterwards (`--scope project --variant nfsroot --skip-prepare`;
`nfs;/` marker confirmed present) — cutting an SD variant overwrites the TFTP loader.

**Netboot benches DO validate this image's code — checked, not assumed.**
`diff-sdimage-vs-export.sh` reported 7 of 336 ELFs differing, including quake3e, quakespasm
and vkquake, which would have made a netboot bench meaningless for them. `cmp -l` settles it:
**4 differing bytes** in quake3e and quakespasm, 24 in vkquake, 3 in python3 — identical sizes,
and the only visible change is the embedded build time (`13:14:56` in the export vs `13:56:47`
in the image tree). That is `__TIME__`, not different code.

### Upstream sync 2026-09-08 (5th sweep)

**Nothing behind upstream** — all 16 siblings 0 behind `origin/master`. All four game port
patches `--check` OK (no drift from the forks). Nothing merged, nothing to verify or push.

### Upstream sync 2026-09-07 (4th sweep)

- **1 commit behind, merged, no conflicts:** libphoenix `067ea81 arch: add STM32U3 target`.
  All 16 repos merged clean (`CONFLICTS (0)`).
- **It is a provable no-op for this target,** so the re-gate in progress stays valid: the
  commit touches only `arch/arm/v8m/reboot.c` (+2 lines) and the aarch64 build tree contains
  only `libphoenix/arch/aarch64` — that file is never compiled here. **Not pushed yet**: it
  lands in a core repo, so it goes out with the post-gate rebuild rather than on that argument
  alone.
- **Game-port patches `--check` OK for all four**; quake3e and quakespasm now **0 behind**.
  `yquake2` 10 and `vkquake` 30 remain **deferred with cause** (yquake2 overlaps our
  `gl3_draw.c` underwater fix; vkquake is bulk Ironwail imports — a re-port, not a merge).

### Upstream sync 2026-09-07 (3rd sweep)

- **All 16 siblings: 0 behind** `origin/master`. Nothing to merge.
- **Game-port patches: `--check` OK for all four** — the forks are still the single source of
  truth, no drift.
- **quake3e MERGED (1 commit)** — upstream `f694bbbc` "improved AVI pipe format validation"
  touches only `code/client/cl_avi.c`, outside our five-file delta, so the merge was textually
  clean. Pin bumped to the new upstream tip (+ archive size/sha256) so
  `game-port-patch.sh` keeps emitting **only our delta**: verified unchanged at 5 files,
  +231/-10 before and after. Fork side is a merge commit, not a rebase (those HEADs are
  published). `phoenix-rtos-ports b36c396`, fork `3cccffa4`. **Q3 RE-VERIFIED on HW** after the
  merge (`-13*-q3sweep`: 0 faults, q3dm1 loaded, `R_Init`, `Q3 1.32e` banner), so it is pushed:
  quake3e `23d284ff..3cccffa4`, ports `bcaeedf..b36c396`. Also pushed the nfs-fs audit fixes
  `0e3f337..be68a10` (booted clean on this build).
- **DEFERRED with cause, not forgotten:**
  - `yquake2` **10 behind** — upstream touches `gl3_draw.c`, `gl3_image.c`, `gl3_main.c`,
    `gl3_shaders.c`, and `gl3_draw.c:390` is exactly where our Phoenix-only underwater
    y-mirror fix lives. A real overlap on a file whose behaviour I hand-verified on pixels;
    it needs a deliberate merge + Q2 re-bench, not a drive-by during a sweep.
  - `vkquake` **28 behind** (was 22, still growing) — bulk Ironwail imports; the sync plan
    measured 8 of 9 delta files conflicting, 3 hard. Still a re-port, not a merge.
  - `quakespasm` **0 behind**.


---

<!-- archived: the 1-in-3 no-keyboard/mouse bug (xHCI error recovery) -->

## 4d. ★★★ 1-in-3 boots have NO KEYBOARD/MOUSE — root-caused to xHCI error recovery

**Matters for the standing goal:** the demo gate passes without input, but a *screen recording*
the owner drives needs a working keyboard and mouse. In 1 of 3 desktop trials there is none.

❌ **My earlier reading was wrong.** I called this "a start-ordering race, not a missing driver —
both devices were created earlier in the same boot". They were **not created at all**. Counting
`usb: New device` across the three trials: **T1/T3 = 4** (root hub + hub + mouse + keyboard),
**T2 = 1** (root hub only). And the X-side errno is **ENOENT**, not EBUSY — the device node never
existed, so this is neither an X race nor the pl011-tty console-bridge contention that
`fbdev.c:735-745`'s own comment blames. X's 1-second retry loop is irrelevant here.

**The actual failure (`-114653-acdesk-T2`):**
```
usb: New device: ... root hub
xhci: command completion code 36   -> Split Transaction Error
usb: Fail to get device descriptor
xhci: command completion code 19   -> Context State Error
usb: Fail to get device descriptor
xhci: command completion code 19   -> Context State Error
usb: Enumeration failed despite 3 attempts
```
A **transient** split-transaction error on the first descriptor fetch, then both retries
rejected with Context State Error — the endpoint was left halted and nothing ever cleared it.

**Root cause: the xHCI driver has no error-recovery path.** `RESET_ENDPOINT`,
`SET_TR_DEQUEUE_POINTER` and `DISABLE_SLOT` appear **nowhere** in
`sources/phoenix-rtos-devices/usb/xhci/xhci.c`, and `xhci_pipeDestroy` (`:3488`) only frees
software state — rings and the interrupt-pipe backpointer — without touching the hardware slot.
So the retry in `hub.c:305-318` recreates the pipe over a slot/endpoint the controller still
considers halted, which is exactly what Context State Error means. The first error is transient
and survivable; **the inability to recover from it is the bug.**

**FIX (rebuilding): `phoenix-rtos-devices`** — `xhci_pipeDestroy` now issues **Disable Slot**
when the DEFAULT CONTROL endpoint (DCI 1) is torn down, and drops the `addressed`/`hubFixedUp`
flags describing that slot so a later Enable Slot cannot inherit stale state.

Chose Disable Slot over Reset Endpoint + Set TR Dequeue deliberately: the failing path is an
**enumeration retry**, which already tears the pipe down and re-enumerates, so discarding the
whole slot context is both the simpler and the more complete recovery — endpoint-level reset is
what a *stalled bulk pipe* needs, which is a different case. Gated on DCI 1 so destroying a
device's interrupt pipe does not take its slot down with it; best effort, since the caller is
already unwinding.

**VERIFIED — 6/6 boots with full input** (`xhcifix`, `xhcifix2`, `xhcifix3`). Every trial:
`usb: New device` = **4** (root hub + hub + mouse + keyboard), keyboard **and** mouse reported
active by X, **0** `open failed`, **0** `Enumeration failed`, **0** faults.

Held to a rate, not one clean boot: the failure was ~1 in 3, so P(6 clean | unfixed) ~ 9%.
Not proof, but the strongest evidence a bench can give without a much longer run, and the
mechanism is understood rather than merely correlated.

**Re-gate for a fresh image (xHCI-fix build), in progress:** desktop ✅ **6/6** (carried over
from the input verification above — same build), SuperTuxKart ✅ **3/3**, Quake III ✅ **3/3**,
QuakeSpasm ✅ **3/3**, Quake II ✅ **3/3**, vkQuake ✅ **3/3** — **gate complete, 21 boots.** The current shipping image predates the xHCI
fix, so it stays the deliverable until this completes — but a demo image whose desktop is
actually **drivable** is the one worth recording with.

⚠️ One measurement artefact caught mid-verification: my first pass reported `mouse0=0` and
looked like a half-fix. The mouse *had* enumerated — the creation line reads
`usbmouse: New /dev/mouse device created`, not `.../dev/mouse0`, so the grep pattern was wrong.
Checked before reporting; the same class of error as the ANSI-anchored grep earlier.


---

<!-- archived: automation restored (2026-09-06) -->

## 4b. AUTOMATION RESTORED (2026-09-06, owner asked)

- **Autonomous heartbeat cron re-created** — there was **none** running (the previous one
  had hit its 7-day expiry), which is why nothing progressed between sessions. Now every
  20 min at :07/:27/:47, carrying the standing goal above. Expires again in 7 days; the
  prompt tells me to re-create it before then.
- **Upstream-sync cron created** — every 6 h at :23: `upstream-status.sh` →
  `git-pull-upstream-all.sh` → game-fork sync + `game-port-patch.sh --check` → rebuild
  `--scope core` and boot-test **before** pushing anything that merged into a core repo →
  push to `publish`, never force. One line per sync lands here.
- **Upstream sync 2026-09-07 (2nd sweep): NOTHING BEHIND** — 0 new commits across 0 repos
  for all 17 siblings. All four game-port patches `--check` clean (fork is still the single
  source of truth, no drift).
  ⚠️ **Game FORKS are behind their own upstreams and I deliberately did NOT merge them:**
  quake3e 1, quakespasm 0, yquake2 10, **vkquake 22**. Per
  `docs/misc/2026-09-03-game-fork-upstream-sync-plan.md` the vkQuake sync is *"a re-port,
  not a merge"* — hard conflicts in `sys_sdl.c`, where our NFS-slurp + libphoenix
  single-stream fix overlaps upstream's handle-table rewrite, plus an SDL3-by-default risk.
  Merging any of them now would invalidate the freshly cut, gated, 3/3-benched SD image and
  need a full re-verification pass. **Judgement: hold until the owner has flashed and
  validated that image**, then sync in the plan's recommended order. Flagging rather than
  silently skipping.
- **Upstream sync 2026-09-07 (1st sweep) — DONE and HW-verified.** 13 commits merged: `devices` 1
  (stm32l4 PWM, irrelevant to us) and `project` 12 (CI + submodule bumps). All 10 conflicts
  were **submodule gitlinks**, resolved to upstream's pointers — this tree builds from the
  `sources/` siblings and `bootstrap-linux-host.sh:425` records that project's submodules
  are deliberately left uninitialised, so they are inert metadata. **One real
  incompatibility caught:** upstream's new top-level `build.project` drives unit tests
  through `b_build_test_target()` and renamed the hook in all of *its* target files; our
  `_targets/aarch64a72` still defined the old `b_test_target`, so `--with-tests` would have
  called a function nobody defines — renamed (`project f257a5f`). Our own
  `_projects/aarch64a72-generic-rpi4b` files were untouched by the merge. Game-fork patches
  re-checked: all four OK. Verified with `--scope core` + a Pi cycle: psh, lwip, IP,
  `startx_gpu deskapps` all five clients rendering, **0 faults**. Pushed.
- **Everything is pushed:** coord `main` → `publish` (`a843c807..f9b1e7a9`), and all 14
  siblings were already level with `publish` (0 ahead).


---

## Second trim, 2026-09-08 — WEEK-2026-W37 351 → 149 lines

The weekly log had grown to 351 lines across 13 sections. Everything below is the
"how we got there" record moved out of it: measurement tables, refuted hypotheses,
retracted figures and per-cycle detail. Sub-headings name the source section.
Live pointers that survived the trim: the analysis for §2b/§2b-bis/§2c/§2e detail is
`docs/misc/2026-09-08-glamor-screen-mirror-and-gl-window-rate.md`; §2d's is
`manifests/2026-09-08-psh-cmdsz-hevc-window.md`.

### from §1b — the 5 doc remarks, remark by remark

| # | owner remark (2026-09-08) | status |
|---|---|---|
| 1 | no mention of HW video decode | ✅ added (`14480b662`) |
| 2 | no mention of the ML CPU-vs-GPU work | ✅ added — framed **parity-first** (end-to-end GPU MLP is 0.90×; the 4.4×/11× microbench ratios rested on an inflated CPU baseline and are recorded as superseded) |
| 3 | no mention of AXI-PMU | ✅ added |
| 4 | study the old archive docs for other uncredited work | ✅ **done** — 35 gaps found, **33 integrated** (13 ★★ + 16 ★ + 4 marginal); doc 885 → **1427 lines**. Two were **corrections to claims the doc made**: the caveats section opened with the X desktop-exit crash as *open* when it is fixed (6/6 crashes before → 0/18 after, and the root cause is an *upstream glamor* bug), and the ffmpeg entry read as if nothing used it. Scope corrected for a 15th patched tree (`external/mesa`). Several sweep claims were **dropped or corrected on verification** — a cited file that no longer exists, a fontconfig figure that disagreed with its own commit, and an inference presented as a measurement. |
| 5 | point to it from `README.md` | ✅ first entry under Documentation (`14480b662`) |

All three new areas live in one new final section, **"Hardware experiments outside the Phoenix
repos"** — they are programs *built on* Phoenix in `tools/`, not changes to it (verified: `git log
--all --grep` for hevc/argon/rpivid/axi across kernel, libphoenix and devices returns nothing), so
the per-repository structure had no place for them. Each carries an explicit limits block.

### from §2 — showcase-reel v5 per-point status table

Every row was closed except nano/mc, which is why the table came out of the log.

| owner point | status |
|---|---|
| X11 segment totally static | ✅ `startx_gpu action` — WM + GL window + Python GoL + xbill + xclock + `top` |
| STK frozen frame | ✅ was my error (pre-clock-fix footage), re-recorded |
| on-screen FPS figures | ✅ all four Quakes + STK |
| Quake III no demo | ✅ bot deathmatch + orbit camera (its `.dm3` demos can never load) |
| boot + shell + Python | ✅ captured |
| HW video playback | ✅ **windowed over the terminal** |
| Dillo | ✅ kept |
| nano/mc in the shell segment | ⏳ **not done** — `mc` renders blank on fbcon, and `nano` is interactive so psh automation cannot drive or exit it |

Numbers behind the two reel caveats that stayed in the log as one-liners: `life.py`'s canvas is
0.09 % changed between t=160 and t=235 while the GL window is 49.9 %, `top` 6.7 %, `xbill` 5.1 %,
xclock 1.3 %; its last good frame reads `gen 536 … 12.2 gen/s`. For the console, a full 239×66
redraw is ~16 KB, `pl011-tty` mirrors every byte to the 115200 UART (~11.5 KB/s), and
16/11.5 ≈ 1.4 s matches the observed 1.0 gen/s. That last one is a hypothesis consistent with the
numbers; the test that settles it is one run with the UART console detached.

### from §2b — the refuted `glamor_spans.c` hypothesis

**First suspect tried and REFUTED — by its own built-in diagnostic.** The fork's screen-pixmap
Y-flip was hand-rolled into `glamor_transfer.c` only, and `glamor_spans.c:234`/`:345` index the FBO
row straight from the X row. That patch shipped with a one-shot `ErrorF` per direction so it could
fail loudly, and it did: **0 hits** in a cycle whose daemon binary provably contained both markers.
Neither span path runs on the screen pixmap, so the flip could not have moved a pixel. **Reverted.**
(The diagnostic channel was validated *before* trusting its silence.) The clip mirror is painted
once at Window Maker startup and never repaired; the xterm one is transient (GoL bright-pixel share
26.0 % at t=112 vs 4–8 % either side). Kept in the log: the measurement, the screen-pixmap
conclusion and the `glamor_copy.c` suspect.

### from §2b-bis — the Mesa-relink hazard, cycle detail

| daemon | Mesa in the binary | result |
|---|---|---|
| the one that has been shipping (built 02:12) | `git-e4be116324` | works, 0 faults |
| relinked from the current tree (21:38) | `git-aa916f2f06` | **SIGILL — X server dies** |

`aa916f2f060` is **our own** `u_vbuf: do not silently drop draws on the index-unrolling path` — the
fix for bug #3 (Quake III glitches). The *games* link Mesa in-process and have it; the X server was
simply never relinked after it landed, so glamor has been running on the previous Mesa all along.
Failure shape: two `v3d-winsys` MMU violations → `signal 4` → `server exited (status=0x300)`.
Restore path: `build-xfbdev.sh` writes `Xphoenix-glamor` and staging renames it, so the known-good
binary was never overwritten; put back into both `_fs` and the live export (sha `8e003ab45ef41009…`,
`git-e4be116324`) and confirmed on hardware — 0 faults, six clients up, desktop identical.

### from §2c — the superseded reel v4, and the 1 fps measurement detail

Reel v4, kept here only as the record it superseded:
`artifacts/hdmi-video/20260908-162023-phoenix-rtos-rpi4-showcase.mp4` (140 s, 7 segments):
QuakeSpasm `demo1` · Quake II `q2demo1` · vkQuake `demo2` · Quake III bot deathmatch ·
SuperTuxKart AI race · Dillo web browser · X desktop. Four segments are real gameplay motion;
browser and desktop are static by nature. Superseded by v5 (245 s, 11 segments).

The 1 fps figure's workings: 120 consecutive captured frames (4.00 s at 30 fps), GL-window crop,
**4 changed pairs → ~1.0 update/s**, one every ~0.73 s. Not a grabber limit — the same frames show
`top` and `xbill` changing far more often, and the same card recorded 35 / 73 fps game counters
that evening. From the same frame's `top`: `/sbin/rpi4-v3d` **76.9 % CPU**, CPU0 **100 %**,
`Xphoenix-glamor-daemon` 17.9 %.

### from §2d — psh CMDSZ: how it was found, and the `autoexec.cfg` trap

Cost two Pi cycles on a Quake III "bug". `pshapp` had `#define CMDSZ 128` and dropped every
character past it with **no bell, no message and no refusal**, then executed the prefix. The UART
echo of the line looks complete, so the run reads as evidence about the *program*. A 167-char
launch line lost `+devmap q3dm1`, and Quake III sat in its menu. `CMDSZ` backs one malloc plus a
same-size static clipboard, and the NOMMU/MCU targets shouldn't pay ~2 KiB for a Pi 4 problem —
hence 1024 on 64-bit MMU targets only (overridable with `-DCMDSZ`). The harness warns against the
old limit too (`0c89ccf88`), since an older psh binary can still be on the target. Verified
functionally, not by "the build succeeded": a **156-character** command was sent and its **tail
token survived** (`… PHX_TAIL_OK` in the program's *output*, not just the echo). `CMDSZ` is a
compile-time constant, so `strings` could not have told us this.

**The rebuild caught a self-inflicted trap:** it re-ran `stage_q1_video_cfg` and replaced
`id1/autoexec.cfg` with the canonical `vid_*` block, **silently dropping the `scr_conscale` line
every FPS capture depends on**. Hand-staging into a tree a build script owns regresses on the next
cut. Both configs now ship **from `scripts/stage-game-data.sh`** (`89bd713fb`) — the q1 file
carries the fps readout alongside `vid_*`, and a new `stage_q3_showcase_cfg` ships the Q3 bots +
orbit camera — so an image cut keeps them.

### from §2e — video playback, the parts that are workings not answers

The reel's clip is **30 s of our own vkQuake gameplay put through `transcode-for-phoenix.sh`** — so
it also exercises the transcoder on a real-world source, the same path an iPhone clip will take.
Getting there found **one open defect and one trap**: `hevc-play` stops after ~10 frames on richer
reference structures (`collocated POC 0 not in DPB`; with TMVP off it stops one frame later with
`a reference POC not in DPB`, which localises it to **RPS/DPB retention**, not TMVP). Playback
clips are therefore encoded **IPPP** — a *player* workaround, **not** a codec-subset restriction:
the hardware decodes B slices, b-pyramid, multi-reference and TMVP bit-exact, and the conformance
vectors keep all of it. (`docs/misc/2026-09-08-glamor-screen-mirror-and-gl-window-rate.md` §6.)

Windowing implementation detail: `hevc-play --window 960x540+480+270 <clip>` scales the decode into
that rectangle with a 2 px white border and writes **only inside it**. `WxH` alone centres it;
omitting `--window` keeps the old full-screen blit; a geometry running off the edge is clamped, not
refused. Nearest-neighbour with a 16.16 fixed-point step (one multiply-shift per pixel) — a CPU
loop per frame; the point is to show the decode is ours, not to resample well. On hardware: the
clip decodes on the rpivid block into a bordered window while the live boot log and the psh prompt
stay readable all around it, with the `hevc-play --window …` command line and its own
`presented N/750 frames` progress visible on screen beside the video. **300 frames presented,
continuous motion, 0 faults.**

Why the transcoder does **not** stream-copy the iPhone's own HEVC even though that is the obvious
move: our decoder covers the tools x265 enables by default and rejects or mismatches on the rest
(tiles, non-zero deblock offsets, AMP, emulation-prevention bytes in headers), Apple may use any of
them, and there is no way to tell from outside — so it re-encodes through the exact parameter set
the conformance harness exercises. Rotation (phones store landscape + a matrix), frame rate
(240 fps slow-mo is decimated — `hevc-play` has no clock) and chroma/bit-depth are handled;
**HDR is not**.

### from §4 — WHAT GOT DONE THIS WEEK, the narration

**★★ STK was capped at exactly 1 fps by a broken C++ clock — fixed, now 5.84 fps (5.8×).**
libstdc++ for aarch64-phoenix is built with **none** of its time backends, so
`std::chrono::steady_clock` falls back to `std::time()` and ticks in whole **seconds**; STK's frame
loop sat in `while (dt == 0) { StkTime::sleep(1); … }` waiting for it. Patch reads
`CLOCK_MONOTONIC` (`ports 6f08c26`). STK is now GPU-bound (~171 ms/frame, ~88% CL submits).
Found by *precision, not magnitude* — 1000.0 ms/frame within 0.08% across two tracks and settings
is a clock, not a workload. Detail: `docs/misc/2026-09-08-stk-frame-budget.md`.

**Also fixed / added:** trusted root CAs (121, real HTTPS) · libphoenix stdio partial-write +
regression test · V3D per-session BO leak (15.7 MB → 2.3 MB; leak proven **bounded**: per-session
cost decays +60 MB → +2.2 → +1.5 over three X lifecycles) · `stk-launcher` now honours caller
options · vkQuake demo playback via `id1/phoenix-demo.cfg` (`ports d7de9aa`) · `startx_gpu browse`
= Window Maker + Dillo · ext2 volume sized from content · `scripts/make-demo-reel.sh`,
`qemu-boot-sdimage.sh`, `record-hdmi.sh`, `tools/v3dmemprobe`.

**Measurement traps found the hard way:** `fault_pattern_matches: 0` is vacuously true when the
program never ran · an ffmpeg `blackframe` check passes on a text console · mpdecimate measures
scene change, not frame rate · an in-boot A/B needs the arms **swapped** · RAM-staging STK's assets
is a measured **loss** (73 s of copying to save 2.7 s).

**QEMU ceiling:** plo-only. The kernel never starts — plo needs a firmware DTB in x0 and QEMU
supplies none, so it faults in early hal init. `-dtb` does not help.
`docs/misc/2026-09-08-qemu-boot-ceiling.md`.

### from §4e — STK time-to-race

Best-looking: keep the deferred pipeline, accept a one-time ~50 s before the first lap after a
boot (cut in editing; steady-state fps is the same or better). Quick take: add
`--disable-dynamic-lights --shadows=0` → same lap in 22 s on any boot. The ~27 s first-run cost is
**not** shader compilation, asset reads, or steady-state rendering — all measured and excluded;
leading hypothesis is the kernel's contiguous allocator. RAM-staging STK's assets is a measured
**loss** (73 s of copying to save 2.7 s; keep using `ram-stage-play` for Quake).
Full workings: `docs/misc/2026-09-08-stk-time-to-race.md`.

### from §4f — upstream sync, 7th and 8th sweeps

**8th sweep 2026-09-08 — 1 commit in, verified, pushed.** plo `953bba7` (upstream: include
`board_config.h` in stm32 `peripherals.h`; touches only `hal/armv7m/stm32/*` + `hal/armv8m/stm32/*`,
no aarch64). Merged clean, **0 conflicts** across all 16 siblings. plo is a core repo so the rule
was followed regardless of it looking STM32-only: `--scope core` rebuild (plo.elf confirmed
rebuilt by mtime) → Pi boot test **0 faults**, psh + lwip up, `test-libc-exit` **31 Tests 0
Failures OK** → pushed. All four game-port patches `--check` **OK**. Now **0 behind everywhere**.
7th sweep: libphoenix `7218adc` (`execve("")`→ENOENT), same treatment.

### from §2 — harness note kept out of the owner-facing log

⚠️ Cutting a `--variant sd` image replaces the TFTP `loader.disk`, so netboot refuses until restored
with `./scripts/rebuild-rpi4b-fast.sh --scope project --variant nfsroot --skip-prepare`. It also
relinks the games, which expires their HW gate — budget 6 cycles after any image cut.
