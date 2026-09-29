# Phoenix-RTOS Raspberry Pi 4 (BCM2711) — Hardware Support Matrix

**Updated:** 2026-09-30. Canonical "where are we" reference for the Pi 4 port.
One row per peripheral/subsystem. For how to use each feature see the
[User Guide](USER-GUIDE.md); for live progress see `docs/inprogress/WEEK-<ISO-week>.md`.

> **STATUS (2026-09-30) — one image, one graphics stack.** Every program is a framework port
> installed into the root filesystem. `loader.disk` holds only the kernel, the drivers and the
> servers.
>
> **Graphics.** The Pi has a single, DRM-shaped graphics stack:
> - `rpi4-v3d-async`, the render server (`/dev/v3d-async`);
> - `rpi4-kms`, the KMS display server (`/dev/kms`);
> - `shmsrv`, shared memory (`/shm`);
> - libdrm, Mesa 26.2 (GBM/EGL/GLES/GL/Vulkan), SDL 2.30 (KMSDRM + Wayland), Xorg 21.1
>   (modesetting + glamor), labwc 0.20 and XFCE 4.20 on GTK 3.24.
>
> All three servers start at boot. The image gate of 2026-09-29 (MIGRATION §7r) passed 7/7 with
> 0 faults (fps at the page flip, 1080p):
>
> | Program | fps |
> |---|---|
> | Quake II | 59.8 |
> | Quake III | 58.8 |
> | QuakeSpasm | 43.8 |
> | vkQuake | 42.2 |
> | SuperTuxKart | 12.8 |
> | the X desktop's GL window | 60.0 |
> | the XFCE session | clean start and logout |
>
> SuperTuxKart runs at 22.3 fps at 1280×720 scaled by the display (M9).
>
> **Dual-mode programs.** Each game and the video player is one program with both SDL video
> drivers: full screen from psh, in a window on the desktop. Measured in a window on the XFCE
> desktop (M8, M10): Quake II 60 fps, Quake III ~90, QuakeSpasm 45–67, the video player 30.
>
> **WiFi** ships in the image (the daemon starts at boot, firmware from linux-firmware).
>
> <!-- TODO(coordinator): the merged image (desktop apps as ports, P3 removal, WiFi in the image)
> is being built; replace "2026-09-29 gate" with its first gate once it has run. -->

The graphics milestones and experiments are recorded in [docs/gpu-new-lane/](gpu-new-lane/PLAN.md)
(engineering history). Earlier status summaries are in git history.


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
| SMP (4 cores) | ✅ done | **4-core SMP scheduling works**, and since the 2026-09-09 upstream merge the priority space is **64 wide** (a 64-bit ready bitmask replaced the linear ready-queue scan; `psh/cpuburn`'s worker clamp moved 6 → 62) (`NUM_CPUS=4U`; secondaries re-arm their own CNTV + run the scheduler; TD-01/TD-11 resolved) (`project_smp_d7_d8_findings`) | the old "cpu0-only" state is FIXED — do not cite |
| Generic Timer | ✅ done | scheduler tick / delays | — |
| Interrupts (GIC-400) | ✅ done | GENET/USB/SD IRQs live | — |
| PL011 UART console | ✅ done | primary console + klog mirror | TD-14 two-owner UART polish (#127) |
| VideoCore property mailbox | ✅ done | userspace (thermal/clocks/power) | kernel-internal primitive ⏸ (for WiFi/BT/DVFS) |
| HDMI framebuffer **console** (fbcon) | ✅ done | klog+psh on HDMI (Tier 0); hands the display to `rpi4-kms` for graphics programs and takes it back when they exit (`KMS srv console handover`) | slow fills (CPU writes to the uncached fb pages; caches are globally ON) |
| HDMI display server `rpi4-kms` (`/dev/kms`) | ✅ done | `video/rpi4-kms/` in phoenix-rtos-devices, started at boot: the firmware's display planes (`planes=0x81`), vblank events from the SMI interrupt, atomic page flips at 60.00 fps (600/600, interval 16665–16670 µs), dumb-buffer pool, fences from the render server (`-G`), `pollNotify` wake-ups, **scaled modes** 1600×900 … 640×480 filled or letterboxed by the display hardware (M9), console handover (`-C`) | no fbdev emulation (`/dev/fb0` is not provided); one CRTC (HDMI0) |
| GENET Ethernet | ✅ done | Tier 5, IRQ-driven, ping ~0.9 ms | — |
| lwIP / DHCP / ICMP / UDP | ✅ done | autonomous DHCP | — |
| USB host (PCIe→VL805 xHCI) | ✅ done | **enum 11/11 cold boots** after the #129 two-step-BSR AddressDevice fix (devices `53383d1`) + TRSTRCY (usb `47eede9`) + #121 dc-civac uncached-page eviction (usb `12c4fe8`) | IRQ event path #145 (perf) ⏸; daemon hardening #142/#143 ⏸ |
| USB HID (kbd + mouse) | ✅ done | `/dev/kbd0`+`/dev/mouse0`, live keys→psh (#122/#124/#126) | — |
| USB mass storage (USB 3) | ✅ done | **A USB 3 stick mounts and is read/written as a real filesystem.** `umass` (BOT/SCSI, filter `08/06/50`) wired for aarch64 2026-09-20; enumerates at **SuperSpeed** (`maxpkt=1024 burst=4`, root port 2) after the framework gained a SuperSpeed concept (`phoenix-rtos-usb` `877cace`) — raw read 29.3 → **59.1 MB/s**. `/dev/umass0`,`/dev/umass1` per MBR partition, ext2 via libext2, 64 KiB block cache. Raw write **18.2 MB/s** (1 GiB; 39.9 at 256 MiB = the stick's SLC cache), ext2 read **47.1**. Metadata write amplification **21.0x → 1.00x** (libcache ranged write-back, `cache_setFlushGranularity`). ⚠ Getting here cost **eleven libext2 defects**, every one silent and passing by return code, several data-losing — a hole read back the **superblock**; unmount **deleted the files it had cached**; on any block size above 1 KiB the allocator reserved bit N and handed out block **N+1**. `e2fsck -fn` on a full 1 GiB read-back of the device is now completely clean across create/write/delete/umount/remount, on **both** 4 KiB and 1 KiB block sizes | ⏭ **hot-plug**: the removal path is implemented and reviewed (unlink under the lock, tear down outside it — the fix for a self-deadlock), but a physical insert/remove cycle has not been exercised. Prefetch/overlap could recover the residual ~10% of read throughput; judged not worth the concurrency risk |
| PCIe RC / VL805 inbound abort (TD-10) | ⏸ attended | SError handler in (#109); abort isolated to PCIe/USB bring-up | unmask SError = boot-risk; root-cause #144 |
| SD card (EMMC2 SDHCI) | ✅ done | `/dev/mmcblk0[pN]` (#119); UHS-I DDR50, 4-bit; writes correct via #154 CMD13-poll completion. **★ 2026-09-20: ADMA2 32-bit scatter-gather is the data path for BOTH directions** — the engine Linux runs on this silicon. The long-recorded "BCM2711 write-DMA quirk" was **ours**: a raw CPU-physical address where the emmc2bus `dma-ranges` wants a BUS address (`SDCARD_DRAM_BUS`), the one DMA master in the tree skipping a translation `rpi4-audio` and the kernel both apply. End-to-end (`/usr/bin/dd`, 64 MiB): PIO 12.2 · SDMA 12.3 · **ADMA2 12.7–12.9 MB/s**; the SD bus bounds all three. Gated 6/6 on netboot **and** 6/6 on the SD lane (0 faults, 0 ADMA errors, 6/6 boots found both partitions) | ⏭ 1.8V/DDR50 switch is best-effort on SD-boot (falls back to HS50). ADMA2's structural headroom — no 512 KiB request cap, no contiguous buffer — is unexploited: raising `SDCARD_MAX_TRANSFER` and dropping the bounce copy are the next levers |
| ext2 persistent rootfs (#120) | ✅ done | mounts as `/`, binaries exec from it (`ifconfig`), boots to psh stably; HW-validated SD-boot 0/10 faults. Crash root cause was a **fs pool-thread stack overflow** (8 KB default too small) — fixed by `storage_run(2, 16*_PAGE_SIZE)`, full multithreading kept, ext2 unchanged | residuals: noisy-but-recovering 50 MHz Data-CRC (signal polish), single-block-only CMD24/CMD18 (perf) |
| SoC thermal + throttle | ✅ done | `/dev/thermal`,`/dev/throttled` (2026-06-05) | firmware owns the trip (telemetry only) |
| Hardware RNG (RNG200) | ✅ done | `/dev/hwrng` (2026-06-05); **now also backs `/dev/urandom`** (posixsrv reads `/dev/hwrng` for entropy, rand() fallback) — HW-verified 2026-06-17 | kernel `getrandom()`/pool wiring (libc-level) still PRNG |
| Watchdog / reboot / poweroff | ⏸ attended | no software reboot today: `hal_cpuReboot` halts in place; the PM-watchdog `r`/`h` path (#43) lived only in the diag-udp responder, removed in lwip `05b8ba4` | productionize `_hal_systemReset` (kernel, boot-risk) |
| WiFi (BCM43455 SDIO) | 🟡 in the image | **ships in the image since 2026-09-30**: `rpi4-wifi` (phoenix-rtos-devices `wifi/rpi4-wifi/`) starts at boot on the sd and nfsroot variants and loads the BCM43455 firmware from `/lib/firmware/brcm/` (linux-firmware `20260810`, sha256-pinned, licences alongside). `wifi connect <ssid> <psk>` / `disconnect` / `status` / `scan`; the `wl` lwip netif follows `/etc/wifi.conf` and rejoins after a reboot or a lost association; WPA2-PSK + DHCP; TX 3.6 / RX 3.3 MB/s (cycle T) | first boot check of the image with the boot-time daemon (cycle W1, `docs/misc/2026-09-30-wifi-in-image.md`); an SSID with a space and a 64-hex PSK; WPA3 and newer firmware need a host-side supplicant |
| Bluetooth (BCM43455 UART HCI) | 🟡 partial | **driver-level bring-up** — `/dev/hci0` up over self-routed mini-UART, firmware patchram 323/323, real BD_ADDR read, HCI Inquiry completes (`tools/bt-probe`, `project_bluetooth_bringup`) | **no host Bluetooth stack** — no pairing, profiles, or audio yet |
| GPIO / pinctrl | 🟡 partial | `/dev/gpio` read-only observer device (#150): snapshot + per-pin `RPI4GPIO_GETPIN` devctl, `gpio/rpi4-gpio/` | **outputs** (GPSET/GPCLR/fsel set) need a bench rig to validate (⏸) |
| I²C / SPI / PWM | ⬜ not started | plans exist | need GPIO alt-fn + clock-manager |
| GPU (V3D 4.2) — render server + OpenGL / GLES | ✅ done | **`rpi4-v3d-async`** (`gpu/rpi4-v3d-async/`, started at boot): owns the V3D, asynchronous multi-queue submit, fence page and sync objects, deferred replies; buffers shared with `rpi4-kms` and the clients through the kernel's `memExport`. **Mesa 26.2** gallium `v3d` (`mesa_drm` port, 16 patches) with GBM and EGL (drm, surfaceless, Wayland, X11) on **libdrm-phoenix**. kmscube 60.00 fps; SuperTuxKart 11.9 fps at 1080p = Raspberry Pi OS on this board (11.7); Quake II 60 fps vsynced after the SDL frame-pacing fix | no on-disk shader cache (`-Dshader-cache=disabled`: shaders compile at every start) |
| GPU (V3D 4.2) — Vulkan (V3DV) | ✅ done | Mesa 26.2 `v3dv` as a static ICD with **`VK_KHR_display`** through SDL's KMSDRM Vulkan path: vkcube; **vkQuake ~44 fps at 1080p** (P1 gate 42.2), the start-map torches present | no Wayland WSI (vkQuake runs full screen only); ray queries unsupported by the hardware |
| Video decode | ✅ CPU playback; 🔬 HW HEVC | **Player:** ffplay (FFmpeg 6.1, `video_player` port) with SDL KMSDRM + Wayland, CPU decode (4 threads): H.264 720p and 1080p, **HEVC 720p at 30 fps**, VP9, AAC/Opus/MP3/Vorbis/FLAC; full screen from psh (`video-play`) or in a window; gtk-video (GTK 3) on the desktop. **Hardware:** the BCM2711 `rpivid` HEVC block is driven bit-exact to 1080p by the stand-alone `tools/hevc-decode/` experiment (intra + inter, rolling DPB, SAND de-tile); H.264 has no MMIO decoder on the BCM2711 (VideoCore/VCHIQ only) | wire `rpivid` into the player (M10 §4); 1080p H.264 on the CPU drops frames; gtk-video full screen is CPU-bound (14–17 fps) |
| Audio (PWM / I²S / HDMI) | 🟡 partial | PWM driver `/dev/audio0` (`audio/rpi4-audio/`): **continuous streaming DMA** (free-running self-chained ring, PWM1=DREQ 1) feeds the FIFO; `write()` fills the ring w/ usleep backpressure (driver sleeps, no spin); PIO fallback retained. **Quakespasm SNDDMA backend** (feeder thread) mixes over it — "Audio: 16 bit, stereo, 44100 Hz", demo renders, 0 faults/underruns (2026-06-17). **SDL2 audio driver** over `/dev/audio0` HW-validated (driver=phoenix, 44100/S16/2ch, tone played, 0 faults, 2026-08-05) | audible jack sign-off ⏸ (headphones); vkQuake reuses the backend; underrun→ring-loop artifact (steady state ok). **★ 2026-09-18: the intermittent "engine comes up parked" stall (~1 boot in 41-70) is CONTAINED, not fixed** — the driver grades the channel by progress, re-arms up to 3×, else serves the device as a paced null sink, so an app never blocks on it; ~14 000 in-process arm trials say the defect is per-BOOT, not per-arm (`docs/misc/2026-09-18-audio-dma-stall-captures.md`) |
| DMA | ✅ done | **★ 2026-09-18: the Normal-NC → Device store-ordering race is MEASURED on this board, not argued** — `tools/pwm-dma-probe --cb-race`, 5 000 trials per arm: with a `dsb sy` before the MMIO kick **5 000/5 000 correct, 0 stale**; without it **146 stale fetches** (2.9 %), i.e. the engine followed control-block bytes the CPU had already overwritten. Five missing barriers were fixed port-wide as a result (audio, V3D TFU, xHCI event ring, SDHCI read, V3D mailbox — `docs/misc/2026-09-18-dma-barrier-audit.md`). ⚠ The control arm's null bounds the rate; it does not prove the ordering is architecturally guaranteed, and neither arm covers a first-ever fetch of freshly `mmap`'d memory. legacy BCM2711 DMA-channel driver **proven + in production for audio** (`rpi4-audio`: self-chained streaming CB, DREQ-paced, low-1GB C0 bus alias); **SD uses the eMMC SDHCI ADMA2 scatter-gather engine for BOTH directions** since 2026-09-20 (~38 MB/s DDR50 reads, multi-block CMD18; 12.7-12.9 MB/s end-to-end writes), falling back to SDMA if the descriptor list cannot be built — the DMA path is validated on HW | open items are both optional/deferred, not functional gaps: a **generalized reusable DMA-helper API** (audio drives DMA inline today — YAGNI until a 2nd consumer such as I²C/SPI/PWM needs it, at which point the helper is extracted against a real second use) ~~and **SD DMA *writes***~~ (**done 2026-09-20** — the "BCM2711 DMA-write quirk" was our own missing emmc2bus address translation; writes now run on ADMA2, tracked in the SD-card row) |
| RTC | 🟡 capability present | Pi 4 has no on-SoC RTC. The **`ntpclient` psh applet** queries SNTP + calls `settimeofday` (kernel `settime` syscall + libphoenix `settimeofday`/`clock_settime` all present) → NTP-over-GENET works | **★ 2026-08-08 VALIDATED end-to-end**: with E2 internet up, `ntpclient -s pool.ntp.org` synced the clock 1970→2026 and enabled CA-verified HTTPS (the E3 cert clock). Still manual per boot — baking it into a boot step is deferred (risky nfsroot rc-model change) |
| Camera (CSI-2) / DSI display | ⬜ not started | — | — |
| posixsrv / psh userspace | ✅ done | pipes, ptys, `/dev/{null,zero,urandom,full}` (urandom now HW-RNG-backed), interactive psh; **AF_UNIX SOCK_STREAM** + **libc `getrandom()`/`getentropy()`** validated on HW (`misc/rpi4-ipcprobe`, 2026-06-17) | psh has no `\|` pipe parsing |
| X11 (Xorg) | ✅ done | **Xorg 21.1.24** with the **modesetting** driver and **glamor** on GLES 3.1 (`xorg_server_drm` port, `/bin/Xorg-drm`), DRI3/Present (a Phoenix xshmfence backend), phxhid input on `/dev/kbd0` + `/dev/mouse0`. `startx` runs the showcase desktop: Window Maker, a GL window at **60.00 fps vsynced** (485 fps unsynced), Life in Python, xclock, xbill, top | — |
| Wayland desktop | ✅ done | **labwc 0.20** (wlroots 0.20) compositing on the GPU (GLES2), **XFCE 4.20** (panel, xfdesktop, Thunar, settings, application finder) on **GTK 3.24** Wayland, the foot terminal, fuzzel, D-Bus session bus; `xfce-session` starts it and Log Out returns to psh. Games (SDL Wayland driver), the video players and **Atril** (Poppler, PDF) run as windows; libinput-phoenix input | no Xwayland (X11 programs need the X desktop) |

## Ported libraries & applications

| Component | Status | Notes |
|---|---|---|
| Mesa 26.2 (`mesa_drm`) | ✅ | GBM, EGL (drm, surfaceless, Wayland, X11), GLES 3.1, desktop GL, v3dv; static, one build per platform set |
| libdrm (`libdrm_phoenix`) | ✅ | libdrm 2.4.134 with a Phoenix backend over `/dev/v3d-async` and `/dev/kms` (`drmprobe` 36/36) |
| **SDL 2.30.12** (`sdl2_kmsdrm`) | ✅ HW-validated | stock KMSDRM and Wayland video drivers in one library, Phoenix HID input and audio drivers; frame pacing (submit before waiting for the previous flip), GBM-buffer release fix (upstream `9cc2f248f5`), monotonic condvar timeouts |
| Xorg + Window Maker (`xorg_server_drm`, `windowmaker`, `xorg_apps`, `xterm`, `xbill`) | ✅ HW-validated | modesetting + glamor, DRI3/Present; `startx` |
| QuakeSpasm (Quake) | ✅ HW-validated | `quakespasm_drm` → **`quakespasm`** (`/usr/bin/quakespasm-drm`); desktop GL on Mesa; ~44 fps at 1080p full screen, 45–67 fps in a 1280×720 desktop window; demos, single player, direct-IP multiplayer |
| vkQuake | ✅ HW-validated | `vkquake_drm` → **`vkquake`** (`/usr/bin/vkquake-drm`); Vulkan on v3dv with `VK_KHR_display`; ~44 fps at 1080p, the start map with its torches | full screen only |
| yQuake2 (Quake II) | ✅ HW-validated | `yquake2` (engine objects) + `yquake2_drm` (the program) → **`quake2`**, a RAM-staging launcher that plays `demo1`; 60 fps vsynced full screen and in a window |
| Quake III (quake3e) | ✅ HW-validated | `quake3` + `quake3_drm` → **`quake3`** (RAM-staging launcher); desktop GL; ~59 fps at 1080p, ~90 fps in a window; **free demo data**: no retail content and no retail CD key (a `pak1.pk3` of QVMs built from ioquake3 + a format-valid `q3key`, staged by `scripts/stage-game-data.sh` from `assets/quake3-qvm/`, reproducible with `tools/quake3-vm/build-quake3-vms.sh`) | in-game mouse-look |
| SuperTuxKart 1.4 | ✅ HW-validated | `supertuxkart` + `supertuxkart_drm` → **`stk`**; GLES 3; ~12 fps at 1080p (Raspberry Pi OS parity), **22.3 fps at 1280×720** through `game-res`, ~90 fps on its menu in a window; both asset roots (194 MB) staged by `scripts/stage-game-data.sh` | C1 heap corruption (`KNOWN-ISSUES.md`) |
| FFmpeg 6.1 player (`video_player`) | ✅ HW-validated | ffplay (SDL KMSDRM + Wayland), `video-play`, gtk-video (GTK 3), four demo clips; LGPL build. The separate `ffmpeg` port is the decode-only library |
| Dillo / mc / nano | ✅ built | Dillo 3.2 (FLTK, X11; HTTPS via mbedTLS) rendered live HTTPS pages on the Pi (2026-08-08) over a host NAT gateway with `ntpclient` setting the certificate clock; `mc` and `nano` build and ship | Dillo not yet run on the Xorg desktop; `mc`/`nano` not use-tested interactively |
| Atril 1.28 (`atril_wayland`) | ✅ HW-validated | Atril + Poppler (PDF backend built in) on GTK 3 Wayland: windowed, `--fullscreen`, `--presentation` (M7 `m7j-atril`) |
| libphoenix libm + libdbg (corelibs) | ✅ | libm gaps filled (rint/rounding/min-max + exp2/log2f/erf/erfc/scalbn, regression-tested in `phoenix-rtos-tests/libc/math`); **libdbg** reusable in-process crash/hang backtrace corelib (`project_libphoenix_libm`, `project_libdbg_facility`) |
| GNU coreutils 9.5 | ✅ HW-validated | full tool set (~105 programs) builds + installs; core tools HW-verified bit-exact (`seq`/`wc`/`sha256sum`), `stat`/`stty` HW-verified; last straggler `stty` closed 2026-08-27 (`project_coreutils_port`) |
| GNU bash 5.2 | ✅ HW-validated | bash 5.2.21 runs **fully interactively** at the console — prompt, command execution, pipes/loops/vars/command-substitution, stays until `exit` (HW-verified). The earlier "self-exits on EOF at the prompt" was a libphoenix `select()` NULL-timeout bug (a NULL/infinite timeout returned 0 immediately instead of blocking, so readline saw EOF) — fixed in `libphoenix sys/select.c` (`project_bash_port`) |
| CPython 3.14 | ✅ HW-validated | static `python3` 3.14.4 with `sqlite3`, `zlib`, `_ssl`/HTTPS, `_decimal`, `ctypes` + `.so` C-extension `dlopen` (`project_python_port`) |
| Redis 7.2 | ✅ HW-validated | Redis 7.2.4 data-store service over lwip TCP; 241 commands, 0 faults (`project_redis_port`) |
| SQLite 3 | ✅ HW-validated | full SQL, in-memory + on-disk file VFS; `integrity_check=ok` (`project_sqlite_port`) |
| jq | ✅ HW-validated | jq 1.7.1, selfcheck + run-tests all pass (`project_jq_port`) |
| Lua 5.4.7 | ✅ HW-validated | interpreter + `luac`; selfcheck ALL-OK (`project_lua_port`) |
| curl / BusyBox | ✅ HW-validated | curl over mbedTLS (HTTP/HTTPS); BusyBox shell utilities |

## Build / test infrastructure (✅)

- Three build variants: `rebuild-rpi4b-fast.sh --variant nfsroot|netboot|sd`.
- Netboot loop: `test-cycle-netboot.sh` (UART + HDMI snapshots); `test-cycle-psh-interact.sh` to run psh commands.
- `scripts/grade-x-desktop-video.py` grades the three reported X artefacts from a recording (validated FAIL-on-bad / PASS-on-good); `verify-sd-image-contents.sh` also gates the **FAT boot partition** and runs automatically inside `--variant sd`; `uart-summary.sh` now catches a **truncated** fault message and a **mid-print stop** (a run that hung the board previously reported zero faults); `test-cycle-bench.sh` for multi-trial pass rates; plus a QEMU structural SD-image boot check. Deterministic rollback: `snapshot-/restore-integration-state.sh` + `manifests/`.

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
   headers). Size alone is not a barrier: on 2026-09-03 all five game engines (18–38 MB, up to
   `supertuxkart` at 38 MB) exec'd from both the ext2 root and the NFS root.
   Mitigation: trim the linked stack; proper fix: demand-page exec-time anon (kernel).
   Other residuals: #156 first-access ENOENT (boot-order race), perf/signal polish.
3. **Graphics** — the stack is complete for the desktop and the games; open items are
   performance (a shader disk cache, render-server pipelining) and the `rpivid` HEVC
   decoder in the video player.
4. **WiFi** — in the image; remaining: a boot check of the image, SSIDs with spaces, WPA3.
5. **Bluetooth** — driver-level bring-up done (`/dev/hci0`, HCI Inquiry); needs a host BT stack.
6. **Reboot / watchdog** — no software reset yet (attended).
7. Greenfield: DMA framework → audio/I²C/SPI/PWM; GPIO full driver.

## Unattended-vs-attended note

Overnight/autonomous netboot work is restricted to **additive + deterministic-self-log +
cannot-silently-regress** items (see `feedback_unattended_scoping` memory). The ⏸ rows above are
attended precisely because their failure is either physically unrecoverable over netboot
(kernel/reboot), statistically invisible to single-boot validation (USB daemon internals), or
needs human judgement (a screen/scope/bench rig).
