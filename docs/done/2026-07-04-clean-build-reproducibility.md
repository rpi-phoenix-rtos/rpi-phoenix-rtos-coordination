# Clean-build reproducibility (publication readiness) — night of 2026-07-03→04

**Goal (user):** a deterministic FULL build (base + all default showcase apps → SD image)
on a clean, separate VM, reproducible by a stranger with no access to our machines/files.
Rules: host is the only workspace; VM is a disposable clean-build target (never edit/develop
there); distribution is git-only (clone from host = stand-in for github; **no rsync**); no
github push; final gate = wipe VM → blank Ubuntu → full build from zero.

## Root causes found + fixed (all are build-SCRIPT gaps, not port-code bugs)

The ported apps (xterm, Xphoenix, mesa/GLQuake) build correctly on the dev host; they failed
on a clean VM because scripts let the result depend on host/accumulated state.

1. **xterm** (`tools/x11-port/build-xterm.sh`) — same source, different `./configure` outcome.
   `CF_FUNC_TGETENT` link-tests `tgetent`; once the ncurses port (#52) puts `libncurses.a` in
   the sysroot, configure defines `USE_TERMINFO`, but the `xtermcap.h` patch bypasses `<term.h>`
   → terminfo entry points undeclared → fail. Result depended on build ORDER (ncurses-before-xterm).
   **Fix:** pin `cf_cv_lib_tgetent=no cf_cv_lib_part_tgetent=no` → neither USE_TERMCAP nor
   USE_TERMINFO defined (the termcap `#else` path the stub satisfies), on any machine.
   Commit cfba0ac. **VALIDATED on VM (builds clean).**

2. **Xphoenix** (`tools/x11-port/build-xfbdev.sh`) — it links the X server from the xorg-server
   1.20.14 core archives but never built them (that was a manual step). **Fix:** new
   `tools/x11-port/build-xserver-core.sh` fetches xorg-server 1.20.14 + builds libmd (SHA1) into
   $PREFIX + configures (exact dev-host config.log invocation; hermetic PKG_CONFIG to
   $PREFIX/{lib,share}/pkgconfig) + makes the core; `build-xfbdev.sh` calls it as an idempotent
   preamble. Commit cfba0ac. **VALIDATED on VM (Xphoenix links, published).**

3. **mesa (and, it turned out, quakespasm + vkquake)** — the GPU/GL build runs `meson setup` on
   `external/mesa`, needing the v3d port edits, which lived only as uncommitted edits in the host's
   `external/mesa` and were carried to the VM by an ad-hoc **rsync**. First attempt (bac7540): a
   tracked patch + bootstrap `git apply`. **The clean-VM run then exposed the real problem — the
   pinned SHA is GONE from upstream:** a fresh mesa clone lands on main ~1500 commits ahead,
   `git fetch <sha>` → `not our ref`, GitLab archive 404s for e8791b4; the quakespasm (4abb324) and
   vkquake (f4d923e) pins **also** report `not our ref` on GitHub. Pinning arbitrary upstream SHAs
   is therefore not reproducible — the validated commits survive only in our clones.
   **Final fix (mesa is the same case as the 13 modified siblings — treat externals as forks):**
   committed the mesa v3d port as a real commit (`external/mesa` branch `phoenix-v3d-port` =
   e8791b4 + the 9 edits, SHA `b234aa4` — the #81 clean pushable state), and bootstrap now clones
   all three externals from `$EXTERNAL_FORK_BASE` (default github.com/<user>; test override = host
   mirrors) at their exact SHAs — mesa needs no patch. Commit 2384b02; mesa-v3d-phoenix.patch kept
   only as a readable port-diff record. **Byte-identical to the HW-proven tree → no GPU
   re-validation needed** (this is what makes the fork legit vs a blind rebase). Validating in the
   full from-zero build now.

## Validation infrastructure (git-only, no rsync)

- VM→host SSH enabled (VM key added to host authorized_keys, marked TEMP — remove after).
- Host bare mirrors in `/home/houp/origins/<repo>.git` (coord + all 16 siblings + the 3 externals
  mesa/quakespasm/vkquake) = stand-in for github forks; captures the unpushed local commits (13/16
  siblings have local commits, incl. lwip=143, kernel=230, devices=305; mesa carries the
  phoenix-v3d-port commit). VM clones with `PHOENIX_FORK_BASE` **and** `EXTERNAL_FORK_BASE` both set
  to `ssh://houp@192.168.122.1/home/houp/origins`.
- Re-mirror a repo (esp. coord) after every new commit before a test run.

## Status

- Phase A (fresh-clone build on the current VM, into ~/phoenix-clean): **COMPLETE SUCCESS
  (BUILD_RC=0, 2026-07-03 23:40).** Fresh `git clone` of coord + 16 siblings + 3 externals +
  lib-lwip submodule, all from host mirrors at exact SHAs (no rsync) → bootstrap (toolchain built)
  → `rebuild-rpi4b-fast.sh --variant sd --with-showcase` → **835 MiB `rpi4b-sd-2part.img`**
  (artifacts/rpi4b/). Verified in the fresh-clone tree: Xphoenix (7.1 MB) + xterm (4.4 MB) [the
  fixed apps], rpi4-quake (GLQuake) staged, GPU archives libGL/libv3d/libquakespasm from the fork
  mesa. Two gaps found + fixed along the way: (a) stale /tmp/mesa-v3d-build reuse (cleared; a true
  wipe makes it moot) → GPU archives then linked clean (GPU_RC=0); (b) lwip lib-lwip submodule not
  initialized (b7bf9be). Everything validated EXCEPT the blank-OS apt layer (this VM had apt
  pre-installed) — that is Phase B's job.
- Phase B (final gate): **PASSED (BUILD_RC=0, 2026-07-04 00:34).** Wiped the VM to a fresh disk
  off the noble cloud image (blank Ubuntu 24.04), then from zero: apt install → uv → `git clone`
  coord + 16 siblings + 3 externals + lib-lwip from host mirrors (exact SHAs) → toolchain →
  GPU/GLQuake (fork mesa) → Xphoenix (7.1 MB) + xterm + xcalc + WindowMaker + nano/mc/dillo →
  **835 MiB `rpi4b-sd-2part.img`**. One blank-OS gap found + fixed mid-run: uv (installed by
  bootstrap into ~/.local/bin) wasn't on PATH for a same-shell `bootstrap && rebuild` → GPU phase
  "uv not found"; fixed in build-showcase-apps.sh (commit 8d09183). Reproducibility goal MET: a
  stranger with a blank Ubuntu + git can build the full SD image from our repo (once the forks are
  pushed to github).
- **Deliverable for manual RPi4 test:** the blank-OS-built image copied to the host at
  `/home/houp/phoenix-sd-images/rpi4b-sd-2part-cleanbuild-2026-07-04.img`
  (sha256 d53dbe07653d6f814f8e5d91d7d73fcbd2eb82c61aaa8635727ff2136be885c2, 875560960 bytes).

## Cleanup owed (revert after validation / before handoff)

- Host `~/.ssh/authorized_keys`: a TEMP line authorizes the buildtest VM key (marked with a dated
  comment) — remove when done.
- `/home/houp/origins/*.git` bare mirrors — throwaway test scaffolding; delete when done.
- Before github publish: push forks houp/{mesa@phoenix-v3d-port,quakespasm,vkquake} + the 13
  modified phoenix-rtos siblings, so the default `*_FORK_BASE=github.com/houp` resolves for outsiders.

## Note for eventual github publish (not tonight; NO push)

All 16 siblings clone from `$PHOENIX_FORK_BASE` (fork) first with upstream fallback. 13 have
local commits, so publishability requires those forks to exist+pushed on github (incl. the four
currently in UPSTREAM_ONLY_REPOS that carry local commits: corelibs, lwip, ports, posixsrv). The
mirror test faithfully simulates "all forks pushed". No bootstrap list change needed — only the
push.

---

## 2026-07-04 (pm): GLQuake flicker regression = unpinned RPi firmware

**Symptom:** clean-build (VM) SD boots fine but Quake flickers (HUD/enemies tear,
worse on heavy frames — monsters, explosions). X11 unaffected. Reported as a
regression from the prior smooth state; user's instinct was double/triple-buffering.

**Root cause (build-process bug, not the ported code):**
`scripts/bootstrap-linux-host.sh` set `PI_FW_REF="${PI_FW_REF:-master}"` — a moving
branch, despite a comment claiming it was pinned. The RPi firmware (`start4.elf`)
decides whether it grants plo's request for a **3x-tall virtual framebuffer** (the
backing for the GLQuake triple-buffer page-flip present path in
`tools/quakespasm-port/platform/pl_phoenix_glctx.c` + `tools/v3d-driver-port/
v3d_phoenix_winsys.c`).

- Host `.bootblobs` staged 2026-05-20 → firmware `VC_BUILD_ID ae9a8e` (from
  raspberrypi/firmware @ `41f4808`, 2026-05-18) → grants 3x → `virt_h=3240`.
- Clean VM build 2026-07-04 → `master` had advanced → firmware `VC_BUILD_ID f68405`
  → DENIES 3x → `virt_h=0`.

Evidence (winsys scanout-init UART line):
```
smooth  (60 logs, 06-21..07-02):  scanout init ... virt_h=3240 -> 3 buffer(s) TRIPLE-BUFFER+page-flip
flicker (07-04 VM build):         scanout init ... virt_h=0    -> 1 buffer(s) single (blit-resolve)
```
`virt_h=0` → `nbuf=1` → single-buffer render-in-place → tearing. Every other boot
artifact (plo source+provenance, config.txt, dtb, armstub, rpi4-fb allocation) was
byte-identical; `start4.elf`/`fixup4.dat` were the only difference.

**Fix (commit 2a11a9f):**
- Pin `PI_FW_REF=41f4808270a922f08fdd927edfeb60212800fe64`.
- Rewrite `stage_pi_firmware`: `--branch` rejects a SHA, so init + `fetch --depth 1
  origin <SHA>` + `checkout --detach FETCH_HEAD` (GitHub allows ref-reachable SHA
  fetch; validated end-to-end). Dropped `pull --ff-only` (it advanced past the pin).
- Also a reproducibility fix: clean builds are now firmware-deterministic.

**Confirmation SD (awaiting user boot):**
`phoenix-sd-images/rpi4b-sd-fwfix-2026-07-04.img` (flashed to /dev/sda) = the VM
flicker image with ONLY `start4.elf`+`fixup4.dat` swapped to `ae9a8e` + `rpi4-quake`
= the proven scanout-ON build (17766088). Firmware-isolated A/B: smooth →
firmware confirmed (also exonerates the unchanged plo binary + kernel8.img).
Verify objectively via the boot log: `scanout init ... virt_h=3240 -> TRIPLE-BUFFER`.
The mesa `create_flags=0` experiment was a wrong lever and has been reverted.
