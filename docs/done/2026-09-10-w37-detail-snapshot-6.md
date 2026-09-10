# W37 detail snapshot 6 — 2026-09-10 (POSIX option macros, settled)

Trimmed out of `docs/inprogress/WEEK-2026-W37.md` to keep the weekly log short. Settled work; the
weekly log carries the one-line version.

---

## 3. ✅ DONE THIS TURN — libphoenix now advertises the POSIX option macros

Your call, executed. libphoenix declared `_POSIX_VERSION 200809L` and then defined **no option macro
at all**, so the portable way to ask for a monotonic clock —
`#if _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK)` — was **always false** and portable code
silently took its fallback. Eleven macros are now claimed (libphoenix `070ceda`).

- **Values are positive (`200809L`), never `0`/`-1`** — a lot of real code tests these with a bare
  `#ifdef`, to which POSIX's `-1` "not supported" convention reads as *supported*.
- **Contract test, both directions** (`phoenix-rtos-tests` `98e7014`, `libc/misc/posix_options`):
  every claim is positive, the portable gate is true, every claim is exercised through a real call,
  and the seven groups we do **not** implement are asserted still unclaimed. **HW 4/4**;
  `test-libc-misc` 208 → **212 Tests, 0 Failures**.
- **`_POSIX_SEMAPHORES` stays unclaimed on purpose** — libphoenix has no `sem_*` and no
  `semaphore.h`; claiming it makes CPython `#include <semaphore.h>` and fail to build. The test pins
  that.
- **`_POSIX_TIMERS` is claimed with a documented shortfall**: clocks complete, `timer_create` family
  absent. Claimed because it is the gate every portable monotonic test uses, and a `timer_create`
  caller now gets a loud link error instead of silent mistiming.
- **The one risk I flagged for you did not materialise.** `_POSIX_THREAD_ATTR_STACKSIZE` was supposed
  to make CPython set a thread stack size; `THREAD_STACK_SIZE` is 0, so `pthread_attr_setstacksize`
  is **never called** at default settings. CPython does now pass `&attrs` where it passed `NULL` —
  also a no-op, because libphoenix's `pthread_attr_init` copies `pthread_attr_default` and
  `pthread_create(NULL)` uses `&pthread_attr_default`, byte-identical. Net gain:
  `threading.stack_size(n)` goes from unsupported to working.
- **openssl's wall-clock entropy timing is fixed as a side effect** — `rand_unix.c:858` picked
  `CLOCK_REALTIME` for exactly this reason. Last night I recorded it as "not worth a wide rebuild";
  your go-ahead makes it free.

## 3a. ✅ Ports rebuilt and re-gated on hardware — 6/6, 0 faults

A libc header change lands unevenly, so the five affected ports were forced to rebuild and the whole
showcase was re-run against them. openssl, CPython, micropython and redis rebuilt clean; so, as a
dependency cascade, did quakespasm, quake3e, vkquake, yquake2 and SuperTuxKart — i.e. the gate below
is testing **rebuilt** games, not the old ones. **Zero `timer_*` link errors across two full ports
builds**, which is the test of the deliberate `_POSIX_TIMERS` shortfall: it holds as shipped.

| app | evidence on the rebuilt tree |
|---|---|
| X desktop (`startx_gpu action`) | wmaker + xbill + xclock + a V3D GL window + **the rebuilt python3** running Game of Life at 17.6 gen/s + xterm/`top` (30 tasks) |
| QuakeSpasm | in-game demo, **31 FPS** |
| Quake III (`+map q3dm1`) | live q3dm1 deathmatch, 4 bots scoring, **28 fps** |
| Quake II | in-game demo1 "Outer Base", 100 health, **38.86 fps** |
| vkQuake | demo2.dem, The Grisly Grotto, console overlay, vkQuake 1.36.0 |
| SuperTuxKart | in-race Hacienda, 4 karts, lap 1/2, **`FPS: 6/9/10 · 57 KTris`** |

Non-graphical checks, one cycle, 0 faults: **CPython** `threading.stack_size(512k)` now returns
`524288` where it was previously unsupported, a thread ran and joined with it, `monotonic` resolution
is **1 µs** (not 1 s) and 250 ms of sleep measures 250.0 ms, 8 threads × 1000 increments = 8000 ·
**MicroPython** `ticks_ms` 58186 (since boot, not epoch), `ticks_diff` over 200 ms = exactly 200 ·
**redis** 7.2.4 runs after its clean reconfigure.

ⓘ STK read `6/9/10` here against the `7/7/9` samples that prompted the 9→7 question. One on-screen
reading is not a measurement (§4), so this is noted, not claimed.

The contract test also grew the half it was missing — `_POSIX_CLOCK_SELECTION` is tied to
`pthread_condattr_setclock`, not just `clock_nanosleep`, and only the latter was being exercised. It
now asserts the selection reaches the wait: `pthread_cond_timedwait` against an absolute
**CLOCK_MONOTONIC** deadline must return `ETIMEDOUT`. HW: 4/4, suite still
**212 Tests, 0 Failures** (unchanged count is the right result — assertions were added to an existing
case, so a count change would itself be a signal).

ⓘ On Quake II's one silent run: the ram-stage wrapper is dated 03:07 while the game ELF it execs was
rebuilt at 09:07, but the absolute-path run rendered at 38.86 fps off exactly that pair, so it is not
an ABI mismatch between them.

## 3b. ⚠ Two traps worth your knowing, both of which cost me a pass

**Deleting a port's build-state json does NOT force a rebuild** — and it fails *convincingly*.
port_manager cleans only when the state has *changed*, so with the file absent it goes straight to the
build, `make` finds every object newer than its source, and the port is merely **re-linked from stale
objects**. The log prints `BUILD: python-3.14.4` and the binary gets a fresh timestamp. Caught by
checking the thing itself: `Python/thread.o` was **8 hours old** after a 4-second "rebuild", and
openssl **never compiled `rand_unix.c`** — the one file the change was for. Four of five ports
silently did not get it. Fixed properly: **`scripts/force-port-rebuild.sh`** poisons only the recipe
digest, so the framework sees a real change and cleans, then writes the correct digest back.
**Verification rule:** a `BUILD:` line is not evidence — require a
`Build state changed for <port>, cleaning` line *and* a compile line for a file you expect to change.

**Two of the six gate commands I had recorded were wrong**, and both failed in ways that look like
build breakage. Quake III launched bare sits on the **main menu** forever — still rendering, so it
passes every mechanical check while testing nothing. Quake II was being launched as the game ELF
instead of `/usr/bin/quake2`, the ram-stage launcher that stages data into a /tmp ramdisk first;
direct over NFS it never finds its data and dies on a missing `colormap.pcx`. The trap is in reading
the command back out of a UART log: the passing log's line 146 is
`ram-stage: exec /usr/bin/yquake2` — the **exec target**, not what was typed. Both are now recorded in
`scripts/run-showcase-gate.sh`, which runs the whole gate and prints the table.

## 3c. ⏸ Deliberately NOT re-cutting the SD image

The image you have queued to flash (`1afa724b`) stays the one to flash, and this change is **not**
folded into it. Reasoning, so you can overrule it: everything the POSIX macros change is invisible to
the demo — openssl's entropy timing, MicroPython's `ticks_*`, `threading.stack_size`, re2's mutex
choice. `1afa724b` is gated and proven; a fresh image would be neither, and retiring a proven artifact
for no demo-visible gain is the wrong trade while you are away. The rebuilt tree is verified over
netboot (§3a) and folds into the next scheduled image.

## 4. Open / not blocking the goal

- **🔎 Allocator double-free — OPEN, but the instruments were wrong and are now fixed.** Two
  corrections to what this log has been telling you. **The evidence base is thinner than claimed:**
  of 31 double-free reports (2026-06-28 → 09-10, 23 logs) **exactly one** ever captured the block
  detail, so "block class established from evidence" was a sample of one — and the one `caller=` in
  the archive is my own synthetic probe, not a field hit. **And one "eliminated" mechanism was never
  eliminated:** duplicate hand-out was refuted by a detector that *could not fire*, because it tested
  `CHUNK_CUSED` after the split had erased it. Repaired (libphoenix `1ee441a`, zero cost — the check
  just moved ahead of the split) and re-tested: 1500 liveness children, **0 hits**. That refutation
  now means something.
  Two readings are also **wrong** and corrected in `docs/KNOWN-ISSUES.md`: `0x410` has `CHUNK_PUSED`
  clear, so "un-coalesced, both neighbours in use" is impossible — both flags clear is the signature
  of a header that was freed and *merged away*; and that report's own `hfree` puts used at 432 bytes
  against a 1040-byte block, so the heap's accounting says the block **was not live** — strong, but
  not proof, since `freesz` is itself reachable by an underrun.
  ✅ The report now prints the two **footers**, which name the mechanism on sight. HW-proven on both:
  a genuine double free gives `foot == size & ~3`; a deliberate 1-byte overrun gives a mismatch,
  `pfoot = 0x4141414141414141` (its own fill bytes) — and reproduces the field occurrence's
  `size = 0x410` **exactly**.
- **⚠ The fault detector never looked for this bug at all.** `uart-summary.sh` and
  `reliability-tally.sh` had no pattern for any allocator report, so all 49 heap-corruption
  occurrences in the archive counted as **clean** — "0 fault-bearing" was never evidence about this
  class. Fixed and validated both directions (it now flags `getname1`, `stk-flipdiag` ×12,
  `stkguard1` ×4, and still passes the clean 1M-line malloc soak and the USB tracer logs).
  ⓘ Re-tallied with it: today **64/64 to prompt, 1 fault-bearing** — and that one is the deliberate
  probe log, which is what should be flagged. No real occurrence since 09-09.
  ⚠ **Coverage gap, deliberately left:** only the core binaries carry the repaired detector — the
  game ports still link the old allocator, so if STK or vkQuake hits this, it will not print the new
  fields. Closing it means `./scripts/force-port-rebuild.sh supertuxkart vkquake quakespasm quake3 yquake2`
  + `--ports-only`, which produces six ungated game binaries and costs a 40-minute re-gate. Not worth
  it while nothing has fired since 09-09 and today is clean; the next scheduled ports rebuild picks it
  up for free.
- **⚠ STK fps 9 → 7 — the missing instrument now exists.** Both named mechanisms stay refuted, and
  the blocker was never the mechanism list: it was that every number we had came from an engine's
  on-screen readout, OCR'd off one HDMI frame, which cannot separate "slower code" from "busier
  frame". Two dead ends worth not repeating: STK's own `--profile-laps`/`--profile-time` summary looks
  authoritative but its `Number of frames` is incremented in `ProfileWorld::update(int ticks)` —
  "number of physics time steps" per its own comment — so its "Average FPS" is the **physics tick
  rate** and barely moves however slowly you render; and a flip counter *sampled over a window* has
  the same scene confound unless the workload is fixed.
  ✅ **Fixed at the right layer** (`phoenix-rtos-devices` `27170e1`): `v3d_phoenix_flip()` is the single
  point every GL and Vulkan app presents through, so it now counts frames actually scanned out and
  prints `flipstat N frames in T ms = X.XX fps` on the **UART** — no screenshot, no OCR, same units
  for every app, `V3D_FLIPSTAT=0` to silence. Verified against a known 30 Hz source *before* it went
  near the target: reads 30.20–31.18 fps. That test paid for itself — the first cut computed centi-fps
  with 1e5 instead of 1e8 and reported `0.03 fps` for a measured 30.
  ⓘ Silence from it means **single-buffer (blit-resolve) mode**, not a stalled app; and the glamor X
  server presents by readback into `/dev/fb0`, so it does not appear there either. Both noted in the
  source.
