# Overnight autonomous run — RESULTS (2026-06-26)

Honest results of the multi-agent overnight push (5 parallel host-side subagents + serial HW
validation by the orchestrator, grabber working). GL Quake flagship is restored as the bundled
default (boots straight into it). Every claim below is grab- or UART-evidenced.

## ✅ DONE + HW-validated
- **JWM window manager (#35 first half)** — `startx jwm` renders a real desktop: bottom taskbar
  (menu button + workspace pager + task list + live clock) + a motif-decorated xeyes window (title
  + min/max/close buttons). Grab 20260625-221314-jwm-polish-final.png. Commits 2fa5b2a/7526d48/2fa7c5d.
- **uname (#37)** — psh `uname` builtin (phoenix-rtos-utils 83fa2f7). HW: `uname`→"Phoenix-RTOS",
  `uname -a`→"Phoenix-RTOS … 3.3.1 ######## +0 aarch64a72". (Cosmetic, NOT the command: nodename
  control-char + VERSION="########" are kernel build-stamp macros, separately fixable.)
- **Quake torch flame (#28)** — animated alias-model garbage FIXED (snap-to-pose, quakespasm 90da546).
  HW grab shows the wall torch as a recognizable flame; GL Quake demos play, render healthy. Validated
  on a demo frame (not the user's exact scene — worth a final eyeball there).
- **#31 logging** — verified already complete (rpi4-klogd + logread + RPI4_LOG_TO_FILE); closed.

## ◑ Code done + committed, HW partially validated / follow-up needed
- **Quake LAN multiplayer (#26)** — Datagram/UDP drivers registered; the prior fatal FIONREAD hang
  fixed (recvfrom MSG_PEEK; quakespasm 4abb324 + coord fc4f0a8). HW: GL Quake still renders the level
  + demos play — the per-frame-poll regression caveat is REFUTED. Full 2-player needs a 2nd node.
- **GENET cacheable RX, default-OFF (#11)** — streaming-DMA RX (cacheable pool + per-frame dc ivac) +
  integrity bench implemented (lwip 476825b/89cc6e7). Default build unchanged. NOT benched on HW yet
  (needs `GENET_RX_CACHEABLE=1` rebuild + a host pattern-server on the gateway:5099; recipe in the
  task). Marked NEEDS-CAREFUL-HW-REVIEW.

## ⚠ Builds, runtime bug localized — NOT working yet (precise follow-up leads)
- **xterm (#36)** — builds (static aarch64, staged) + reaches display-open → vt100 widget → pty-open
  OK → spawn, then crashes. ROOT CAUSE found: Phoenix posixsrv EINVALs `ioctl(FIONBIO)` on the pty →
  xterm `SysError(ERROR_FIONBIO)` → crash. A FIONBIO guard was committed (xterm-396-phoenix.patch) +
  rebuilt, but HW shows it STILL crashes (Instruction-Abort storm) — the FIONBIO guard is necessary
  but insufficient; there's a further failure after it. NEXT: rebuild the -DPHX_DIAG binary, grab,
  find the next failing step past FIONBIO.
- **Window Maker (#35 stretch)** — builds (the whole expat→fontconfig→libXft→wraster→WINGs stack
  ported — major), staged. Two config bugs fixed by me (fonts.conf XML double-hyphen 14688f2; GNUstep
  dir). Diagnostic grab LOCALIZES the stall precisely: last marker = `wScreenInit: before
  WMCreateScreenWithRContext (loads WINGs system font)` — it stalls in the WINGs **system-font load**
  (Xft font-open of DejaVu fails/hangs on-target despite valid fonts.conf). NEXT: fix the on-target
  Xft/DejaVu match (fontconfig cache? TTF not found? Xft NULL→hang) in WMCreateScreenWithRContext.
- **vkQuake (#29)** — full init + all 41 shaders + render resources + `vkvid: present 1`, then HANGS
  (no present 2, no next SCR_UpdateScreen, no crash); HDMI stays black (no content, not even the
  magenta clear). Significant progress but still NO visible content. NEXT: debug the post-first-present
  hang (the frame loop blocks after present 1 — likely a fence/acquire/present wait that never returns).

## ⏸ Deferred (documented, not attempted unattended)
- **#12 reorg + full phoenix-rtos-ports integration** — each new port (jwm/xterm/wmaker) ships a
  port.def.sh recipe DRAFT + a README noting the blocker (the X11 lib stack lives in tools/x11-port,
  not as ports), feeding this reorg. The mechanical migration is build-critical → attended.
- **#32 real rootfs**, **#33 USB #121 instrumentation** — brick-risk / statistically-sensitive →
  attended.

## Notes
- Repos clean; flagship buildable + bundled (GL Quake). One multi-agent `git add` race produced a
  cross-attributed coord commit (ebbe5d0) — files preserved, harmless, flagged for tidy-up.
- The 5 subagents hit a few API stalls mid-run; each was checked for a clean footprint and
  re-launched/resumed. The xterm agent stalled twice but left its FIONBIO diagnosis in the patch.
