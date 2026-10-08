# Browser site survey

**Why:** the owner's goal is a "fully stable, working and performant browser". The B6 checks
(`b6.sh sites`/`soak`) use five sites. This survey loads about thirty real, popular and varied
websites, one after another, in the real windowed browser. For each site it records the result,
the load time, the title, crashes, hangs, JS console errors and memory, so that regressions and
site-specific failures show up as one table per build.

Files (coordination repo, `tools/browser/survey/`):

| File | Where it runs | What |
|---|---|---|
| `pi/survey.sh` | Pi, staged to `/usr/share/wpe-browser/survey.sh` | the harness: XFCE session, one browser per site, `SURVEY` lines |
| `pi/survey-sites.txt` | Pi, staged to `/usr/share/wpe-browser/survey-sites.txt` | the site list, `<name> <address>` per line |
| `stage.sh` | host | copies both onto the netboot export (`/srv/phoenix-rpi4-nfs-gcc16`) |
| `parse-survey.py` | host | UART log (+ the per-site logs on the export) → the markdown of [Results](#results) |

## Methodology

### One browser process per site

`survey.sh` runs inside an XFCE session, as its `XFCE_AUTOSTART` item, the way `b6.sh` and
`bench.sh` do. For each site it starts **a new `wpe-browser` process** on that address, waits for
it, and ends it. It does not use a single process with `--cycle` (b6 soak's model), for three
reasons:

1. **The launcher's per-load knobs act on the first load only.** `--snapshot`,
   `--exit-after-load` and `--timeout` all key on `firstLoadDone`. Per-site PNGs, exit codes and
   load limits therefore exist only per process.
2. **Isolation.** A site that wedges the UI, leaks memory or crashes cannot spoil the
   measurements of the sites after it. The ~1/16 UI start stall (PLAN B6) costs one site, not the
   rest of the survey.
3. **Attribution.** Every line between a site's `SURVEY-SITE` and `SURVEY` lines (WebKit's,
   the kernel's fault dumps) belongs to that site.

Navigating from site to site inside one process (process swap, the process cache, memory over
time) is what `b6.sh soak` checks. Run it next to the survey; it is not part of this harness.

### The browser's settings

The settings are real use, i.e. `/bin/browser`'s defaults:
- a 1280x960 window;
- Skia CPU raster (`--cpu-rendering`);
- frames to labwc as dma-bufs (`--dmabuf`);
- the toolbar shown;
- a persistent session (cookies, HTTP disk cache).

The harness adds:

| Option | Why |
|---|---|
| `--data-dir`/`--cache-dir` in `/root/survey/<run>/profile` | Empty at the start of each survey, kept from site to site. Every site loads cold, as for a new user, and runs stay comparable. With `profile=home` the survey uses the real `$HOME` profile instead, which is warm. A persistent session is also required for console messages: WebKit prints none for an ephemeral one. |
| `--snapshot=/root/survey/<run>/<NN>-<name>.png` | The PNG is painted once the first load has finished, then the browser exits by itself. The snapshot is `takeSnapshotLegacy` (the web process paints into a ShareableBitmap), so it does not depend on the frame transport. Note that window mode plus `--dmabuf` has not been snapshotted before this survey. If `snapshot-error` appears, run with `dmabuf=0`. |
| `--timeout=<limit>` | The launcher's own limit (exit 2). The harness's limit is `limit + 30` s, then SIGTERM, then SIGKILL after 16 s. A UI stuck before its main loop never runs the launcher's timer and ignores SIGTERM. |
| `--hang-recovery=0` | The launcher's recovery would terminate a wedged web process, and the harness would then report it as a crash (`web-process-terminated`). Disabled, the hang stays a hang. |
| `--stall-secs=60` | The browser's default of 10 s floods the UART on heavy pages. `bench.sh` uses 60 s too. |
| `--rss-secs=3` | Every process logs its memory footprint every 3 s. |

**Option `dwell=S`.** The page is kept S seconds after its load finished, then ended with
SIGTERM. This catches crashes and console errors that happen after the load, but this launcher
can only snapshot at the load, so there is no PNG. A launcher option `--snapshot-delay=S` would
give both; it is a possible follow-up.

### Per-site measurements

Each `SURVEY` line is computed on the Pi from the site's own log, `/root/survey/<run>/<NN>-<name>.log`,
which holds the browser's full output (also tee'd to the UART).

| Field | Source | Meaning |
|---|---|---|
| `result` | see Classification below | OK, ERROR, CRASH, HANG or TIMEOUT |
| `load_ms` | UI `WPEB t=` of the first `load started` → the first `load finished` | page load time, without browser start-up |
| `commit_ms` | `load started` → the first `load committed` | time to the first response being committed (first paint follows) |
| `start_ms` | the UI's `t=` at `load started` | browser start-up: display, network session, web process launch |
| `http` | the last `policy response status=` (main frame) before `load finished` | what the site answered. A 403, 429 or 503 is usually a bot wall |
| `console_errors` | lines matching `CONSOLE [SOURCE] [TYPE] ERROR` | every console error: JS, network, security (CSP), `console.error` (JSC `ConsoleClient::printConsoleMessage`) |
| `js_errors` | `CONSOLE JS ERROR` | uncaught exceptions and script errors |
| `console_msgs` | every `CONSOLE` line | including INFO/LOG/WARN |
| `webprocess_rss_kb` | the largest `mem role=web ... footprint_kb=` | the web process's peak footprint, from WTF `memoryFootprint()`: the anonymous pages of its map entries (accurate since kernel build 30; it once overcounted). Phoenix has no `/proc`, and `ps` shows only VMEM. Compare between sites and builds, not with Linux. |
| `sysmem_used_kb` | the largest `sysmem used_kb=` | the kernel's page allocator: RAM really in use, system-wide |
| `webprocs` | `role=web pid=` lines | web processes started (more than 1: a process swap, e.g. a cross-site redirect) |
| `stalls` | the first report of each `stall`, `start-stall`, `frame-stall`, `present-stall` and `ipc-stall` | a main loop, a start-up phase or the frame pipeline stuck for 60 s |
| `unresponsive` | `web-process responsive=0` | WebKit's verdict: 3 s without an answer to a message |
| `rc`, `end` | the browser's exit status; `none`/`dwell`/`limit` | how the run ended (`limit`: the harness killed it) |
| `reason` | | why it is not OK (below) |
| `snapshot` | `snapshot file=` | the PNG, or `error` |
| `title` | the last non-empty `title` | always the last field; it may contain spaces |

**Classification** (first match wins):
1. **CRASH**: `web-process-terminated` (reason `web-crashed` or `web-memory-limit`), or the UI
   process ended by itself with an exit status other than 0–3 (`ui-rc-N`, e.g. a fault).
2. **ERROR**: before the first `load finished`, a `load-failed` (other than a cancellation:
   reason = WebKit's error text) or a `load-failed-tls`. WebKit then loads an error page, which
   also reaches `load finished`, so this check comes before OK. A UI exit status of 1 without a
   load (`ui-rc-1`, e.g. no Wayland display) is an ERROR too.
3. **OK**: the first load finished. The HTTP status is reported separately: a 403 bot wall
   still counts as a working browser.
4. **HANG**, when no load finished:
   - `no-load-start`: no `load started` at all;
   - `ui-start-stall`: as above, and the UI reported a start-up stall;
   - `unresponsive`: the web process ended unresponsive;
   - `stalled`: stall reports.
5. **TIMEOUT**: still loading and responsive at the limit (`launcher-timeout` or `harness-limit`).

The host parser adds, from the UART window between `SURVEY-SITE` and `SURVEY`:
- **kernel fault dumps** (`Exception #`; EL0 dumps are printed twice and counted once);
- **the commonest console errors**, read from the per-site logs on the export when they are
  there.

### Site list

There are 32 sites in `pi/survey-sites.txt`, ordered from simple to heavy, so that the first
minutes also prove the harness:

- **simple:** the local start page, example.com, plain-HTTP CERN, Hacker News, Lobsters,
  kernel.org, w3.org, the Python docs, CNN lite, DuckDuckGo HTML;
- **documents and developer sites:** Wikipedia, the ArchWiki (behind an Anubis JS
  proof-of-work), MDN, GitHub, Stack Overflow, old Reddit, CSS-Tricks, caniuse;
- **news:** BBC, the Guardian, the NYT (paywall/consent), Reuters (bot protection);
- **search, shopping, media:** DuckDuckGo (JS), Bing, Google (EU consent wall), Amazon, eBay,
  IMDb, yr.no weather;
- **heavy:** apple.com, OpenStreetMap (tiles + canvas), YouTube (no MSE in this build).

None needs a login. A consent wall, paywall or bot check still counts as a valid load: the
`http` field and the title show what the site answered.

## Running it (coordinator)

1. Stage the files after any edit: `tools/browser/survey/stage.sh`. This was already done for
   the first version on 2026-10-07.
2. Run one cycle (netboot; the Pi needs internet through the host NAT). Every row below is
   longer than 10 minutes, so it runs in the background:
   ```
   ./scripts/netboot-server-up.sh
   ./scripts/test-cycle-psh-interact.sh --label survey --inter-cmd-secs 8 --idle-secs 150 \
       --max-cmd-secs 6300 -- "/bin/bash /usr/share/wpe-browser/survey.sh"
   ```
   - Bash `timeout`: 6 900 000 ms, with `run_in_background: true`.
   - `survey.sh` prints a line at least every 60 s (`SURVEY-ALIVE` while a site loads), so
     `--idle-secs 150` only ends the command after the script has returned to the prompt.
   - A `Monitor` with `grep --line-buffered -a -o -E 'SURVEY run=.{0,200}|SURVEY-SUM.{0,200}'`
     wakes the coordinator per site.
3. Parse the results:
   `python3 tools/browser/survey/parse-survey.py artifacts/rpi4b-uart/<log>`. The run defaults to
   the last one in the log, and the per-site logs are read from
   `/srv/phoenix-rpi4-nfs-gcc16/root/survey/<run>/`. Paste the output under Results.

**Check on the first run** (not covered by the host dry run, which used a fake browser):
- `SURVEY-SH selftest ok` right after `SURVEY-SH begin`. survey.sh runs its log analysis on a
  known log first, because the Pi's awk is busybox 1.27.2 on libphoenix's regex. On `FAIL` it
  stops before the session starts.
- Site 1's `WPEB … snapshot file=` line: snapshots in window mode with `--dmabuf` are new. On
  `snapshot-error`, use `dmabuf=0`.
- The `SURVEY` lines have `result=`. `webprocess_rss_kb=-` is normal for pages that finish
  before the first 3 s sample (`start`, `example`).
- A UI fault is classified `CRASH ui-rc-N` only if bash on Phoenix reports a status outside 0–3
  for it. That is unverified, but the parser's fault column shows the kernel's `Exception #`
  either way.
- `survey.sh` is hand-staged next to the port-installed `b6.sh`. After an image build or sync,
  check that it is still on the export, or re-run `stage.sh`. Installing it from
  `webkit_wpe/files/checks/` like `b6.sh` is a possible follow-up.

**Expected duration:**
- **About 25–40 min** for the 32 sites:
  - XFCE session start ~1–2 min;
  - per site, ~12 s of overhead (browser start ~2.5 s, snapshot and exit, the 5 s gap);
  - plus the load: 3–8 s for the simple sites, 10–30 s for the medium ones, 30–90 s for the
    heavy ones;
  - each failing site costs up to ~3 min (the 150 s limit, then SIGTERM/SIGKILL).
- **Worst case**, every site at its limit: the session's `HOLD` of
  32 × (120 + 60) + 300 = 6060 s (~100 min).

Variants:
- `only=wikipedia,github`: the named sites only;
- `from=19`: resume from site 19 after a broken run;
- `limit=180`: a slower build;
- `dwell=15`: post-load stability, no PNG;
- `dmabuf=0`: shared-memory frames;
- `gpu=1`: GPU raster;
- `env=JSC_useJIT=false`: the LLInt;
- `help`: the script's usage.

Outputs, on the export: `/srv/phoenix-rpi4-nfs-gcc16/root/survey/<run>/`, holding:
- `<NN>-<name>.log`, `<NN>-<name>.png`;
- `summary.txt` (the `SURVEY` lines);
- `sites.run`.

`/root/survey/LATEST` names the last run. The host's HDMI snapshots in `artifacts/hdmi/` show
whatever was on screen at each 25 s tick.

## Results

**Build 68, 2026-10-08** (zero-copy video, MSE, B10): **GPU raster 31/32 OK** (the one TIMEOUT is nytimes: committed and titled at 1.8 s, 0 stalls, never reached load-finished within 120 s; it took 25.6 s on build 52 and 36.9 s in the CPU run below) and **CPU raster 32/32 OK**. 0 kernel faults in either run.

### Run `s10081029x11243`

- Settings: `sites=32 limit=120 snap=1 dwell=0 size=1280x960 dmabuf=1 gpu=0 stall=60 rss=3 profile=fresh env=none temp_mC=53033`
- Network check: `date=2026-10-08T10:29:30Z clock=set https=301 http=200`
- Per-site logs and snapshots: `/srv/phoenix-rpi4-nfs-gcc16/root/survey/s10081029x11243` (on the Pi: `/root/survey/s10081029x11243/`)
- Result: **31/32 OK** (31 OK, 1 TIMEOUT, 0 CRASH, 0 HANG, 0 ERROR)
- Load time of the OK sites (`load started` → `load finished`): median 9.4 s, max 41.8 s
- Web process footprint (peak per site, WTF memoryFootprint): median 108 MB, max 406 MB
- Kernel fault dumps during the survey: 0

| # | site | result | load s | commit s | HTTP | JS err | console err | web MB | stalls | faults | snap | title |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | start | **OK** | 0.4 | 0.1 | 0 | 0 | 0 | – | 0 |  | png | – |
| 2 | example | **OK** | 1.5 | 0.7 | 200 | 0 | 0 | – | 0 |  | png | Example Domain |
| 3 | cern | **OK** | 0.3 | 0.1 | 200 | 0 | 0 | – | 0 |  | png | – |
| 4 | hackernews | **OK** | 1.8 | 1.0 | 200 | 0 | 0 | 68 | 0 |  | png | Hacker News |
| 5 | lobsters | **OK** | 5.3 | 0.9 | 200 | 0 | 2 | 76 | 0 |  | png | Lobsters |
| 6 | kernelorg | **OK** | 16.6 | 0.9 | 200 | 0 | 0 | 78 | 0 |  | png | The Linux Kernel Archives |
| 7 | w3c | **OK** | 33.2 | 5.3 | 200 | 0 | 0 | 89 | 0 |  | png | W3C |
| 8 | pythondocs | **OK** | 6.5 | 1.0 | 200 | 0 | 0 | 70 | 0 |  | png | The Python standard library — Python 3.14.8 documentation |
| 9 | cnnlite | **OK** | 2.2 | 0.8 | 200 | 0 | 0 | 56 | 0 |  | png | Breaking News, Latest News and Videos \| CNN |
| 10 | ddghtml | **OK** | 3.0 | 2.2 | 200 | 0 | 1 | 44 | 0 |  | png | raspberry pi at DuckDuckGo |
| 11 | wikipedia | **OK** | 13.1 | 1.2 | 200 | 0 | 0 | 172 | 0 |  | png | Raspberry Pi - Wikipedia |
| 12 | archwiki | **OK** | 12.1 | 3.7 | 200 | 0 | 0 | 71 | 0 |  | png | Code of Conduct \| Arch Linux Terms |
| 13 | mdn | **OK** | 14.1 | 0.6 | 200 | 0 | 1 | 108 | 0 |  | png | JavaScript \| MDN |
| 14 | github | **OK** | 9.4 | 1.3 | 200 | 0 | 1 | 153 | 0 |  | png | GitHub - phoenix-rtos/phoenix-rtos-kernel: Phoenix-RTOS mic… |
| 15 | stackoverflow | **OK** | 2.3 | 1.2 | 403 | 0 | 0 | 42 | 0 |  | png | Just a moment... |
| 16 | oldreddit | **OK** | 15.1 | 2.5 | 200 | 2 | 18 | 170 | 0 |  | png | Welcome to Reddit |
| 17 | csstricks | **OK** | 41.8 | 1.4 | 200 | 20 | 36 | 367 | 0 |  | png | A Complete Guide to CSS Flexbox \| CSS-Tricks |
| 18 | caniuse | **OK** | 13.1 | 1.7 | 200 | 0 | 0 | 138 | 0 |  | png | CSS Grid Layout (level 1) \| Can I use... Support tables fo… |
| 19 | bbc | **OK** | 14.6 | 0.8 | 200 | 1 | 2 | 213 | 0 |  | png | BBC News - Breaking news, video and the latest top stories … |
| 20 | guardian | **OK** | 4.5 | 1.1 | 200 | 0 | 1 | 112 | 0 |  | png | Latest news, sport and opinion from the Guardian |
| 21 | nytimes | **TIMEOUT** | – | 1.8 | 200 | 0 | 0 | 257 | 0 |  | - | The New York Times - Breaking News, US News, World News and… |
| 22 | reuters | **OK** | 25.3 | 1.1 | 200 | 0 | 1 | 259 | 0 |  | png | Reuters \| Breaking International News & Views |
| 23 | ddg | **OK** | 20.2 | 1.2 | 200 | 0 | 3 | 293 | 0 |  | png | raspberry pi at DuckDuckGo |
| 24 | bing | **OK** | 4.8 | 3.0 | 200 | 0 | 0 | 68 | 0 |  | png | raspberry pi - Search |
| 25 | google | **OK** | 2.2 | 1.3 | 200 | 0 | 0 | 59 | 0 |  | png | – |
| 26 | amazon | **OK** | 22.5 | 2.3 | 200 | 0 | 0 | 304 | 0 |  | png | Amazon.com: Raspberry Pi 4 Model B 2019 Quad Core 64 Bit Wi… |
| 27 | ebay | **OK** | 2.9 | 1.4 | 403 | 0 | 0 | 43 | 0 |  | png | Error Page \| eBay |
| 28 | imdb | **OK** | 1.9 | 1.0 | 202 | 1 | 1 | 42 | 0 |  | png | – |
| 29 | weather | **OK** | 11.3 | 1.8 | 200 | 0 | 0 | 80 | 0 |  | png | Yr - Warsaw - Long term forecast |
| 30 | apple | **OK** | 18.7 | 1.0 | 200 | 0 | 0 | 177 | 0 |  | png | Apple |
| 31 | osm | **OK** | 9.4 | 1.5 | 200 | 0 | 1 | 140 | 0 |  | png | OpenStreetMap |
| 32 | youtube | **OK** | 14.0 | 2.3 | 200 | 0 | 0 | 406 | 0 |  | png | YouTube |

**Problems** (not OK, HTTP ≥ 400, stall reports or kernel faults):

- **15 stackoverflow** (https://stackoverflow.com/questions/tagged/rtos): OK, reason `-`, end `none`, rc 0, HTTP 403, wall 7 s, stalls 0, unresponsive 0
- **21 nytimes** (https://www.nytimes.com/): TIMEOUT, reason `launcher-timeout`, end `none`, rc 2, HTTP 200, wall 124 s, stalls 0, unresponsive 0
- **27 ebay** (https://www.ebay.com/): OK, reason `-`, end `none`, rc 0, HTTP 403, wall 12 s, stalls 0, unresponsive 1

CPU raster, same build (the export's `survey.sh` was a stale copy defaulting to `gpu=0`; restaged):

### Run `s10081011x11890`

- Settings: `sites=32 limit=120 snap=1 dwell=0 size=1280x960 dmabuf=1 gpu=0 stall=60 rss=3 profile=fresh env=none temp_mC=38421`
- Network check: `date=2026-10-08T10:11:39Z clock=set https=301 http=200`
- Per-site logs and snapshots: `/srv/phoenix-rpi4-nfs-gcc16/root/survey/s10081011x11890` (on the Pi: `/root/survey/s10081011x11890/`)
- Result: **32/32 OK** (32 OK, 0 TIMEOUT, 0 CRASH, 0 HANG, 0 ERROR)
- Load time of the OK sites (`load started` → `load finished`): median 6.9 s, max 36.9 s
- Web process footprint (peak per site, WTF memoryFootprint): median 109 MB, max 500 MB
- Kernel fault dumps during the survey: 0

| # | site | result | load s | commit s | HTTP | JS err | console err | web MB | stalls | faults | snap | title |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | start | **OK** | 0.5 | 0.1 | 0 | 0 | 0 | – | 0 |  | png | Phoenix-RTOS Web Browser |
| 2 | example | **OK** | 1.3 | 0.5 | 200 | 0 | 0 | – | 0 |  | png | Example Domain |
| 3 | cern | **OK** | 0.2 | 0.1 | 200 | 0 | 0 | – | 0 |  | png | – |
| 4 | hackernews | **OK** | 1.8 | 1.0 | 200 | 0 | 0 | 64 | 0 |  | png | Hacker News |
| 5 | lobsters | **OK** | 5.4 | 0.9 | 200 | 0 | 2 | 65 | 0 |  | png | Lobsters |
| 6 | kernelorg | **OK** | 3.6 | 0.6 | 200 | 0 | 0 | 29 | 0 |  | png | The Linux Kernel Archives |
| 7 | w3c | **OK** | 6.9 | 0.9 | 200 | 0 | 0 | 56 | 0 |  | png | W3C |
| 8 | pythondocs | **OK** | 5.4 | 0.7 | 200 | 0 | 0 | 64 | 0 |  | png | The Python standard library — Python 3.14.8 documentation |
| 9 | cnnlite | **OK** | 2.0 | 0.8 | 200 | 0 | 0 | 67 | 0 |  | png | Breaking News, Latest News and Videos \| CNN |
| 10 | ddghtml | **OK** | 2.1 | 1.2 | 200 | 0 | 1 | 47 | 0 |  | png | raspberry pi at DuckDuckGo |
| 11 | wikipedia | **OK** | 13.3 | 1.3 | 200 | 0 | 0 | 201 | 0 |  | png | Raspberry Pi - Wikipedia |
| 12 | archwiki | **OK** | 2.9 | 1.1 | 200 | 0 | 0 | 29 | 0 |  | png | Code of Conduct \| Arch Linux Terms |
| 13 | mdn | **OK** | 11.9 | 0.8 | 200 | 0 | 1 | 86 | 0 |  | png | JavaScript \| MDN |
| 14 | github | **OK** | 9.0 | 1.6 | 200 | 0 | 1 | 120 | 0 |  | png | GitHub - phoenix-rtos/phoenix-rtos-kernel: Phoenix-RTOS mic… |
| 15 | stackoverflow | **OK** | 2.3 | 1.4 | 403 | 0 | 0 | 42 | 0 |  | png | Just a moment... |
| 16 | oldreddit | **OK** | 13.3 | 2.4 | 200 | 2 | 18 | 230 | 0 |  | png | Welcome to Reddit |
| 17 | csstricks | **OK** | 33.1 | 1.2 | 200 | 0 | 21 | 284 | 0 |  | png | A Complete Guide to CSS Flexbox \| CSS-Tricks |
| 18 | caniuse | **OK** | 13.2 | 1.7 | 200 | 0 | 0 | 152 | 0 |  | png | CSS Grid Layout (level 1) \| Can I use... Support tables fo… |
| 19 | bbc | **OK** | 14.6 | 0.8 | 200 | 1 | 2 | 213 | 0 |  | png | BBC News - Breaking news, video and the latest top stories … |
| 20 | guardian | **OK** | 4.7 | 1.3 | 200 | 0 | 1 | 109 | 0 |  | png | Latest news, sport and opinion from the Guardian |
| 21 | nytimes | **OK** | 36.9 | 2.2 | 200 | 0 | 0 | 247 | 0 |  | png | The New York Times - Breaking News, US News, World News and… |
| 22 | reuters | **OK** | 20.6 | 0.8 | 200 | 0 | 1 | 246 | 0 |  | png | Reuters \| Breaking International News & Views |
| 23 | ddg | **OK** | 17.2 | 0.8 | 200 | 0 | 3 | 271 | 0 |  | png | raspberry pi at DuckDuckGo |
| 24 | bing | **OK** | 3.4 | 1.8 | 200 | 0 | 0 | 60 | 0 |  | png | raspberry pi - Search |
| 25 | google | **OK** | 2.3 | 1.3 | 200 | 0 | 0 | 40 | 0 |  | png | – |
| 26 | amazon | **OK** | 22.0 | 1.5 | 200 | 0 | 0 | 319 | 0 |  | png | Amazon.com: Raspberry Pi 4 Model B 2019 Quad Core 64 Bit Wi… |
| 27 | ebay | **OK** | 2.7 | 1.1 | 403 | 0 | 0 | 68 | 0 |  | png | Error Page \| eBay |
| 28 | imdb | **OK** | 1.8 | 0.8 | 202 | 1 | 1 | 41 | 0 |  | png | – |
| 29 | weather | **OK** | 10.2 | 1.7 | 200 | 0 | 0 | 121 | 0 |  | png | Yr - Warsaw - Long term forecast |
| 30 | apple | **OK** | 17.0 | 0.8 | 200 | 0 | 0 | 178 | 0 |  | png | Apple |
| 31 | osm | **OK** | 9.1 | 1.5 | 200 | 0 | 1 | 137 | 0 |  | png | OpenStreetMap |
| 32 | youtube | **OK** | 21.1 | 2.1 | 200 | 0 | 0 | 500 | 0 |  | png | YouTube |

**Problems** (not OK, HTTP ≥ 400, stall reports or kernel faults):

- **15 stackoverflow** (https://stackoverflow.com/questions/tagged/rtos): OK, reason `-`, end `none`, rc 0, HTTP 403, wall 7 s, stalls 0, unresponsive 0
- **27 ebay** (https://www.ebay.com/): OK, reason `-`, end `none`, rc 0, HTTP 403, wall 9 s, stalls 0, unresponsive 1

**Build 52, 2026-10-07** (player memory fix; GPU raster + dma-buf): **32/32 OK**

### Run `s10071223x13238`

- Settings: `sites=32 limit=120 snap=1 dwell=0 size=1280x960 dmabuf=1 gpu=1 stall=60 rss=3 profile=fresh env=none temp_mC=52546`
- Network check: `date=2026-10-07T12:23:07Z clock=set https=301 http=200`
- Per-site logs and snapshots: `/srv/phoenix-rpi4-nfs-gcc16/root/survey/s10071223x13238` (on the Pi: `/root/survey/s10071223x13238/`)
- Result: **32/32 OK** (32 OK, 0 TIMEOUT, 0 CRASH, 0 HANG, 0 ERROR)
- Load time of the OK sites (`load started` → `load finished`): median 7.1 s, max 37.0 s
- Web process footprint (peak per site, WTF memoryFootprint): median 81 MB, max 436 MB
- Kernel fault dumps during the survey: 0

| # | site | result | load s | commit s | HTTP | JS err | console err | web MB | stalls | faults | snap | title |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | start | **OK** | 0.5 | 0.1 | 0 | 0 | 0 | – | 0 |  | png | Phoenix-RTOS Web Browser |
| 2 | example | **OK** | 1.5 | 0.6 | 200 | 0 | 0 | – | 0 |  | png | Example Domain |
| 3 | cern | **OK** | 0.4 | 0.1 | 200 | 0 | 0 | – | 0 |  | png | – |
| 4 | hackernews | **OK** | 1.8 | 1.0 | 200 | 0 | 0 | 57 | 0 |  | png | Hacker News |
| 5 | lobsters | **OK** | 7.1 | 0.8 | 200 | 0 | 2 | 54 | 0 |  | png | Lobsters |
| 6 | kernelorg | **OK** | 3.7 | 0.4 | 200 | 0 | 0 | 63 | 0 |  | png | The Linux Kernel Archives |
| 7 | w3c | **OK** | 4.3 | 0.6 | 200 | 0 | 0 | 70 | 0 |  | png | W3C |
| 8 | pythondocs | **OK** | 6.3 | 0.5 | 200 | 0 | 0 | 43 | 0 |  | png | The Python standard library — Python 3.14.8 documentation |
| 9 | cnnlite | **OK** | 2.1 | 0.8 | 200 | 0 | 0 | 47 | 0 |  | png | Breaking News, Latest News and Videos \| CNN |
| 10 | ddghtml | **OK** | 1.8 | 0.9 | 200 | 0 | 1 | 41 | 0 |  | png | raspberry pi at DuckDuckGo |
| 11 | wikipedia | **OK** | 14.2 | 1.0 | 200 | 0 | 0 | 157 | 0 |  | png | Raspberry Pi - Wikipedia |
| 12 | archwiki | **OK** | 2.8 | 1.3 | 200 | 0 | 0 | 41 | 0 |  | png | Code of Conduct \| Arch Linux Terms |
| 13 | mdn | **OK** | 13.1 | 0.7 | 200 | 0 | 1 | 81 | 0 |  | png | JavaScript \| MDN |
| 14 | github | **OK** | 9.4 | 1.5 | 200 | 0 | 1 | 144 | 0 |  | png | GitHub - phoenix-rtos/phoenix-rtos-kernel: Phoenix-RTOS mic… |
| 15 | stackoverflow | **OK** | 2.6 | 1.5 | 403 | 0 | 0 | 39 | 0 |  | png | Just a moment... |
| 16 | oldreddit | **OK** | 15.9 | 2.1 | 200 | 2 | 18 | 202 | 0 |  | png | Welcome to Reddit |
| 17 | csstricks | **OK** | 37.0 | 1.1 | 200 | 0 | 21 | 264 | 0 |  | png | A Complete Guide to CSS Flexbox \| CSS-Tricks |
| 18 | caniuse | **OK** | 13.9 | 1.8 | 200 | 0 | 0 | 115 | 0 |  | png | CSS Grid Layout (level 1) \| Can I use... Support tables fo… |
| 19 | bbc | **OK** | 16.6 | 1.0 | 200 | 1 | 2 | 182 | 0 |  | png | BBC News - Breaking news, video and the latest top stories … |
| 20 | guardian | **OK** | 5.0 | 1.0 | 200 | 0 | 1 | 81 | 0 |  | png | Latest news, sport and opinion from the Guardian |
| 21 | nytimes | **OK** | 25.6 | 2.2 | 200 | 0 | 0 | 173 | 0 |  | png | The New York Times - Breaking News, US News, World News and… |
| 22 | reuters | **OK** | 25.9 | 0.6 | 200 | 0 | 1 | 233 | 0 |  | png | Reuters \| Breaking International News & Views |
| 23 | ddg | **OK** | 20.2 | 1.0 | 200 | 0 | 3 | 242 | 0 |  | png | raspberry pi at DuckDuckGo |
| 24 | bing | **OK** | 4.0 | 2.0 | 200 | 0 | 0 | 81 | 0 |  | png | raspberry pi - Search |
| 25 | google | **OK** | 2.2 | 1.1 | 200 | 0 | 0 | 63 | 0 |  | png | – |
| 26 | amazon | **OK** | 23.5 | 1.8 | 200 | 0 | 0 | 228 | 0 |  | png | Amazon.com: Raspberry Pi 4 Model B 2019 Quad Core 64 Bit Wi… |
| 27 | ebay | **OK** | 2.7 | 1.1 | 403 | 0 | 0 | 41 | 0 |  | png | Error Page \| eBay |
| 28 | imdb | **OK** | 1.6 | 0.6 | 202 | 0 | 0 | 54 | 0 |  | png | – |
| 29 | weather | **OK** | 10.9 | 2.6 | 200 | 0 | 0 | 97 | 0 |  | png | Yr - Warsaw - Long term forecast |
| 30 | apple | **OK** | 16.6 | 0.6 | 200 | 0 | 0 | 139 | 0 |  | png | Apple |
| 31 | osm | **OK** | 11.3 | 1.5 | 200 | 0 | 1 | 146 | 0 |  | png | OpenStreetMap |
| 32 | youtube | **OK** | 17.8 | 2.1 | 200 | 0 | 0 | 436 | 0 |  | png | YouTube |

**Problems** (not OK, HTTP ≥ 400, stall reports or kernel faults):

- **15 stackoverflow** (https://stackoverflow.com/questions/tagged/rtos): OK, reason `-`, end `none`, rc 0, HTTP 403, wall 7 s, stalls 0, unresponsive 0
- **27 ebay** (https://www.ebay.com/): OK, reason `-`, end `none`, rc 0, HTTP 403, wall 11 s, stalls 0, unresponsive 1



**Build 50, 2026-10-07** (GPU raster + dma-buf, `survey.sh gpu=1`):

### Run `s10071013x1943`

- Settings: `sites=32 limit=120 snap=1 dwell=0 size=1280x960 dmabuf=1 gpu=1 stall=60 rss=3 profile=fresh env=none temp_mC=52546`
- Network check: `date=2026-10-07T10:13:34Z clock=set https=301 http=200`
- Per-site logs and snapshots: `/srv/phoenix-rpi4-nfs-gcc16/root/survey/s10071013x1943` (on the Pi: `/root/survey/s10071013x1943/`)
- Result: **31/32 OK** (31 OK, 1 TIMEOUT, 0 CRASH, 0 HANG, 0 ERROR)
- Load time of the OK sites (`load started` → `load finished`): median 5.7 s, max 38.1 s
- Web process footprint (peak per site, WTF memoryFootprint, overcounts): median 81 MB, max 1177 MB
- Kernel fault dumps during the survey: 0

| # | site | result | load s | commit s | HTTP | JS err | console err | web MB | stalls | faults | snap | title |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | start | **OK** | 0.4 | 0.1 | 0 | 0 | 0 | – | 0 |  | png | Phoenix-RTOS Web Browser |
| 2 | example | **OK** | 1.5 | 0.7 | 200 | 0 | 0 | – | 0 |  | png | Example Domain |
| 3 | cern | **OK** | 0.7 | 0.6 | 200 | 0 | 0 | – | 0 |  | png | – |
| 4 | hackernews | **OK** | 1.8 | 1.1 | 200 | 0 | 0 | 56 | 0 |  | png | Hacker News |
| 5 | lobsters | **OK** | 6.0 | 0.8 | 200 | 0 | 2 | 58 | 0 |  | png | Lobsters |
| 6 | kernelorg | **OK** | 3.4 | 0.7 | 200 | 0 | 0 | 42 | 0 |  | png | The Linux Kernel Archives |
| 7 | w3c | **OK** | 4.8 | 0.9 | 200 | 0 | 0 | 76 | 0 |  | png | W3C |
| 8 | pythondocs | **OK** | 5.7 | 0.7 | 200 | 0 | 0 | 49 | 0 |  | png | The Python standard library — Python 3.14.8 documentation |
| 9 | cnnlite | **OK** | 2.1 | 0.8 | 200 | 0 | 0 | 47 | 0 |  | png | Breaking News, Latest News and Videos \| CNN |
| 10 | ddghtml | **OK** | 2.7 | 1.8 | 200 | 0 | 1 | 39 | 0 |  | png | raspberry pi at DuckDuckGo |
| 11 | wikipedia | **OK** | 12.3 | 0.9 | 200 | 0 | 0 | 158 | 0 |  | png | Raspberry Pi - Wikipedia |
| 12 | archwiki | **OK** | 2.9 | 1.8 | 200 | 0 | 0 | 40 | 0 |  | png | Code of Conduct \| Arch Linux Terms |
| 13 | mdn | **OK** | 13.7 | 0.8 | 200 | 0 | 1 | 81 | 0 |  | png | JavaScript \| MDN |
| 14 | github | **OK** | 9.9 | 1.5 | 200 | 0 | 1 | 130 | 0 |  | png | GitHub - phoenix-rtos/phoenix-rtos-kernel: Phoenix-RTOS mic… |
| 15 | stackoverflow | **OK** | 2.4 | 1.3 | 403 | 0 | 0 | 39 | 0 |  | png | Just a moment... |
| 16 | oldreddit | **OK** | 14.3 | 2.4 | 200 | 1 | 11 | 172 | 0 |  | png | Welcome to Reddit |
| 17 | csstricks | **OK** | 35.1 | 1.1 | 200 | 0 | 21 | 268 | 0 |  | png | A Complete Guide to CSS Flexbox \| CSS-Tricks |
| 18 | caniuse | **OK** | 15.2 | 2.1 | 200 | 0 | 0 | 129 | 0 |  | png | CSS Grid Layout (level 1) \| Can I use... Support tables fo… |
| 19 | bbc | **OK** | 20.3 | 0.9 | 200 | 0 | 1 | 202 | 0 |  | png | BBC News - Breaking news, video and the latest top stories … |
| 20 | guardian | **OK** | 5.5 | 1.5 | 200 | 0 | 1 | 80 | 0 |  | png | Latest news, sport and opinion from the Guardian |
| 21 | nytimes | **TIMEOUT** | – | 2.1 | 200 | 0 | 2 | 1177 | 0 |  | - | The New York Times - Breaking News, US News, World News and… |
| 22 | reuters | **OK** | 27.1 | 1.2 | 200 | 0 | 1 | 230 | 0 |  | png | Reuters \| Breaking International News & Views |
| 23 | ddg | **OK** | 24.8 | 1.1 | 200 | 0 | 3 | 232 | 0 |  | png | raspberry pi at DuckDuckGo |
| 24 | bing | **OK** | 5.3 | 2.8 | 200 | 0 | 0 | 60 | 0 |  | png | raspberry pi - Search |
| 25 | google | **OK** | 2.5 | 1.4 | 200 | 0 | 0 | 39 | 0 |  | png | – |
| 26 | amazon | **OK** | 38.1 | 2.3 | 200 | 0 | 2 | 318 | 0 |  | png | Amazon.com: Raspberry Pi 4 Model B 2019 Quad Core 64 Bit Wi… |
| 27 | ebay | **OK** | 2.9 | 1.0 | 403 | 0 | 0 | 110 | 0 |  | png | Error Page \| eBay |
| 28 | imdb | **OK** | 1.6 | 0.7 | 202 | 0 | 0 | 41 | 0 |  | png | – |
| 29 | weather | **OK** | 11.1 | 2.2 | 200 | 0 | 0 | 75 | 0 |  | png | Yr - Warsaw - Long term forecast |
| 30 | apple | **OK** | 22.3 | 1.2 | 200 | 0 | 0 | 145 | 0 |  | png | Apple |
| 31 | osm | **OK** | 12.8 | 2.1 | 200 | 0 | 1 | 148 | 0 |  | png | OpenStreetMap |
| 32 | youtube | **OK** | 20.8 | 3.2 | 200 | 3 | 3 | 426 | 0 |  | png | YouTube |

**Problems** (not OK, HTTP ≥ 400, stall reports or kernel faults):

- **15 stackoverflow** (https://stackoverflow.com/questions/tagged/rtos): OK, reason `-`, end `none`, rc 0, HTTP 403, wall 9 s, stalls 0, unresponsive 0
- **21 nytimes** (https://www.nytimes.com/): TIMEOUT, reason `launcher-timeout`, end `none`, rc 2, HTTP 200, wall 124 s, stalls 0, unresponsive 1
  - console ×1: `CONSOLE ERROR Unhandled Promise Rejection (unhandledrejection): Error: Could not get player context. Is this rendering inside a betamax component?`
  - console ×1: `CONSOLE ERROR  ERROR  [Statsig] A networking error occurred during GET request to https://static01.nytimes.com/statsig/config/$client-BasfMtnVqHD0fEI7mV1O7Vkyk…`
- **27 ebay** (https://www.ebay.com/): OK, reason `-`, end `none`, rc 0, HTTP 403, wall 12 s, stalls 0, unresponsive 1



_No run yet._ Paste `parse-survey.py`'s output here, one `### Run` section per build. Note the
build number and the manifest above each one.
