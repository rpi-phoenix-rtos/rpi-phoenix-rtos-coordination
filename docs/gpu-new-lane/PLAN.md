# The new GPU lane — plan and status

Design: [`docs/research/2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md).
Owner directive (2026-09-26): build it autonomously and in parallel, **without breaking the old GPU
lane**, then migrate every GPU user and delete the old lane.

## Ground rules

1. **The old lane stays demo-able the whole time.** The five games, the glamor X desktop, SDL2 and
   vkQuake keep running on today's in-process winsys, `/dev/fb0` and firmware-pan present until the
   final migration. Nothing in the old path changes behaviour by default.
2. **The new lane is additive.** New servers, libraries and probes live in new directories and new
   binary names; test apps are **clones** (e.g. `stk-newlane`, a second X server binary) — never the
   shipped binaries. A new server is not started by the default boot scripts until its milestone
   gate passes.
3. **Kernel changes are additive.** A new facility (e.g. `vm_objectExport`) is appended — no
   renumbered syscalls, no changed semantics for existing callers — and must pass the stock
   `--scope core` gate plus the five-game + X desktop gate before it lands on `master`.
4. **Parallel work, serialized hardware.** Subagents write and compile code (`syntax-check.sh`,
   standalone builds into their own directories). They never run Pi cycles or `rebuild-rpi4b-fast.sh`
   (its image stage overwrites the TFTP `loader.disk`); the coordinating session does.
5. **Agents editing existing sibling code work in git worktrees** on a temporary `gpu-lane/<topic>`
   branch (e.g. `/home/houp/.claude/jobs/…/wt-libphoenix`), never in `sources/<repo>` directly: every
   image build compiles the `sources/` working trees, so an uncommitted edit leaks into an unrelated
   build. The coordinator reviews, merges to `master` and deletes the branch (no stray branches).
   New files in brand-new directories that no Makefile references are the one exception.
6. **Every experiment is pre-registered** (question, method, what each outcome means) in its own file
   here before its first Pi cycle, and gets a result section afterwards.
7. **Migration is the last step.** When M3 (or M4 for X) is ready, all GPU users move in one planned
   migration with the full showcase gate, then the old winsys, scanout hooks and kdrive DDX are removed.

## Milestones

| # | What | Status |
|---|---|---|
| **M0** | E1 kernel export prototype; E2 STK submit breakdown; E3/E6 firmware planes + SMI vblank + scanout range; E5 deferred reply / event blocking | ✅ done 2026-09-26 (E1/E3/E5 PASS on the Pi, E2 = serial mix + render phase 3× slow → E2b; E7 builds) |
| M1 | async multi-queue render server (`rpi4-v3d` evolved), fence page, syncobjs; cloned games on it | ▶ **STK `stk-v3da` 12.12 fps vs old lane 8.34 (+45 %) = Raspberry Pi OS parity** (queue14, HDMI-verified, 0 wedges/rejects); quakespasm-v3da 40.4 fps; **EINVAL draw FIXED** (`05141ff7d`, control proved the cause: ~90 chained BCLs/run dropped). quakespasm same clock: 40.4 vs 33.4 (+21 %). Open: pipeline mode shows `overlap=0` (bin∥render never overlaps — STK gains are CPU∥GPU) |
| M2 | `rpi4-kms` Stage A: firmware planes, vblank events, atomic flips, dumb-BO pool, fbdev emulation | ✅ **Stage A core PASS on the Pi** (`m2-kms-b`): 600/600 flips at 60.00 fps, interval 16665–16670 µs, event delivery 28 µs, pan fallback, clean exit (fixes: parked-read rebase, thread endthread). Remaining: fbdev emulation ([M2 doc](M2-kms-server.md)) |
| M3 | kernel export productised; libdrm-phoenix; Mesa GBM/EGL; SDL2 KMSDRM | ▶ libdrm-phoenix ✅; server gaps ✅ (`drmprobe` 36/36); **Mesa GBM/EGL ✅ kmscube on HDMI at 60.00 fps** (was 30: rpi4-kms never woke on a fence-deferred flip, fixed `efa4c1f9f`; kernel `pollNotify` shipped, P9 mitigated); **SDL2 KMSDRM ✅ `quakespasm-drm` 30.9 fps** (GPU-bound + vblank wait). First-crash root cause: Mesa never queried DRM_CAP_PRIME on Phoenix (patch 0008) |
| M4 | Xorg + modesetting + glamor + DRI3/Present | ▶ prep: **`Xorg-drm` builds** (static xorg-server 21.1.24 + modesetting + glamor on GBM/EGL GLES + DRI2/3/Present/Xv, builtin-module table, `phxhid` input; [M4 doc](M4-xorg-modesetting.md)); first cycle `m4a` (05:38): modesetting opens /dev/dri/card0, then `Fatal server error: Cannot run in framebuffer mode` (legacy fb-slot probe without a platform bus) — fix in progress (agent). Waits on G4/G7/G16 for DRI3 client buffers, G12 for vsync pacing |
| M5 | Vulkan WSI (display, xcb) | ▶ display half built: **`vkcube-drm`** (static v3dv + VK_KHR_display, no loader — `phxvk`; [M5 doc](M5-vulkan.md)); libdrm gaps G4a (render-node re-export of an imported BO) + G17 (atomic cap implies universal planes) fixed; cycle `m5-vkcube` queued. xcb half waits on M4 DRI3/Present + G4/G6 |
| M6 | Wayland (Weston DRM backend) | — |
| Migration | all GPU users moved, old lane deleted | — |

## M0 experiments

| ID | Question | File | Status |
|---|---|---|---|
| E1 | Can the kernel expose server-owned pages under an oid for zero-copy, refcounted `mmap(fd)`? | [E1-vm-object-export.md](E1-vm-object-export.md) | ✅ **PASS** on the Pi (build 9, kernel `38ad32cf`): same PA both sides, writes both directions, memory type enforced, survives unexport, crosses AF_UNIX as an fd. Side results: object-tree fix `d0fb0ca9`; [port-death](port-death.md) fix PASS vs baseline FAIL |
| E2 | What is STK's ~88 % "in submit" made of? | [E2-stk-submit-breakdown.md](E2-stk-submit-breakdown.md) | ✅ **SERIAL MIX**: 70 % GPU wait (render **91 ms/frame**), 30 % CPU, maintenance 0.2 %; async submit ≤ ×1.43 for STK; clone overhead 0.9 %. ★ The V3D render phase is ~3× too slow → **E2b** |
| E3 / E6 | Does the pinned firmware honour `SET_PLANE` + raise SMI vblank IRQs? Which physical range can it scan? | [E3-firmware-planes-vblank.md](E3-firmware-planes-vblank.md) | ✅ **Stage A viable**: vblank IRQ 60.01 Hz; planes on HDMI; 60 vsynced flips/s 0 missed; 12 443 flips/s unsynced (SET_PLANE p50 78 µs); scans **only the low 1 GiB** (↩ corrected: the >1 GiB buffer showed noise); 256 MiB contiguous OK |
| E2b | Why is the V3D render phase 91 ms/frame at 500 MHz, resolution-independent? | [E2b-v3d-render-slowness.md](E2b-v3d-render-slowness.md) | render phase **shader-bound** (corrected QPU normalisation); EZ no gain; QRMAXCNT=3 small (1 trial); **core clock 250→500 = +13 %, adopted**; **H7 (Linux L2T order) refuted: 0.0–0.2 % fps**, guard mildly unfavourable → knobs stay off. Remaining 3× question → E2c (Pi OS on this board) |
| E2c | What do the same STK/quakespasm settings give on Raspberry Pi OS on this board (fps, V3D render ms/frame)? | [E2c-pios-baseline.md](E2c-pios-baseline.md) | ✅ **Pi OS STK 11.7 fps, render 83 ms/frame at 97 % busy (97 ms at core 250)** — GPU-bound on the same phase; Phoenix's render was never 3× slow. **New lane `stk-v3da` 12.1 fps = Pi OS parity**; old lane 8.3 (71 %). Run 1 void (no udev → vc4 incomplete; fixed by coldplug) |
| E7 | Does libdrm + Mesa GBM/EGL build for Phoenix? | [E7-drm-userspace-build.md](E7-drm-userspace-build.md) | ✅ compiles; static GBM+EGL+GLES+KMS program links; seams + libphoenix gaps (agent in worktrees) |
| core-500 | Adopt `core_freq=500` (+13 % GPU) safely | [core-clock-500.md](core-clock-500.md) | ✅ **adopted** 2026-09-27: gate PASS (BT, WiFi, audio, 0 exceptions; STK 8.25 / 8.30 fps vs 7.43); devices `27db916` + project `e70e124` on `master`; manifest `2026-09-27-core-freq-500.md` |
| E5 | Can a server `msgRespond` later from another thread? Does `block_ms` event blocking work? | [E5-deferred-reply.md](E5-deferred-reply.md) | ✅ PASS: RTT 31 µs, deferred replies 32/32, fence WAIT 23 µs, blocking read 20 µs, `poll()` 0–20 ms (needs `block_ms` change for M4/M6); dead-server wedge fixed on branch `gpu-lane/port-death` (build 9) |

## Log

- 2026-09-26: plan created; four M0 agents launched in parallel (code only); Pi cycles queued behind
  the running `c1sd` series.
- 2026-09-26 19:00: all four M0 agents delivered. Queue: E5 → build 8 (tree fix) → `c1tf` ×6 → E2 →
  build 9 (E1 + vcmbox-xl) → E1 + E3 probes. Started in parallel: M1 design + skeleton (new binary,
  old `rpi4-v3d` untouched) and E7 (libdrm + Mesa GBM/EGL build study, own build dir).
- 19:05: E7 done — libdrm + Mesa DRM path compile; a static GBM+EGL+GLES+KMS program links
  ([E7-drm-userspace-build.md](E7-drm-userspace-build.md)). E5 first run void (psh has no `&`);
  probe fixed, re-run queued. libphoenix-gaps agent started in worktrees (`gpu-lane/libc-gaps`).
- 19:55: E5 PASS; E2 = SERIAL MIX (render phase 91 ms/frame is the real gap → E2b agent); c1tf shows
  the object-tree fix is not C1's cause (fix kept); M1 part 2 written; port-death kernel fix on a
  branch. Queues: M1 ping → portdeath baseline → build 9 (E1 + port-death + vcmbox-xl) → smoke →
  E1/port-death probes → E3 → M1 P2-A.
- 23:10: **M0 complete.** E3 Stage A viable (60 Hz vblank, planes on HDMI, 12k flips/s); E1 + port-death
  PASS; M1: part-1 + P2-A PASS, P2-B +26 % timedemo **but frames not on HDMI** → present-path agent;
  core clock 500 MHz = +13 % (adoption agent); M2 skeleton compiles (doc being finished).
- 23:30: M1 P2-B **PASS** (my "frames not on HDMI" was a misread of pre-launch snapshots — corrected);
  M2 server + kmstest compile, cycle queued; E3 corrected (scan-out only below 1 GiB); libphoenix gaps
  done on branch `gpu-lane/libc-gaps` (merge = build 10, needs the old-lane Mesa barrier shim removed first);
  core-500 gate queued. Queues: E2b arms → core-500 gate → M2 first cycle.
- 2026-09-27 00:25: Pi queue: core-500 gate (G1 BT PASS at 500 MHz) → M2 `m2-kms-a` → E2b H7 arms →
  E2c Pi OS baseline → WiFi first-join wake test. Agents: M3 libdrm-phoenix (running), M1 EINVAL-draw
  analysis (server tags every submit reject), M1 `stk-v3da` clone build.
- 2026-09-27 04:10: overnight queues done. **STK on the new lane = Pi OS parity** (12.1 vs 11.7 fps; old lane 8.3); E2c shows Pi OS is GPU-bound at the same render cost (E2b's 3× premise void); EINVAL fix confirmed; core_freq=500 adopted; H7 refuted; M2 display PASS / events FAIL (fix with M3p2).
- 2026-09-27 05:45: **kmscube + quakespasm-drm render on HDMI through the whole new stack** (poll-capped ~30/24 fps; kernel poll-wake agent). Build 10 (libc-gaps merge) gate PASS and pushed; the new inttypes test found a real scanf bug (fixed `5020478`). Xorg-drm built; M4 first cycle `m4a` running.
- 2026-09-27 07:45: **kmscube 60.00 fps** (deferred-flip wake fix A/B PASS); pollNotify kernel shipped (build 11, showcase 6/6); m5-vkcube hangs after the first sync_file export (agent); M4 m4b root cause = libphoenix ctype double evaluation (fix staged for the upstream-sync build 12).
