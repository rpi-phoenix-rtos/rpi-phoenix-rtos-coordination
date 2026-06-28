# X11 polish session — colour, responsiveness (IPC), rename (2026-06-28)

Overnight deep-dive on X11 experience quality. Headline: the sluggishness was an
**IPC latency** bug (poll() not readiness-woken), now fixed; colours are fully
correct; wmaker background-save root-caused + the core fix landed.

## ✅ Colour — SOLVED + HW-verified (committed)
- Root cause: the Pi firmware framebuffer is **RGB** byte order (plo sets mailbox
  `SET_PIXEL_ORDER=1`, `plo/hal/aarch64/generic/video.c`), but the fbdev DDX
  hardcoded **BGRX** visual masks → R/B swapped on every blit (deep blue rendered
  orange; greys fine since R=G=B).
- Fix: RGBX masks in `fbdevScreenInitialize` (coord `bc5acfb`).
- Verified: `xcolortest` (new probe, coord `335fe21`) renders all 11 colours true
  on HDMI (red/green/blue/yellow/magenta/cyan/orange/pink/white/grey/black) and
  its pixel-value log confirms correct RGBX packing. xbill should look right now.

## ✅ Responsiveness — root-caused + FIXED (interim + proper, committed, validated)
- Decisive symptom (user): **cursor moves smoothly, but app reactions to clicks
  are laggy.** Cursor is server-local (16ms input timer); app reactions need a
  client↔server **round-trip**. So the lag is IPC, not rendering.
- Root cause: `posix_poll` was a poll-and-sleep loop (NOT readiness-woken) with a
  **100ms** re-check interval. libxcb waits every X reply with `poll(-1)`, so each
  synchronous round-trip cost up to 100ms → the crawl. (Same systemic gap that
  made NFS 20× slow.) select() routes through poll() too (libphoenix select.c).
- **Interim** (kernel `01715f09`): re-check interval 100ms → 2ms (50× latency
  cut). Revert point / safety net.
- **Proper** (kernel `7a52147c`): readiness-woken poll()/select() for AF_UNIX —
  the transport every local X client uses. A global `unix_common.pollQueue` that
  every unix-socket state change `proc_threadBroadcast()`s; `posix_poll` blocks on
  it (interruptible) with the interval (raised back to 20ms) as a fallback for
  non-AF_UNIX fds + safety. Design: one global queue + the battle-tested
  `proc_threadWait` (a poller is a single waiter — sidesteps multi-waiter
  registration); lost-wakeup-safe (the `wakeupPending` sentinel); hang-safe (the
  timeout always fires); deadlock-safe (broadcast under the same per-socket
  spinlock that already guards the recv-queue wakeup). `proc_threadBroadcast`
  (wake-ALL) is essential: the X server + every client block on the one queue.
- HW-validated: boots clean to psh, NFS+FSHEALTH healthy (lwip poll fallback
  path), `startx wmaker` comes up + renders, no hang/fault. **The multi-client
  responsiveness gain is for the user to feel** (scripted boots can't measure
  "smoothness"); a scripted boot can only confirm no regression/hang — it did.

## ◑ wmaker "can't change background" — core fix landed; deploy steps remain
- Root cause 1 (SAVE): wmaker writes defaults to a temp file then `rename()`s it
  onto the existing `~/GNUstep/Defaults/WindowMaker`. libphoenix `rename()` is
  emulated as `link(old,new)+unlink(old)`, and `link()` can't overwrite an
  existing name → `-EEXIST` → save fails (`WINGs error: rename(...) failed`).
- Fix (libphoenix `c01f7a7`): `rename()` now drops an existing destination on
  EEXIST and retries (POSIX replace semantics; EEXIST-gated so it never destroys
  the dest on other errors). General improvement for all save-via-rename apps.
- **REMAINING to actually deliver bg-change:**
  1. **Rebuild wmaker** against the fixed libphoenix (`build-wmaker.sh`) — the
     staged wmaker is a static binary with the old rename baked in. NOT done
     tonight to avoid replacing the working staged wmaker with an unvalidated
     build while unattended. Rebuild + boot-test bg-save.
  2. Root cause 2 (MENU): `appearance.menu`/`background.menu` start with
     `#include "wmmacros"` (cpp directives); wmaker parses them as proplist domain
     files → "Comments are not allowed" → the appearance/background submenus are
     broken. Fix: pre-process the `.menu` files through `cpp -P` at stage time
     (wmaker's normal install does this), or stage proplist-format menus. Until
     then, set bg via `wmsetbg`/WPrefs rather than the menu.
- The GNUstep dir-create error is already fixed (host-side `mkdir -p
  /srv/phoenix-rpi4-nfs/root/GNUstep/Defaults`).

## Cosmetic / low-priority (noted, not fixed)
- wmaker `swback.png`/`swtile.png` "could not load" — Alt-Tab switch-panel art;
  the PNGs exist but wraster rejects them (likely a PNG-support gap in the ported
  wrlib). Cosmetic.
- `xterm: fatal IO error 11 (EAGAIN)` — most likely teardown/AF_UNIX edge; xterm
  works. Not chased.
- locale warnings ("locale not supported by Xlib, locale set to C") — cosmetic.

## Reliability note (hit during testing, relevant to X experience)
3 of tonight's test boots hit *intermittent* infra failures that also block X
launches: NFS-mount-fail (`nfs-fs: FAIL mount`) and exec-`-ENOMEM` on startx (the
#43 residual — notably with NO `DIAG-NOMEM` marker firing, which points at the one
allocation left unmarked: the **eager segment maps** in `process_load64`). Also a
useful #43 data point captured: `mem` showed ~3.9GB free / 191 of 327168 map
entries at a failing moment → the residual ENOMEM is a **specific allocation
failure, not exhaustion** (matches the segment-population hypothesis).

## Next session
1. Rebuild wmaker → boot-test background save (deploys the rename fix).
2. Pre-process the wmaker `.menu` files (cpp) → working appearance/background menus.
3. Confirm multi-client responsiveness with the user (the parallel X-app demo).
4. (Bigger) extend readiness-woken poll to lwip/network sockets (cross-process
   notify) so network apps get the same snappiness AF_UNIX now has.
