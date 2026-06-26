# Overnight autonomous run — 2026-06-26 (plan + dispositions)

You're away a few hours; HDMI grabber is FIXED so I can do full HW validation. Working all pending
tasks via parallel subagents (host-side builds) while I serialize the HW validation (one Pi at a time)
and do the small tasks myself. New-software ports follow the phoenix-rtos-ports recipe structure where
feasible (see the per-port notes). I won't touch the GL Quake flagship's correctness, the parked WIP
(sdcard #154, wifi #91, gldraw, vkquake-stub), or break the buildable flagship.

## ✅ Done by me this session
- **JWM window manager — DONE + grab-proven + polished.** `startx jwm` shows a real desktop (taskbar
  + clock + workspace pager + menu) with a motif-decorated xeyes window (title + min/max/close
  buttons). Commits 2fa5b2a/7526d48/2fa7c5d. (task #35 first half)
- **uname (#37) — code committed** (phoenix-rtos-utils 83fa2f7): psh `uname` builtin (-a/-s/-n/-r/-v/-m),
  uses kernel sys_uname + libphoenix uname(2). HW-verify batched (needs --scope core).
- **#31 logging — verified already complete** (rpi4-klogd + logread + RPI4_LOG_TO_FILE), marked done.

## 🚀 In flight — 5 parallel background subagents (host-side; partitioned by repo, no conflicts)
1. **xterm (#36 + unblocks #30 keyboard)** — terminal emulator, core fonts, PTY via posixsrv, shell
   /nfstest/bin/sh. tools/x11-port + a phoenix-rtos-ports recipe draft.
2. **Window Maker (#35 second half)** — heaviest: builds expat→fontconfig→libXft→libwraster→WINGs→
   wmaker (Xft/fontconfig are new; JWM/twm/xterm use core fonts). Partial+documented OK.
3. **Quake torch fix (#28) + LAN multiplayer net (#26)** — external/quakespasm. #28 = animated
   alias-model lerp (static models fine, animated broken); fix true lerp or snap-to-pose fallback.
   #26 = register Datagram/UDP drivers on lwip sockets.
4. **vkQuake (#29)** — push from crash-free-init toward visible content (frame loop + 2D draw +
   no-WSI present landing sustained frames on fb0). external/vkquake + mesa. Honest partial OK.
5. **GENET cacheable RX (#11)** — DEFAULT-OFF streaming-DMA RX (cacheable pool + per-frame dc ivac) +
   integrity bench. sources/phoenix-rtos-lwip. Default build stays unchanged.

## 🧭 My role while they build
Serialize HW validation as artifacts land (the Pi/UART/grabber is a single exclusive resource):
- Batch 1 (boot-to-psh, current image): xterm grab + keyboard test; wmaker grab.
- Batch 2 (Quake-bundled): torch-fix grab (same torch scene) + multiplayer init line.
- Batch 3 (vkQuake-bundled): vkQuake content grab.
- Batch 4 (netboot + GENET flag): cacheable-RX throughput + integrity bench.
- Then: --scope core build covering uname + any committed core changes → `uname -a` grab.
Restore the GL Quake flagship default at the end.

## ⏸ Deferred (with reasoning) — for your call
- **#12 reorg (apps→ports, rpi4-quake out of devices/misc) + full phoenix-rtos-ports integration of
  the X11 ports.** The X11 lib stack lives in tools/x11-port, NOT as ports recipes, so fully driving
  xterm/wmaker through the ports build harness is entangled with this larger reorg — build-critical
  and a one-way door you previously gated. The new ports each ship a *recipe draft* + a note on what
  blocks full integration; the mechanical migration is best done attended. Will write a concrete
  consolidation plan.
- **#32 real rootfs ("/" = real fs, drop /nfstest indirection).** Large boot-order change (the
  takeover/dummyfs dance); brick-risk; better attended.
- **#33 USB #121 free-list instrumentation.** USB daemon internals are statistically-sensitive +
  invisible in a single boot (feedback_unattended_scoping flags these attended). Holding.

## Constraints honored
One Pi cycle at a time; subagents commit incrementally in their own repos; flagship stays buildable;
core builds deferred until the libphoenix-touching agents finish (avoid building a half-edited tree).
