# Dillo/FLTK startup crash — #58-class bad-free (status 0x46)

Task #53. Dillo is fully built + staged (`/srv/phoenix-rpi4-nfs/bin/dillo`, 4.9MB
static aarch64-phoenix ELF) but crashes at startup on HW during FLTK UI bring-up,
before any browser window maps.

## The crash class

UART log of `startx dillo`:

```
xlaunch: starting client[0]: /bin/dillo (DISPLAY=:0)
paths: Creating directory '/root/.dillo/'
paths: Cannot open file '/root/.dillo/dillorc': ENOENT      (benign — dillo uses defaults)
dillo_dns_init: Here we go! (threaded)
Cookies: Created file: /root/.dillo/cookiesrc
paths: Cannot open file '/root/.dillo/hsts_preload': ENOENT
xlaunch: client[0] exited (status=0x46)     <-- CRASH
```

`status=0x46` = `EX_SOFTWARE`, the exit code **libphoenix's allocator** uses when
`free()` is handed a chunk whose `CHUNK_CUSED` bit is clear — i.e. a **non-heap /
.rodata / static / already-freed pointer**. glibc tolerated such frees silently;
libphoenix aborts immediately. This is the **same bug class as #58** (libX11 freed
a `.rodata` FontSet base-name literal in `omGeneric.c`/`XDefaultOMIF.c`).

The X stack itself works (xcalc, WindowMaker, xterm render on HW), so this is
Dillo/FLTK-specific, not an X-server problem.

## Where in startup

`src/dillo.cc:main()` ordering (last log line is `a_Hsts_init`, line 494):

```
494  a_Hsts_init(... PATHS_HSTS_PRELOAD)    <-- last log line before crash
496  a_UIcmd_init()
497  StyleEngine::init()
519  Fl_Window::default_xclass("dillo")
521  Fl::scheme(prefs.theme)
526  setColors()                            (Fl::set_color / fl_contrast / fl_lighter)
533  Fl::set_labeltype(FL_NORMAL_LABEL, ...)
536  Fl::set_labeltype(FL_FREE_LABELTYPE, ...)
538  checkPreferredFonts()                  -> FltkFont::fontExists -> initSystemFonts
                                            -> Fl::set_fonts("-*-iso10646-1") -> XListFonts (X font path)
541  FltkFont::get(prefs.font_sans_serif,0) (first Fl_Font_Descriptor -> XCreateUtf8FontStruct -> XCreateFontSet)
542  Fl::set_font(FL_HELVETICA, defaultFont)
549  a_UIcmd_browser_window_new(...)        (window create + map — never reached)
```

The bad free fires somewhere between 494 and 549. The font path (538/541) is the
prime #58-class suspect because it goes through libX11's OM/FontSet machinery.

## Source-reading results (suspects examined, NOT yet conclusive)

Built with `--disable-xft`, so the X11 (non-Xft) FLTK font path is in play:
`fl_set_fonts_x.cxx`, `fl_font_x.cxx`, `src/xutf8/utf8Wrap.c`, `fl_set_font.cxx`.

- **`fl_set_font.cxx` `Fl::set_font(FL_HELVETICA, defaultFont)` (line 34)** —
  examined as the leading hypothesis (remapping built-in slot 0, whose original
  `name` is the `.rodata` literal `"-*-helvetica-medium-r-normal--*"` from
  `built_in_table[]`). **CLEARED:** `Fl::set_font` never `free()`s `s->name`
  (comment at line 30-32: "string pointer is simply stored, not copied, must be
  static"). On the remap path it only `XFreeFontNames(s->xlist)` (guarded by
  `s->xlist != 0`; built-in slot 0's `xlist` is never set because `set_fonts`
  only ever assigns to `j = fl_free_font++`, never to built-in slots) and deletes
  `Fl_Font_Descriptor`s. So the literal is NOT freed here in 1.3.10.
- **`fl_set_fonts_x.cxx` `Fl::set_fonts` (XListFonts + qsort + XFreeFontNames)** —
  `XFreeFontNames(xlist)` (line 304) only runs when `used_xlist==0`, and `xlist`
  is a genuine `XListFonts` return. Internally consistent; not the #58 shape.
- **`src/xutf8/utf8Wrap.c` `XCreateUtf8FontStruct`/`load_fonts`** — the
  `font_name_list[]` entries are consistently malloc'd/strdup'd and freed; this
  path only runs at 541 (first `Fl_Font_Descriptor`), AFTER 538.
- **`fl_font_x.cxx`** — `put_font_size`/`find` use matched `strdup`/`free`.

libX11's #58 fix **is present** in the source used to build the staged X stack
(`tools/x11-port/src/libX11-1.8.7/modules/om/generic/omGeneric.c` line 1165/1594
carry the `owned = strdup(...)` ownership fix). So if the crash is in the libX11
FontSet path, the remaining bad-free is a **different site** than #58's `create_oc`
(answers the task's "is the #58 fix incomplete for FLTK's path" — the create_oc
ownership site is covered; any new hit is elsewhere).

**Conclusion of source reading:** the obvious literal-free sites are clean in
1.3.10. The first-hit bad-free site cannot be pinned from source alone (the
494→549 span includes `StyleEngine::init`, `Fl::scheme`, `setColors`, two
`set_labeltype`, plus the font calls). Instrumentation is required.

## dillo-dbg: the instrumented binary + capture recipe

Built by `tools/ports/build-dillo-dbg.sh`, which relinks the existing dillo
objects + static libs with the `--wrap` allocator tracer
`tools/ports/dillo-dbg-wrap.c` (ported from the proven #58 `xcalc-dbg-wrap.c`),
`-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free`. The shim is compiled
`-O0 -g -fno-omit-frame-pointer` so `__builtin_return_address(0)` (the direct
`free()` caller) is exact; deeper frames are best-effort (FLTK/libX11 are `-O2`).

**Thread-safe** (unlike the xcalc original): Dillo is threaded (`dillo_dns_init
... (threaded)` + dpid IPC), so the shim guards its `live[]`/`freed_ring[]`
bookkeeping with a static `PTHREAD_MUTEX_INITIALIZER`, held ONLY around the table
operations — never across `__real_*` or `fprintf` (which can re-enter
`__wrap_malloc`). This prevents a torn slot from a concurrent DNS-thread alloc
reading a legitimate free as `idx<0` → a FALSE `NON-HEAP` line pointing at
resolver/IPC code.

Staged:
- `/srv/phoenix-rpi4-nfs/bin/dillo-dbg` — runnable on HW
- `/home/houp/phoenix-rpi/artifacts/x11/dillo-dbg` — the **addr2line target**

Pre-flight (all PASS): static aarch64 ELF, `with debug_info, not stripped`,
0 undefined symbols, `__wrap_{malloc,calloc,realloc,free}` all present (T).

### Capture recipe (main session, owns the UART)

1. Boot and run `startx dillo-dbg` (instead of `dillo`).
2. The shim stays quiet on the ~thousands of normal frees. The crash is preceded
   by exactly one decisive line, one of:

   ```
   FREE-TRACE: free #<N> NON-HEAP / never-malloc'd ptr=<P> -> static/aliased buffer freed (hypothesis A)
   ```
   (the #58 shape — a .rodata/static/aliased pointer is being freed), or
   ```
   FREE-TRACE: free #<N> DOUBLE-FREE ptr=<P> (was malloc'd, already freed once) -> hypothesis B aliasing
   ```
   immediately followed by:
   ```
   FREE-BT: <a0> <a1> <a2> <a3> <a4> <a5>
   ```
   `<a0>` is the direct caller of `free()` — the call site to fix.

3. addr2line the backtrace against the artifact binary:

   ```
   /home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-addr2line \
     -f -C -e /home/houp/phoenix-rpi/artifacts/x11/dillo-dbg <a0> <a1> <a2> <a3> <a4> <a5>
   ```

   `-f` prints the function name for each frame. Note: dillo's own objects are
   `-O2` without `-g`, so addr2line gives the **function name** (e.g.
   `destroy_oc`, `XFreeStringList`, `Fl::set_font`) but `??:?` for the line; the
   FLTK/libX11 libs likewise resolve to function names. The function name + the
   `free #N` ordinal + ptr is enough to pin the site; cross-reference with the
   source map above.

### Reading the outcome

- **NO `FREE-TRACE` line before the `0x46` exit** = the crash is NOT an allocator
  bad-free. It's then an `abort()`/assert path (C++ exceptions are already ruled
  out — dillo builds `-fno-exceptions`). Pivot off the free hypothesis: look for
  an `assert`/`Fl::fatal`/`X` I/O-error handler in the same 494→549 window.
- **MULTIPLE `FREE-TRACE` lines** (possible once threaded, despite the lock, if a
  genuinely benign aliasing exists elsewhere): the REAL one is whichever FREE-BT
  frame 0/1 lands in the **font / X / FLTK** path (per the source map above) and
  whose `free #N` is the LAST before the process exits. Don't fixate on the first
  line printed.

## Fix

NOT yet applied — the first-hit site needs the FREE-BT capture to confirm. Likely
shapes once the function is named:

- If `<a0>`/`<a1>` lands in libX11 `destroy_oc`/`omGeneric.c` or
  `XDefaultOMIF.c` / `XFreeStringList`: a FontSet ownership site analogous to but
  distinct from #58's `create_oc` — extend the
  `tools/x11-port/patches/libX11-1.8.7-phoenix-fontset-basename-ownership-58.patch`
  with the new site, rebuild libX11, relink dillo, restage.
- If it lands in FLTK (`fl_set_font.cxx`, `fl_set_fonts_x.cxx`, `utf8Wrap.c`):
  add an FLTK port-side patch under `tools/ports/` (new
  `tools/x11-port/patches/fltk-1.3.10-phoenix-*.patch` or a sed in
  `build-fltk.sh`), rebuild FLTK, relink dillo.
- If it lands in Dillo's own code: patch in `build-dillo.sh` (sed) like the
  existing `strndup`/`connect_ret_size` fixes.

After the fix: rebuild the affected lib, relink dillo (0 undefined), restage
`/srv/phoenix-rpi4-nfs/bin/dillo`, and commit (coord repo).
