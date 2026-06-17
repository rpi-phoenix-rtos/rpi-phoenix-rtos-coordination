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
