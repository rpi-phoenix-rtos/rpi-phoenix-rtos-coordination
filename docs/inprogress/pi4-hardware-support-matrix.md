# Phoenix-RTOS Raspberry Pi 4 (BCM2711) — Hardware Support Matrix

**Updated:** 2026-08-27. Canonical "where are we" reference for the Pi 4 port.
One row per peripheral/subsystem. For narrative gap analysis see
`docs/knowledge/scope-pi4-uncovered.md`; for live progress see `docs/inprogress/status.md`.

> **STATUS (2026-08-27):** the port is a working graphical + media machine with a real CLI/language
> ecosystem (coreutils 9.5, interactive bash 5.2, CPython 3.14, Redis 7.2, SQLite3, jq, Lua 5.4.7 — all
> HW-verified; see the ports table). **Games render on the GPU:** GLQuake (quakespasm) flagship;
> **Quake III renders gameplay fully lit** via the `quake3` RAM-staging launcher (the earlier q3dm7
> black-lightmap bug is fixed; residual = in-game mouse-look + an intermittent GPU binner wedge);
> **Quake II renders its demo in textured 3D** via the `quake2` RAM-staging launcher; **vkQuake**
> renders the full textured 3D start map through Vulkan/V3DV (the earlier post-menu hang is fixed —
> a semaphore lost-wakeup; residual = un-wired input). **WiFi** control-plane
> is up (WPA2 associated + 4-way keyed; data-plane under active debugging — not usable for
> networking); **Bluetooth** driver-level bring-up works (`/dev/hci0`, HCI Inquiry; no host stack).
> **Dillo browses the live HTTPS internet** via a host NAT gateway. The 2026-08-06 blurb below is
> retained for history.

> **STATUS (2026-08-06):** the port is a working graphical + media machine. **Games render on the
> GPU:** GLQuake (quakespasm) flagship, **vkQuake full textured 3D via Vulkan** (the old #29 no-WSI
> gap is FIXED — see the vkQuake row), **Quake II fullscreen 3D** (SDL2+ref_gl1), and Quake III
> engine+renderer (VM-exec banked). **SDL2 2.30.12** (fullscreen GL + input + audio) HW-validated.
> **E4: an ffmpeg decode core** now decodes **MJPEG and H.264** correctly on HW and **plays moving
> video on the HDMI screen** (`tools/ffmpeg-port/`, `/dev/fb0` sink). libphoenix **libm** filled
> (rint/rounding/min-max + exp2/log2f/erf/erfc/scalbn, regression-tested); **libdbg** in-process
> backtrace corelib (kernel-side B2 feasibility done); Dillo builds HTTPS-capable (mbedTLS). The old
> 2026-06-26 blurb below is retained for history.

> **STATUS (2026-06-26):** since the 2026-06-18 pass — the X11 software desktop is fully
> live on HW (Xphoenix kdrive fbdev DDX + kbd/mouse input + JWM + Window Maker WMs + xterm
> running a BusyBox shell, #30/#35/#36); GLQuake is the working flagship (mouse #24, QUIT/
> fbcon restore #25, LAN multiplayer #26, NFS-root #27, torch flame #28); vkQuake reached
> 2D GPU raster on HW, paused at the no-WSI texture-upload gap (#29); VideoCore mailbox is
> serialized via the rpi4-vcmbox server; logging→/var/log shipped (#31); a stress-test
> suite ran clean across all layers (#38-40). Rows below updated accordingly.

**Status legend:**
- ✅ **done** — works on hardware, committed, validated.
- 🟡 **partial** — usable but incomplete / a known sub-feature missing.
- 🔬 **groundwork** — mechanism proven, but a deliberate decision/step remains.
- ⏸ **attended** — implementable but deferred to a human-attended session
  (boot-risk, statistical-regression, or needs a screen/scope/bench rig).
- ⛔ **blocked** — stuck on an external dependency (datasheet/JTAG/firmware/HW).
- ⬜ **not started**.

| Subsystem | Status | Evidence / entry point | Remaining |
|---|---|---|---|
| CPU bring-up, EL2→EL1, MMU | ✅ done | boots to userspace; **caches ON** (SCTLR.{M,C,I}, all Normal RAM WB-cacheable) since 2026-05-17 (TD-16 RESOLVED) | the once-proposed "make the GENET RX DMA pool cacheable" lever (Policy B) was TRIED and **CONCLUDED UNVIABLE** — corrupts the GPU framebuffer under load (#11 RE-OPENED, default-off); no global cache switch remains |
| SMP (4 cores) | ✅ done | **4-core SMP scheduling works** (`NUM_CPUS=4U`; secondaries re-arm their own CNTV + run the scheduler; TD-01/TD-11 resolved) (`project_smp_d7_d8_findings`) | the old "cpu0-only" state is FIXED — do not cite |
| Generic Timer | ✅ done | scheduler tick / delays | — |
| Interrupts (GIC-400) | ✅ done | GENET/USB/SD IRQs live | — |
| PL011 UART console | ✅ done | primary console + klog mirror | TD-14 two-owner UART polish (#127) |
| VideoCore property mailbox | ✅ done | userspace (thermal/clocks/power) | kernel-internal primitive ⏸ (for WiFi/BT/DVFS) |
| HDMI framebuffer **console** (fbcon) | ✅ done | klog+psh on HDMI (Tier 0) | slow fills (CPU writes to the uncached fb pages; caches are globally ON) |
| HDMI framebuffer **device** `/dev/fb0` | 🟡 partial | device LANDED + HW-validated netboot (#148): read/write + `RPI4FB_GETMODE` devctl, `video/rpi4-fb/` | attended (#149): fbdev `FBIOGET_*` veneer (Tiny-X), true `mmap(fd,0)` kernel backing, drawing/display-ownership |
| GENET Ethernet | ✅ done | Tier 5, IRQ-driven, ping ~0.9 ms | — |
| lwIP / DHCP / ICMP / UDP | ✅ done | autonomous DHCP, diag-udp :9999 | — |
| USB host (PCIe→VL805 xHCI) | ✅ done | **enum 11/11 cold boots** after the #129 two-step-BSR AddressDevice fix (devices `53383d1`) + TRSTRCY (usb `47eede9`) + #121 dc-civac uncached-page eviction (usb `12c4fe8`) | IRQ event path #145 (perf) ⏸; daemon hardening #142/#143 ⏸ |
| USB HID (kbd + mouse) | ✅ done | `/dev/kbd0`+`/dev/mouse0`, live keys→psh (#122/#124/#126) | — |
| USB mass storage | ⬜ not started | — | umass driver |
| PCIe RC / VL805 inbound abort (TD-10) | ⏸ attended | SError handler in (#109); abort isolated to PCIe/USB bring-up | unmask SError = boot-risk; root-cause #144 |
| SD card (EMMC2 SDHCI) | ✅ done | `/dev/mmcblk0[pN]` (#119); **reads ~38 MB/s (UHS-I DDR50, 4-bit, SDMA)**; writes correct via #154 CMD13-poll completion (16/16 0 faults) | full-speed writes = an SDMA-write path, IMPLEMENTED but gated off at `sdcard.c:1625` (`&& dir==sdio_read`); flip + HW-validate — currently HW-blocked (no card in Pi/host) (`project_pi4_sd_fullspeed_state`) |
| ext2 persistent rootfs (#120) | ✅ done | mounts as `/`, binaries exec from it (`ifconfig`), boots to psh stably; HW-validated SD-boot 0/10 faults. Crash root cause was a **fs pool-thread stack overflow** (8 KB default too small) — fixed by `storage_run(2, 16*_PAGE_SIZE)`, full multithreading kept, ext2 unchanged | residuals: noisy-but-recovering 50 MHz Data-CRC (signal polish), single-block-only CMD24/CMD18 (perf) |
| SoC thermal + throttle | ✅ done | `/dev/thermal`,`/dev/throttled` (2026-06-05) | firmware owns the trip (telemetry only) |
| Hardware RNG (RNG200) | ✅ done | `/dev/hwrng` (2026-06-05); **now also backs `/dev/urandom`** (posixsrv reads `/dev/hwrng` for entropy, rand() fallback) — HW-verified 2026-06-17 | kernel `getrandom()`/pool wiring (libc-level) still PRNG |
| Watchdog / reboot / poweroff | ⏸ attended | works via diag-udp `r`/`h` (PM block #43) | productionize `_hal_systemReset` (kernel, boot-risk) |
| WiFi (BCM43455 SDIO) | 🟡 partial | **control-plane up** — the firmware executes, the driver associates to a real WPA2-PSK AP and completes the 4-way key handshake (`tools/wifi-probe` `join`; `project_wifi_fw_exec_gate_91`) | **data-plane does not carry traffic yet** — TX reaches the firmware but not the air (SDPCM seq/credit); under active debugging (owner E7). Not usable for networking — use wired Ethernet. Then WPA3 |
| Bluetooth (BCM43455 UART HCI) | 🟡 partial | **driver-level bring-up** — `/dev/hci0` up over self-routed mini-UART, firmware patchram 323/323, real BD_ADDR read, HCI Inquiry completes (`tools/bt-probe`, `project_bluetooth_bringup`) | **no host Bluetooth stack** — no pairing, profiles, or audio yet |
| GPIO / pinctrl | 🟡 partial | `/dev/gpio` read-only observer device (#150): snapshot + per-pin `RPI4GPIO_GETPIN` devctl, `gpio/rpi4-gpio/` | **outputs** (GPSET/GPCLR/fsel set) need a bench rig to validate (⏸) |
| I²C / SPI / PWM | ⬜ not started | plans exist | need GPIO alt-fn + clock-manager |
| GPU (V3D 4.2) — OpenGL | ✅ done | ported Mesa gallium v3d driver + GL frontend (`tools/v3d-driver-port/`); **GLQuake (quakespasm) runs ~40-50fps@1080p** via render-to-scanout; R/B color + particle render-stall fixed (2026-06-16/17); **early-Z re-enabled** (06-22) + **triple-buffer page-flip** landed; mouse #24, QUIT/fbcon-restore #25, LAN/direct-IP multiplayer #26/#68, NFS-root #27, torch flame #28 all done. **★ 2026-08-27: SuperTuxKart 1.4 (modern GLES3/SP renderer) PLAYS** — boots→full engine init (ES 3.1 ctx + Irrlicht COGLES2 + SFX/Music + all SP shaders)→18 karts/41 tracks→**main menu**→**fully-lit in-game 3D race** on HDMI, 0 crashes (`project_supertuxkart_feasibility`). Two V3D driver fixes shipped en route: **GPU VA window 256 MiB→1 GiB** (`GPUVA_PT_PAGES`, fixes heavy-scene VA-exhaustion crash) + **QPU-interrupt-ack fix** (uncleared CTL_INT QPU bits stalled the CT1 render under heavy deferred-lighting → wedge; Linux-parity full-status clear; STK render wedges 330→0, scene lit). Plus **Mesa on-disk shader cache** implemented (was stubbed) — GL apps no longer recompile shaders on the V3D every boot (HW-verified 52-blob cold/all-hit warm) | gamma retune (cosmetic), audible audio sign-off (attended), formal multi-boot soak; shader-cache invalidation is manual (bump `V3D_PHX_CACHE_VERSION` on Mesa-codegen changes) |
| GPU (V3D 4.2) — Vulkan (V3DV) | ✅ working (driver) | full ported Mesa V3DV (`libv3dv`); **vkQuake renders textured 3D on the V3D** (real SPIR-V VS+FS → NIR→QPU, render passes, TFU texture uploads land); no-WSI fb0 scanout; the torch/fullbright **alpha-scanout** bug is FIXED (opaque present alpha=1, `project_vkquake_torches_dark_fullbright`, vkQuake d3e329c pushed) | app-level only: **vkQuake keyboard/mouse input is not yet wired** (the earlier post-menu hang is FIXED — libphoenix semaphore lost-wakeup `e75c4fe`); an intermittent V3D binner wedge on long GPU runs (not Vulkan-specific; **the heavy-fragment CT1 *render*-stage wedge class was root-caused + fixed 2026-08-27 — uncleared QPU-interrupt bits, see the OpenGL row's QPU-int fix; a residual intermittent q3dm7-class binner/CT0 wedge with a different signature remains banked/owner-attended**); RT gated off (V3D lacks ray_query) |
| Video decode | 🟡 SW done / HW scoped+M0 | **Software:** ffmpeg decode core does **MJPEG + H.264** on the CPU and plays moving video on `/dev/fb0` (`tools/ffmpeg-port/`, e4-play). **Hardware (2026-08-27 scoped):** H.264 HW decode = **VideoCore-firmware/VCHIQ wall → banked** (WiFi-scale ~27k LOC, no MMIO H264 block on BCM2711); **H.265/HEVC via the `rpivid`/hevc_dec block = tractable + M0 HW-PROVEN** — directly-MMIO, no VCHIQ (`tools/hevc-probe`: HEVC block @0xfeb00000 reachable, version 0x202, clock via mailbox id 11) (`project_ffmpeg_hw_decode_scope`) | HEVC M1-M4 (~4-8 wk register decode core + SAND-tiled de-tile) = **OWNER-GATED** (decodes H.265 not H.264; a content-strategy call) |
| Audio (PWM / I²S / HDMI) | 🟡 partial | PWM driver `/dev/audio0` (`audio/rpi4-audio/`): **continuous streaming DMA** (free-running self-chained ring, PWM1=DREQ 1) feeds the FIFO; `write()` fills the ring w/ usleep backpressure (driver sleeps, no spin); PIO fallback retained. **Quakespasm SNDDMA backend** (feeder thread) mixes over it — "Audio: 16 bit, stereo, 44100 Hz", demo renders, 0 faults/underruns (2026-06-17). **SDL2 audio driver** over `/dev/audio0` HW-validated (driver=phoenix, 44100/S16/2ch, tone played, 0 faults, 2026-08-05) | audible jack sign-off ⏸ (headphones); vkQuake reuses the backend; underrun→ring-loop artifact (steady state ok) |
| DMA | ✅ done | legacy BCM2711 DMA-channel driver **proven + in production for audio** (`rpi4-audio`: self-chained streaming CB, DREQ-paced, low-1GB C0 bus alias); **SD reads use the eMMC SDHCI SDMA engine** (~38 MB/s DDR50, multi-block CMD18) — the DMA path is validated on HW | open items are both optional/deferred, not functional gaps: a **generalized reusable DMA-helper API** (audio drives DMA inline today — YAGNI until a 2nd consumer such as I²C/SPI/PWM needs it, at which point the helper is extracted against a real second use) and **SD DMA *writes*** (a BCM2711 SDHCI DMA-write quirk keeps writes PIO — tracked in the SD-card row, not here) |
| RTC | 🟡 capability present | Pi 4 has no on-SoC RTC. The **`ntpclient` psh applet** queries SNTP + calls `settimeofday` (kernel `settime` syscall + libphoenix `settimeofday`/`clock_settime` all present) → NTP-over-GENET works | **★ 2026-08-08 VALIDATED end-to-end**: with E2 internet up, `ntpclient -s pool.ntp.org` synced the clock 1970→2026 and enabled CA-verified HTTPS (the E3 cert clock). Still manual per boot — baking it into a boot step is deferred (risky nfsroot rc-model change) |
| Camera (CSI-2) / DSI display | ⬜ not started | — | — |
| posixsrv / psh userspace | ✅ done | pipes, ptys, `/dev/{null,zero,urandom,full}` (urandom now HW-RNG-backed), interactive psh; **AF_UNIX SOCK_STREAM** + **libc `getrandom()`/`getentropy()`** validated on HW (`misc/rpi4-ipcprobe`, 2026-06-17) | psh has no `|` pipe parsing |
| X11 / windowing (kdrive) | ✅ done | host-side `tools/x11-port/`: full client+render+font+toolkit lib stack + kdrive xorg-server core build for aarch64-phoenix. **LIVE ON HW:** Xphoenix (fbdev DDX → shadow → /dev/fb0, periodic full-screen flush) with real kbd+mouse input (`/dev/kbd0`+`/dev/mouse0` via the DDX after FBCON_DISABLED), running **xeyes (mouse-tracking)**, the **JWM** and **Window Maker** window managers (#30/#35), and **xterm** with a live BusyBox shell (#36). | **★ 2026-08-09 WINDOWED GPU ACHIEVED** — accelerated V3D OpenGL renders in an X window (`gl-x11-window`: offscreen FBO + glReadPixels + XPutImage, single libX11+libGL process — sidesteps the structurally-blocked GLX/DRI/Glamor route: no DRM node / no PRIME / no dlopen). Also **WM-managed GPU** (twm-decorated), a **multi-app desktop** (twm + GPU + xcalc + xeyes), and a **media desktop** (concurrent V3D GPU + ffmpeg video + WM). **★ 2026-08 GLAMOR GPU-ACCELERATED 2D X now runs on V3D 4.2** (`Xphoenix-glamor`, renders to HDMI; shadow-RAM SW cursor fixed 2026-08-27; the `/sbin/rpi4-v3d` daemon lets an accelerated desktop + a second GPU client run at once). (`project_x11_gpu_windowed_feasibility`). Cross-process DRI3/PRIME buffer-sharing stays blocked; full XFce open |

## Ported libraries & applications

| Component | Status | Notes |
|---|---|---|
| Mesa V3D OpenGL stack (`libGL/libv3d-phoenix.a`) | ✅ | GL 2.1 on real V3D 4.2, in-process winsys, no-WSI fb0 scanout (`project_pi4_v3d_scout`) |
| Mesa V3DV Vulkan stack (`libv3dv`) | ✅ | SPIR-V → NIR → QPU; textured 3D on HW; no WSI (fb0 scanout) |
| **SDL2 2.30.12** (`ports/sdl2`) | ✅ HW-validated | fullscreen GL + input (kbd0/mouse0) + audio (/dev/audio0) all proven on Pi; phoenix video/GL/input/audio drivers; org `ports c191d20`. Vulkan backend = phase 2 (needs V3DV WSI). `dlopen`→static, GPL-glue kept out of zlib `libSDL2.a` (`project_sdl2_port`) |
| X11 desktop (kdrive/Xphoenix) | ✅ HW-validated | fbdev DDX → /dev/fb0, kbd+mouse input, xeyes/xterm/xcalc/xedit + JWM/Window Maker WMs (`project_x11_lib_port`) |
| QuakeSpasm (GLQuake) | ✅ HW-validated | textured GLQuake ~40fps@1080p, demos + SP map + direct-IP multiplayer (#68 fixed 2026-08-10, in-game 0 faults) (`project_quakespasm_port`) |
| vkQuake (Vulkan Quake) | 🟡 renders, input WIP | textured 3D via Vulkan on V3D; torch/alpha-scanout fixed (d3e329c); the earlier **post-menu hang is FIXED** (a libphoenix counting-semaphore lost-wakeup, `e75c4fe`) — vkQuake now renders the full textured 3D start map (3150+ frames). Remaining: **keyboard/mouse input not yet wired**; an intermittent V3D binner wedge on long GPU runs (`project_vulkan_v3dv_port`) |
| yQuake2 (Quake II, `ref_gl1`) | ✅ HW-validated | single-ELF (dlopen→static); **renders its demo in full textured 3D** on V3D via SDL2+ref_gl1, launched by the **`quake2`** RAM-staging launcher (copies assets to a `/tmp` ramdisk then runs — fixes the black screen seen on slow NFS texture loads); 0 faults (`project_quake2_port`) |
| Quake III (quake3e) | 🟡 renders, in-game mouse WIP | launched by the **`quake3`** RAM-staging launcher; **renders gameplay fully lit on V3D GL @1080p**. The earlier `q3dm7` **black-lightmap-sector** bug is **FIXED** — a Phoenix `should_tile` gate wrongly forced the ≥1024² lightmap atlas to a linear layout; excluding sampled textures from that gate restores tiling (host-vs-Pi visual parity SSIM 0.989). Remaining: **in-game mouse-look** (console text input is wired via `SDL_TEXTINPUT`) (`project_quake3_port`, `project_quake3_lightmap_uif_xor`) |
| **ffmpeg decode core (E4)** | ✅ HW-validated | **MJPEG + H.264** decode correct on HW (bit-exact vs host ffmpeg) and **moving video plays on the HDMI screen** (decode → YUV→RGB → `/dev/fb0`, paced loop); reproducible LGPL-clean scaffold `tools/ffmpeg-port/`; h264 needs an 8 MB-stack thread. **★ 2026-08-09 ALSO PLAYS IN AN X WINDOW** (`e4-x11-play`: decode → XPutImage into an X window under Xphoenix; 2730 frames, 0 faults) — and concurrently with a live GPU app in the media desktop. Remaining = audio/demux/A-V-sync for a full player (`project_ffmpeg_e4_feasibility`) |
| Dillo / mc / glib2 | ✅ HW-validated | render on fbcon; Dillo HTTPS-capable via mbedTLS (E1). **★ 2026-08-08 E2+E3 DONE: Dillo BROWSES THE LIVE HTTPS INTERNET under Xphoenix** — rendered example.com over CA-verified TLSv1.2 on HDMI, via host NAT (`pi-internet-nat.sh`) + Phoenix `route add default gw 10.42.0.1 dev en1` + dnsmasq opt3/6 + `ntpclient` cert-clock (`project_pi4_internet_e2_feasibility`, `project_dillo_https_tls`). Dillo is FLTK/core-X (not fontconfig); file:// needs dpid (unstaged), http/https in-process |
| libphoenix libm + libdbg (corelibs) | ✅ | libm gaps filled (rint/rounding/min-max + exp2/log2f/erf/erfc/scalbn, regression-tested in `phoenix-rtos-tests/libc/math`); **libdbg** reusable in-process crash/hang backtrace corelib (`project_libphoenix_libm`, `project_libdbg_facility`) |
| GNU coreutils 9.5 | ✅ HW-validated | ~102 tools built + HW-verified; `seq`/`wc`/`sha256sum` bit-exact; a handful skipped that need OS facilities Phoenix lacks (`stat`/`stty`/`df`/…) (`project_coreutils_port`) |
| GNU bash 5.2 | ✅ HW-validated | bash 5.2.21 runs **fully interactively** at the console — prompt, command execution, pipes/loops/vars/command-substitution, stays until `exit` (HW-verified). The earlier "self-exits on EOF at the prompt" was a libphoenix `select()` NULL-timeout bug (a NULL/infinite timeout returned 0 immediately instead of blocking, so readline saw EOF) — fixed in `libphoenix sys/select.c` (`project_bash_port`) |
| CPython 3.14 | ✅ HW-validated | static `python3` 3.14.4 with `sqlite3`, `zlib`, `_ssl`/HTTPS, `_decimal`, `ctypes` + `.so` C-extension `dlopen` (`project_python_port`) |
| Redis 7.2 | ✅ HW-validated | Redis 7.2.4 data-store service over lwip TCP; 241 commands, 0 faults (`project_redis_port`) |
| SQLite 3 | ✅ HW-validated | full SQL, in-memory + on-disk file VFS; `integrity_check=ok` (`project_sqlite_port`) |
| jq | ✅ HW-validated | jq 1.7.1, selfcheck + run-tests all pass (`project_jq_port`) |
| Lua 5.4.7 | ✅ HW-validated | interpreter + `luac`; selfcheck ALL-OK (`project_lua_port`) |
| curl / BusyBox | ✅ HW-validated | curl over mbedTLS (HTTP/HTTPS); BusyBox shell utilities |

## Build / test infrastructure (✅)

- Two build variants: `rebuild-rpi4b-fast.sh --variant netboot|sd` (2026-06-05).
- Netboot loop: `test-cycle-netboot.sh` (UART + HDMI snapshots + diag-udp `--probe`).
- Network observability: diag-udp responder on :9999 — full command + `/dev`-node
  reference in **[docs/knowledge/diag-udp-reference.md](../knowledge/diag-udp-reference.md)** (`c` clocks+thermal,
  `r`/`h` reboot/halt, `g` GPIO, `V` framebuffer probe, `R` device-read smoke test,
  `D` devnodes, plus the WiFi/SDIO bring-up set).
- Deterministic rollback: `snapshot-/restore-integration-state.sh` + `manifests/`.

## What "fully supported" still needs (priority order)

1. **USB** is functionally complete (enum + HID); the remaining items (#142/#143/#144/#145)
   are *hardening/perf/root-cause* and are **attended** (statistical regression or boot-risk).
2. **ext2 rootfs** (#120) — DONE (mounts as `/`, exec-from-card, boots to psh); **NFS rootfs**
   also DONE + HW-proven (`project_nfs_rootfs_feasibility`). **Direct exec from NFS FIXED** (the
   `object_fetch` short-read corruption — kernel `f145658f`); residual (root-caused 2026-08-05,
   `project_large_binary_exec_hang`): large-**BSS** binaries (~19 MB text / big BSS, e.g. yquake2's
   26.5 MB BSS) intermittently **silently hang** at exec — Phoenix eagerly commits BSS page-by-page
   + `hal_memset`s it under `map->lock`, and that long exec window stalls over the flaky netboot
   NFS. NOT the old `-ENOMEM at process_load:704` (that note is STALE — current code forces only ELF
   headers). Mitigation: trim the linked stack; proper fix: demand-page exec-time anon (kernel).
   Other residuals: #156 first-access ENOENT (boot-order race), perf/signal polish.
3. **fb0 driver** — decide ABI + display ownership, then implement (attended).
4. **X11** — DONE: the software kdrive desktop (Xphoenix + fbdev DDX + kbd/mouse input + JWM/
   Window Maker WMs + xterm) is live on HW. Remaining is the *accelerated* GPU-X research stretch.
5. **WiFi #91** — control-plane up (firmware executes, WPA2 associated + 4-way keyed); the
   **data-plane** (carrying traffic) is the remaining work, under active debugging.
6. **Bluetooth** — driver-level bring-up done (`/dev/hci0`, HCI Inquiry); needs a host BT stack.
7. Greenfield: DMA framework → audio/I²C/SPI/PWM; GPIO full driver.

## Unattended-vs-attended note

Overnight/autonomous netboot work is restricted to **additive + deterministic-self-log +
cannot-silently-regress** items (see `feedback_unattended_scoping` memory). The ⏸ rows above are
attended precisely because their failure is either physically unrecoverable over netboot
(kernel/reboot), statistically invisible to single-boot validation (USB daemon internals), or
needs human judgement (a screen/scope/bench rig).
