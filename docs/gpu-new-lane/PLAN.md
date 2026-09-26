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
| **M0** | E1 kernel export prototype; E2 STK submit breakdown; E3/E6 firmware planes + SMI vblank + scanout range; E5 deferred reply / event blocking | ▶ started 2026-09-26 |
| M1 | async multi-queue render server (`rpi4-v3d` evolved), fence page, syncobjs; cloned games on it | — |
| M2 | `rpi4-kms` Stage A: firmware planes, vblank events, atomic flips, dumb-BO pool, fbdev emulation | — |
| M3 | kernel export productised; libdrm-phoenix; Mesa GBM/EGL; SDL2 KMSDRM | — |
| M4 | Xorg + modesetting + glamor + DRI3/Present | — |
| M5 | Vulkan WSI (display, xcb) | — |
| M6 | Wayland (Weston DRM backend) | — |
| Migration | all GPU users moved, old lane deleted | — |

## M0 experiments

| ID | Question | File | Status |
|---|---|---|---|
| E1 | Can the kernel expose server-owned pages under an oid for zero-copy, refcounted `mmap(fd)`? | [E1-vm-object-export.md](E1-vm-object-export.md) | code done (patch in `tools/gpu-lane/exportprobe/`); ★ found a real kernel bug (contiguous-object rb-remove empties the object tree) — fixed alone (kernel `d0fb0ca9`), build 8 + C1 A/B queued; E1 itself → build 9 |
| E2 | What is STK's ~88 % "in submit" made of? | [E2-stk-submit-breakdown.md](E2-stk-submit-breakdown.md) | code done (devices `0425f93`, default build byte-identical; clone `stk-prof`); Pi runs queued on build 8 |
| E3 / E6 | Does the pinned firmware honour `SET_PLANE` + raise SMI vblank IRQs? Which physical range can it scan? | [E3-firmware-planes-vblank.md](E3-firmware-planes-vblank.md) | code done; `SET_PLANE` (60 B) exceeds `/dev/vcmbox` (48 B) → additive `vcmbox-xl.patch` goes into build 9 |
| E5 | Can a server `msgRespond` later from another thread? Does `block_ms` event blocking work? | [E5-deferred-reply.md](E5-deferred-reply.md) | kernel reading definitive: yes (same process); `read()` blocks, `poll()` 0–20 ms late; a dead server wedges parked clients. Pi run queued (build 7) |

## Log

- 2026-09-26: plan created; four M0 agents launched in parallel (code only); Pi cycles queued behind
  the running `c1sd` series.
- 2026-09-26 19:00: all four M0 agents delivered. Queue: E5 → build 8 (tree fix) → `c1tf` ×6 → E2 →
  build 9 (E1 + vcmbox-xl) → E1 + E3 probes. Started in parallel: M1 design + skeleton (new binary,
  old `rpi4-v3d` untouched) and E7 (libdrm + Mesa GBM/EGL build study, own build dir).
- 19:05: E7 done — libdrm + Mesa DRM path compile; a static GBM+EGL+GLES+KMS program links
  ([E7-drm-userspace-build.md](E7-drm-userspace-build.md)). E5 first run void (psh has no `&`);
  probe fixed, re-run queued. libphoenix-gaps agent started in worktrees (`gpu-lane/libc-gaps`).
