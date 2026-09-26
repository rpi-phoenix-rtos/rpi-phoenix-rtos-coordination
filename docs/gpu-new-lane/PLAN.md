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
| M1 | async multi-queue render server (`rpi4-v3d` evolved), fence page, syncobjs; cloned games on it | ▶ **quakespasm-v3da +26 % (38.2 vs 30.4 fps), on HDMI, 0 wedges**; part-1 + P2-A PASS (IRQ works, jobs pixel-exact). Open: one EINVAL draw per run; STK clone; pipeline overlap proof |
| M2 | `rpi4-kms` Stage A: firmware planes, vblank events, atomic flips, dumb-BO pool, fbdev emulation | ▶ design + server/test tool compile (`tools/gpu-lane/kms/`, [M2 doc](M2-kms-server.md)); SET_PLANE default, pan fallback; first Pi cycle `m2-kms-a` pre-registered |
| M3 | kernel export productised; libdrm-phoenix; Mesa GBM/EGL; SDL2 KMSDRM | — |
| M4 | Xorg + modesetting + glamor + DRI3/Present | — |
| M5 | Vulkan WSI (display, xcb) | — |
| M6 | Wayland (Weston DRM backend) | — |
| Migration | all GPU users moved, old lane deleted | — |

## M0 experiments

| ID | Question | File | Status |
|---|---|---|---|
| E1 | Can the kernel expose server-owned pages under an oid for zero-copy, refcounted `mmap(fd)`? | [E1-vm-object-export.md](E1-vm-object-export.md) | ✅ **PASS** on the Pi (build 9, kernel `38ad32cf`): same PA both sides, writes both directions, memory type enforced, survives unexport, crosses AF_UNIX as an fd. Side results: object-tree fix `d0fb0ca9`; [port-death](port-death.md) fix PASS vs baseline FAIL |
| E2 | What is STK's ~88 % "in submit" made of? | [E2-stk-submit-breakdown.md](E2-stk-submit-breakdown.md) | ✅ **SERIAL MIX**: 70 % GPU wait (render **91 ms/frame**), 30 % CPU, maintenance 0.2 %; async submit ≤ ×1.43 for STK; clone overhead 0.9 %. ★ The V3D render phase is ~3× too slow → **E2b** |
| E3 / E6 | Does the pinned firmware honour `SET_PLANE` + raise SMI vblank IRQs? Which physical range can it scan? | [E3-firmware-planes-vblank.md](E3-firmware-planes-vblank.md) | ✅ **Stage A viable**: vblank IRQ 60.01 Hz; planes on HDMI; 60 vsynced flips/s 0 missed; 12 443 flips/s unsynced (SET_PLANE p50 78 µs); scans **only the low 1 GiB** (↩ corrected: the >1 GiB buffer showed noise); 256 MiB contiguous OK |
| E2b | Why is the V3D render phase 91 ms/frame at 500 MHz, resolution-independent? | [E2b-v3d-render-slowness.md](E2b-v3d-render-slowness.md) | pre-registered; instrument built (PCTR per job + Mesa job notes + census + EZ / core-clock / QRMAXCNT knobs; clone `stk-e2b`; default builds byte-identical). ★ Premise void: render scales with RTT pixels (launcher scale_rtts table). ★ `core_freq=250` pinned in config.txt since April (Pi OS 500); EZ forced off in Mesa. Pi runs not started |
| E7 | Does libdrm + Mesa GBM/EGL build for Phoenix? | [E7-drm-userspace-build.md](E7-drm-userspace-build.md) | ✅ compiles; static GBM+EGL+GLES+KMS program links; seams + libphoenix gaps (agent in worktrees) |
| core-500 | Adopt `core_freq=500` (+13 % GPU) safely | [core-clock-500.md](core-clock-500.md) | branches `gpu-lane/core-clock` (devices `27db916` WiFi SDHCI clock query; project `e70e124` config.txt); gate queued |
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
