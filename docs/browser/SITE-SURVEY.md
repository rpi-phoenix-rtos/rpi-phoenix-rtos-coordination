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
| `webprocess_rss_kb` | the largest `mem role=web ... footprint_kb=` | the web process's peak footprint, from WTF `memoryFootprint()`: the anonymous pages of its map entries. ⚠ This overcounts, by up to several times (the kernel's `meminfo()` amap issue, see the wpe README). Phoenix has no `/proc`, and `ps` shows only VMEM. Compare between sites and builds, not with Linux. |
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

_No run yet._ Paste `parse-survey.py`'s output here, one `### Run` section per build. Note the
build number and the manifest above each one.
