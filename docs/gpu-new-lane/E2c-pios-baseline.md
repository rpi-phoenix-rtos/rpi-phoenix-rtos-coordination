# E2c — The same benchmarks on Raspberry Pi OS, on this board

*Pre-registered 2026-09-26, before any E2c Pi run. Follows [E2b](E2b-v3d-render-slowness.md)
("E2c (optional, coordinator)") and the research doc's
[§2.3 / §5.3](../research/2026-09-26-gpu-drm-architecture.md#23-reference-numbers-pi-4-stock-clocks-to-measure-against).*

Status: **prepared, not run.** Rootfs installed and staged, boot script and cycle script written and
checked on the host (syntax, awk units on synthetic data, `--prepare-only`). No Pi cycle has been
run and the live netboot server is unchanged.

## Question

"Phoenix renders SuperTuxKart ~3× slower than Raspberry Pi OS" comes from citations (29 fps at 720p
in 2019, 30–110 fps in 2023) that recorded neither the STK version nor anisotropic filtering, dynamic
lights or the RTT scale. E2b's corrected reading says STK's render phase is **shader-bound**: 91 ms
of V3D render time per frame at 1440×810 RTTs (`scale_rtts_factor 0.75`), 7.4 fps at
`core_freq=250`. The question for E2c: **what do the same game, data, settings and GPU produce on
Raspberry Pi OS, on this Pi 4?** Two readings are possible:

* Pi OS renders the same frame in far less **V3D time** → the gap is in our GPU path (Mesa build,
  patches, job setup, clocks). The Pi OS counters show what Phoenix should reach.
* Pi OS's per-frame V3D time is about the same → the GPU is not slower on Phoenix, and the "3×" is
  CPU time, the display path, settings, or never was like-for-like.

## What is compared with what

| Pi OS number (tag in the log) | Phoenix number it is compared with | Where the Phoenix number is |
|---|---|---|
| STK **HUD fps over the race window** (`E2C stk run=2 hud-gameplay … mean= median=`) | `flipstat` gameplay mean: **7.38 fps** (E2 W1), **7.43 / 8.39** at core 250 / 500 (E2b step 1) | [E2](E2-stk-submit-breakdown.md), [E2b step 1](E2b-v3d-render-slowness.md#result--step-1-core-clock-ab-queue5-build-9-shipped-stk-warm-cache-interleaved) |
| STK **render ms per frame** (`gpu-race … render_ms_per_frame=`) and render jobs per frame | E2 W1 **render spin 91.35 ms**, 8 CL jobs per frame | E2 / E2b base1–2 (91–94 ms) |
| STK **bin ms per frame** (`gpu-race … bin_ms_per_frame=`) | E2 W1 **bin spin 3.21 ms** | E2 |
| STK CPU share, derived: `1000/fps − render_ms` (upper bound on CPU + wait) | E2 W1 **40.4 ms** CPU outside the winsys | E2 |
| STK `profile: Number of frames … Average FPS` | the same line on Phoenix, e.g. 43.67 (`c1p4x8`) | see the caveat below: this is *not* fps |
| quakespasm `timedemo demo1` (`E2C quake run=N timedemo … fps`) | old lane **30.4 fps** (30.2/30.6/30.5), new lane (M1 `quakespasm-v3da`) **38.2 fps** — 969 frames | [M1 result P2-B/C](M1-async-render-server.md) |
| glmark2-es2 off-screen 800×600 score + per-scene fps | none on Phoenix yet (needs EGL, M3); cited Pi OS 256–450 (Mesa 19/20), 425–697 (2023) | research doc §2.3 |
| kmscube (vsync) | none on Phoenix; cited 60 | research doc §2.3 |
| vkmark headless score | none on Phoenix; cited 311 (2020) | research doc §2.3 |

**Caveat — STK's own "Average FPS" is physics ticks, not frames.** `ProfileWorld::update()` counts
one per physics step (120 Hz, `data/stk_config.xml` `physics fps="120"`), and `main_loop.cpp` clamps
a frame's `dt` to three substeps' worth, 50 ms. Below 20 fps each frame therefore advances the game by
50 ms = 6 ticks and the game clock runs slow (Phoenix's 2-lap race takes ~460 s), so the number is
**6 × render fps** there (Phoenix: 43.67 / 6 = 7.28 ≈ flipstat 7.3); at ≥ 20 fps it saturates at
~120. It is reported for completeness; the fps comparison uses the HUD counter.

**The fps counter on Linux** is Mesa's HUD, invisible, printing `fps: N` to the app's stdout every
second (`GALLIUM_HUD=stdout,fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_PERIOD=1`,
`src/gallium/auxiliary/hud/hud_context.c`: the frame count advances in `hud_run()` at each
`SwapBuffers`, and `GALLIUM_HUD_VISIBLE=false` skips drawing). That is the direct analogue of the
Phoenix winsys `flipstat` (frames presented). The race window is the STK log between
`Track: Overall scene complexity estimated at` and `profile: Number of frames` — the same anchors
exist in the Phoenix logs; the first 3 and the last 1-s sample are dropped (E2's "first/last window
dropped"). The raw series is printed too (`hud-series`), so any other window rule can be applied
afterwards.

**The GPU time on Linux** comes from the v3d driver's sysfs `gpu_stats` (kernel ≥ 6.8,
`drivers/gpu/drm/v3d/v3d_sysfs.c`): per queue, jobs completed and busy nanoseconds. The render
queue's clock starts in `v3d_render_job_run()` right before the CT1 kick and stops at the FRDONE
interrupt — the same interval as Phoenix's render spin (CT1 kick → FRDONE). The boot script
snapshots it at the two race anchors and divides by the frames the HUD counted in between.

## What is installed (2026-09-26)

Rootfs: `artifacts/linux-netboot/rootfs/`, **Raspberry Pi OS Lite arm64 2026-06-18 (trixie, Debian
13.5)**, NFSv3 root, kernel **6.18.34+rpt-rpi-v8** (`tftp/kernel8.img` and `lib/modules/` match),
firmware `start4.elf` **May 21 2026** (Phoenix's pinned firmware: May 8 2026). `tftp/config.txt` is
Pi OS's default plus `enable_uart=1`: `dtoverlay=vc4-kms-v3d`, `max_framebuffers=2`,
`disable_fw_kms_setup=1`, `arm_boost=1`, no `core_freq`/`force_turbo`.

Added by E2c (from the host, no Pi boot — see "Reproduce the install"):

| package | version | role |
|---|---|---|
| supertuxkart / supertuxkart-data | 1.4+dfsg-5+b1 / 1.4+dfsg-5 | the game (data package installed only as a dependency; not used) |
| quakespasm | 0.96.3+dfsg-1+b1 | timedemo |
| mesa-libgallium, libgl1-mesa-dri, libegl-mesa0, libgbm1, mesa-vulkan-drivers | **26.2.2-1~bpo13+0~rpt1** | Mesa v3d / v3dv (same 26.2 branch as Phoenix's 26.2.0 `51c5ee977b`) |
| libgles2 / libegl1 (glvnd) | 1.7.0-1+b2 | GL ES / EGL dispatch |
| libsdl2-2.0-0 | 2.32.4+dfsg-1 | KMSDRM video backend (Phoenix: SDL 2.30.12, own backend) |
| libdrm2 | 2.4.134-3~bpo13+1+rpt1 | (upgraded from 2.4.131 as a dependency) |
| glmark2-es2-drm | 2023.01+dfsg-2 | micro |
| kmscube | 0.0.0~git20210103-1+b3 | micro |
| vkmark | 2025.01-1 | micro (headless winsys) |
| mesa-utils, mesa-utils-bin | 9.0.0-2+b2 | `eglinfo` |

Kernel packages are `apt-mark hold` (linux-image-*, linux-headers-rpi-*, raspi-firmware) so a later
install can never replace the modules that must match `tftp/kernel8.img`.

Game data (copied read-only from the Phoenix export, byte-identical — `diff -rq` clean, pak0 sha256
`35a9c55e…a946af`): `/opt/e2c/stk/data` (46 MB, stk-code 1.4 `data/`) + `/opt/e2c/stk/stk-assets`
(149 MB, the 1.4 mobile-reduced art set); `/opt/e2c/quake/id1/{pak0.pak,config.cfg,autoexec.cfg}`.

## Settings parity

| item | Phoenix (E2 / E2b trials) | Pi OS (E2c) | parity |
|---|---|---|---|
| STK version and renderer | stk-code 1.4, `USE_GLES2` build, SP renderer, "OpenGL ES 3.1 Mesa 26.2.0" | Debian 1.4+dfsg-5: also a **GLES2 build** (Irrlicht `COGLES2Driver` in the binary, no `COpenGLDriver`), SP renderer expected; the log line `Using renderer:` is asserted | ✅ (Debian's dfsg repack + patches not reviewed) |
| STK data | `/usr/share/supertuxkart/{data,stk-assets}` | the same files via `SUPERTUXKART_DATADIR=/opt/e2c/stk`, `SUPERTUXKART_ASSETS_DIR=/opt/e2c/stk/stk-assets`. With `DATADIR` set, Debian's own `/usr/share/games/supertuxkart` (full 700 MB art set) is not a root at all (`file_manager.cpp`) | ✅ identical bytes; shaders are in `data/`, so identical GLSL too |
| STK config | launcher seeds `players.xml` + `config.xml` (v8, `enable_internet=2`, `<Video show_fps="true" scale_rtts_factor="0.75"/>`) into a fresh `/tmp/stk/config-0.10/` | the same two files, byte for byte from `stk-launcher.c`, into a fresh `/tmp/stk/config-0.10/` (tmpfs) per run | ✅ |
| STK defaults that matter | anisotropic 4, dynamic lights on, `swap-interval` 0, `max_fps` 120 (profile mode ignores the throttle) | same — not set, 1.4 defaults on both | ✅ |
| STK args | `--screensize=1920x1080 --fullscreen --disable-texture-compression --disable-addon-karts --disable-addon-tracks --track=hacienda --numkarts=4 --profile-laps=2` | identical | ✅ |
| Graphics restrictions | `vendor="Broadcom"` → `HighDefinitionTextures256` disabled | the same rule matches | ✅ |
| Render size | scanout 1920×1080, RTTs 1440×810 | KMS mode pinned to 1920×1080@60 (`video=HDMI-A-1:1920x1080@60`); RTTs 1440×810 | ✅ (mode logged) |
| Present | in-process winsys, firmware pan, 3 buffers, not vsync-waited in the render loop | SDL2 KMSDRM, GBM surface + DRM page flip: at most one flip per vblank → **capped at 60** | ✗ but irrelevant for STK (≪ 60); caps quakespasm/kmscube |
| Mesa | 26.2.0 + Phoenix patches (early-Z forced off, RASTER gate, NPOT mip decline, winsys) | 26.2.2 + rpt packaging, upstream early-Z | ✗ — **this is part of what is being measured** |
| Clocks | ARM 1500 fixed, core **250**, V3D 500 (`pctr-clk`), `force_turbo=1` | arm `stock`: arm_boost (up to 1800), core 500, DVFS; arm `phxclk250`: ARM 1500 / core 250 / V3D 500 fixed; arm `phxclk500`: core 500 | selectable; every run logs `vcgencmd get_config` and samples measured clocks every 5 s |
| Governor | n/a (no DVFS) | `performance` (logged before/after) | — |
| Shader cache | warm (`Mesa shader disk cache KEPT`) | persistent `MESA_SHADER_CACHE_DIR=/var/cache/e2c-mesa` on the NFS root; `files_before` logged; **grade STK run 2** of a boot (run 1 may compile) | ✅ with run 2 |
| Audio | Phoenix STK/quakespasm audio path | vc4 HDMI audio may exist once vc4 loads; OpenAL/SDL use it or run silent | minor CPU difference, not controlled |
| quakespasm | 0.97.0 port, desktop GL, same pak0 + cfgs (1920×1080 fullscreen, `scr_showfps 1`, `scr_conscale 4`) | Debian 0.96.3, desktop GL via SDL2 KMSDRM/EGL, same pak0 + cfgs, `+vid_vsync 0` | ≈ (version differs; page flips cap at 60) |
| Root filesystem | NFS (v4) | NFS (v3) | affects load time only; fps windows start after the track has loaded |

## Run

Nothing here needs the coordinator to edit anything by hand. One boot per arm:

```
setsid nohup ./scripts/e2c-cycle.sh --arm stock     > artifacts/e2c/stock.out 2>&1 &     # first: the pipe-cleaner
setsid nohup ./scripts/e2c-cycle.sh --arm phxclk250 > artifacts/e2c/phxclk250.out 2>&1 & # after it finishes
setsid nohup ./scripts/e2c-cycle.sh --arm phxclk500 > artifacts/e2c/phxclk500.out 2>&1 & # optional
```

(`mkdir -p artifacts/e2c` first.) A boot runs 20–40 min — longer than the 10-min cap of one Bash
call — so run it detached and watch the `.out` file (a `Monitor` on `=== e2c-cycle done` works). The
cycle ends ~10 s after `===== E2C DONE =====` reaches the UART; `--capture-secs` (default 2700) is
only the cap. Useful options: `--only stk` (STK only, ~10–15 min), `--stk-runs N`, `--prepare-only`
(build the TFTP tree and install the boot script, touch nothing else).

What `scripts/e2c-cycle.sh` does:

1. Preflight: Linux boot files, the rootfs has the packages and `/opt/e2c` data, passwordless sudo,
   modules for `-rpi-v8`; installs `tools/gpu-lane/e2c/e2c-bench.sh` (the source of truth) as the
   rootfs's `/usr/local/bin/e2c-bench.sh` if it differs.
2. Builds `artifacts/linux-netboot/tftp-e2c/` — a copy of `tftp/` with this run's `cmdline.txt`
   (`… init=/usr/local/bin/e2c-bench.sh video=HDMI-A-1:1920x1080@60 video=HDMI-A-2:1920x1080@60
   loglevel=4 e2c.arm=… e2c.only=… e2c.stkruns=… e2c.label=…`, `console=serial0,115200` still the
   last `console=`) and `config.txt` (+ the arm's
   clock lines under `[all]`). `tftp/` itself is never modified (the sdflash lane keeps working).
3. Asserts `nfs-server` is active and the rootfs export is live (`/etc/exports` already exports it,
   NFSv3, `10.42.0.0/24` — nothing is exported or unexported).
4. Restarts dnsmasq through the worker `scripts/netboot-server.sh up` with
   `RPI4B_NETBOOT_TFTPROOT=…/tftp-e2c` and checks the generated `tftp-root`. **Not**
   `netboot-server-up.sh`: that wrapper first syncs the Phoenix NFS export.
5. Runs `test-cycle-netboot.sh --skip-server-up --skip-bridge-recovery --capture-secs N --label
   e2c-<arm>-<time>` (`--skip-bridge-recovery`: its DHCP recovery would restart dnsmasq on the
   *Phoenix* tree mid-cycle), ends the capture early on `E2C DONE`.
6. **Always** (trap on EXIT/INT/TERM, HUP ignored): Pi power off, then the same worker with the
   Phoenix bootfs as `tftp-root` (again not the wrapper, so the Phoenix export and its shader cache
   are not touched; it writes exactly the `dnsmasq.conf` a Phoenix server-up writes), then verifies
   that `artifacts/netboot/dnsmasq.conf` serves the Phoenix bootfs again **and**
   `check-netboot-blob.sh --expect nfsroot` passes; prints `=== Phoenix netboot restored ===` or a
   loud failure (exit 4). A SIGKILL cannot run the trap — then run `./scripts/netboot-server-up.sh`
   by hand (the usual server-up).
7. Prints the `E2C` lines and copies the UART log plus the Pi's full per-benchmark logs (written to
   the NFS root, `/var/log/e2c/run-NNN/`, found from `E2C DONE results=` or, if that line was
   corrupted, from the Pi's `latest` link only when its `summary.txt` carries this run's label) to
   `artifacts/e2c/<label>/`.

What `e2c-bench.sh` does on the Pi (PID 1, no systemd — Pi OS first-boot services reboot-loop on an
NFS root): refuses to run unless PID 1; mounts proc/sys/devtmpfs/devpts/tmpfs; `dmesg -n 4`;
`modprobe vc4 v3d` (no udev); finds the vc4 KMS card and v3d `gpu_stats`; sets the `performance`
governor; logs `uname -a`, model/revision/RAM, `vcgencmd version`, `get_config`
arm/core/v3d/gpu_freq/force_turbo/arm_boost, idle clocks/temperature/throttling, Mesa library,
package versions, `eglinfo -B`; runs STK ×`stkruns`, quakespasm timedemo ×2, kmscube (600 frames),
glmark2-es2-drm `--off-screen -s 800x600`, vkmark `--winsys headless`, each under `timeout`, each
with a 5-s `E2C clk <bench>` sampler (measured ARM/core/V3D clocks, temperature, throttled flags,
per-queue GPU jobs/busy-ns); writes each app's output to a file on the NFS root and prints only short
`E2C <bench> …` lines to the console (a slow serial console blocks its writer, which would perturb
fps); ends with `E2C DONE results=…`, `sync`, sysrq power-off.

Log lines to read (`grep -a '^E2C ' <uart log>`; `clk` lines are the sampler):
`E2C boot/cmdline/drm/hdmi/kms-mode/governor/config/clk idle/pkg/eglinfo`,
`E2C stk run=N log Using renderer: …` (must say **OpenGL ES 3.1 Mesa 26.2.2**),
`E2C stk run=N log … scene complexity estimated at …` (Phoenix: 181),
`E2C stk run=N hud-gameplay n= used= mean= median= min= max=`,
`E2C stk run=N gpu-race window_s= frames= fps= bin_ms_per_frame= … render_ms_per_frame= render_jobs_per_frame= render_busy=`,
`E2C quake run=N timedemo <frames> frames <s> seconds <fps> fps` (+ `gpu-process`),
`E2C kmscube … fps_wall=`, `E2C glmark2 offscreen … glmark2 Score: N` (+ `scene` lines),
`E2C vkmark headless … vkmark Score: N`, `E2C DONE`.

## Validity gates (a boot failing one is void for that benchmark)

1. `E2C stk … log Using renderer:` names OpenGL ES 3.x on V3D (not llvmpipe/softpipe, not a legacy
   GL 2 path), and `scene complexity estimated at` matches Phoenix's 181 ± 5 % (same track, same
   karts, same data).
2. `E2C stk run=N kms-mode mode: "1920x1080": 60 …` — the active CRTC mode read from the DRM
   atomic state (debugfs) while the race runs (the connector's `preferred` mode may be 2160p: the
   HDMI grabber offers it).
3. STK run 2 is graded (`mesa-cache files_before` > 0 at its start, i.e. after run 1 or a previous
   boot); `gpu-race` present with `frames` ≥ 300.
4. No `E2C dmesg` line with a v3d/vc4 hang, reset or timeout; `get_throttled` = `0x0` in the
   samplers of the graded window (else the number is thermal/power, not the GPU).
5. `render_busy` ≤ 100 % (one render queue; more means the accounting is not what this doc
   assumes). `render_busy + bin_busy` **may** exceed 100 %: Linux's scheduler runs bin job N+1 on CT0
   while render job N runs on CT1 — overlap Phoenix's old lane never has, and part of the answer.
6. Arms `phxclk250/500` only: `E2C clk` samplers show core at the configured value during the race
   (vc4 KMS asks the firmware for a core-clock floor at modeset — on bcm2711 500 MHz during the commit
   — so `force_turbo=1 core_freq=250` may be overridden or clamped), `E2C stk … kms-mode` is
   1920×1080, and no `E2C dmesg` line reports a vc4 modeset/clock error. If core is not 250 during
   the race, that arm does not isolate the software stack; say so instead of grading it.

## Pre-registered readings (STK run 2; boots in the order `stock`, `phxclk250`, `phxclk500`)

`stock` runs first: it is the config Pi OS ships and has the fewest unknowns (pipe-cleaner for the
whole setup). `phxclk250` holds the clocks equal to the Phoenix trials, so it isolates the software
stack — the table below is graded on it (if gate 6 fails, on `stock`, reading the clock difference
through E2b's +13 % for core 250 → 500); `phxclk500` pairs with Phoenix's core-500 numbers (8.39 fps).
**Headline number: `render_ms_per_frame` against Phoenix's 91 ms.**

| outcome (`phxclk250`, STK run 2) | reading | next |
|---|---|---|
| render ms/frame ≤ 0.5 × Phoenix's 91 ms | the GPU executes the same frame ≥ 2× faster under Pi OS's Mesa/kernel: the gap is in our GPU path | diff the two stacks at the job level: job count/sizes (render jobs/frame vs our 8), Phoenix Mesa patches (early-Z, tiling), shader variants; E2b's counters vs Pi OS perfmon (follow-up below) |
| render ms/frame within ± 15 % of 91 ms | same GPU cost per frame: **the "3×" is not a V3D-execution gap** | compare fps: any fps gap is CPU/present (E2's 40 ms CPU share; libc, allocator, clocks) — E2 already bounds async submit at ≤ ×1.43 |
| fps ≥ 2 × 7.4 but render ms/frame ≈ Phoenix | Pi OS overlaps CPU and GPU (async submit) and/or has less CPU per frame | M1 (async render server) is the lever; measure Phoenix CPU per frame vs `1000/fps − render_ms` here |
| STK fails to reach gameplay (no `scene complexity` / renderer not GLES on V3D) | E2c STK void | fix the environment first (see Risks) |

`stock` vs `phxclk250` on Pi OS then gives the clock share on the reference stack (compare with
E2b's +13 % for core 250 → 500 on Phoenix). quakespasm: if Pi OS's timedemo is ≥ 60 it is vsync
capped, and the result reads "≥ 60 vs 30.4 / 38.2"; its `gpu-process` render ms per timedemo frame
is still uncapped.

## Risks and known differences

* **Never booted with this script.** The pieces are checked on the host (dash syntax; the awk
  summaries on synthetic HUD logs and `gpu_stats` files with the rootfs's mawk; the STK and
  quakespasm binaries start under qemu-user and accept every option used), but modprobe, KMSDRM and
  the HUD on the real board are untested. The first boot should be run with `--only stk
  --stk-runs 1` if a short look is wanted.
* **Lite rootfs, no display server:** there is no X/Wayland; everything goes through SDL2's KMSDRM
  (STK, quakespasm) or DRM directly (kmscube, glmark2-es2-drm). That is a legitimate Pi OS stack and
  the closest one to Phoenix's full-screen path, but not the labwc desktop the cited 2023 numbers
  used. Without udev, SDL finds no input devices — irrelevant for timedemo/profile modes.
* **First-boot services** are bypassed by `init=`; nothing of systemd runs. The rootfs's
  `etc/resolv.conf` points at 10.43.0.1 (left from the WiFi lane) — irrelevant, no network name
  lookups are needed on the Pi.
* **NFS root:** STK loads ~200 MB of data over NFS (slower loading, not slower frames — the fps
  window starts after `scene complexity`), the shader cache and all logs live on NFS.
* **Pi OS's Mesa is newer by two point releases** and not patched like ours. That is the point of
  the comparison, but a difference found here names the *stack*, not yet the line of code.
* **SDL2 KMSDRM page flips are vblank-bound** (60 Hz): irrelevant for STK; caps quakespasm/kmscube.
  glmark2 off-screen and vkmark headless are uncapped.
* **Audio:** vc4 pulls in HDMI audio; Phoenix's apps have their own audio path. Minor CPU cost.
* **Board state:** the Pi has a written, bootable SD card in; dnsmasq selects the lane (server up =
  netboot), which is what e2c-cycle relies on. A **blank** card would stop the Pi booting at all
  (`feedback_selfflash_sd_via_netboot_linux`).
* **Kernel/module match:** `tftp/kernel8.img` is 6.18.34+rpt-rpi-v8 and so are the rootfs modules;
  kernel packages are on hold. If `tftp/` is ever refreshed, refresh both.
* **`e2c-cycle.sh` restore** calls the worker `netboot-server.sh up` with the Phoenix bootfs —
  the same `dnsmasq.conf` a Phoenix server-up generates, without the wrapper's export sync. It does
  not touch host NAT (`pi-internet-nat.sh`), which the switch did not change either. A SIGKILL of
  the script skips the trap.
* **HDMI port:** `video=` pins both HDMI-A-1 and HDMI-A-2 to 1080p60; `E2C hdmi … status=` names the
  connected one.

## Reproduce the install (host, no Pi)

`scripts/e2c-rootfs-chroot.sh <cmd>` runs a command in the rootfs through qemu-aarch64 binfmt
(needs `qemu-user-binfmt` with the F flag and passwordless sudo; binds /proc /sys /dev /dev/pts and
the host's `resolv.conf`, drops a `policy-rc.d`, undoes all of it on exit):

```
./scripts/e2c-rootfs-chroot.sh apt-get update
./scripts/e2c-rootfs-chroot.sh apt-mark hold linux-image-rpi-v8 linux-image-6.18.34+rpt-rpi-v8 \
    linux-image-rpi-2712 linux-image-6.18.34+rpt-rpi-2712 raspi-firmware linux-headers-rpi-v8 linux-headers-rpi-2712
./scripts/e2c-rootfs-chroot.sh apt-get install -y --no-install-recommends supertuxkart quakespasm \
    glmark2-es2-drm kmscube mesa-utils mesa-utils-bin libgl1-mesa-dri libegl-mesa0 libgles2 libgbm1 \
    mesa-vulkan-drivers vkmark
R=artifacts/linux-netboot/rootfs; E=/srv/phoenix-rpi4-nfs-gcc16
sudo mkdir -p $R/opt/e2c/stk $R/opt/e2c/quake/id1
sudo cp -r --no-preserve=ownership,mode $E/usr/share/supertuxkart/{data,stk-assets} $R/opt/e2c/stk/
sudo cp --no-preserve=ownership,mode $E/usr/share/quake/id1/{pak0.pak,config.cfg,autoexec.cfg} $R/opt/e2c/quake/id1/
sudo chmod -R a+rX $R/opt/e2c
```

## Follow-ups (not in this run)

* **Counters on Pi OS.** Mesa exposes the same V3D 4.2 performance counters through the kernel
  perfmon (`GL_AMD_performance_monitor`, `DRM_IOCTL_V3D_PERFMON_*`); a small LD_PRELOAD or an STK
  patch could read E2b's set A/B per frame and give the counter values Phoenix should reach
  (render QPU split, late-Z share with upstream early-Z, TLB/L2T traffic).
* The Phoenix side of the table for glmark2/kmscube/vkmark arrives with M3 (EGL/GBM).

## Result

*(empty until the first boot: per-arm tables of the E2C lines above, gates, reading.)*
