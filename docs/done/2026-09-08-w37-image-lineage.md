# W37 image lineage (archived from the weekly log)


## Delta of image 486acda5 vs 0ddcee67 (superseded by 23f46860)

**What changed vs the 21-boot-gated image** (`0ddcee67…`, now superseded at that path):
**exactly 4 artifacts**, each HW-verified, and the image's copies are byte-identical to the
tested ones — `Xphoenix-glamor-daemon` (the #4 fix; 4 trials, 0 faults, desktop
pixel-verified), the xlaunch launcher ×3 copies (adds the test-only `--quit-after`), and
rebuilt `python3` + `micropython` (both verified on HW this session). **All other 183 files in
`/bin`+`/sbin` are byte-identical** — same file sets, no additions or removals — so the five
game engines and their data keep their existing gate.



## §1b as it stood in the weekly log

## 1b. ✅ DONE (owner, 2026-09-08): full-project change study for Phoenix-RTOS maintainers

**Deliverable: [`docs/PHOENIX-RTOS-RPI4-CHANGES.md`](../PHOENIX-RTOS-RPI4-CHANGES.md)** (885 ln).
Opens with a 14-row ★ shortlist of defects in *upstream's* code that this port found, then four
stand-alone sections (kernel+plo+posixsrv · libphoenix+corelibs+tests · devices+USB+net+fs ·
ports+build), then caveats/open defects/licensing and a repo map. Scope stated honestly: ~1 540
commits / ~890 files / ~150k raw insertions, **minus** libphoenix `math/`+`libm/` (+28 472,
upstream's vendored libmcs) and `vkquake_shaders.c` (+30 506, generated SPIR-V) = **~90k
authored**. Built by four parallel subagents whose claims I spot-verified rather than trusted.

★ Best find, invisible to any single agent: kernel `5d8645f6` (pmap) and `usb/mem.c` `12c4fe8`
are **two instances of one class** — nothing retires cache lines when a cached page becomes
uncached DMA memory. Measured ~12 corrupt GPU control lists per 8 boots -> 0.

Reviewed for cross-section conflicts (`83b8579b3`): every multiply-cited commit stays inside one
section; the one real trap was the ports section listing the pthread 4 KiB stack / missing guard
page as an open gap while the shortlist calls it fixed — now cross-referenced both ways.

Method notes archived to `docs/done/2026-09-08-fork-vs-upstream-study.md`.



## STK clock-fix block as it stood in the weekly log

**★★ ROOT-CAUSED AND FIXED: STK was capped at exactly 1 fps by a broken C++ clock — now 5.84 fps
(5.8×).** `phoenix-rtos-ports 6f08c26`.

libstdc++ for aarch64-phoenix is built with **none** of its time backends
(`_GLIBCXX_USE_CLOCK_MONOTONIC` / `_REALTIME` / `GETTIMEOFDAY` / `NANOSLEEP` / `SCHED_YIELD` all
`#undef` in `c++config.h`), so `std::chrono::steady_clock` falls back to `std::time()` and ticks
in whole **seconds**. STK's frame loop sits in `while (dt == 0) { StkTime::sleep(1); … }`
(`main_loop.cpp:265`) waiting for it to move — one frame per second, whatever is on screen. The
patch reads `CLOCK_MONOTONIC` directly under `__phoenix__`.

Measured on HW (counter in the V3D winsys at the page-flip): **1000 ms/frame → ~171 ms/frame**,
exactly the ~173 ms the frame budget predicted (150 ms GPU + 23 ms physics). GPU time is
unchanged, so STK is now **GPU-bound** — ~88% of the frame is command-list submits.

**What found it: precision, not magnitude.** 1000.0 ms/frame within 0.08% across two tracks,
1080p, the whole deferred pipeline disabled and sound off — a workload doesn't hold that still
while the camera moves; a clock does. Everything else was measured and refuted first: render
passes (1.7%), uncached BO stores (new `tools/v3dmemprobe`: uncached is *faster* for scattered
stores, 28 vs 89 ns), the page-flip mailbox (1–9 ms per *32* frames), audio, kart count, scene
complexity. Also: the earlier race-clock metric was never "saturated" — it was correctly
reporting 1 fps.

**⚠️ YOUR CALL — the general fix.** This is a toolchain defect, not an STK bug: **any** C++ code
here timing with `std::chrono` gets 1-second granularity, and `sleep_for`/`yield` are degraded
too. STK was just where it was catastrophic; elsewhere it fails silently (a coarse timeout, a
rate limiter that does nothing). Proper fix = rebuild libstdc++ with `--enable-libstdcxx-time=rt`
— and note `steady_clock::now()` lives in `libstdc++.a`, so defining the macros in a header
changes nothing. That's a whole-system C++ rebuild, so I have NOT started it mid-demo-prep; the
per-port workaround is in. Detail: `docs/misc/2026-09-08-stk-frame-budget.md`.



## STK launcher + profile-crash block as it stood in the weekly log

**Fixed:** `stk-launcher` silently ignored every caller option (`697cd21a3`). It appended user
args after its own and assumed "later wins"; STK's `CommandLine` *rejects* a duplicate, and
rejects it **non-fatally**, so `--screensize=640x480` was accepted on the command line and the
game ran at 1080p anyway. HW-verified: `invalid_param=0`, STK logs "You choose to use 640x480."

**New crash, not on the demo path:** `--profile-time` prints its report, then takes a Data Abort
(EL0) formatting the per-kart table — `esr=0x92000044` (write, translation fault L0), `far` and
x4/x5 hold a 16-bit ascending sequence dereferenced as a pointer, with a stack-range line printed
just before. Stack exhaustion is the first suspect. `--no-sound` normal-mode run did not crash.



## §4e (STK time-to-race) as it stood in the weekly log

## 4e. STK time-to-race (2026-09-08, post clock fix)

Same deterministic 1325-tick lap throughout (includes load + shaders + race):

| config | **first** run of a boot | second run, same boot |
|---|---|---|
| full deferred pipeline | 55.5 · 55.6 · 56.3 · 50.0 · 47.3 (n=5) | 20.0 |
| `--disable-dynamic-lights --shadows=0` | 22.7 · 22.4 (n=2) | 21.8 |

**Two separate effects.** The deferred pipeline costs **~30 s of one-time, per-boot warm-up**
(first run only) and **nothing at steady state** (20.0 warm vs no-deferred's 21.8 — marginally
*faster*). So the flags buy time-to-race, not frame rate.

⚠️ **I over-corrected last turn.** I reported a "2.54× render win", then retracted it as an
ordering artifact after reversing the arms. The retraction was too strong: the ordering effect
and the flag effect are both real and different. No steady-state win, but a genuine **~2.4×**
improvement in time-to-race on a fresh boot — which is the case a demo take actually hits.

**RAM-staging STK's assets: measured, and it's a LOSS.** 4120 files / 137 MiB copied in **73.0 s**
(1.88 MiB/s, latency-bound on small files) to save 2.7 s. Net −70 s. It works for Quake (~47 MiB,
mostly read) but STK reads a small fraction of a 149 MiB 40-track library. **Corollary: the ~30 s
is not asset-read latency** — with the whole art root in RAM the run still took 47.3 s. Keep using
`ram-stage-play` for Quake; don't for STK.

**For a demo take:** best-looking → keep the deferred pipeline, accept a one-time ~50 s before the
first lap (trivially cut in editing, same steady frame rate). Quick take → add
`--disable-dynamic-lights --shadows=0`, 22 s on any boot. Detail + the still-open question of what
the ~30 s actually is: `docs/misc/2026-09-08-stk-time-to-race.md`.



## Queued-polish list as it stood in the weekly log

**Queued polish, deliberately not started:** `ncurses --enable-overwrite`; micropython
golden files; ~37 dead `/srv/phoenix-rpi4-nfs` references in legacy `tools/*/build.sh`
(live export is `-gcc16` — treat staging through them as unverified); **TD-21** syscall
order revert, due at the next full rebuild; mc's `/etc/mc/sfs.ini` not staged.