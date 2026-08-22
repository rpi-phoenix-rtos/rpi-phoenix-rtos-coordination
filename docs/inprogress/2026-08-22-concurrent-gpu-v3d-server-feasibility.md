# Concurrent GPU access on the Phoenix-RTOS RPi4 V3D 4.2 port — feasibility + staged plan

**Date:** 2026-08-22. **Owner item:** #13 ("v3d-server time-slicer"). **Type:** analysis/design only — no code changed, no Pi cycle run. **Goal:** lift the "single-GPU-process" constraint so the glamor-accelerated `Xphoenix` (GPU owner) and a second GL/Vulkan client (e.g. GLQuake, vkQuake) can share the V3D 4.2 without the current EL1 abort.

Source basis: `tools/v3d-driver-port/v3d_phoenix_winsys.c` (the in-process Mesa/gallium winsys backend, ~1650 lines), `v3d_phoenix_power.c`, `libvcmbox.c`, and the Phoenix device-server pattern in `sources/phoenix-rtos-devices/misc/rpi4-vcmbox/rpi4-vcmbox.c`.

---

## 1. Root cause of the single-process abort

### 1.1 Architecture as built (all state is per-process)

There is **no GPU server today**. `drmIoctl()` is an inline in the vendored `xf86drm.h` that forwards directly to `phoenix_v3d_ioctl()` (`v3d_phoenix_winsys.c:1601`, confirmed by the shim header comment in `v3d_libdrm_shim.c:4`). Mesa's v3d gallium driver, the winsys, and all GPU state are therefore **linked into each GPU-using process** (`libv3d-phoenix.a`). `Xphoenix-glamor` and a GLQuake binary are two separate processes, each with its own copy of everything.

All winsys state is a single process-local static struct `W` (`v3d_phoenix_winsys.c:209-237`): the mapped HUB/CORE0 registers, the MMU flat page table `W.pt` + its PA `W.pt_pa`, the GPU-VA bump allocator `W.next_gpuva`, the BO table `W.bos[]`, the VA free-list `W.holes[]`, and the binner-overflow pool. Nothing is shared; there is no cross-process lock (the file includes `sys/threads.h` but uses no mutex on the submit path).

### 1.2 What a second process does — several independent, mutually-sufficient conflicts

On its first MMIO-touching ioctl, every process runs `winsys_init()` (`:257`, guarded only by the **per-process** `W.inited`), which conflicts with a live first owner in at least three distinct ways. **Each one alone is sufficient to break concurrency** — they are not a single bug:

1. **Independent, destructive power-on/reset.** `winsys_init()` unconditionally calls `v3d_phoenix_powerOn()` (`:263` → `v3d_phoenix_power.c:187`): mailbox `SET_QPU_ENABLE` + `SET_DOMAIN_STATE` + a **V3D clock toggle** (`SET_CLOCK_STATE` on/off/on) around a **`PM_V3DRSTN` reset deassert** (`power.c:216-218`) and an **ASB async-AXI bridge re-enable** (`power.c:221-222`). Running this while the first process has live GPU state clock-glitches and re-initialises the block under it. This is the same "concurrent clock-toggle/reset races our submit → core0 reads 0xdeadbeef" hazard the code's own comment warns about (`winsys_init` comment `:260-262`), now caused by a second *client* instead of the old scout process.

2. **The single global `MMU_PT_PA_BASE` register is stolen.** `apply_core_regs()` writes `W.hub[MMU_PT_PA_BASE/4] = W.pt_pa>>PAGE_SHIFT` (`:794`). `MMU_PT_PA_BASE` (HUB reg `0x1204`) is **one physical register** — the V3D MMU can point at exactly one flat page table at a time. When process 2 runs `apply_core_regs()`, the GPU's page table is repointed at process 2's `W.pt`, in which **none of process 1's BOs are mapped**. Process 1's next (or in-flight) GPU job then walks process 2's PT, every BO VA resolves to an invalid/unmapped PTE → MMU illegal-address fault.

3. **Overlapping GPU-VA allocators.** Both processes start `W.next_gpuva = GPUVA_BASE` (0x100000, `:301`) and bump-allocate identically (`va_alloc`, `:462`). Even with a *shared* PT they would hand out the same GPU VAs to different physical BOs and clobber each other's PTEs (the winsys already has a VA-collision detector for the intra-process case, `:527-533`).

### 1.3 Where the EL1 abort comes from (hypothesis, not observed fact)

We have **no 2-process runtime log** — source analysis cannot pin the faulting PC, and this must be stated honestly. The most likely mechanism, consistent with the tree's evidence:

- The **GPU-side** fault is an MMU illegal-address abort (`mmu_ill`), exactly the `mmu_ill=0x8000886x` signature already seen intermittently on the heavy q3dm7 map (MASTER-RECONCILED-PLAN O2). Under concurrency it becomes *deterministic* because conflict #2 unmaps a whole address space rather than corrupting one PTE.
- The **CPU-side EL1/SError** most likely follows the GPU AXI master taking an external abort against an unmapped/rescinded PA after PT-base theft or the mid-flight reset — matching this project's standing finding that "SError on RPi4 = an external-abort from the PCIe/USB/GPU bring-up path" (memory: *Pi4 SError = PCIe/USB abort*; *serror_pcie_source*). `apply_core_regs()` also arms `MMU_CTL_PTI_ABORT` + `MMU_ILLEGAL_ADDR` scratch redirect (`:799-805`), so whether a given fault aborts, interrupts, or redirects depends on timing.

**M0 must turn this hypothesis into an observation** (see §5) by logging which event coincides with the fault.

### 1.4 Is it "easy to lift"? — No, and this is the key finding

A tempting quick fix is "guard the double power-on." **That is insufficient.** Even if only the first process ever powers on and resets, conflicts #2 (single global `MMU_PT_PA_BASE`) and #3 (overlapping VA allocators) remain: two independent flat page tables cannot both be active, and two allocators starting at the same base collide. There is no cheap in-place patch — **exactly one entity must own power, the MMU page table, and the VA space.** That is the design conclusion driving the options below.

---

## 2. Phoenix IPC / process model available to build a server

Phoenix gives us a small, sufficient toolkit — proven in-tree by the existing RPi4 device servers:

- **Message-port server pattern** (`rpi4-vcmbox.c:337-369`): `portCreate(&port)` → `create_dev(&dev, "name")` (registers a node in `devfs`) → a loop of `msgRecv(port, &msg, &rid)` / `msgRespond(port, &msg, rid)`. **A Phoenix server handles one message at a time**, which gives *serialization for free* — this is precisely how `rpi4-vcmbox` de-races the single VideoCore mailbox FIFO across thermal/genet/usb/sdio/v3d clients (`rpi4-vcmbox.c:6-17`). The same property is what a GPU server needs.
- **Message payload** (`sources/phoenix-rtos-kernel/include/msg.h:72-145`): each `msg_t` carries a fixed inline `i.raw[64]`/`o.raw[64]` (ideal for a small control/submit descriptor) **plus** `i.data`/`i.size` and `o.data`/`o.size` pointer+length buffers that the kernel copies across the address-space boundary (for larger payloads). `mtDevCtl` is the control verb (see `libvcmbox.c:111-155`, `vcmbox_call` packs a request into `msg.i.raw` and reads `msg.o.raw`).
- **Client-side resolution** (`libvcmbox.c:87-108`): `lookup("/dev/<name>", …)` with a bounded retry budget + a pre-`bind devfs` fallback via the `devfs` named port. A GPU client library would mirror this exactly.
- **Cross-process memory sharing = MAP_PHYSMEM at a resolved PA.** This is the linchpin. Phoenix has **no anonymous shared memory**: `MAP_SHARED` is defined as `0x0` (a no-op bit; `mman.h:28`) and there is no POSIX `shm_open`. The *actual* sharing mechanism used throughout the RPi4 drivers is: allocate physically-contiguous DRAM with `mmap(MAP_CONTIGUOUS|MAP_ANONYMOUS)`, resolve its physical address with `va2pa()`, and hand that PA to another agent which maps the **same physical page** with `mmap(MAP_PHYSMEM, …, pa)`. The VideoCore mailbox bounce buffer (`rpi4-vcmbox.c:309-331`), the MMIO windows, and the winsys scanout-readback (`v3d_phoenix_winsys.c:419-424`) all rely on this. A BO shared between GPU client and server travels as *a PA + size in `msg.i.raw`*, never as copied bytes.
  - Caveat: the winsys scanout-readback comment claims "MAP_SHARED is required" (`:419`). Given `mman.h`, that flag bit is a no-op; the effect it depends on is `MAP_PHYSMEM` at the framebuffer PA. Do not lean on the comment — lean on the PA+MAP_PHYSMEM pattern, which the whole driver set proves.
- **No cross-process mutex primitive.** `sys/threads.h` mutexes are intra-process. Any lock *between* processes must be built from a server (message serialization) or a hand-rolled spinlock in a shared MAP_PHYSMEM page. This asymmetry is decisive below.

---

## 3. How Linux/Mesa solve this, and why we can't copy it

The Linux `v3d` DRM kernel driver arbitrates multiple GL/CL contexts: one MMU page table **per address space** (per `drm_file`), per-fd GEM BO handles, and a DRM GPU scheduler (`v3d_sched.c`) that queues BIN/RENDER/TFU/CSD jobs with dma-fences, servicing `OUTOMEM` overflow (`v3d_overflow_mem_work`) and doing bridge resets (`v3d_reset`). Multiple processes never touch V3D registers directly — the kernel is the sole owner and multiplexes.

We have **no kernel DRM layer** and are not writing one. The winsys is a userspace shim that *impersonates* the DRM ioctl surface. So the arbiter has to live in userspace. That leaves the two options in §4. Note Linux's per-address-space PT is a luxury we can't cheaply match (our MMU has a single global `MMU_PT_PA_BASE` and no context-switch of it mid-stream), which is exactly why a **shared single address space owned by one server** (Option A) is the natural fit.

---

## 4. Options analysis

### Option A — `v3d-server` daemon (RECOMMENDED)

One privileged daemon owns the GPU exclusively: it runs power-on once, maps HUB/CORE0, owns the single MMU page table + VA allocator + BO table + binner-overflow pool, and is the *only* code that writes V3D registers. Clients link a thin `libv3d-client` that exposes the same `phoenix_v3d_ioctl()` entry point Mesa already calls, but routes the MMIO-touching ioctls over `mtDevCtl` messages to the daemon.

**Natural RPC boundary already exists.** `phoenix_v3d_ioctl()` (`:1601`) partitions cleanly:

| ioctl | Handling under Option A |
|---|---|
| `GET_PARAM`, `WAIT_BO` | **stay client-local** — constants, no MMIO (already served before `winsys_init`, `:1610-1618`) |
| `CREATE_BO` | forward: server does `va_alloc` + PT map, allocates/returns the BO **PA + assigned GPU VA**; client maps the PA `MAP_PHYSMEM` for CPU access |
| `GET_BO_OFFSET`, `MMAP_BO`, `GEM_CLOSE` | forward: server owns the BO table + VA free-list |
| `SUBMIT_CL`, `SUBMIT_TFU`, `SUBMIT_CSD` | forward: client sends the tiny submit descriptor (`drm_v3d_submit_*`, references BOs by handle/VA); server executes synchronously and responds on completion |

**Efficiency — the data never gets copied.** Mesa builds the command list *inside a BO*; the BO is shared physical DRAM (client and server both `MAP_PHYSMEM` it). Only the fixed-size `drm_v3d_submit_cl` descriptor (BO handles, CL start/end VAs) crosses IPC in `msg.i.raw`. This is the key reason A's per-submit overhead is small — one message round-trip per submit, no CL/vertex/texture bytes on the wire.

**Serialization is free + correct.** One-message-at-a-time `msgRecv` loop = no two submits ever race the registers. Power-on/reset are centralized in the daemon, which *removes conflicts #1–#3 by construction*: one power-on, one PT, one VA space.

**Trade-offs:**
- (+) Uses only primitives already proven in-tree (message ports, `create_dev`, `MAP_PHYSMEM` BO sharing). No new kernel facility.
- (+) Correct by construction; the destructive-reset and PT-base-theft classes cannot occur.
- (+) Covers Vulkan/V3DV too — V3DV rides the same `phoenix_v3d_ioctl` surface (`SUBMIT_CSD`/`SUBMIT_CL`), so the daemon serves it unchanged.
- (−) One message round-trip per submit (latency, not bandwidth). Acceptable: submit is already synchronous spin-wait (`WAIT_BO`=0, `:1617`), so a round-trip per frame-ish batch is cheap relative to the multi-ms GPU job.
- (−) BO-PA lifetime/security: the server must validate client-supplied PAs/VAs (a client could ask it to map arbitrary PA). Bound to client-registered BOs.
- (−) Global reset hazard (see §4.3).

### Option B — shared-state multi-process winsys (NOT recommended)

Keep the winsys in-process but make `W` shared: put the MMU PT, VA allocator, and BO table in a fixed `MAP_PHYSMEM` page every process maps, and guard the register/submit path with a cross-process lock.

**Why it loses:**
- Phoenix has **no anonymous shared memory and no cross-process mutex** (§2). B must hand-roll *both*: a shared-state page at a hard-coded PA (fragile allocation/lifetime) *and* a spinlock in that page (no OS support, no priority-inheritance, a crashed lock-holder wedges everyone).
- It still cannot safely serialize the **destructive power-on/reset** — two processes independently deciding to reset the block is the worst conflict, and a userspace spinlock around a clock-glitch is brittle.
- Every process still maps and writes the raw V3D registers → far larger attack/corruption surface; a buggy client corrupts the GPU for all.
- B's *only* advantage over A is avoiding the per-submit IPC round-trip — and A already recovers most of that by sharing BOs via `MAP_PHYSMEM` (only the descriptor is copied).

**Verdict: Option A.** B trades a correctness-by-construction design for a marginal latency win it can't even safely bank.

### 4.3 One real risk to design for (both options, acute in A)

`reset_reinit_core()` (`:857`) resets the whole V3D to recover a wedged job (the still-open intermittent q3dm7 binner wedge, MASTER-RECONCILED-PLAN O2). In a multi-client server this reset is **global** — one client's wedge resets the GPU under *all* clients. The daemon needs a client-recovery story: on a wedge, reset, mark all in-flight jobs failed, and have clients resubmit (the submit is already synchronous, so "resubmit on error" is a small extension). Flip side / benefit: centralizing power-on + reset in the daemon *eliminates* the concurrent-reset conflict class that makes today's 2-process case abort.

---

## 5. Recommended architecture + staged plan

**Architecture:** a `v3d-server` daemon (place under `sources/phoenix-rtos-devices/gpu/rpi4-v3d/` per the P8 tools→devices migration, or prototype in `tools/v3d-driver-port/`) that owns power/regs/PT/VA/BOs and serializes submits over `mtDevCtl`; a `libv3d-client` that re-implements `phoenix_v3d_ioctl()` as an RPC stub. Scope target = **job-granularity serialization** ("both processes run, no abort"), which is the owner's actual ask. True preemptive fair time-slicing is a *later* increment (it needs async submit + real fences/CT-done IRQs the current spin-wait winsys lacks — see M-future).

Each milestone is independently testable on HW.

### M0 — Reproduce + *discriminate* the 2-process abort (no new server code)
- **Cheapest first step (feasibility linchpin):** a 5-line 2-process `MAP_PHYSMEM` coherency test. Proc A `mmap(MAP_CONTIGUOUS)` a page, write a pattern, print `va2pa`. Proc B `mmap(MAP_PHYSMEM, pa, MAP_UNCACHED)` and read it back. Must match — this validates the entire BO-sharing premise of Option A before any daemon is built. (Expected to pass per the scanout-readback precedent.)
- **Discriminator, not just "make it crash":** instrument process 1 to log `MMU_PT_PA_BASE` at each submit; launch a second trivial GL client; capture over UART which event coincides with the fault — (a) process 2's power-on/reset, (b) PT-base theft (base value changes under process 1), or (c) process 1's next submit after theft. Also capture the abort PC + SError ESR + both `v3d-coldstate:` lines.
- **Files:** temporary logging in `v3d_phoenix_winsys.c` (`apply_core_regs`, `ioc_submit_cl` prologue); a tiny 2nd GL client (reuse `gl_frontend_smoke.c` / `harness_screen_create.c`).
- **Risk:** low. **Validate:** UART log shows the fault event; confirms §1.3 hypothesis and tells us the minimum the daemon must arbitrate. Multi-trial (the wedge is intermittent) — use `test-cycle-bench.sh`.

### M1 — Minimal daemon owning the GPU + ONE client draws a triangle
- Build `v3d-server`: `portCreate`+`create_dev("v3d")`, power-on once, map regs, own PT/VA/BOs; `msgRecv` loop dispatching CREATE_BO / GET_BO_OFFSET / MMAP_BO / GEM_CLOSE / SUBMIT_CL/TFU/CSD (lift the existing `ioc_*` bodies verbatim — they already are the server logic). Build `libv3d-client`: reimplement `phoenix_v3d_ioctl` as an RPC stub; CREATE_BO returns PA+VA, client `MAP_PHYSMEM`s the PA.
- **Files:** new `rpi4-v3d.c` (server main + msg loop, patterned on `rpi4-vcmbox.c`); new `libv3d-client.c` (RPC, patterned on `libvcmbox.c`); the existing winsys `ioc_*`/`W`/`apply_core_regs`/power move into the server largely unchanged; GET_PARAM/WAIT_BO stay in the client stub.
- **Risk:** medium — the BO-PA hand-off + submit descriptor marshaling are new; MMAP_BO semantics (client CPU mapping of a server BO) must be coherent (both uncached, `MAP_PHYSMEM`).
- **Validate:** one GL client renders a triangle/clear through the daemon to /dev/fb0, HDMI grab, 0 faults — matches today's single-process capability but *via the server*.

### M2 — Two clients, serialized
- Launch two independent GL clients against the one daemon; both submit; the msg loop serializes. Confirm no PT-base theft (only the daemon writes `MMU_PT_PA_BASE`), no VA collision (one allocator), no double power-on.
- **Files:** none new — exercises M1. Possibly a fairness/queue-depth counter in the server.
- **Risk:** medium — surfaces the global-reset hazard (§4.3): add "mark in-flight failed + client resubmit" on a wedge.
- **Validate:** two `gl_det_harness`-style clients each render deterministically; `test-cycle-bench.sh` multi-trial, 0 aborts. **This is the milestone that proves item #13's core claim** (the abort is gone).

### M3 — glamor-X + GLQuake concurrent
- Run `Xphoenix-glamor` (GPU client of the daemon) and launch GLQuake as a second daemon client. Both submit GPU work without aborting.
- **Scope honestly:** M3 = "both submit GPU work concurrently, no abort." It is **not** "GLQuake composited into an X window" — display composition + who owns /dev/fb0 / page-flip (`v3d_phoenix_fb_flip`, scanout ownership) is a separate windowing problem, out of scope for the GPU-sharing server. Likely GLQuake renders to its own surface / fbdev region while X owns the desktop; sorting out shared scanout is a follow-on.
- **Files:** `Xphoenix-glamor` and the game link `libv3d-client` instead of the in-process winsys (build-system wiring: `build-xfbdev.sh --glamor`, the quake GPU-libs).
- **Risk:** high — two heavy real workloads; scanout/present arbitration; the q3dm7 wedge now global.
- **Validate:** startx desktop + GLQuake running together, HDMI grab, sustained frames, 0 EL1 aborts (`uart-summary.sh`).

### M-future — fair preemptive time-slicing (later increment, not required for #13's ask)
Async submit + real fences (CT0/CT1 done interrupts instead of the current spin-wait) so long jobs from one client don't starve another. Needs a V3D IRQ path the winsys deliberately avoids today (`HUB_INT_STS` is polled raw, `:106-107`). Explicitly deferred.

---

## 6. Effort / risk / blocking

- **Effort:** M0 ~1 focused session (instrumentation + 2-3 multi-trial Pi cycles). M1 the bulk — ~1 week-equiv: refactor winsys into server + write client RPC + BO-PA hand-off (the `ioc_*` logic is reused, so this is plumbing not new GPU logic). M2 small once M1 lands. M3 large (two real workloads + present arbitration).
- **Risk hotspots:** BO-PA coherency across processes (retire in M0's linchpin test); submit-descriptor marshaling; the *global* reset/recovery story (§4.3); M3 scanout ownership.
- **Blocked / owner-attended?** None structurally blocked — all primitives exist in-tree. M0/M2/M3 need the exclusive one-cycle-at-a-time Pi (serialize cycles). The residual **intermittent q3dm7 GPU wedge** (O2) is orthogonal but will manifest globally under M2/M3 — track separately, don't let it gate the server. No owner decision required to start; M0+M1 are autonomously executable.

---

## 7. Cheap first HW experiment (M0 — describe, do NOT run yet)

Two parts, one Pi cycle each, both autonomously runnable:

**(a) BO-sharing coherency probe (the linchpin).** Build a 2-process test: process A `mmap(_PAGE_SIZE, MAP_CONTIGUOUS|MAP_ANONYMOUS|MAP_UNCACHED)`, writes a known 32-word pattern, prints `va2pa` PA over UART, then spins. Process B (launched after) `mmap(_PAGE_SIZE, MAP_PHYSMEM|MAP_UNCACHED, pa)` at that PA and prints the 32 words read back. **Capture:** UART. **Pass:** B reads A's pattern verbatim. This confirms Option A's shared-BO premise before any daemon exists.

**(b) 2-process abort discriminator.** Add temporary prints to `v3d_phoenix_winsys.c`: in `apply_core_regs()` log `"v3d dbg: apply_core_regs pid=%d writing MMU_PT_PA_BASE=0x%x (was 0x%x)"` (read-back the old value first), and at the top of `ioc_submit_cl()` log `pid` + the live `MMU_PT_PA_BASE`. Launch the glamor X server (GPU owner), then at the psh prompt launch a second minimal GL client (a `gl_frontend_smoke`-style clear). **Capture:** full UART (`--capture-secs 240`, Bash `timeout 420000`) + HDMI ticks; run under `test-cycle-bench.sh` for ~3 trials (the wedge/abort may be intermittent). **Read out with `uart-summary.sh`.** **Determines:** whether the fault coincides with (a) the 2nd process's `powerOn`, (b) `MMU_PT_PA_BASE` changing under the first process, or (c) the first process's next submit through the stolen base — plus the abort PC / SError ESR. That fixes which of the §1.2 conflicts fires first and sizes the minimal daemon arbitration.

*(Analysis-only task: neither experiment was run.)*

---

## M0 — HW result (2026-08-22): concurrent conflict CONFIRMED + characterized

Reproduced with two existing compute probes (no relink needed) via a script on the
netboot export (`csd-matmul & csd-probe`; an inline `bash -c '...'` failed on psh
single-quote parsing, so a script file `bash /gpu-2proc.sh` was used):

- **Sequential (gap between them): WORKS.** csd-matmul then csd-probe each init →
  run → tear down cleanly; powerOn is idempotent; csd-probe returns rc=0 with correct
  output. So the limit is **single *concurrent* GPU process**, not "one ever" — the
  in-process winsys is fine as long as only one process touches the GPU at a time.
- **Concurrent (true overlap): BROKEN — silent corruption, not a clean abort.** With
  both processes GPU-live simultaneously (UART output interleaves): csd-probe STEP2/
  STEP3 FAIL (out `0xeeeeeeee`/garbage vs expected `0xC0DE1234`/0..7) + a **CSD
  TIMEOUT**; csd-matmul runs **42× slower** (504 vs 11.8 ms/matmul) and numeric-FAILs
  (wrong results). Both processes clobber each other's single global `MMU_PT_PA_BASE`
  + interfere on the shared CT/submit registers → each reads/writes the wrong GPU
  memory. Evidence: `artifacts/rpi4b-uart/*-gpu-2proc-m0d.log`.

**Implication for M1 (the v3d-server):** the failure is *insidious* (data corruption
+ timeout, NOT a clean EL1 abort in this compute-only case), so the daemon must
**serialize every submit** (and own power+PT+VA) — a design that only catches a clean
abort/reset would silently corrupt results. This HW result validates Option A's core
requirement (one owner, serialized submits). M0 is complete; M1 is the multi-week build.

## M1 step 2b — HW result (2026-08-22): CSD compute routes through the daemon, BIT-EXACT

First real proof that GPU work executes correctly through the serializing v3d-server
daemon (not the in-process winsys). Netboot, `bash /gpu-csd-daemon.sh`:

- Server: `powerOn PM_GRAFX asb M=ok S=ok` → `V3D up CORE0_IDENT0=0x04443356` →
  `registered /dev/v3d-srv`. It is the sole GPU owner; the client (csd-matmul-daemon,
  linked against libv3d-client, NOT the winsys) connects over the message port.
- 100 CSD dispatches all complete via IPC (`rpi4-v3d: CSD done ... num_completed=1..100`).
- **Numeric: `max_rel_err=0.000e+00`, `o[0]=-7.03244`, `o[255]=-4.46325`, `PASS` —
  BIT-IDENTICAL to the in-process reference.** GPU=12.62 ms/matmul vs in-process 11.96
  (ratio 6.83x vs CPU; the ~modest delta is the per-dispatch IPC round-trip, as expected).

Architecture validated: create_dev-before-power-on single-owner guard; BO handoff by
physical address + client `mmap(MAP_PHYSMEM,pa)`; SUBMIT_CSD forwarding cfg[0..6];
one-message-at-a-time server serialization.

### Known benign diagnostic (deferred to 2c): 4 startup VA COLLISION warnings
The daemon logs 4 `VA COLLISION ... live-BO overlap` at startup (the stale Aug-14
in-process reference showed 0). Root-caused as benign + NOT a correctness bug (result is
bit-exact): csd_matmul allocates its 5 BOs once (monotonic va_alloc, no hole recycling),
so the collisions are the first client BOs landing at the boundary of the 32 MiB
binner-overflow pool that `v3d_gpu_init` eagerly maps. That eager pool is the
"dormant overhead" flagged at 2a — it is consumed ONLY by the CL binner path (stubbed
until 2c). The clean fix (make the pool alloc part of the CL path, or give it a reserved
VA region that doesn't perturb BO allocation) lands with 2c; for CSD it is behavior-neutral
(the detector is "Logged, not fixed" by design and the map proceeds correctly).

## M1 step 2c-server (2026-08-22): CL+TFU lifted; VA-collision fixed + HONESTY CORRECTION
The daemon now has the CL (render) + TFU submit paths (ioc_submit_cl/tfu lifted verbatim
into v3d_gpu.c; descriptor via msg.i.data, no bo_handle array). Build-verified; CL not
HW-tested yet (needs the winsys-as-client refactor to get a CL client).

**Correction to the M1-2b "4 startup VA COLLISION" note above:** I cannot claim those were
daemon-specific. The Aug-14 in-process csd-matmul binary PREDATES the VA-COLLISION detector
(a later winsys-source addition), so its "0 collisions" is trivial (detector absent), not a
clean baseline. The daemon is simply the first binary to run the detector on HW.

**Fix + HW confirmation:** va_alloc now zeros the bump range at hand-out. HW re-run
(m1-2c-vafix): 0 VA COLLISION (was 4) and still bit-exact (max_rel_err=0). ROOT MECHANISM
UNEXPLAINED: static analysis shows the init full-PT zero loop covers the colliding indices,
yet the fresh slot read garbage (PTE=0x17ffffbd) on HW — the one-time init clear apparently
did not persist to first use in the standalone server. At-handout clear fixes the ALLOCATED
path; whether the init zero persists for the ~57k UNALLOCATED PT entries is OPEN and
load-bearing for CL's MMU fault net (moot for CSD). Confirm at CL HW bring-up: do unmapped
PT slots read 0? If not, full-PT-zero persistence is the real fix.
