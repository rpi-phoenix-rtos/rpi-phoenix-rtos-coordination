# Attended work-order — user-stack-exhaustion signal-push kernel DoS (aarch64)

**Status:** root-caused + blast-radius characterized on HW (2026-08-22). **Fix is
ATTENDED** — `hal_cpuPushSignal` is the highest-blast-radius path in the kernel (every
signal + every fault delivery), and a regression is invisible to an unattended
green-boot smoke. Diagnosis + proposed guard + validation plan below; do the apply
pass with the owner, exactly like the B-items work-order.

## Severity: userspace-triggerable kernel DoS (NOT cosmetic)
Any unprivileged process that exhausts its main-thread user stack takes down the box.
Confirmed on HW (netboot, `tools/stack-bomb/stackbomb.c`, ~4 KiB/frame recursion past
the 1 MiB `SIZE_USTACK`): the follow-up `echo POST-BOMB-ALIVE` never printed — psh/the
kernel did not recover after the fault. This upgrades the earlier "corrupts the crash
dump" note (project_coreutils_cksum_od_dataabort) to a real availability bug.

## Register evidence (artifacts/rpi4b-uart/…-stackbomb-blastradius.log)
```
Exception #36: Data Abort (EL0)   ; the stackbomb overflow
  sp =0x7fffefff90  far=0x7fffefff90  esr=0x92000047 (DA, write, L3 translation fault)
Exception #37: Data Abort (EL1)   ; the kernel signal-frame push, immediately after
  pc =0xffffffffc000a0e8 (kernel)  far=0x7fffeffc60  esr=0x96000047 (DA at EL1, write)
  x0 =0x7fffeffc60  x2=0x330        ; x0 = memcpy dest = signalCtx, x2 = 0x330 = sizeof(cpu_context_t)
  lr =0xffffffffc000678c
```
`far(EL1) = 0x7fffeffc60 = far(EL0) 0x7fffefff90 − 0x330`. Exactly `signalCtx = userSP −
sizeof(cpu_context_t)` — the page just below the exhausted SP, which is unmapped, so the
kernel's write into it faults again at EL1 and there is no recovery.

## Root cause (hal/aarch64/cpu.c:92 `hal_cpuPushSignal`)
On an EL0 fault the kernel delivers the signal by pushing a signal frame onto the user
stack. `proc/threads.c:489/1481` compute `signalCtx = hal_cpuGetUserSP(ctx) −
sizeof(cpu_context_t)`; `_threads_checkSignal` (threads.c:1446) calls
`hal_cpuPushSignal`, which does **unconditionally**:
```
hal_memcpy(signalCtx, ctx, sizeof(cpu_context_t));   // cpu.c:106  <-- faults here at EL1
signalCtx->pc = handler; signalCtx->sp -= sizeof(cpu_context_t);
hal_stackPutArgs(&signalCtx->sp, …);                 // more user-stack writes below signalCtx
```
No check that `signalCtx` (and the args region below it) is mapped-writable in the
process VM. When the crashing process's stack is exhausted, `userSP` is at/below the
stack VMA limit, so `signalCtx` is unmapped → the memcpy write-faults at EL1.

## Proposed fix (design — verify the flagged unknowns first)
1. **Guard the push:** before writing, validate the whole target range
   `[signalCtx->sp − argsSize, signalCtx + sizeof(cpu_context_t))` is within a
   **writable, mapped** region of `proc->mapp`. If not, `hal_cpuPushSignal` returns
   non-zero (the caller already treats non-zero as "not delivered": threads.c:1456→1465
   returns -1).
2. **Terminate cleanly on failure:** ensure that when the signal frame cannot be pushed
   for a *fatal* fault signal (SIGSEGV/SIGBUS default = terminate), the process is
   KILLED (default action), not resumed into the faulting instruction (refault loop) and
   not left to double-fault. Trace the EL0-fault→signal→(`_threads_checkSignal` == −1)
   path and confirm/ add the terminate. `threads_sigpost`→`proc_kill` (threads.c:1374/1389)
   is the likely lever.

## CRITICAL design trap (advisor-flagged) — do NOT get this wrong
The user stack is **demand-paged** (SIZE_USTACK 1 MiB, paged in on fault). The guard MUST
distinguish:
 - **inside the stack VMA but not yet resident** (NORMAL — the write must succeed and
   demand-page it) — must NOT fail, or every process whose top stack page isn't faulted
   in yet gets silently SIGKILLed = a widespread invisible regression;
 - **beyond the stack VMA** (EXHAUSTED — must fail).
So the check is a **VMA/region membership + prot** test against `proc->mapp` (demand-zero
counts as mapped), NOT a "is this page currently resident/resolvable" test.

## Unknowns to confirm BEFORE building (they decide correctness)
1. **`vm_mapFind` semantics** (vm/map.h:77) — is it an allocator (find free space) or a
   lookup? Wrong primitive → wrong check. Need the map-entry lookup that returns the
   entry covering a vaddr + its `prot` (see `vm_map_t` entries / amap). If none exists,
   the guard needs a small read-only map-walk helper.
2. **Spinlock context:** `_threads_checkSignal` runs under `threads_common.spinlock`
   (threads.c:1477). Can the frame write even take a page fault there, or does the normal
   path already assume the top stack page is resident? If it assumes residency, the bug
   model is incomplete and the fix may belong at a different layer.
3. Confirm the `_threads_checkSignal == −1` path for a fatal fault actually terminates.

## Validation plan (attended — a green boot proves nothing here)
- **Normal signal delivery** still works: a program that installs a handler + raises a
  signal runs the handler + returns (regression gate #1).
- **Delivery when the top stack page is non-resident** (fresh deep-but-bounded stack use
  that pages in): handler still delivered, no spurious kill (regression gate #2 — the
  demand-paged trap).
- **True exhaustion terminates cleanly:** `tools/stack-bomb/stackbomb` → process dies with
  a correct single EL0 diagnostic, **NO EL1 double-fault, psh survives** (`POST-BOMB-ALIVE`
  prints). Multi-trial (it's deterministic here, but confirm).
- Cross-arch: the guard is in aarch64 `hal_cpuPushSignal`; check the other HALs' equivalents
  for the same unconditional-write pattern before upstreaming (likely shared latent bug).

## Repro (banked)
`tools/stack-bomb/stackbomb.c` (cross-build `-O0 -static`), stage to the netboot export
root, `test-cycle-psh-interact.sh -- "/stackbomb" "echo POST-BOMB-ALIVE"`. DoS = the second
command never prints.
