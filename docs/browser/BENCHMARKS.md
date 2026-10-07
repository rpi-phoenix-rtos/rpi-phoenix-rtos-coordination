# Browser benchmarks: Speedometer 3.1, JetStream 2.2, MotionMark 1.3.2, Acid3, css3test

Owner request 2026-10-06: run Speedometer 3.1 and similar benchmarks on our browser (WPE WebKit
2.54, `/usr/bin/wpe-browser`, JIT on since build 35) and analyse performance, compatibility and
stability. Owner update the same day: **for now the benchmarks are test tools** while the browser
is stabilised and sped up. They must show stalls, hangs, crashes, page errors and failed subtests
clearly, and give per-subtest timings that show what is slow. A smoke mode runs after every build;
the full runs come later. The comparison with Chromium on Raspberry Pi OS is deferred (see the end).

Status: **suite staged and checked on the host; no Pi run yet.** The tables below are to be filled
in from Pi runs (the coordinator runs them).

## What is staged where

Everything is under the Pi's NFS root, `/srv/phoenix-rpi4-nfs-gcc16/usr/share/browser-bench/` on the
host (the Pi sees `/usr/share/browser-bench/`). It is test data, not part of an image, and no core
repo was changed. Sources and pins: `tools/browser/bench/stage.sh`. Clones are kept in
`external/browser-bench/` (git-ignored). Licences: `tools/browser/bench/LICENSES.txt`, copied to the
export.

| Benchmark | Version (pinned) | Licence | On the export |
|---|---|---|---|
| Speedometer | 3.1, WebKit/Speedometer `1386415b` (branch `release/3.1`) | BSD-2-Clause; the demo apps keep their own (TodoMVC MIT, ...) | `speedometer-3.1/` (+ `bench.html`) |
| JetStream | 2.2, WebKit/JetStream `332d8ee1` (branch `JetStream2.2`) | no top-level file: BSD-2 driver and Apple benchmarks; Octane/LuaJSFight BSD-3, ARES-6 MIT, SunSpider MPL tri-licence, SeaMonster MIT, cdjs BSD-style, web-tooling-benchmark BSD-3 | `jetstream-2.2/` (+ `bench.html`) |
| MotionMark | 1.3.2, WebKit/MotionMark `0e740d50` (tag `release/MotionMark1.3.2`) | BSD-2-Clause | `motionmark-1.3.2/MotionMark/` (+ `bench.html`) |
| Acid3 | acid3.acidtests.org, 17 files downloaded 2026-10-06 (sha256: `tools/browser/bench/acid3.sha256`) | no licence statement found; kept on the local export only | `acid3/` (+ `.phx-headers.json`) |
| The CSS3 Test | LeaVerou/css3test `bc9731cf` (2025-08-21, the last flat static version) | MIT | `css3test/` (analytics, ad scripts and one remote background removed: marked in the files) |

Why these two compatibility pages: **Acid3** gives a 100-point score for DOM, CSS selectors, SVG,
SMIL, events and the parser's error handling. It also needs exact server behaviour (a deliberate
404, PNGs served as `text/html`, `text/xml` for the XHTML files); `serve.py` reproduces that from
`acid3/.phx-headers.json`. **css3test** shows which CSS features the engine recognises
(about 6400 tests over 140 specifications, per-specification percentages). Both are static, run
offline in seconds, and score the same on every engine, so a gap points at our build (a missing
feature flag, an old WebKit) and not at the hardware. A curated web-platform-tests subset (DOM,
ES modules, fetch, workers) was considered. It was deferred because WPT's `.any.js`/`.window.js`
tests and absolute `/resources/` paths need wptserve or a generator. It is the next step if
these two leave questions open.

The suite's own files (BSD-3-Clause; sources in `tools/browser/bench/` of the coordination repo):

| File (export) | Source | What |
|---|---|---|
| `bench.sh` | `pi/bench.sh` | the Pi runner (below) |
| `phx/bench-common.js` | `hooks/` | the result channel (below) |
| `phx/speedometer-hook.mjs`, `phx/jetstream-hook.js`, `phx/motionmark-hook.js` | `hooks/` | wrap the benchmarks' own client callbacks. Each benchmark's `bench.html` is a copy of its `index.html` with two `<script>` lines added after a marker comment; `index.html` is untouched |
| `phx/acid3.html`, `phx/css3test.html` | `hooks/` | run the unmodified test in a same-origin frame and read its result |
| `phx/jetstream-ab.list`, `phx/speedometer-suites.list` | `jetstream-ab.list`, generated | the JetStream subset; Speedometer's 20 default suites |
| `tools/serve.py` | `serve.py` | the HTTP server (Python stdlib; host or Pi) |
| `index.html`, `VERSIONS.txt`, `LICENSES.txt` | `pi/index.html`, generated | a menu for manual runs; versions and hook checksum |
| `results/logs/<run>.log` | written by the Pi | each run's complete browser output (world-writable directory) |

Host-side tools: `parse-bench-log.py` (what a log says), `host/serve-for-pi.sh` (the server for
the Pi), `host/run-host-baseline.sh` + `host/pw-run.mjs` + `host/setup-host-tools.sh` (the host
baseline with Playwright).

## How it works

**Served over HTTP, not `file://`.** The launcher does not enable file-URL universal access, so
each `file://` document is its own origin. That breaks all of these benchmarks. Speedometer and
MotionMark drive their frames through `contentWindow`, JetStream `fetch()`es its sources (and
refuses to run on one load error), and Acid3 needs status codes and MIME types. `serve.py` is
used everywhere:

- **default: on the netboot host**, `http://10.42.0.1:8090` (`host/serve-for-pi.sh start`). It
  serves straight from the export, so the Pi's CPU stays with the browser, and it writes the
  pages' JSON reports to `artifacts/browser-bench/pi-results/`.
- `server=pi`: `/bin/python3` on the Pi at `127.0.0.1:8090`. Reports go to
  `/usr/share/browser-bench/results/`. This is not tried yet: it needs lwip's loopback.

**Three result channels**, because none is reliable alone on the Pi:

1. `console.log("BENCH <bench> <run> <seq> <kind> <text>")`: wpe-browser prints console messages
   in window mode (`<url>:<line>:<col>: CONSOLE LOG ...`). In headless mode none have reached the
   UART (b35 gate: the B9 page's console lines are missing, its title is there).
2. `document.title`, the same strings one by one (a 25 ms gap; the sequence number keeps
   consecutive titles distinct). The launcher logs every title change (`WPEB t=<ms> title ...`) in
   both modes. The last title is always
   `BENCH-DONE <bench> score=<x> status=<ok|partial|error> ... run=<run> t=<s>`.
3. `POST /phx-report?bench=&run=&part=final` with the full JSON (JetStream also POSTs its own
   `?report=true` result to `/report`). The server writes `<stamp>-<run>-<bench>-<part>.json`.

Lines stay under 700 characters (the launcher's line buffer is 1 KiB, and the console prefix
holds the URL). JSON goes out in 450-character chunks (`json <part> k/n ...`), and
`parse-bench-log.py` joins them again. The run id (`?run=`, set by bench.sh) is in every line,
so a line replayed by the UART capture from an earlier cycle is never counted for this run.

**Diagnostics per benchmark** (the owner's priority). Every progress line carries `t=<s>` since
the page started:

| Bench | Progress (hang attribution) | Per-subtest timing | Errors |
|---|---|---|---|
| Speedometer | `suite iter=I <suite>`, `step iter=I <suite>/<step>` before each of the ~80 steps | `step-done ... wall_ms=` (console); after each iteration `timing iter=I <suite>/<step> sync_ms= async_ms=` (console) and `iteration I total_ms= geomean_ms= score=`; at the end `suite-result <suite> mean_ms=` and `json metrics` (every metric: mean, ±, values) | `error during=<suite>/<step> <stack>` + `BENCH-DONE ... status=error failed_at=`; `page-error`, `page-rejection` |
| JetStream | `test-start i/N <name>` | `test i/N <name> score= first= worst= avg= wall_s=` | `test-error i/N <name> <stack>`, `test-timeout` (no result in `phx-test-timeout`, default 900 s). The stock driver stops at the first failure; the hook logs it and **goes on**, and the score is then `partial` (geomean of what finished) |
| MotionMark | `test-start MotionMark/<test>` | `test <test> score= low= high=` | `page-error` |
| Acid3 | `test NN score=` every 10 tests | Acid3's own "took N ms" (`slow count=`) | `fail NN <Acid3's message>` for each failed test |
| css3test | — (synchronous, about 1 s) | `spec <id> <percent>` (console); per-feature `failures` in the POSTed JSON (`css3test-diff.py`) | — |

Every page reports `env wasm= workers= sab= webgl= webgpu= audio= cores= view= dpr= ua=`.
**WebAssembly is off at run time on Phoenix** (patch 0013: `useWasm=false`, so there is no
`WebAssembly` global). JetStream itself then leaves out its 5 wasm benchmarks (HashSet-wasm,
tsf-wasm, quicksort-wasm, gcc-loops-wasm, richards-wasm). The hook says so (`skip <name>
reason=no-wasm`) and reports `js_score`, the geomean without `*-wasm`. Compare that with other
browsers, not the 64-benchmark score.

## bench.sh: the Pi runner

```
/bin/bash /usr/share/browser-bench/bench.sh <mode> [arm[,arm...]] [key=value...]
```

| Mode | Runs | Use |
|---|---|---|
| `smoke` | speedometer1 (1 iteration, all 20 suites), jetstream-ab (16 benchmarks), acid3, css3test | **after every build** (target < 10 min, see the estimates) |
| `speedometer1` / `speedometer` | Speedometer 3.1, 1 / 10 (official) iterations; `iter=N` | |
| `speedometer-suites` | the 20 suites one by one, one browser each | which suite hangs, crashes or throws (a stock run stops at the first) |
| `jetstream-ab` / `jetstream` | the subset / all of JetStream 2.2 (minus wasm) | |
| `motionmark` / `motionmark-quick` | 30 s (official) / 10 s per test (10 s is the shortest that works: 3 s breaks the ramp) | window mode only |
| `compat` | acid3, css3test | |
| `all` | compat, speedometer, jetstream, motionmark | the full report |

Arms (comma list; every run of the mode once per arm): `jit` (default) / `nojit`
(`JSC_useJIT=false`), `cpu` (default, `--cpu-rendering`) / `gpu` (Skia Ganesh), `shm` (default) /
`dmabuf` (`--dmabuf`), `headless`. Join them with `-`, e.g. `jit,nojit` or
`jit-cpu-shm,jit-gpu-dmabuf`. Options: `iter=N`, `timeout=S` (every run), `stall=S` (the browser's
`--stall-secs`, default 60), `quiet=1` (Speedometer without per-step lines), `hold=S`,
`base=URL` / `server=pi`, `env=NAME=VALUE` (repeatable; e.g. `env=JSC_jitMemoryReservationSize=67108864`,
`env=JSC_useFTLJIT=false`).

The runs, with the same structure as b6.sh/b7.sh: window-mode runs go in one XFCE session
(`XFCE_AUTOSTART=/bin/bash=bench.sh`). The script logs the session out itself when it is done;
`HOLD` is only the upper limit (the sum of the run limits + 300 s). Headless runs go first, at
psh. Each run is
`wpe-browser --size=1280x800 --toolbar=never --ephemeral --stall-secs=60 --hang-recovery=0
[--cpu-rendering] [--dmabuf] [--headless] <url>`, with its output going through `tee` to
`results/logs/<run>.log`. `--hang-recovery=0` keeps a hang visible instead of reloading the page.
Every 5 s the script checks that log:

- `title BENCH-DONE <bench> ... run=<id>` → **DONE**
- `web-process-terminated` → **CRASHED**: `BENCH <bench> CRASHED run= reason=crashed|memory-limit last=<the last progress line>`
- the browser exited by itself → **EXITED** (`rc=`)
- the run's limit is reached → **HUNG**: `BENCH <bench> HUNG run= after_s= last=<the last progress line>`,
  e.g. `last=331 step iter=2 Perf-Dashboard/SelectingPoints t=11.3` (from a host dry run). A main
  thread blocked for 60 s has already printed the launcher's `stall` report with threads and stack
  (`WPEB ... stall n= ...`, `stall-stack ...`; symbolise with addr2line, see the launcher's comment).
- every 60 s: `BENCH-SH alive id= t= temp_mC= last=<the last progress line>`. The UART is never
  quiet for long, and each heartbeat says where the run is.

Then SIGTERM (SIGKILL after 12 s) and one summary line, repeated at the end of the command:

```
BENCH-SUM bench=speedometer page=speedometer1 arm=jit run=speedometer-speedometer1-jit-r101530x1234 result=DONE secs=412
          score=0.812 delta=0.000 status=ok iterations=1 official=0 temp_mC=52000/61000 throttled=0x00000000
```

**Checked on the host** with the shipped bench.sh, driven end to end with Playwright's WPE
MiniBrowser standing in for wpe-browser (`BENCH_BROWSER`/`BENCH_SUITE` exist only for that):
`smoke headless` DONE ×4 with the right summaries. A browser that stays up after BENCH-DONE (as
wpe-browser does) gets SIGTERM, then SIGKILL after 16 s, and the next run starts. One that ignores
SIGTERM and leaves a child holding the output pipe gives the `WARNING ... not waiting` line, and the
next run still starts. `timeout=10` gives `HUNG ... last=35 step iter=1
TodoMVC-React-Redux/CompletingAllItems t=7.8`. Liveness is taken from the log's
`BENCH-SH browser-exit` marker, not from `kill -0`. Not checked on the host: the XFCE session path
(the same autostart pattern as b6/b7, with logout through `$XFCE_LOGOUT_FLAG`), the console channel
of the real launcher, and `/dev/thermal`. Smoke run #1 on the Pi is the check for those.

## Commands for the coordinator (Pi cycles)

Before any Pi cycle: `tools/browser/bench/stage.sh --no-fetch` if the hooks changed (idempotent),
then `tools/browser/bench/host/serve-for-pi.sh start`. The pages' JSON lands in
`artifacts/browser-bench/pi-results/`, the per-run logs in
`/srv/phoenix-rpi4-nfs-gcc16/usr/share/browser-bench/results/logs/`. Afterwards:

```
python3 tools/browser/bench/parse-bench-log.py artifacts/rpi4b-uart/rpi4b-uart-<ts>-<label>.log
python3 tools/browser/bench/parse-bench-log.py /srv/phoenix-rpi4-nfs-gcc16/usr/share/browser-bench/results/logs/*<nonce>*.log --out artifacts/browser-bench/pi-<label>
```

The run logs on the export are complete (no UART corruption) and are the primary source. The
UART is the fallback.

`bench.sh` prints `BENCH-SH alive` every 60 s, so `--idle-secs 150` only ends the command
after bench.sh has returned to the prompt. `--max-cmd-secs` = the expected time × 2 + session
start-up. Everything here is longer than 10 minutes, so it runs with `run_in_background: true`
and a Bash `timeout` ≥ (150 + max-cmd-secs + 150 + 120) × 1000 ms. A `Monitor` on the cycle's
output with `grep --line-buffered -a -o -E 'BENCH-SUM.{0,200}|BENCH \S+ (HUNG|CRASHED|EXITED).{0,200}'`
wakes the coordinator per run.

| # | Command (psh) | Expected on the Pi | `--max-cmd-secs` | Bash `timeout` (ms) |
|---|---|---|---|---|
| 1 | `/bin/bash /usr/share/browser-bench/bench.sh smoke` | 8–9 min | 1500 | 1 920 000 |
| 2 | `/bin/bash /usr/share/browser-bench/bench.sh speedometer-suites` | 10–20 min | 2400 | 2 820 000 |
| 3 | `/bin/bash /usr/share/browser-bench/bench.sh speedometer1 jit,nojit` | 3 + 3.5 min | 1500 | 1 920 000 |
| 4 | `/bin/bash /usr/share/browser-bench/bench.sh jetstream-ab jit,nojit` | 4 + 35 min | 4800 | 5 220 000 |
| 5 | `/bin/bash /usr/share/browser-bench/bench.sh speedometer1 jit-cpu-shm,jit-gpu-shm,jit-gpu-dmabuf` | 3 × 3 min | 1500 | 1 920 000 |
| 6 | `/bin/bash /usr/share/browser-bench/bench.sh speedometer quiet=1` (official 10 iterations) | 10–17 min | 3000 | 3 420 000 |
| 7 | `/bin/bash /usr/share/browser-bench/bench.sh jetstream` | 17–30 min | 3600 | 4 020 000 |
| 8 | `/bin/bash /usr/share/browser-bench/bench.sh motionmark` | 6–8 min | 1200 | 1 620 000 |

Cycle form (netboot):
```
./scripts/netboot-server-up.sh
./scripts/test-cycle-psh-interact.sh --label bench-smoke --inter-cmd-secs 8 --idle-secs 150 \
    --max-cmd-secs 1500 -- "/bin/bash /usr/share/browser-bench/bench.sh smoke"
```
Order: #1 first, as the pipeline check on the Pi (window mode, console channel, session logout,
`/dev/thermal`). #2 next if #1 shows a Speedometer error or hang, otherwise #3/#4 (JIT A/B), then
#5. #6–#8 later, for the performance report. Every row is over the 10-minute foreground
limit: run each in the background, one per cycle.

## Pre-registration (written before any Pi run)

**The Pi/host ratio.** The B9 page (fixed JS workload) measures **1136 ms (JIT) / 2374 ms (LLInt)**
on the Pi (build 35, gate2), and **96 / 200 ms** on the host in Playwright's WPE (WebKit 26.6). Both
give a **Pi ≈ 12× host** ratio. SunSpider in the jsc shell was 1.3 s/pass with the JIT. These
estimates take Pi time ≈ 12 × the host-WebKit time for JS-bound work, and 12–20× for the
DOM/layout/paint-bound Speedometer (CPU raster, a slower memory system).

**Host baseline** (2026-10-06, AMD Ryzen 7 PRO 250, 16 threads, Linux 7.0; Playwright 1.63
headless; same staged copies, hooks and server; `artifacts/browser-bench/host-20261006/`). A
second agent's build was running on the host at times: the load is recorded per run in
`summary.txt`. These are context, not a tuned reference.

| Run | Host WPE (WebKit 26.6) | Host WPE, `JSC_useJIT=false` | Host Chromium 153 | Page time, WPE (JIT) |
|---|---|---|---|---|
| B9 page (ms, lower is better) | 96 | 200 | 172.5 | <1 s |
| Speedometer 3.1, 1 iteration | 9.66 | 6.81 / 6.65 (2 runs) | 20.18 | 7.1 s |
| Speedometer 3.1, 10 iterations (official) | **11.96 ± 0.76** | — | **24.56 ± 1.23** | 46.6 s |
| JetStream 2.2 ab subset (16), `js_score` | 629.6 | 55.4 | 608.7 | 12.8 s (LLInt 157.6 s) |
| JetStream 2.2 full: score (64) / `js_score` (59, no wasm) | 239.6 / **246.2** | — | 289.1 / **301.9** | 84.5 s |
| Acid3 | 96/100 (fails 22, 23, 25, 35) | — | 96/100 | 1.5 s |
| css3test | 71 % (4349/6419) | — | 72 % (4453/6419) | 2.0 s |

Not run on the host: MotionMark. A headless browser's frame pacing says nothing about the Pi.
The Playwright run (headless, 10 s tests) only showed that the hook works: score 1551 on Chromium,
and 3 s tests break MotionMark's ramp. Firefox 155 (Playwright): css3test 64 %. One run each, not
repeated. JetStream-ab with 17 benchmarks (`float-mm.c` included, before it was dropped from the
list): WPE 548.4 JIT / 46.9 LLInt. `float-mm.c` alone took 326 of the LLInt run's 463 s, so it is
out of the subset.

**Expected on the Pi (JIT, CPU raster, shm), and what each estimate rests on:**

| Run | Pi time (estimate) | Pi score (estimate) | Basis |
|---|---|---|---|
| speedometer1, JIT | 2–3 min incl. start | 0.5–0.8 | host WPE 9.66 / 12–20 |
| speedometer1, LLInt | 2.5–3.5 min | 0.35–0.55 | host LLInt 6.7 / 12–20 |
| speedometer (10 it.) | 10–17 min | 0.6–1.0 | host WPE 11.96 / 12–20 |
| speedometer-suites | 20 × 0.5–1 min ≈ 10–20 min | per suite | 20 browser starts + 1 suite each |
| jetstream-ab, JIT | 3–4 min | `js_score` 35–65 | host 629.6 / 12 = 52 |
| jetstream-ab, LLInt | 30–40 min | `js_score` 3–6 | host 55.4 / 12 = 4.6; 157.6 s × 12 |
| jetstream full | 17–30 min | `js_score` 15–25 | host 246.2 / 12 = 20.5; 84.5 s × 12 |
| acid3 / css3test | < 1 min each | 96 / 65–71 % | host ×12 is seconds; page start dominates |
| motionmark | 6–8 min | unknown (frame-rate target likely 30 or 15 on the Pi) | 8 tests × (30 s + warm-up), fixed |
| **smoke** | **8–9 min** | | session start ~1 min + the four runs + ~20 s each to start and stop |

These figures come from one host run under varying load. They are meant to size timeouts and to
show when the Pi is far off (e.g. JetStream 10× below the estimate means look for the cause: JIT
pool exhaustion, GC, the 4 kB AF_UNIX buffer on IPC...). They are not targets.

**Hypotheses to check** (each says what result would refute it):

1. Speedometer completes all 20 suites with no `error`. *Risk*: suites that use features our
   build lacks or has never run. The Perf-Dashboard and chart suites use SVG/canvas heavily; the
   NewsSite and Editor suites use large frameworks. A `status=error failed_at=` names the step.
2. **The 32 MiB JIT pool** (patch 0013) is enough. *Risk*: Speedometer and JetStream generate far
   more code than B9. Pool exhaustion shows as a JIT run slower than expected or no faster than
   `nojit` for later suites/benchmarks, or a crash. Arm to separate it:
   `env=JSC_jitMemoryReservationSize=67108864`.
3. JIT/LLInt: B9 measured 2.09× on the Pi and 2.08× on the host (B9 is short: the JIT
   barely warms up). On the host, JetStream-ab is **11.4×** and Speedometer-1 **1.42–1.45×**.
   Expect the Pi to show about the same: ≥ 6× on JetStream-ab and 1.3–1.6× on Speedometer.
   A JetStream-ab ratio near 2× would mean the DFG/FTL tiers do little on Phoenix (tier A/B:
   `env=JSC_useFTLJIT=false`, `env=JSC_useDFGJIT=false`).
4. JetStream's worker benchmarks (`bomb-workers`, `segmentation`) run. They have never been
   tried on Phoenix: a `test-error`/`test-timeout` is a finding, not a harness fault.
5. Acid3: the host's WPE (WebKit 26.6) and Chromium 153 both score 96/100 (WPE fails 22, 23,
   25, 35: spec changes since 2008 that modern engines share). The distribution's WebKitGTK
   2.52.6 scored 94 in a first try under GTK's broadway backend (fails 23, 25, 35, 46, 72, 77).
   That display reported a negative viewport, so 46/72/77 (text-transform, SVG text and fonts)
   may be display artifacts rather than engine facts. Expect 94–96 on the Pi. A lower score points at our build
   (SVG fonts, `font.ttf` via `@font-face`, XML parsing). `slow count` will be high on the Pi
   (Acid3 counts every test over 33 ms) and is not a failure.
6. css3test: the host's WPE (WebKit 26.6) 71 %, Chromium 72 %, Firefox 64 %. WebKit 2.54 is
   older, so expect 65–71 %. Per-specification gaps point at feature flags off in our build.
7. Raster/transport (#5): GPU raster ≥ CPU raster on Speedometer only if painting dominates. The
   B7 work found WebGL not presenting; GPU raster itself rendered B7-anim. A crash in the gpu arm
   is a B7 finding.
8. Thermal: 4 cores at 100 % for 30+ minutes may throttle the Pi 4 (`throttled` ≠ 0, or
   `temp_mC` > 80 000). A full run with throttling is marked as such, not compared.

## Results (Pi runs)

### Runs

| Date | Build | Mode / arm | Result | Duration | Score | temp before/after, throttled | Log |
|---|---|---|---|---|---|---|---|
| 10-07 | 38 | smoke / jit: speedometer1 | DONE | 284 s | **0.377** | 49.1/59.4 °C, 0 | `rpi4b-uart-20261007-003040-bench-smoke.log` |
| 10-07 | 38 | smoke / jit: jetstream-ab | DONE, 16/16 ran, 0 failed | 255 s | `js_score` **32.3** | 58.4/63.7 °C, 0 | same |
| 10-07 | 38 | smoke / jit: acid3 | DONE | 24 s | **96/100** | 64.2 °C, 0 | same |
| 10-07 | 38 | smoke / jit: css3test | DONE | 35 s | **69 %** (4181/6419) | 61.8 °C, 0 | same |

| 10-07 | 41 | smoke / jit: speedometer1 | DONE | 123 s | **0.432** | 56.0/60.3 °C, 0 | `rpi4b-uart-*-b41-gate.log` |
| 10-07 | 41 | smoke / jit: jetstream-ab | DONE, 16/16 | 266 s | `js_score` 31.3 | 59.9/64.7 °C, 0 | same |
| 10-07 | 41 | smoke / jit: acid3, css3test | DONE | 24 s, 35 s | 96, 69 % | 0 | same |

| 10-07 | 43 | smoke / jit: speedometer1 | DONE | 51 s | **1.159** | 57.4/61.3 °C, 0 | `rpi4b-uart-20261007-045202-b43c.log` |
| 10-07 | 43 | smoke / jit: jetstream-ab | DONE, 16/16 | 91 s | `js_score` **76.8** | 59.9/62.8 °C, 0 | same |
| 10-07 | 43 | smoke / jit: acid3, css3test | DONE | 8 s, 13 s | 96, 69 % | 0 | same |

**Build 43 (syscall-free `pthread_getspecific`/`pthread_self`, page cache across exit, futex locks in
lwIP/posixsrv): Speedometer 0.432 → 1.159 (2.7×), JetStream-ab 31.3 → 76.8 (2.5×).** The
allocation/GC-heavy JetStream tests gained most (string-unpack 7.1×, json-parse 4.4×, Babylon 3.6×,
splay 3.2×): build 42's profile had the WebProcess main thread spending 60 % of a CPU in the
kernel, 90 % of it in the global mutex of `pthread_getspecific` (JSC `Thread::current()`). The
Pi/host ratio is now **6–13× on 15 of 16 JetStream tests** (hash-map 21×) and 5–12× on the
Speedometer suites — the plain CPU ratio the B9 page predicted.

| 10-07 | 43 | jetstream (full, no wasm) / jit | DONE, 57/59 ran, **2 wrong results** | 566 s | `js_score` **32.95** (host 246.2: 7.5×) | 53.0/65.7 °C, 0 | `rpi4b-uart-20261007-050905-b43-jsfull.log` |
| 10-07 | 43 | motionmark-quick / jit | DONE (10 s tests: not a valid score) | 193 s | 2.50 @ 30 fps | 0 | same |

| 10-07 | 45 | smoke / jit | DONE ×4 | 55 + 87 + 8 + 12 s | Speedometer **1.165**, JetStream-ab **76.4**, Acid3 96, css3test 69 % | 54.5–63.3 °C, 0 | `rpi4b-uart-*-b45-smoke.log` |

| 10-07 | 49 | smoke (GPU raster + dma-buf default, quiet) | DONE ×4 | 45 + 87 + 7 + 13 s | **Speedometer 1.281, JetStream-ab 79.7**, Acid3 96, **css3test 70 %** (4324/6419: corner-shape, object-view-box, ident() enabled) | 57–63 °C, 0 | `rpi4b-uart-*-b49-gate.log` |

**Raster × transport A/B on build 47** (`b47-arms`, Speedometer 1 iteration `quiet=1`, MotionMark 10 s tests):

| arm | Speedometer | MotionMark-quick |
|---|---|---|
| CPU raster, shared memory (the bench default until now) | 1.168 | 4.96 @30 |
| CPU raster, dma-buf | 1.238 | — |
| GPU raster, shared memory | 1.225 | — |
| **GPU raster, dma-buf** | **1.256** | **40.87 @45** |

GPU raster + dma-buf is now the default of both `/bin/browser` and `bench.sh` (arms `cpu`/`shm` opt out). Results before 2026-10-07 with the bare arm `jit` were CPU raster + shared memory.

**Profiles on build 47** (`b47-prof2`, `prof record` under the run): *hash-map* (still 21× the host) runs in JIT-generated code (top PCs outside the static text), with the WebProcess in the kernel only 1.1 % of all CPU — no OS cost left there. *Speedometer* (1.090 under the profiler): the main thread is spread over ordinary user code (`malloc` 2.5 % top) with the system 55 % idle (single-thread-bound); the visible remaining costs are the compositor thread's CPU copies of tiled GPU memory (`v3d_load_utile`/`v3d_store_utile`/`memcpy`, ~10 % of a CPU), the UART console printing the bench's own progress lines (`pl011-tty` 9 % of a CPU — use `quiet=1` for scoring), and posixsrv pipe traffic (6.5 % of all CPU; kernel-native pipes would remove it). lwIP spends ~30 % of one thread in software checksums (`lwip_standard_chksum`) during NFS traffic — GENET checksum offload is a candidate.

✅ (build 45) the two Stanford crypto tests now pass (scores 106 / 104): libphoenix's libm is FreeBSD msun.

⚠ **Correctness** (build 43): `stanford-crypto-sha256` ("Bad result") and `stanford-crypto-pbkdf2` ("Bad output")
compute wrong values on the Pi (both pass on the host). Being bisected by JIT tier.

Build 38's smoke took 13.7 min (estimate 8–9). No hang, no crash, no fault, no throttling in either.
**Build 41 (posixsrv pipes wake `poll()` through `pollNotify`): Perf-Dashboard 86.1 s → 2.6 s
(SelectingRange 84.7 s → 1.1 s), Speedometer 0.377 → 0.432; the other 19 suites within ±10 %.**
The 84 s were ~4 200 `window.screenX` reads, each a synchronous IPC to the UI process that waited a
20 ms pipe poll quantum (`ipc-rtt.html`: 23 ms → 3 ms per round trip).

### Stability: hangs, crashes, errors

| Run | Bench | What | Where (last progress line / failed_at / test) | Launcher stall report? | Follow-up |
|---|---|---|---|---|---|
| smoke 10-07 | speedometer1 | **84.5 s in one step** (host 0.13 s: 661×; every other step ≤ 36×) | Perf-Dashboard/SelectingRange, Sync part (84 534 ms) | yes: main thread in `waitForSyncReply` ← `windowRect` ← `screenX` | ✅ fixed build 41 (posixsrv pipe `pollNotify`): 1.1 s |
| smoke 10-07 | all | one `GLib-CRITICAL g_bytes_get_data: assertion 'bytes != NULL' failed` in the WebProcess | during the run (UART line 708) | — | find the caller |
| smoke 10-07 | speedometer1 / css3test | 116 of 244 and 140 of 160 progress lines not seen | console lines lost (UART flood; the POSTed JSON is complete) | — | harness: rely on the POST |

### Speedometer 3.1: per suite (mean ms per iteration) and slowest steps

| Suite | Pi JIT | Pi LLInt | Host WPE | Pi / host |
|---|---|---|---|---|
| Perf-Dashboard | 86133 | | 249 | **346×** |
| React-Stockcharts-SVG | 4843 | | 179 | 27× |
| Editor-TipTap | 3967 | | 284 | 14× |
| NewsSite-Next | 3618 | | 145 | 25× |
| TodoMVC-jQuery | 3551 | | 207 | 17× |
| TodoMVC-Angular-Complex-DOM | 3009 | | 92 | 33× |
| Charts-observable-plot | 2803 | | 119 | 24× |
| Charts-chartjs | 2750 | | 807 | 3× |
| NewsSite-Nuxt | 2656 | | 130 | 20× |
| TodoMVC-React-Redux | 2398 | | 109 | 22× |
| TodoMVC-React-Complex-DOM | 2275 | | 86 | 26× |
| TodoMVC-Backbone | 1945 | | 76 | 26× |
| TodoMVC-WebComponents | 1919 | | 36 | 53× |
| TodoMVC-Vue | 1805 | | 52 | 35× |
| TodoMVC-JavaScript-ES5 | 1710 | | 114 | 15× |
| TodoMVC-Lit-Complex-DOM | 1706 | | 90 | 19× |
| Editor-CodeMirror | 1462 | | 52 | 28× |
| TodoMVC-JavaScript-ES6-Webpack-Complex-DOM | 1260 | | 70 | 18× |
| TodoMVC-Preact-Complex-DOM | 1139 | | 34 | 34× |
| TodoMVC-Svelte-Complex-DOM | 1018 | | 27 | 38× |

Pi build 38, smoke, 1 iteration; host = the 1-iteration WPE run. All 20 suites completed.

Slowest steps (from `timing` / `json metrics`; `parse-bench-log.py` lists them):

| Step | Pi sync ms | Pi async ms | Host ms | Note |
|---|---|---|---|---|
| Perf-Dashboard/SelectingRange | 84534 | 126 | 128 | **661×: a stall, not slow code** |
| Editor-TipTap/Long | 2314 | 128 | 164 | 15× |
| TodoMVC-Angular-Complex-DOM/Adding100Items | 1869 | 211 | 57 | 36× |
| React-Stockcharts-SVG/ZoomTheChart | 1871 | 45 | 88 | 22× |
| TodoMVC-jQuery/CompletingAllItems | 1584 | 107 | 107 | 16× |
| React-Stockcharts-SVG/Render | 1526 | 160 | 52 | 32× |
| Editor-TipTap/Highlight | 1488 | 37 | 120 | 13× |
| Charts-chartjs/Draw scatter | 1343 | 24 | 391 | 3× (the host run is slow here) |

Without Perf-Dashboard the geomean of the other 19 suites is ~25× the host: twice the 12× the
B9 page predicted. Speedometer is DOM/layout/paint-bound, so the extra factor is outside the JS JIT
(see the JetStream split below).

### JetStream 2.2: per benchmark

| Benchmark | Pi JIT score | Pi LLInt score | wall s (Pi) | Host WPE score | Note |
|---|---|---|---|---|---|
| crypto | 180.3 | | 1.5 | 1667 | 9× |
| regex-dna-SP | 149.7 | | 3.2 | 1483 | 10× |
| navier-stokes | 134.0 | | 2.1 | 910 | 7× |
| richards | 90.4 | | 2.8 | 979 | 11× |
| UniPoker | 54.2 | | 6.5 | 882 | 16× |
| delta-blue | 52.1 | | 5.3 | 1016 | 20× |
| Basic | 36.1 | | 9.1 | 791 | 22× |
| 3d-cube-SP | 30.4 | | 15.2 | 570 | 19× |
| raytrace | 29.4 | | 11.8 | 842 | 29× |
| base64-SP | 24.1 | | 24.9 | 771 | 32× |
| hash-map | 22.5 | | 18.9 | 651 | 29× |
| splay | 18.8 | | 26.9 | 503 | 27× |
| Babylon | 15.3 | | 21.1 | 712 | 46× |
| string-unpack-code-SP | 12.3 | | 36.4 | 682 | 56× |
| json-parse-inspector | 11.3 | | 12.4 | 391 | 35× |
| acorn-wtb | 2.1 | | 25.6 | 63 | 29× |

**The split is the finding.** Compute-bound benchmarks that allocate little (crypto, regex-dna,
navier-stokes, richards) run at **7–11×** the host: the CPU ratio the B9 page predicted, so the JIT
tiers work. The allocation- and GC-heavy ones (splay, hash-map, string-unpack, Babylon, acorn,
json-parse, base64) run at **27–56×**. The extra 3–5× is in allocation, GC or the memory system
(page faults on fresh heap, `mmap`/`munmap`/`madvise` cost, the GC's threads), not in generated
code. Next: profile `splay` and `string-unpack-code-SP` with `prof record` (build 39).
Overall `js_score` 32.3 vs host 629.6 on these 16 (19×); the pre-registered estimate was 35–65. (Host per-benchmark scores are from the 17-benchmark host run, the only one with a per-benchmark report.)

Overall: `js_score` (JS only; 59 benchmarks full / 16 ab), `first` / `worst` / `average` geomeans.

### JIT vs LLInt

| Benchmark | JIT | LLInt | Ratio | Host ratio |
|---|---|---|---|---|
| B9 page (ms) | 1136 | 2374 | 2.09 | 2.08 |
| Speedometer 1 iteration (score) | | | | 1.42–1.45× |
| JetStream-ab (`js_score`) | | | | 11.4× |

### Raster and frame transport

| Arm | Speedometer1 score | MotionMark score @fps | Notes |
|---|---|---|---|
| jit-cpu-shm | | | |
| jit-gpu-shm | | | |
| jit-gpu-dmabuf | | | |
| jit-cpu-dmabuf | | | |

### Compatibility

| Test | Pi | Host WPE 26.6 | Host Chromium | Failures on the Pi only |
|---|---|---|---|---|
| Acid3 | 96/100 (fails 22, 23, 25, 35) | 96 | 96 | none: the same 4 as the host |
| css3test | 69 % (4181/6419) | 71 % | 72 % | 168 checks, attributed in [CSS3TEST-GAPS.md](CSS3TEST-GAPS.md): 147 are 3 features 2.54 ships off (now on in the launcher), the rest WebKit-version differences |
| JetStream benchmarks that ran | 16/16 (ab subset) | 64/64 | 64/64 | full run not yet done |
| Speedometer suites that completed | 20/20 | 20/20 | 20/20 | |

### MotionMark 1.3.2 (window mode only)

| Test | Pi score | fps target | Note |
|---|---|---|---|
| Multiply ... Suits (8 rows) | | | |

## Methodology

- **Versions**: `VERSIONS.txt` on the export (pins, stage time, the hooks' checksum); the
  browser's `WPEB ... start ... webkit=2.54.0` line; the build number from the manifest of the
  image under test.
- **Flags**: as in the bench.sh section; the arm is in every run id and summary line. Window
  mode is 1280×800, no toolbar (the pinned toolbar's user style sheet would shift layout),
  ephemeral session, `--stall-secs=60`, `--hang-recovery=0`.
- **Clocks**: the pages time with `performance.now()`, and the Pi's monotonic clock is the
  ARM generic timer. bench.sh durations come from bash `SECONDS`. The Pi's wall clock (NTP at
  boot) is only used for the run id.
- **Thermal**: `/dev/thermal` (milli-°C) and `/dev/throttled` (firmware throttle bits) before and
  after each run, in `BENCH-SUM`; a `-` means the device was not there. Leave ≥ 5 min idle
  between long runs; never compare runs whose `throttled` differs.
- **Repeats**: Speedometer reports its own ± over iterations. Arms that are compared run in the
  same boot, in alternating order when repeated. One run per arm is a smoke result, not a
  measurement.
- **What the hooks add**: one `document.title` change and one console line per progress line.
  In Speedometer that is about 2 per step, inside the measured async window. `quiet=1` removes
  the per-step lines for the official-score runs (#6).
- **Deviations from stock**: JetStream continues past a failed benchmark (stock: stops; score
  then `partial`); MotionMark's `phx-test-interval`/`phx-frame-rate` make a run `official=0`;
  Speedometer runs with `iterationCount≠10` or a suite filter are `official=0`. Only `official=1`
  scores are comparable with published numbers.

## Later: Chromium on Raspberry Pi OS (deferred by the owner)

For the final performance report, the fairest reference is Chromium on the same Pi. Netboot
Raspberry Pi OS from `artifacts/linux-netboot` (its NFS export is already in `/etc/exports`),
start `host/serve-for-pi.sh`, and open the same `bench.html` URLs at the same window size
(`chromium --window-size=1280,800 'http://10.42.0.1:8090/speedometer-3.1/bench.html?startAutomatically&run=pios-chromium-sp'`).
The reports land in `pi-results/` like ours. Compare `js_score` for JetStream (Chromium runs the
wasm benchmarks too).
