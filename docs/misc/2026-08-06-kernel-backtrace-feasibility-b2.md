# B2 — Kernel-side backtrace facility: feasibility memo

**Date:** 2026-08-06
**Scope:** Read-only analysis. No source changed, nothing built, Pi untouched.
**Question:** Can the in-process libdbg backtrace facility (B1/B3) be extended to the
**kernel**, so an EL1 fault/panic prints a call-chain backtrace next to the register
dump it prints today?

---

## Verdict: **TRACTABLE**, but *not* with the kernel as it is built today.

A simple frame-pointer (x29) chain walk — the exact technique libdbg uses — **cannot
work against the current kernel binary**, because the kernel is compiled
`-O2 -fomit-frame-pointer` and demonstrably does **not** maintain an x29 frame-record
chain (empirically confirmed below). The make-or-break gate therefore **fails as-built**.

It becomes tractable with a **single, kernel-scoped one-line build change**
(`CFLAGS += -fno-omit-frame-pointer`), after which libdbg's ~20-line fp-walk can be
reused almost verbatim in-kernel. A zero-build-change fallback (stack scanning) also
exists but is noisier. Effort: **~0.5–1 day** including a HW validation cycle.

---

## Findings, point by point

### 1. Where kernel faults are handled and where the register dump is printed

The aarch64 exception vector dispatches through `exceptions_dispatch()`
(`hal/aarch64/exceptions.c:278`) → the per-EC handler. Two handlers are registered at
runtime by higher layers and *override* the HAL defaults:

- **`process_exception`** — registered as `EXC_DEFAULT`
  (`proc/process.c:1930`, `hal_exceptionsSetHandler(EXC_DEFAULT, process_exception)`).
- **`map_pageFault`** — registered as `EXC_PAGEFAULT`, i.e. all four
  INSTR/DATA × EL0/EL1 abort slots (`vm/map.c:1728`; the `EXC_PAGEFAULT` fan-out is
  `hal/aarch64/exceptions.c:374-379`).

Both funnel into a **single chokepoint** that emits the `Exception #NN: Data Abort
(EL1) …` line seen in UART logs:

> **`process_dumpException()` — `proc/process.c:251`**, which calls
> `hal_exceptionsDumpContext(buff, ctx, n)` (`process.c:259`) then prints it
> (`process.c:260`).

Callers of `process_dumpException` for a **kernel (EL1)** fault:
- `vm/map.c:811-814` — `if (hal_exceptionsPC(ctx) >= VADDR_KERNEL) process_dumpException(...)`
  (kernel-PC fault, dumped ASAP), and `vm/map.c:828` — unresolved fault
  (`vm_mapForce` failed); if `thread->process == NULL` it halts (`map.c:830-833`).
- `proc/process.c:289` — `process_exception` → `process_dumpException`; a kernel
  thread (`process == NULL`) then `hal_cpuHalt()`s (`process.c:291-293`).

The HAL also has three *direct* callers of `hal_exceptionsDumpContext` that survive for
special ECs even though `EXC_DEFAULT` is overridden: `exceptions_defaultHandler`
(`:181`), `exceptions_serrorHandler` (`:212`, TD-10), `exceptions_watchpointHandler`
(`:260`). These are the SError / watchpoint / never-registered-EC paths.

The saved context struct is `exc_context_t` (`hal/aarch64/arch/exceptions.h:56`):
`{ u64 esr; u64 far; cpu_context_t cpuCtx; }`, and `cpu_context_t`
(`hal/aarch64/arch/cpu.h:65`) holds `x[31]` (with `x[29]=fp`, `x[30]=lr`), `pc`, `sp`,
`psr`. The dump already prints `fp=ctx->cpuCtx.x[29]`, `lr=x[30]`, `sp`, `pc`
(`exceptions.c:163-168`) — so a backtrace has everything it needs *if the fp is valid*.

### 2. Frame-pointer availability — **the make-or-break — fp is NOT available**

The aarch64 target flags set it explicitly:

> `sources/phoenix-rtos-build/target/aarch64.mk:14` `OLVL ?= -O2`
> `sources/phoenix-rtos-build/target/aarch64.mk:20`
> `CFLAGS += -mcpu=$(cpu) -mtune=$(cpu) -fomit-frame-pointer -mstrict-align -mno-outline-atomics`

No `-fno-omit-frame-pointer` / `-mno-omit-leaf-frame-pointer` override exists anywhere in
`phoenix-rtos-kernel` (grepped: none).

**Empirically confirmed** against the built kernel ELF
(`aarch64-phoenix-objdump -d prog/phoenix-aarch64a72-generic.elf`):
- `process_dumpException` prologue is
  `sub sp, sp, #0x420` / `stp x30, x19, [sp]` / `str x20, [sp,#16]` — it saves the
  return address (`x30`) paired with a **callee-saved GP reg (x19)**, and there is
  **no `mov x29, sp`**. x29 is not a frame pointer here.
- Whole-kernel counts: **`mov x29, sp` = 4** (a handful of functions where GCC happened
  to use x29 as scratch) and **`stp x29, x30` = 0** (zero frame-record saves). 57 total
  x29 mentions across the image.

A naive `next=[fp]; ret=[fp+8]` walk starting from `ctx->cpuCtx.x[29]` will therefore
read garbage and terminate immediately (or worse). **The libdbg technique does not
transfer to the kernel as-built.**

### 3. Walk feasibility in fault context

Starting point is available and safe to read from the saved context
(`ctx->cpuCtx.x[29]`, `.sp`, `.x[30]`, `.pc`). The problem is purely #2 — with fp
omitted there is no chain to walk. Two ways to make a walk feasible:

- **(A) Enable frame pointers (recommended).** With `-fno-omit-frame-pointer`, every
  non-leaf function emits `stp x29,x30,[sp,#-N]!; mov x29,sp`, giving the classic
  `[x29]=prev fp`, `[x29+8]=saved lr` chain. libdbg's `dbg_walkFp()`
  (`libdbg/dbg.c:53-73`) then works verbatim, including its robustness guards:
  ascending-fp check + bounded delta (`next<=fp || next-fp>0x400000` → stop) + null-ret
  stop + iteration cap (40). For the kernel add one extra guard: accept a return address
  only if it is inside the kernel text range (`.init`+`.text`, VA
  `0xffffffffc0000000 … 0xffffffffc0023568` per the ELF section headers; use the
  existing `VADDR_KERNEL` bound). Kernel stacks are small (`SIZE_KSTACK = 2*PAGE = 8 KiB`,
  `hal/aarch64/arch/cpu.h:29`), so bound the walk to the current stack page span too.
- **(B) Stack scan (zero build change).** Scan 8-byte words from `sp` upward within the
  stack bound; print any word that lands in kernel text as a candidate return address.
  Works on the as-built kernel, no fp needed, but yields false positives (spilled code
  pointers) — acceptable because symbolization is offline and human-filtered.

Both must be bullet-proof (see Risks). The walk reads only the current, already-faulted
stack — no allocation, no locks.

### 4. Symbolization — **works, same workflow as libdbg**

A non-stripped kernel ELF **with full debug info** exists:

> `/.buildroot/_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf`
> — `ELF 64-bit … not stripped, with debug_info`; 621 FUNC symbols; contains
> `hal_exceptionsDumpContext`. (Global `CFLAGS += -ggdb3`, `Makefile.common:156`; only
> the userspace `prog.stripped/` copies are stripped.)

Offline, identical to libdbg:
`aarch64-phoenix-addr2line -f -e prog/phoenix-aarch64a72-generic.elf <printed addrs>`.

Note: `kernel8-reloc/phoenix-kernel8-reloc.elf` is the relocation wrapper (1 symbol) —
**not** the symbol source. Use the `prog/` ELF.

**Relocation caveat (check, not assumption).** Active branch is
`agent/rpi4-program-reloc`. The kernel is *linked* at fixed high VA
`VADDR_KERNEL_INIT = 0xffffffffc0000000` (`aarch64.mk:35`) and the reloc work relocates
the **physical** load address while the MMU maps text back to that fixed VA — so runtime
kernel PCs should equal link VAs and symbolize directly. No kernel-EL1 fault PC is
present in current `artifacts/` logs to confirm empirically (only EL0 user PCs, e.g.
`pc=0x68f2c0`). **Verify on the first real EL1 dump:** if a printed kernel `pc=` is not
in the `0xffffffffc0…` range, subtract the runtime→link delta before `addr2line`.

### 5. Integration point & reuse

libdbg is a **libphoenix-linked userspace** archive; the kernel does not link libphoenix,
so `libdbg.a` cannot be reused directly. But the load-bearing logic is the ~20-line
`dbg_walkFp()` — trivially reimplemented in-kernel using facilities the dump already
uses: `hal_consolePrint()` and `hal_i2s()` (both already called in
`hal_exceptionsDumpContext`, `exceptions.c:160-171`).

**Recommended hook:** add `void hal_exceptionsBacktrace(exc_context_t *ctx)` in
`hal/aarch64/exceptions.c` (arch-local — it knows `cpuCtx.x[29]`/`sp`), printing
incrementally via `hal_consolePrint`/`hal_i2s`. Mirror libdbg's leaf handling: emit the
faulting `pc` (leaf) and `x[30]` (lr) first, then walk `x[29]`. Call it from the single
chokepoint **`process_dumpException` (`proc/process.c`) right after the dump print at
line 260**, gated on supervisor mode:

```c
if (hal_cpuSupervisorMode(&ctx->cpuCtx) != 0) {   /* EL1 only */
    hal_exceptionsBacktrace(ctx);
}
```

`hal_cpuSupervisorMode()` already exists (`cpu.h:154`). One hook here covers both the
`EXC_DEFAULT` and `EXC_PAGEFAULT` kernel paths. Optionally add the same call to the three
HAL direct-dump paths (`exceptions_defaultHandler`/`serrorHandler`/`watchpointHandler`)
for full coverage; SError is asynchronous so its chain is best-effort.

**Why the gate matters (point 3, restated):** for an EL0 fault reaching the dump,
`x29/sp` and the return addresses are *userspace* — wrong stack, wrong ELF for symbols,
unsafe to deref from EL1. EL0 faults are already covered by userspace libdbg; the kernel
backtrace must run **only** when `hal_cpuSupervisorMode(&ctx->cpuCtx) != 0`.

**Build change for route (A):** append in `phoenix-rtos-kernel/Makefile` at line ~28
(`CFLAGS += -I. -ffreestanding`), which is evaluated *after* `Makefile.common` pulls in
`aarch64.mk` (kernel Makefile `include ../phoenix-rtos-build/Makefile.common` is line 26,
the `CFLAGS +=` additions follow at 28). GCC honors the last flag, so
`CFLAGS += -fno-omit-frame-pointer` here **wins over** aarch64.mk's `-fomit-frame-pointer`
and is scoped to the kernel only — userspace, plo, libphoenix builds are unaffected.
Rebuild with **`--scope core`** (committed core change; stale-image hazard per CLAUDE.md)
and verify the fp setups appear (`aarch64-phoenix-objdump -d … | grep -c 'mov.*x29.*sp'`
should jump from 4 into the hundreds).

### 6. Risks

- **The backtrace must never itself fault** — it runs inside the fault handler. Mandatory:
  strict stack bounds, kernel-text range check on every candidate return address, ascending
  monotonic fp, hard iteration cap (libdbg uses 40). No dereference of an unvalidated pointer.
- **No locks / no allocation.** `process_dumpException` runs in fault context (map.c
  comment at `:812` explicitly avoids spinlock deadlock). The walk reads only the current
  stack and prints via `hal_consolePrint` — do not take the exceptions/scheduler lock.
- **No recursion.** If the backtrace faults it must not re-enter itself; a static
  per-CPU "in backtrace" guard is cheap insurance.
- **Stack-overflow faults.** `process_dumpException` already allocates `buff[1024]`
  (`SIZE_CTXDUMP`) on the faulted kernel stack; a deep/overflowing stack makes that risky.
  Prefer printing the backtrace **incrementally** with small local buffers rather than one
  more 1 KiB buffer.
- **SMP.** Only the faulting core runs the handler; other cores keep running (kernel-thread
  fault path halts *this* core, `process.c:291-293`; `map.c:830-833`). Tag the output with
  `hal_cpuGetID()` (`cpu.h:161`) so multi-core dumps are attributable. No cross-core stack
  access.
- **Route (A) cost:** `-fno-omit-frame-pointer` adds a small code-size / per-call overhead
  (prologue stp + `mov x29,sp`, one reserved reg). Negligible for this kernel; the standard
  price for a reliable unwinder (Linux `CONFIG_FRAME_POINTER`).

---

## Recommendation

Take **route (A)**: one-line kernel-scoped `-fno-omit-frame-pointer`, add
`hal_exceptionsBacktrace()` in `hal/aarch64/exceptions.c` (reusing libdbg's fp-walk +
guards, plus a kernel-text range check), and call it from `process_dumpException`
(`proc/process.c:260`) gated on supervisor mode. Symbolize offline against
`prog/phoenix-aarch64a72-generic.elf` with `aarch64-phoenix-addr2line` — the libdbg
workflow, unchanged. Keep route (B) stack-scan as a documented fallback if the frame-pointer
build cost is ever unwanted.

**Effort:** ~0.5–1 day (one-line flag + ~40 LOC + one `--scope core` rebuild + one HW
cycle to induce a deliberate EL1 fault and confirm a symbolized chain).

**Top risk:** a backtrace that faults *inside the fault handler*; mitigated by strict
bounds-checking, text-range validation, iteration cap, and no locks/allocation.
