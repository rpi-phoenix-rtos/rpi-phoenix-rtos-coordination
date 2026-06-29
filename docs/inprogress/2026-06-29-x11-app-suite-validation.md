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

## Also working — fixed during this session

| App | Status |
|-----|--------|
| **Midnight Commander** (`mc`, ncurses TUI) | ✅ Full two-panel TUI renders on the teken fbcon + is **interactive** (executes typed commands via fork+exec). Was crashing (#55) on the libphoenix `vasprintf` heap overflow (its ~1088-byte `--colors` `g_strdup_printf` > the buggy fixed-1024 buffer); fixed in libphoenix `stdio/asprintf.c`. |
| **Dillo** (FLTK browser) | ✅ Renders the `about:splash` HTML page (toolbar, address bar, hyperlink). The status=0x46 (#59) was the **same** `vasprintf` overflow — fixed by the **same one-line libphoenix change** + a dillo relink. |

The `vasprintf` overflow (`malloc(1024)` + unbounded `vsprintf`) was a latent core bug
hitting any `asprintf`/`g_strdup_printf` >1024 bytes — root-caused via the redzone
allocator (`mc-guard`), one fix unblocked both mc and Dillo.

## Not yet working

| App | Status |
|-----|--------|
| **xedit** | Past the #58 double-free, but Data-Aborts (EL0) on `"Unable to load any usable fontset"` — a **separate i18n issue**: `XCreateFontSet` finds no usable fontset for Phoenix's minimal (C/POSIX) locale, and xedit (unlike xcalc, which falls back to `XFontStruct`) dereferences the failed result. Needs the locale→charset→font matching traced. Not the #58 bug (that's fixed). |

## #43 (exec-from-NFS ENOMEM) — refined

Observed once: `proc: exec '/bin/mc-guard' failed (err=-12)` on a 5.0 MB binary, while
4.9 MB dillo exec'd fine. But it did **not** recur across 12+ subsequent rapid large
execs (incl. 8 back-to-back) — so it's a **rare transient**, not a deterministic
size threshold. The kernel exec diag (`DIAG-NOMEM`, `TEMP-NOMEM-DIAG (#43)`) is now
armed at every exec mmap site (header, per-segment file/bss, stack) so the next
occurrence names the failing allocation. Retry succeeds, consistent with transient
kernel-map fragmentation.

## Recurring theme: libphoenix's strict `free()`

Both #58 (fixed) and #59 (open) are the same shape: ported code frees a non-heap /
static / aliased pointer that glibc happened to tolerate (or never hit on its code
path), and libphoenix's allocator correctly aborts (`_exit(EX_SOFTWARE)` = 0x46). The
proven diagnosis tool is the `--wrap` allocator tracer (`tools/x11-port/apps/
xcalc-dbg-wrap.c`) that prints the bad pointer + a `__builtin_return_address`
backtrace, which `addr2line` resolves to the exact free site. This pinned #58 in two
boots and is being reused for #59 and mc (#55).
