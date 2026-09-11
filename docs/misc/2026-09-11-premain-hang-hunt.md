# The pre-`main` hang: what is ruled out, and why the cheap reproducer does not exist

*2026-09-11. Companion to the `premain-hang` row in `docs/KNOWN-ISSUES.md`. This file holds the
working detail so the weekly log can stay short.*

## The symptom

One QuakeSpasm launch produced **no output at all** after psh echoed the command, and the prompt
never returned — so it hung *before* `main()`. It has not recurred in the 29 boot-scoped runs since:
the rate is **1 in 30 (~3%)**, not the "1 in 2" or "1 in 8" reported earlier off small samples.

## Localised from artifacts, not guesses

- **Not slow exec.** The failing run had a *longer* observation window than passing ones.
- **It hung rather than exited** — no prompt ever came back.
- **Before `main()`.** A passing run prints `main() entered` on the very next line.
- **`_init_array()` exonerated by disassembly.** 7 entries: 4 compiler-generated, 3 that only
  construct a static object and call `__cxa_atexit`. No syscalls.

That leaves `_libc_init()`, where **exactly one call can block**: `isatty(stdout->fd)`
(`stdio/file.c:1546`) = `tcgetattr()`, an ioctl to the tty **through a port**. Everything else is
`calloc`/`mutexCreate`.

## The instrument

libphoenix `0bc13cc` + `1c6cc83`, compile-time gated on `LIBC_STARTUP_TRACE=y`: markers between all
seven initialisers, emitted with `debug()` — a raw syscall, so it is safe before `_file_init`, where
an `fprintf` would deadlock on the very machinery under test. 22 boot-scoped trials with it live:
**22/22 reached `main`**. The hypothesis is *untested*, not confirmed.

## ⛔ The per-launch model is dead (measured)

The hunt had been costed wrong. Both known pre-`main` faults — this hang and `atexit-null-head` —
are **large binaries demand-paged from the NFS root**, so the unit of exposure is a **launch**, not a
**boot**. A boot-per-trial bench was paying ~3 minutes for what a loop does in 0.1 s.

`/bin/spawn-storm` (phoenix-rtos-tests `dce1612`) re-launches one program N times inside a single
boot. It prints the launch index **before** each spawn on an unbuffered stream, so a child that never
returns leaves its index as the last line of the UART log — no watchdog needed to localise a hang.

| subject | launches | failed | slowest | `object EOF at` | post-banner exceptions |
|---|---|---|---|---|---|
| `python3 -V` (58 MB, port-linked) | ~563 | 0 | 557 ms (launch 35) | 0 | 0 |
| `bash -c true` (1.4 MB, port-linked) | 2500 | 0 | 92 ms (launch 1) | 0 | 0 |

**~100 failures were expected at the 1-in-30 rate. Zero occurred.** Conclusions:

1. The fault is **not a per-launch lottery**. It depends on boot state, or on the specific binary.
2. The `isatty`/`tcgetattr` hypothesis loses its cheapest test: 3063 processes ran exactly that code
   with the tty port warm and none blocked.
3. **Byproduct worth keeping:** `vfork`+`execv` is solid — 3063 cycles, 0 failures, and the slowest
   launch was the **cold first one** in both runs (no latency drift, no leak). Process spawn is not a
   demo risk.

### Reading the two runs honestly

The python3 run has **no `DONE` line** and its log ends at `launch 558/600`, which looks identical to
a hang. It was not one: the harness reported `max-cmd-secs (300s) reached, moving on`, 558 launches ×
~538 ms fills exactly 300 s, and the *next* cycle's stale-buffer flood contains `launch 563/600` —
past where its own log ended. The bash run completed properly and printed `DONE 2500 ok, 0 failed`.

## ⏭ What is actually left to try

- **Boot-scoped trials of the real failing binary** (QuakeSpasm), since binary-dependence survives.
- **Reproduce under gate-like load** — X server + GPU + NFS traffic — rather than on a quiet system.
  Every storm above ran in a quiet system, which is the one variable the original failure had and
  these runs did not.

## ⚠ Traps recorded so a future hunt is not wasted

- `--scope core` does **not** put the trace into port binaries — they link their own libc. Check
  `strings <app> | grep libc-init` returns nonzero *before* believing any trial.
- psh **cannot set env vars**, so the trace has to stay compile-time gated.
- Never read a bench's per-trial log while the bench is still running: a half-written log is
  indistinguishable from a hang. A false reproduction was reported and retracted that way.
- Turning a `-D` knob off does **not** invalidate the objects it changed. A "clean" rebuild reused
  `misc/init.o` with the define still baked in and exited 0. Verify with
  `strings <binary> | grep <marker>` returning 0 — never the exit code.

## Candidate fix, deliberately unapplied

Defer the `isatty()` probe to the first write on `stdout`. That removes the only blocking IPC from
every process's startup, but it moves when `F_LINE` is set. Changing stdio buffering on a hypothesis
that now has *less* support than it did is not a trade worth making without evidence.
