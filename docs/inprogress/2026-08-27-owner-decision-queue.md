# Owner decision queue — for Witold's return (as of 2026-08-27, autonomous session)

You've been away a while; the autonomous loop has kept working the master plan. This is the
**short list of decisions that need your call** (the board `autonomous-plan.md` has the full
chronological log; `pi4-hardware-support-matrix.md` has the capability state). Everything below
is at a clean stopping point — nothing is half-broken waiting on you.

## Shipped since you left (headline)
- **SuperTuxKart 1.4 PLAYS on the Pi4** — modern GLES3/SP renderer, one static ELF: boots → full
  engine init → main menu → **fully-lit in-game 3D race** on the V3D over netboot, 0 crashes. This
  killed the old "structurally blocked / defer multi-week" feasibility verdict.
- **Two real V3D driver fixes shipped** (found while bringing STK up, benefit all GPU apps):
  (1) GPU VA window 256 MiB→1 GiB (`GPUVA_PT_PAGES`) — fixes heavy-scene VA-exhaustion crash;
  (2) **QPU-interrupt-ack fix** — the CT1 render stage wedged per-frame under heavy fragment load
  because the poll-based winsys never cleared the QPU interrupt bits (Linux clears full INT status
  every IRQ); fix = Linux-parity clear+service → 330 wedges/run → 0, dark scene → fully lit.
- **Mesa on-disk shader cache implemented** (was stubbed) — GL apps no longer recompile shaders on
  the V3D every boot (HW-verified). Footgun documented: bump `V3D_PHX_CACHE_VERSION` on Mesa
  QPU-codegen changes (no build-id auto-invalidation).
- **ffmpeg → framework port** (P8) done; **python → framework port** in progress.

## DECISIONS AWAITING YOU (ranked)

1. **HEVC hardware video decode — pursue the full port, or leave banked?**
   Scoped + M0 HW-proven this session. Verdict: HW **H.264 decode is a VCHIQ/firmware wall** (~27k LOC,
   WiFi-scale) → banked. HW **H.265/HEVC via the `rpivid` block IS tractable** (directly-MMIO, no VCHIQ);
   `tools/hevc-probe` proved the block is reachable (version 0x202, clock via mailbox). **The catch:** it
   decodes **H.265, not the H.264** your e4-play clips use — so it's only worth ~4-8 weeks if your media
   target moves to H.265. **My call:** banked pending your content-strategy decision. Detail:
   `project_ffmpeg_hw_decode_scope` / the matrix "Video decode" row.

2. **gcc-16 promotion to the default toolchain?**
   The gcc-16.2.0 cross-toolchain builds core+ports clean AND boot-verified on HW (E10). Remaining = the
   *promotion* (swap gcc-16 → default `.toolchain/`, full-flow rebuild, manifest) — HIGH blast-radius, so
   I left it owner-attended (gcc-14 rollback kept). **Your call:** promote now, or keep gcc-14 default.

3. **WiFi data-plane — keep digging, or accept wired-only for now?**
   Control-plane is up (associates + 4-way-keyed to a real WPA2 AP). Data-plane doesn't carry traffic yet
   (TX reaches firmware, not the air — SDPCM seq/credit); it's banked at the firmware-opaque wall (I've
   avoided blind-coding SDPCM). **Your call:** prioritize a deeper WiFi dig, or stay on wired Ethernet.

4. **q3dm7 intermittent GPU binner wedge (CT0) — worth an attended dig?**
   Separate from the (now-fixed) CT1 render wedge; different signature, intermittent ~50%, owner-attended-
   adjacent. quake3 renders correctly (SSIM 0.989); this is a dropped-frame robustness issue, recovered by
   the winsys reset. **My call:** banked; the STK render fix may or may not touch it (untested — needs a
   quake3 relink + multi-trial bench).

5. **Upstream B5** (the one deferred B-item) — lowest rpi4 value, cross-board/attended. Your call whether
   it's worth an attended pass.

6. **v3d-driver-port placement — the last P8 item.** Everything else in the "move tools/ → framework ports"
   directive is DONE (libpng…glib2, ffmpeg, python, all 4 game ports — every tools/ port is now a registered
   framework port). The ONLY holdout is the V3D driver itself: does it become a `phoenix-rtos-devices` GPU
   *component*, or stay a `tools/` build producing `tools/.gpu-libs/*.a`? The game ports currently anchor to
   `tools/.gpu-libs/` (the STK precedent); that anchor changes if V3D moves to devices, so I left the
   placement to you. **My call:** low urgency (it works as-is); decide when you next touch the GPU stack.


## What I'll keep doing autonomously (no input needed)
- Finish P8 tools/→framework port migrations (python in flight; then the 4 game ports — which also
  auto-relink the Quakes against the QPU-int + shader-cache libv3d).
- Bounded residue: propagate the QPU-fix to the rest of the GL stack; the Mesa NULL-BO-alloc→crash
  robustness gap.
- The big *capability* thrusts are now largely done / firmware-walled / owner-gated (above), so the
  autonomous work is trending toward hygiene + the decisions above. If it fully drains I'll say so rather
  than force low-value work.
