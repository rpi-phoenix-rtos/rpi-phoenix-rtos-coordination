# C9: the ntpclient faults, re-read from the dumps (2026-09-27)

**Short version.** C9 is not a new bug. All five archived `/bin/ntpclient` deaths come down to one
observation: a page of ntpclient's writable image, or a heap mapping, does not hold what ntpclient
wrote to it. Four of the five are C3 or its known relatives. The new sighting (09-27) carries the
clearest evidence so far: `environ` read back **the bytes the ELF file has at that offset**. That
is the backing object's page, not the process's private copy. The "stack overflow" shape the C9 row
lists is a follow-on fault, not a shape of its own. `env.c` is not implicated. No fix is proposed,
because nothing is reproduced; a fork()+exec storm is ready to change that.

Status language: **enriched, n = 5, all in one process; mechanism not reproduced.**

## 1. The five dumps

Found with `grep -a -c 'process "/bin/ntpclient"'` over `artifacts/rpi4b-uart/*.log`. The kernel
prints every EL0 dump twice, so the counts are halved. The strings below come from reading the
stack-dump words as little-endian ASCII.

| Date | Log | First fault | What follows | Class |
|---|---|---|---|---|
| 09-07 | `…0907-115130-acq3-T1` | **Instruction abort at `pc=0`**, `lr=0x420cbc`, `x7`="ntp.conf": a call through a NULL function pointer while the applet reads `/etc/ntp.conf` | the signal path's `fprintf(stderr)` faults at `far=0x30` (NULL `stderr`), then recurses until it writes below `sp` (`far=0x7fffffefc0`) | C3: zero where libc wrote |
| 09-11 | `…0911-153023-g-relF3-q2` | **Store to NULL** (`far=0`, WnR) at start-up: `sp=0x7fffffff20` with only argv/envp above it; `x20=0x435000` is a page base of the RW segment | the same store repeats (2070 aborts, the "ntpclient-null-loop") | `atexit-null-head`: a `.data` pointer read NULL. Already attributed to `object_fetchCluster` zero-filling a short NFS read (W37) |
| 09-15 | `…0915-001244-opbench-T9` | `far=0x30`, stack text **`psh: ntpclient: unknown command`**: the applet list and `stderr` are both zero, and they share one `.bss` page | `libphoenix: NULL handler for signal 11, applying default disposition` → `far=0x30` again → stack overflow (`far=0x7fffffef20`) | C3 (its defining sighting) |
| 09-25 | `…0925-225946-c1hpa01` | `far=0x5000`, inside the C1-hunt guard `malloc_heapSizeValid` (libphoenix `819e461`) | none | Our own instrument faulted, and is fixed. The underlying anomaly is real, though: `live[]` named a heap at `0x5000` that the page tables say is not mapped |
| 09-27 | `…0927-061949-pollwake-qsdrm` | `far=0x0079747265706f72` in `_env_find`, called from `getenv("C1_HEAP_TRACE_ALL")` (`x1=0x11`=17=its length), called from `malloc_caTraceOn` ← `_malloc_heapAlloc` ← `malloc` | one line before it: **`malloc: mmap returned a region OVERLAPPING a live heap`, `new = live = 0x2000`, `nsize = lsize = 0x1000`**. This is the first time that guard has fired anywhere in the archive | **see §2** |

⚠ **Symbol caveat.** No archived psh matches any of these runs. The 09-27 run used libphoenix
`d40050c`; the only unstripped `prog/psh` left is the 11:45 build (`bf35aaf`), and `atexit.c` alone
moved ~400 lines between them. Against that binary, `lr` resolves to `setenv:227`. That is drift:
the applet never runs the only `setenv` caller (`psh_pshapp`), and `getenv` sits right next to
`setenv` in `env.c`. The call chain above rests on register contents, which do not drift: `x1=17`,
`x2 = 0x438000` (the `adrp` page base `_env_find` loads `environ` from, inferred from today's code shape), and the code words on the stack. The 09-15 and 09-25
symbols are the ones recorded at the time, against identity-checked binaries.

## 2. The 09-27 fingerprint: `environ` held the ELF file's own bytes

- `0x0079747265706f72` is `roperty\0` in little-endian: the tail of the section name
  `.note.gnu.property` in the stripped psh's `.shstrtab`. That table is not loaded into memory; the
  file stores it right after `.data` (layout: `.data` | `.comment` "GCC: (GNU) 16.2.0" |
  `.shstrtab`).
- `environ` is in the `.bss` tail of the **last file-backed page** of the RW `PT_LOAD`
  (`crt0-common.c:73`, `bss_start+0x40`). The kernel zeroes that tail at `process.c:724`, and crt0
  then writes `envp` into it.
- The bytes at `environ`'s file offset depend on how `_edata` is aligned, which leaves two
  candidates. In today's build, `_edata ≡ 0xc (mod 16)` places `environ` on `te.gnu.p`
  (`xxd -s 0x38b60`). If `_edata ≡ 4`, it lands exactly on `roperty\0` (`xxd -s 0x38b68`). The
  faulting run read the second candidate, byte for byte. A random stray write matching the file
  exactly at the address that was read is not credible.
- That page's `.data` also holds `caTrace.5` (`0x437b08` today), the value `malloc_caTraceOn`
  caches. Its file value is `-1`, which is why `getenv` ran at all on a heap creation other than the
  first. **One page, two anomalies:** the page ntpclient was reading was the pristine file page.
  The kernel's zeroing, crt0's store and malloc's cache update were all missing from it.
- The OVERLAPPING line fits the same picture. `malloc_common.live[]` (anonymous `.bss`, a different
  page) still records a heap at `0x2000`/`0x1000`, yet the kernel handed out `0x2000` again. The
  kernel's map no longer had it.

So in one process, within a few milliseconds, two things happened. A private `.data` page showed its
backing object's content, and a live heap mapping disappeared from the map. C3 shows the anonymous
counterpart (an anonymous `.bss` page reading zero), and 09-25 shows another heap mapping
disappearing.

## 3. Why only ntpclient: a selection effect worth stating

- `psh_clockSync()` (`pshapp.c:1652`) starts ntpclient with **`fork()`**, then waits (`access()` +
  `sleep`), then calls `execl`. Every other launch in the boot uses `vfork()`: `sysexec`,
  `runfile`, and psh's own command path (`pshapp.c:1401`).
- In the kernel, a `fork()`ed child reaches `process_execve()` with `spawn->parent == NULL`. That
  is the **only** path that runs `vm_mapDestroy(process->map)` and then, in `process_exec()`,
  `vm_mapCreate()` **on the same struct**. That map had also shared amaps copy-on-write with the
  parent (`vm_mapCopy`). A vfork child borrowed the parent's map, and exec just gives it a fresh one.
- The storms on record (~4200 launches, W37 `spawn-storm`) were **all vfork**. Nothing has ever
  stress-tested fork()+exec.

This explains the membership. It does not prove the mechanism.

## 4. Hypotheses, ranked

1. **M1: after a fork()+exec, part of the new address space is torn down again.** Map entries or
   their anonymous pages go away, and the next access re-faults them from the backing store: a file
   page for `.data`, a zero page for `.bss`, nothing for a heap. **Favoured:** it is the only
   reading that covers every observation (09-27's two lines, C3's zeros, 09-25's vanished `0x5000`).
   It also matches the §3 selection. Places to read, not yet read: `vm_mapDestroy` and
   `amap_putanons` (a dangling `anon_t*` is already on record in
   `done/2026-09-15-w38-fixes-detail.md`), plus anything that still holds the old `process->map`
   across the destroy/create in place.
2. **M2: a physical page freed while still mapped, then reused.** Every psh-image process has
   `live[0] = 0x2000/0x1000` at the same in-page offset, so a recycled `malloc_common` page would
   look exactly like 09-27's OVERLAPPING line. But M2 alone does not explain `mmap` handing out
   `0x2000` again, or `environ` showing *file* bytes. It needs M1 as well, or a second defect.
3. **M3: a stale TLB entry on another core** (a COW split whose invalidation some core misses, or
   one reached by migration). ASID allocation broadcasts (`tlbi aside1is`), and the only raw
   local `tlbi` (`_pmap_mapScratch`, `pmap.c:199`) invalidates on the CPU that uses it before use,
   so nothing points here yet. Kept as a candidate; no evidence either way.
4. ⛔ **`env.c` defect or a concurrent writer of `environ`: not supported.** ntpclient never calls
   `setenv`/`putenv`, and nothing else in the process writes `environ` after crt0. `env.c` cannot
   produce a pointer equal to the ELF's section-name bytes. `getenv` inside malloc
   (`malloc_caTraceOn`) only reads.
5. ⛔ **vfork address-space sharing: ruled out.** ntpclient is started with `fork()`, not
   `vfork()`, and in any case exec gives the child a new map before the applet runs.
6. ⓘ **The "stack overflow" shape (09-07, 09-15)** is the libphoenix signal path printing
   `NULL handler for signal 11` through a NULL `stderr` and recursing. That message no longer exists
   in `sources/libphoenix` (it arrived in `da69de7` and went with the upstream signal rework merged
   on 09-17), so this follow-on cannot recur on the current tree. Nothing to fix.

## 5. What was done

- No library fix. None of the code read here has a defect that produces these faults. A host test
  that "fails before, passes after" on a non-bug would be a fabricated result.
- **Reproducer prepared.** phoenix-rtos-tests branch `c9/spawnstorm-fork` (`a5ee387`, pushed to
  `publish`) adds `spawn-storm -f`. It launches with `fork()`. Like psh, the child writes `.bss`,
  the heap and the stack, and calls `access()`, before it execs. It cross-compiles and links with
  `-Wall -Werror`. It has **not** been run on hardware.

## 6. Pre-registered confirmation (written before running)

One boot confirms nothing: ntpclient faults about once in ~650 boots: 5 faults across the ~3 300 archived boots since 09-07 whose log shows ntpclient reporting. What can
settle it is a **rate**.

**Grading lines, applied to every future boot log:**
`process "/bin/ntpclient"` (halve the dump count) together with the decoded first `far`: `0x30` =
NULL `stderr`, a file word = §2, anything else = new. Also grade `mmap returned a region OVERLAPPING
a live heap`, `live[] entry is NOT MAPPED` / `lubase`, and `applet list is EMPTY`.

**Storm (one cycle per arm, same image):**
1. Build `spawnstorm` from `c9/spawnstorm-fork` and stage it as `/bin/spawn-storm-f` on the live
   export. Do not overwrite `/bin/spawn-storm`.
2. Arm F: `./scripts/test-cycle-psh-interact.sh --label c9stormF --idle-secs 60 -- '/bin/spawn-storm-f -f 300 /bin/printenv PATH'`.
   Arm V (control): the same command without `-f`, `--label c9stormV`. `printenv` is the psh binary
   under an applet name; it reads `environ` and needs the applet list, so it exercises both
   fingerprints.
3. For each arm, grade: the `spawn-storm: DONE <ok> ok, <failed> failed` line; every
   `spawn-storm: pid … BAD status` line; any `process "/bin/printenv"` dump; `unknown command` or
   `applet list is EMPTY`; any printed PATH value other than `/bin:/usr/bin:/sbin:/usr/sbin`; and
   the OVERLAPPING / NOT MAPPED lines.

**Reading rules, fixed in advance:**
- **F fires and V does not:** fork()+exec is the trigger. M1/M2 move to the exec-in-place path, and
  the next step is a kernel read of `process_execve` → `vm_mapDestroy` → `vm_mapCreate`.
- **Both arms fire:** the trigger is not fork-specific. ntpclient is then the victim only by
  timing. Re-rank with M3 up.
- **Both clean at 300:** that bounds the per-launch rate below ~1 %. It says nothing about the
  per-boot rate (~1 in 650) and is **not** a negative. The next step is F with `-p 4` next to a
  live GPU app, which is the condition every field sighting had.

## 7. Proposed register text

Merge C9 into C3. They are the same observation, and C9's "three shapes" do not survive the dumps:

> **C3 | A page of `ntpclient`'s writable image, or a heap mapping, does not hold what the process
> wrote.** Seen as `stderr` and the applet list reading zero (`far=0x30`, 09-07, 09-15), a `.data`
> pointer reading NULL (09-11), a heap mapping gone from the map (09-25; 09-27 `mmap` re-issued
> live `0x2000`), and `environ` reading the ELF file's own bytes at that offset (`roperty\0`,
> 09-27). ntpclient is the only process started by `fork()`+exec; everything else uses vfork. |
> **Open, n = 5 in one process (~1 in 650 boots), not reproduced.** Leading reading: after a
> fork()+exec, part of the new address space is torn down and re-faulted from backing store.
> Enriched, not proven. A fork-mode storm (`spawn-storm -f`, tests `c9/spawnstorm-fork`) is ready
> to measure it. Not C1's `0x8000000x` signature. [details](misc/2026-09-27-c9-ntpclient-faults.md) |
