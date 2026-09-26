# libstdio-hosttest

Runs libphoenix's real `stdio/file.c` + `stdio/memstream.c` on the host (their
public symbols renamed `ph_*` with objcopy; `shim/` stands in for the Phoenix-only
headers) and checks them in about a second.

    make run                        # against sources/libphoenix (needs stdio/memstream.c,
                                    # i.e. gpu-lane/libc-gaps merged; before that use
                                    # MEM=0 or point LIBPH at the worktree)
    LIBPH=/path/to/tree make run    # e.g. a worktree
    LIBPH=... MEM=0 make run        # a tree without memstream.c (file scenario only)
    ./stdiff --trace | --canary | --seed N
    TESTS=/path/to/phoenix-rtos-tests make unity
                                    # the target Unity groups stdio_memstream +
                                    # stdio_fmemopen, built with -D__phoenix__ against
                                    # libphoenix's <stdio.h> and run on this stdio

| scenario | oracle | what it covers |
|---|---|---|
| `file` | glibc | the descriptor path every existing stream uses, on a real file. Prints a digest of libphoenix's results: it must not change when file.c is refactored (compare with `MEM=0` on the old tree). |
| `mem edges` | glibc | empty stream, gap after a seek past the end, seek back + close, argument errors, overflow, NULL buffer, fileno |
| `memstream` | glibc | open_memstream, append-only (see below) |
| `ms-model` | POSIX model | open_memstream with seeks anywhere, including past the end and negative |
| `fmem-model` | POSIX model | fmemopen, all six modes, reads/writes/ungetc/fflush/turnarounds; whole buffer compared after close |
| `fmemopen` | glibc | informational only |

Only the first difference in a run is counted (the rest follow from it).
`known_case()` names deliberate differences. `--canary` feeds glibc different data
and must report differences.

glibc 2.43 deviates from POSIX here, which is why the models are the oracle for
seeks and why the glibc fmemopen scenario does not fail the run:

- open_memstream: a write after a backward seek TRUNCATES the length to that write
  (POSIX: the length only grows); a seek past the end extends the length without a
  write.
- fmemopen: after one fseek fails with EINVAL, reads continue from a wrong place;
  after buffered writes and `fseek(fp, 0, SEEK_CUR)` the next read returns bytes from
  beyond the data; an update stream filled to the end has its last byte overwritten
  with a NUL (POSIX: only "if it fits"); `"w+"` writes the initial NUL but `"wb+"`
  does not.

Known libphoenix limitations it shows (pre-existing, not memstream-specific):
`ftell()` on an append-mode stream with buffered output reports where the data
would go without append; `ungetc()` on a stream that has not read anything yet
fails (C guarantees one byte of pushback).

SPDX-License-Identifier: BSD-3-Clause
