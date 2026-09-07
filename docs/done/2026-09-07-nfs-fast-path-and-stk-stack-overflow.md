# NFS fast path + the STK stack overflow — full investigation record (2026-09-07)

Archived from `docs/inprogress/WEEK-2026-W37.md` once both were resolved, per the standing
rule that the weekly log stays short and history moves here. Kept verbatim, **including the
hypotheses that turned out to be wrong and the retractions** — the wrong turns are the useful
part of the record: three of them were caused by measurement artefacts
(`/ramtmp` silently being NFS, a `pgrep -f` self-match, an ANSI-prefixed grep anchor) and one
by pattern-matching data to a hypothesis without testing it (PCM read as mesh indices).

Outcome, in one line each:
- **nfs-fs path resolution was quadratic** — attribute cache + directory-snapshot reuse gave
  stat 8.4x, open 10.2x, readdir 142x, and a 243-file asset read 32 s -> 3 s.
- **STK's crash was never nfs-fs** — `char pcm[44100]` on libphoenix's 4 KiB default thread
  stack with no guard page, proven byte-exact against decoded `menutheme.ogg`.

## 4c. ★★★ PATH RESOLUTION COSTS ~8-9 ms PER COMPONENT — system-wide, not NFS, not STK

Owner challenged my "STK is NFS-load-bound" story ("NFS now runs over fast ethernet, SD
will NOT be much faster — look at the asset loading code path"). He was right, and chasing
it properly found something much bigger.

`tools/fileperf` (new) times each syscall separately in one process — no shell loop, no
`cat`/`ls` per file, no `tar` bookkeeping. Per file, 150 flat files on NFS / 60 on the
ramdisk:

| filesystem | readdir | stat | open | read | close |
|---|---|---|---|---|---|
| NFS (6 path components) | 39 ms | **47 ms** | **49 ms** | 3.3 ms | 1.2 ms |
| dummyfs ramdisk (3) | 17 ms | **57 ms** | **62 ms** | 13 ms | 8.5 ms |
| devfs `/dev` (2) | — | **17.8 ms** | — | — | — |

**The RAM disk is slower than NFS**, and a warm second pass is byte-identical, so this is
not caching and not a filesystem issue. `read`/`close` — which use an existing fd — are
fine; only the calls that RESOLVE A PATH are slow. The cost tracks path depth:
`/dev/x` (2) ~= 18 ms, a 6-component NFS path ~= 47 ms → **~8-9 ms per component**, i.e.
every path-component lookup appears to cost a full ~8-9 ms round-trip where it should be
tens of microseconds.

That explains STK exactly (5127 files x deep paths ~= 9 min) and silently taxes every load
in the system. Bulk I/O is healthy for contrast: `dd bs=1M` of a 50 MB pak = **2 s
(~25 MB/s)**, matching the documented 29.9 MB/s.

⚠️ **Three of my own measurements were invalid; I retract them.** `cat`/`ls -l` in a shell
loop fork+exec once per file, so they timed process spawn; "2.8 MB/s NFS throughput" came
from `cat` through bash instead of `dd bs=1M` (10x understated); and the first `fileperf`
run pointed a non-recursive tool at a directory tree, so it measured `files=0`. Caveat on
the ramdisk row: a background `tar` copy may still have been running, which would inflate
it — the NFS and devfs rows are clean.

**★★★★ ROOT CAUSE FOUND: path resolution is QUADRATIC in path depth.** A `stat()` of a
depth-*d* path issues **d(d+1)/2 + 2d + 1 NFS round trips** at ~1372 us each. The model fits
every measurement I took, to +/-1.3%:

| depth | measured stat | model RPCs | implied us/RPC |
|---|---|---|---|
| 1 | 5 500 us | 4 | 1375 |
| 2 | 10 902 us | 8 | 1363 |
| 3 | 17 742 us | 13 | 1365 |
| 5 | 35 953 us | 26 | 1383 |

**At depth 5 those 26 RPCs cover only 5 distinct paths** — `/usr` is lstat'd 5 times,
`/usr/share` 4 times, and so on, all inside ONE `stat()`. Three layers each re-walk the path:

1. **libphoenix `resolve_path`** (`unistd/dir.c:204-299`) rebuilds the FULL absolute prefix
   each iteration and probes it with **two messages per prefix** — `safe_lookup` plus an
   `mtGetAttr(atMode)` (`dir.c:604-651`) purely to ask "is this a symlink?", which it almost
   never is. Then `_stat_abs` discards all of it and resolves the leaf again
   (`sys/stat.c:34,130`).
2. **nfs-fs `nfs_ops_lookup`** (`nfs/nfs_ops.c:218-341`) walks components and calls
   `nfs_lstat64` on the **full prefix** each time (`:249`, `:289`) — even when
   `nfs_node_findPath` already returned a live node.
3. **No caching anywhere** on that path: the kernel "dcache" holds only `portRegister` names,
   nfs-fs caches `id<->path` and open filehandles but **re-lstats regardless**, and libnfs's
   dircache is v3-only and disabled (`srv.c:434`). That is why 20 repeated stats of the same
   path showed zero speedup.

⚠️ **Two more of my premises were wrong.** **libnfs already batches** — one `nfs_lstat64` of a
5-component path is ONE COMPOUND, one round trip (`libnfs nfs_v4.c:1155-1168,1425`), so
"batch the COMPOUND" was already done and is not a fix. And the cost is **not** ~5 ms/RPC:
it is **1372 us/RPC**, only **1.57x** the 873 us UDP floor, explained by TCP + IPC hops. My
"1 component = 1 RPC at 5.5 ms" was **4 RPCs misread as one**, and my "linear in components"
was a quadratic curve sampled too sparsely. **The per-RPC latency hunt I was about to start
would have found nothing.**

**Fix plan, ranked (all confined, no new subsystems):**
1. **Positive attribute cache in nfs-fs** (~40 lines, `nfs_ops.c`/`nfs_node.h`): 26 -> 5 RPCs
   at d=5 (**5.2x**). The redundancy is *intra-syscall*, so even a ~100 ms TTL captures
   nearly all of it with no user-visible staleness. Risk: `nfs_ops_open`'s re-stat is
   deliberate (`nfs_ops.c:374`, "so a redeployed file isn't shadowed") — it needs a `force`
   flag to bypass the cache.
2. **One-shot lstat in `nfs_ops_lookup`** (~20 lines): walk components locally (needed anyway
   for mount splices at `:263` and `..` at `:236`, both free rbtree hits), then a single
   `nfs_lstat64` on the longest unspliced prefix. Alone 26 -> 16; **with #1, 26 -> 2 (~13x)**,
   i.e. a 5-component stat 36 ms -> ~2.8 ms.
3. **Drop the per-prefix `_readlink_abs` probe** (`libphoenix dir.c:251`) — biggest ceiling and
   it taxes **every** filesystem, which is why the dummyfs ramdisk benchmarked *slower* than
   NFS. Riskiest (POSIX path semantics); the clean version is to add a type field to the
   lookup reply so the VFS need not ask twice.
4. **Splice `/ramtmp`** (2 lines, `srv.c:667-672` currently splices the tmpfs oid onto `/tmp`
   only) — narrow but trivial.

### 4a-fix. #1 IMPLEMENTED, #2 DROPPED (2026-09-07)

**nfs-fs attribute cache landed** — `phoenix-rtos-filesystems fe3979d` (+63/-13 over
`nfs_ops.c`, `nfs_node.c/.h`). Each node carries `struct nfs_stat_64 attr` + a
`CLOCK_MONOTONIC` deadline (`NFS_ATTR_TTL_US`, **100 ms**); the `nfs_ops_lookup` component
walk and `nfs_refreshStat` answer from it. Invalidated by write / truncate / setattr /
create / unlink / link, by `nfs_node_detachPath` (unlink-while-open), and wholesale by an
NFSv4 state reclaim; `mtOpen`'s deliberate redeploy re-stat passes `force=1` so it still
reaches the server. The spliced-special-file synthesis stays **ahead** of the cache check
(such a node has no file on the export). Compiles clean under the target toolchain with
`-Wall -Wextra`.

**#2 (one-shot lstat) dropped, not deferred — it adds nothing on top of #1.** With the cache,
prefix *i*'s lookup gets *i-1* hits plus one fresh lstat, so *d* lookups already cost *d*
lstats: exactly what the one-shot version would cost. It only wins on a *cold deep single
lookup*, which libphoenix never issues (it always walks prefix-by-prefix). Against that it
would have cost a real semantic regression: `nfs_lstat64` of a full path makes the **server**
follow intermediate symlinks, so `/a/link/b` would silently resolve instead of returning
`-ENOTDIR` as it does today (and as dummyfs does), moving `ELOOP` accounting off the client
and straight into the known `resolve_path` symlink-depth gap in `test-libc-misc`.

**Revised expectation: 26 -> ~6 RPCs at d=5 (~4.3x), not the 13x quoted above.** The floor is
one lstat per distinct path (5) plus the leaf op — the earlier "26 -> 2" assumed #2 removed
lookups that #1 already collapses.

### 4a-result. MEASURED ON HARDWARE: stat 8.4x, open 10.2x (2026-09-07)

Same tool, same flat 647-file directory, same 150-file sample as the 4c BEFORE table
(`rpi4b-uart-20260907-092334-fileperf2` vs `-111401-acfault`), per file in us:

| | readdir | stat | open | read | close | 150-file total |
|---|---|---|---|---|---|---|
| before | 38 936 | **46 781** | **48 972** | 3 272 | 1 237 | 20 918 ms |
| after  | 39 324 | **5 587** | **4 810** | 3 391 | 1 323 | **8 176 ms** |

**stat 8.4x, open 10.2x, whole walk 2.6x.** `read`/`close` unchanged as expected (they use an
existing fd). Repeated back-to-back: 8 176 / 8 192 ms — stable.

`stat()` at a fixed depth, warm (20 reps), against the 4c model curve:

| depth | before | after | speedup |
|---|---|---|---|
| 1 (`/usr`) | 5 500 us | **188 us** | 29x |
| 2 | 10 902 us | **251 us** | 43x |
| 3 | 17 742 us | **325 us** | 55x |
| 5 | 35 953 us | **505 us** | 71x |
| 6 (`stk_config.xml`) | ~46 800 us | **572 us** | ~82x |

**Per added component: ~7 000 us -> ~85 us.** The quadratic term is gone.

**The network is now entirely out of warm path resolution.** `--mkdepth` builds real nested
directories and times a stat at each depth; NFS and the dummyfs ramdisk are within noise of
each other at every depth (d=1: 277 vs 274 us; d=9: 1 510 vs 1 532 us). Whatever is left is
client-side — libphoenix's two messages per prefix plus kernel resolution, ~157 us per
component, i.e. fix **#3**, which no filesystem change can reach.

### 4a-stk. END-TO-END: 243 STK asset files 32 s -> 5 s (6.4x)

`stkrate.sh`, unchanged, same 243-file / 3 MB `supertuxkart/data/gui` tree
(`-090848-stkrate` vs `-111856-stkrate2`):

| | before | after |
|---|---|---|
| NFS, 243 files / 3 MB | **32 s** | **5 s** |
| dummyfs ramdisk, same files | 26 s | 5 s |

This is the number the owner's challenge was about: 3 MB took 32 s on a link that does
25 MB/s, and it was never NFS.

⚠️ The ramdisk row also improved, which an **nfs-fs-only** change cannot explain — `/ramtmp`
lookups short-circuit at the mount splice before any lstat. The 26 s was already flagged in
4c as possibly confounded by a concurrent `tar`; treat it as unreliable and do not build on
the ramdisk delta. The NFS row is the sound comparison.

### 4a-next. readdir was the dominant per-file cost — ROOT-CAUSED AND FIXED

`readdir` did not move (39 ms/file) and was the same on a ramdisk (37.5 ms/file over 548
files): for 150 files, 5.9 s of the 8.2 s total.

**Root cause, in our own code:** `nfs_ops_readdir` called `nfs_opendir()` — which in libnfs
**snapshots the entire directory** (a READDIR round trip, or several) — on *every* `readdir()`
call, emitted one entry, and closed it. A full listing per entry, so one scan of a directory
was O(n^2) in its size. The per-entry cost is flat in how far you have read (5/20/150 entries
-> 62/45/39 ms per file, i.e. ~38 ms per additional entry) and scales with **directory size**,
which is why a 649-entry directory costs ~38 ms/entry while `data/gui`'s small subdirectories
cost far less. It also opened the directory to answer "." and "..", a handle it never used —
two extra full listings per scan.

**Fixed: `phoenix-rtos-filesystems 667a41b`.** Hold the snapshot on the node
(`nfs_node.dirCache` + the cookie it sits at) and step through it locally: one listing per
scan. Reused only when positioned at exactly the requested cookie, so a seek, a rewind to
offs 0, or an interleaved second scan re-lists — which is also what keeps a re-scan from
inheriting a stale listing. One snapshot fs-wide (`nfs_fs_t.scanNode`) bounds pinned memory;
create/unlink drop the affected directory's snapshot; the reclaim path forgets snapshots
structurally. Consistency is not weakened — every call already re-listed, so entries added
mid-scan could already be missed or repeated. Compiles clean; **not yet measured on HW.**

Root cause confirmed from libnfs's own source, not inferred: `struct nfsdir` holds the
complete entry list, `nfs_readdir` walks it locally with no RPC (`lib/libnfs.c:1759`), and
`nfs_closedir` frees it because we disable libnfs's dircache (`srv.c:434`, it is v3-only).
So one `readdir()` really was one full listing over the wire.

⚠️ **A prediction to hold the fix to, recorded before the test.** dummyfs shows the same
~37.5 ms/entry, and reading it settles that this is NOT a re-walk: `dummyfs_readdir` already
keeps an iterator hint (`d->dir.hint`, `dummyfs.c:848`), so it is O(1) per call. So on the
ramdisk ~34 ms/entry is spent somewhere in the **mtReaddir message path itself**, not in the
server's logic — which is strange next to `stat` (<100 us for several messages) and `read`
(3.7 ms for up to 64 KB through the same `o.data` mechanism).

If that per-message cost is shared, removing the per-entry listing buys only ~4 ms of the
38 ms and readdir stays the dominant cost. **Two outcomes, both informative:** readdir drops
to ~1 ms/entry => the listing was the whole cost and dummyfs's number has an unrelated cause;
readdir stays near 34 ms => there is a third, shared bug in the message path, and it is the
next thing to chase. Not chased yet on purpose: the gate is a benched image.

### 4a-verdict. HW-MEASURED (`-113200-readdirfix`): readdir 142x, whole walk 11.3x

**The prediction resolved to the FIRST branch** — the per-entry listing was the entire cost,
so dummyfs's 37.5 ms/entry has an unrelated cause and stays open.

**Correctness checked before speed, and it is exact.** `stk-assets/textures` = **649**;
a staged `/root/rdmix` of 40 files + 2 subdirs + 3 symlinks = **45** visible / **47** with
dotfiles, all 45 names present and correct, **0 duplicates**; `/bin` = 165, `/etc` = 22. So
the carried cookie does not skip or repeat an entry. 0 faults on the boot.

Per file over the same 150-file sample of the same flat 649-entry directory, in us:

| | readdir | stat | open | read | close | total |
|---|---|---|---|---|---|---|
| baseline | 38 936 | 46 781 | 48 972 | 3 272 | 1 237 | 20 918 ms |
| + attr cache | 39 324 | 5 587 | 4 810 | 3 391 | 1 323 | 8 176 ms |
| + readdir fix | **277** | **2 765** | **4 698** | 3 234 | 1 274 | **1 848 ms** |

**readdir 142x. Whole walk 11.3x end to end.** `stat` halved again as a side effect (fewer
RPCs in flight). End-to-end `data/gui`, 243 files / 3 MB: **32 s -> 5 s -> 3 s**.

`open` at 4 698 us is now the largest item and the obvious next target. The 5127-file /194 MB
READ-ALL did not finish inside the capture window — rerun it with a longer one.

**Pushed to `publish` (fast-forward, no force):** filesystems `cd30ad2..0e3f337`, coord
`445ccb425..41d76ab8e`.

### 4a-stk-crash. STK faults entering the menu — cause NOT yet attributable

`stk` (bare, so the menu path) faulted in trial 1 of a 3-trial bench
(`-113657-acstk-T1`): **EL0 Data Abort** then a **PC alignment fault**, with
`x0 = x3 = x19 = far = 0xf122f1c9f290f379` — a high-entropy garbage value dereferenced as an
object pointer (`esr=0x92000004`, translation fault level 0). So: a corrupt/uninitialised
pointer, in the SP mesh path, not in file I/O.

What is ruled OUT: `kartDirt shader is missing` x5 is **normal** (exactly 5 in both
known-good runs, 0 faults); nfs-fs logged no error or reclaim; **0** V3D render-timeout or
MMU-VIO lines. In the known-good menu run (`20260827-173317-stk-cleanmenu2`) those same
kartDirt lines are the **last** UART output — STK reaches the menu and then renders silently
— so this run diverges at exactly that point.

⚠️ **I cannot attribute this to the nfs-fs work, and will not claim it either way.** STK was
never benched on the pre-change build (4a says so explicitly), and the last known-good STK run
is 2026-08-27/28 — a different kernel, Mesa and ports. There is no baseline to compare
against. Note also that the 2026-08-28 "good" race run itself hit a V3D RENDER TIMEOUT, so
STK has prior instability here.

Bench aborted after trial 1: the host killed it for low memory (transient — 26 GB free now),
not a Pi failure.

**The crash PC decodes, and it points at the allocator, not at file I/O.**
`pc=0x91bc04` -> `malloc_chunkSize` (`libphoenix/stdlib/malloc_dl.c:78`), `lr=0x91e86c` ->
`lib_rbInsert` (`sys/rb.c:242`). So STK died *inside malloc*, reading a chunk header at
`far = x0 - 0x20` from a garbage pointer: **heap corruption**, tripped over later by the
allocator. The high entropy of the bad value is consistent with **file content written past
the end of a buffer**.

That gives one concrete mechanism by which the nfs-fs work COULD be responsible: a buffer
sized from `stat().st_size` and then filled from a read that returns more bytes. A stale
size would do it, and the attribute cache is the only new source of one. So the next test
checks the filesystem's bytes AND its sizes together — `cksum` prints a CRC and a byte
count — over **107 STK asset files spread across path depths 2..8**, against a host
reference (`tests/pi-repro/cksumcheck.sh`). Every line matching exonerates the cache for
this crash and moves the bug into STK or the allocator; any mismatch is the bug itself.

Ordered below re-gating the five components the SD image actually ships.

### 4a-regate. Re-gating the shipped demo set on the new nfs-fs

| component | 3-trial | evidence |
|---|---|---|
| GPU X desktop | ✅ **3/3** | glamor up, 5 clients, 8 CL submits, **0 faults** — identical in all three |
| Quake III | ✅ **2/2 valid** | q3dm1 loaded + bots in T2/T3, 0 faults, 0 dropped draws. T1 = **VOID** (truncated capture; the fault in it is an unrelated boot daemon, and Q3 never started) |
| QuakeSpasm | ✅ **3/3** | pak found + `Host_Init` in all three, **0 faults**, no VOID |
| Quake II | ✅ **3/3** | `demo1` + GLES3 refresher in all three, **0 faults**, no VOID |
| vkQuake | ✅ **3/3 valid** | Vulkan + `Host_Init` in all three, **0 faults** (acvq-T1 + acvq2-T1/T2) |

**GATE COMPLETE — all five shipped demo components pass. 18 post-change boots, 1 fault**
(the unrelated ntpclient/malloc one). Against 18 pre-change boots with 0: **1/18 vs 0/18 is
not a signal at all** (Fisher exact p = 1.0).

⚠️ **HOST hazard, not a Pi or build problem. 4 benches killed; here is what actually works.**
The host's low-memory watchdog stops long benches. ❌ **`vm.min_free_kbytes` 66 MB -> 2 GB did
NOT fix it** — I claimed it had after two benches survived, then a 3-trial run died anyway; and
reserving 2 GB arguably makes a watchdog reading *available* memory worse, so it is back to
512 MB. The reliable pattern is **short invocations with the cache dropped between them**:
every 3-trial invocation has died mid-run, every 1- and 2-trial invocation preceded by
`sync; sysctl -w vm.drop_caches=3` has completed. Cause is a transient spike from the
per-trial rootfs rsync, not a real shortage (23 GB free / 21 GB available at rest, top process
RSS 643 MB). **Bench in 1-2 trial invocations, dropping caches before each.** The Pi powered
off cleanly through its exit trap every time; no cycle was left holding the UART.

**ntpclient/malloc-fault rate so far: 1 of 14 post-change boots** (vs 0 of 18 pre-change).
Under a true 1/14 rate, seeing 0 in 18 has probability ~27% — **still not significant**, so
the A/B stays necessary rather than optional.

### 4a-integrity. nfs-fs returns CORRECT BYTES AND SIZES — cache exonerated for corruption

107 STK asset files across path depths 2..8, `cksum` (CRC **and** byte count) on the Pi vs the
host: **107/107 identical** (`-120625-cksumchk`). So the stale-`st_size` mechanism that could
have produced STK's heap corruption is ruled out, and reads are byte-exact.

### 4a-ntpclient. A boot daemon faults — 1 boot in 8, cause open

`/bin/ntpclient` (PID 24, named in the dump) faults at boot in `-115130-acq3-T1`: Instruction
Abort at **pc=0** (a NULL jump) twice, then Data Aborts, the last with `esr=0x92000047` and
**`far == sp`** — a write to an unmapped stack page, i.e. **stack overflow** (the same
signature as the coreutils `SIZE_USTACK` bug). It runs silently on every boot, so it only
appears in a log when it crashes.

⚠️ **I overstated the significance earlier and am correcting it.** I wrote "0 in 18 vs 1 in 3,
so ~0.07%" — the wrong denominator. It is **0/18 pre-change vs 1/8 post-change**; if the true
rate were 1/8, seeing 0 in 18 has probability ~9%. **Not significant.** This may well be a
pre-existing rare intermittent I happened to sample.

What the evidence does support: the build-versions diff between the 18 clean boots and now
shows **only** `coordination` (docs, no code in the image) and `phoenix-rtos-filesystems`
changing — so if it IS new, my nfs-fs work is the only candidate. But integrity is proven
intact, so the mechanism would be **timing**, not corruption: resolution is ~10x faster, so
startup ordering shifted. The `/dev/kbd0` race above is that same signature.

### 4a-malloc. ★★ BOTH crashes are libphoenix malloc metadata corruption — same mechanism

Decoding both dumps against the unstripped binaries puts them in the **same place**, and it is
neither STK nor ntpclient:

| crash | pc | lr | reading |
|---|---|---|---|
| STK | `malloc_chunkSize` (`malloc_dl.c:78`) | `lib_rbInsert` (`rb.c:242`) | tree node is garbage (`0xf122f1c9f290f379`) |
| ntpclient (in psh) | **0x0** | `lib_rbInsert` (`rb.c:242`) | `rbtree->compare` is **NULL** |

`malloc_dl.c` keeps `rbtree_t lbins[32]` for big chunks and inserts with
`lib_rbInsert(&malloc_common.lbins[idx], &chunk->node)` (`:204`); `rb.c:242` is
`c = rbtree->compare(y, z)`, whose comparator `malloc_cmp` is what calls `malloc_chunkSize`.
So **STK walked a corrupted free-chunk tree** and **psh called through a NULL comparator** —
i.e. either `malloc_common` itself was overwritten or `idx` addressed outside `lbins[32]`.
Two unrelated processes, one allocator, one class of fault. ntpclient's later `far == sp`
fault in `vfprintf` is a *consequence* (a smashed frame), not a second bug.

This is in **libphoenix**, shared by everything that runs, so it outranks either symptom.
It also has prior form here (`malloc_dl.c` malloc(0), `SIZE_USTACK`).

**Attribution still open, and I am not going to guess it.** Against my change: integrity is
byte-exact, and 7 of 8 post-change boots are fault-free. For it: only my commits differ from
the 18 clean boots. The remaining gate benches give 9 more boots on this build for free — the
ntpclient rate comes out of them at no extra cost. If the rates stay ambiguous, settle it with
a real A/B: revert the nfs commits, rebuild, re-run the same tests.

**Static analysis narrowed it; two of my own hypotheses are REFUTED, not carried forward:**
- ❌ *`sbins[32]` index overflow writing into `lbins[0].root`.* `malloc_getsidx` can reach 32
  in the abstract, but with the real `chunk_t` layout `offsetof(next)` is 16, so
  `CHUNK_SMALLBIN_MAX_SIZE` is 240 and the max index is **30**. In range. (`malloc_getlidx`
  clamps to 31, and the search loop is `while (idx < 32 …)`.) I had this as a root cause until
  I computed the real layout instead of a mock one.
- ❌ *Uninitialised comparator.* `_libc_init` runs `_atexit_init` and `_errno_init` **before**
  `_malloc_init`, but neither allocates. And `errno_common.tree` — whose `lib_rbInit` is
  `#ifndef __LIBPHOENIX_ARCH_TLS_SUPPORTED` — is never touched on aarch64: that arch defines
  TLS support and *every* use of the tree sits inside the same `#ifndef`.

What survives: libphoenix has only three `lib_rbInsert` callers — malloc's `lbins`,
`posix/idtree.c`, and the compiled-out errno tree. `_malloc_init` is unconditional, so a NULL
comparator means **`malloc_common` (in BSS) was overwritten by a wild write** — genuine
memory corruption in the process, not a missing init.

### 4a-audit. Independent audit of the nfs-fs diff: no client-corrupting bug; 1 leak fixed

A subagent audited `cd30ad2..0e3f337` for memory safety and object lifetime against a fixed
checklist. **No memory-safety bug is introduced by the range** — and nothing in it can corrupt
another process's heap, which together with the byte-exact integrity result removes the
mechanism by which the nfs-fs work could cause the malloc faults. Confirmed clean, each traced
rather than assumed:

- **Dangling `fs->scanNode`: does not occur.** All four `nfs_node_remove` call sites are
  preceded by a `nfs_dirDrop` on the same node, including the rmdir-during-live-scan path
  (which keeps the node via `nfs_node_detachPath` until `mtClose`).
- **Holding a `struct nfsdir *` across other libnfs calls: safe** — it is self-contained
  (own `fh.val`, own `strdup`'d entry list) and `nfs_readdir` is a pure local list walk with
  no I/O. ⚠️ **Conditional on `nfs_set_dircache(nfs, 0)` (`srv.c:435`)**: with the dircache on,
  `nfs_closedir` re-parks the snapshot on a list whose `MAX_DIR_CACHE` eviction frees it.
  Re-audit if that flag is ever flipped. This was the assumption my fix rested on and I had
  only checked one function.
- **`dirent` bound check is exact.** `d_name` is a flexible array member and has alignment 1,
  so `offsetof(d_name) == sizeof(struct dirent)`; the check bounds the write with zero slack
  and no off-by-one, and stays conservative (never unsafe) if that ever diverges.
- **No renew-thread race:** `nfs_ops_renew`/`nfs_reclaim` run inline on the single message-loop
  thread, so there is no unsynchronised access to any of the new fields at all.

**Fixed in `be68a10`** — one real leak plus hardening:
1. **Reclaim leaked the whole snapshot.** My comment claimed it could not be closed through the
   dying context; that is **wrong** for our configuration — with the dircache off,
   `nfs_closedir` reaches `nfs_free_nfsdir`, which ignores its `nfs` argument, and
   `nfs_destroy_context` only frees dirs on the (permanently empty) dircache list. So nothing
   ever freed it: every reclaim stranded a `struct nfsdir` plus every `strdup`'d name in it.
2. `nfs_dirDrop` now clears `scanNode` **before** its early return, so it alone owns that
   pairing instead of depending on an adjacent line elsewhere.
3. A directory's snapshot is released on last close — directories get no filehandle, so
   neither existing release branch covered them and an abandoned scan pinned its listing.
4. + 5. Bound the synthesized `.`/`..` write against the caller's buffer (pre-existing gap,
   reachable only from a malformed `mtReaddir`), and stop persisting a possibly-uninitialised
   handle onto the node.

**Consequence for the plan:** a full revert A/B is now much weaker value — the audit removes
the corruption mechanism, so the remaining candidate is timing.

### 4a-stkram. A one-cycle attribution test for STK, staged (better than a revert A/B)

STK honours `SUPERTUXKART_DATADIR` / `SUPERTUXKART_ASSETS_DIR`
(`stk-code src/io/file_manager.cpp:179,241`) and bash can set them (psh cannot). So extract
the already-staged `stk-assets.tar.gz` (122 MB -> 194 MB) to the dummyfs ramdisk and point STK
at it: **every asset open then bypasses nfs-fs entirely**, leaving only the ELF on NFS.

- still crashes => **nfs-fs is not the cause**; the bug is STK's or the allocator's
- loads clean => the asset path is implicated and attribution flips

One Pi cycle instead of a rebuild-and-revert A/B, and it is the owner's own suggested
experiment (one archive -> ramdisk) reused as a controlled test.

### ★★ 4a-ramtmp. `/ramtmp` IS NOT A RAMDISK AFTER TAKEOVER — several of my numbers were wrong

`df /ramtmp` on the Pi reports **490 GB, 53% used** — the host's disk. The boot log says
`nfs-fs: re-bound /tmp (takeover, tmpfs port=5)`: nfs-fs splices the RAM-backed dummyfs onto
**`/tmp`**, and uses `/ramtmp` only to *resolve* that oid (`srv.c:662-670`). So once NFS owns
"/", **`/ramtmp` is an ordinary directory on the NFS export.** Proof: the export now contains
`ramtmp/{dp,flat,g,stk,t,x}` — every "ramdisk" directory I thought I had created in RAM.

**I retract three conclusions built on that mistake, and it RESOLVES two open anomalies:**
- ❌ *"NFS and the dummyfs ramdisk agree at every depth, so the residual ~157 us/component is
  client-side."* `--mkdepth /ramtmp/dp` vs `/root/dp` compared **NFS against NFS**; identical
  numbers were guaranteed. The residual is real but its attribution is unsupported.
- ✅ *"dummyfs shows the same ~37.5 ms/entry readdir from a separate cause."* There is **no
  separate cause and no second bug** — `/ramtmp/flat` was nfs-fs, so the one `nfs_opendir`
  -per-entry root cause explains **all** of the data. dummyfs was never implicated.
- ✅ *"stkrate's ramdisk row improved 26 s -> 5 s, which an nfs-fs-only change cannot
  explain."* It can: that row was NFS too. Anomaly closed.
- ❌ *"`/ramtmp` caps at 32 MiB, which defeated the ramdisk test and the owner's original
  suggestion."* Wrong twice: this board overrides `DUMMYFS_SIZE_MAX` to **256 MiB**
  (`board_config.h:110`, with a comment about exactly this asset-staging use case), and
  `/ramtmp` is not dummyfs at all. The real failure was writing 194 MB back onto NFS.

**Consequences:** the STK attribution test must use **`/tmp`** (the genuine 256 MiB tmpfs);
`tests/pi-repro/stkram.sh` is being repointed. And plan item #4 "splice `/ramtmp`" is worth
more than "narrow but trivial" — a path that silently looks like a ramdisk and is not has now
produced bad measurements in more than one session.

**STK has now crashed 4 for 4** — close to deterministic, hence tractable.

### ★★★ 4a-stkverdict. nfs-fs EXONERATED — STK crashes identically with assets off NFS

The attribution test finally ran. STK loaded **entirely from the RAM-backed tmpfs** — its own
log confirms `fetched from: '/tmp/assets/supertuxkart/data/'` and
`'/tmp/assets/supertuxkart/stk-assets/'` (`-13*-stktmp3`), with nfs-fs out of the asset path
completely — and **it crashed at exactly the same point**, after the same `kartDirt` warnings:
`Exception #34: PC alignment fault`, `pc = lr = far = 0x047d046e02c304c3`.

**So the nfs-fs work is not responsible for STK's crash.** That, plus byte-exact integrity
(107/107), the independent audit finding nothing that can corrupt another process, and 5/5 gate
components at 3 trials each, closes the attribution question I had left open.

The garbage pointer differs run to run (`0xf122f1c9f290f379` on NFS,
`0x047d046e02c304c3` on tmpfs), which is what reading **uninitialised or freed** memory looks
like — not a stale filesystem value, which would repeat. STK's bug is STK's, the allocator's,
or the GPU stack's, at the kart-mesh stage. **5 crashes for 5 attempts.**

### ★★★ 4a-stknarrow. Localised to the ADVANCED pipeline — and STK RUNS CLEAN without it

Two probes, no rebuild, one Pi cycle each:

| run | faults | SPMeshBuffer | shaders compiled | V3D |
|---|---|---|---|---|
| `stk` (default, advanced) | **crash** | 5 | 34 | 31 |
| `stk --no-graphics` | 0 | 0 | 0 | — |
| `stk --disable-dynamic-lights` | **0** | 5 | **21** | 31 |

- Headless is clean but skips the SP path entirely, so it only rules out asset loading,
  config, GUI/skin, GrandPrix and the allocator in general.
- **`--disable-dynamic-lights` is the decisive one:** the SP mesh path still runs (same 5
  kartDirt warnings) and V3D still renders (same 31 lines), yet **no fault** — and its last
  output is kartDirt x5, which is precisely the known-good signature (STK reaches the menu and
  then renders silently), so it got as far as the crashing runs rather than exiting early.

**The corruption is confined to the advanced pipeline's extra 13 shaders**, named by diffing
the two runs: `sp_skinning.vert`, `sp_skinning_shadow.vert`, `sp_shadow.vert`,
`sp_grass_shadow.vert`, `sp_shadow_alpha_test.frag`, `sp_normal_map.frag`, `sp_unlit.frag`,
`sp_displace.frag`, `sp_road_blending.frag`, `sp_vertical_mapping.frag`,
`sp_tilling_mitigation.frag`, `sp_dynamic_night_bloom.frag`, `colorize.frag`, `white.frag`.
That points at the V3D/Mesa shader compiler, not STK's asset or object code.

⚠️ **This is very likely a REGRESSION, so do not just paper over it.** STK is recorded as
having run *fully lit* in-game on this board (host SSIM 0.991), i.e. the advanced pipeline
worked. Something since then broke it.

❌ **CORRECTION: `--disable-dynamic-lights` does NOT fix STK — my single clean run was luck.**
Benching it gave **1 crash in 3** (`imgstk-T1` faults=2, `imgstk-T2` clean, plus the earlier
clean `stkfixed`). So the flag lowers the rate (2/3 clean vs **0/5** on the default pipeline)
but does not eliminate the fault, and **STK is not a demo component on the strength of it.**

That reframes the bug usefully: the extra 13 shaders are **allocation volume, not the cause**.
`imgstk-T1` decodes to `malloc_chunkSetFooter` (`malloc_dl.c:175`) from **`free`** (`:505`),
writing through a garbage chunk pointer (`far=0x1399112a213df9d0`) — so `chunk->size` was
already corrupt and the footer address computed from it was wild. Together with the earlier
`malloc_cmp`/`lib_rbInsert` and PC-alignment faults, this is **one bug: something overflows a
malloc'd block and overwrites a chunk header**, and the allocator trips on it later, in
whichever of malloc/free/rbtree touches it first. The same corruption hit `ntpclient` once in
18 boots, so it is a latent system-wide bug, not STK-specific.

**★ Instrument built instead of guessed — `libphoenix` (rebuilding).** Reading `free()` showed
why every symptom lands far from the culprit: it takes the size straight from the chunk header
and hands it to `malloc_chunkSetFooter()`, which writes at `chunk + size - sizeof(size_t)`. So
a caller overflowing a block onto the next chunk's header makes **free() itself perform a
second, unbounded write** at an arbitrary address. That single mechanism explains all three
observed shapes (fault in `malloc_cmp` via `lib_rbInsert`; `malloc_chunkSetFooter` writing
through garbage; a jump to a garbage address).

`free()` now validates the header before deriving any write from it — heaps are mmap'd so
page-aligned and page-sized, the chunk must lie inside its heap, and the size must be
8-aligned, >= `CHUNK_MIN_SIZE`, and not run past the heap end. On failure it **reports the
block and leaks it** rather than scribbling on unrelated memory: a leak is recoverable, a wild
write is not, and the message names the block that was actually smashed instead of the innocent
allocation that faults later. Prints via `debug()` + a stack hex formatter, never `printf`,
which would re-enter malloc under its own lock.

Also checked and cleared as a suspect: our `disk_cache_put`/`disk_cache_get` stubs and
`v3d_cache_key_path` (`v3d_phoenix_stubs.c`) — bounded `snprintf` throughout, and the read path
is never even exercised because the cache dir is cleared before every trial.

⚠️ This is a **core** change; the already-gated SD image does not contain it and is unaffected.
Verify on HW, then re-gate before any image carries it.

### ★★★ 4a-stkvalue. The corrupting value is REPRODUCIBLE — and it looks like mesh indices

HW result on the guarded build: **1 clean, 2 crashed of 3**, and the guard **never fired**
(`guard=0` in both crashes), all three runs on the full advanced pipeline (34 shaders).

❌ **Correction to my own inference.** I wrote that the garbage pointer "differs run to run …
which is what reading uninitialised or freed memory looks like". It does **not** differ: both
`mguard2` crashes and the earlier `stktmp3` crash fault with the *identical*
`pc = far = 0x047d046e02c304c3` — **the same constant in 3 runs**. So it is not random junk;
something specific and repeatable is being jumped to, which makes it tractable.

**What that constant looks like:** as four little-endian `uint16`s it is
**1219, 707, 1134, 1149** — small, plausible **mesh/vertex indices**, not a pointer, not ASCII,
not a float pattern. Together with the crash sitting in the SP mesh path (`SPMeshBuffer`), the
reading is: **an index-buffer write overruns onto a neighbouring object's vtable/function
pointer, and the next virtual call jumps into it.**

**★ REFRAME: `pc == lr == far` in every PC-alignment crash — this is STACK corruption.**
That equality is exactly what a `ret` to a corrupted saved return address looks like (the CPU
loads x30 from the stack, jumps, and faults on the misaligned fetch). So the family of
PC-alignment faults is **mesh index data overwriting a saved LR**, not a vtable call — and
that is also why the new `free()` guard never fires: the damage is on the stack, not in
allocator metadata. (The `malloc_cmp` / `malloc_chunkSetFooter` faults are heap metadata, so
either a second victim of the same overflow or a second site; not yet merged into one story.)

**Suspects eliminated this turn, each by reading the code rather than guessing:**
- `u_vbuf` index unrolling — **hard-disabled on Phoenix by our own bug-#3 patch**
  (`u_vbuf.c:1696` `false &&`), so it cannot write indices anywhere. The fix holds.
- our `disk_cache_put`/`get` + `v3d_cache_key_path` stubs — bounded `snprintf`, and the read
  path never runs (cache cleared per trial).
- BO overruns reaching the C++ heap — BOs are `mmap`'d from the contiguous DMA pool
  (`v3d_phoenix_winsys.c`), not malloc'd, so they are not adjacent to program data.
- STK's instanced-data write — the struct is exactly the 44 bytes the buffer is sized for
  (12 position + 16 rotation + 8 scale + tm/hue/skinning), and `s[3]` is hardcoded 0 whereas
  our 4th halfword is `0x047d`.

**Instruments found for the next cycle, in preference order:**
1. **The kernel already has a hardware watchpoint** — `hal_wpTrapLo/Hi` with a handler that
   prints `watchpoint hit - halting (pc=writer, far=watched addr)` **plus a backtrace**
   (`hal/aarch64/exceptions.c:319-327`). That names the writer outright, once there is an
   address to watch.
2. **EL0 faults get no backtrace today** — `hal_exceptionsBacktrace` is documented and wired
   for *kernel* exceptions only (checked: 0 backtrace lines in the STK crash logs).

**★ Built instead (kernel, rebuilding): a bounded stack window on user faults.**
`hal/aarch64/exceptions.c` now appends ~128 bytes from `sp` to the dump for the two EL0 aborts
plus PC/SP-alignment. A **flat read**, not a frame-pointer walk — a crash of this shape has
already proven x29 untrustworthy, and a nested EL1 fault chasing it would turn every userspace
crash into a board reset. Clipped to `sp`'s page so a read starting on a mapped page cannot
leave it, and printed from its own buffer since the caller's is sized for registers only.
Kept **aarch64-local**: adding a no-op to the other 11 hal arches to satisfy a generic header
would have been untestable churn, so it hooks the per-arch dump instead.

The point is the words next to `sp`: that is where the data which overwrote the saved return
address is still sitting, and its extent says how far the overflow ran.

### ★★★★★ 4a-ROOTCAUSE. STK: a 44,100-byte stack array on a 4 KB thread stack

**ROOT-CAUSED, with byte-exact proof, and it is NOT what I concluded below — see the retraction.**

**The writer:** `music_ogg.cpp:329` (stk-code 1.4)

```cpp
char pcm[m_buffer_size];   // m_buffer_size = 11025*4 = 44,100 BYTES, on the stack
... ov_read(&m_oggStream, pcm + size, m_buffer_size - size, ...);
```

**The reason it is fatal here:** libphoenix's default thread stack is **4096 bytes** —
`pthread.c:135` uses `ALIGN(PTHREAD_STACK_MIN, PAGE_SIZE)` with `PTHREAD_STACK_MIN = 256`
(`limits.h:53`) — and the same default sets **`guardsize = 0`**, so `pthread.c:296` skips the
`mprotect(PROT_NONE)` and there is **no guard page**. `std::thread` passes a NULL attr
(`gthr-posix.h:709`), so STK's SFX thread (`sfx_manager.cpp:98`) gets that default. A 44,100-byte
frame therefore puts `sp` ~40 KB below the thread's own 4 KB region, and `ov_read` fills PCM
upward across ~11 pages of *whatever is adjacent* — silently, because stacks are plain
`mmap()`s with no guard.

**That merges the two victim classes the log had left open as possibly-separate bugs:** land on
a neighbouring thread's stack and you get `pc == lr == far` with a page-aligned `fp` (and a
*different* victim thread each run — 69, 72, 80, 81); land on heap pages and you get the
`malloc_chunkSetFooter` / `malloc_cmp`-via-`lib_rbInsert` faults. **One bug.**

**Proof (independently re-run by me, `tests/pi-repro/stkpcmproof.sh`):** decoding the staged
`menutheme.ogg` to raw s16le locates the **entire 128-byte stack window** the kernel dumped at
PCM offset **36040**; corrupt `lr 0xf4b5f41d…` at **36008** (= window − 32, matching an
`ldp x29,x30,[sp],#0x20` epilogue); corrupt `lr 0x047d046e…` at **80108** — and the two "constants"
are exactly **44100 bytes apart**, i.e. `m_buffer_size`, the same intra-chunk offset in
consecutive `streamIntoBuffer()` reads. That is why the value was reproducible.

❌ **RETRACTION — my "mesh indices" reading was wrong, and I had called it PROVEN.** The bytes
are **16-bit PCM audio**, not indices. Two things should have stopped me: the 64 halfwords
contain **zero repeated values**, which is mechanically impossible for a triangle index list
(every vertex recurs 4–6×), and the largest kart mesh has 3016 vertices so index 8132 cannot
exist. I read "ascending with local variation" as an index buffer and did not test the reading.
A sweep of all 1234 `.spm` assets for the offset-invariant delta signature returned **0 hits**.

**FIX (rebuilding): `phoenix-rtos-ports 4eed9f0`**, `supertuxkart/patches/0011` — the PCM
staging buffer becomes a heap `std::vector` instead of a 44 KiB stack frame. Generated as a
real diff against the pristine tarball copy (not hand-written hunks), and the patch header
carries the byte-exact proof so the next reader does not have to re-derive it.

⚠️ **THE SYSTEMIC HAZARD IS THE BIGGER ITEM, and it is an owner call.** libphoenix defaulting a
thread stack to `ALIGN(PTHREAD_STACK_MIN, PAGE_SIZE)` = **4 KiB** with **`guardsize = 0`** means
*any* port that puts a sizeable buffer on a thread stack gets **silent cross-thread memory
corruption** instead of a fault. STK is simply the one that got caught. Two candidate changes,
deliberately NOT made unilaterally because libphoenix also serves MCU targets (armv7m/armv8m)
where both cost real memory:
- a **guard page by default** (`guardsize = PAGE_SIZE`) — turns this whole class of bug into an
  immediate, obvious fault instead of corruption 40 KiB away;
- a **larger default stacksize on the MMU targets only** (aarch64 etc.) — `PTHREAD_STACK_MIN` is
  a POSIX *floor*, not a sensible default (glibc uses 8 MiB).

⚠️ Also not explained by the root cause, and **not load-bearing**: the `--no-graphics` /
`--disable-dynamic-lights` rate differences. Best read as page-layout luck — whether the 40 KB
excursion lands on a live thread stack, live heap, or dead pages — not causation.

### 4a-stkproof. (superseded) The stack window: 128 bytes of non-index data

`-stkdump-T1/T2` (both crashed, both dumped). The whole window from `sp` is index data:

```
sp=00000000090c1f30
  0243014800eb0118   056d0563049f035d   0a4c089e075d0617   0ff60e780d0c0be4
  12ce12961210111c   13261314138b1362   12eb127b12aa131c   187416e415b4145e
  1b671b0d1a451970   1a981bdf1c4c1bd6   1c611b4e1b251a84   1f5e1fc41f301de7
  187219f21c701e8b   1823164916a217a4   1aec1c3e1a971969   171f178218c81959
```

Read as little-endian `uint16` these look like plausible indices — which is exactly the trap.
They are **PCM samples** (see the root cause above). The window contains no saved registers, no
pointers and no ASCII, which was the sound part of the inference; "therefore indices" was not.

**So the fault is proven, not inferred: an index array is being written onto the STACK, running
at least 128 bytes past whatever it was meant to fill, and taking a saved return address with
it** (which is why `pc == lr == far` and why the corrupted value decoded as four indices).
The hypothesis chain — reproducible constant -> indices -> `ret` to a corrupt LR -> stack
overflow — is now closed by direct observation.

Remaining question is narrow and mechanical: **which writer.** A stack buffer sized for
something smaller than the index count, in STK's SP mesh path or in our V3D/Mesa draw path.
A subagent is sweeping all three trees (stk-code SP, our Mesa fork's v3d + gallium aux, and the
hand-written Phoenix V3D glue) for a fixed-size stack array filled from a mesh/draw count.

**Complementary evidence being gathered in parallel (kernel, rebuilding):** the first window
dumped 128 bytes *upward* from `sp`, which proved the overrun but not the array's size — above
`sp` is only the overflow's tail. The window now spans **384 bytes starting 128 bytes below
`sp`** (still clipped to `sp`'s page, with the word at `sp` marked), so the buffer the data came
from is visible too. **The distance between the first and last index word gives the size of the
array that ran over, which is what names it** — useful whether the sweep finds a candidate
(it confirms) or not (it becomes the primary lead).

**The stack-dump diagnostic itself is clean and worth keeping:** desktop **2/2** and Quake III
**2/2** on the diagnostic kernel, 0 faults, and **0 spurious dumps** — it fires only on the
fault classes it targets, so it adds no log noise. That makes it shippable as a permanent
improvement for a project that debugs over UART, once it is gated properly.

Also tried and it came up empty: searching every STK asset for the exact corrupting byte
sequence `c3 04 c3 02 6e 04 7d 04` — **0 files**. Expected in hindsight: the SP loader *adds a
vertex offset* to indices when merging mesh buffers (`sp_mesh_buffer.hpp:150`), so the
in-memory values are adjusted and will never match file bytes. Recorded so it is not retried.

**Ruled out by reading, so the next step is instrumentation not speculation:** the
`kartDirt shader is missing, fallback to solid` warning is a red herring —
`sp_mesh_buffer.cpp:490` falls back to the `solid` shader, `data/shaders/sps_00_solid.xml`
exists, so `m_shaders[0]` is a valid pointer and not null; and nothing in `data/` defines
`kartDirt` at all, which is why those five warnings appear in known-good runs too. The two
faults are also *different* shapes (garbage code pointer vs corrupted malloc tree), i.e.
generic corruption rather than one bad deref. Chasing it needs a real instrument — inject
libphoenix's exported-but-dead `malloc_test()` around the kart-mesh stage, or link `libdbg`
for an in-process backtrace — both of which require an STK rebuild, so they wait until the
gate finishes and the Pi/build resources are free.

### 4a-stkblocked. Getting there cost THREE separate environment bugs

Each attempt hit a different real defect, none of them STK's:
1. **`/ramtmp` is NFS, not RAM** (above) — fixed by targeting `/tmp`, the genuine 256 MiB
   tmpfs (`df /tmp` = 262144 1K-blocks, confirming `board_config.h:110`).
2. **The Pi's `tar` rejects GNU LongLink** (`typeflag 0x4c`, names >100 chars): it aborted at
   162 of 194 MB, STK saw an incomplete tree and silently fell back to NFS. Worked around with
   `cp -r` — the full 194 MB then copied fine (tmpfs 0 -> 188 MB used, 74%, both `cp` rc=0).
3. ❌ **"Environment variables do not reach an exec'd program" — RETRACTED, I was wrong.**
   Env vars survive exec perfectly. I traced the whole chain instead of inferring from one
   symptom: kernel `proc_execve` copies `envp` and `process_exec` pushes it
   (`process.c:1204,1249`), the aarch64 crt0 loads all four words
   (`ldp x0,x1 / ldp x2,x3`) in exactly the order `hal_stackPutArgs` lays them down, and
   `_startc` assigns `environ = env` (`crt0-common.c:79`). All correct.

   **The real cause: `/bin/stk` is a launcher** (`tools/supertuxkart-port/stk-launcher.c`,
   which exists precisely because psh cannot set env vars) and it called
   `setenv(..., 1)` — **overwrite=1** — hardcoding `/usr/share/supertuxkart` over whatever the
   caller exported. My variable arrived and was then thrown away.

   **Fixed:** the three `setenv` calls now use **overwrite=0**, so they are defaults rather
   than mandates and a caller can stage assets elsewhere — which is exactly what
   `board_config.h`'s raised `DUMMYFS_SIZE_MAX` exists for. The launcher also now prints the
   three resolved paths, so a future run cannot silently disagree with its own configuration.

⚠️ Also re-learned the hard way: `bash -c 'export X=1; env'` from psh is mangled because **psh
does not strip quotes** — a documented footgun that has cost cycles before. Env checks must go
in a script file (`/root/envtest.sh`, staged).

Also noted: libphoenix exports `malloc_test()` (`malloc_dl.c:680`) — a full bin/heap
consistency walk — which is **dead code, called from nowhere**. It is the tool for catching
this corruption at the moment it happens, but only in a process that calls it, so using it on
STK means rebuilding STK. Held until attribution is settled.

⚠️ **`/dev/kbd0` + `/dev/mouse0` open failed in T2 only** (1 of 3) — X started before USB
enumeration finished, so that trial had no input. Benign for a bench (nothing types) but it
matters for a **screen recording the owner wants to drive**: it is a start-ordering race, not
a missing driver (both devices were created earlier in the same boot). Worth an ordering fix
once the gate is settled; not widening scope now.

**Next target, analysed but deliberately NOT implemented until the image is gated:** an
`open()` is two RPCs — `nfs_ops_open`'s `force=1` re-stat plus the `nfs_open` itself. That
re-stat is redundant: the path resolution immediately preceding every open already stat'd the
same file microseconds earlier, so `force=1` buys at most the 100 ms the attribute cache
already accepts everywhere else. Dropping it to `force=0` removes ~1.4 ms of the 4 698 us
(~30%) and makes the staleness contract uniform instead of one op opting out. The residual
risk is a file redeployed on the server and exec'd within 100 ms, which a multi-second NFS
restage cannot hit. Do it AFTER the bench and the image, since it invalidates both.

**Next, in this order:** one cycle checking readdir **correctness first** (exact entry counts:
649 for `stk-assets/textures`, 45 for a purpose-built dir of 40 files + 2 subdirs + 3
symlinks, plus a duplicate check — a mis-stepped cookie would surface as a missing texture,
not an error), then the timings; then the 5-game + desktop bench; then a new image + manifest.
`restore-integration-state.sh` if the Pi does not boot.

`readdir` did not move (39 ms/file) and is **the same on a ramdisk** (37.5 ms/file over 548
files), so it is not a filesystem or network cost: for 150 files it is 5.9 s of the 8.2 s
total. Both servers hand back **one dirent per message keyed by a cumulative offset/cookie**
(`nfs_ops_readdir`, and dummyfs likewise), which re-walks the directory from the start on
every call — quadratic in directory size. Not yet verified; the test is per-entry cost vs
directory size (a 20-entry dir should cost ~1 ms/entry if quadratic, ~38 ms if not).

⚠️ **One unexplained EL0 Data Abort, recorded not dismissed.** The first run
(`-110907-attrcache`) faulted repeatedly in fileperf's `phMutexLock`
(`pc=0x41436c esr=0x92000007` read fault, `far=0x549028` — **outside every segment** of the
binary, whose RW LOAD ends at 0x428b20) on the flat-dir mode, producing no output. The
identical command then ran clean **twice** in the next boot, and the same mode over a ramdisk
(no nfs-fs code at all) is also clean, so it is not attributable to the attribute cache. It
was the first program exec'd over NFS after boot. Left open — if it recurs there will be a
second data point.

