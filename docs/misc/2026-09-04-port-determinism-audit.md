# Port determinism audit: where a clean build and an incremental build disagree

**Why this exists.** The full-clean rebuild aborted at `lighttpd`, and the two defects
there were both *determinism* defects rather than compile errors: the build's OUTPUT
depended on which of several config files was found, and an extraction pattern matched
commented-out lines. The owner's standing request is to "prove determinism with a clean
chain", so the whole ports tree was swept for the same classes.

Scope: all 67 `sources/phoenix-rtos-ports/*/port.def.sh` plus the helper scripts they call.
Read-only; nothing built.

**Confidence marking is deliberate.** Findings I re-verified myself are marked
**[verified]**. The rest are **[reported]** — plausible, specific, with file:line, but not
independently re-checked. Do not act on a `[reported]` row without confirming it first;
that is the rule that caught five false closures on #67.

## The fact everything hangs off

`phoenix-rtos-build/port_manager/candidates.py:175-183`: `install_path` returns
`$PREFIX_BUILD` for every port whose `conflicts` is empty, and `openssl111:22` is the
**only** port with a non-empty `conflicts`. So for 66 of 67 ports `PREFIX_H`/`PREFIX_A`
are **one shared directory** with no per-port namespacing. Every finding below is a
consequence of that: ports can, and do, overwrite each other's headers and archives.

## Ranked

**1. Two different JPEG libraries write the same archive and header. [verified]**
`xorg_fonts/port.def.sh:104-107` guards on `$PREFIX/lib/libjpeg.a` +
`$PREFIX/include/jpeglib.h` and then builds **IJG jpeg-9e** (`--prefix="$PREFIX"`). The
`libjpeg` port builds **libjpeg-turbo 3.0.4** (`CMAKE_INSTALL_PREFIX=${PREFIX_PORT_INSTALL}`,
`LIBDIR=lib`, `INCLUDEDIR=include`) and `make install`s it unconditionally (`:67`) to the
same two paths. Neither port declares the other as a dependency, so the order is
unconstrained.

These are **not ABI-compatible**: turbo targets the libjpeg 6.2 ABI by default
(`JPEG_LIB_VERSION` 62) while IJG 9e is 90, and `jpeg_decompress_struct` grew fields
across 7/8/9. A consumer can therefore compile against one header and link the other
archive — a struct-layout mismatch, i.e. silent memory corruption rather than a link
error. Live consumers: `supertuxkart:154` (`-DJPEG_LIBRARY=${pfx}/lib/libjpeg.a`),
`dillo:72` (`-ljpeg`), `fltk:64` (`--disable-localjpeg`).

*Clean vs incremental:* on a clean build `xorg_fonts`' guard fires and jpeg-9e lands
first; whether turbo then overwrites it is port-manager state. On an incremental build
turbo's files already exist, so `xorg_fonts` skips jpeg entirely. The header and the
archive can even end up from **different** implementations.

**Fix, resolved to an exact edit 2026-09-04 (verified, not yet applied).** It is a pure
deletion — no dependency edge needs adding, and no consumer changes:

* `xorg_fonts:15` says outright that it builds jpeg for the *downstream* "WindowMaker +
  Xft stack", and nothing in `xorg_fonts` itself uses jpeg (its later blocks are freetype,
  expat, fontconfig, Xft, cairo, Xpm, Xaw7 — none consume libjpeg).
* **Every actual consumer already depends on the `libjpeg` port**, so they already order
  against turbo: `fltk:27` (`xorg_libs libpng libjpeg`), `supertuxkart:36` (`libjpeg …
  xorg_fonts`), and `dillo:28` transitively via `fltk`.
* So deleting `xorg_fonts:103-111` (the guarded jpeg-9e block) leaves turbo as the single
  producer, and the race disappears. `xorg_fonts` needs no `depends` change, because it
  never used the library it was building.
* Also drop `jpeg(IJG)` from the `license=` list at `:28` and the word `jpeg` from the
  stack comment at `:15`.

API risk is the opposite of what it looks like: turbo defaults to the libjpeg **6.2** API,
which is the classic 6b interface (`jpeg_std_error`, `jpeg_create_decompress`,
`jpeg_read_header`, `jpeg_read_scanlines`, …) that all four consumers use. IJG 9e is the
*newer* outlier here. Nothing in the tree calls a 9-only entry point.

Sequenced AFTER the authoritative clean build rather than into it: it touches a large port,
and a mistake costs the whole build. Verify standalone — `build-port.sh libjpeg`, then
`xorg_fonts`, then one consumer (`fltk`) — and confirm afterwards that
`$PREFIX/include/jconfig.h` still reports `JPEG_LIB_VERSION 62` with
`LIBJPEG_TURBO_VERSION`, i.e. exactly one implementation remains.

**2. Same collision for libpng. [reported]** `xorg_fonts:92` vs `libpng:38-51`, both
targeting `$PREFIX/lib/libpng16.a`. Same version (1.6.40) but different flag sets, so
which flags the shipped archive was built with is not determined by the recipes.

**3. My own lighttpd fix is INERT on incremental builds. [verified]**
`lighttpd/port.def.sh:32` wraps the whole `p_prepare` body — including the
`plugin-static.h` generation at `:46` — in `if [ ! -f "$PREFIX_PORT_WORKDIR/config.h" ]`.
The generated header's *input* lives outside the workdir, but the guard keys on a file
inside it, and `b_port_invalidate_stale_configure` only fires on a libphoenix API change,
never on a `lighttpd.conf` change. So the anchored-grep fix committed earlier today takes
effect on a clean build and **not** on an incremental one: editing `lighttpd.conf` does
not change the linked plugin table. **FIXED 2026-09-04** (ports `c396a75`): the CONFIGFILE
lookup and the generation now run outside the guard, so the table tracks the config on
every build; configure itself stays guarded, which is what that stamp is for.

**4. Ports that stage headers into the shared prefix change other ports' configure
answers. [VERIFIED 2026-09-04 for glib2]** `windowmaker:84-93` stages `libftw.a` + `ftw.h`, and libphoenix has
no `<ftw.h>` — so `AC_CHECK_HEADERS(ftw.h)` answers differently before and after
windowmaker runs. `glib2:61-70` stages **stub** `libintl.h`, `resolv.h`, `arpa/nameser.h`
and a stub `libresolv.a` whose own comment says it fails at runtime — and `mc:96` already
links `-lresolv`. `ncurses:55-57` flattens ncurses headers into the shared include root;
the scar tissue is already in-tree, `xterm:89` must seed `cf_cv_lib_tgetent=no` because
configure otherwise finds a tgetent provider in the sysroot. Same class as the `mc`
`<mntent.h>`/`<langinfo.h>` incident this repo already paid for.

*Verification of the glib2 half (2026-09-04).* `glib2:61-70` unconditionally copies stub
`libintl.h`, `arpa/nameser.h` and `resolv.h` into the **shared** `PREFIX_H`, and builds a
stub `libresolv.a` into the shared `PREFIX_A`. Its own comment states the resolv stub
"fails cleanly at runtime" and is needed only for gio's gresolver, "not built for the
mc-critical libglib-2.0" — yet `mc:96` links `-lresolv`, so **mc ships against the stub**.
That is tolerable for mc (no DNS needed to browse locally); the real hazard is the shared
prefix: any port configured *after* glib2 probes `resolv.h` / `res_query` and the gettext
macros successfully **against stubs** — order-dependent on a clean build, universal on an
incremental one.

**Fix direction (not yet implemented):** stage such stubs into a **port-private** include
dir and add `-I` for the consuming port only, instead of the shared `PREFIX_H`/`PREFIX_A`.
Same treatment for `windowmaker`'s `ftw.h`/`libftw.a` and `ncurses`'s header flattening.
Each is its own controlled change — three ports, three builds — and none should ride along
with an unrelated turn.

**5. The four game ports include `-I/tmp/mesa-v3d-build/src`. [reported]**
`quake3:150`, `quakespasm:128`, `yquake2:124`, `supertuxkart:226`, with directory
assertions that `b_die` without it. Generated Mesa headers come from a host-local `/tmp`
tree, recorded in no manifest and wiped on reboot. Within a single build the gpu phase
creates it, so this is a coupling rather than a clean-room blocker — but the binaries'
contents depend on when `build-gl-phoenix.py` last ran. `ports.yaml` documents the
`tools/.gpu-libs` absolute-path wart as accepted; it does **not** mention the `/tmp`
include path.

**6. `b_port_invalidate_stale_configure` is autoconf-only. [reported]**
`port.subr:209` gates on `[ -f config.status ] || [ -f config.log ]`, so the 11 CMake
ports (whose `CMakeCache.txt` caches compiler and libc probe results), `ffmpeg` and
`openssl111` are never invalidated — their probes stay frozen at whenever the port was
first configured. `ffmpeg:88-94`'s hand-flip of `HAVE_ERF/EXP2/EXP2F/LOG2F` from 0→1,
commented "fresh-libc reconcile", is a manual patch over exactly this.

**Finding 6, designed 2026-09-04 (verified against the source, not yet applied).**
`b_port_invalidate_stale_configure` (`port_manager/port.subr:200-275`) keys a stamp on
the sha256 of libphoenix.a's exported symbol list and, when that changes, deletes the
configure outputs so the port reconfigures against the new libc. Its gate at `:209`
returns early unless `config.status` or `config.log` sits at the workdir root, and the
comment explains why `Makefile` is deliberately excluded from detection: for busybox and
friends the Makefile IS the shipped source.

That reasoning is right but leaves the CMake ports out, and their `CMakeCache.txt` caches
the compiler identification plus every `try_compile`/`check_function_exists` answer — i.e.
exactly what a libc change invalidates. The fix is safe because a CMake port's outputs are
segregated: every one of them does `mkdir -p "${PREFIX_PORT_WORKDIR}/build"` and configures
*into* that directory (verified in `zlib:36-41`, which guards on `build/Makefile`), so
`build/` is pure output and can go wholesale — nothing shipped lives there.

Shape:

1. Replace the early return with type detection — `autoconf` when
   `config.status`/`config.log` is present, `cmake` when `build/CMakeCache.txt` is,
   otherwise return 0 as today (so busybox-style trees stay untouched).
2. After the key comparison, branch the deletion: for `cmake`, `rm -rf "${workdir}/build"`
   and write the stamp. Recipes guard on `build/Makefile`, which disappears with it.
3. Add `${workdir}/ffbuild/config.log` to the autoconf detection. That is the only reason
   ffmpeg is missed — its log is not at the workdir root — and the existing
   `[ -f Makefile.in ]` guard already protects ffmpeg's *shipped* Makefile, so the
   deletion stays safe. ffmpeg is the port most exposed here: `:88-94` hand-asserts
   `HAVE_ERF/EXP2/EXP2F/LOG2F=1` against configure's own probe result of 0, a standing
   claim about libphoenix's libm that nothing re-validates.

**Still not covered, deliberately:** `openssl111`. It guards its configure on `Makefile`
and ships no `Makefile.in`, so the Makefile deletion is skipped by the existing safety
check and openssl keeps a stale Makefile. Covering it needs an openssl-specific marker;
out of scope for a change that should stay one mechanism wide.

Verify by construction: touch libphoenix (any exported-symbol change), rebuild one CMake
port, and confirm the "libc API changed" line appears and `build/` was recreated.

**7. ~35 X11 tarballs fetched with no sha256. [reported]** `xorg_libs:73-91`,
`xorg_fonts:69-89`, `xorg_apps:94-106` — `curl` with no checksum, unlike every
framework-fetched anchor. `xorg_libs`/`xorg_fonts` additionally cache into
`${PHOENIX_DISTFILES:-$HOME/.phoenix-distfiles}/xorg`, **outside the buildroot**, keyed by
basename and reused unverified forever. This host therefore builds from a `$HOME` cache
nothing verifies while a clean room re-downloads whatever the mirror serves that day.

**Finding 7, prepared 2026-09-04 — the cache is (probably) genuine, and here are the
hashes to pin.** The host cache holds **33** tarballs, 64 MB, in
`~/.phoenix-distfiles/xorg`, reused unverified forever. Two sampled tarballs
(`libX11-1.8.7`, `pixman-0.42.2`) were re-fetched from the recipe's own mirror
(`artfiles.org`) and **agree byte-for-byte** with the cached copies, and several of the
sums below match widely published values. Stated honestly: **mirror agreement is not a
signature check** — it rules out local corruption and a stale cache entry, not a
compromised mirror. x.org's `.sha256` side-files are not served at the paths tried
(`www.x.org/releases/individual/...` returns HTML), so the tarball-vs-tarball comparison
above is the strongest cheap check available.

*Note:* the cache holds **both** `libXt-1.3.0` and `libXt-1.3.1`, i.e. a version bump left
the superseded tarball behind — harmless, but it shows the cache only ever accumulates.

**Fix shape:** give `_fetch_extract` a third argument (expected sha256), verify after
download and before extraction, and `b_die` on mismatch — the same contract the framework
already applies to every `sha256=`-pinned anchor. The table is the input:

| tarball | sha256 |
|---|---|
| `libXaw-1.0.16.tar.gz` | `012f90adf8739f2f023d63a5fee1528949cf2aba92ef7ac1abcfc2ae9cf28798` |
| `libICE-1.1.1.tar.gz` | `04fbd34a11ba08b9df2e3cdb2055c2e3c1c51b3257f683d7fcf42dabcf8e1210` |
| `xcb-util-image-0.4.1.tar.gz` | `0ebd4cf809043fdeb4f980d58cdcf2b527035018924f8c14da76d1c81001293b` |
| `libXext-1.3.5.tar.gz` | `1a3dcda154f803be0285b46c9338515804b874b5ccc7a2b769ab7fd76f1035bd` |
| `freetype-2.13.2.tar.gz` | `1ac27e16c134a7f2ccea177faba19801131116fd682efc1f5737037c5db224b5` |
| `xcb-util-keysyms-0.4.1.tar.gz` | `1fa21c0cea3060caee7612b6577c1730da470b88cbdf846fa4e3e0ff78948e54` |
| `xcb-util-0.4.1.tar.gz` | `21c6e720162858f15fe686cef833cf96a3e2a79875f84007d76f6d00417f593a` |
| `libXdmcp-1.1.5.tar.gz` | `31a7abc4f129dcf6f27ae912c3eedcb94d25ad2e8f317f69df6eda0bc4e4f2f3` |
| `libXft-2.3.8.tar.gz` | `32e48fe2d844422e64809e4e99b9d8aed26c1b541a5acf837c5037b8d9f278a8` |
| `libXau-1.0.11.tar.gz` | `3a321aaceb803577a4776a5efe78836eb095a9e44bbc7a465d29463e1a14f189` |
| `jpegsrc.v9e.tar.gz` | `4077d6a6a75aeb01884f708919d25934c93305e49f7e3f36db9129320e6f4f3d` |
| `libxcb-1.16.tar.gz` | `4348566aa0fbf196db5e0a576321c65966189210cb51328ea2bb2be39c711d71` |
| `libSM-1.2.4.tar.gz` | `51464ce1abce323d5b6707ceecf8468617106e1a8a98522f8342db06fd024c15` |
| `libpthread-stubs-0.5.tar.gz` | `59da566decceba7c2a7970a4a03b48d9905f1262ff94410a649224e33d2442bc` |
| `cairo-1.16.0.tar.xz` | `5e7b29b3f113ef870d1e3ecf8adf21f923396401604bda16d44be45e66052331` |
| `libXrender-0.9.11.tar.gz` | `6aec3ca02e4273a8cbabf811ff22106f641438eb194a12c0ae93c7e08474b667` |
| `expat-2.5.0.tar.bz2` | `6f0e6e01f7b30025fa05c85fdad1e5d0ec7fd35d9f61b22f34998de11969ff67` |
| `libX11-1.8.7.tar.gz` | `793ebebf569f12c864b77401798d38814b51790fce206e01a431e5feb982e20b` |
| `libpng-1.6.40.tar.gz` | `8f720b363aa08683c9bf2a563236f45313af2c55d542b5481ae17dd8d183bb42` |
| `libXpm-3.5.17.tar.gz` | `959466c7dfcfcaa8a65055bfc311f74d4c43d9257900f85ab042604d286df0c6` |
| `xcb-proto-1.16.0.tar.gz` | `a75a1848ad2a89a82d841a51be56ce988ff3c63a8d6bf4383ae3219d8d915119` |
| `xtrans-1.5.0.tar.gz` | `a806f8a92f879dcd0146f3f1153fdffe845f2fc0df9b1a26c19312b7b0a29c86` |
| `libXfont2-2.0.6.tar.gz` | `a944df7b6837c8fa2067f6a5fc25d89b0acc4011cd0bc085106a03557fb502fc` |
| `libfontenc-1.1.8.tar.gz` | `b55039f70959a1b2f02f4ec8db071e5170528d2c9180b30575dccf7510d7fb9f` |
| `libXmu-1.2.1.tar.gz` | `bf0902583dd1123856c11e0a5085bd3c6e9886fbbd44954464975fd7d52eb599` |
| `libxkbfile-1.1.3.tar.gz` | `c4c2687729d1f920f165ebb96557a1ead2ef655809ab5eaa66a1ad36dc31050d` |
| `libXrandr-1.5.4.tar.gz` | `c72c94dc3373512ceb67f578952c5d10915b38cc9ebb0fd176a49857b8048e22` |
| `libXt-1.3.1.tar.gz` | `cf2212189869adb94ffd58c7d9a545a369b83d2274930bfbe148da354030b355` |
| `fontconfig-2.14.2.tar.xz` | `dba695b57bce15023d2ceedef82062c2b925e51f5d4cc4aef736cf13f60a468b` |
| `xcb-util-wm-0.4.2.tar.gz` | `dcecaaa535802fd57c84cceeff50c64efe7f2326bf752e16d2b77945649c8cd7` |
| `libXt-1.3.0.tar.gz` | `de4a80c4cc7785b9620e572de71026805f68e85a2bf16c386009ef0e50be3f77` |
| `xcb-util-renderutil-0.3.10.tar.gz` | `e04143c48e1644c5e074243fa293d88f99005b3c50d1d54358954404e635128a` |
| `pixman-0.42.2.tar.gz` | `ea1480efada2fd948bc75366f7c349e1c96d3297d09a3fe62626e38e234a625e` |

**8. Ports that write into the rootfs outside `b_install`. [reported]** `xorg_apps`,
`windowmaker`, `xterm`, `xbill`, `mc`, `dropbear`, `busybox` write directly into
`${PREFIX_FS}/root/...`, several best-effort (`mc:137-139` `|| true`, whose own comment
says mc degrades to monochrome without the skins). A `build.sh fs project image` run after
a clean therefore yields an image missing `/bin/{xterm,wmaker,xcalc,…}` **with no error**,
while an incremental run has them surviving from the previous ports stage.

**9. `coreutils` installs whatever AArch64 ELF is in `src/`. [reported]** `:62-79` —
`make -k … || true`, then install by `readelf | grep -q AArch64`, backstopped only by a
`>= 100` count. A binary from a previous successful link is indistinguishable from one
built this run.

**10. `micropython` generates its golden test results with the BUILD HOST's python3.
[reported]** `:118-129`, guarded by `[ -f "$test.exp" ] && continue`, so the `.exp` files
are kept forever from whenever the port was first tested. Test verdicts are
host-dependent and not reproducible in a clean room.

Lower-tier, tabled in the agent's full report and not repeated here: `glib2`/`mc` donor
`config.sub` picked from whatever other port happens to be extracted (`glib2:45`,
`mc:56`); `xorg_server` compiling `libmd` inside the checked-out ports repo (`:56-61`,
already papered over with a `.gitignore`); `azure_sdk` baking `$AZURE_CONNECTION_STRING`
into a source file; `lsb_vsx` baking the host `$PATH`; env-var-selected build inputs in
`heatshrink`/`lua`/`busybox`/`micropython`.

## What the sweep did NOT find

- **No second instance of the unanchored-grep class.** `lighttpd:46` was the only one;
  every python guard is `^`-anchored, `jq`'s generation is deterministic, and
  `mc/fake-pkg-config.sh` is pure.
- **No other port combines a disabled feature with unconditionally-compiled optional
  modules** — the combination that broke lighttpd. `ffmpeg:70-81` is the exemplary
  inverse (`--disable-autodetect --disable-everything`, then an explicit enable list).
- **The `config.h`/`Makefile` stamp guards are no longer the marker mismatch** the
  `libpng:29-37` comment describes: `b_port_invalidate_stale_configure` now deletes
  `config.status`, `config.cache`, `config.h`, `config.log` and `Makefile`. The residual
  gap is finding 6.

## Order of work

One port at a time, per the owner's rule. First two: **finding 3** (my own fix is
currently inert — smallest, and it makes work already committed actually take effect)
then **finding 1** (two libraries at one path, with a live consumer in supertuxkart).
Findings 4 and 6 are the ones that will keep producing "impossible" bugs, but each needs
its own controlled change.
