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

- Phase A (fresh-clone build on the current VM, into ~/phoenix-clean): first run CONFIRMED
  clone-from-mirror of coord + 16 siblings, then surfaced the vanished-external-SHA problem (killed;
  it was building against the wrong mesa). Fixes committed (2384b02 + mesa fork). Re-running with
  `EXTERNAL_FORK_BASE` = host mirrors.
- Phase B (final gate): wipe VM (disk.qcow2 recreate backed by noble base) → blank Ubuntu →
  `git clone` coord from mirror → `PHOENIX_FORK_BASE=… EXTERNAL_FORK_BASE=… bootstrap` →
  `rebuild-rpi4b-fast.sh --variant sd --with-showcase`.

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
