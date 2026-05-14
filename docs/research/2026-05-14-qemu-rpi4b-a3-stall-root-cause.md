# 2026-05-14 — QEMU rpi4b A3 stall: root cause + tool capability

## TL;DR

QEMU rpi4b stalls right after plo prints `A3` because **plo's
`hal_exitToEL1` `exit_el3` block erets directly from EL3 to EL1 (SPSR =
0x3c5 → M field = 0b00101 = EL1h)**, while passing the kernel ELF
entry `0xffffffffc0000000` as the new PC. On real Pi the
`kernel8.img` image is wrapped by `phoenix-kernel8-reloc.S` — a small
trampoline that runs at the **low PA where plo placed kernel8.img**
and forwards control to the high VA only after the MMU is up. **QEMU
loads plo.elf + loader.disk directly, with no relocation trampoline**,
so plo erets to a high VA at EL1 with MMU off → instant translation
fault → PC lands in `VBAR_EL1 + 0x200` (sync exception, same EL with
SPx) which is at `0x200` because VBAR_EL1 is still zero. Captured
state matches exactly:

```
CPU#0:
  PC=0x200
  PSTATE=0x3c5  ---- NS EL1h    FPU disabled    SP=0x0
  X3=0xfe201000 (PL011 base)
Cores 1-3: wedged in plo's secondary spin loop @ PA 0x2001e8 (expected)
```

This is **not** the same bug as the cache-enabled real-Pi
userspace-silent failure documented in
`2026-05-13-iter-11-12-cache-walker-finding.md`. Both involve EL
transitions but the mechanisms are independent:

* **QEMU**: plo never successfully hands control to the kernel
  because no low-PA trampoline exists in this boot path.
* **Real Pi (cache-enabled)**: kernel reaches `proc_reap idle`
  cleanly, but spawned user processes don't actually execute any
  code (EL1→EL0 transition silently fails under cache).

## How the debug capability landed

Image `2e8ed7dd…` baseline (M-only) reaches `(psh)%` on real hardware
but until today QEMU rpi4b never reached the kernel banner — `A3`
silence with no fault info. New `./scripts/qemu-debug.sh --gdb`
solves that: it launches the new qemu-11.0.0 (built
`--enable-debug --enable-debug-stack-usage --enable-valgrind`)
with the gdb stub paused on startup, attaches `gdb-multiarch` with
an auto-script that sets PA breakpoints at `0x80000` /
`0x8007c` / `0x80100` (kernel image entry candidates) plus symbol
breaks on `_start` / `el1_entry`, then resumes; a Python watcher in
gdb interrupts the CPU after 20 s so we capture state even when no
breakpoint fires. After interrupt, `monitor info registers` dumps
every architectural register for all four cores — including
`PSTATE` mode, `ELR_ELx`, `SP_ELx`, etc — which is what surfaced the
SPSR=EL1h mismatch.

## What to do about the QEMU boot

Two cleanish options, ranked by least intrusive:

1. **Build a QEMU-side relocation trampoline** equivalent to
   `phoenix-kernel8-reloc.S` and pass it as `-kernel` instead of
   `plo.elf`. Plo can be embedded as the payload the trampoline
   relocates to a known low PA, then jumps. This mirrors the real-Pi
   boot path and means the same kernel ELF binary works in both
   environments.

2. **Have plo's `exec` command (the kernel loader) read the kernel
   ELF entry, but rewrite it to a low-PA stub** at load time. The
   stub takes the high-VA target as a parameter and jumps to it
   after MMU enable. Less mirroring of real boot, more changes in
   plo, but no separate trampoline binary needed.

Either way, the fix is not on the critical path for real-Pi work
since QEMU rpi4b is not the authoritative test platform. But once
fixed, QEMU becomes a viable iteration loop for non-cache changes
(syscall correctness, scheduler bugs, userspace bring-up, etc).

## What's the impact for the real-Pi userspace-silent problem

QEMU is not the right tool to diagnose the real-Pi cache-on
userspace-silent regression — even after fixing QEMU's boot, the
exact bug would not reproduce (the bug is in EL1→EL0 transition
under D-cache; QEMU's cache model is permissive and may hide it).

The real-Pi diagnostic path documented in earlier sessions
(`pcie:main()` direct-PL011 probe + serial capture) remains the
right approach for the cache-on EL0 regression.

## qemu-debug.sh capability summary

```
./scripts/qemu-debug.sh                          # bare 60s run, qemu-11.0.0
./scripts/qemu-debug.sh --gdb                    # gdb-attached, captures
                                                 #   full CPU state on stall
./scripts/qemu-debug.sh --qemu 10.2.2            # use older qemu for A/B
./scripts/qemu-debug.sh --label myrun --timeout 90
./scripts/qemu-debug.sh --print                  # also stream UART to stdout
```

Outputs to `artifacts/qemu/`:
* `*.uart.log` — captured serial console
* `*.qemu.stderr.log` — QEMU's own stderr
* `*.gdb.log` — full gdb session with breakpoints, register dumps,
  disassembly around stop PC (only with `--gdb`)

Allowlisted via `.claude/settings.json`, so future sessions can
run it without permission prompts.
