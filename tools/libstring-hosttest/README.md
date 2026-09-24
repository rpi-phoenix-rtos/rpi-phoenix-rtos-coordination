# libstring-hosttest — guard-page + differential harness for libphoenix's string.c

Runs libphoenix's **actual** `string/string.c` on the host and checks two things at once,
in about a second, with no Pi cycle and no cross-build:

1. **Over-reads.** Every input is placed so its last byte is the last byte of a mapped
   page, with the next page `PROT_NONE`. A bounded function that reads even one byte past
   its limit takes `SIGSEGV`, which is caught and reported as
   `SFAULT <func> case=… READ PAST THE BOUND`.
2. **Results.** The same call is compared against the host glibc on an identical but
   unguarded copy.

```
make run       # 1368 bound checks
make canfail   # rebuild against a known-buggy string.c and prove the guard fires
```

## Why this shape

This is not a hypothetical class of bug for this project. `strncmp`/`strncasecmp` tested
their bound **after** dereferencing, so they read one byte more than the standard allows:

```c
for (p = us1, k = 0; (*p != '\0') && (k < n); p++, k++)   /* WRONG: *p read first */
for (p = us1, k = 0; (k < n) && (*p != '\0'); p++, k++)   /* fixed */
```

Harmless unless that byte is the first of an unmapped page — so it surfaced only as an
*intermittent* SuperTuxKart crash, `Data Abort (EL0) far=0x0cf0e000` inside `strncmp` with
`n=1`, reached from AngelScript's tokenizer. Finding it cost a debugging session.

`make canfail` puts that exact defect back and confirms the harness catches it. It fires on
the **first** case (`w0/w0/n1` — a 1-byte unterminated string ending at a page boundary,
`n=1`), which is precisely the crash signature. A harness that cannot fail proves nothing;
this one is checked against a real defect rather than an invented one.

The run also opens with a canary that deliberately reads past the guard, so a clean result
cannot come from protection that was never armed.

## Result as of 2026-09-25

```
STRING-HOST total=1368 overreads=0 wrong=0
```

**Clean.** 40 functions compiled; the bounded ones (`strncmp` `strncasecmp` `memcmp`
`memchr` `memrchr` `memmem` `strnlen` `strncpy` `stpncpy` `memmove` `mempcpy`) were each
given an unterminated input of exactly `n` bytes against the guard, and the NUL-terminated
ones (`strlen` `strchr` `strrchr` `strchrnul` `strcmp` `strcasecmp` `strspn` `strcspn`
`strpbrk` `strstr` `strcasestr` `strlcpy`) were given a string whose terminator is the last
readable byte. No function read past its bound, and every compared result matched glibc.

## Mechanics worth reusing

* **`objcopy --redefine-sym`** renames only the symbols `string.c` *defines*, so its
  undefined references still bind to glibc. `--prefix-symbols` renames those too and breaks
  the link.
* **`shim/arch.h`** supplies the one macro `string.c` needs from the target-only header
  (`__EXPORT_INLINE`), copied verbatim from `include/sys/cdefs.h`.
* `SA_NODEFER` + `siglongjmp` let a single process survive and attribute many faults.

Sibling harnesses: `tools/libwchar-hosttest/`, `tools/libext2-hosttest/`.
