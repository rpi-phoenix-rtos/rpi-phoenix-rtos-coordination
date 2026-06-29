# #58 — Xt/Xaw clients (xcalc, xedit) abort with "Double free detected"

Date: 2026-06-29
Status: ROOT-CAUSED + FIXED (host-side; awaiting HW re-validation). The tracer
pinned the freed pointer to a `.rodata` string literal; root cause is a latent
**upstream libX11 ownership bug** in the default Output-Method fontset path
(`XDefaultOMIF.c`), triggered by Phoenix's missing-fonts + default-OM
environment. Fix: make `base_name_list` an owned heap copy in `create_oc` before
any failure path can `Xfree` it. Clients relinked + staged.

## RESOLUTION (the answer)

### Tracer result (HW)

```
FREE-TRACE: free #319 NON-HEAP / never-malloc'd ptr=0x5af0e8
            -> static/aliased buffer freed (hypothesis A)
```

`objdump` on `/bin/xcalc-dbg`: `0x5af0e8` = the `.rodata` literal
`"-*-*-*-R-*-*-*-120-*-*-*-*,*"` — libXt's hardcoded fallback default-FontSet
XLFD base-name pattern. So a string literal is being `Xfree`'d. (The preceding
"Cannot convert string \"calculator\" to type Pixmap" warning was unrelated and
benign; the death is in FontSet handling, which runs for xcalc's default font
resource.)

### Exact mechanism (libX11 1.8.7, src/xlibi18n/XDefaultOMIF.c)

1. libXt `XtCvtStringToFontSet` (Converters.c:1040) calls
   `XCreateFontSet(display, "-*-*-*-R-*-*-*-120-*-*-*-*,*", ...)` with the
   literal as the base-name argument (the earlier user/default fontsets having
   failed to load on Phoenix).
2. `XCreateFontSet` -> default OM -> `create_oc` -> `_XlcSetValues` stores the
   `base_name_list` resource. That resource is `XlcString` == `sizeof(XPointer)`,
   so `_XlcCopyFromArg` (lcWrap.c:486-487) does `*dst = (XPointer)src` — a **pure
   pointer copy** (verified; identical on every platform). `oc->core.base_name_list`
   now ALIASES the caller's literal.
3. `create_oc` calls `create_fontset` -> `parse_fontname`. On Phoenix no font
   matches the pattern, so the chain fails (`create_fontset` returns False —
   either `init_fontset` fails before `parse_fontname` runs, or `parse_fontname`
   returns <=0). Upstream only takes its OWN heap copy of `base_name_list` at the
   `found:` label deep inside `parse_fontname` (line 442) — NOT reached on these
   failure paths.
4. `create_oc` `goto err` -> `destroy_oc` -> `Xfree(oc->core.base_name_list)`
   (line 525) frees the **literal**. Freeing a `.rodata` pointer is undefined on
   any libc; libphoenix's allocator catches it immediately
   (`malloc_dl.c` CHUNK_CUSED check -> `_exit(EX_SOFTWARE)` = status 0x46).

This is a **latent upstream bug**, NOT a libphoenix divergence: `_XlcCopyFromArg`
pointer-copies on glibc too. Real-locale hosts never hit it because they use the
generic/Uniconv OM (not `XDefaultOMIF`) and have fonts, so `XCreateFontSet`
succeeds and the `parse_fontname:442` copy is taken. (This also explains why the
earlier host valgrind run below was clean — it exercised a different OM path.)
Worth reporting upstream to xorg/libX11.

### Fix (libX11 src/xlibi18n/XDefaultOMIF.c)

In `create_oc`, right after `_XlcSetValues` + the NULL check, take an owned heap
copy of `base_name_list` so EVERY subsequent failure path frees a real
allocation:

```c
    if (oc->core.base_name_list == NULL)
        goto err;
    {
        char *owned_base = strdup(oc->core.base_name_list);
        /* assign before the NULL check so the err-path destroy_oc frees
         * NULL (safe), never the borrowed literal again */
        oc->core.base_name_list = owned_base;
        if (owned_base == NULL)
            goto err;
    }
```

and remove the now-redundant `strdup` at the `found:` label in `parse_fontname`
(re-copying there would leak the create_oc copy). Net ownership: `base_name_list`
is heap-owned from creation, freed exactly once in `destroy_oc`/`close_om` — no
double-free, no leak, on success and on every failure path.

Tracked as `tools/x11-port/patches/libX11-1.8.7-phoenix-fontset-basename-ownership-58.patch`
(the `src/` tree is gitignored; `build-x11-phoenix.sh` auto-applies
`patches/libX11-1.8.7*.patch` via `apply_patches`, so a clean rebuild reproduces
the fix). Upstreamable to xorg/libX11 as-is.

Build: `make` in `src/libX11-1.8.7`, install to `/tmp/x11-phoenix`, relink the
clients (they link libX11 statically). 0 undefined symbols. Compiles with no new
warnings. Xphoenix does NOT link libX11 (`nm` = 0 fontset symbols) — no server
relink needed.

Staged (all 02:45):
- `/srv/phoenix-rpi4-nfs/bin/xcalc`
- `/srv/phoenix-rpi4-nfs/bin/xcalc-dbg`  (tracer kept; should now print 0 FREE-TRACE lines)
- `/srv/phoenix-rpi4-nfs/bin/xedit`      (same libX11 path — fixed by the same change)

### HW re-validation

Success criterion for THIS fix = **no "Double free detected" abort**; the client
reaches `XtAppMainLoop`. (This fix stops the crash; it does NOT make the fontset
load — that still fails on Phoenix, which is exactly why the literal-fallback
path ran. After the fix `XtCvtStringToFontSet` cleanly returns False with the
benign "Unable to load any usable fontset" warning instead of crashing.)

Recommended order:
1. `startx xcalc-dbg` — the clean discriminator. If it reaches the event loop
   with **0 `FREE-TRACE:` lines**, the double-free is definitively fixed,
   independent of rendering.
2. `startx xcalc` — expected to render the calculator on HDMI. xcalc/Xaw use
   `XtRFontStruct` for labels when `international=False` (disk fonts load per
   xfontprobe), so the fontset failure should be non-fatal and the window draws.
   If xcalc reaches the loop but does NOT draw, that is a SEPARATE fontset-
   fallback/rendering issue, NOT a regression of this double-free fix — do not
   conflate them.

xedit (`xedit /etc/hostname`) links the same libX11 and is fixed by the same
change.

---

## Investigation history (how we got here)

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

   Version-gap closure (the load-bearing check): the distro libXt used in the
   valgrind run is 1.2.1, but the port ships 1.3.0. Diffed pristine libXt-1.2.1
   vs 1.3.0 for every file on the death path (Convert.c, Resources.c, Create.c,
   Destroy.c, Alloc.c, GetValues.c, Initialize.c). The only changes are cosmetic:
   `memmove`->`memcpy`, `__XtMalloc(n*size)`->`XtMallocArray(n,size)` (same result,
   overflow-checked), `XtRealloc`->`XtReallocArray`, and a NEW `XtReallocArray`
   helper. NO change to the conversion cache (`CacheEnter`/`FreeCacheRec`),
   `XtConvertAndStore`, or the resource-default free logic. So the
   conversion-failure path is functionally identical in 1.2.1 and 1.3.0, and the
   clean 1.2.1 valgrind result validly establishes 1.3.0 is clean on this path
   too. libXaw is the exact version-matched 1.0.16. (Also built pristine
   libXt-1.3.0 natively to confirm it compiles clean; full static relink was not
   needed once the diff proved path-equivalence.)

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

Tracer caveats (the offending pointer still prints; only the A/B tag can be
imperfect):
- On the host the X libs are SHARED, so `--wrap` only caught xcalc's own
  malloc/free; the Pi build is STATIC, so the wrappers also see all libXt/libXaw
  internal traffic (far more). The set is 16384 slots (load < ~0.06 even at the
  measured ~1000 live host blocks; library traffic raises this but headroom is
  large). If the `table full` line ever prints, raise `SLOTS`.
- `freed_ring` is 4096 deep: if a double-freed pointer's first free was >4096
  frees earlier, it wraps out of the ring and the line is tagged A instead of B.
  The pointer + ordinal stay correct; only the hypothesis label may flip. Treat a
  NON-HEAP line whose pointer looks heap-shaped (not a small constant / .rodata
  address) as possibly-B.

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
