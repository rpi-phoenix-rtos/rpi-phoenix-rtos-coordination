# In-process debugger / backtrace for Phoenix-RTOS Pi4 — Tier-0 VALIDATED on HW (2026-07-29)

Goal (user request): diagnose CPU-side hangs / crashes / memory / stack of a running process
(e.g. vkQuake) over UART, keeping the Pi booted and iterating only NFS binaries — no kernel
rebuild/reboot. Saves the boot→capture→analyze cycle.

## What Phoenix provides (researched + HW-probed with tools/dbg-probe/test-sig.c)
- **NO ptrace** in kernel or libphoenix → stock gdbserver does NOT drop in.
- **Userspace signal delivery WORKS on aarch64** (HW-confirmed, sigprobe3 boot):
  - `sigaction()` installs handlers; ABI is the SIMPLE `sa_handler(int)` form — **NO SA_SIGINFO /
    sa_sigaction / ucontext** (libphoenix/signal/signal.c `_signal_handler` calls `handler(sig)`;
    struct sigaction has separate sa_handler/sa_sigaction but only sa_handler is honored).
  - **SIGSEGV (sync fault, NULL deref) IS delivered to the handler** → crash backtrace feasible.
  - **SIGALRM (async timer via alarm()) IS delivered** → we can INTERRUPT a hang (watchdog).
- **Frame-pointer backtrace works** (build with -fno-omit-frame-pointer): the fp-walk from the
  SIGSEGV handler produced clean ascending frames with valid return addresses; addr2line against
  the ELF resolved them. BUT from the handler the chain only reaches the signal PLUMBING
  (`_signal_trampoline`, `_startc`) — NOT the interrupted/fault site — because there's no ucontext
  and the trampoline breaks the chain to the interrupted code.

## The key to a USEFUL backtrace (names the actual bug site), userspace-only
The kernel `hal_cpuPushSignal` (hal/aarch64/cpu.c) **memcpy's the full interrupted `cpu_context_t`
onto the user signal stack** (has the interrupted `pc`, `sp`, and all x[] incl. x29=frame pointer),
and the trampoline `_signal_trampoline` (libphoenix/arch/aarch64/signal.S) already loads that
`cpu_context_t*` (to pass to `sigreturn`). So the interrupted context IS recoverable. Minimal fix:
add ~3 instructions to `_signal_trampoline` to stash that `cpu_context_t*` into a global
(`_dbg_signal_ctx`) before `bl _signal_handler`. Then a userspace `dbg_backtrace()` reads
`_dbg_signal_ctx->pc` + `->x[29]` and walks the fp chain from the INTERRUPTED frame → names the
real fault/hang site. libphoenix is static-linked into each test binary, so this is NO kernel
change / NO reboot.

## Tier-1 plan (concrete, de-risked)
A `dbg` facility in the platform shim (pl_phoenix_sys.c), linked into every test binary:
1. Install SIGSEGV/SIGILL/SIGABRT handlers → on crash, `dbg_backtrace()` (context-aware) dumps
   `pc` + fp-chain return addresses + key registers over UART. Host: addr2line/gdb → function:line.
2. A SIGALRM **watchdog** (`alarm(N)` re-armed) → on a HANG, the timer fires, the handler dumps the
   CURRENT (interrupted) backtrace via `_dbg_signal_ctx` → "process is stuck at function X". This is
   exactly what the vkQuake Host_Init hang needs.
3. Optional `dbg_dump_mem(addr,len)` over UART → host `gdb`/hexdump for memory/stack examination.
Channel: UART (mute klog via console_setmode while dumping); TCP-over-lwip is a later nice-to-have.
Tier-3 (full interactive gdb-RSP stub with BRK breakpoints, no single-step=no kernel change) is a
follow-on if step/breakpoint interactivity is needed; Tier-1 already gives "where is it hanging".

## HW evidence
tools/dbg-probe/test-sig.c (v2, sa_handler + fp backtrace); boots sigprobe/sigprobe2/sigprobe3.
sigprobe3: `sigaction(11/14)=0`, `SIGALRM handler fired #1 (sig=14)`, `SIGSEGV handler fired
(sig=11) — SYNC-FAULT DELIVERY WORKS`, fp frames resolved to _signal_trampoline/_startc.
