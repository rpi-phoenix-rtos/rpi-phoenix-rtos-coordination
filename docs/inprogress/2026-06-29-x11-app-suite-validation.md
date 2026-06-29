# X11 application-suite validation on HW (2026-06-29)

After the #56 (named-font loading) and #58 (Xt/Xaw FontSet double-free) fixes landed,
the X11 stack was exercised across several real applications on the Pi 4 (NFS root,
fbdev DDX, `startx <client>` via `pl_phoenix_xlaunch`). HDMI snapshots in
`artifacts/hdmi/*`.

## Working — HW-confirmed rendering

| App | Toolkit | Result |
|-----|---------|--------|
| **xcalc** | Xaw (Athena) | ✅ Full scientific calculator renders — display + entire button grid in crisp bitmap fonts. First complete Athena-widget app on Phoenix. (`startx xcalc`) |
| **WindowMaker** (`wmaker`) | WINGs | ✅ Desktop renders — signature gradient root, Workspace clip, dock app-icon, cursor. The flagship WM (#35). (`startx wmaker`) |
| **xterm + twm + /bin/sh** | Xt + raw Xlib | ✅ `startx term` brings up twm managing a decorated xterm running a live BusyBox ash shell at the `/ #` prompt. Terminal emulator (#36) + WM (#35) + shell + fonts together. |
| **xeyes** | raw Xlib | ✅ (prior) renders + tracks the mouse. |

These validate, end-to-end: the X server (Xphoenix kdrive fbdev DDX), libX11/libXt/
libXaw/libXmu/libICE/libSM, named bitmap font loading (#56), the FontSet path (#58),
the launcher's multi-client session modes (`desktop`, `term`), and shell exec from X.

## Not yet working

| App | Status |
|-----|--------|
| **Dillo** (FLTK) | ❌ Launches (DNS + cookies init OK) but exits **status=0x46** during FLTK UI creation, before mapping a window — the libphoenix strict-allocator bad-free abort, same class as #58. Tracked as #59; subagent instrumenting `dillo-dbg`. Suspect FLTK's X font path freeing a static/aliased string (analogous to the #58 FontSet literal). |
| **xedit** with a file arg | `startx xedit /etc/passwd` produced no launch — the `startx` convenience mode only honours `argv[1]` (the extra path is ignored / mis-handled). xedit itself shares the #58-fixed Athena path (xcalc proves it); bare `startx xedit` is the path to retry. Minor launcher-ergonomics item. |

## Recurring theme: libphoenix's strict `free()`

Both #58 (fixed) and #59 (open) are the same shape: ported code frees a non-heap /
static / aliased pointer that glibc happened to tolerate (or never hit on its code
path), and libphoenix's allocator correctly aborts (`_exit(EX_SOFTWARE)` = 0x46). The
proven diagnosis tool is the `--wrap` allocator tracer (`tools/x11-port/apps/
xcalc-dbg-wrap.c`) that prints the bad pointer + a `__builtin_return_address`
backtrace, which `addr2line` resolves to the exact free site. This pinned #58 in two
boots and is being reused for #59 and mc (#55).
