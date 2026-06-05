# kernel-core — RPi4 upstream-readiness review

- **Area:** kernel-core
- **Repo:** phoenix-rtos-kernel (base `57b30411` → head `6cdf217e`)
- **Files reviewed (changed hunks only):** main.c, proc/name.c, proc/threads.c,
  proc/msg.c, syscalls.c, syspage.c, log/log.c, vm/map.c, vm/vm.c, posix/posix.c,
  usrv.c, lib/lib.h, README.md
- **Scope:** arch-independent kernel files shared by all targets. Only RPi4 hunks
  reviewed; pre-existing upstream logic not flagged.

These files are compiled into **every** Phoenix target (ia32, armv7, sparcv8leon,
riscv64, zynqmp, …). The dominant theme below is that RPi4 bring-up diagnostics and
two SMP-observability blocks were added to this shared code **without target gating**,
so they affect (and in one case fail to link on) non-aarch64 builds.

---

## Findings (ordered by severity)

### 1. usrv.c:75-77 · **BUG** · sev=high — ungated raw-VA UART write in arch-shared code
`_usrv_init()` writes `'U'` to a hardcoded VA `0xffffffffffe00000` with **no `#ifdef`**.
This file is built for every arch. On every non-aarch64 target this VA is meaningless
(and on a 32-bit target the 64-bit constant truncates); the write faults or corrupts an
unrelated location at boot.
WHY: this is the RPi4 early-UART probe address (TD-05 class). It is a Pi4-only MMIO alias
leaking into shared code, and there is no `TODO(TD-xx)` marker.
REC: delete the `uart` declaration and the `*uart = 'U';` write entirely (it is a dead
boot marker — `_log_init()` is called immediately after and emits its own trace). If a
probe is genuinely wanted, gate it behind `__TARGET_AARCH64A72/A53` like the main.c
include already is.
**APPLY-SAFE** (dead diagnostic removal).

### 2. log/log.c:488-510 · **BUG** · sev=high — ungated raw-VA UART probes in `_log_init`
`_log_init()` writes `'l','L','m','k','E'` to fixed VAs `0xffffffffffe00000` /
`...e00018`, each with a busy-wait on the PL011 flag register, **no `#ifdef`**. Same
cross-arch fault/corruption hazard as #1; every non-aarch64 target runs this at boot.
WHY: TD-05-class early-UART markers, no `TODO(TD-xx)` marker, hardcoded Pi4 PL011 alias.
REC: delete the `uart`/`uartfr` declarations, all five `*uart = …` writes, and their
`while ((*uartfr & 0x20u) …)` spin-waits. `_log_init` should reduce back to the
`hal_memset` + `proc_lockInit` + `enabled = 1` body.
**APPLY-SAFE** (dead diagnostic removal).

### 3. main.c:186-264 + main.c:295-312 · **BUG** · sev=high — SMP observability blocks gated on `NUM_CPUS` but reference aarch64-only externs
The `#if NUM_CPUS != 1` blocks reference `threads_smpTickCount`,
`hal_smpInterruptsInitPerCpuCount`, `hal_smpInterruptsEnabledPpiCount`,
`hal_smpTimerInitPerCpuCount`, `hal_smpPrimaryReady`, `hal_smpFirstIntervalUs`.
Verified: every one of those `hal_smp*` symbols is defined **only** under
`hal/aarch64/` (interrupts_gicv2.c, gtimer_timer.c). The gate is `NUM_CPUS != 1`, not
the target. And this is a **present-tense link break, not a latent hazard**: three
shipping non-aarch64 targets build `main.c` with `NUM_CPUS != 1` —
`sparcv8leon/gaisler/gr740/config.h` (`NUM_CPUS 4U`),
`armv7a/zynq7000/config.h` (`NUM_CPUS 2`), and
`sparcv8leon/gaisler/gr712rc/config.h` (`NUM_CPUS 2U`) — all of which reference these
undefined externs and **fail to link**. (zynqmp lives *under* `hal/aarch64/` so it has
the externs and is not a counterexample.)
WHY: the gate condition doesn't match the symbols' availability. Note main.c:18-20
already arch-gates the `hal/aarch64/aarch64.h` include with
`__TARGET_AARCH64A72/A53` — the SMP blocks use a different, wrong gate.
REC: wrap both blocks in `#if (defined(__TARGET_AARCH64A72) || defined(__TARGET_AARCH64A53)) && (NUM_CPUS != 1)`,
or better, move this SMP-bringup diagnostic out of shared main.c entirely (it is pure
Phase-D observability and the file's own comment says it can be "removed entirely" once
Phase D closes). Referent: the existing `__TARGET_AARCH64A72`/`A53` gate at main.c:18.
(Note: `hal/aarch64/generic/config.h` defines `NUM_CPUS 4U`, so these blocks are LIVE on
the rpi4 build even though the project notes describe single-core bring-up — confirm the
effective build-time `NUM_CPUS` before assuming the blocks are dead on Pi4.)
**NEEDS-HW** (the `hal_smpPrimaryReady`/`hal_smpFirstIntervalUs` writes are live SMP
control flow on aarch64 multi-core; removing/regating them affects secondary-core
release timing — document, don't blind-apply the regate to the live writes).

### 4. main.c:69-124 · **ROLLBACK/COMMENT** · sev=med — TD-13 spawn-cap is correctly marked but ungated and fails silently
The 32-iteration spawn-loop cap carries a proper `TODO(TD-13-spawn-cap)` marker and is
documented in `TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` (status ACTIVE HACK). It masks an
un-root-caused Pi4 cache-coherency bug (`psh->next` not returning to the list head).
Two upstream concerns remain: (a) it is **ungated** in shared main.c, so every arch pays
the iteration counter and a >32-prog syspage **silently truncates** the program list
with only a `lib_printf`; (b) the underlying terminator
`(prog = prog->next) != syspage_progList()` is left in place, so on a correct arch the
cap is pure overhead.
WHY: interim-acceptable for bring-up (documented debt), but as written it is a
silent-data-loss footgun for any future config and for non-Pi4 arches.
REC: keep the marker. For upstream readiness: target-gate the cap
(`__TARGET_AARCH64A72/A53`) so other arches use the natural terminator, and make the cap
**fail loudly** (it already prints, but the bound should arguably be a build-time
`#error`-guarded constant tied to the syspage prog count rather than a magic 32). Do not
remove until TD-13 root cause is found.
**NEEDS-HW** (changing the loop termination affects the Pi4 boot path that the cap exists
to rescue — document only).

### 5. proc/name.c:82-89, 103-106 + all callsites · **ROLLBACK** · sev=med — dead no-op trace stubs and dead `traceDevfs` plumbing
`name_traceRegister()` and `name_traceDevfs()` are no-op stubs (`(void)arg;`). They are
called at name.c:128, 168 (`name_traceRegister`) and 266, 280, 357, 390, 393
(`name_traceDevfs`), plus the `int traceDevfs` local (name.c:230) and its guarding
`if (traceDevfs != 0)` blocks, exist **only** to call those no-ops. This is leftover
TD-14-probe-strip scaffolding (the probes were stripped to no-ops but the call sites and
local were left behind).
WHY: dead code that obscures the live logic; the misleading "trace" naming actively
confused a prior cleanup pass (see the cautionary comment at name.c:91-96).
REC: delete `name_traceRegister`, `name_traceDevfs`, the `traceDevfs` local, and every
`if (traceDevfs != 0) { name_traceDevfs(...); }` block. **Do NOT** touch
`name_traceDevfsLookup` (it is live — see finding 6) or `name_traceIs` (live helper used
by the real devfs registration logic at name.c:161, 202).
**APPLY-SAFE** (no-op/dead removal; the no-ops compile to nothing already, so behavior is
unchanged — gate on a `--scope core` build + boot smoke).

### 6. proc/name.c:97-100, 230, 255-270, 472-478 · **COMMENT/STYLE** · sev=med — live TD-14-devfs-direct workaround has a misleading name and no `TODO(TD-14)` marker
The `devfsRegistered`/`devfsOid` fields + the `proc_portLookup` short-circuit (name.c:255-270)
ARE the live `TD-14-devfs-direct` workaround (status ACTIVE WORKAROUND in the TD doc), and
the predicate that drives it is named `name_traceDevfsLookup()` — a name that screams
"diagnostic" but is actually load-bearing fast-path logic (the comment at name.c:91-96
documents that a previous cleanup broke boot by stubbing it to `return 0`).
WHY: a maintainer cannot grep `TODO(TD-14)` to this workaround; the name invites exactly
the deletion that already caused a regression. The TD doc's own marker-grep
(`grep "devfs_registered\|devfs direct"`) no longer matches the current identifiers.
REC: (a) add a `TODO(TD-14-devfs-direct):` comment at the `devfsRegistered` struct field
and at the short-circuit block; (b) rename `name_traceDevfsLookup` →
`name_isDevfsFastpath` (or similar) and update its now-accurate comment. Referent: the
existing `rootRegistered`/`rootOid` fast path in the same function (name.c:236-253) is the
canonical in-kernel namespace fast-path shape this mirrors.
**NEEDS-HW** (renaming live boot-critical code + the workaround itself touches the Pi4
cold-boot devfs race — document; the comment/marker addition alone is APPLY-SAFE).

### 7. vm/vm.c:43-64, vm/map.c:1621,1636,1639-1650, main.c:231-318 · **ROLLBACK** · sev=med — ungated `hal_consolePrint` boot-trace prints, no markers
`_vm_init` gained ~10 `hal_consolePrint(ATTR_USER, "vm: …")` step markers; `_map_init`
gained `"map: enter"`/`"map: pool link"`/`"map: zero free"` plus a raw `lib_printf` pool
dump; `main()` gained `"hi: vm-done"` … `"hi: reschedule-done"` markers. All in
arch-shared files, **no `TODO(TD-xx)`** marker, unconditional on every target.
WHY: TD-05-class boot-progress scaffolding. `ATTR_USER`/`hal_consolePrint` are generic so
these compile everywhere, but they are pure bring-up noise that will print on ia32/zynqmp/etc.
REC: delete the `hal_consolePrint(ATTR_USER, "vm: …"/"map: …"/"hi: …")` lines and the
`map: pool …` `lib_printf`. Keep the genuine pre-existing `vm: Initializing memory mapper`
and `proc: Initializing thread scheduler` banners (upstream). The `map.c` `nfree == 0`
guard (map.c:1639-1648) is a real defensive fix, NOT diagnostic — keep that.
**APPLY-SAFE** (diagnostic-print removal; the `nfree==0` guard stays).

### 8. proc/msg.c:31-32, 356-362, 370-377, 399-405, 442-449 · **ROLLBACK** · sev=med — `td14_lookupTrace` devfs IPC-timing probe, no `TODO` marker
`proc_send` gained a `td14_lookupTrace` counter in `msg_common` and a block that, for the
first 16 `mtLookup("devfs")` messages, timestamps the send and prints
`td14: send devfs …` via `hal_consolePrint`. Arch-shared, unconditional, no marker.
WHY: this is the TD-14 IPC-latency timing probe (kernel commit `60703368` per the TD doc)
that was supposed to be stripped in the TD-14-probe-strip pass but survived in `proc_send`.
It adds a struct field, a `hal/timer.h` include (msg.c:17), and per-lookup overhead on all
arches. The `int state = msg_rejected;` initialization change (msg.c:359) is a benign
correctness tidy and can stay.
REC: delete the `td14_lookupTrace` field, the `traceLookupDevfs`/`sawReceived`/`traceStart`/
`traceQueued`/`traceEnd`/`traceBuff` locals, the two trace blocks, and the now-unused
`#include "hal/timer.h"` if nothing else needs it. Keep `int state = msg_rejected;`.
**APPLY-SAFE** (diagnostic removal; verify `hal/timer.h` isn't otherwise required after).

### 9. proc/threads.c:698-717 · **ROLLBACK** · sev=low — `threads_smpTickCount[8]` defined + incremented unconditionally on all arches
The per-CPU tick counter is declared `volatile unsigned int threads_smpTickCount[8];`
(file scope, no gate) and incremented via `hal_cpuAtomicInc` on **every** timer tick on
**every** arch, even single-core/non-aarch64 builds where it is never read.
WHY: SMP Phase-D observability that only main.c's aarch64 SMP block consumes (finding 3).
The comment itself says it "can be promoted to a debug syscall (or removed entirely)".
REC: gate the array + the increment under the same condition as its only consumer
(`#if (defined(__TARGET_AARCH64A72)||defined(__TARGET_AARCH64A53)) && (NUM_CPUS != 1)`),
or remove with finding 3. `hal_cpuAtomicInc` exists on all arches so this currently
compiles — it is overhead, not a build break.
**APPLY-SAFE** if removed together with finding 3's diagnostic; **NEEDS-HW** if kept,
since the gate must match the live SMP path.

### 10. proc/threads.c (TIMER_WAKEUP_IRQ blocks) + `_threads_programWakeup` refactor · **ARCH/COMMENT** · sev=low — real SMP wakeup-coalescing logic, well-structured, but verify cross-arch neutrality
The extraction of `_threads_programWakeup` from `_threads_updateWakeup`, the
`#ifdef TIMER_WAKEUP_IRQ` remote-wakeup-IPI coalescing (threads.c:213-235), the
`threads_wakeupintr` handler, and the secondary-CPU CNTV re-arm in `threads_timeintr`
(threads.c:300-311) are genuine SMP timer logic, not diagnostics — correctly `#ifdef`-gated
on `TIMER_WAKEUP_IRQ` (an aarch64-defined symbol) and `hal_cpuGetCount() > 1`.
WHY: this is legitimate functionality, flagged only to confirm it was reviewed and is NOT
rollback. The gating is correct (referent: existing `#ifdef PENDSV_IRQ` handler pattern in
the same struct, threads.c:62-65). No regression to arches that don't define
`TIMER_WAKEUP_IRQ`.
REC: keep. Minor: the `threads_smpTickCount` increment inside `threads_timeintr` (finding 9)
is the only un-gated rider in this otherwise clean block; fix it there.
**NEEDS-HW** (SMP timer semantics — no change recommended, validation note only).

### 11. syspage.c:191-244 · **COMMENT** · sev=low — 64-entry relocation caps + NULL guards are TD-04-hack-1 residue, unmarked, while TD-04-hack-1 is "RESOLVED"
`syspage_init` gained: a `syspage == NULL` early return, a 64-iteration cap on both the
map-entry loop and the prog loop (with `original_entries` to fix the loop terminator), and
NULL guards on `prog->imaps`/`prog->dmaps` relocation. The TD doc lists these under
TD-04-hack-1 as the *resolution* ("restored prog-reloc loop with NULL guards + 64-iter
cap") — but TD-04-hack-1 is marked **RESOLVED** while these caps/guards are left permanent
and **unmarked** in source. The map-*entry* 64-cap in particular is not separately
documented.
WHY: a reader can't tell whether the 64-caps are permanent hardening or leftover debt; the
TD doc says RESOLVED but the source still carries the workaround shape.
REC: either (a) keep the caps as permanent defensive bounds and add a one-line comment
"defensive bound, see TD-04-hack-1 (resolved)" so they read as intentional, or (b) if the
underlying corruption is truly fixed, reconcile by documenting why the caps stay. The
NULL guards on `imaps`/`dmaps` are reasonable defensive code — keep. The `syspage == NULL`
guard is a clean correctness improvement — keep.
**APPLY-SAFE** (comment-only; do not change the cap/guard logic without HW).

### 12. syscalls.c:564-571, 825-851, 907-930 + posix/posix.c:600 · **STYLE** · sev=low — leftover probe-strip churn (no-op refactors, stray blank lines)
`syscalls_msgSend`/`syscalls_lookup` introduce an `int err` temp that is assigned then
immediately returned (`err = proc_send(...); return err;`) — no behavioral change vs the
original `return proc_send(...);`. `syscalls_phMutexCreate` merges two `vm_mapBelongs`
checks into one `||` and drops blank lines. posix.c:600 adds a stray blank line inside
`posix_open`'s lookup loop. syscalls.c:43 removes a blank line.
WHY: these are residue of the TD-13/TD-14 probe-strip (the `err` temps were probe insertion
points; the diff churn will make a maintainer ask "why is this here?"). Harmless but noise.
REC: revert the `err`-temp indirection back to `return proc_send(port, msg);` /
`return proc_portLookup(name, file, dev);`, drop the posix.c:600 and syscalls.c:43 blank-line
churn. The `vm_mapBelongs` `||` merge is a legitimate readability tidy — keep it.
**APPLY-SAFE** (pure formatting/no-op revert).

### 13. lib/lib.h:24-56 · **COMMENT** · sev=low — TD-13 `lib_atomic*` single-core fallback is correct and marked; confirm gate, refine comment
The `#if defined(__aarch64__) && (NUM_CPUS == 1)` spinlock-masked
`lib_atomicIncrement`/`Decrement` carries a clear `TODO(TD-13)` marker and matches the
validated single-core spinlock path (TD-11). Correctly scoped: only single-core aarch64.
Note the gate is `defined(__aarch64__)`, not `__TARGET_AARCH64A72/A53`, so it also swaps the
atomics impl for **single-core zynqmp-aarch64**, not just rpi4 — benign (interrupt-masking is
correct when there is no other CPU). All **non-aarch64** arches keep the `__atomic_*` builtins.
WHY: legitimate documented workaround, flagged to confirm review. No cross-arch regression
(the `#else` branch is the unchanged upstream path).
REC: keep as-is. Optional: the TD doc notes multicore aarch64 must revisit this — the marker
already says so. No action needed beyond the existing TODO.
**NEEDS-HW** (atomics correctness on Pi4 — no change recommended).

### 14. README.md:2-7 · **COMMENT** · sev=low — fork-warning banner appropriate now, must be dropped before actual upstream submission
The added "Fork warning: AI-generated changes … not fully reviewed/tested" block is correct
and honest for the current fork state.
WHY: appropriate for the readiness phase, but it is fork metadata that must not land in an
actual upstream PR to phoenix-rtos.
REC: keep for now; remove in the commit that opens an upstream PR.
**APPLY-SAFE** (doc-only; no action this phase).

---

## Summary

- **Counts:** 14 findings — BUG 3 (all high, all cross-arch), ROLLBACK 5 (med/low),
  COMMENT 4 (med/low), STYLE 1 (low), ARCH 1 (low). By severity: high 3, med 6, low 5.
- **Cross-cutting theme:** RPi4 bring-up diagnostics and one SMP-observability path were
  injected into arch-**shared** kernel files without target gating. Findings 1, 2, 7, 8, 9
  are TD-05-class diagnostic scaffolding lacking any `TODO(TD-xx)` marker. Three are concrete
  cross-arch regressions: **1 + 2** (raw-VA UART writes — unconditionally compiled and run on
  **every** non-aarch64 boot, certain), **3** (SMP-block link break, present-tense for three
  non-aarch64 `NUM_CPUS != 1` targets), **9** (unconditional per-tick overhead, all arches).
- **Single most important issue:** **finding 3** — the `#if NUM_CPUS != 1` SMP blocks in
  shared `main.c` reference `hal_smp*` externs defined **only** under `hal/aarch64/`. Verified
  present-tense link break: `gr740` (NUM_CPUS 4U), `zynq7000` (2) and `gr712rc` (2U) build
  `main.c` with `NUM_CPUS != 1` and fail to link. The gate must be the target macro (the file
  already uses `__TARGET_AARCH64A72/A53` for its aarch64.h include), not `NUM_CPUS`.
  Co-headline: **findings 1+2** (ungated raw-VA UART writes that fault on every non-aarch64
  boot — more certain than 3, since unconditionally executed).
- **Lock-nesting check (log mirror, requested):** SAFE from deadlock. `log_write` holds
  `log_common.lock` across the per-byte `hal_consolePutch` loop; `hal_consolePutch` takes
  `console_common.lock`. Order is always log→console and no console path re-enters
  `log_write`, so no inversion. Caveat: the per-byte PL011 busy-wait runs **under the held
  `log_common.lock`**, serializing all klog producers behind UART speed — a latency concern,
  not a correctness bug.
