# libwchar-hosttest — differential harness for libphoenix's wchar

Runs libphoenix's **actual** `wchar/wchar.c` on the host, next to glibc, and compares
both on the same inputs. No Pi cycle and no cross-build: `make && ./wdiff C` takes
about a second, so a wide-char change can be checked before it ever reaches hardware.

Same idea as `tools/libext2-hosttest/`, which found 21 defects this way.

## How it works

`wchar.c` includes only `<wchar.h> <stdlib.h> <string.h> <errno.h>`, so it compiles
natively as-is. Two mechanics make the comparison possible:

* **`objcopy --redefine-sym`** renames only the 40 functions `wchar.c` *defines*, to
  `ph_*`. Its *undefined* references (`malloc`, `strtol`, `strlen`, …) are left alone
  and still bind to glibc. `--prefix-symbols` would have renamed those too and broken
  the link.
* **`-Dcount=__count`** reconciles the one field-name difference: Phoenix's
  `mbstate_t` has `count`, glibc's has `__count`. `wchar.c` touches it exactly once
  (`mbsinit`) and has no other identifier named `count`.

The harness prints one tagged `WDIFF <func> case=… ph=… glibc=…` line per mismatch and
a final `WCHAR-HOST total=N diffs=M`. It opens with a **canary** that deliberately
compares 1 against 2 and aborts if that does not register — so a `diffs=0` result
cannot come from a comparison path that never fires.

## Two locales, both gating (since 2026-11-21, libphoenix branch `utf8-locale`)

```
make O=/tmp/wh LIBPH=<libphoenix tree>
/tmp/wh/wdiff C          # single-byte C/POSIX locale: total=4083 diffs=0 known=122 (unchanged)
/tmp/wh/wdiff C.UTF-8    # strict UTF-8: total=45490672 diffs=0 known=983470
```

Build out of tree (`O=`): the old in-tree objects are tracked. The harness now
links wchar.c, wcwidth.c, uchar.c and setlocale() (locale.c), renames the union
of their functions in all four, and `check-refs` refuses a build in which any
libphoenix object still calls an unrenamed name or a glibc `*_chk` wrapper.
`setlocale()` is called on BOTH sides.

Under `C`, non-ASCII cases are `known` (glibc's C locale is ASCII, libphoenix's
is Latin-1 byte identity). Under `C.UTF-8` everything gates, plus exhaustive
runs: every code point (wcrtomb/wctomb/c32rtomb, round trip, char16_t pairs,
byte-at-a-time decode, wcwidth), every 1-3 byte sequence, 4-byte boundary
sequences, string conversions at every length limit, btowc/wctob. `known` there
is only two by-design glibc laxities, each re-verified per case: glibc accepts
forms above U+10FFFF (8665), and answers (size_t)-2 for prefixes it rejects
only later where libphoenix rejects at the first impossible byte (974803).
wcwidth matches glibc 2.43 (Unicode 17.0) on all 0x110000 code points.
Mutation-checked: accepting surrogates gives 8330 diffs, dropping one width
run gives 112.
