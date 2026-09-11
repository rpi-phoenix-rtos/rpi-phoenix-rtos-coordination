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

## Not reproduced in ~4200 launches of three *other* binaries (measured)

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
| `cxxprobe` (1.5 MB, **C++**/libstdc++ static init) | 589 (500 reaped) | 0 | 522 ms | 0 | 0 |

**Zero failures.** Conclusions, stated no more strongly than the data allows:

1. Since `_libc_init()` runs identically in every process, this points **away from `_libc_init`
   itself** and toward something specific to **the failing binary** or to **boot state**.
2. The `isatty`/`tcgetattr` hypothesis loses its cheapest test: ~4200 processes ran exactly that
   code, many with the tty port contended, and none blocked.
3. **Byproduct worth keeping:** `vfork`+`execv` is solid — ~4200 cycles, 0 failures, and the slowest
   launch was the **cold first one** in both the `bash` and `python3` sequential runs (no latency
   drift, no leak). Process spawn is not a demo risk.
4. **`0 object EOF at` across all ~4200 NFS-paged launches** — a result, not just a column. The
   premature-EOF path shipped in kernel `6cd3adec` does not fire even under heavy demand-paging
   load, which bounds how often that zero-fill can be occurring.

### ⚠ What these runs do NOT show

**That the fault is not per-launch *for QuakeSpasm*.** The 1-in-30 rate was measured on QuakeSpasm
alone. Using it to predict an expected failure count for `bash`/`python3` would assume the very
binary-independence the experiment set out to test — a circular inference, and an earlier draft of
this file made it ("~100 expected"). The honest statement is that the fault does not fire on `bash`,
`python3` or `cxxprobe` startup at any rate these runs could detect.

Nor did the first two cover QuakeSpasm's **shape**: it is an **18.5 MB C++** static ELF with a
7-entry `.init_array`, while `bash` and `python3` are **C**. ✅ That gap is now closed — `cxxprobe`
(a C++ binary whose startup runs real libstdc++ static init: `chrono`, `std::filesystem`) storms
clean too, **589 launched / 500 reaped / 0 failed**. So the C++ static-construction path is covered,
and what remains untested is QuakeSpasm's own `.init_array` at its own size, boot-cold state, and a
live X + GPU load.

### Concurrency does not reproduce it either (4-way, same day)

Sequential runs all started a process on a quiet system, while the original failure happened with a
desktop and a GPU app live — so concurrency was the untested variable, and startup's one blocking
call is an ioctl to the tty *through a port*. This target has already had a multi-waiter wakeup bug
(libphoenix `semaphoreUp` signalling only 0→1), so contention there was worth a direct test.
`spawn-storm -p N` overlaps N launches (tests `8d00cda`).

| run | launches | reaped | failed | slowest | `object EOF at` | exceptions |
|---|---|---|---|---|---|---|
| `python3 -V`, `-p 4` | 556 | ≥500 | **0** | **2214 ms** | 0 | 0 |

The slowest launch went **557 ms → 2214 ms** against the sequential run, which confirms the four
children really were contending rather than serialising. Still **zero** failures.

⚠ **The first concurrent attempt had to be thrown away — the instrument was lying.** Children share
the tty, and their writes shredded the parent's lines (`torm: pid 33 is the new slowest`,
`id 26 is the new slowest`), so a `BAD status` line could have been mangled past any grep: the
`0 failures` it reported was not evidence of anything. Fixed by redirecting child stdout/stderr to
`/dev/null` (their exit status is the real evidence) and banking a `PROGRESS` tally every 100 reaps,
since a harness cutoff — not `DONE` — is how these runs normally end. The re-run shows **0 mangled
lines**, which is what makes its zero trustworthy.

### Reading the two runs honestly

The python3 run has **no `DONE` line** and its log ends at `launch 558/600`, which looks identical to
a hang. It was not one: the harness reported `max-cmd-secs (300s) reached, moving on`, 558 launches ×
~538 ms fills exactly 300 s, and the *next* cycle's stale-buffer flood contains `launch 563/600` —
past where its own log ended. The bash run completed properly and printed `DONE 2500 ok, 0 failed`.

## ⏭ What is actually left to try

- **Boot-scoped trials of the real failing binary** (QuakeSpasm), since binary-dependence survives.
- **Reproduce under gate-like load** — X server + GPU + NFS traffic. Plain 4-way process
  concurrency is now ruled out, so if load matters it is the *kind* of load (a live X server and GPU
  submits) rather than concurrent startup as such.

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
