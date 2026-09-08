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

