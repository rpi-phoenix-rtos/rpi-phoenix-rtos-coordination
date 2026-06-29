# xedit Lisp-interpreter startup crash (CRASH #2) — root cause + fix

Date: 2026-06-29
Scope: `tools/x11-port/src/xedit-1.2.2` (xedit's bundled Common-Lisp interpreter),
`tools/x11-port/build-xedit.sh`, `tools/x11-port/patches/`. Host-side only; HW
validation is run by the main session.

## Summary

xedit (X11 Athena text editor) Data-Aborted at startup inside its embedded
Common-Lisp interpreter (`LispBegin`). Root cause is an **upstream xedit bug** in
the error-unwind path (`LispDestroy`) that only triggers when the interpreter's
module `.lsp` files are not resolvable at the compiled-in `LISPDIR` — which was
the case on the Pi because (a) `LISPDIR` was baked as the host build prefix
`/tmp/x11-phoenix/lib/X11/xedit/lisp` (a path that does not exist on the Pi) and
(b) the `.lsp` files were never staged onto the NFS rootfs.

Two complementary fixes, both validated:
1. **Primary (staging + path):** build xedit with `--with-lispdir=/usr/lib/X11/xedit/lisp`
   and stage the module `.lsp` tree there on the NFS rootfs, so `(require "lisp")`
   succeeds at startup and the buggy error path never runs.
2. **Defensive (one-line guard):** patch the bundled `lisp/lisp.c` so a `require`
   failure degrades gracefully instead of crashing. Cheap insurance against any
   future LISPDIR/NFS file-resolution hiccup.

## Root cause (fully pinned, host repro + GDB watchpoint)

Mechanism, in order:

1. `LispBegin()` (lisp/lisp.c:5372) runs `EXECUTE("(require \"lisp\")")` to load
   `lisp.lsp` from `LISPDIR`.
2. The file is not found → `Lisp_Open` (stream.c:383) raises
   `LispDestroy("%s: file %s does not exist")`.
3. `LispDestroy` (lisp.c, was line 700) unconditionally did:
   `PACKAGE = lisp__data.savepackage;`
   But `lisp__data.savepackage` is assigned **only** inside `LispTopLevel()`
   (lisp.c:835), which has not run yet this early in init — so it is still NULL.
   The macro `PACKAGE` expands to `*PACKAGE*`'s symbol value, so this **nulls the
   value of the `*PACKAGE*` special variable.**
4. Control longjmps back to `LispExecute` and `LispBegin` continues to line 5374:
   `object = ATOM2("*DEFAULT-PATHNAME-DEFAULTS*")`. `ATOM2 → LispNewSymbol` copies
   the current `PACKAGE` (now NULL) into the new atom's `->package`.
5. lisp.c:5395 `LispProclaimSpecial(object, APPLY1(Oparse_namestring, path), NIL)`:
   `APPLY1(parse-namestring, …)` **succeeds** and returns a valid pathname object;
   then inside `LispProclaimSpecial` (lisp.c:3530)
   `pack = name->package->data.package.package;` dereferences `name->package ==
   NULL` → Data Abort, `far = 0x18` (= reading `[NULL + 24]`).

### Reconciling the original triage note

The original brief reported the Pi fault as "pc in `LispProclaimSpecial`, lr in
`LispBegin`", pinned to line 5395, with `far=0x18`, x0=0. That is exactly this
bug. The secondary theory in the brief — that `parse-namestring`'s *function cell*
was NULL (an init-order/builtin-registration problem inside `APPLY1`/`LispFuncall`)
— is **refuted**: the host repro shows `Oparse_namestring`'s atom has
`a_builtin=1` and a valid non-NULL `property`, and `APPLY1` returns a valid value.
The `[obj+8]→[+40]→[+24]` deref chain in the brief maps onto LispProclaimSpecial
line 3530 (`atom->data.atom` = name at +8; `name->package` at +40 = NULL;
`->data.package.package` at +24), not onto the `LispFuncall` dispatch.

### Host reproduction (no Pi needed)

`lisp/lsp.c` is a standalone `main()` that calls `LispBegin()` directly. Compiling
the bundled lisp lib + `lsp.c` natively with `-DLISPDIR=<nonexistent dir>`
reproduces the segfault identically (build script + X stubs in the session
scratchpad). Under GDB:
- crash at `LispProclaimSpecial` lisp.c:3530, `name->package == NULL`;
- a hardware watchpoint on the `*PACKAGE*` value field caught the writer:
  `LispDestroy` (the `PACKAGE = savepackage` line), reached from
  `Lisp_Open → LispLoadFile → Lisp_Require → (require "lisp")` in `LispBegin`.
- With the `.lsp` files present at `LISPDIR`, `LispBegin` completes cleanly
  (`*default-pathname-defaults*` set, exit 0).
- With the files still absent **but the guard applied**, `LispBegin` also
  completes cleanly (require fails gracefully).

## Fixes implemented

### 1. Patch: `patches/xedit-1.2.2-phoenix-lispbegin-savepackage-null.patch`

Guards the `LispDestroy` restore so it only runs when `savepackage` is non-NULL:

```c
if (lisp__data.savepackage)
    PACKAGE = lisp__data.savepackage;
```

Applied by `build-xedit.sh` (idempotent `patch -p1 -N`). Verified compiled into
the shipped binary (a `cmp x0,#0; b.eq` guard now precedes the PACKAGE store in
`LispDestroy`).

### 2. `build-xedit.sh`: Pi-resident LISPDIR + staged module tree

- Configure with `--with-lispdir=/usr/lib/X11/xedit/lisp` (option is in
  configure.ac:43). Reconfigures automatically if `config.status` carries a stale
  LISPDIR, and `rm`s `lisp/*.o`/`liblisp.a` so the new `-DLISPDIR` takes effect.
- Stage `lisp/modules/*.lsp` + `lisp/modules/progmodes/*.lsp` (20 files) to
  `$NFS/usr/lib/X11/xedit/lisp/` (+ `progmodes/`), mirroring the layout
  `require.c` expects (`LISPDIR + "/" + module + ".lsp"`).
- Pre-flight now asserts the binary contains the new LISPDIR and warns if the
  stale `/tmp/x11-phoenix` path is still present.

## Status / handoff

- Build: PASS. `aarch64` static ELF, 0 undefined symbols, LISPDIR baked as
  `/usr/lib/X11/xedit/lisp`, guard compiled in, 20 `.lsp` staged on NFS.
- Staged: `/srv/phoenix-rpi4-nfs/bin/xedit` +
  `/srv/phoenix-rpi4-nfs/usr/lib/X11/xedit/lisp/{*.lsp,progmodes/*.lsp}`.
- NEEDS HW: `startx xedit` should now bring up the xedit window (text area, menu,
  scrollbar) with no Data Abort. Watch UART for any *next* missing-resource crash
  (e.g. XKeysymDB / bitmaps); report the last log line + HDMI.

## Notes for upstreaming

The `LispDestroy` NULL-`savepackage` guard is a genuine upstream xedit bug
(reproducible on a plain host build with a bad LISPDIR) and is a clean candidate
for upstream. The LISPDIR/staging change is packaging-specific to this port.
