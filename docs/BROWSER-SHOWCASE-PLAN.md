# Browser showcase video — recording plan

A second reel next to the main showcase ([SHOWCASE-VIDEO-PLAN.md](SHOWCASE-VIDEO-PLAN.md)),
about the two web browsers on the Pi 4 image:

- **WPE WebKit 2.54** (`/bin/browser`). GPU raster, dma-buf frames and WebGL are its defaults.
- **WebKitGTK 2.54** (`/usr/bin/webkit-browser`). It has tabs and downloads.

Everything is shown **at real-time speed**: the reel cuts windows out of the HDMI recordings and
never speeds them up. A page that takes 14 s to load takes 14 s on the reel. The reel lasts about
4 minutes:

1. a few popular sites, with the address typed at a person's pace;
2. WebGL and GPU-composited animation;
3. HEVC 1080p video on the hardware decoder, drawn with zero copies;
4. WebKitGTK with tabs and a download.

The recording mechanics are those of the main reel: `scripts/record-showcase-clip.sh` records
one Pi power cycle per clip, boot included, and `XFCE_AUTOSTART` opens the programs inside the
session ([SHOWCASE-VIDEO-PLAN §1](SHOWCASE-VIDEO-PLAN.md#1-how-capture-works)). This plan adds:

| File | What |
|---|---|
| `tools/browser/showcase/pi/*.sh`, `gpu.html` | The Pi side, staged to `/usr/share/browser-showcase/`. There is one wrapper per `XFCE_AUTOSTART` item (`wpe-sites.sh`, `wpe-gpu.sh`, `wpe-hls.sh`, `wpe-demo.sh`, `wpe-mse.sh`, `gtk-tabs.sh`), plus `common.sh`, which they source, `scene.sh` (a scene as one psh command) and `gpu.html` (the GPU page, with no network) |
| `tools/browser/showcase/stage.sh` | Copies the Pi side onto the NFS export |
| `scripts/make-browser-reel.sh` | The reel. It works like `make-demo-reel.sh`, but starts can be anchors such as `@sites.load1+2`. `--list` prints the cut list without encoding |
| `scripts/browser-reel-events.py` | Finds the anchors in a clip from its UART log |
| `scripts/verify-demo-reel.py` | Gains `--static-ok` (web pages may sit still), and `--segments` now reads this reel's table or a plain segments file |

## 1. How the scenes are driven

**Wrappers, not psh arguments.** An `XFCE_AUTOSTART` item takes at most one argument. An address
with `:` works only as the last item with a time, and environment variables cannot be given per
item at all. So each item is a wrapper script, `/bin/bash=/usr/share/browser-showcase/<item>.sh:<seconds>`.
The wrapper sets its environment and `exec`s the browser, so the item's SIGTERM at `<seconds>`
reaches the browser itself.

**Typing in real time.** `wpe-browser --auto` feeds synthetic keys through the same path as
the keyboard (B6 check (d)). Its `type:<text>` step delivers the whole text in one tick, so
`common.sh` makes **one step per character, 0.12 s apart** (`BSHOW_TYPE_CS`). The address then
appears letter by letter, as a person types it.

- The times count from the browser's start. They leave the survey's load times plus a margin
  (Wikipedia 14.2 s, GitHub 9.4 s, DuckDuckGo HTML 1.8 s, build 52) before any scroll key.
- The browser quits itself with Ctrl+Q at the end.

**Anchors for the reel.**

- Each wrapper prints the Pi's wall clock just before the browser starts:
  `BSHOW item=<name> start epoch=<UTC s>`.
- The browsers time their lines from their own start: `WPEB t=<ms>` and `WKGB t=<ms>`.
- `scripts/browser-reel-events.py` turns those lines into offsets into the recording, whose file
  name holds the host's UTC start time.
- `make-browser-reel.sh` resolves starts written as anchors, so a re-recorded clip needs no new
  offsets.
- The accuracy is about 1 s:
  - both clocks are NTP-set (`psh-interact.py` waits for the Pi's clock step before it sends a
    command);
  - `--lag` (default 1.0 s) covers the second the file name truncates and ffmpeg's start-up.

**Only the keyboard is scripted.** Nothing moves the pointer, so there are no clicks: no
Ctrl+T in WebKitGTK, no menus.

- The WebKitGTK scene uses the launcher's own options instead: several addresses become several
  tabs, `--tab-cycle` switches between them and `--download` starts a download.
- `xfce-desktop.sh` can also add a keyboard read from a file (`INPUT_EXTRA=/tmp/kbd-inject:keyboard`,
  `b6.sh keys-hid`). It reaches any Wayland client, WebKitGTK's Ctrl+T included, but **it has
  never run on the Pi**, so this plan does not depend on it.

## 2. Before recording

1. **The image.** The netboot NFS-root image, build 68 or later:
   - zero-copy video frames by default;
   - the `webkit_gtk` port with `launcher=b10-r3`;
   - `/usr/bin/date` from coreutils, for the `%N` in the `BSHOW` epoch. Busybox's
     `/bin/date` is the fallback and gives whole seconds.
2. **Stage the showcase** (host, no sudo; the export is additive, so `netboot-server-up.sh` keeps
   it):
   ```
   tools/browser/showcase/stage.sh
   ```
   This copies `common.sh gpu.html gtk-tabs.sh scene.sh wpe-demo.sh wpe-gpu.sh wpe-hls.sh
   wpe-mse.sh wpe-sites.sh` to `/srv/phoenix-rpi4-nfs-gcc16/usr/share/browser-showcase/`.
   Re-stage after `make-pristine-nfs-export.sh` or a `SYNC_DELETE=1` sync.
3. **The media server**, for the video scenes:
   ```
   tools/browser/media/serve-for-pi.sh start
   curl -s http://10.42.0.1:8091/phx-ping
   ```
   It serves the following, all present on the host on 2026-10-08:
   - the HEVC ladder `artifacts/media/ladders/hevc-fmp4/`;
   - the pages, and hls.js for the optional MSE scene (`tools/browser/media/stage.sh`);
   - the demo file `artifacts/media/demo/phoenix-rpi4-showcase-hevc-1080p30.mp4`.
4. **Internet** for the sites and the download. `netboot-server-up.sh` sets up the NAT itself
   (`pi-internet-nat.sh`, as in every cycle log).
   - WiFi is not used. The AP can stay up, as for the main reel.
5. **Warm the browsers' disk caches** with one rehearsal (§4). A recording then loads the sites
   from a warm profile, as a returning user would.
   - Both profiles live on the export: `/root/.cache/wpe-browser` and
     `/root/.cache/webkit-browser`.
   - For a cold first visit on camera, delete those directories before the recording.
6. **One Pi cycle at a time.** Each clip is one power cycle.

## 3. Scenes

Each scene is **two psh commands**, as in the main reel:

1. one `export` with every variable (`THUNAR_START=0` keeps Thunar off the browser window);
2. `/bin/bash /bin/xfce-session`.

`REC_IDLE_SECS=60` is safe because the session prints a heartbeat every 10 s while `HOLD` runs.
The `export` prints nothing, so it costs the full 60 s idle window.

**Record time** = boot (~75 s) + the clock-step wait (~0–15 s) + 8 + `export` (~62) + 8 + session
start (~20) + `HOLD` + logout (~15) + **30 s of margin**. The margin costs only disk (~5 MB per
30 s). The Bash `timeout` is `(secs + 80) × 1000`.

**`HOLD`** counts from the panel. It covers:

- the 5 s autostart delay;
- the items;
- for every item closed by its time, the browser's exit after SIGTERM. That can take up to
  ~10 s (WebKit's children end on their own watchdogs) and delays the next item.

**The clock step.** `psh-interact.py` sends the first command only after the Pi's clock step
(`System time set to`), waiting up to 150 s. The step normally arrives during the boot. A cycle
log with `clock step not seen in 150s` means: re-record.

**Long commands.** A command longer than 128 characters (S3's `export`) makes `psh-interact.py`
print its legacy-CMDSZ warning. Current psh takes 1024 characters, so the warning is harmless;
`scene.sh video` avoids it.

**One-command form.** Each scene also runs as one command,
`/bin/bash /usr/share/browser-showcase/scene.sh <scene>`. It sets the same three variables and
starts the session, so it saves the `export`'s idle minute: use `secs` − 70. It is handy for
rehearsals and works for recordings too.

Grade every clip's UART log first:

```
./scripts/uart-summary.sh rec-<label>
python3 scripts/browser-reel-events.py artifacts/hdmi-video/<ts>-<label>.mp4
```

Both must hold:

- 0 `Exception #` (EL0 dumps print twice);
- `XFCE-SESSION done rc=0`.

### S1 — WPE WebKit: three popular sites (clip `bshow-sites`, reel ~100 s)

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=240 ./scripts/record-showcase-clip.sh bshow-sites 380 \
    "export THUNAR_START=0 HOLD=140 XFCE_AUTOSTART=/bin/bash=/usr/share/browser-showcase/wpe-sites.sh:120" \
    "/bin/bash /bin/xfce-session"
```

Bash `timeout`: **460000** ms. One-command form: `"/bin/bash /usr/share/browser-showcase/scene.sh sites"`
with 310 s and 390000 ms.

**On screen**, in a 1600×900 window on the XFCE desktop. The times count from the browser's
start and are printed in the `BSHOW item=sites` line:

| Time | What happens |
|---|---|
| 0 s | The browser's start page |
| 6 s | Ctrl+L, then `en.wikipedia.org/wiki/Raspberry_Pi` typed letter by letter, then Return at 12 s |
| ~26 s | Wikipedia over HTTPS has loaded |
| 32, 35, 38 s | Page Down, three times |
| 44 s | `phoenix rtos` typed, Return at 47 s: a DuckDuckGo search |
| 57 s | `github.com/phoenix-rtos/phoenix-rtos-kernel` typed, Return at 64 s |
| 82, 86, 90 s | Page Down through the README, three times |
| 96, 102 s | Alt+Left twice: back to the search, then back to Wikipedia |
| 111 s | Ctrl+Q: the browser quits |

**Grading lines:**

- `BSHOW item=sites start … steps=104`;
- `WPEB … chrome action=go source=auto input=en.wikipedia.org/wiki/Raspberry_Pi …`, then
  `WPEB … load finished uri=https://en.wikipedia.org/wiki/Raspberry_Pi`;
- the same pair for `phoenix rtos` (`uri=https://html.duckduckgo.com/html/?q=phoenix%20rtos`) and
  for GitHub;
- `chrome action=back source=auto ok=1` twice;
- `chrome action=quit source=auto`;
- `XFCE-AUTOSTART closed … rc=0` before 120 s.

**Check on the clip:**

- the address appears letter by letter;
- the progress line moves while a page loads;
- **Page Down scrolls the page.** It is the one key step that has not run on the Pi: B6 (d)
  drove the chrome only. If the pages do not move, use `space` in `wpe-sites.sh`, or drop the
  scroll steps;
- no window covers the browser.

### S2 — WebGL and GPU compositing (clip `bshow-gpu`, reel ~30 s)

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=200 ./scripts/record-showcase-clip.sh bshow-gpu 340 \
    "export THUNAR_START=0 HOLD=100 XFCE_AUTOSTART=/bin/bash=/usr/share/browser-showcase/wpe-gpu.sh:70" \
    "/bin/bash /bin/xfce-session"
```

Bash `timeout`: **420000** ms. One-command form: `scene.sh gpu`, 270 s, 350000 ms.

**On screen:** `gpu.html` in a 1600×900 window.

- **Left:** a lit torus knot of 19,200 triangles turning in front of a moving gradient. WebGL 2
  draws it through ANGLE on OpenGL ES 3.1 on the V3D.
- **Right:** eight cards that rotate, scale, slide, flip in 3D and fade. Each is a CSS animation
  on its own compositing layer.
- **Under the canvas:** `WebGL 2 · 19,200 triangles · NN.N fps`. The page measures the frame
  rate with requestAnimationFrame.
- The page shows no renderer name: WebKit masks it as "Apple GPU" on every GPU (b42-gate log).
- `?fps=0` takes the frame rate off the screen. The B7 page ran at ~18 fps on build 42, so
  decide whether the number helps the reel once you have seen it.

**Grading lines:**

- `BSHOW item=gpu start`;
- `WPEB … gpu raster=gpu transport=dmabuf webgl=on`;
- `CONSOLE LOG BSHOW-GPU context=webgl2 …`;
- `BSHOW-GPU fps=…` every 5 s, with a rising frame count;
- no `BSHOW-GPU error`;
- `WPEB … present … buffer=dma-buf`.

**Check on the clip:**

- the knot turns smoothly and the cards keep moving;
- the status line updates.

**Fallback:** the page has not run on the Pi before. It has run in headless Chromium at
1600×860, and that screenshot is how it should look. If it misbehaves, run the proven B7 pages:

```
export BSHOW_GPU_PAGE=file:///usr/share/wpe-browser/b7-webgl.html
```

Use `b7-anim.html` for the composited layers.

### S3 — HEVC 1080p on the hardware decoder (clip `bshow-video`, reel ~50 s)

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=250 ./scripts/record-showcase-clip.sh bshow-video 390 \
    "export THUNAR_START=0 HOLD=150 XFCE_AUTOSTART=/bin/bash=/usr/share/browser-showcase/wpe-hls.sh:55,/bin/bash=/usr/share/browser-showcase/wpe-demo.sh:55" \
    "/bin/bash /bin/xfce-session"
```

Bash `timeout`: **470000** ms. One-command form: `scene.sh video`, 320 s, 400000 ms.

**On screen:** two items, each in a 1280×960 window. The pages fix the `<video>` at 960×540.

**`wpe-hls.sh` (55 s), the test set's HEVC HLS ladder in the native-HLS page `b8-hls.html`:**

- The player picks the **HEVC 1080p** variant itself.
- The picture carries its variant (`HEVC 1080p`) and a clock, burned in.
- The video is muted, and `--autoplay=allow` is set.

**`wpe-demo.sh` (55 s), our own HEVC 1080p30 file:**

- The file is the 2026-09-30 showcase reel, so it shows the desktop and the games.
- `b8.html` plays it from the host's HTTP server, as a video site would serve it.

**Grading lines** (per item, after its `BSHOW item=hls|demo start`):

- `WPEB-MEDIA … decoder video=hevc_rpivid codec=hevc … zero_copy=1`;
- `WPEB-MEDIA zero-copy path=external-oes compositor=skia`;
- every 5 s, `WPEB-MEDIA … stat … fps=29–31 … hw=1 … zc=1 zc_painted=<rising>`;
- the HLS item: `B8HLS … playing`;
- the file item: `B8PAGE … loadedmetadata size=1920x1080 …`, then `B8PAGE … playing`.

On the host, `tools/browser/media/check-media.py requests artifacts/media/serve.log --run
bshow-hls` names the variant directory fetched (`hevc-1080`).

**Check on the clip:**

- the burned-in label reads `HEVC 1080p`, and its clock runs without stalls;
- the demo reel plays smoothly.

**Optional, MSE** (clip `bshow-mse`; it adds one more cycle):

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=185 ./scripts/record-showcase-clip.sh bshow-mse 320 \
    "export THUNAR_START=0 HOLD=85 XFCE_AUTOSTART=/bin/bash=/usr/share/browser-showcase/wpe-mse.sh:60" \
    "/bin/bash /bin/xfce-session"
```

Bash `timeout`: 400000 ms.

- hls.js runs over Media Source Extensions, on the same ladder.
- Its adaptive bitrate starts low and climbs, so the burned-in label changes on screen up to
  `HEVC 1080p`. Stage 1c (build 59) climbed to 1080p HEVC with 0 stalls.
- **Grading lines:** `B8HLSJS … branch=hls.js`, its level switches, `WPEB-MEDIA … mse decoder
  video=hevc_rpivid`, and `stat … hw=1`.

### S4 — WebKitGTK: tabs and a download (clip `bshow-gtk`, reel ~50 s)

```
REC_IDLE_SECS=60 REC_MAX_CMD_SECS=210 ./scripts/record-showcase-clip.sh bshow-gtk 350 \
    "export THUNAR_START=0 HOLD=110 XFCE_AUTOSTART=/bin/bash=/usr/share/browser-showcase/gtk-tabs.sh:80" \
    "/bin/bash /bin/xfce-session"
```

Bash `timeout`: **430000** ms. One-command form: `scene.sh gtk`, 280 s, 360000 ms.

**On screen:** WebKitGTK's MiniBrowser window with three tabs, all of which loaded cleanly in
the site survey:

1. Wikipedia `Raspberry_Pi`;
2. GitHub `phoenix-rtos/phoenix-rtos-kernel`;
3. the Python documentation.

`--tab-cycle=12` shows the next tab every 12 s. When the first tab has loaded (~15 s), the
**downloads bar** shows a download:

- The download is the CPython 3.14.0 source tarball from python.org (23,595,844 bytes).
- It goes to `/tmp/bshow-downloads`, which is in RAM, so nothing is left on the export.
- Without internet, or for a longer progress bar, use our 94 MB demo file over the LAN:
  `BSHOW_DOWNLOAD_URL=http://10.42.0.1:8091/demo/phoenix-rpi4-showcase-hevc-1080p30.mp4`
  (`export` it at psh).

**Grading lines:**

- `BSHOW item=gtk start`;
- `WKGB … ui start … launcher=b10-r3`;
- `WKGB gdk-gl ok use_es=1`, and no `Disabled hardware acceleration`;
- `WPEB-WEBKIT swap-chain … type=texture-dmabuf`;
- three `WKGB … load finished`;
- `WKGB … download request uri=https://www.python.org/…`, then `download started`, then
  `download destination /tmp/bshow-downloads/Python-3.14.0.tar.xz`;
- `download finished … received=23595844`;
- `tab switch page=2/3 …`.

**Check on the clip:**

- the tab bar shows three titled tabs;
- the shown page changes every 12 s;
- the downloads bar shows progress, then completion.

`--download=URL` has not run on the Pi before: G3 used a download that the page started itself.
The launcher calls WebKit's standard `webkit_web_view_download_uri()`.

## 4. Rehearsal (recommended, one cycle, no recording)

```
./scripts/test-cycle-psh-interact.sh --label bshow-rehearsal --wait-secs 220 --inter-cmd-secs 8 \
    --idle-secs 60 --max-cmd-secs 600 -- "/bin/bash /usr/share/browser-showcase/scene.sh all"
```

Run it with `run_in_background: true` and a Bash `timeout` of **1000000** ms. Boot, the 480 s
session and the 60 s idle tail come to ~11–14 min, past the 600000 ms foreground cap.

- `scene.sh all` runs S1, S2, S3 and S4 in one session (`HOLD` 480 s), so it fits one cycle.
- It warms both disk caches.
- Grade it with the lines of §3. `python3 scripts/browser-reel-events.py` needs a recording to
  measure against, but the `BSHOW`, `WPEB` and `WKGB` lines grade the same.
- The periodic HDMI snapshots (`artifacts/hdmi/`, every 25 s) show the layout.

## 5. Assembling the reel

Run this from the main checkout, where `artifacts/` is, after the four clips are recorded:

```
./scripts/make-browser-reel.sh --list        # the resolved cut list: check every offset
./scripts/make-browser-reel.sh               # artifacts/hdmi-video/<ts>-phoenix-rtos-rpi4-browser-showcase.mp4
.venv/bin/python scripts/verify-demo-reel.py <reel.mp4> --segments scripts/make-browser-reel.sh \
    --static-ok 'Boot,WPE WebKit,WebKitGTK'
```

- `verify-demo-reel.py` needs numpy, which is in the repo's `.venv`.
- **The table** in `make-browser-reel.sh` names clips by their label (the newest
  `*-<label>.mp4`) and starts by anchors.
  - Once the reel is final, **pin the basenames**, so that a later re-record does not change it.
  - Measure the Boot segment's numeric start on the frame: `ffmpeg -ss <t> -i <clip> -frames:v 1 /tmp/f.png`.
- **Calibrate on an in-browser anchor, never on `<item>.start`.** For example, compare
  `@sites.load1` with the frame where Wikipedia appears, or `@gtk.load1` with the first tab
  rendering.
  - The `.start` anchors are the least precise. Between the `BSHOW` epoch and the browser's
    `t=0` lie bash, `/bin/browser` and the exec of a 121 MB static ELF over NFS. That gap has
    not been measured, and it is longer for a session's first browser.
  - If a clip's in-browser anchors are all off by the same amount, pass `BROWSER_REEL_LAG=<s>`;
    the default is 1.0.
  - Segments that start at `.start` (2, 5 and 8) have a few seconds of slack at their start.
- **Expected `verify` notes:**
  - The site and WebKitGTK segments are static between keystrokes, hence `--static-ok`. The GPU
    and video segments must move.
  - The three WPE site segments share the toolbar and the window frame, so `LOOKALIKE` may pair
    them. That is the same class of false positive as the main reel's two windowed games. Check
    the caption frames in `artifacts/reel-captions/`.

| # | Segment (caption) | Source | Start | Length |
|---|---|---|---|---|
| 1 | Boot — Raspberry Pi 4 netboot: the Phoenix-RTOS kernel, drivers, GPU servers and the NFS root | `bshow-sites` | 20 (measure) | 12 |
| 2 | WPE WebKit 2.54 — an address typed in real time, then Wikipedia over HTTPS, scrolled | `bshow-sites` | `@sites.start+4` | 40 |
| 3 | WPE WebKit 2.54 — a DuckDuckGo search | `bshow-sites` | `@sites.go2-3` | 12 |
| 4 | WPE WebKit 2.54 — GitHub, the Phoenix-RTOS kernel; then back, back | `bshow-sites` | `@sites.go3-7` | 48 |
| 5 | WebGL on the V3D GPU — a lit 19,200-triangle knot, and CSS animation composited on the GPU | `bshow-gpu` | `@gpu.start+6` | 30 |
| 6 | HEVC 1080p on the hardware decoder, zero-copy — an HLS stream; the player chose the 1080p HEVC variant | `bshow-video` | `@hls.load1+3` | 25 |
| 7 | HEVC 1080p30 in a web page on the hardware decoder, zero-copy — our own showcase reel as a video file | `bshow-video` | `@demo.load1+3` | 25 |
| (opt.) | hls.js over Media Source Extensions — adaptive bitrate climbs to HEVC 1080p on the hardware decoder | `bshow-mse` | `@mse.load1+8` | 25 |
| 8 | WebKitGTK 2.54 — tabs and downloads: Wikipedia, GitHub, Python docs, a 23 MB download | `bshow-gtk` | `@gtk.start+6` | 50 |

The total is 242 s, or 267 s with the MSE segment.

- Segments 2–4 run back to back, from 4 s to ~105 s of the browser's run: the typing, both loads
  in full and the scrolling.
- Cut them shorter only by moving starts and lengths, never with a speed change.

**Anchors** (`browser-reel-events.py <clip>` prints them all; N counts from 1):

| Anchor | Log line it comes from |
|---|---|
| `<item>.start` | `BSHOW item=<item> start epoch=…` |
| `goN` | the Nth `WPEB t= chrome action=go` |
| `loadN` | the Nth `WPEB`/`WKGB t= load finished uri=` |
| `backN` | the Nth `chrome action=back` |
| `quit` | `chrome action=quit` |
| `download` | `WKGB download started` |
| `downloaded` | `WKGB download finished` |
| `tabN` | the Nth `WKGB tab switch` |

The items are `sites`, `gpu`, `hls`, `demo`, `mse` and `gtk`.

## 6. Validation done on the host (2026-10-08, no Pi)

- `bash -n` and shellcheck 0.11 (`uvx --from shellcheck-py`; `-x`) are clean on every new script.
- **The wrappers**, run on the host with `exec` replaced by `echo`, give the expected command
  lines.
  - The sites `--auto` value has 104 steps and the times of §3 S1.
- `stage.sh` into a temporary directory installs the 9 files.
- **`gpu.html`** in Playwright's headless Chromium (SwiftShader):
  - `context=webgl2`, 19,200 triangles, no page errors;
  - the screenshot shows the knot and the cards;
  - `?mode=css` works.
- **The reel smoke test.** `BROWSER_REEL_SEGMENTS=<file>` ran four segments:
  - two numeric cuts from the 2026-09-30 clips;
  - two anchored cuts on a stand-in clip (a symlinked recording plus a synthetic UART log with
    `BSHOW`/`WPEB`/`WKGB` lines: duplicate `load finished` lines and a child's line ignored).
- **The smoke reel** is 30.0 s, 900 frames at 30 fps: the table's 30 s, nothing sped up.
  - `verify-demo-reel.py --segments <file> --static-ok 'WPE WebKit,WebKitGTK'` reports every
    segment `ok`.
  - It also flags one `LOOKALIKE`, which is real: the stand-in clip is the same recording as the
    moving segment.
  - Without `--static-ok`, the still page segment is `FROZEN`, as intended.
- **Error paths:** a cut past the end of its clip and an unknown anchor both stop the script with
  rc 1 and a message.
- `verify-demo-reel.py`'s new parser still finds the main reel's 13 segments.

## 7. Risks and what is not scripted

- **Live sites change.** Wikipedia, DuckDuckGo, GitHub and python.org were chosen for
  stable, neutral content: no news front page in the reel. Look at what is on screen before
  publishing.
- **First runs on the Pi:**
  - Page Down via `--auto` (S1);
  - `gpu.html` (S2);
  - `--download=URL` (S4).

  Each has a fallback above. The rehearsal (§4) covers all three in one cycle.
- **WebGL frame rate.** The B7 page ran at 18.4 fps on build 42. A low number on screen is honest
  but may not flatter; `?fps=0` hides it.
- **The demo file is our own showcase reel**, so S3b shows games inside a browser window. The
  caption must say so, which it does: "our own showcase reel as a video file".
- **Not scripted:** pointer actions (menus, clicks, Ctrl+T by mouse). Typed keys into
  WebKitGTK would need the `INPUT_EXTRA` keyboard file, which has never run on the Pi
  (§1).
