# M1 — asynchronous multi-queue V3D render server (`rpi4-v3d-async`)

Milestone M1 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §4.3, §4.4,
§4.9 and §5 (M1). It builds on the kernel facts measured or read in
[E5](E5-deferred-reply.md) (deferred replies, parked clients), [E2](E2-stk-submit-breakdown.md) (the
synchronous submit path, step by step) and [E1](E1-vm-object-export.md) (the future buffer export).

**Status (2026-09-26, evening):** part 2 code complete and compiling (`-Wall -Wextra -Werror`),
**no Pi cycle yet**. Part 1 (skeleton, ownership + fences, §12) is queued for its first cycle from
`out/`; part 2 (real job submission, the Mesa adapter, the `quakespasm-v3da` clone) builds into
`out-p2/` and `artifacts/quakespasm-v3da/` so it cannot replace the part-1 binaries. What part 2
implements, and where it differs from the design below, is §15; its pre-registered Pi tests are §16;
the numbers that prove M1 are §17. Nothing is committed. The old lane is untouched.

Evidence tags as in the research doc: **[read]** = read in source at the cited place;
**[measured]** = measured on this hardware (source cited); **[inferred]** = reasoning, not verified.
Line numbers: `phoenix-rtos-devices` HEAD `0425f93` unless marked; the in-process winsys is cited
through E2's step table (`cad97dc` numbering), so E10's A/B names the same steps.

---

## 0. Decisions at a glance

| Question | Decision |
|---|---|
| Where does the code live? | `tools/gpu-lane/v3d-async/` (coord repo) for M1 part 1, moved to `sources/phoenix-rtos-devices/gpu/rpi4-v3d-async/` with a `Makefile` at the M1 gate. Reason in §1.1. |
| Names | server binary `rpi4-v3d-async`, device **`/dev/v3d-async`**, client library `libv3da-client`, probe `v3dasync-ping`. The old daemon registers `/dev/v3d-srv` (`gpu/rpi4-v3d/v3d_rpc.h:75`); the in-process winsys registers nothing; no other `create_dev` in the devices tree names `v3d*` (grep, §1.2). |
| Process shape | one process: N dispatch threads (`msgRecv`), one **event thread** (IRQ-driven or polling, same code), later one CPU-queue worker. One server mutex. |
| Interrupt | GIC SPI 74 = Phoenix IRQ **106**, one line for hub **and** core (§3). Handler = W1C + OR into an event word. **Off by default**; switched on at runtime (`DBG_IRQ_MODE`) so the first cycle proves poll mode before it risks the interrupt. |
| Queues | BIN, RENDER, TFU, CSD, CACHE_CLEAN, CPU. One job in flight per queue; queues run concurrently; render(N) waits for bin(N). **As built (§15):** CACHE_CLEAN unused (FLUSH_CACHE inline, knob-gated); concurrency is a runtime mode, `serial` (one hardware job at a time, oldest first) by default, `pipeline` switchable. |
| OUTOMEM | pre-mapped overflow pool cut into chunks; the IRQ handler hands the **pre-staged** chunk itself (register writes only); the event thread re-stages and attributes chunks to the bin job; a chunk returns to the pool when *its render job* completes. |
| Cache maintenance | **every empirically needed step of the old path is kept by default** (§5, E2 steps 1–16). Candidates to drop are flags, off, for E10-style A/B only. |
| Fences | one read-only fence page (cached, `MAP_PHYSMEM` today, E1 export later); fence = `(slot, queue, seqno)`, where `slot` is the **submitting client's** row, so seqnos are known at submit time and complete in order. |
| Waits | fast path: read the fence page, no IPC. Slow path: `FENCE_WAIT`/`SYNCOBJ_WAIT`/`BO_WAIT` parked and answered by the event thread; **every wait bounded** (≤ 2 s server-side, client loops); answered exactly once. |
| BOs | M1a: one `MAP_CONTIGUOUS` block per BO (as today, so `MAP_PHYSMEM` mapping keeps working), **pooled and never returned to the kernel** while the server lives; quarantine on free (PTE clear → TLB flush → fence pass). M1b: power-of-two contiguous slabs sub-allocated, stitched by the V3D MMU. |
| Client identity | client `open()`s the node; `mtOpen` returns a per-open **client id** (becomes the fd's `oid.id`); `HELLO` is an `ioctl()` on that fd; later requests go by direct `msgSend` to `{port, id}`. Death = the kernel's `mtClose` for that id. |
| Protocol | own opcodes, raw-only for everything except submits; a superset of the old `v3d_rpc.h`; memory handed out as a **memref** (`PHYS pa` today, `OID` after E1) so the mapping method can change without an ABI break. |
| Power | only through `/dev/vcmbox`, with clock read-back; **no direct-FIFO fallback** — the server refuses to start without vcmbox. |
| First app | a **quakespasm clone** (`quakespasm-v3da`), `+timedemo demo1` (§11). |

---

## 1. Constraints and placement

### 1.1 Why the skeleton is not (yet) in `phoenix-rtos-devices`

* `phoenix-rtos-devices/Makefile` includes **every** `Makefile` below it:
  `ALL_MAKES := $(shell find . -mindepth 2 -name Makefile …)` [read]. A new component directory
  with a `Makefile` becomes part of every devices build the coordinating session runs, including
  its failure modes, before this code has seen hardware.
* Any untracked file in a sibling makes `rebuild-rpi4b-fast.sh --scope auto` treat the repo as dirty
  (`repo_is_dirty`, `git status --short`) and switch the coordinator's rebuild to `core` scope [read,
  `scripts/rebuild-rpi4b-fast.sh:218-259`].
* The M0 probes (`ipcprobe`, `exportprobe`, `kmsprobe`) already live in `tools/gpu-lane/` and build
  standalone against the tree sysroot; M1 part 1 follows that pattern (§10).

At the M1 gate the directory moves to `sources/phoenix-rtos-devices/gpu/rpi4-v3d-async/` with a
`Makefile` (server + `libv3da-client` static lib, both `%LICENSE%`, as the old daemon's files are).
The old directory `gpu/rpi4-v3d/` is not touched at any point.

### 1.2 What the old lane owns, and the one hard rule

* In-process winsys: every game, SDL2, vkQuake (`mesa/v3d_phoenix_winsys.c`,
  `v3d_phoenix_power.c`). It powers and resets the V3D **inside the application** on first use.
* Old daemon `rpi4-v3d` → `/dev/v3d-srv`: launched only by `startx_gpu` (glamor X desktop).
* **Hard rule: exactly one owner of the V3D hardware at a time.** Two owners corrupt each other
  silently (M0 of 2026-08-22: wrong results + CSD timeouts + 42× slowdown [measured]). The new
  server resets the block at start and owns `MMU_PT_PA_BASE`; running it next to any old-lane GPU
  app, or next to `rpi4-v3d`, breaks both. Every M1 test cycle therefore runs **no game, no X**, and
  the server is never started by boot scripts before the M1 gate (PLAN ground rule 2).

Name check: `grep -rn 'create_dev(.*"' sources/phoenix-rtos-devices --include=*.c` lists no `v3d*`
device besides the old daemon's `V3D_RPC_DEV_NAME "v3d-srv"`; `/dev/v3d-async` is free.

---

## 2. Process and thread structure

```
                     clients (libv3da-client; later libdrm-phoenix)
   open("/dev/v3d-async") ─ mtOpen → client id      msgSend {port,id} (raw requests)
   ioctl(fd, HELLO)                                   mmap(MAP_PHYSMEM) fence page / BOs
            │                                                    ▲
            ▼                                                    │ read-only, cached
 ┌───────────────────────── rpi4-v3d-async ──────────────────────┴───────────────────┐
 │ dispatch thread ×N (prio 3)     event thread (prio 1)          [CPU worker, M1b]   │
 │  msgRecv → decode →              wakes on: IRQ cond | timeout    runs CPU-queue    │
 │  lock → act → maybe park         drains event word → completes  jobs (timestamps,  │
 │  → unlock → msgRespond           jobs → kicks next → publishes   indirect CSD)     │
 │                                  seqnos → answers parked waits                    │
 │                                  → job watchdog, OUTOMEM restage                  │
 │   ─────────────── srv.lock (one mutex: clients, BOs, queues, waits) ───────────── │
 │ IRQ handler (kernel ctx, server pmap): read CTL/HUB INT_STS → W1C → OR events     │
 └────────────────────────────────────────────────────────────────────────────────────┘
      MMIO: V3D HUB+CORE0 0xfec00000 (64 KiB)   power/clock: /dev/vcmbox (libvcmbox)
```

* **Dispatch threads** (default 2, `-r N`). All receive on the one port (a Phoenix port can be
  received on by several threads [read, `ipcprobe -r`]). A handler takes `srv.lock`, does its work,
  and either fills `o.raw` and responds, or **parks** the request (waits) and returns without
  responding. Heavy work (zeroing a large BO) happens outside the lock where possible.
* **Event thread** (highest priority). Loop: `condWait(irq_cond, lock, timeout)` where `timeout` is
  the earliest of: next job-watchdog check, next parked-wait deadline, next CPU/NOP job deadline, and
  — in poll mode with hardware busy — the poll period (`-p us`, default 200 µs). On wake it drains
  the event word (IRQ mode) or reads and clears the STS registers itself (poll mode), so **the
  completion path is identical in both modes** [design]. It then advances queues, kicks ready jobs,
  publishes seqnos, and moves answerable parked requests to a local list; responses are sent after
  `srv.lock` is dropped (`proc_respond` reschedules after every response, E5 §1(a), so responding
  under the lock would convoy the dispatch threads [inferred]).
* **Scheduler entry** `sched_run()` is called with the lock held by whichever thread changed state
  (a submit, a completion, a dependency signal). Only it writes kick registers.
* **IRQ handler**: kernel context in the server's address space (`userintr_dispatch` does
  `pmap_switch`, `proc/userintr.c:49-96` [read]); MMIO and a volatile OR only — no libc, no locks, no
  fence-page writes, no page faults. The handler's code and data are resident: on MMU builds
  `process->lazy` is 0 and exec `_map_force`s every page of every segment (`proc/process.c:226-230,
  610-623`, `vm/map.c:655-666`) [read]. The skeleton is built with `-mno-outline-atomics` and its
  handler disassembles to straight-line code with no calls (checked with `objdump`).
  Returning ≥ 0 broadcasts the cond (`userintr.c:85-88`). Same split as `genet_irqHandler` /
  `genet_irqThread` (`phoenix-rtos-lwip/drivers/bcm-genet.c:1247-1300,1972-1996` [read]).

---

## 3. Interrupts

### 3.1 Line and number

* Device tree: `v3d: gpu@7ec00000 { interrupts = <GIC_SPI 74 IRQ_TYPE_LEVEL_HIGH>; }`
  (`external/linux/arch/arm/boot/dts/broadcom/bcm2711.dtsi:605-614`, same in
  `bcm2711-rpi-ds.dtsi:554-567`) [read]. The DTB the bench actually boots
  (`.buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/bcm2711-rpi-4-b.dtb`, `dtc -I dtb`) has
  `v3d@7ec04000 { interrupts = <0x00 0x4a 0x04>; status = "disabled"; }` = SPI 74, level-high
  [read]; `disabled` only means no Linux driver binds it. **One** interrupt line.
* Phoenix numbers SPIs as GIC IDs (`SPI_FIRST_IRQID 32`, `hal/aarch64/interrupts_gicv2.c:35`;
  genet uses SPI 157 = 189, `bcm-genet.c:237`) → **IRQ 106**. `SIZE_INTERRUPTS` is 256 on this
  platform (`hal/aarch64/generic/config.h:36`). Every SPI is configured level-high
  (`_interrupts_gicv2_classify`, `generic/generic.c:32-35`) and routed to all CPUs
  (`DEFAULT_CPU_MASK`, `interrupts_gicv2.c:41,254`) [read]. Level-high matches the DT.
* Linux on BCM2711 takes the `single_irq_line` path: the core ISR handles core sources and, if it
  found none, falls through to the hub ISR (`drivers/gpu/drm/v3d/v3d_irq.c:143-147,280-293`) [read].
  Our handler reads **both** status registers every time.

### 3.2 Sources and registers (V3D 4.2)

| Block | Register | Offset | Bits used | Source |
|---|---|---|---|---|
| core | `CTL_INT_STS / SET / CLR` | 0x50 / 0x54 / 0x58 | FRDONE 0, FLDONE 1, OUTOMEM 2, SPILLUSE 3, TRFB 4, GMPV 5, PCTR 6, **CSDDONE 7** (ver < 71), QPU 27:16 | `v3d_regs.h:275-290` [read] |
| core | `CTL_INT_MSK_STS / SET / CLR` | 0x5c / 0x60 / 0x64 | a set MSK bit masks the source | `v3d_regs.h:278-280`, `v3d_irq.c:314-317` [read] |
| hub | `HUB_INT_STS / SET / CLR` | 0x50 / 0x54 / 0x58 | TFUF 0, **TFUC 1**, MSO 2, MMU_CAP 3, MMU_PTI 4, MMU_WRV 5 | `v3d_regs.h:62-74` [read] |
| hub | `HUB_INT_MSK_STS / SET / CLR` | 0x5c / 0x60 / 0x64 | | `v3d_regs.h:65-67` [read] |
| hub | `MMU_CTL` | 0x1200 | status bits W1C: Linux writes `MMU_CTL = MMU_CTL` after an MMU interrupt | `v3d_irq.c:202` [read] |
| hub | `MMU_VIO_ID / VIO_ADDR / DEBUG_INFO` | 0x122c / 0x1234 / 0x1238 | fault decode (AXI id ranges L2T/PTB/PSE/TLB/CLE/TFU/MMU/GMP) | `v3d_irq.c:172-237`, `v3dx_simulator.c:370-422` (MIT) [read] |

Enabled set (Linux parity, `v3d_irq.c:23-33`): core `OUTOMEM|FLDONE|FRDONE|CSDDONE|GMPV`, hub
`MMU_WRV|MMU_PTI|MMU_CAP|TFUC`. TFUF is not an interrupt source in Linux; the event thread still
reads it raw after a TFUC or a TFU timeout, as the old code does (`v3d_gpu.c:1380-1397`).

**What the old code does:** it never touches `INT_MSK_*` and registers no handler; it polls raw
`CTL_INT_STS` / `HUB_INT_STS` in spin loops and clears with W1C (`v3d_gpu.c:1209,1221-1231,1276,
1290-1298,1358,1380-1390`; the winsys comment calls `HUB_INT_STS` "polled raw"). `STS` is the raw
status regardless of mask (the old code's own comment on `HUB_INT_MSK_STS`, `v3d_gpu.c:134`) [read].
The **reset value of the masks is not known** [inferred: probably all masked, since nothing has ever
stormed on line 106 with no handler — but the GIC line is disabled without a handler, so that proves
nothing]. The server therefore sets them explicitly.

### 3.3 Bring-up order (the storm-proof sequence)

1. power on, map MMIO, verify identity (§8);
2. `CTL_INT_MSK_SET = ~0`, `HUB_INT_MSK_SET = ~0` (mask everything);
3. `CTL_INT_CLR = ~0`, `HUB_INT_CLR = ~0` (drop anything latched);
4. poll mode: stop here. IRQ mode (`DBG_IRQ_MODE on`, or `-i`): `interrupt(106, v3da_irq, &hw,
   irq_cond, &handle)` — the handler is registered while every source is still masked;
5. `CTL_INT_MSK_CLR = CORE_IRQS`, `HUB_INT_MSK_CLR = HUB_IRQS` (Linux `v3d_irq_enable`,
   `v3d_irq.c:304-321`).
6. After every GPU reset: steps 2, 3, 5 again (Linux `v3d_irq_reset`).

Turning IRQ mode off: mask all, `resourceDestroy(handle)` (userintr `rtInth` resource;
`userintr_put` deletes the handler and disables the GIC line when it was the last one,
`userintr.c:26-46`, `interrupts_gicv2.c:289-304`) [read].

### 3.4 The handler

```
core = CTL_INT_STS; hub = HUB_INT_STS;
if (core) CTL_INT_CLR = core;                      // raw, includes latched QPU bits (Linux parity)
if (hub)  { if (hub & MMU_*) MMU_CTL = MMU_CTL;    // W1C the MMU fault status (v3d_irq.c:202)
            HUB_INT_CLR = hub; }
if ((core & OUTOMEM) && staged chunk present)       // §4.3
    { PTB_BPOA = chunk.va; PTB_BPOS = chunk.size; mark chunk consumed; }
events |= core; hub_events |= hub; count++;
if (!core && !hub && ++spurious > 1000)     { MSK_SET = ~0 both; storm = 1; }  // line stuck
if (count - count_at_last_event_loop > 2e5) { MSK_SET = ~0 both; storm = 2; }  // re-latching source
return (core|hub) ? 1 : -1;                  // -1: no cond broadcast (userintr.c:85)
```

All shared words are written with `__atomic` builtins; the event thread takes them with
`__atomic_exchange_n(…, 0)`. A level interrupt that is not cleared re-fires as soon as the kernel
writes EOI (`interrupts_dispatch`, `interrupts_gicv2.c:106-150`) — the clear is not optional, and
the storm guard converts "hung kernel" into "server logs `IRQ STORM`, falls back to polling"
[design; the guard is the part a first cycle validates only if something goes wrong].

---

## 4. Queues, jobs and the bin/render pipeline

### 4.1 Queues

| Queue | HW | Done event | One in flight | Notes |
|---|---|---|---|---|
| BIN | CT0 (`CLE_CT0Q*`, 0x15c–0x174) | FLDONE | yes | tile alloc `QMA/QMS/QTS` from the submit; `PTB_BPOS = 0` before kick |
| RENDER | CT1 (`CLE_CT1QBA/QEA` 0x164/0x16c) | FRDONE | yes | runnable only after its own bin job's fence |
| TFU | hub TFU (0x400–0x430) | TFUC (TFUF raw) | yes | `ICFG` write kicks (with IOC) |
| CSD | `CSD_QUEUED_CFG0..6` (0x904…) | CSDDONE | yes | wait "no CURRENT dispatch" before kick (winsys :3466-3476) |
| CACHE_CLEAN | L2TCACTL | synchronous (bounded spin, µs) | yes | TMUWCF + L2T clean for `SUBMIT_CL_FLUSH_CACHE` and CSD epilogues |
| CPU | none (server CPU) | worker | yes | v3dv CPU jobs (timestamps, perf queries, indirect CSD); in part 1 only the **NOP test job** |

Linux runs exactly these queues, `credit_limit = 1` each, concurrently (`v3d_sched.c`; research §3.1)
[read]. One job per queue in flight keeps the old code's per-job register model valid.

### 4.2 Job life cycle

```
SUBMIT (dispatch, locked): validate handles + syncs → allocate job(s) → take BO refs (in-flight)
   → assign seqno = client.slot.next[q]++ → append to client FIFO[q] → sched_run() → respond
      {fence(s)} immediately (~20 µs RTT design figure; E5 measures it)
READY  : head of its client FIFO[q]; all in-fences signalled; (RENDER) its bin job done;
         (BIN) an overflow chunk can be staged
KICK   : prologue (§5) → program → kick → queue.active = job, job.t_kick, watchdog armed
DONE   : event thread sees the done bit → epilogue (§5) → fence page slot.completed[q] = seqno
         (release store) → drop in-flight BO refs → answer waiters → sched_run()
FAILED : watchdog (§7) → reset → every in-flight job completes with ERROR (fence advances, error
         recorded) → queued jobs kept → sched_run()
```

Cross-client order: round-robin per queue over clients whose FIFO head is READY (drm_sched shape).
Within a client each queue is FIFO, so `(slot, q, seqno)` completes in seqno order — that is what
makes a fence known at submit time and checkable with one load.

### 4.3 Bin/render pipelining and OUTOMEM without spinning

* **Pipelining:** bin(N+1) may run while render(N) runs — they are different queues. Mesa already
  expresses the bin→render dependency inside one `SUBMIT_CL` (and chains submits through
  `in_sync_bcl`/`in_sync_rcl` = `out_sync`, `external/mesa/src/gallium/drivers/v3d/v3d_job.c:657-690`
  [read]). The server honours `in_sync_bcl` for BIN and `in_sync_rcl` for RENDER separately, so a
  client whose next bin depends only on the previous *bin* overlaps naturally [inferred about what
  Mesa actually sets per job — to be observed in part 2].
* **Overflow memory must outlive the bin job:** the binner writes tile lists into overflow chunks;
  the render job reads them. So a chunk handed to bin(N) is attributed to bin(N) and returns to the
  free pool only when **render(N)** completes (Linux attaches the overflow BO to the render job's
  unref list, `v3d_irq.c:73-74`) [read]. The old code resets its cursor per job and hands the whole
  32 MiB remaining pool in one grant (`v3d_gpu.c:1188,1225-1235`), which is correct only because it
  never overlaps two jobs.
* **Chunking:** the 32 MiB pool (`BINOVF_PAGES` 8192, `v3d_gpu.c:158`) is cut into chunks
  (default **1 MiB**, `-c KiB`; Linux uses 256 KiB, `v3d_irq.c:44`). [inferred: the old whole-pool
  grant avoided repeated OUTOMEM stalls; chunk size is an A/B knob, not a correctness choice.]
* **No spinning:** before each BIN kick the event thread **stages** one free chunk in a single
  atomic slot. On OUTOMEM the handler (or the poll path) writes `PTB_BPOA/BPOS` from the staged slot
  at once and marks it consumed; the event thread then attributes it to the active bin job and
  stages the next. If nothing is staged (pool exhausted by overlapped jobs), the binner simply stays
  stalled — OUTOMEM is edge-signalled and FLDONE is not reported while OOM is pending
  (`v3d_irq.c:112-117`) [read] — until a render completes and frees chunks; then the event thread
  grants directly. If the pool is empty and **no** render is in flight, the server grows the pool
  (map another block, flush TLB) or, capped, fails the job — never a silent wedge.
* Chunk memory is premapped at init (as today), so a grant needs no TLB flush; a grown pool block
  does (Linux flushes after mapping, `v3d_irq.c:77`).

---

## 5. Per-job cache and TLB maintenance

**Rule: M1 ships the old sequence unchanged. Dropping any step is a separate, flagged A/B (E10).**
Step numbers are E2's (winsys `cad97dc`); the second column is the same step in the old daemon
(`gpu/rpi4-v3d/v3d_gpu.c`, identical sequence), which the new server copies.

| E2 step | old daemon | What | Kept? | Drop candidate? (flag, default off) |
|---|---|---|---|---|
| 1, 3 | :1192 | `dsb sy` before the first MMIO poke (Normal-NC BO stores → DRAM) | **yes, always** | never |
| 4 | :1195 | `SLCACTL = INVAL_ALL` early (#67 ordering fix: later waits are its settle time) | yes | no |
| 5 | :1197 → :952-959 | `mmu_flush_tlb` (MMUC flush + TLB clear, both spun) **every job** | yes | `--tlb-on-change`: flush only when the PT changed since the last flush (Linux flushes on insert/remove only, `v3d_mmu.c`). E2's U3. |
| 6 | :1200-1202 | L2T flush: wait-old, issue `L2TFLS`, **wait-new** | yes | `--no-l2t-wait-new`: Linux does not wait ("L2T accesses will be stalled until the flush has completed", `v3d_gem.c:177-190`) |
| 7 | :1205-1207 | **fix-A**: a second identical waited L2T flush | **yes** | `--no-fixa` — removal regressed on 2026-07-26 (1/3 boots, 94 CT1 timeouts, E2 step 7); E10 re-asks under async |
| 8 | :1209-1213 | `INT_CLR = FLDONE|FRDONE|QPU`; `PTB_BPOS = 0`; `CT0QMA/QMS`, `CT0QTS`; kick `CT0QBA/QEA` | yes (INT_CLR only in poll mode; the handler W1Cs in IRQ mode — clearing FRDONE here would **lose render(N-1)'s completion** when bin(N) is kicked during render(N-1)) | — |
| 9 | :1215-1249 | bin completion: FLDONE, OUTOMEM service | replaced by §4.3 (event-driven) | — |
| 11 | :1276-1282 | bin→render handoff: waited L2T flush, `SLCACTL` — now the **render prologue** | yes | `--no-handoff-wait` (Linux: `v3d_invalidate_caches` without wait before render, `v3d_sched.c:292`) |
| 12 | :1284 | kick CT1 | yes | — |
| 13 | :1296-1298 | ack latched QPU interrupt bits during a long render (collapsed the STK render wedge 330→0 [measured, old comment]) | yes: the handler clears raw STS incl. QPU bits on every interrupt; the watchdog tick (≤ 160 ms) clears them too while a render is in flight | no |
| 14 | :1307-1331 | wedge → dump → `reset_reinit_core` → drop job | yes, via §7 | — |
| 15 | :1334-1335 | post-render: wait-old, `L2TFLS|FLM_CLEAN` **not waited** (drained by the next prologue's wait-old) | yes, in the render epilogue | no |
| 16 | winsys :3013-3036 | `SUBMIT_CL_FLUSH_CACHE`: TMUWCF + waited clean only with `V3D_CL_CACHE_CLEAN=1` (default off; C1 A/B arm) | as a CACHE_CLEAN job **only** when the flag is set in the submit **and** `--cl-cache-clean` (default off = old default) | — |
| TFU | :1350-1355, :1358, :1403-1408 | prologue `dsb` + TLB + `SLCACTL` + waited L2T flush; clear TFUC/TFUF; epilogue TMUWCF (spun) + waited clean + `SLCACTL` | yes | TLB per `--tlb-on-change` |
| CSD | :971-976, :998-1003 | prologue `dsb` + `SLCACTL` + TLB + waited L2T flush; epilogue TMUWCF + waited clean + `dsb` | yes | same |

`apply_core_regs` (copied verbatim, `v3d_gpu.c:575-599`) writes `MISCCFG = QRMAXCNT | OVRTMUOUT`.
The research doc lists "never write `MISCCFG`" among the learned disciplines (§1.4); the HW-proven
sequence does write it (`V3D_QRMAXCNT >= 0` block, comment "required by our Mesa build"). The server
keeps the proven sequence; the research doc's phrasing needs reconciling, not the code.

Concurrency caveat [inferred, the one real new risk of §5]: with two queues busy, a prologue's
waited L2T flush for one queue runs while the other queue's job uses L2T. Linux does exactly that
(`v3d_invalidate_caches` at every bin and render start, `v3d_sched.c:238,292`), and GFXH-1897 only
forbids *issuing* a flush while one is pending (hence every wait-old). The TLB clear of step 5 while
another job runs is also what Linux does on every PTE change. Still, this is exactly where a new
wedge would come from; the first render bring-up (part 2) runs with `--serialize` (never two queues
at once — the old behaviour with async completion) before enabling overlap.

---

## 6. Fences, syncobjs and waits

### 6.1 The fence page

One 4 KiB page, `mmap(MAP_ANONYMOUS | MAP_CONTIGUOUS)`, **cached on both sides** (two memory
types on one PA is the stale-dirty-line bug class; cached Normal memory is coherent between the A72
cores, E5 §1(d)). Server writes, clients map it `PROT_READ` via the memref from `HELLO`.

```
header (256 B): magic 'V3DF', version, nslots, reset_gen, flags (irq on/off, storm),
                server_pid, heartbeat (u64, bumped every event-thread loop),
                hw_submitted[8], hw_completed[8]  (global per queue, all clients)
slot[60] (64 B each): completed[6] (u64 per queue), error_seq (u64: (queue << 56) | seqno of the
                      last job that completed with an error), gen (u64)
```

Every value is a naturally aligned u64 written with a release store and read with an acquire load
(single-copy atomic on AArch64) — no seqlock needed. `slot.gen` changes when a slot is reused, so a
stale fence from a dead client can never match a new client's seqno. `heartbeat` lets a client in
poll-wait mode notice a dead server.

**Security today:** `MAP_PHYSMEM` lets any process map the PA writable (E5 `fence_rw` predicts
`map_rw=1`), so a buggy client could corrupt fences. The design never trusts the fence page for
**server** decisions (the server's own copy is authoritative); a corrupted page can only mislead
clients reading it. E1's export makes it read-only by construction.

### 6.2 Fences and syncobjs

* A **fence** is `(slot, queue, seqno)` (16 bytes; `v3da_fence_t`). Signalled ⇔
  `page.slot[slot].completed[queue] >= seqno` **and** `slot.gen` matches.
* A **syncobj** (server object, per client, handle namespace per client) holds either nothing, a
  fence, or "signalled". `SUBMIT_*` replaces each out-syncobj's fence; `SIGNAL`, `RESET`, `QUERY`,
  `WAIT(any|all, timeout, WAIT_FOR_SUBMIT)` map 1:1 to `DRM_IOCTL_SYNCOBJ_*`. Timelines are not
  implemented (v3dv disables them, `v3dv_device.c:1574-1580` [read, research §3.7]).
* The client library mirrors each of its own syncobjs' current fence, so `drmSyncobjWait` on a
  syncobj only this client writes needs **no IPC** when already signalled — Mesa's gallium
  `glFinish` → `drmSyncobjWait(out_sync)` (`v3d_context.c:315`, `v3d_job.c:721`) [read] becomes one
  load on the fast path.
* **Implicit sync / `WAIT_BO`**: each BO records its last writer/reader fence per queue (from the
  submit's BO list). `BO_WAIT` = wait for all of them. Mesa relies on it: `v3d_bo_map()` waits
  before every CPU map (`v3d_bufmgr.c:612-623`) and the BO cache probes `v3d_bo_wait(bo, 0)`
  (`:104`) [read]. Today both are no-ops (`WAIT_BO` returns 0, `libv3d-client.c:311-312`), correct
  only because submits are synchronous. **So the new client must send the BO handle list** (the old
  one sends none, `libv3d-client.c:419-430`).

### 6.3 Parked waits — E5's hazards designed out

| E5 hazard | Design |
|---|---|
| Deferred reply works only from the **same process** (another process's respond unmaps the wrong address space) | only the server's own threads respond [read, `proc/msg.c:208-214`] |
| **Rids are reused at once** (lowest free); a late or duplicate respond answers someone else | every parked request is a `v3da_wait_t` owned by exactly one list; **it is removed from that list under `srv.lock` before anyone responds**, and whoever removed it is the only responder. Timeouts, completions, reset, client death and server quit all go through the same `wait_claim()` |
| Parked client **cannot be interrupted or killed** | every wait is bounded: server clamps to `V3DA_WAIT_MAX_MS` = 2000 ms and answers `-ETIMEDOUT`; the library loops for longer or infinite waits, so no client is parked > 2 s |
| Parked payload windows (`i.data`) stay mapped in the server | wait requests are **raw-only** (≤ 64 B); `mtDevCtl` is never packed into raw by the kernel (`msg.c:284-286`), so `i.data = NULL` is the only way to hold nothing |
| Server **dies** holding rids → clients wedged forever, unkillable (`proc_portsDestroy` never walks `p->rid`) | cannot be fixed in the server. Mitigations: (1) the fast path makes parked waits rare; (2) a client knob `V3DA_WAIT=poll` never parks (spins on the fence page with `usleep` backoff and watches `heartbeat`); (3) `DBG_QUIT` answers every parked request before exit; (4) **kernel follow-up E5 §5 item 3** (reject received-but-unanswered rids on port destroy) is an **M1-gate dependency** before any shipped app parks in this server |
| GPU reset | every waiter on an errored fence is answered `-EIO`… after the fence advances (so fast-path readers also move on); `reset_gen` bumps |
| Client death | `mtClose(id)` → claim and answer all of that client's parked requests `-EPIPE` (a sibling thread may still be parked), then drop its refs |
| `hal_cpuReschedule` after every respond (N waiters = N reschedules) | answers are batched per event-thread loop, sent after unlock |

### 6.4 What happens on a server crash

* Clients parked in the server: wedged until kernel item 3 lands (above). Clients not parked get
  `-EINVAL`/`-ENOENT` on their next `msgSend` (port gone).
* GPU: may still be executing a job against pages the kernel is reclaiming (the process's
  `MAP_CONTIGUOUS` BOs are freed at exit). **This is the C1 hazard class** (E1 §7). The server
  cannot help after it is dead; what it can do while alive: never exit with a job in flight
  (`DBG_QUIT` refuses while any queue is busy; a fatal-signal path is not available on Phoenix
  [inferred]). M3 needs the device-owner reference E1 §7 calls for. Recorded as risk R3.
* Fence page: clients reading it after the crash see a frozen heartbeat.

---

## 7. Scheduling across clients, job timeout, reset

* **Per-client FIFO per queue**, round-robin per queue over clients with a READY head. No priorities
  in M1.
* **Watchdog:** checked every 125 ms of a job's life by the event thread (timeout-driven, not a
  spin). BIN/RENDER: after 500 ms, if `CTnCA` (0x110/0x114) or `CTnRA` (0x118/0x11c) moved since the
  last check, extend (Linux `v3d_cl_job_timedout`, `v3d_sched.c:750-770` [read]); else declare a
  wedge. Hard cap 10 s per job even while progressing [inferred: protects other clients from one
  runaway]. TFU: 500 ms. CSD: progress = batches counter (`CSD_CURRENT_CFG4`, as Linux,
  `v3d_sched.c:800-812`). While a render is in flight each check also acks latched QPU interrupt bits
  (E2 step 13).
* **Wedge:** raw one-line dumps as the old code prints them (`BIN TIMEOUT … mmu_ill=…`,
  `v3d_gpu.c:1250-1271,1307-1321`, including `wedge_cl_dump`), then: mask interrupts → `idle_axi`
  (GMP stop, `v3d_gpu.c:1041-1049`) → V3D reset (ASB stop, `PM_V3DRSTN` assert, power-on with clock
  read-back via vcmbox) → `apply_core_regs` → re-enable interrupt masks → `reset_gen++` → every
  in-flight job completes with error (fence advances, `slot.error_seq` set) → queued jobs stay →
  `sched_run()`. This keeps today's "drop the frame" semantics (the wedge is data-dependent;
  resubmitting re-hangs, `v3d_gpu.c:1323-1325` [measured, old comment]) while other clients carry on.
  Mesa does not see a per-fence error through `drmSyncobjWait` [inferred from Linux semantics], so GL
  apps just lose one frame, as today.

---

## 8. Power, clock, identity

* Only through `/dev/vcmbox` (`libvcmbox`: `vcmbox_call`, `misc/rpi4-vcmbox/libvcmbox.h`) — the
  project rule, and the lesson of two outages from racing the unarbitrated FIFO (quake flicker
  `3cc684c`; the V3D clock race, root-caused 2026-09-11: a lost `SET_CLOCK_STATE` response left the
  clock off and the next MMIO read never completed, `v3d_phoenix_power.c:394-461`) [read].
* Sequence (the HW-proven order of `v3d_phoenix_powerOn`, `v3d_phoenix_power.c:465-522`, and
  `v3d_gpu.c:425-470`): `SET_QPU_ENABLE 1`, `SET_DOMAIN_STATE(V3D, on)`, `SET_CLOCK_STATE(V3D, on)`;
  clock on, 50 µs, clock off; `PM_GRAFX |= V3DRSTN` (with `PM_PASSWORD`); **clock on with
  read-back** (`GET_CLOCK_STATE` bit0, up to 20 tries); 50 µs; ASB master + slave enable (ACK
  polled); 2 ms. Every vcmbox return code is checked; **no direct-FIFO fallback** (the old daemon
  still races the FIFO directly, `v3d_gpu.c:267-360,403-421` — the new server does not carry that
  code at all). No vcmbox → the server refuses to start.
* **Identity proof** (what the first cycle checks): read `CORE0_IDENT0/1/2` (0x0/0x4/0x8),
  `HUB_UIFCFG`, `HUB_IDENT1/2/3` (hub 0x4, 0xc, 0x10, 0x14) and compare with the constants the old
  client reports for GET_PARAM (`libv3d-client.c:233-239`: `0x04443356`, `0x81001422`, `0x40078121`,
  `0x45`, `0x000e1124`, `0x100`, `0xe00`). `CORE0_IDENT0 != 0x04443356` (e.g. `0xdeadbeef` =
  unpowered) → refuse to serve. Also logged once: `GET_CLOCK_RATE` / `GET_CLOCK_MEASURED` for clock 5.
* GET_PARAM in the new server answers the ident params from the **live** registers (the old client
  hard-codes them).

---

## 9. BO manager

### 9.1 Backing

| Stage | Backing | Client CPU mapping | Why |
|---|---|---|---|
| **M1a** (part 2) | one `MAP_CONTIGUOUS` block per BO (uncached by default; `CACHEABLE` flag drops `MAP_UNCACHED`) — as today (`v3d_gpu.c:760-783`) | memref `PHYS pa` → client `mmap(MAP_PHYSMEM, pa)` (as today) | keeps Mesa and the proven client mapping unchanged |
| **M1b** | power-of-two contiguous **slabs** (64 KiB … 4 MiB; exact sizes under `vm_objectContiguous`, `vm/object.c:491`), sub-allocated; a BO = list of extents; the V3D MMU stitches them (one PTE per 4 KiB page, `v3d_gpu.c:780-783`) | memref extent list; client reserves a VA window and maps each extent `MAP_PHYSMEM|MAP_FIXED` (`_vm_mmap` unmaps then maps under `MAP_FIXED`, `vm/map.c:621-625`) [inferred: stitching never tried] | ends power-of-two rounding (a 9 MiB RT costs 16 MiB today) |
| **M3** | same slabs, exported with E1 `memExport` per BO sub-range | memref `OID` → `open("/gpubuf/<id>")` + `mmap(fd)` | E1 exports only sub-ranges of `MAP_CONTIGUOUS` anonymous memory (E1 §1) — which is exactly why M1b uses contiguous slabs rather than plain anonymous (amap) pages, which E1 refuses |

The old code's MMU set-up already supports non-contiguous backing: the PT is flat, one PTE per
4 KiB page with the page's own PA from `va2pa` (`v3d_gpu.c:778-783`, comment "a cacheable mapping may
not be physically contiguous; per-page va2pa is correct either way") [read]. What does **not**
support it is the client side (`MAP_PHYSMEM` of one PA range) — hence the memref.

### 9.2 Zeroing, lifetime, quarantine (a C1 safety property)

* **Zero every BO at hand-out** (Phoenix contiguous pages are not zeroed; a garbage binner BO wedged
  CT1, `v3d_gpu.c:774-776`). Pool blocks are zeroed on reuse, not on free.
* **References:** handle refs (creator + importers), **in-flight job refs** (every BO in a submit's
  list, dropped when that job's fence completes — Linux semantics), CPU-map refs (M3 exports).
* **Free = quarantine, never an immediate free** (research §4.3/§4.9):
  1. last ref dropped → clear the BO's PTEs, `pt_gen++`;
  2. TLB flush (immediately if no job runs; else the next prologue; Linux flushes at once);
  3. record the **fence pass** = `hw_submitted[q]` for every queue; the BO sits in quarantine until
     `hw_completed[q] >= pass[q]` for all q *and* a TLB flush newer than step 1 has executed;
  4. only then do its pages go back to the **server pool** and its VA range to the hole list.
  This makes "V3D writes a page after it was given back" impossible for any job the server knows
  of, including a CL that addresses a VA without listing the BO.
* **Pages never go back to the kernel while the server lives** (default). Two reasons: (a) E1 §6
  found a pre-existing kernel bug — the last `munmap` of a `MAP_CONTIGUOUS` object empties the
  kernel object tree (`vm_objectPut` removes a never-inserted node) — so every BO free today corrupts
  kernel state; (b) C1's working picture is a stale device pointer into a **recycled contiguous
  block** that malloc has since received (`docs/c1-heap-corruption.md` §6). A server-owned pool
  means a stale V3D or stale client `MAP_PHYSMEM` write lands in another GPU buffer, never in a
  malloc heap or kernel structure. A high-water cap (default 256 MiB idle) returns blocks with
  `munmap` only after the E1 §6 kernel fix is on `master`.
* **Client death:** `mtClose(id)` → drop every handle ref of that client (BOs survive while in
  flight or imported), destroy its syncobjs, discard its unstarted jobs (their fences complete with
  error), free its fence slot after its last in-flight job (slot `gen++`). The old daemon's
  `kill(pid, 0)` sweep (`rpi4-v3d.c:92-98,223-231`) returns in part 2 as a backstop in case the
  kernel's `mtClose` does not arrive (Q1).
* *(Superseded by §15.2: handles are global but generation-tagged, `(gen << 13) | (slot + 1)`,
  never reused while the server lives.)* **Handles** stay global (`slot + 1`), as today: Mesa's Phoenix import path opens a foreign handle
  by number (`v3d_bufmgr.c:464-503`). An import takes an explicit ref (`BO_IMPORT`); the old
  implicit route (MMAP_BO of a foreign handle) is accepted and converted to an import ref.

---

## 10. Client protocol

Header: [`tools/gpu-lane/v3d-async/v3da_proto.h`](../../tools/gpu-lane/v3d-async/v3da_proto.h).

* Transport: `mtDevCtl` to `{port, client_id}`; request header `{magic 'V3DA'(0x41443356), op,
  flags, arg[52]}` in `i.raw`; response `{err, op, data[56]}` in `o.raw`. The magic distinguishes a
  raw request from an `ioctl()`-packed one (`ioctl_in_t.request` has `IOC_INOUT` in bits 31:30,
  `include/ioctl.h:22-39`; the magic has bit 31 clear). Only `HELLO` arrives as an `ioctl`.
* **Submits** carry `[descriptor][u32 bo_handles[n]][v3da_sem_t in[n]][v3da_sem_t out[n]]` in
  `i.data`. The client library keeps that buffer **page-aligned** (E5: every unaligned end costs a
  shadow page + copy, `msg.c:111-137`). Replies carry the fence(s) in `o.raw`.
* **Memrefs**: `{kind PHYS|OID, cache, size, pa | (port, id)}`. `BO_MMAP` and `HELLO` (fence page)
  return memrefs, never bare PAs — switching to E1 is a server change plus a library change, no
  opcode change.

| Opcode | DRM equivalent | Old `v3d_rpc.h` | Part 1 |
|---|---|---|---|
| `HELLO` (ioctl) | — (open) | — | ✅ |
| `GET_INFO` | — | — | ✅ idents, irq mode, limits |
| `GET_PARAM` | `DRM_IOCTL_V3D_GET_PARAM` | client-local constants | ✅ live idents + caps |
| `BO_CREATE` | `V3D_CREATE_BO` | `CREATE_BO` | ✅ |
| `BO_CLOSE` | `GEM_CLOSE` | `GEM_CLOSE` | ✅ (quarantine) |
| `BO_MMAP` | `V3D_MMAP_BO` | `MMAP_BO` (bare pa) | ✅ memref |
| `BO_GET_OFFSET` | `V3D_GET_BO_OFFSET` | `GET_BO_OFFSET` | ✅ |
| `BO_WAIT` | `V3D_WAIT_BO` | client no-op | ✅ (parks on last fences; none yet in part 1) |
| `BO_IMPORT` | `GEM_OPEN` / PRIME import | implicit | reserved |
| `SUBMIT_CL / TFU / CSD` | `V3D_SUBMIT_CL / TFU / CSD` (+ MULTI_SYNC ext) | same, synchronous | part 2 ✅ (own wire descriptors, §15.2) |
| `SUBMIT_CPU` | `V3D_SUBMIT_CPU` (ext 0x02–0x07) | silently 0 | reserved |
| `SUBMIT_NOP` | — (test job on the CPU queue) | — | ✅ |
| `FENCE_WAIT` | — (library fast path's slow half) | — | ✅ |
| `SYNCOBJ_CREATE/DESTROY/WAIT/RESET/SIGNAL/QUERY` | `DRM_IOCTL_SYNCOBJ_*` | stubs in `v3d_libdrm_shim.c` | ✅ server objects; part 2: submits attach fences, `SYNCOBJ_IMPORT` (sync-file emulation) |
| `PERFMON_*` | `V3D_PERFMON_*` | no-op | reserved `-ENOSYS` |
| `SCANOUT_INFO / SCANOUT_BO / FLIP` | — (transitional present family for the SDL glue) | winsys-only | part 2 ✅ `SCANOUT_INFO`, `FLIP` (fence-gated); a scanout BO is `BO_CREATE` + `V3DA_BO_SCANOUT` (`SCANOUT_BO` stays reserved) |
| `DBG_IRQ_MODE / DBG_IRQ_SELFTEST / DBG_BO_CHECKSUM / DBG_STATS / DBG_QUIT` | — | — | ✅ |
| `DBG_SET_MODE / DBG_QSTATS` | — | — | part 2 ✅ serial/pipeline + cache knobs; per-queue busy/overlap counters |

**Transitional present family.** The games' SDL2 glue calls the winsys's
`v3d_phoenix_scanout_active/double/nbuf`, `v3d_phoenix_set_next_scanout` and `v3d_phoenix_flip`
(`phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c`) [read]. For the M1 clone the library provides
the same symbols: `SCANOUT_BO` backs the next RT with firmware-fb pages (the winsys's aliasing,
server-side), and `FLIP(k, after-fence)` makes the **server** issue `SET_VIRTUAL_OFFSET` via vcmbox
when the fence passes — so the game no longer needs `glFinish()` before a flip [design; the glue's
own `glFinish` stays until part 2 measures without it]. Retired with M2/M3.

---

## 11. Test plan

### 11.1 First cloned app: **quakespasm** (`quakespasm-v3da`)

Why quakespasm over yquake2: `+map`/`+timedemo` work on the shareware data (fork `b2a47ae`,
HW-verified) so a **deterministic timedemo** exists; three glue files instead of four; it avoids the
Quake II FBO-0-redirect traps. yquake2 (38.2–38.8 fps, tight) is the second app. Build: the port's
link line with `libv3d-phoenix.a` minus `v3d_phoenix_winsys.o`/`v3d_phoenix_power.o` plus
`libv3da-client.a` — the proven M3c swap (`tools/x11-port/build-gl-x11-window.sh:94-105`) — under a
new binary name; the shipped `quakespasm` is never touched.

### 11.2 What proves M1 works

| Number | How | Gate |
|---|---|---|
| timedemo fps, old vs new | `quakespasm +timedemo demo1` vs `quakespasm-v3da +timedemo demo1`, same boot image, 3 runs each, interleaved | new ≥ old − 3 % (M1 must not regress); report the ratio |
| CPU/GPU overlap | server stats per 5 s window: Σ GPU busy per queue (kick→done), Σ time with ≥ 2 queues busy, Σ time a client had a job queued while the GPU was idle; client: time spent in `drmSyncobjWait` | overlap share > 0 and rising with `--no-serialize`; E2's U1/U2 bounds are the ceiling |
| correctness | HDMI grab at `+map start` (reference MAE as in the Quake memory note), 0 EL1 faults, 0 wedges | same MAE band as old lane |
| two clients | quakespasm-v3da + a second client (`v3dasync-ping stress`, to be added in part 2) at once | both complete, no corruption |
| C1 posture | quarantine counters: `bo_freed_to_pool`, `bo_quarantine_max`, `pages_returned_to_kernel = 0` | as designed |

STK (`stk-v3da`) follows the quakespasm gate; its number is judged against E2's U1/U2 bounds, not
against a prediction.

---

## 12. Pre-registered first Pi test (part 1): ownership + fences, no GPU jobs

**Question:** does the new server own the GPU (power-on through vcmbox, live identity registers),
does the full request path work (per-open client id, fence page, BO create/map/checksum/close,
deferred `FENCE_WAIT` answered from the event thread, bounded timeout), and does the V3D interrupt
reach a Phoenix handler on IRQ 106 when switched on at runtime?

**Method:** one netboot cycle, stock image, the two binaries staged into the live NFS root's `/bin`
by the coordinator (build: §13). **No game, no X, no `rpi4-v3d`** — the new server resets the V3D and
would break any other GPU user. The server starts in **poll mode**; the ping tool switches IRQ mode on
halfway, so a failure of the interrupt path is isolated from everything before it.

```
./scripts/test-cycle-psh-interact.sh --label m1-v3da-ping --idle-secs 20 -- \
    "/bin/rpi4-v3d-async -r 1" \
    "/bin/v3dasync-ping all" \
    "/bin/v3dasync-ping irq-on" \
    "/bin/v3dasync-ping all" \
    "/bin/v3dasync-ping quit"
```

(Bash `timeout: 600000`.) psh passes no quotes; none are used. **No `&`:** psh has no background
jobs — `cmd &` runs `cmd` in the foreground (psh vforks and waits in `waitpid`,
`psh/runfile/runfile.c:33-48`; the bare `&` is passed on as an argument), which is what voided the
first E5 attempt. The server therefore **detaches itself** (the fixed `ipcprobe server` pattern):
`fork()` before any port, thread, mapping, GPU power-on or `interrupt()` exists (none survive a
fork); the child does all set-up and writes one byte into a pipe once `/dev/v3d-async` is registered,
the GPU is owned and its threads run; the parent then prints `V3DA srv detached pid=<n>` and exits,
so psh gets its prompt back. A child that fails exits, the pipe reads EOF, and the parent prints
`V3DA srv FAIL child did not come up`. A stray `&` argument is accepted and ignored; `-f` keeps the
server in the foreground (no fork). **`-r 1` (one dispatch thread) is
deliberate:** `DBG_QUIT` exits the server 50 ms after answering; with a second dispatch thread, the
ping's own `mtClose` (sent when it exits) could be *received* by that thread just before `exit()` —
a received-but-unanswered request whose sender is then parked forever (E5 condition 2), which would
hang psh. With one thread the `mtClose` stays queued and is rejected when the port is destroyed. The server prints its banner lines
and then only on errors; each ping run prints one tagged line per test and ends with
`V3DAPING RESULT failures=<n> verdict=PASS|FAIL`. Grade with `./scripts/uart-summary.sh m1-v3da-ping`
plus the `V3DA`/`V3DAPING` lines; re-read a garbled line (~1.3 % UART corruption) rather than count
it as a failure.

**Predictions and what each outcome means:**

| Line | Predicted | If instead… |
|---|---|---|
| `V3DA srv power vcmbox=ok qpu=0 domain=0 clk=on tries=<n> … asb_m=ok asb_s=ok` | `clk=on`, `tries` 0 (≥1 = the clock race was caught, fine) | `vcmbox=missing` → server exits 3 (vcmbox not up at that point; retry later in boot). `clk=NOT-CONFIRMED` → stop: power path broken, no MMIO was read |
| `V3DA srv ident core0=0x04443356 …` | all seven values equal the old client's constants | `0xdeadbeef` → unpowered despite `clk=on` (bridge/ASB issue); any other difference → a different V3D revision than the constants assume — record it, it changes GET_PARAM |
| `V3DA srv mmu pt_pa=… reset_msk_core=… reset_msk_hub=… clk_rate_hz=… clk_meas_hz=…` | `clk_rate_hz` 500000000 (research §1.3); the two `reset_msk_*` values are **recorded, not predicted** — they answer R5 | `clk_meas_hz` far from `clk_rate_hz` → PLL not settled (the old render-stall suspect) |
| `V3DA srv ready dev=/dev/v3d-async irq=off irqnum=106 threads=1 …` | printed once (by the child) | missing → read the preceding error line |
| `V3DA srv detached pid=<n>` | printed once, right after `ready`; psh prompt returns | `FAIL child did not come up` → the child's own error line above says why (vcmbox, power, ident); no psh prompt at all → the detach did not happen (check the binary is the rebuilt one) |
| `V3DAPING connect` | `ok=1 id>=1 slot>=0 proto=1` | `open_rc<0` → devfs/`mtOpen` path; `hello_rc<0` → ioctl unpack path; `id==0` → the positive-`mtOpen` multiplexer reading (E5 §1(b)) is wrong |
| `V3DAPING info` | `ident_match=1` | 0 → see ident line |
| `V3DAPING fencepage` | `magic_ok=1 heartbeat_moves=1` | `map_rc<0` → `MAP_PHYSMEM` of a cached page failed; `heartbeat_moves=0` → the event thread is not looping |
| `V3DAPING bo` | `create=0 zeroed=1 sum_match=1 close=0 stale_handle=-22 quarantine_passed>=1 to_kernel=0 reuse=0 reuse_same_pa=1 reuse_zeroed=1` | `zeroed=0`/`reuse_zeroed=0` → zeroing broken (a real wedge source); `sum_match=0` → the client's uncached `MAP_PHYSMEM` view and the server's view differ: **stop**, memory-type mismatch; `reuse_same_pa=0` → the pool was bypassed (not a failure, but the C1 posture is not what the design says) |
| `V3DAPING nopwait` | `ok=1 early=0 after=1 echo_ok=1 wait_ms` ≈ 20 (±5) | `wait_ms` ≫ 25 → event-thread wake latency is coarse (condWait granularity) — matters for every slow-path wait |
| `V3DAPING fastpath` | `ok=1 ipc_waits=0` | `ipc_waits>0` → fence-page visibility lags the server's store |
| `V3DAPING timeout` | `rc=-110 elapsed_ms` 100–120, then `late_ok=1` | ≫ 120 → the server-enforced bound is coarse; `rc=0` early → wrong fence compare |
| `V3DAPING many` | `ok=80/80 bad_token=0 errs=0 parked_now=0`, `parked_max` 1–4 | `bad_token>0` (an answer whose echoed seqno is not the caller's, or that arrives before the fence page shows the fence) → **exactly-once is broken** (rid reuse, E5 condition 3): stop before any GPU work |
| `V3DAPING syncobj` | `create=0 wait_unsignalled=-110 signal=0 wait=0 first=0 reset=0 wait_empty=-22 destroy=0 create_signaled=0 wait_signaled=0` (`wait_unsignalled` uses WAIT_FOR_SUBMIT; `wait_empty` is DRM's `-EINVAL` for an empty syncobj without it) | wrong codes → syncobj table bug |
| `V3DAPING irqtest` (1st run, poll mode) | `mode=poll core_sts_seen=1 hub_sts_seen=1 core_events=1 hub_events=1 handler_delta=0 inconclusive=0` (INT_SET raises the raw status; the poll path feeds the event thread) | `*_sts_seen=0` → `INT_SET` does not latch on V3D 4.2 [the self-test's premise is inferred]: printed `inconclusive=1`, not counted as a failure, and the IRQ test after `irq-on` is inconclusive too |
| `V3DAPING irqmode` | `rc=0 irq=106 mode=irq` | `rc<0` → `interrupt()` refused (number/permission) — record the errno |
| `V3DAPING irqtest` (2nd run, IRQ mode) | `mode=irq handler_delta>=1 core_events=1 hub_events=1 storm=0` (one invocation may see both bits); `core_msk`/`hub_msk` show only the enabled sources unmasked | `handler_delta=0` with `*_sts_seen=1` → the line does not reach IRQ 106 (wrong SPI number, or the GIC route); `storm=1` → the clear sequence is wrong (§3.4) — the guard masked the block and the server fell back to polling |
| 2nd `all` | every line as in the 1st run, `nopwait`/`many` still clean | a difference → IRQ-mode side effect on the event thread |
| `V3DAPING stats` | `parked=0 to_kernel=0`, `loops` growing, `irq_count` 0 in the 1st run and ≥1 in the 2nd | `parked>0` → a leaked wait |
| `V3DAPING quit` | `rc=0 parked=0 inflight=0`, then `V3DA srv exit parked=0 inflight=0` | `parked>0` would mean a leaked wait |
| kernel faults | 0 EL1 faults; no EL0 fault | an EL1 fault → `addr2line` the PC first |

**What the run does not test:** any GPU job (part 2), the OUTOMEM path, a wedge/reset, client death
via `mtClose` (a follow-up: `v3dasync-ping die` exits with a parked sibling thread), E1 memrefs.

---

## 13. The skeleton (part 1) — what exists and how to build it

`tools/gpu-lane/v3d-async/` (all `%LICENSE%`, Phoenix header; code taken from the old lane keeps its
attribution comment):

| File | Contents |
|---|---|
| `v3da_proto.h` | the wire protocol (§10), fence-page layout, memrefs, static asserts |
| `v3da_regs.h` | V3D 4.2 register map (old daemon's, plus INT_MSK/INT_SET/IDENT/CTnRA/MMU fault regs from Linux `v3d_regs.h` — hardware facts only) |
| `v3da.h` | server-internal types (clients, BOs, queues, jobs, waits) |
| `v3da_main.c` | args, self-detach (fork before any set-up, readiness pipe; `-f` = foreground; a stray `&` is ignored), dispatch threads, message decode, client open/close/HELLO, `mtGetAttr(atMode)` (path resolution asks every component; answered like `rpi4-fb`) |
| `v3da_param.c` | GET_PARAM (own file: the vendored DRM uapi's `sys/ioccom.h` shim clashes with `<sys/ioctl.h>`) |
| `v3da_hw.c` | vcmbox power-on with read-back, MMIO map, identity, MMU PT + scratch, `apply_core_regs`, interrupt masks, handler, poll, runtime IRQ switch, self-test |
| `v3da_sched.c` | queues, seqnos, fence page, event thread, NOP CPU job, parked waits (claim-once), syncobjs |
| `v3da_bo.c` | BO create/close/mmap/offset, pool, quarantine |
| `libv3da-client.{c,h}` | connect/HELLO, fence page map + fast path, BO, NOP, waits, syncobjs, debug ops |
| `v3dasync-ping.c` | the probe of §12 |
| `build.sh` | standalone build (below) |

Build (toolchain gcc against the tree sysroot, as `tools/serrprobe/README.md`; `libvcmbox.c` is
compiled from `sources/phoenix-rtos-devices/misc/rpi4-vcmbox/` because it is not in the sysroot):

```
tools/gpu-lane/v3d-async/build.sh              # -> tools/gpu-lane/v3d-async/out/{rpi4-v3d-async,v3dasync-ping,libv3da-client.a}
sudo cp tools/gpu-lane/v3d-async/out/rpi4-v3d-async tools/gpu-lane/v3d-async/out/v3dasync-ping \
    /srv/phoenix-rpi4-nfs-gcc16/bin/            # coordinator only
```

`-O2 -Wall -Wextra -Werror -std=gnu11`; writes only into its own `out/`.

---

## 14. Open questions and risks

| # | Item | Plan |
|---|---|---|
| R1 | Server crash wedges parked clients (E5 condition 2) — and so does an orderly `DBG_QUIT` with ≥ 2 dispatch threads, if another thread has just *received* a request (the §12 cycle uses `-r 1` for this reason) | kernel follow-up E5 §5.3 is an M1-gate dependency; `V3DA_WAIT=poll` meanwhile; part 2: quit stops the other dispatch threads (a "stop receiving" flag + a self-sent wake-up message each) before `exit` |
| R2 | New wedges from queue overlap (§5 caveat) | part 2 brings up rendering with `--serialize`, then overlap; E10 A/B for fix-A under async |
| R3 | GPU DMA after server death onto reclaimed pages (C1 class) | never exit busy; M3 device-owner reference (E1 §7) |
| R4 | `INT_SET` may not latch → the IRQ self-test is inconclusive | then the IRQ path is first proven by a real FLDONE in part 2 |
| R5 | Reset values of `INT_MSK_*` unknown | set explicitly before registering (§3.3) |
| R6 | condWait timeout granularity may make slow-path waits and the 125 ms watchdog coarse | measured by `nopwait` / `timeout` |
| R7 | Mesa's `in_sync_bcl`/`in_sync_rcl` choices decide how much bin/render overlap one client gets | observe in part 2 before tuning |
| R8 | MULTI_SYNC and CPU-queue jobs (v3dv) are net-new, not ports (research §3.7) | after the GL gate |
| R9 | `MAP_FIXED` extent stitching (M1b) untested | a probe before M1b |
| R10 | The E1 §6 kernel bug (contiguous free empties the object tree) affects today's lane on every BO free | the pool avoids it here; the one-hunk kernel fix is its own gated commit |
| Q1 | Does `mtOpen`'s positive return → fd `oid.id` hold for a devfs node served by a user server, and does the kernel send `mtClose` with that id at process exit? | `connect` proves the first; a `die` test (follow-up) the second |
| Q2 | Is 1 MiB the right overflow chunk? | A/B in part 2 (OUTOMEM counts, bin time) |
| Q3 | Should the server itself flip (FLIP after fence) or should the client wait then flip? | server-side first (removes a CPU wait); compare in part 2 |
| R11 | Part 2 has never run: the smoke CLs are generated from Mesa's packers but untested on this GPU; a wrong packet reads as a wedge | P2-A grades each generator separately (CL / TFU / CSD); the server's recovery is validated by the same wedge |
| R12 | Implicit sync on a BO covers only the adapter's own submits (M1: one GL client) | two GL clients need the server-side `BO_WAIT` for every wait (M3 / libdrm-phoenix) |
| R13 | Poll-mode completion latency (R6) is paid twice per CL submit | fps only in IRQ mode (§17) |
| R14 | `PTB_BPOA/BPOS` written by the handler while a second, directly-granted chunk is pending — assumed impossible (OUTOMEM left latched by the drain; the handler serves at most one staged chunk per OUTOMEM) | `starved` counter + the `cl-burst`/timedemo runs; the old lane never overlapped jobs so this is new ground |
| R15 | No post-wedge CL hexdump yet (old `wedge_cl_dump`) | port it if a wedge needs attribution |
| R16 | Overflow pool exhaustion fails the bin instead of growing the pool | old lane never saw 32 MiB exhausted; grow only if `starved` says so |
| R17 | Two `srv.lock` deviations from §2 ("heavy work outside the lock"): `BO_CREATE` zeroes the BO under the lock (an 8 MB uncached RT ≈ tens of ms, during which no job completes and no wait is answered), and `pan()` does a vcmbox IPC under the lock | visible as `cstat wait_us` spikes; move both out of the lock (claim → unlock → work → relock) once measured |
| R18 | The smoke generators' own uncertainties: the TFU copy moves the data typed as R32F (Mesa's exact-copy trick; the pattern keeps every word a normal float); the per-tile generic list sits inside the RCL buffer after `rcl_end` (Mesa uses a separate BO) | a `tfu-smoke`/`cl-smoke` failure with a clean fence is read against these first |

---

## 15. Part 2 — what is implemented (compiles; not yet run on hardware)

Files (all in [`tools/gpu-lane/v3d-async/`](../../tools/gpu-lane/v3d-async/), `%LICENSE%` unless
noted):

| File | Part 2 contents |
|---|---|
| `v3da_jobs.c` (new) | submission, per-queue FIFOs, the scheduler (serial / pipeline), kick + completion sequences of BIN/RENDER/TFU/CSD, binner-overflow chunks, watchdog, wedge → reset, `SCANOUT_INFO`/`FLIP`, `DBG_SET_MODE`, `DBG_QSTATS`, the periodic `V3DA srv qstat` line |
| `v3da_sched.c` | now only fence page, parked waits, syncobjs (+ `SYNCOBJ_IMPORT`) and the event-thread loop that drives `v3da_jobs.c` |
| `v3da_hw.c` | + `v3da_hw_reset` (mask → GMP drain → PM_V3DRSTN reset + vcmbox power-on with clock read-back → `apply_core_regs` → TLB → masks back), `v3da_hw_drain`, the handler counts OUTOMEMs it could not serve (`ovf_missed`) |
| `v3da_bo.c` | generation-tagged handles, in-flight pins, scanout BOs, the premapped 32 MiB overflow pool |
| `libv3da-client.{c,h}` | + `v3da_submit` (page-aligned, whole-page `i.data`), `v3da_fence_error`, `v3da_syncobj_import`, `v3da_scanout_info`, `v3da_flip`, `v3da_dbg_set_mode/qstats` |
| `v3da_winsys.c` (new) | **the Mesa adapter** (§15.5) |
| `v3da_clgen.{c,h}` (new) | CPU-side generators for the smoke jobs; the 26 `V3D42_*_pack()` functions are copied verbatim from Mesa's generated MIT `v3d_packet_v42_pack.h` (checked line by line), packet sequences transcribed from `v3dx_draw.c` / `v3dx_rcl.c` / `v3d_job.c` / `v3dx_tfu.c`, the CSD kernel from `tools/v3d-driver-port/csd_probe.c` (HW-proven through the old winsys) |
| `v3dasync-ping.c` | + `cl-smoke`, `tfu-smoke`, `csd-smoke`, `cl-burst`, `gpu`, `mode-serial`, `mode-pipeline`, `qstats`, `qstats-reset` |
| `build.sh` | output dir `--out <dir>` / `$V3DA_OUT` (default `out/`); part 2 builds into `out-p2/` (git-ignored); project target flags (`-mcpu=cortex-a72 -mstrict-align -ffunction-sections -fdata-sections`) |
| `build-quakespasm-v3da.sh` (new, BSD-3) | the clone relink (§15.6) |

**Binary compatibility — read before staging.** Part 2 bumped `V3DA_PROTO_VERSION` to 2. A part-1
ping (`out/`) against a part-2 server (or the reverse) fails at HELLO with `-EPROTO`, which the §12
table would misread as a broken ioctl path. (a) Never mix binaries across `out/`, `out-p2/` and
`artifacts/quakespasm-v3da/v3da/`. (b) Never rebuild `out/` from the current tree for the §12 cycle:
the shared sources are already part 2, so a rebuild there silently turns the pre-registered part-1
run into a part-2 run. The part-1 binaries in `out/` (built 19:02) are the ones §12 grades.

### 15.1 Job path as built

`SUBMIT_*` (dispatch thread, `srv.lock`): validate the flat buffer → every in-/out-syncobj exists
(`-ENOENT`) → **pin** every listed BO (`inflight++`, all or none) → create the job(s) (CL = a bin
job + a render job linked both ways) → resolve each in-syncobj to a fence **now** (DRM semantics;
an empty or already-signalled syncobj adds nothing — Mesa's first frame waits on a freshly created
`out_sync`) → assign seqnos → mark each BO's last use = the last job's fence → replace every
out-syncobj's fence → push to the client's FIFOs → `v3da_sched_run()` → reply with both fences.
The server copies the BO list before replying (the `i.data` window dies with the reply).

Readiness: in-fences signalled (fence page) and, for a render, its bin job complete. A render whose
bin **failed** is completed with an error without ever being kicked (the old lane skipped the render
too). Completion (event thread only): the job's BO pins drop (a closed BO then enters quarantine),
overflow chunks move from bin(N) to render(N) and are freed when render(N) completes, the fence
page advances, parked waits are answered, `v3da_sched_run()` runs. A job completed without ever
reaching the hardware bumps `hw_submitted` first, so `hw_completed` can never run ahead of it (that
would let a quarantined BO pass while an older job still runs).

**Modes** (runtime, `-m` or `DBG_SET_MODE`; both switches are safe at any time):
* `serial` (**default**): at most one hardware job across BIN/RENDER/TFU/CSD; among ready FIFO heads
  the **globally oldest** (a global submission counter) — i.e. exactly the old synchronous lane's
  order (bin N, render N, TFU, bin N+1 …), only the completion is asynchronous.
* `pipeline`: every hardware queue runs one job concurrently, round-robin over clients, so bin(N+1)
  overlaps render(N) and TFU/CSD run beside CL work (Linux `v3d_sched` shape).

Mesa's gallium driver sets only `in_sync_rcl = out_sync` (the previous job) and no `in_sync_bcl`
(`v3d_job.c:657-690`), so in pipeline mode bin(N+1) of one client really can start while render(N)
runs (R7 answered by reading; to be observed).

### 15.2 Wire format decisions that differ from the design text

* **Own job descriptors** (`v3da_cl_desc_t`, `v3da_tfu_desc_t`, `v3da_csd_desc_t`) instead of the
  DRM structs: the wire ABI no longer depends on the DRM uapi snapshot; the adapter copies field by
  field. `V3DA_PROTO_VERSION` is 2.
* **Handles are generation-tagged**, `(gen << 13) | (slot + 1)`, never reused while the server lives
  (§9.2 said "slot + 1, as today" — but the in-process winsys itself moved to monotonic handles after
  a recycled handle put texture data into a control list, `v3d_phoenix_winsys.c` ioc_create_bo). The
  old `v3d_bufmgr.c` import-by-number path still works (a handle is a number).
* **`SCANOUT_BO` is not an opcode**: a scanout BO is `BO_CREATE` with `V3DA_BO_SCANOUT` (DRM flag bit
  1, or the glue's one-shot `v3d_phoenix_set_next_scanout()`); the reply's `scanout` field says which
  firmware buffer backs its GPU pages.
* The **CACHE_CLEAN queue is not used**: `SUBMIT_CL_FLUSH_CACHE` (E2 step 16) runs inline in the
  render epilogue, and only with knob `V3DA_KNOB_CL_CACHE_CLEAN` (old default: off).

### 15.3 Per-job maintenance as built (E2 steps; default = every step of the old lane)

| Kick / completion | Sequence | Knob that drops a step (default off) |
|---|---|---|
| BIN kick | `dsb sy` (1, 3) → `SLCACTL=INVAL_ALL` (4) → MMUC flush + TLB clear (5) → waited L2T flush (6) → **fix-A** second waited flush (7) → stale-latch drain (8, see below) → `PTB_BPOS=0` → stage one overflow chunk → `CT0QMA/QMS`, `CT0QTS=ENABLE\|qts` → `CT0QBA/QEA` | `TLB_ON_CHANGE` (5), `NO_L2T_WAIT_NEW` (6), `NO_FIXA` (7) |
| RENDER kick | waited L2T flush + `SLCACTL` (11, the old hand-off, now the render prologue) → drain → `CT1QBA/QEA` (12) | `NO_HANDOFF_WAIT` (11) |
| FRDONE | wait-old + `L2TFLS\|FLM_CLEAN` **not** waited (15); TMUWCF + waited clean only for `FLUSH_CACHE` jobs with `CL_CACHE_CLEAN` (16) | `CL_CACHE_CLEAN` enables 16 |
| TFU kick / TFUC | `dsb` + TLB + `SLCACTL` + waited L2T flush; drain TFUC/TFUF; regs; `ICFG\|IOC` / TMUWCF (spun) + waited clean + `SLCACTL`; TFUF = error; BUSY-cleared-without-TFUC after 250 ms = done (old fallback) | `TLB_ON_CHANGE` |
| CSD kick / CSDDONE | `dsb` + `SLCACTL` + TLB + waited L2T flush; bounded wait for "no CURRENT dispatch" (stuck → reset); drain CSDDONE; CFG1..6, CFG0 / TMUWCF + waited clean + `dsb` | `TLB_ON_CHANGE` |
| every 125 ms of a render | ack latched QPU interrupt bits, never FRDONE (13) | — |

**Step 8 replaced by a drain.** The old `INT_CLR = FLDONE|FRDONE|QPU` before the bin kick would, in
pipeline mode, erase render(N-1)'s FRDONE. `v3da_hw_drain()` instead folds every pending status bit
**except OUTOMEM** into the event words and W1Cs it, then the kick drops only its own queue's done
bits from the event words (the queue is idle, so they can only be stale). OUTOMEM stays latched so
exactly one path (handler or poll) hands out memory — two grants for one stall would re-point a
binner already running on the first chunk.

### 15.4 OUTOMEM, watchdog, reset

* **Overflow**: 32 MiB premapped at init (as the old daemon), cut into `-c` KiB chunks (default
  1 MiB → 32 chunks). One chunk is **staged** for the handler at every bin kick and re-staged after
  each hand-out; on OUTOMEM the handler (IRQ mode) or the poll path writes `PTB_BPOA/BPOS` from the
  staged slot. An OUTOMEM that finds nothing staged is counted (`ovf_missed`) and granted directly by
  the event thread from the pool; if the pool is empty the binner stays stalled until a render frees
  chunks (legitimate: the watchdog does not count it while a render runs). Empty pool **and** no
  render running → `binner overflow pool EXHAUSTED` → the bin fails through the wedge path. Pool
  growth is not implemented (old lane: one 32 MiB grant, never observed exhausted).
* **Watchdog** (event thread, every 125 ms per hardware job): progress = `CTnCA`/`CTnRA` (bin,
  render), `CSD_CURRENT_CFG4` (CSD); no progress for `-w` ms (default 500) or 10 s total → **wedge**:
  the old lane's one-line dumps (`V3DA srv BIN|RENDER|TFU|CSD TIMEOUT …`, `V3DA srv DBG fdbg…`),
  `v3da_hw_reset`, `reset_gen++`, every job on the hardware completes **with error** (the reset killed
  all of them; a bin's render follows as a failed dependency), queued jobs stay, then `V3DA srv GPU
  wedged (<why>) - true reset rc=… N job(s) failed, drops=…`. The frame is dropped, not retried (the
  wedge is data-dependent, old lane). The old lane's post-wedge CL hexdump (`wedge_cl_dump`) is not
  carried yet.

### 15.5 The Mesa adapter `v3da_winsys.c`

It replaces the three in-process members of `libv3d-phoenix.a` — `v3d_phoenix_winsys.o`,
`v3d_phoenix_power.o`, `v3d_libdrm_shim.o` — and exports every symbol the rest of the archive,
`libGL-phoenix.a`, libSDL2 and the SDL glue reference (checked with `nm` against the archive):
`phoenix_v3d_ioctl`, the nine `drm*` functions of the shim, `v3d_phoenix_scanout_init/_active/
_double/_nbuf`, `_set_next_scanout`, `_peek_next_scanout`, `_flip`, `_flip_count`, `_scanout_readback`,
`_set_scanout`, `_harness_reset`, `_last_bin_crc`, `_powerOn/_reset/_fb_flip/_fb_virtual_height/
_logColdState` (power is the server's: these are inert or forward).

Three traps that would pass every link-time check and fail on the Pi:
1. **`WAIT_BO`** answers "not yet" as `errno = ETIME; return -1` (Mesa's `v3d_wait_bo_ioctl` decodes
   only `-1`/`errno`; `v3d_bo_wait` **aborts** on anything but `-ETIME`), and really waits:
   `v3d_bo_map()` waits (infinite) before every CPU access, and the BO cache probes with timeout 0.
   Fast path: the adapter records each BO's last-use fence from its own submits and answers from the
   fence page with no IPC; a BO it did not create goes to the server's `BO_WAIT`.
2. **`drmSyncobjWait` takes an absolute `CLOCK_MONOTONIC` deadline** (`os_time_get_absolute_timeout`,
   `INT64_MAX` = forever); converted to relative. The adapter mirrors its own syncobjs' fences, so
   `glFinish` / context-destroy waits are fence-page loads (plus one bounded server wait if not done).
3. **`glFinish` needs a fence fd.** The Phoenix Mesa patch in `v3d_pipe_flush` turns an exported fd
   of `-1` into a NULL fence and `st_finish` then waits for nothing — harmless with a synchronous
   submit, a torn frame with an asynchronous one. `drmSyncobjExportSyncFile` returns a **real**
   descriptor (a `dup` of the device fd; the kernel refcounts dups, `posix_fileDeref`, so Mesa's
   `close()` never closes the device) mapped to a snapshot of the syncobj's fence;
   `drmSyncobjImportSyncFile` attaches that fence to a syncobj in the server (`SYNCOBJ_IMPORT`).

Present: `v3d_phoenix_scanout_init()` → `SCANOUT_INFO` (the server asks the firmware for the granted
virtual height through vcmbox and derives 1–3 buffers); `v3d_phoenix_flip(k)` → `FLIP(k, after =
the last render fence this client submitted)`: the **server** pans through `/dev/vcmbox`
(`SET_VIRTUAL_OFFSET`) as soon as that fence passes — the old `v3d_phoenix_fb_flip` drove the raw
mailbox FIFO. The glue's own `glFinish` before the flip stays (Q3 is then measurable by removing it
in a later clone). The flip also prints the in-process winsys's **identical** `v3d-winsys: flipstat …`
line (every existing grader reads it) plus `v3da-winsys: cstat t= fr= cl= tfu= csd= create=
wait_us= ipc_waits=` — submits, BO creates and the client's time blocked in GPU waits per window.

M1 limits: implicit sync on a BO covers this process's submits only (one GL client per server in M1);
no GEM_OPEN/FLINK/PRIME; MULTI_SYNC is flattened (≤ 16 each), other extensions (CPU queue) are
refused with `EINVAL`.

### 15.6 The clone relink — `build-quakespasm-v3da.sh` (done; the clone links)

1. `build.sh --out artifacts/quakespasm-v3da/v3da` (server, ping, adapter objects);
2. copy `tools/.gpu-libs/libv3d-phoenix.a`; in the copy `ar d` the three winsys members and `ar r`
   `v3da_winsys.o` + `libv3da-client.o` (411 → 410 members, checked);
3. re-run the port's final link **exactly as recorded** in
   `.buildroot/_build/<target>/port-sources/quakespasm-0.97.0/build.log` (the one `+ aarch64-phoenix-gcc
   … -o …/prog//quakespasm` line), with the archive path and `-o` substituted.

Proofs it prints (2026-09-26 19:40): **control relink with the untouched archive == shipped
`prog/quakespasm`, byte-identical**; the clone defines `phoenix_v3d_ioctl`, `v3da_connect`,
`drmSyncobjExportSyncFile`, carries none of `winsys_init`/`boPool_take`/`mboxProp`/…, has no undefined
symbols, carries the adapter banner and not the winsys strings; the shared inputs are unchanged.
Output: `artifacts/quakespasm-v3da/quakespasm-v3da.stripped` (18.56 MB vs shipped 18.58 MB) → stage as
`/usr/bin/quakespasm-v3da`, with the matching server `artifacts/quakespasm-v3da/v3da/rpi4-v3d-async`.
No launcher is needed (the quakespasm glue finds the staged data itself). The adapter keeps its
tables in `.bss` (an initialised struct first put ~600 KiB into `.data` of the game).

---

## 16. Pre-registered Pi tests for part 2

Prerequisite: the part-1 cycle (§12) has run; its `irqtest` verdict decides whether part 2 runs in
IRQ mode (`-i`) or poll mode. Every cycle: **no game and no X on the old lane in the same boot**
(single-owner rule, §1.2), server started first, `-r 1` (R1). Binaries from `out-p2/` (or the
identical ones in `artifacts/quakespasm-v3da/v3da/`), staged by the coordinator.

### 16.1 Cycle P2-A — GPU smoke, serial then pipeline (one netboot cycle)

```
./scripts/test-cycle-psh-interact.sh --label m1p2-smoke --inter-cmd-secs 8 --idle-secs 20 -- \
    "/bin/rpi4-v3d-async -r 1 -m serial" \
    "/bin/v3dasync-ping all" \
    "/bin/v3dasync-ping gpu" \
    "/bin/v3dasync-ping irq-on" \
    "/bin/v3dasync-ping gpu" \
    "/bin/v3dasync-ping qstats-reset" \
    "/bin/v3dasync-ping mode-pipeline" \
    "/bin/v3dasync-ping gpu" \
    "/bin/v3dasync-ping stats" \
    "/bin/v3dasync-ping quit"
```
(Bash `timeout: 600000`.) If §12 showed the IRQ path broken, drop `irq-on` (the rest still grades).

| Line | Predicted | If instead… |
|---|---|---|
| `V3DA srv ready … mode=serial knobs=0x00 ovf=32x1024KiB wedge_ms=500 proto=2` | once | `ovf=0x…` → the 32 MiB contiguous pool failed (`fence page / overflow pool allocation failed`) |
| `V3DAPING all` lines | as §12 (`proto=2`) | part-1 regression from the part-2 changes |
| `cl-smoke n=0..2` | `wait_rc=0 fence_err=0 bad_px=0/4096 bo_wait=0 ok=1`, `us` ≲ 1000 (poll) | `wait_rc=-110` + `V3DA srv BIN TIMEOUT` → the BCL or CT0 set-up is wrong (the dump's `ct0ca` vs `[bcl]` says where); `RENDER TIMEOUT` → RCL/tile lists; `bad_px=4096 first_bad=0xdeadbeef` with `ok` fence → the store never wrote (RT address / store packet); `bad_px` partial → tiling/clipping; any `MMU fault` line → a VA not mapped (the line names the address). **A wedge here that the server survives** (next test still runs, `resets=1`) is itself the validation of §15.4 |
| `tfu-smoke` | `rc=0 fence_err=0 bad_px=0/256 ok=1` | `fence_err=1` + `TFU FAIL` → descriptor fields; `bad_px` > 0 with the fence ok → the layout formula (the generator's address model, not the server) |
| `csd-smoke` | `out0=0xc0de1234 others_written=0 ok=1` | `out0=0xeeeeeeee` → the dispatch never stored (cfg/uniforms/TMU clean); `CSD TIMEOUT` → the unit never raised CSDDONE |
| `cl-burst` | `jobs=8/8 wait_fail=0 fence_err=0 bad_px=0 ok=1` | `bad_px` > 0 → queued jobs corrupt each other (BO pinning / overflow attribution) |
| `qstats` (serial, 1st `gpu`) | bin/render `jobs` = 11 each (3 `cl-smoke` + 8 `cl-burst`; the 2nd `gpu` reads cumulative 22 — the reset comes after it), tfu/csd 1 each, `errors=0`, `overlap_us=0`, `ovf_free=32/32` (32 = the default `-c 1024`) | `overlap_us>0` in serial → the SERIAL gate leaks; `ovf_free<32` → a chunk leak |
| 2nd `gpu` (IRQ) | as the 1st; `us` per job lower than in poll mode | a hang/timeout only in IRQ mode → a completion lost between handler and event thread |
| `mode` | `rc=0 want=pipeline mode=pipeline` | — |
| 3rd `gpu` (pipeline) | all `ok=1`; `qstats … overlap_us` **> 0** after `cl-burst` (bin(N+1) ∥ render(N)) | `overlap_us=0` → the pipeline never overlapped (Mesa-shaped deps absent here: each clear is independent, so overlap must appear); any wedge only in pipeline mode → R2 (concurrent L2T flush / TLB clear): keep `serial` as the default, A/B the knobs |
| `stats` / `quit` | `parked=0 to_kernel=0`, `quit rc=0 inflight=0` | `inflight>0` → a job never completed |
| `V3DA srv qstat …` lines | one per 5 s while jobs ran, `err=0 wedges=0` | — |
| kernel | 0 EL1 faults | an EL1 fault → addr2line the PC first |

### 16.2 Cycles P2-B/C — `quakespasm-v3da +timedemo demo1` against the shipped `quakespasm`

Two GPU owners cannot share a boot, so the arms are **consecutive cycles**, interleaved
old/new/old/new/old/new (3 each), same image, same staged data, each a fresh boot:

```
# old lane (shipped binary, in-process winsys)
./scripts/test-cycle-psh-interact.sh --label m1p2-qs-old --inter-cmd-secs 8 --idle-secs 60 \
    --ready-line 'frames .* seconds .* fps' --ready-extra-secs 5 -- \
    "/usr/bin/quakespasm +timedemo demo1"
# new lane (clone + server; serial mode first, IRQ mode if §12/P2-A proved it)
./scripts/test-cycle-psh-interact.sh --label m1p2-qs-v3da --inter-cmd-secs 8 --idle-secs 60 \
    --ready-line 'frames .* seconds .* fps' --ready-extra-secs 5 -- \
    "/bin/rpi4-v3d-async -r 1 -m serial -i" \
    "/usr/bin/quakespasm-v3da +timedemo demo1"
```
(Bash `timeout: 600000`.) One timedemo per boot: QuakeSpasm returns to its console after a timedemo
and does not exit. After three serial pairs, three more `quakespasm-v3da` cycles with `-m pipeline`.

| Line | Predicted | If instead… |
|---|---|---|
| `v3da-winsys: connected to rpi4-v3d-async …` | once, then `scanout init … -> 3 buffer(s) page-flip`, two/three `RT scanout bufN` lines, `phxgl: scanout FBO(s) … resolve=1 double=1` | `unavailable (rc=…)` → the server was not up (its `ready` line?); `resolve=0` → scanout BOs not granted (server `scanout` line) |
| `N frames X seconds Y fps` (QuakeSpasm) | printed by both arms | missing in the new arm with no wedge line → a wait never returned (check `V3DA srv qstat` still advancing) |
| `v3d-winsys: flipstat …` | both arms | — |
| `v3da-winsys: cstat … wait_us=` | per window; `ipc_waits` ≪ frames × submits | `ipc_waits` ≈ every WAIT_BO → the BO fast path does not hit |
| server | `wedges=0`, `err=0`, `starved` small | wedges only in the new arm → R2 or a cache-step difference |
| HDMI tick snapshots | demo1 visibly rendering, same look as the old arm | torn / flickering frames → flip before render completion (fence gate) — compare against `+map start` reference MAE as in the Quake notes |

**Not predicted:** the fps ratio. The design gate (§11.2) is new ≥ old − 3 % in serial mode — serial
is the old order plus IPC and event-thread latency, so parity is the expectation, not a win; the
pipeline arm's gain is bounded by E2's U2 (bin ∥ render) and is what M1 exists to measure.

## 17. The numbers that prove M1

| Number | Source | M1 verdict |
|---|---|---|
| timedemo fps, old vs `-m serial` vs `-m pipeline` | QuakeSpasm's `frames … fps` line, 3 boots per arm | serial ≥ old − 3 % (no regression); pipeline reported as a ratio vs old |
| per-queue GPU busy | `V3DA srv qstat … bin=n/ms render=n/ms` (Σ kick→done) over the timedemo window | bin + render busy vs wall = the GPU share E2's G measures on the old lane |
| overlap | `qstat … busy= overlap=` (≥ 1 and ≥ 2 hardware queues busy) | pipeline: `overlap/busy` > 0 is the evidence that CT0 ∥ CT1 happened; serial: must be 0 |
| client blocked time | `v3da-winsys: cstat … wait_us` per 5 s window vs the window length | the CPU∥GPU overlap the async submit bought (U1): serial mode still blocks in glFinish before every flip |
| correctness | 0 wedges, 0 EL1 faults, `ovf_free` back to total after the run, same HDMI look | any wedge only on the new lane is a finding to explain before any fps number counts |

BO-cache caveat: on the old lane Mesa's BO cache probe `v3d_bo_wait(bo, 0)` never saw a busy BO; on
the new lane it can, so `v3d_bo_from_cache` misses more often → more `CREATE_BO` round trips, each
with a server-side memset of the BO under `srv.lock` (R17). A fps dip can come from that path rather
than from IPC or event latency; `cstat … create=` counts the creates per window to tell them apart.

Poll mode caveat: in poll mode each completion is noticed at the next event-thread poll (`-p`,
default 200 µs, subject to the kernel's `condWait` timeout granularity — R6); a CL submit is two
completions. **fps comparisons use IRQ mode**; poll mode is a correctness fallback.


## Result

*(to be filled after the §12 cycle)*

## Result — part-1 ping (cycle `m1-v3da-ping`, build 8, 2026-09-26 19:55): **PASS, 0 faults**

- Server detached (`V3DA srv detached pid=26`), powered V3D through `/dev/vcmbox`
  (`grafx=0x1000->0x1040`, `asb_m/asb_s=ok`), identity `core0=0x04443356 … match=1`, clock
  500 MHz (measured 499.99 MHz).
- Poll mode: connect, info, fence page (heartbeat moving), BO create → zeroed → checksum → close →
  quarantine passed → pooled (never returned to the kernel), stale handle `-22`, NOP fence wait
  19 ms, timeout `-110` at 100 ms, `irqtest ok=1` (status seen, handler not yet registered).
- **IRQ mode (`irq-on`): the V3D interrupt reaches a userspace server** — `irqtest mode=irq
  handler_delta=1`, `irq_count=1`, no storm; same checks pass again. Masks after unmask:
  `core_msk=0x00ff0058 hub_msk=0x00000005`.
- `ipc_waits=87`, `resets=0`, `parked_max=4`. Four `V3DAPING RESULT … verdict=PASS` lines.
⇒ Part 2 (real jobs) can use IRQ mode; P2-A queued.
