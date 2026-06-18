# Unattended work log — 2026-06-17 onward (for Witold, back Fri late afternoon)

You left me running unattended for a few days with a broad mandate: process the
docs, implement what's not done, work on performance/stability/usability/breadth,
the big ports (X11, Vulkan+vkQuake, audio), Pi4 device support, and a
publication-readiness pass + TODO cleanup. Take risks; skip only items that need
you physically (SD-card testing, bench rig/scope/LED, BLE-in-range, audible audio).
This file is my running log + the decisions/parked items for you to review.

---

# ★ READ THIS FIRST — delivery summary (2026-06-17)

## 1. What works now (in the flagship netboot image, boot-verified clean, 0 faults)
- **GLQuake (Quakespasm)** — textured 3D world, ~40 fps @1080p, render-to-scanout. Unchanged baseline.
- **Audio — NEW, the headline.** Full PWM-audio subsystem on `/dev/audio0`: clock+FIFO bring-up →
  legacy-DMA mechanism (PWM1=DREQ 1) → **continuous streaming DMA** (free-running ring, driver sleeps) →
  **Quakespasm SNDDMA backend** (feeder thread). Quake boots with "Audio: 16 bit, stereo, 44100 Hz" and
  mixes live. **➡️ YOUR ONE ACTION FRIDAY: plug headphones into the 3.5 mm jack and confirm Quake audio
  is audible.** Everything up to "is sound coming out" is self-verified; only your ears can close it.
- **/dev/urandom is now hardware-RNG-backed** (was weak rand()); **libc `getrandom()`/`getentropy()`
  added** (broad app support). Full entropy stack: /dev/hwrng → /dev/urandom → libc. HW-verified.
- **`rpi4-sysinfo` boot banner** — build stamp, uptime, HW-entropy sample, /dev inventory (10/12 nodes).
- **psh `mv` applet** — was missing (a documented MUST); added (rename + dir-target + EXDEV copy-fallback),
  HW-verified. Upstreamable.

## 2. Named-goal status
- **Audio (you named it): DONE** (driver + DMA + Quake backend); audible check is yours.
- **X11 (you named it): foundation validated + library port STARTED.** AF_UNIX (the gate for every X
  client) is HW-confirmed READY; fb0 + USB HID also done. The X11 library port (net-new, PR #82 never
  landed) is now under way in `tools/x11-port/` (isolated `/tmp/x11-phoenix`, host-side, flagship
  untouched): the **X11 client-library foundation cross-compiles end-to-end for aarch64-phoenix** —
  xorgproto, libXau, xtrans, libXdmcp, xcb-proto, libpthread-stubs, **libxcb + ~24 ext libs**, and the
  keystone **libX11 (core Xlib)**. Needed small Phoenix-gap fixes: libxcb patches (`<arpa/inet.h>`,
  `MSG_TRUNC`/`MSG_CTRUNC` no-ops) + **real libphoenix libc additions** (`getpwuid_r`, functional
  `getpwnam_r`, `sys/poll.h`, `hypot` — libphoenix `89d1543`+`6e2b929`, additive, ship on next rebuild).
  **The ENTIRE X11 client + rendering + font library stack now cross-compiles for aarch64-phoenix —
  36 archives** (libX11, libxcb +24 exts, libXext, libXrender, libXfont2, libfontenc, libfreetype,
  libpixman-1, libXau, libXdmcp, libz) + **5 libphoenix libc fixes** (getpwuid_r, getpwnam_r, sys/poll.h,
  hypot, alloca.h-size_t — all additive, ship on the next libphoenix/image rebuild). Both remaining
  frontiers SCOUTED + documented: (1) **kdrive Xfbdev server** — modern xorg-server dropped fbdev, so it
  needs a restored/written fbdev backend (or PR#82 tinyx / old xserver) + the libphoenix rebuild + a
  kdrive input driver (deep, multi-session); (2) **toolkit/apps** (libICE→libSM→libXt→Xaw→twm) —
  speculative-until-server, libXt now alloca-unblocked, libICE needs a 1-line `time`-decl patch. Ladder +
  full libc-gap inventory + recipe in `tools/x11-port/PROGRESS.md` + memory `project_x11_lib_port`.
  **UPDATE: the TOOLKIT base also builds** (libICE/libSM/libXt/libXmu/libXpm; coord 427ec46) — so the
  whole X11 *library* stack (client+render+font+toolkit) cross-compiles for Phoenix.
  **★★ MAJOR UPDATE — the EXECUTABLE BOUNDARY IS CROSSED + the first X11 app is ported (coord fce360c):**
  the full lib stack is now **45 archives** (added libXaw/Athena-widgets + libXrandr); I completed the
  libphoenix libc gaps (full wide-char set + C-locale multibyte set mblen/mbtowc/wctomb/mbstowcs/wcstombs;
  libphoenix `0cb9f72`+`e29c840`), rebuilt libphoenix so they're on-device, and **the first X11
  executables now LINK as static aarch64-phoenix ELFs: `xprobe` (a minimal Xlib client) and — the
  headline — `twm` 1.0.12, a complete X11 window manager** (3.1 MB ELF, full toolkit closure; binaries in
  `artifacts/x11/`). Two gotchas solved + scripted: the cross-toolchain bundles its OWN stale
  libphoenix/libc/libm (must sync after a libphoenix change), and app configure needs
  `PKG_CONFIG="pkg-config --static"`. So the **entire X11 client + toolkit + a real application build
  for Phoenix** — the ONLY remaining gate is the kdrive Xfbdev **server** (deep/multi-session; modern
  xorg-server dropped fbdev). Nothing X *runs* until the server exists, but everything up to it is DONE
  + de-risked (host-side, isolated, flagship frozen throughout). Build apps: `build-x11-phoenix.sh
  --with-apps`.
- **Vulkan+vkQuake (you named it): furthest-ever progress, 5 blockers cleared.** vkCreateInstance +
  enumerate(count=1) work on HW; cleared a name-print abort + the threaded-submit hang (is_shim fix).
  vkCreateDevice now reaches the noop-job and NULL-derefs the binner CL (winsys/V3DV BO-interop, the 6th
  blocker, precisely localized in `project_vulkan_v3dv_port` memory). vkQuake is far; this is a research
  stretch best finished attended.

## 3. Parked / attended items — each with the human action it needs
- **Quake audio audible check** — plug in headphones, listen. (minutes)
- **SD card #120/#154** (exec-from-card, write-completion) — needs card swaps host↔Pi. (batched, ~5 min)
- **USB mass storage (umass)** — plug in a USB stick once.
- **GPIO outputs / I²C / SPI / PWM** — need a bench rig (LED / logic-analyzer / sensor).
- **Bluetooth (BCM43455 HCI)** — needs the kernel mailbox for BT_REG_ON (GPIO) + a `.hcd` blob + a BLE
  device in range to scan.
- **WiFi #91** — firmware-exec gate; worst-case needs JTAG (FT2232 ~$20).
- **Vulkan vkCreateDevice** — continue the winsys BO/CL probe (deep); then Tier 2 → vkQuake (big).
- **GENET cacheable-RX (Policy B / task #11)** — silent-corruption risk, needs a multi-boot integrity
  soak + is gigabit-cable-gated; attended.
- **TD-10 SError unmask / #43 reboot / SMP-beyond-cpu0** — kernel boot-risk, careful attended.

## 4. Decisions I made unattended (revisit if you disagree)
- Backed /dev/urandom with /dev/hwrng (rand() fallback) — a security fix; touched the shared posixsrv.
- Added getrandom/getentropy to libphoenix (shared libc) — additive, low-risk.
- Forced is_shim=true in v3dv on Phoenix (synchronous submit) — committed in external/mesa (libv3dv-only,
  swapped out of the flagship, zero effect on the shipped GL/Quake image).
- Kept rpi4-sysinfo as a permanent boot banner; kept rpi4-ipcprobe as a re-runnable (disabled) probe.

(Full chronological detail + per-item commits follow below.)

---

## Operating rules I'm following
- Each delivered change: build (`--scope core` for core), netboot-validate, HDMI
  snapshot where visual, commit in the touched repo, record here.
- I will NOT touch the parked WIP: `sources/phoenix-rtos-devices/storage/bcm2711-emmc/{sdcard,sdstorage_dev}.c`
  (SD #154, needs you), the WiFi fw/nvram blobs in lwip, the gldraw experiment.
- Risk is OK (you said so); broken intermediate builds are OK. Known-good rollback:
  the colors+stall-fixed Quake build at coord `2cde73d` / external/quakespasm `55b479e`.

## Priorities (my plan, highest-value-autonomous first)
1. Quake polish: gamma/brightness, then double-buffering (you asked) — finishes the flagship.
2. Audio: Pi4 audio driver + Quakespasm sound backend (driver+wiring autonomous; "audible" sign-off is yours).
3. Vulkan V3DV port → vkQuake (Tier 0 link → Tier 1 device-on-HW → as far as I get).
4. X11: software X (kdrive → /dev/fb0) → a window + input → (accel is research-gated).
5. Device breadth: RTC-via-NTP (usability), GPIO outputs, others from the HW matrix.
6. Publication-readiness: codebase review of the Pi4 changes, fix TODO(TD-xx) + stray TODOs.
7. Remaining inprogress/todo items that are autonomous.

## SKIPPED (need you) — review these Friday
- SD card #120 exec-from-card / #154 write-completion: needs card swaps + your bench.
- WiFi #91 worst-case: needs JTAG.
- GPIO output / I2C / SPI / PWM / audio *audible*: need LED/scope/speaker.
- Bluetooth Tier-4 scan: needs a BLE device in range (impl can be done; scan validation can't).

## Decisions I made unattended (revisit if you disagree)
(none yet — will append as I go)

## Progress log
(appended chronologically below)

### 2026-06-17 — session start
- Surveyed docs/inprogress (38 files) + docs/todo (7 files). Repo at coord 2cde73d.
- Created this log.
- **Launched background subagent: Vulkan V3DV Tier 0** (get V3DV to compile+link for
  aarch64-phoenix, mirror build-gl-phoenix.py, add drmSyncobj* shims). It builds to
  /tmp/libv3dv-phoenix.a + may rebuild libv3d — so Pi boots during its run risk a torn
  /tmp/libv3d read (recoverable: just retry the build/boot).

### 2026-06-17 — progress
- ✅ **Audio P0-P1 DONE** (devices d5933ec, project 32867df). rpi4-audio driver →
  /dev/audio0. HW-verified self-log: CM_PWMCTL=0x91 (clock ENAB+BUSY+osc running),
  PWM_CTL=0xa1a1 (PWEN1/2 + M/S + FIFO), 0 faults. write()=s16 PCM→FIFO (PIO).
  REMAINING (autonomous): DMA streaming (P3), a Quakespasm snd backend → /dev/audio0.
  ATTENDED (you, Fri): plug headphones, confirm audible (P2 tone / WAV / Quake sound).
- ✅ **Vulkan V3DV Tier 0 DONE** (external/mesa dbd03bef831, coord 9da241f). V3DV +
  Vulkan runtime + spirv_to_nir compile+link for aarch64-phoenix, harness 0 undefined
  symbols. Reuses the GL backend libv3d. NEXT: Tier 1 = boot the harness, vkCreateDevice
  on real HW (doc 2026-06-17-vulkan-v3dv-tier0-progress.md has the steps).

## EXECUTION ROADMAP (ordered; each firing picks the top unfinished item)
Each item: implement → build (`--scope core`) → netboot-verify (uart-summary + HDMI
snapshot if visual) → commit in the touched repo → tick here. Boot budget is large.

1. **[BIG] Audio — rpi4-pwm-audio driver + Quakespasm backend.** Plan: docs/todo/pi4-audio-impl.md.
   Standalone userspace driver (rpi4-thermal pattern) at sources/phoenix-rtos-devices/audio/pwmsnd/.
   Regs: PWM1 0xfe20c800, CPRMAN 0xfe101000, DMA 0xfe007000, GPIO40/41 ALT0.
   Autonomous: P0 scout (mmap+readback), P1 clock/GPIO/PWM bring-up (BUSY=1/PWEN=1 self-log),
   P3 DMA streaming (block-count/underrun self-log), P4 /dev/audio0 char dev + a Quakespasm
   snd backend (snd_phoenix.c → /dev/audio0). ATTENDED (skip/defer to user): P2 audible tone,
   final WAV/Quake audible sign-off. Deliver the data path + self-verify; user confirms audible Fri.
2. **[MED] Render-to-scanout double-buffering** (you asked). plo 2x virtual fb (board_config
   PLO_RPI_FB_HEIGHT*2 virtual) + rpi4-fb RPI4FB_PAN devctl (mailbox SET_VIRTUAL_OFFSET) +
   winsys remap RT to back buffer + flip in pl_phoenix_vid GL_EndRendering. Fixes tearing +
   the transition flicker. HDMI-verify. NOTE: touches libv3d (winsys) — don't run concurrently
   with a libv3d-rebuilding subagent.
3. **[MED] RTC via NTP** — lwip SNTP client → set system clock at boot (Pi4 has no RTC).
   Fully autonomous, verify via `date`. Usability.
4. **[BIG] X11 software path** — docs/inprogress/2026-06-16-x11-accelerated-desktop-plan.md +
   docs/todo/tinyx-x11-demo.md. Build the dep libs (pixman, libxcb, libX11) for aarch64-phoenix
   via ports; kdrive → /dev/fb0 shadow-fb; an xterm/twm visible on HDMI; input via /dev/kbd0.
   Big; make milestone progress (M0 server links → M1 draws to fb0).
5. **[BIG] Vulkan** — integrate the subagent's Tier 0, then Tier 1 (vkCreateInstance/Device
   on HW, boot-verify) → Tier 2 (clear+readback) → triangle → fb0 present → vkQuake.
6. **[MED] GPIO outputs** — extend /dev/gpio (currently read-only) with GPSET/GPCLR/fsel writes
   (RPI4GPIO_SETPIN devctl). Verify register write-back (GPLEV); full LED test is yours.
7. **[BIG] Publication-readiness sweep** — grep TODO(TD-xx) + stray TODO/FIXME across the Pi4
   changes; resolve/clean the autonomous ones; remove disproved diagnostic code; update
   TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md. (This is the fallback once docs are processed.)
8. **[MED] Userspace demo apps** — docs/todo/userspace-demo-apps.md (make psh do something).
9. **[MED] Bluetooth impl** — docs/todo/bluetooth-*.md; impl autonomous, scan needs your BLE device.
10. **Cosmetic/parked:** Quake gamma/brightness (advisor: matches host = a preference, not a bug;
    palette-brighten in gl_texmgr.c d_8to24table risks over-bright HUD — low priority).

### Decisions parked for you
- **Gamma/darkness:** deprioritized as cosmetic (render matches host per the visual-regression
  harness; it's Quake's default-gamma dimness, a player preference). Did not spend boot cycles
  on it; can add a palette/GPU gamma pass on request.

### Vulkan V3DV Tier 0 (subagent)

**Outcome: Tier 0 COMPLETE.** Mesa's V3DV (Vulkan for Broadcom V3D) driver + the Vulkan
runtime + `spirv_to_nir` compile and link for aarch64-phoenix. A
vkCreateInstance→vkEnumeratePhysicalDevices→vkCreateDevice harness links to a static
aarch64 ELF with **0 undefined symbols** (`nm -u` clean, not just a passing link).
Mirrors the GL gallium port recipe. NOT boot-tested (HW serialized by main agent) — that
is Tier 1.

Full detail + Tier-1 next steps: `docs/inprogress/2026-06-17-vulkan-v3dv-tier0-progress.md`.

**Committed in external/mesa @ dbd03bef831** (4 files, all `#if __phoenix__`-guarded, upstream +
the main agent's GLQuake gallium build unchanged): `vulkan/runtime/vk_image.{h,c}` (extend the
LINUX||BSD guards on drm_format_mod with `|| __phoenix__` — Vulkan-runtime-only files, GL build
never compiles them), `include/renderdoc_app.h` (__phoenix__ ⇒ RENDERDOC_CC),
`broadcom/vulkan/v3dv_device.c` (enumerate_devices bypasses drmGetDevices2 → create_physical_device
with an inert render-fd; skip fstat of absent DRM nodes). NOTE: a global __phoenix__⇒DETECT_OS_LINUX
in detect_os.h was tried first and REVERTED — it flipped u_cpu_detect.c onto a <sys/auxv.h> path
Phoenix lacks and broke the SHARED libv3d-phoenix.a (would have silently regressed GLQuake too).
I staged ONLY my files — the GL port's pre-existing uncommitted `gallium/drivers/v3d/*` +
`os_memory_aligned.h` working-tree changes were left untouched; libv3d-phoenix.a rebuilt clean
(GL build `[link] PASS`).

**New/edited files in tools/v3d-driver-port/ (MAIN AGENT: commit these in the coord repo):**
- `build-v3dv-phoenix.py` (cross-build + link-drive; header documents the one-time host
  Vulkan meson build via a uv venv since system python3.14 lacks mako)
- `v3dv_harness.c`, `vk_icd_link.c`, `v3dv_libdrm_shim.c`, `v3dv_v71_stubs.c`,
  `v3dv_gap_stubs.c`, `v3dv-aux-sources.txt`
- edited: `shim-include/{xf86drm.h,xf86drmMode.h,dlfcn.h}`, `phoenix_mesa_compat.h`,
  and `v3d_phoenix_winsys.c` (ioc_get_param: added SUPPORTS_MULTISYNC_EXT=1 / PERFMON=0 /
  CPU_QUEUE=0 — needed for Tier-1 device-create; read-only constants, GL path unaffected)
- new doc: `docs/inprogress/2026-06-17-vulkan-v3dv-tier0-progress.md`

**Key engineering decisions (all advisor-reviewed):**
- Reused `/tmp/libv3d-phoenix.a` back-end as-is (front-end-agnostic; plan §4.2).
- No C11 threads.h shim needed — HAVE_PTHREAD routes c11/threads.h to pthread; cnd_*/thrd_*
  bodies come from src/c11/impl/threads_posix.c (aux).
- V3D-7.1 dispatch branch (57 v3d71_* syms) is **weak trap-stubs**, not no-ops (it's dead at
  ver==42 but `v3d_X` macro takes the address of both v42 & v71 fns). Build v3dvx_* at
  V3D_VERSION=42 only — building v71 would drag a whole v71 backend closure for a dead path.
- 3 v3d42_* helpers collide between V3DV and gallium back-end (never co-linked upstream) →
  tolerated with `-Wl,--allow-multiple-definition` (link order binds V3DV's copy = correct).
  Any NEW dup in later tiers must be investigated, not absorbed.

**Tier-1 watch item flagged for boot:** `init_uuids` calls libv3d's real ELF-walking
`build_id_find_nhdr_for_addr` which may return NULL on Phoenix and hard-fail vkCreateDevice
before the stubbed leaves are hit — if so, add a weak fixed-note stub (details in the doc).

### 2026-06-17 — Vulkan Tier 1 (partial: instance OK on HW)
- ✅ **vkCreateInstance WORKS on real V3D hardware.** Built a boot-launched
  rpi4-v3dv-tier0 (sources/phoenix-rtos-devices/misc/rpi4-v3dv-tier0/, links
  /tmp/libv3dv-phoenix.a + /tmp/libv3d-phoenix.a with -Wl,--allow-multiple-definition
  for the v3d42_* helpers in both archives). Booted (swapped in for rpi4-quake — only
  one large GL/VK binary fits in loader.disk). Harness printed "start" then reached the
  vkEnumeratePhysicalDevices printf — i.e. vkCreateInstance returned VK_SUCCESS (past the
  return-2 check). **Vulkan instance creation on the Pi4 V3D: confirmed.**
- ⏳ **vkEnumeratePhysicalDevices HANGS** (no further harness output; boot continues; no
  fault dumped → the harness process is stuck inside the enumerate call, not crashed).
  Next debug (Tier 1 cont.): instrument phoenix_v3d_ioctl in the winsys to log each cmd
  → find the ioctl it wedges on (likely a winsys_init/power-on path or an ioctl V3DV
  issues that the shim mishandles), or add prints in create_physical_device/init_uuids.
  Swapped rpi4-quake back IN (flagship restored); re-enable rpi4-v3dv-tier0 (Makefile +
  user.plo.yaml, both have the commented line) to resume.

### 2026-06-17 — Vulkan Tier 1: 4 blockers cleared, device-create reaches blake3
Debugged vkEnumeratePhysicalDevices (was -3, not a hang). Cleared, in order:
1. **device_has_expected_features** (-3 "requires kernel 6.8+"): V3DV gates on TFU && CSD
   && CACHE_FLUSH && CPU_QUEUE && MULTISYNC. winsys ioc_get_param returned CSD=0,
   CPU_QUEUE=0 → set both to 1 (HW has CSD; CPU_QUEUE is a fib; graphics never uses them).
2. **build-id**: init_uuids' build_id_find_nhdr_for_addr (libv3d's real ELF-phdr walk)
   returns NULL on Phoenix's static ELF → added a weak override in v3dv_gap_stubs.c
   returning a non-NULL fixed-note sentinel (+ -Wl,--build-id on the link). 
Now device-create reaches **init_uuids' BLAKE3 pipeline-cache-UUID hash and FAULTS**
(Data Abort EL0, far=0 NULL-deref; regs show BLAKE3/SHA constants). NOTE: NOT NEON (host build sets no NEON flag; blake3 uses portable). Deeper portable-path NULL-deref (the subagent flagged: detect_os
must NOT force DETECT_OS_LINUX or u_cpu_detect breaks). NEXT: force Mesa's blake3 to the
PORTABLE impl in build-v3dv-phoenix.py (no SIMD dispatch → no cpu-detect dependency),
or fix u_cpu_detect for aarch64-phoenix. Then vkCreateDevice should complete → Tier 2.
The ioctl-trace debug was removed; rpi4-quake restored as the flagship.

### 2026-06-17 — audio data path verified + Vulkan blake3 reassessed
- ✅ **Audio write->FIFO path VERIFIED** (devices). rpi4-audio boot self-test feeds a
  ~0.2s 440Hz tone through s16->duty->PWM-FIFO (PIO); underruns=0, STA 0x102->0x731,
  0 faults. The output path works end-to-end. Audible blip on the jack = your Fri check.
- ℹ️ **Vulkan blake3 blocker re-assessed**: NOT NEON (host build sets no NEON flag; blake3
  uses the portable path). The init_uuids BLAKE3 fault is a deeper portable-path NULL-deref
  — needs addr2line of the fault PC in the rpi4-v3dv-tier0 binary (or prints in init_uuids).
  Parked as the Vulkan Tier-1 next step (device-create is otherwise past 4 blockers).
- NOTE: usable Quake/vkQuake audio needs DMA streaming (P3, driver) before the Quakespasm
  SNDDMA backend (pl_phoenix_snd.c, currently a silent stub) is worth wiring — PIO alone
  underruns at per-frame Submit. DMA is the next audio step.

### 2026-06-17 — Vulkan device-create: 5 blockers cleared, now an instruction abort
init_uuids fixed (the BLAKE3 data-abort was libv3d's real build_id_data() dereferencing my
synthetic note → wild ptr → blake3 read fault). Fix: #if __phoenix__ in init_uuids
(v3dv_device.c) uses fixed dummy UUIDs, skipping build_id+blake3 (UUIDs only gate
pipeline-cache validity + cross-process sharing, unused at first light). Confirmed: the
data-abort is GONE — device-create now reaches further and hits an **Instruction Abort (EL0)**
(a NULL/garbage code-pointer call, likely in v3d_compiler_init or disk_cache setup, after
init_uuids). 5 blockers cleared total: instance, enumerate-caps, feature-gate (CSD+CPU_QUEUE),
build-id, init_uuids/blake3. **NEXT (Vulkan Tier 1 cont.): extract the instruction-abort PC
(it interleaves with pcie boot noise — grep the raw log) + addr2line in the rpi4-v3dv-tier0
prog binary to find the bad call; likely another stub/NULL-fn in create_physical_device's
tail. Then vkCreateDevice should return → Tier 2 (clear+readback).** PIVOTING to breadth now
(3 turns on Vulkan); this is a clean focused next step. Flagship (rpi4-quake) restored.

### NEXT ACTION (decisive — execute, don't re-deliberate): Audio DMA (P3)
The audio write→FIFO path works (PIO); DMA is the bridge to usable Quake audio. Implement in
rpi4-audio.c: (1) pick a free legacy-DMA channel (query firmware dma-channel-mask via mailbox
tag 0x00060001, else default ch 5); (2) Pi4 legacy-DMA DRAM bus addr — try 0xC0000000|phys for
low-1GB buffers, else physical (iterate via the CS error/progress self-log); (3) map DMA ch regs
(0xfe007000 + ch*0x100), build a 32B CB (TI = SRC_INC|DEST_DREQ|PERMAP=5[PWM]|WAIT_RESP; SRC=bus(tone
ring); DEST=0x7e20c818 [PWM_FIF1 bus]; TXFR_LEN); PWM_DMAC = ENAB|(DREQ<<8)|PANIC; CONBLK_AD=bus(CB);
CS=ACTIVE; CB NEXTCONBK→self for continuous. (4) Verify self-log: CS active + no ERROR bit, TXFR_LEN
progresses, PWM STA drains, underruns=0. Then wire pl_phoenix_snd.c SNDDMA_* to /dev/audio0 (ring +
GetDMAPos from elapsed-time estimate). The user said take risks + iterate — boot, read CS, fix
address/channel empirically. THEN consider X11-start or the Vulkan instruction-abort.

### 2026-06-17 — Audio DMA DONE (mechanism proven on HW) — devices 7bdb1c4
Implemented the single-shot legacy-DMA feed of the PWM1 FIFO. First boot: channel armed correctly
but **held forever** (CS=0x21 = ACTIVE + bit5 ISHELD "held by DREQ flow control", remain=full, 0
bytes, no error). Root cause via rpi4os.com part9-sound: **PWM1 is DMA DREQ peripheral 1, not 5** —
DREQ 5 is the *legacy* PWM0; the analog jack on the Pi4 (GPIO40/41 ALT0) is PWM1 = DREQ 1. Changed
PERMAP 5→1; second boot **completed**: CS=0x0a (END=1, ACTIVE=0, no ERROR), remain=0 (all 35292
bytes DMA-paced into the FIFO, no CPU spin), FIFO drained. Buffers land in low 1 GB so the
0xC0000000 legacy uncached alias is valid (cb_pa/tone_pa both <0x40000000). DEST=PWM_FIF1 bus
0x7e20c818. So the audio DMA *hardware mechanism* is proven end-to-end.

**What's left for actual Quake audio (decided to bank + pivot, not tunnel):**
1. Continuous streaming: self-chain the CB (NEXTCONBK→CB) over a ring; or double-buffer with two CBs.
   Self-verifiable (DMA stays ACTIVE for seconds, underruns=0). Straightforward increment.
2. **The real lift = cross-process audio ring.** rpi4-quake and rpi4-audio are *separate processes*.
   Quake's snd_dma wants a *directly-writable* `shm->buffer` (it mixes ahead at arbitrary offsets keyed
   on `SNDDMA_GetDMAPos`), so a simple `write()` doesn't fit. Clean model: the driver exports its DMA
   ring via `mmap(/dev/audio0)` (device-file memObject returning the physical ring pages) + a
   `RPI4AUDIO_GETPOS` devctl for the play cursor; Quake mmaps it as `shm->buffer`. Needs mmap-over-msg
   support on the device — non-trivial in Phoenix. Until then `pl_phoenix_snd.c` stays the silent stub.
3. Audible sign-off is YOURS (headphones on the jack), Friday.
DECISION: the hardware-hard part (clock, FIFO, M/S, **DMA pacing**) is all proven + committed. The
remaining work is the cross-process shared-ring plumbing, which I'll either do next or after a breadth
pass. Pivoting now to breadth per "I want as many items as possible" + the no-tunnel lesson.

### 2026-06-17 — stale-doc cache-claim fixes (bounded, publication-readiness)
Advisor flagged that the open-ended TODO sweep is your *fallback* ("if out of documented tasks"),
not the primary work. So I did only the FINITE td16 §8 stale-doc list: corrected the false "caches
globally off" claims in the hardware matrix (lines 18, 24), the network-fs perf plan (:31, :88), and
the glquake capstone status (§2 + recommended order), plus the `bcm-genet.c:74` comment. Caches have
been globally ON since 2026-05-17 (TD-16 RESOLVED); the real lever is the narrow uncached-GENET-RX
pool (Policy B, attended). Commits: coord c2315f0 + lwip 0bb0123. Bounded — did NOT metastasize.

### 2026-06-17 — ★ QUAKESPASM AUDIO BACKEND DONE (your named deliverable) — coord 311412a + devices 7dc5540
"Add audio support to Quakespasm" — DONE to the self-verifiable bar. Checked snd_dma.c's contract
first (advisor's gate): the mixer paints the ring strictly FORWARD and reads only a monotonic play
cursor (SNDDMA_GetDMAPos) — it does NOT need random back-offset writes, so the heavy mmap-over-msg
shared ring is unnecessary. Implemented the light path: pl_phoenix_snd.c spawns a FEEDER THREAD that
reads shm->buffer sequentially behind the paint cursor and write()s to /dev/audio0, which blocks at
the PWM drain rate (backpressure). GetDMAPos = bytes the device accepted. Driver fix: audio_write now
returns bytes actually consumed (honest short-write) so the cursor is accurate. Stereo s16 → shared
FIFO alternates ch1/ch2.
HW-validated netboot: "PL_SND: /dev/audio0 44100 Hz 2ch 16-bit, feeder thread up" + Quake accepted it
("Audio: 16 bit, stereo, 44100 Hz"), demo1 renders ~35 fps for 40+s with audio mixing running
concurrently, 0 faults, no sound-overflow. **YOUR ONE CHECK FRIDAY: plug headphones into the 3.5mm
jack and confirm Quake audio is audible.** (FPS dipped 40-42→~35 from the feeder's PIO-spin write;
the continuous-DMA-streaming driver mode — mechanism already proven — would remove that, efficiency
not correctness.) Same backend will serve vkQuake.

### 2026-06-17 — ★ X11 Phase-1 gate ANSWERED: AF_UNIX READY on HW — devices 343fcb8 + project 215ac86
You named X11 as a target. The full tinyx/kdrive stack is a multi-week net-new library port (PR #82
never landed — no libX11/libxcb/xtrans/pixman/freetype/fontconfig in-tree), NOT unattended-completable.
So I did the SMART unattended thing: validated the foundation the whole port gates on. The tinyx plan's
Phase-1 dep #1 is "does AF_UNIX work on aarch64-rpi4b?" (every X client connects over a local socket).
Desk-check: the kernel fully implements AF_UNIX (posix/unix.c, 1283 lines, all the SOCK_STREAM ops X
needs). Runtime confirm: wrote rpi4-ipcprobe (misc/), a boot probe doing socketpair + the full named
bind/listen/accept/connect/send/recv dance. HW result: BOTH PASS → "AF_UNIX READY for X11". Reverted
it from the default boot (kept the source as a re-runnable probe; clean image restored, audio still in).
So: X11's IPC foundation is PROVEN. The remaining X11 cost is purely the library port (big, attended-
or-multi-session). Documented in docs/todo/tinyx-x11-demo.md (✅ AF_UNIX GATE PASSED banner).

### Decision recorded for you: X11 full port is NOT an unattended job
The honest call: porting libX11+libxcb+xtrans+pixman+freetype+fontconfig+the kdrive server is weeks of
build-system fighting with low odds of a working unattended result. I validated the foundation (AF_UNIX
✅) so the port is de-risked, but I will NOT sink the unattended window into a likely-partial X lib port.
If you want X, the foundation is green; it's a focused multi-session effort. Flagging for your call Friday.

### 2026-06-17 — Continuous streaming DMA audio (production arch) — devices 85c1c94
Promoted the proven single-shot audio DMA to a free-running streaming engine: persistent uncached
duty-word ring (~0.19s) played by a self-chained CB (NEXTCONBK→itself) forever, DREQ-paced;
audio_write() fills the ring ahead of the live read cursor (SOURCE_AD) with usleep backpressure (the
driver SLEEPS instead of busy-spinning); PIO push kept as fallback. HW: active=1, path=DMA,
0 underruns, Quake mixes over it, 0 faults. HONEST FINDING: this did NOT raise Quake FPS (~35-40, same
as PIO) — the audio CPU cost is the MIXER + feeder thread, not the driver wait (usleep vs spin made no
measurable diff). But it's the correct production architecture (no per-sample MMIO, driver sleeps) +
the base for vkQuake audio. Audio subsystem is now complete: bring-up → DMA mechanism → SNDDMA backend
→ streaming DMA. Only the audible jack sign-off (yours, Friday) + a vkQuake hookup remain.
Also: noted the DMA framework as 🟡 partial in the matrix (legacy DMA now in production for audio).

### Note on what's left that's unattended-doable
Most remaining documented FEATURES are attended-gated: X11 full lib port (multi-week), Bluetooth
(needs GPIO output for BT_REG_ON = bench), umass (needs a USB stick plugged), I²C/SPI/PWM (bench rig),
SD #120/#154 (card swaps), WiFi #91 (JTAG), Vulkan instr-abort (fiddly/attended-friendlier), GENET
Policy B (silent-corruption soak + cable-gated). The Pi4 device-driver code is already publication-clean
(no stray TODO/FIXME; 3 prior cleanup waves). So the unattended frontier is thinning.

### 2026-06-17 — ★ Vulkan V3DV Tier-1: instruction abort CLEARED, advanced to vkCreateDevice (hangs)
You explicitly named Vulkan+vkQuake, so I engaged it (libs were still in /tmp → feasible). Swapped the
v3dv-tier0 harness in for quake, booted. Findings: **vkEnumeratePhysicalDevices now returns count=1**
(device found). The device-create "instruction abort" from the prior session was root-caused: pc=0
(NULL fn-ptr call), lr→addr2line = vk_common_GetPhysicalDeviceProperties dispatching to the physical-
device table's GetPhysicalDeviceProperties2 slot = NULL. So it was the harness's COSMETIC device-name
print, not device-create. Guarded it (devices 0bfa53e) → reached vkCreateDevice, which now **HANGS**
(no return/abort; rest of boot proceeds, so only the harness thread blocks). Two open Vulkan issues:
(1) phys-device dispatch Properties2 NULL (create_physical_device's Phoenix shortcut likely skips the
vk_physical_device dispatch-table init); (2) the vkCreateDevice hang — needs instrumenting external/
mesa v3dv_device.c, which means a HEAVY host-meson rebuild of libv3dv per probe.
**RESTORED quake as the boot image** (devices 60d48e6 + project 453ac33) so you boot to the working
Quake+audio demo, not a hung harness. To resume Vulkan: re-swap (Makefile + plo) + touch the stub;
/tmp libs are present.

### Decision recorded: Vulkan is incremental-only unattended; not reaching vkQuake this window
The vkQuake road is long+heavy (device-create hang → Tier 2 clear+readback → full renderer → port
vkQuake), each step gated on a host-meson libv3dv rebuild. I'll make incremental progress when it's the
best available item, but it won't reach a demo unattended. Flagged for your Friday call: worth a focused
attended session.

### 2026-06-17 — ★ /dev/urandom now HW-RNG-backed (security/publication fix) — posixsrv ef6e39b
Publication review surfaced a real defect: posixsrv served /dev/urandom from rand()/srand(time(NULL))
— a weak, predictable PRNG — despite the system having a hardware RNG (/dev/hwrng, BCM2711 RNG200). For
a published OS that's a security bug (any crypto/uuid/session gets guessable bytes). Fixed: random_read_op
now reads real entropy from /dev/hwrng (lazy open since posixsrv starts before the hwrng driver; rand()
fallback so it stays portable to targets without /dev/hwrng). Also fixed a latent bug where the old loop
never advanced the dest pointer (reads >64B left a tail uninitialised). HW-verified via scripted psh:
"posixsrv: /dev/urandom entropy source = /dev/hwrng (hardware RNG)" + a 64B dd read succeeds. Ties the
existing rpi4-hwrng driver into something the whole system uses.

### 2026-06-17 — ★ libc getrandom()/getentropy() added (broad app support) — libphoenix 40053fd
Follow-on to the /dev/urandom fix: libphoenix had NO getrandom()/getentropy() — modern crypto libs
(libsodium, recent OpenSSL) + many runtimes require them, so their absence limited app support. Added
sys/random.h + stdlib/getrandom.c (both wrap /dev/urandom → now HW-RNG-backed; GRND_* accepted+ignored
since urandom never blocks; getentropy caps 256B). Runtime-verified by extending rpi4-ipcprobe to probe
entropy too: getentropy+getrandom fill 32B, two draws differ 32/32 bytes (real HW entropy). Probe
disabled from default boot again (one-shot). FULL entropy stack now real-RNG-backed: /dev/hwrng →
/dev/urandom (posixsrv) → libc getrandom/getentropy. devices b3cf23e + project bd091f8.

### Tally so far this unattended run (for your Friday review)
1. Audio DMA mechanism (PWM1=DREQ 1). 2. Quakespasm SNDDMA audio backend (feeder thread). 3. Stale
"caches-off" doc corrections. 4. X11 AF_UNIX foundation gate (READY). 5. Continuous streaming DMA audio.
6. Vulkan Tier-1: instr-abort cleared → vkCreateDevice (hangs, localized). 7. /dev/urandom HW-RNG-backed.
8. libc getrandom()/getentropy(). Plus license-header audit (clean). All committed + HW-verified where
applicable; flagship Quake+audio image is the persisted boot state.

### 2026-06-17 — Publication scan #2 (clean) + ★ rpi4-sysinfo boot banner (usability/demo)
Scanned the Pi4 kernel hal + device drivers for leftover diagnostics / non-TD "temporary/hack"
markers / commented-out code: all hits are legitimate (temporary identity map, A72 erratum workaround,
scratch page, DEBUG register name, intentional boot self-logs). Code confirmed publication-clean again.
Then delivered the documented Tier-A "feels alive" item: rpi4-sysinfo (devices 9... + project 7087e3e)
— a boot banner printing build stamp, uptime, an 8-byte hardware-RNG sample (via the new getentropy),
and a present/absent inventory of the /dev nodes. HW-verified: 10/12 nodes present (kbd0/mouse0 pending
USB enum at snapshot), entropy non-constant, boot proceeds to Quake. Permanent (every boot). Showcases
the whole Pi4 device suite (thermal/throttled/hwrng/gpio/fb0/audio0/urandom) in one screen.

### Tally so far this unattended run (for your Friday review) — 9 items
1. Audio DMA mechanism. 2. Quakespasm SNDDMA audio backend. 3. Stale "caches-off" doc corrections.
4. X11 AF_UNIX foundation gate (READY). 5. Continuous streaming DMA audio. 6. Vulkan Tier-1: instr-abort
cleared → vkCreateDevice (hangs, localized). 7. /dev/urandom HW-RNG-backed. 8. libc getrandom()/
getentropy(). 9. rpi4-sysinfo boot banner. 10. psh `mv` applet (was a missing MUST). 11. ★ X11 library
port — the ENTIRE client+rendering+font stack cross-compiles for aarch64-phoenix (36 archives:
libX11, libxcb+24 exts, libXext, libXrender, libXfont2, libfontenc, libfreetype, libpixman-1, libXau,
libXdmcp, libz; tools/x11-port/, isolated, flagship untouched) + 4 libphoenix libc fixes (getpwuid_r,
getpwnam_r, sys/poll.h, hypot). Remaining = kdrive Xfbdev server (multi-session). Plus 2 publication
scans (code clean) + license audit + restored MEMORY.md recall. All committed + HW-verified where
applicable; flagship Quake+audio+banner+mv is the persisted boot state.

### 2026-06-17 — ★ X11 library port STARTED (named capstone; host-side, isolated, flagship-safe)
Advisor reconciled: the host-side/fast-iterate/additive nature (no Pi boots, isolated prefix) removes
the risks it had flagged, so starting X11 is right given your repeated explicit mandate. Built the base-
lib tier into /tmp/x11-phoenix: xorgproto (129 headers), libXau.a, xtrans, libXdmcp.a — all cross-compile
for aarch64-phoenix (config.sub accepts --host=aarch64-phoenix; static-lib recipe vs the toolchain+sysroot
works). Reproducible: tools/x11-port/build-x11-phoenix.sh; ladder+blockers in tools/x11-port/PROGRESS.md.
HARD RULE honored: X11 is NEVER in the rpi4b default components / flagship image — it lives beside the OS
in /tmp until a server is runnable. coord 224e30e.

### NEXT ACTION (decisive): continue the X11 ladder — libxcb, then libX11
One brick per iteration (host-side, fast): next = xcb-proto (python codegen) + libxcb (--disable-mitshm,
since libphoenix lacks shm_open) → then libX11 → pixman → the kdrive Xfbdev server. Document each brick +
any Phoenix libc wall in PROGRESS.md; if a brick hits a hard wall, log it + move to the next independent
brick. Keep X11 out of the flagship. (Deep items Vulkan-winsys/GENET-perf remain attended/lower-value.)

### 2026-06-17 — ★ Vulkan vkCreateDevice hang ROOT-CAUSED + FIXED (is_shim); new blocker localized
Engaged the named Vulkan goal. The vkCreateDevice HANG was threaded submit: v3dv enables a submit
thread when !is_shim, but our winsys is synchronous with no real syncobj → the device-create noop-job
submit waited forever on a never-signaled syncobj. FIX (external/mesa 52b08a7987d): force is_shim=true
on Phoenix (selects the synchronous no-threaded-submit path = our model). Verified: moved PAST the hang.
NEW blocker (precisely localized): vkCreateDevice now ABORTS (pc=0) calling a NULL fn-ptr in the
physical-device dispatch table (Properties2/QueueFamilyProperties2 slot) — the entrypoints exist + the
table is built + passed to vk_physical_device_init, yet the runtime slot is NULL (the built table isn't
reaching the live pdevice->dispatch_table). Needs instrumentation (libv3dv rebuild per probe — feasible,
~min/cycle). Mesa patch refreshed (4 files). Quake restored as the boot image. devices/project swap-back
committed. So Vulkan is 2 blockers further along: instance ✓, enumerate ✓ (count=1), abort ✓ (was the
cosmetic name print), hang ✓ (is_shim) → now the phys-dispatch NULL.

### 2026-06-17 — Vulkan vkCreateDevice instrumented; blocker moved DEEP (noop-job CL-NULL)
Instrumented v3dv_CreateDevice (temp prints, since reverted — external/mesa HEAD stays clean is_shim-only).
The phys-dispatch P2 (GetPhysicalDeviceProperties2) slot is NULL (only that one; QFP2/Feat2 are fine —
it only bit the skipped cosmetic name print). With is_shim fixing the hang, CreateDevice now PROGRESSES:
vk_device_init ✓ → queue_init (1 queue) ✓ → init_device_meta ✓ → then DATA-ABORTS (far=0) in
v3d42_job_emit_binning_prolog (strb to a NULL CL ptr at job+104), reached via v3dv_device_create_noop_job.
So the noop job's binner control-list BO isn't allocated/mapped. NEXT (6th blocker): the winsys/V3DV
BO+CL interop for the noop job (v3dv_cl_ensure_space → v3dv_bo_alloc → winsys create_bo/mmap returns
NULL for the V3DV path; GL works, so it's a V3DV-vs-GL diff). Precisely documented in
project_vulkan_v3dv_port memory for the next session to continue. Flagship Quake restored; net code this
turn = the is_shim fix (committed last turn) + this localization (knowledge). Vulkan is now 5 blockers
cleared, 6th localized — the furthest Vulkan-on-Phoenix has reached.

### NEXT ACTION (decisive): frontier nearly exhausted of safe bounded items
Remaining: (a) the Vulkan vkCreateDevice-hang (named goal — instrument external/mesa v3dv_device.c +
host-meson libv3dv rebuild loop; heavy but the furthest-along named target); (b) more small usability
(a 2nd demo app / a psh-runnable tool); (c) consider winding the loop down with a final state summary
if no high-value bounded item remains. Lean (a) as the named ambitious goal if iterations remain.
Avoid: X11 lib port (weeks), BT/umass/I2C/SD/WiFi (hardware).

### 2026-06-18 — ★★ X11 EXECUTABLE BOUNDARY CROSSED — first X11 app (twm) builds for Phoenix — coord 0cb9f72→fce360c
Continued the X11 port to a real milestone. (1) **libXaw** (Athena widgets, the last toolkit lib) built
once I completed libphoenix's wide-char set — added wcsncpy/wcscpy/wcscat/wcschr/wcsrchr/wcsncmp/wmem*
(libphoenix `0cb9f72`). So the full lib stack = client+render+font+toolkit. (2) Crossed the **executable
boundary**: rebuilt libphoenix (`--scope core --build-only`) so the committed libc additions land
on-device, then linked the FIRST X11 executables for aarch64-phoenix: `xprobe` (a minimal Xlib client)
and — the headline — **`twm` 1.0.12, a complete X11 window manager** (3.1 MB static ELF; binaries in
`artifacts/x11/`). En route I had to (a) complete the **C-locale multibyte set** (mblen/mbtowc/wctomb/
mbstowcs/wcstombs — wctomb was a broken stub returning 0; libphoenix `e29c840`), (b) discover + script
the fact that **the cross-toolchain bundles its own stale libphoenix/libc/libm** (the auto-linked libc)
distinct from the build sysroot — `sync_toolchain_libc()` now refreshes it, and (c) build libXrandr +
use `PKG_CONFIG="pkg-config --static"` so static private deps reach the link line. The full X11
client+toolkit+a real app now build for Phoenix; the ONLY remaining gate is the kdrive Xfbdev **server**
(deep/multi-session — nothing X runs without it). All host-side/isolated; flagship untouched. Commits:
libphoenix `0cb9f72` (wide-char) + `e29c840` (multibyte); coord `a4be46b` (libXaw) + `fce360c` (twm).
**LESSON banked:** after any libphoenix change, the toolchain-bundled libc must be re-synced or ad-hoc/X
exe links silently use the stale one (mbtowc/mblen "undefined" despite being in the fresh sysroot lib).

### Tally — 2026-06-18 (this unattended run, cumulative)
Flagship-shipping: audio subsystem (driver+DMA+Quake backend), /dev/urandom HW-backed, getrandom/
getentropy, rpi4-sysinfo banner, psh mv. Libc completeness (additive, on-device): getpwnam_r/getpwuid_r/
sys-poll, hypot, alloca size_t, full wide-char set, full C-locale multibyte set. **X11: 45-archive lib
stack + xprobe + twm window manager build as aarch64-phoenix ELFs** (server is the remaining gate).
Vulkan: 5 blockers cleared, 6th (noop-job CL) localized. All netboot/host-validated; flagship 0-fault.
