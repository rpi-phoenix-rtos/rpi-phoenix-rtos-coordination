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

## Pass the right locale — it is the whole story

```
./wdiff C          # the contract libphoenix implements
./wdiff C.UTF-8    # expect ~138 by-design differences
```

libphoenix's conversion layer is **C/POSIX locale only**: every byte maps 1:1 to a
`wchar_t`, stated in a comment above each function. Comparing it against glibc under
UTF-8 therefore "finds" a large number of differences that are design, not defects.
The first run of this harness did exactly that, and the result was misleading until
the baseline was corrected.

## Result as of 2026-09-24 (4083 comparisons)

**All 17 locale-independent functions match glibc exactly, zero differences:**
`wcslen wcscmp wcsncmp wcschr wcsrchr wcsstr wcsspn wcscspn wcspbrk wmemchr wmemcmp
wmemset wcscpy wcsncpy wcscat wcstol` (+ end-pointer). That is a positive result — those
paths are correct, not merely untested.

The 122 remaining differences under `C` are all in the locale layer and all explained:

| group | count | verdict |
|---|---|---|
| `mbtowc` `mblen` `mbrtowc` `mbrlen` `mbstowcs` `wcstombs` `wcrtomb` `wctob` | 112 | **By design.** glibc's C locale is ASCII-only and returns `EILSEQ` for any byte ≥ 0x80; Phoenix maps it 1:1. Neither is wrong; they are different C-locale charmaps. |
| `wcwidth` `wcswidth` | 10 | **By design, documented**, but see below: East Asian Wide characters and emoji report 1 column where glibc reports 2. |

No defects were found. What the run *does* pin down is the **limitation** now recorded in
`docs/KNOWN-ISSUES.md`: libphoenix has no UTF-8 multibyte support, so `%ls`, `wcstombs`
and `mbstowcs` are byte-identity, and codepoints above U+00FF cannot be encoded at all
(`wcstombs` returns -1).
