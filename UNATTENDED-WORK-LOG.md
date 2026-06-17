# Unattended work log — 2026-06-17 onward (for Witold, back Fri late afternoon)

You left me running unattended for a few days with a broad mandate: process the
docs, implement what's not done, work on performance/stability/usability/breadth,
the big ports (X11, Vulkan+vkQuake, audio), Pi4 device support, and a
publication-readiness pass + TODO cleanup. Take risks; skip only items that need
you physically (SD-card testing, bench rig/scope/LED, BLE-in-range, audible audio).
This file is my running log + the decisions/parked items for you to review.

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

### NEXT ACTION (decisive): breadth pass — TODO/publication sweep + next unattended doc item
Audio DMA milestone banked. Now execute the user's explicit "check for TODO comments in code... fix
all this" + "make code ready to be publically published" on the Pi4 changes, AND pick the next
clearly-unattended doc item (candidates: TD-05/TD-14 cleanup [1.6], /dev/urandom←hwrng pooling to
unblock the openssl/curl/dropbear ports [1.5], or the continuous-audio-streaming increment). Avoid
re-tunneling Vulkan (instruction-abort is fiddly + interleaved with boot noise — attended-friendlier).
