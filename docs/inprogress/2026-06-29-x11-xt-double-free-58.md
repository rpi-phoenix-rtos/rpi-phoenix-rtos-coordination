# #58 — Xt/Xaw clients (xcalc, xedit) abort with "Double free detected"

Date: 2026-06-29
Status: ROOT CAUSE NARROWED to a libphoenix-specific runtime divergence (NOT an
upstream Xt/Xaw logic bug). Instrumented `xcalc-dbg` staged to pinpoint the exact
offending pointer on the next HW boot.

## Symptom (HW, from artifacts/rpi4-uart/rpi4b-uart-20260629-014730-xcalc-payoff.log)

```
startx xcalc
xlaunch: starting client[0]: /bin/xcalc (DISPLAY=:0)
Warning: Cannot convert string "calculator" to type Pixmap
Double free detected
xlaunch: client[0] exited (status=0x46)
```

- `status=0x46` = 70 = `EX_SOFTWARE` = the `_exit(EX_SOFTWARE)` in libphoenix's
  `free()` double-free detector (`sources/libphoenix/stdlib/malloc_dl.c:501-502`).
  So the death is genuinely `free()` being handed a chunk whose `CHUNK_CUSED` bit
  is clear.
- `"calculator"` is `XCalc.IconPixmap: calculator` from the staged app-defaults
  (`/srv/phoenix-rpi4-nfs/usr/share/X11/app-defaults/XCalc:5`). The bitmap file
  "calculator" is not present, so the String->Pixmap conversion fails — a normal,
  non-fatal Xt warning on any system. The death is the `free()` that follows.

## What was ruled OUT

1. **Not a port-side source modification.** Diffed the entire ported
   libXt-1.3.0 / libXaw-1.0.16 / libXmu-1.2.1 `src/` trees against the pristine
   release tarballs (sitting next to the sources). The *only* modified `.c` is
   `libXt-1.3.0/src/Alloc.c`, and the only change there is three diagnostic
   `fprintf("[XtMalloc] FAIL …")` lines on the alloc-failure path. The converter
   chain (`Convert.c`, Xaw `Converters.c`, `Pixmap.c`, Xmu `StrToBmap.c`,
   `GrayPixmap.c`) and xcalc itself are byte-for-byte upstream.

2. **Not the malloc(0)/XTMALLOC_BC mismatch (secondary latent bug, see below).**
   libphoenix `malloc(0)` returns NULL and `realloc(p,0)` does `free(p);return NULL`
   (glibc-incompatible), and libXt's `config.h` does NOT define
   `MALLOC_0_RETURNS_NULL`/`XTMALLOC_BC`, so `XtMalloc(0)`/`XtRealloc(p,0)` do not
   bump 0->1. BUT those paths die in `_XtAllocError` ("Cannot perform malloc/realloc",
   a *different* exit), not in the `free()` detector. The failing UART log contains
   **no** `[XtMalloc] FAIL` / `[XtRealloc] FAIL` markers — only `Double free detected`.
   So a 0-size alloc is not in the death chain. The flag is therefore NOT the fix
   (and worse, defining it would route freed chunks through `realloc()`, which —
   unlike `free()` — does not check `CHUNK_CUSED`, masking the abort into silent
   corruption). Recorded as a real but separate latent bug.

3. **Not an upstream logic double-free.** Decisive host reproduction: built the
   pristine `xcalc-1.1.2` natively (`gcc`, glibc) against the distro shared
   `libXaw7.so.7` (1.0.16, version-matched), `libXt.so.6` (1.2.1), `libXmu.so.6`
   (1.1.3) and ran it on a live `:0` with the bitmap absent (`XBMLANGPATH=/nonexistent`)
   AND the real `XCalc` app-defaults loaded so the **exact** `Cannot convert string
   "calculator" to type Pixmap` warning fires. Under `valgrind --error-exitcode=99`:

   ```
   Warning: Cannot convert string "calculator" to type Pixmap
   ...
   ERROR SUMMARY: 0 errors from 0 contexts
   ```

   xcalc reached `XtAppMainLoop` and ran cleanly until SIGTERM. valgrind catches
   true double-frees, free-of-non-heap, and overflows — **zero of any** on the
   identical code path. The upstream logic frees everything correctly.

   (Caveat for airtightness: distro libXt/libXmu are 1.2.1/1.1.3 vs the port's
   1.3.0/1.2.1; libXaw is the exact 1.0.16. The converter/resource code on this
   path is materially identical across those minor versions, and the warning
   reproduced byte-for-byte, so the clean valgrind result is trustworthy.)

## Conclusion: libphoenix-specific runtime divergence

Because the same pointers/frees are valgrind-clean under glibc but abort under
libphoenix, the doubly-managed pointer comes from a **libphoenix libc behavioral
difference**, invisible to a source diff and to host valgrind by construction.
Two candidate mechanisms, distinguished by the instrumented binary below:

- **Hypothesis A (favored):** a libphoenix libc function in the conversion /
  path-resolution chain (e.g. `XtResolvePathname`/`XtFindFile` -> getenv-family,
  `strdup`/`asprintf`, `setlocale`/`nl_langinfo`, wide-char/multibyte — several
  net-new in libphoenix per project memory) returns a **static / aliased /
  non-heap** buffer where glibc returns a fresh `malloc`'d one, and Xt then
  `XtFree()`s it. Freeing a non-heap pointer trips libphoenix's `CHUNK_CUSED`
  check immediately. Favored because libphoenix `free`/`realloc` are exercised by
  every program in the image (busybox, quake, the X server) without this abort,
  so a broad allocator bug is unlikely; a specific call-site divergence is.

- **Hypothesis B:** a pointer that WAS `malloc`'d is freed twice due to aliasing
  that only manifests under libphoenix.

## Instrumented binary (staged): /bin/xcalc-dbg

Source: `tools/x11-port/apps/xcalc-dbg-wrap.c`
Build:  `tools/x11-port/build-xcalc-dbg.sh`
Staged: `/srv/phoenix-rpi4-nfs/bin/xcalc-dbg` (+ `artifacts/x11/xcalc-dbg`)
ELF:    aarch64 static, 0 undefined symbols, `__wrap_free`/`__wrap_malloc` present.

It links xcalc with `-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free`
and keeps a set of live heap pointers + a ring of recently-freed ones. The
wrappers run BEFORE libphoenix `free()` can abort, so the offending free prints
one decisive line on the UART:

- `FREE-TRACE: free #N NON-HEAP / never-malloc'd ptr=0x… -> ... (hypothesis A)`
- `FREE-TRACE: free #N DOUBLE-FREE ptr=0x… (was malloc'd, already freed once) -> hypothesis B`

Validated on the host glibc build: the tracer emits **0** FREE-TRACE lines on a
clean run (no false positives) while the "calculator" warning still fires.

### HW run (main session)

```
export XFILESEARCHPATH=/usr/share/X11/app-defaults/%N
startx xcalc-dbg          # or launch xcalc-dbg under twm
```

Read the single `FREE-TRACE:` line printed just before `Double free detected`.
- Hypothesis A line -> the printed pointer + free ordinal #N pins the allocation;
  re-run the deterministic host `xcalc-dbg-host` build (same `--wrap`, with
  `backtrace()`/symbols) and read the Nth free's stack to get the file:line of the
  libphoenix function returning the static buffer. Fix is at that call site
  (port-side: copy-into-malloc, or stop `XtFree`ing it).
- Hypothesis B line -> the pointer is freed twice; find the second owner.

## Secondary latent bug to file (NOT this crash)

libphoenix `malloc(0)`/`realloc(p,0)` return NULL, but the cross-compiled
libXt/libXaw `config.h` lacks `MALLOC_0_RETURNS_NULL` (the
`xorg_cv_malloc0_returns_null` autoconf test cannot run under cross-compile and
defaulted to "non-null"). This is the same class as the existing
`patches/xorg-server-1.20.14-record-malloc0.patch`. Correct fix when addressed:
pass `xorg_cv_malloc0_returns_null=yes` to the libXt/libXaw `configure` in
`build-x11-phoenix.sh` (do NOT hand-`#define` it, and do NOT use it as the #58
fix). Tracked here so it is not lost.

## xedit

xedit (`build-xedit.sh`) links the same libXaw7/libXmu/libXt stack and is an
Xt/Xaw client, so it almost certainly hits the **same** libphoenix divergence
during widget creation. Expect the #58 root-cause fix to resolve xedit too; the
same `--wrap` tracer can be applied to xedit if it aborts at a different free.

## HW validation (one line)

`startx xcalc` -> the calculator window renders on HDMI with no "Double free
detected" abort (and the icon warning, if any, stays non-fatal).
