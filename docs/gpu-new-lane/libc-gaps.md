# libphoenix gaps for the DRM userspace stack

**Question:** E7 ([E7-drm-userspace-build.md](E7-drm-userspace-build.md) §3.2) found
the libc gaps that libdrm and a Mesa DRM build hit and worked around them with
simulated fixes and compat headers. This fills them in libphoenix itself, each
with a real implementation and a Unity test group, so the new lane (and every
later port) can drop the workarounds.

**Where:** branch `gpu-lane/libc-gaps` in two worktrees. Nothing is merged or pushed.

- libphoenix: `/home/houp/.claude/jobs/c8f1289c/tmp/wt-libphoenix`, 9 commits on `a844f10`
- phoenix-rtos-tests: `/home/houp/.claude/jobs/c8f1289c/tmp/wt-tests`, 9 commits on `7e6d891`

Each libphoenix commit has a matching tests commit and can be merged on its own.
One of them, **barriers, needs an old-lane change first** (see "Merge notes").

## What was added

| # | libphoenix commit | What | Test group (binary) |
|---|---|---|---|
| 1 | `8551094` | `static_assert` in `<assert.h>`: C11/C17 only, since it is a keyword in C++ and C23; an existing definition is left alone | `assert_static` (test-libc-misc) |
| 2 | `c1c2af2` | `<inttypes.h>` brought up to C99: `SCN{d,i,o,u,x}PTR`, `SCN*MAX`, `PRI*FAST{8,16,32,64}`, `imaxdiv_t`, `imaxabs`, `imaxdiv`, `wcstoimax`, `wcstoumax`. The fast-type macros reuse the per-arch `_SCN_FAST*` prefixes, so no arch header changed | `stdlib_inttypes` (test-libc-stdlib) |
| 3 | `1f5c7db` | `flock()` was a stub that returned 0 for everything. It is now a real lock built on the kernel's fcntl record locks (whole file; `LOCK_NB` selects `F_SETLK`, otherwise `F_SETLKW`). `<sys/file.h>` now includes `<fcntl.h>`, which brings in the `LOCK_*` constants. The function lives in its own object | `sys_file_flock` (test-libc-misc) |
| 4 | `3162bd4` | `sysconf(_SC_PHYS_PAGES)` and `_SC_AVPHYS_PAGES` (keys 103/104). They read the kernel page allocator's counters through `meminfo()` with every table size set to −1, so no tables are copied | `unistd_sysconf_mem` (test-libc-misc) |
| 5 | `c69d829` | `posix_memalign()`, `aligned_alloc()`, `memalign()` (the last in `<malloc.h>`, which was empty) | `stdlib_memalign` (test-libc-stdlib) |
| 6 | `6c23c9d` | `pthread_setcanceltype()` plus `PTHREAD_CANCEL_DEFERRED` and `PTHREAD_CANCEL_ASYNCHRONOUS` | `pthread_canceltype` (test-libc-pthread) |
| 7 | `7cc5628` | `open_memstream()` and `fmemopen()` | `stdio_memstream`, `stdio_fmemopen` (test-libc-stdio) |
| 8 | `293f48c` | *(optional item)* `scandir()` and `alphasort()`, in their own object | `dirent_scandir` (test-libc-dirent) |
| 9 | `3da702b` | *(optional item)* POSIX barriers (`pthread_barrier_*`, `pthread_barrierattr_*`), in their own object | `pthread_barrier` (test-libc-pthread) |

The matching tests commits are `41c6df4`, `1c15941`, `ac8fdd4`, `128b99e`,
`45ab5e0`, `22eee0e`, `a0363ff`, `2740265` and `15818aa`. The last one also updates
the now-stale message in `libc/misc/posix_options.c`: the test still asserts that
`_POSIX_BARRIERS` is unclaimed, but its message no longer says the functions are
missing.

### Design notes on the parts that carry risk

**Aligned allocation touches `malloc_dl.c`.** The change adds one function,
`_malloc_aligned()` (declared in `stdlib/malloc-internal.h`), and alters no existing
path. It uses dlmalloc's `internal_memalign` technique:

1. `malloc()` the chunk size that `size` needs, plus `alignment + CHUNK_MIN_SIZE` of slack.
2. If the payload is not aligned, take the first aligned payload at least
   `CHUNK_MIN_SIZE` further on.
3. Turn the leading piece into an ordinary free chunk. It keeps the original PUSED
   bit and gets a footer, a `freesz` credit, a bin entry and coalescing, the same
   steps `free()` performs.
4. The rest becomes an ordinary used chunk with PUSED clear.
5. `realloc(p, size)` then splits the tail slack off in place.

Both headers are shapes the allocator already produces. Payloads are only 8-aligned
here, so every alignment above 8 takes this path. Note that the old lane's
`gl_stubs.c` comment "Phoenix malloc returns 16-byte-aligned" is wrong.

The public wrappers live in `stdlib/memalign.o`, not in `malloc_dl.o`. Every program
links `malloc_dl.o`, so the old lane's own `posix_memalign` in `gl_stubs.c` keeps
linking. The C1 instrumentation is untouched. One small side effect: `lastCaller` is
set by `malloc()`, so for an aligned allocation it names `_malloc_aligned`.

**Stdio changes `file.c` but not the `FILE` layout.**

- A new flag, `F_OPS`, marks a FILE that has no descriptor (`fd = -1`). Such a FILE
  is the first member of a private `ops_FILE`, the same pattern as the existing
  `popen_FILE`.
- The four descriptor primitives (read, write, lseek, close) now go through
  dispatchers, `file_rawRead/Write/Seek/Close`, which call the FILE's hooks when it
  has them. Buffering, `ungetc`, the `ftell` adjustment, `fflush(NULL)` and the
  read/write turnaround all run unchanged on top of them.
- Descriptor streams behave exactly as before. The harness proves this with an
  identical result digest (below).
- New behaviour applies only to hook-backed streams: a hook that accepts nothing is
  reported as a short write with ENOSPC, and `fileno()` returns −1 with EBADF.
- The constructor is `_file_openOps()`, declared in `stdio/stdio-internal.h`. A
  public `fopencookie()` could be layered on it later. The existing
  `cookie_io_functions_t` in `<stdio.h>` has `void *` members, not glibc's types.

**open_memstream semantics follow POSIX.**

- The length only grows.
- A write after a seek past the end zero-fills the gap.
- `*bufp` and `*sizep` hold the buffer and `min(position, length)`. They are updated
  on every write and seek, so they are always correct after `fflush()`.
- The data is always NUL-terminated at the length, and at `*sizep` after `fclose()`.

**fmemopen semantics:**

- Reads stop at the data length, writes at the buffer size.
- Seeks may go anywhere in `[0, size]`; anything outside fails with EINVAL.
- Mode `"a"` writes at the end of the data, which starts at the first NUL.
- `"w+"` empties the string, as glibc and musl do.
- When a write grows the data, a NUL follows it if it fits. A write-only stream that
  fills the buffer puts the NUL in its last byte instead (musl's reading of POSIX).
- `size == 0` fails with EINVAL.

**Cancellation.** libphoenix's `pthread_cancel()` already runs the target's cleanup
handlers and kills it immediately whenever cancellation is enabled. That is
asynchronous cancellation, so `PTHREAD_CANCEL_ASYNCHRONOUS` is fully honoured,
including the rule that a pending request fires as soon as an async-type thread
re-enables cancellation. `PTHREAD_CANCEL_DEFERRED`, the default, is recorded but not
honoured, the same as before; `<pthread.h>` and the function comment both say so.
Making deferral real would change every existing caller and needs cancellation
points first. The new type field sits in the private `pthread_ctx`, so there is no
ABI change.

**flock deviations** come from building it on record locks; gnulib's emulation
behaves the same way:

- Locks belong to the process, so two descriptors in one process never conflict.
- A lock is dropped on the first `close()` of any descriptor to the file.
- The kernel refuses pipes, sockets and ttys; flock reports that as EOPNOTSUPP, as
  BSD does.

## Evidence

### Compile checks

Every touched file was compiled with the real target flags (`-Werror` included) by
recovering the `syntax-check.sh` command, putting the worktree's `include/` **first**
and adding `-fsyntax-only`. A negative control showed that without the reordering
the stale installed headers are used instead.

- A sweep over all 109 non-arch, non-libm libphoenix sources is clean. The only 2
  failures are pre-existing: `malloc_trivial3.c`, which is not built, and
  `strerror.c`, which needs a generated `.inc`.
- A C++17 translation unit that includes every touched header compiles.
- Every new test file compiles with the tests' target flags. A sweep over all 108
  existing `libc` test sources against the worktree headers is clean except
  `stdlib/stdlib_strto.c:402`, which fails identically against the unmodified headers
  (a `%ld` vs `LONG_MIN` type warning, pre-existing).
- Sibling repos were searched for the new types and names. The only collision is
  the old-lane compat header (see "Merge notes").

### Host harness results

Every Unity group also passes against glibc on the host. The glibc-only
expectations are the Phoenix-deviation cases, guarded by `#ifdef __phoenix__`.

| Item | Host proof |
|---|---|
| inttypes | A generated probe calls `printf`/`sscanf` with every PRI/SCN macro and its type under `-Wformat=2 -Werror`: clean. Swapping `intptr_t` for `int` gives 11 errors, so the probe can fail. |
| flock | `flock.c` built for the host over Linux fcntl passes 8 of the 9 cases in `sys_file_flock`. The 9th is the pinned same-process deviation, which takes its non-Phoenix branch off-target. The old stub fails 6/9. |
| posix_memalign | **`tools/malloc-harness`** gained an opt-in `--memalign N` op (build with `MH_CFLAGS="-DHZ_MEMALIGN -DMH_MALLOC_DL_SRC=..."`). It ran the worktree's `malloc_dl.c` for 8 seeds × 100k ops with 15 % aligned bursts (8 B to 64 KiB), checking the full heap/bin/footer/PUSED/freesz invariants after every op: all OK. The 4-thread × 200k-op stress with 30 % aligned ops had 0 tag mismatches. Three seeded bugs (PUSED left set, freesz not credited, payload off by 8) are each caught at the first aligned burst. The full default harness run prints output identical (modulo addresses) to the run against the unmodified allocator, and the generator draws no extra random numbers without `--memalign`, so existing seeds are unchanged. `stdlib_memalign` passes against `malloc_dl.c` + `memalign.c` built for the host. |
| open_memstream / fmemopen | New tool **`tools/libstdio-hosttest`** runs the real `file.c` + `memstream.c` on the host under ASan+UBSan, with public symbols renamed `ph_*`. Results: the descriptor path against glibc on real files has 0 differences, and its result digest `957daf050379ec27` is **identical** for the unmodified `file.c` (`MEM=0`). open_memstream against a POSIX model (300 runs × 200 ops, seeks past the end and negative) has 0 differences; append-only against glibc, 0. fmemopen against a POSIX model (600 × 200, all six modes, ungetc/fflush/turnarounds, whole buffer compared after close) has 0 differences. Seeded bugs (gap not zero-filled, size = position, reads ignoring the length) are each caught. `make unity` builds the target Unity groups with `-D__phoenix__` against libphoenix's `<stdio.h>` and runs them on this stdio: **17/17 pass**. |
| scandir | `scandir.c` built for the host passes `dirent_scandir`. |
| barriers | `barrier.c` over glibc pthreads under ThreadSanitizer: 8 threads × 20000 rounds, 0 early releases, exactly one serial thread per round. The old lane's stub fails the same check 99491 times. `pthread_barrier` passes against `barrier.c` built for the host, with `-D__phoenix__`. |
| static_assert, sysconf, canceltype | Unity groups pass against glibc. The implementations are target-only (`meminfo`, Phoenix threads), so the Pi run is their first execution. |

### glibc 2.43 deviations found while building the stdio harness

In every case below glibc is the side that deviates from POSIX, and I reproduced
each one standalone. The harness classifies them in `known_case()`, and its README
lists them.

- **open_memstream:** a write after a backward seek *truncates* the length. A seek
  past the end extends the length without any write. A glibc memstream can also be
  read back.
- **fmemopen:**
  - A failed `fseek` leaves the read position wrong.
  - After buffered writes and `fseek(fp, 0, SEEK_CUR)`, the next read returns bytes
    from beyond the data. Standalone: `w+`, size 292, data 278, `fread` returns 29.
  - An update stream filled to the end has its last byte overwritten with a NUL.
  - `"w+"` writes the initial NUL, but `"wb+"` does not.

The harness's glibc-fmemopen scenario is informational: 24 of its 600 runs diverge
in ways not yet explained. The POSIX model is the pass/fail oracle.

### Pre-existing libphoenix stdio limitations the harness exposed (not fixed, not new)

- `ftell()` on an append-mode stream (`fopen "a"`, and now `fmemopen "a"`) with
  buffered output adds the buffered bytes to where they would go *without* append.
  It is correct again after a flush.
- `ungetc()` on a stream that has not read anything yet fails, although C guarantees
  one byte of pushback.

## Left out, and why

- **`dl_iterate_phdr`.** Phoenix binaries get no auxv/`AT_PHDR`. Relying on
  `__ehdr_start` would need proof that the ELF header is mapped in every static
  program, and a correct implementation would also have to enumerate the objects
  `dlopen` loaded. E7 already reduces the need to `-Dshader-cache=disabled`. A
  half-correct version would be worse than a link error.
- **`_POSIX_BARRIERS` stays unclaimed.** `libc/misc/posix_options` asserts that it is
  absent, and changing the claimed option set is an owner decision
  (project_posix_option_macros_absent). Mesa does not test it.
- **Deferred cancellation:** see above.
- **An 8 GB Pi.** `meminfo_t` carries the byte counts as `unsigned int`, so they wrap
  above 4 GiB of managed RAM (the kernel `FIXME` in `vm_mapinfo`). The 4 GB board
  manages 3997696 KB, which is fine. Widening the fields is a kernel ABI change.
- **fmemopen on a write-only stream:** "NUL at the current position on flush" when
  the position is behind the data. POSIX asks for it; neither musl nor glibc does it.
  Not implemented.
- `fopencookie`, and the pre-existing limitations listed above.

## Merge notes (for the coordinator)

1. **Barriers (`3da702b`) break an old-lane Mesa *rebuild*.** The devices file
   `gpu/rpi4-v3d/mesa/phoenix_mesa_compat.h` defines its own `pthread_barrier_t`
   unconditionally and is force-included into every old-lane Mesa translation unit.
   After the merge that fails with `conflicting types for 'pthread_barrier_t'`. The
   fix is to delete its barrier block (the typedefs, the three prototypes and the
   `PTHREAD_BARRIER_SERIAL_THREAD` guard) and the three stubs in `gl_stubs.c`, which
   also turns old-lane barriers into real ones.
   - Existing old-lane binaries and links are not affected, because `barrier.o` is
     only pulled in when referenced.
   - Either land that devices change together with this commit, or hold the commit.
2. **The same compat header, other items: only warnings, no breakage.**
   - `static_assert` is left alone because it is already defined.
   - `SCNxPTR`/`SCNuPTR` and `_SC_PHYS_PAGES` (85 there, 103 here) are redefined,
     which only warns. The old-lane builds strip `-Werror` and use `-w`.
   - After an old-lane Mesa rebuild, `os_get_total_physical_memory()` starts
     returning the real RAM size instead of failing. That is a behaviour change for
     the old lane, and possibly the showcase gate should cover it. Deleting those
     compat lines is the tidy follow-up.
   - `posix_memalign` has the same prototype in both places, and `gl_stubs.c`'s
     definition keeps winning at link time.
3. **Ports.** Once rebuilt, ports whose configure scripts probe for
   `posix_memalign`, `open_memstream`, `fmemopen`, `scandir`, `flock` or barriers
   will start using ours. `flock` changes from "always succeeds" to real locking,
   which can block or return EWOULDBLOCK/EOPNOTSUPP. `sources/phoenix-rtos-ports`
   holds only recipes, so no census of `flock(` call sites could be taken from it;
   grep the extracted port trees before forcing port rebuilds.
   - The Window Maker port's `scandir`/`alphasort` in `libftw.a` still link, because
     ours are in their own object and the prototypes are identical.
   - Ports do not rebuild on their own when the headers change
     (project_ports_staleness_model).
4. **The toolchain's libphoenix copy.** Standalone tools link the toolchain's
   `libphoenix.a` and headers, so re-sync them after the merge
   (project_sysconf_nprocessors_hw_bug).
5. **Coordination repo, uncommitted in `/home/houp/phoenix-rpi`:**
   - `tools/malloc-harness/harness.c` gains `MH_MALLOC_DL_SRC` and the opt-in
     `--memalign` op; the default behaviour is byte-identical.
   - `tools/libstdio-hosttest/` is new (Makefile, README, `shim/`, `stdiff.c`,
     `support.c`).
   - This file.

## Pi test commands

After merging, build with `./scripts/rebuild-rpi4b-fast.sh --scope core --with-tests`.
The build must use `LIBPHOENIX_DEVEL_MODE=y` (the default), so the tests compile
against the build sysroot and not the toolchain's stale libphoenix headers. Check the
image with `strings` for a new symbol, for example `open_memstream`. Then run:

```
/bin/test-libc-misc    -v -g assert_static
/bin/test-libc-misc    -v -g sys_file_flock
/bin/test-libc-misc    -v -g unistd_sysconf_mem
/bin/test-libc-stdlib  -v -g stdlib_inttypes
/bin/test-libc-stdlib  -v -g stdlib_memalign
/bin/test-libc-pthread -v -g pthread_canceltype
/bin/test-libc-pthread -v -g pthread_barrier
/bin/test-libc-stdio   -v -g stdio_memstream
/bin/test-libc-stdio   -v -g stdio_fmemopen
/bin/test-libc-dirent  -v -g dirent_scandir
```

Then run the full `test-libc-misc/stdlib/pthread/stdio/dirent` binaries without
`-g` to check that nothing else regressed.

- `sys_file_flock` needs a writable `/tmp` and uses `fork()`.
- `dirent_scandir` creates `test_scandir` in the current directory.
- `unistd_sysconf_mem` prints the page counts it saw. On a 4 GB board expect about
  `phys=999424` pages.
- The Pi run is the first execution of: `sysconf` over `meminfo`,
  `pthread_setcanceltype` on Phoenix threads, and `flock` over the real kernel lock
  table (including the 20 ms `F_SETLKW` poll). Everything else has already run on
  the host as the real libphoenix code.

## Host harness commands

```
cd tools/libstdio-hosttest && LIBPH=/home/houp/.claude/jobs/c8f1289c/tmp/wt-libphoenix make run
TESTS=/home/houp/.claude/jobs/c8f1289c/tmp/wt-tests LIBPH=... make unity
# malloc (build into a private dir; build.sh writes into the tool dir):
gcc -std=gnu11 -O0 -g -pthread -Itools/malloc-harness/stubs -DHZ_MEMALIGN \
    '-DMH_MALLOC_DL_SRC="<wt>/stdlib/malloc_dl.c"' -c tools/malloc-harness/harness.c ...
./mh --memalign 15 --seeds 8 --ops 100000 ; ./mh --memalign 30 --threads 4 --mt-ops 200000 --seeds 0
```
