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
- The bytes at `environ`'s file offset depend on how `_edata` is aligned (`mod 16` ∈ {0, 4, 8, c}),
  which leaves at most four candidate 8-byte words. In today's build, `_edata ≡ 0xc` places
  `environ` on `te.gnu.p` (`xxd -s 0x38b60`). With `_edata ≡ 4` it lands exactly on `roperty\0`
  (`xxd -s 0x38b68`). The faulting run read one of those four candidates, byte for byte. A random
  stray write matching the file exactly at the address that was read is not credible.
- That page's `.data` also holds `caTrace.5` (`0x437b08` today), the value `malloc_caTraceOn`
  caches. Its file value is `-1`, which is why `getenv` ran at all on a heap creation other than the
  first. **One page, two anomalies:** the page ntpclient was reading was the pristine file page.
  The kernel's zeroing, crt0's store and malloc's cache update were all missing from it.
- The OVERLAPPING line fits the same picture. `malloc_common.live[]` (anonymous `.bss`, a different
  page) still records a heap at `0x2000`/`0x1000`, yet the kernel handed out `0x2000` again. The
  kernel's map no longer had it.
  ⚠ **Which process printed it is inferred.** The line carries no process tag. Two emitters fit:
  **(a)** the exec'd ntpclient, which is the reading above and indicts the exec path; **(b)** the
  *pre-exec* fork child. That child is a COW copy of psh, so its inherited `live[]` legitimately
  says `0x2000` is live, and its `access()` loop can allocate. If (b), the child's copied map
  lacked the parent's heap, which indicts `vm_mapCopy` rather than exec teardown. Either way it is
  pid 24's fork()+exec lifecycle.

So around pid 24's fork()+exec, two things happened within a few milliseconds. A private `.data`
page showed its backing object's content, and a live heap mapping went missing from a map (§2 (a)
or (b)). C3 shows the anonymous counterpart (an anonymous `.bss` page reading zero), and 09-25 shows
another heap mapping disappearing.

## 3. Why only ntpclient: a selection effect worth stating

- `psh_clockSync()` (`pshapp.c:1652`) starts ntpclient with **`fork()`**, then waits (`access()` +
  `sleep`), then calls `execl`. It is the only fork()+exec **on the psh boot path**; everything
  else psh launches uses `vfork()` (`sysexec`, `runfile`, psh's own command path at
  `pshapp.c:1401`). This is not a system-wide claim: bash and some X clients fork too.
- Archive check of the selection: every `far=0x30` dump in `artifacts/rpi4b-uart/*.log`, sorted by
  process name. There are 3 in ntpclient (09-07, 09-15), 6 in `/bin/test-libc-unix-socket`, all on
  09-02, and 1 in `quake3e` (09-12, caught by its own handler). The unix-socket ones are
  **fork()ed test children** from the week the EL1 user-copy COW bug was fixed
  (`project_el1_usercopy_fault_prot_user`). That fits "fork children lose page contents", but they
  are not attributed here and not counted. Apart from the synthetic `/bin/notanapplet` test, no
  process other than ntpclient ever printed `unknown command` or `applet list is EMPTY`.
- In the kernel, a `fork()`ed child reaches `process_execve()` with `spawn->parent == NULL`. That
  is the **only** path that runs `vm_mapDestroy(process->map)` and then, in `process_exec()`,
  `vm_mapCreate()` **on the same struct**. That map had also shared amaps copy-on-write with the
  parent (`vm_mapCopy`). A vfork child borrowed the parent's map, and exec just gives it a fresh one.
  ↩ **Corrected (§8.3):** on MMU builds the copy is not copy-on-write. `process->lazy` is 0
  (`process.c:227`), so `vm_mapCopy` skips `remap_readonly` (`map.c:1422-1427`) and copies every
  page into the child up front (`map.c:1430-1439` → `amap_page`, `amap.c:280-316`). There are no
  COW faults after a fork, and no amap pages are shared with the parent.
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
   `done/2026-09-15-w38-fixes-detail.md`), anything that still holds the old `process->map` across
   the destroy/create in place, and `vm_mapCopy` (for emitter (b) in §2).
   ↩ This is not re-opening the W38 "clean negative on every kernel mapping path". That review
   covered how an exec'd image is populated (eager `process_load`, fault serialisation, no
   pageout). It did not cover the `spawn->parent == NULL` path, which destroys and re-creates the
   same `process->map`, or `vm_mapCopy`. Those are exactly what the fork()+exec selection points at.
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

**Power.** If one fork()+exec launch fails as often as one ntpclient boot (~1/650), then 300
launches give a ~37 % chance of seeing one event and 1500 give ~90 %. So each arm is **3 cycles of
500 launches**. That split is needed anyway: the 10-minute Bash cap and the cycle's
`--max-cmd-secs` both limit how long one cycle can run.

**Storm (same image for both arms):**
1. Build `spawnstorm` from `c9/spawnstorm-fork` and stage it as `/bin/spawn-storm-f` on the live
   export. Do not overwrite `/bin/spawn-storm`.
2. Arm F, three times (`c9stormF1`..`F3`):
   `./scripts/test-cycle-psh-interact.sh --label c9stormF1 --idle-secs 60 --max-cmd-secs 420 -- '/bin/spawn-storm-f -f 500 /bin/printenv PATH'`
   (Bash `timeout` 600000). Arm V (control) is the same without `-f`, `c9stormV1`..`V3`.
   `printenv` is the psh binary under an applet name; it reads `environ` and needs the applet list,
   so it exercises both fingerprints.
3. For each arm, grade: the `spawn-storm: DONE <ok> ok, <failed> failed` line; every
   `spawn-storm: pid … BAD status` line; any `process "/bin/printenv"` dump; `unknown command` or
   `applet list is EMPTY`; any printed PATH value other than `/bin:/usr/bin:/sbin:/usr/sbin`; and
   the OVERLAPPING / NOT MAPPED lines.

**Reading rules, fixed in advance:**
- **F fires and V does not:** fork()+exec is the trigger. M1/M2 move to the exec-in-place path, and
  the next step is a kernel read of `process_execve` → `vm_mapDestroy` → `vm_mapCreate`, and of
  `vm_mapCopy`. A failure before exec (a BAD status with no child output) points at the copy; a
  failure after it points at the exec.
- **Both arms fire:** the trigger is not fork-specific. ntpclient is then the victim only by
  timing. Re-rank with M3 up.
- **Both clean at 1500:** that bounds the per-launch rate below ~0.2 % (95 %). It is **not** a
  negative for ntpclient, because the storm differs from the field in two ways. The field child
  lingers for seconds in its `access()`/`sleep` loop while `/bin` is not yet mounted, where the
  storm child execs at once; and every field sighting had other load live. Next knobs, in order: a
  child-side delay under `-f`, then `-p 4` next to a live GPU app.

## 7. Proposed register text

Merge C9 into C3. They are the same observation, and C9's "three shapes" do not survive the dumps:

> **C3 | A page of `ntpclient`'s writable image, or a heap mapping, does not hold what the process
> wrote.** Seen as `stderr` and the applet list reading zero (`far=0x30`, 09-07, 09-15), a `.data`
> pointer reading NULL (09-11), a heap mapping gone from the map (09-25; 09-27 `mmap` re-issued
> live `0x2000`), and `environ` reading the ELF file's own bytes at that offset (`roperty\0`,
> 09-27). ntpclient is the only `fork()`+exec on the psh boot path; everything psh launches uses
> vfork. |
> **Open, n = 5 in one process (~1 in 650 boots), not reproduced.** Leading reading: after a
> fork()+exec, part of the new address space is torn down and re-faulted from backing store.
> Enriched, not proven. A fork-mode storm (`spawn-storm -f`, tests `c9/spawnstorm-fork`) is ready
> to measure it. Not C1's `0x8000000x` signature. [details](misc/2026-09-27-c9-ntpclient-faults.md) |

## 8. Fork storm hang (F1–F3)

### 8.1 Evidence

The §6 storm ran on 09-27 (kernel `fb8b66ee`, build 14; userspace build 15; tests `c4b23a8` image,
`/bin/spawn-storm-f` from `a5ee387`). **Arm F hung in 3 of 3 runs; arm V finished 3 of 3.**

| Run | Log (`artifacts/rpi4b-uart/rpi4b-uart-20260927-…`) | Launches before the stop | Last bytes on the UART | Last thing on HDMI |
|---|---|---|---|---|
| F1 | `134654-c9stormF1` | 17 clean | `spawn-storm: launch 1` (the parent's line for launch 18, cut after one digit) | the same 21 characters, cursor after the `1` (`hdmi/20260927-134853-c9stormF1-final.png`) |
| F2 | `134856-c9stormF2` | 190 clean | `launch 191/500`, then `/bin:/usr/bin:/s`: **16 bytes** of printenv's line for launch 191 | `launch 191/500` and **no** PATH bytes (`…-135058-c9stormF2-final.png`) |
| F3 | `135100-c9stormF3` | 106 clean | `launch 107/500`, then `/bin:/u` (7 bytes of launch 107's PATH line) | `launch 107/500` and no PATH bytes (last tick `…-135300-c9stormF3-tick.png`) |
| V1–V3 | `135302`, `135517`, `135731` | 500 / 500 / 500 | `spawn-storm: DONE 500 ok, 0 failed, slowest 29-30 ms` | — |

F: 3 stops in 315 launches. V: 0 in 1500. Fisher exact p ≈ 0.005. None of the six logs has a fault
dump or a `BAD status` line, and the kernel printed nothing either.

⚠ **This was not the pre-registered signature.** §6 defined a fire as a `BAD status`, a dump, a
wrong PATH value, or the OVERLAPPING / NOT MAPPED lines. None of those happened. A hang was not on
the list. Status language: **enriched, n = 3 runs (3 events), fork()-specific against this
kernel's vfork control; mechanism unknown.** That fork() reproduces the hang says nothing yet
about whether fork() also produces C3's page corruption.

**What the cut says: the console driver stopped as well, not just the storm.** `pl011-tty`'s drain
thread (`pl011_thr`, `phoenix-rtos-devices` `tty/pl011-tty/pl011-tty.c:1029`) works in batches.
Under `tty->lock` it pops bytes from the libtty queue and writes them to the UART data register
until the TX FIFO fills (`:1078-1084`). It then drops the lock (`:1100`) and draws **the same
batch** on the HDMI fbcon under its own `fbLock` (`:1109-1110`). It sleeps only when nothing is
pending (`:1124-1128`). So:
- **F2 and F3:** the UART received bytes that never reached the framebuffer. F2's 16 bytes are
  one FIFO fill. `pl011_thr` stopped **between writing DR and finishing that batch's fbcon draw**.
  Either it was never scheduled again, or it blocked on `fbLock`. The fb mapping is uncached
  (`:536-544`), so a draw that did complete would show.
- **F1:** the batch was drawn (HDMI matches the UART to the byte), but the rest of the line
  (`8/500\n`) was already in the libtty queue and was never popped. With work pending, the thread
  does not sleep.
- So in all three runs a polling thread **with work pending** stopped running, at different
  points of its loop. A stuck storm child or a missed `waitpid()` wakeup cannot do that on its
  own. The candidates are a system-wide stop (a kernel spinlock deadlock or a CPU spinning with
  IRQs masked while holding a lock the others need), a stop of the scheduler, or the tty server
  itself being wedged.
- ⚠ Two things not to over-read. First, the storm keeps the tty busy all the time, so "stopped
  mid-drain" is the expected place to catch it. Second, the three stop points (the parent's
  printf in F1, the exec'd printenv's output in F2 and F3) mean the last visible byte **does
  not** mark where the storm was. The tty lags, and the queue was not empty.
- The HDMI view of F2 also shows a doubled PATH line after `launch 160` and F3 shows a garbled one
  after `launch 86`. The UART has neither. These are fbcon drawing artefacts, not output, and are
  not counted.

### 8.2 What the storm prints, and where (tests `c3/fork-hang`)

Nothing the storm printed through stdout could have localised this, because stdout **is** the tty
that stopped. The instrumented storm (phoenix-rtos-tests branch `c3/fork-hang`: `14d17dc`, `8749dbc`,
`f74ef5c`) prints every step through `debug()` instead. That call reaches `syscalls_debug` →
`hal_consolePrint` (kernel `hal/aarch64/generic/console.c:68`), which writes the UART synchronously
under `console_common.lock`, with no server in between. A tag is therefore on the wire **before**
the call it announces is made.

| Tag | Printed by | Where in `spawnstorm/spawn_storm.c` | Meaning |
|---|---|---|---|
| `STORM p fc <i>` | parent | before `fork()`/`vfork()` | about to fork |
| `STORM p fr <i> <pid>` | parent | after it returns (pid or `-errno`) | fork returned in the parent |
| `STORM c ac <i>` | child, -f only | before `access()` | the pre-exec filesystem call (psh's ntpclient path) |
| `STORM c ax <i>` | child, -f only | after `access()` | access returned |
| `STORM c ex <i>` | child | before `execv()` | about to exec |
| `STORM c ef <i> <errno>` | child | after a failed `execv()` | exec failed |
| `STORM p wc <i>` | parent | before each `waitpid(-1)` | about to wait |
| `STORM p we <i>` | parent | `waitpid` returned `EINTR` | only after the monitor's poke (below) |
| `STORM p wr <i> <pid> <status> <ms>` | parent | after `waitpid` | reaped, with the wait's duration |

The stdio lines (`spawn-storm: launch …`, the child's PATH line, `DONE`) are unchanged in both modes.
`-f` implies the tags, `-t` turns them on for vfork, and `-n` turns them off.

**Watchdog.** Unless `-w 0` is given, the process psh starts is only a **monitor**. It vforks and
execs the same binary as a `--worker`, which runs the storm exactly as before, blocking
`waitpid()` included, and it raises its own priority to 1, above the tty (4) and the storm. A
watchdog *inside* the worker would need a thread: libphoenix's `alarm()` starts one
(`unistd/alarm.c:72`) and re-starts it in every fork child through `pthread_atfork`. That would
change what every fork() copies, which is the thing under test. Once a second the monitor takes
`threadsinfo(PH_THREADINFO_BASIC | PH_THREADINFO_NAME)`. VMEM is left out on purpose:
`_proc_calculateVmem` walks each map under its lock, unsynchronised against exec replacing it
(`proc/threads.c:2994`). Progress means a child pid it has not seen before. Pids come from a rising
counter (`process.c:163`; V1's pids ran 26…~525), and a child is alive for most of a ~30 ms
launch. The monitor prints:
- `STORM m start worker=<pid> watchdog=<s>s`, a `STORM m tick t=<s> newest-child=<pid> quiet=<s>s`
  every 5 s, and `STORM m done worker=<pid> status=…` at the end.
- After `-w` seconds (default 10) with no new child: one `STORM HANG worker=<w> child=<pid> tid=…
  st=… quiet=…s round=<n>` line per live child thread, or `STORM HANG worker=<w> child=none …`.
  Then `STORM d …` lines for **every thread in the system** (pid, tid, ppid, state 0=ready,
  priority, cpu time, max ready-wait, load, name), and 2 s later `STORM d2-moved` / `d2-new` /
  `d2-gone` for every thread whose cpu time or state changed. A thread whose cpu time grows while
  everything is stuck is a spinner.
- Recovery: `STORM m kill child=<pid> rc=<rc>` (SIGKILL each live child), or, with no live child,
  `STORM m poke worker=<pid> rc=<rc>`. The poke is a SIGUSR1 to the worker, whose no-op handler has
  no `SA_RESTART`, so a `waitpid()` that missed its child's exit returns `EINTR` (`posix.c:3610-3615`
  is interruptible) and the worker tags `we`, then retries. After 3 rounds with no progress the
  monitor only ticks (`STORM m stuck`).

Instrument costs, stated in advance:
- `hal_consolePrint` busy-waits on the UART (~87 µs/byte at 115200) with the console spinlock
  held and IRQs masked. The tags come to ~90–110 bytes per fork launch, so **~8–10 ms of
  IRQ-masked UART time on a ~30 ms launch**. That is a timing change of the kind that has hidden
  intermittents here before (C1). If the hang stops appearing with tags on, that is the
  instrument, **not a fix**: rerun with `-f -n` (monitor only).
- The kernel console and `pl011_thr` write the same UART with no common lock, so tag bytes and
  printenv bytes can interleave within a line. Grade by **counting intact tags** and keep a
  residue column. Do not grade with an exclusion regex. The UART also corrupts ~1.3 % of lines
  on its own.
- The monitor's snapshot holds up to 256 threads. If there are more, it prints
  `STORM m warning: … truncated` once, and a HANG after that warning is suspect.

Compile check: the file was compiled with the tests build's exact flags (`-Wall -Werror -O2
-std=gnu17 …`, recovered via `make -n` from `.buildroot/phoenix-rtos-tests`), then linked against
the toolchain libphoenix. `scripts/syntax-check.sh` itself was not used, because it stages the
`sources/` copy into `.buildroot` while a core build is starting. The tag formatter was tested on
the host. **Not yet run on hardware.**

### 8.3 The fork path, read end to end (kernel `fb8b66ee`)

What differs between fork() and vfork() on this kernel (`proc/process.c`):
- **Both** start with `proc_vfork()` (`:1767`). The child thread runs `process_vforkThread`
  (`:1654`), waits under `spawn->sl` for `FORKING`, copies the parent's kernel stack, and
  longjmps into the parent's context **on the parent's kernel stack** (`:1742-1763`). The parent
  sleeps in `proc_vfork` until the child's `release()` (libphoenix `fork()`, `unistd/sys.c:403`)
  or its exec.
- **fork() only:** the child then runs `process_copy()` (`:1839`) on that borrowed stack.
  `vm_mapCopy` (`vm/map.c:1389`) holds the parent's and child's map locks for the whole copy and,
  since `lazy` is 0, **copies every page of the parent eagerly**. Per page that is one
  `vm_pageAlloc`, two `amap_map` kmap mappings, a memcpy and two unmaps, plus an object fetch
  (fs IPC, parent map lock still held, `object.c:448-452`) for any page not cached.
- **fork() only:** at exec the child takes the `spawn->parent == NULL` path (`:1989-2004`). It
  calls `pmap_switch(kmap)`, then `vm_mapDestroy` on its own map, and `process_exec` re-creates
  that same struct (`:1256`). A vfork child's exec has no destroy.
- **Both:** exit → `process_destroy` → `posix_died` → the parent's `waitpid` wakes.

Checked and clean (by the code-read agent, with the lines named spot-checked here):
- **`waitpid`:** `proc_lockWait` keeps the lock's spinlock until the waiter is queued, and
  `posix_died` broadcasts under `ppinfo->lock` (`posix/posix.c:3610`, `:3648-3657`). So a missed
  wakeup is ruled out by reading, and it could not stop the tty anyway.
- **`vm_mapCopy` lock order:** map → amap → anon → kmap → page, the same as the fault path, and
  `proc_lockSet2` backs off.
- **Exec in place:** `proc_changeMap(NULL)` runs under the scheduler lock before the destroy, and
  the other threads are joined first.
- **Not on this path:** the port-death commits (`9ce228c7`, `ac5ee1a6`, `ee437693`, `3adc9950`),
  pollwake (`ee5939fc`, `fb8b66ee`), and `msg_map`.

Hypotheses for F1–F3, ranked by how well they explain **fork()-only, ~1 %, no dump, and the tty
drain thread stopping with work pending**:

0. **The class: a CPU wedged in the kernel with IRQs masked.** This explains "everything stops,
   nothing prints", which a stuck child or parent cannot. One concrete path that stays silent:
   a kernel-mode fault on a **user** address taken while `threads_common.spinlock` is held
   (signal-frame push in `_threads_schedule` / `threads_setupUserReturn`). `map_pageFault` skips
   the up-front dump for user addresses (`vm/map.c:948-952`) and calls `proc_current()` (`:956`),
   which takes that same non-recursive lock (`proc/threads.c:770`). The CPU deadlocks on itself,
   and the others follow at their next tick or syscall. This is not tied to fork() by itself. The
   fork-specific items below are ways to get there.
1. **A page table freed while another CPU still has it in TTBR0 (fork-weighted, corruption
   leading to a hang).** On aarch64 `pmap_switch(kmap)` is a no-op (`hal/aarch64/pmap.c:425-428`).
   So the scheduler's "protects against use after free of process' memory map in SMP"
   (`threads.c:700-702`) protects nothing, and an idle CPU keeps the last user TTBR0. When that
   process exec's in place or dies, `vm_mapDestroy` frees the tables and the ASID
   (`pmap.c:386-388`, no TLBI). The next `pmap_create` gets the same ASID back (lowest free) and
   often the same pages. The one broadcast TLBI, in `_pmap_asidAlloc` (`:244`), cannot stop a
   later speculative refill through the stale TTBR0. The ASID's new owner then runs with entries
   read from reused pages. This is a real architectural hazard. **fork() adds exactly one destroy
   V does not have:** the exec-in-place destroy of a table that was live, a moment earlier, on
   the CPU the child slept on during `access()`. The exit-time destroy happens in both arms. It
   would also explain C3 (pages "not holding what was written"). It needs a hardware speculative
   walk, which is not shown. **Candidate fix on a branch** (§8.4).
2. **The tty server itself wedges** (userspace; `pl011_thr` blocked on `fbLock` or the libtty
   lock). Fork-specific only through timing. Nothing in the read points here, but the evidence
   in §8.1 cannot exclude it. The instrument separates it from 0 and 1 (§8.5 reading (d)).
3. **The vfork handshake reads a stale `parent->context` (a definite race; not fork-specific).**
   The parent drops `spawn->sl` inside `proc_threadWaitEx` (`threads.c:1392`) before the
   scheduler saves its context (`:592`, reached through `svc` → `.L_el1_syscall`). A child
   spinning on `spawn->sl` sizes the kernel-stack copy from the parent's previous switch-out
   (`process.c:1709`, `:1582`) and copies from a possibly different pointer (`:1714`). That can
   overrun a kernel-heap buffer by up to ~1 KB, and `process_restoreParentKstack` (`:1588`)
   writes the same span back. The window is a few hundred instructions and needs the child to
   already be spinning on `spawn->sl`. It is identical in both arms, so it cannot explain
   0/1500 vs 3/315 on its own. **Fixed on the branch** (§8.4) because it is a bug either way.
4. **Kernel-stack overflow during `process_copy` (fork-specific; demoted by measurement).** The
   eager copy runs on the parent's 8 KiB kernel stack with no guard page. `-fstack-usage` with
   the kernel flags gives: exception frame 816 B, `syscalls_dispatch` 80, `proc_fork` 96,
   `vm_mapCopy` 112, `_map_force` 80, `amap_page` 96, `vm_objectPage` 304, `proc_sendEx` 496,
   `proc_threadWaitEx` 112, the `svc` frame 816, `_threads_schedule` 128. That is ≈ 3.1 KB, plus
   ≈ 1.3 KB per nested IRQ frame. It fits. It would only come back if a path deeper than
   `vm_objectPage` → `proc_sendEx` turns up on the copy.
5. ⛔ **Lost `waitpid` wakeup, COW-fault races:** ruled out by reading (above; there is no COW).

### 8.4 Branches

| Repo | Branch (pushed to `publish`) | Commits | What |
|---|---|---|---|
| phoenix-rtos-tests | `c3/fork-hang` (on local master `dfb30bc`) | `14d17dc`, `8749dbc`, `f74ef5c`, `929f441` | step tags, watchdog monitor, `-t`/`-n`/`-w`, truncation warning, COW comment corrected |
| phoenix-rtos-kernel | `c3/fork-hang` (on master `fb8b66ee`) | `8cf9e488` | aarch64/pmap: switching to the kernel pmap installs an all-invalid TTBR0 with ASID_NONE (hypothesis 1). **Candidate, causal link unproven** |
| phoenix-rtos-kernel | same branch | `33af3e81` | proc: `proc_schedulerBarrier()` after the vfork handshake, so the child reads `parent->context` only after the parent's context has been saved (hypothesis 3). A definite race; not shown to be this hang |

Both kernel changes were compiled with the kernel's own flags (`-Werror`) from the worktree. They
were **not built into an image and not run.** Neither can have a unity test: one needs a hardware
speculative walk, the other two CPUs interleaving inside the kernel. ⚠ `8cf9e488`'s message says
fork()+exec is "the one path" that destroys a live table. Too strong: exit-time destroys do it
too, in both arms. What only fork() adds is the in-place destroy at exec (hypothesis 1).

### 8.5 Pre-registered cycles (written before running)

**Arm A: localise (kernel unchanged, build 14's `fb8b66ee`).**
1. Build `spawnstorm` from tests `c3/fork-hang` (`929f441`) and stage it as
   `/bin/spawn-storm-t` on the live fsid=0 export. Do not overwrite `spawn-storm` or
   `spawn-storm-f`. Check it before running: `strings -a <staged> | grep -c 'STORM HANG'` ≥ 1.
2. On the host, for each cycle: `ping -D -i 1 10.42.0.12 > artifacts/host-side/<label>-ping.log`,
   started before the cycle and stopped after it. lwip is a userspace server, so its replies say
   whether userspace is still being scheduled.
3. `c3tagF1`..`c3tagF3`:
   `./scripts/test-cycle-psh-interact.sh --label c3tagF1 --idle-secs 60 --max-cmd-secs 420 -- '/bin/spawn-storm-t -f 500 /bin/printenv PATH'`
   (Bash `timeout` 600000). Control `c3tagV1`: the same with `-t` in place of `-f` (vfork plus
   tags plus monitor, 500 launches), to show that the instrument does not hang by itself.
4. Grade by the STORM tags. Count intact `STORM p wr <i> <pid> 0 ` lines as clean launches, and
   keep a residue column for lines the UART or the byte interleaving mangled. For every stop,
   record the last `p`/`c` tag, the last `STORM m tick`, whether `STORM HANG` printed, and when
   the ping replies stopped.

**Reading rules, fixed in advance:**
- **(a) Kernel-level freeze.** Tags and monitor ticks stop together (the last tick ≤ 5 s after
  the last tag), there is no `STORM HANG`, and ping stops. That is hypothesis 0; the last tag
  names the step in flight:
  - `p fc <i>` with no `p fr`: inside fork itself (handshake, `process_copy`, `release`).
  - `c ac` with no `c ax`: the child's `access()`.
  - `c ex` and later: exec, printenv, exit or reaping.
  - `p wc`: the parent is asleep, so it is the child side.
  Next step: a kernel-side record of the spinlock owner (CPU, PC) readable after the hang, plus
  the existing `EXEC_ENTRY_TICK` kernel ticks (`process.c:1661`) if the step is inside fork or
  exec. Then arm B.
- **(b) Child-only hang.** Ticks and ping continue, `STORM HANG … child=<pid>` prints with the
  child's last tag, and a BAD status `sig=9` follows. The storm continues and can catch more
  events. The dump shows the child's state and any spinner.
- **(c) Parent missed the child's exit.** Ticks continue, `HANG child=none`, the last tag is
  `p wc`, and after the poke `p we` then `p wr` follow. This contradicts §8.3 (waitpid read
  clean) and would be a new finding.
- **(d) The tty server is the victim.** Ticks and ping continue, tags go on for a while after
  the stdio lines stop (the storm fills the tty queue), and then `HANG` shows the worker blocked
  (last tag `p wr <i>` with no `p fc <i+1>`: it is in `printf`) or the child blocked in its
  write. The `STORM d` lines show the `pl011-tty` threads' states. That is hypothesis 2.
- **(e) Ticks stop but ping answers.** The monitor is stuck in `threadsinfo` (`threads_common.lock`)
  or starved: a kernel-level partial wedge. Read it like (a).
- **(f) No stop in 3 × 500 fork launches with tags on.** At the untagged rate (3 in 315),
  P(0 in 1500) ≈ 10⁻⁶, so a zero means the tags' ~8–10 ms of IRQ-masked UART per launch hide
  the event. **It is not a fix.** Rerun with `-f -n` (monitor only), same labels plus `n`.

**Arm B: the candidate kernel (only after arm A has run, and never in the same build as a tag
change).** Kernel `c3/fork-hang` @ `33af3e81`, built with `--scope core`. Verify that the image
has it before any cycle:
- `aarch64-phoenix-nm .buildroot/_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf | grep -c proc_schedulerBarrier` = 1.
- `nm -S` shows `pmap_common` at `0x14000` bytes or more (it is `0x13000` in build 14).

Then the same three `c3tagF*` commands (labels `c3fixF1`..`F3`) with the same storm binary, so
arm A is its only comparator. Reading:
- 0 stops in 1500 against arm A's rate is enriched support for hypotheses 1 or 3. Then bisect:
  `8cf9e488` alone.
- An unchanged rate retires both as the cause of this hang (either may still matter for C3).
- (a)-style freezes that move to a different step are a new reading, not a fix.
- ⚠ The core build now starting from `sources/` master does **not** contain these commits.
  Label it so that nobody reads it as arm B.

Status: **enriched, n = 3 runs, fork()-specific; mechanism unknown; two kernel candidates on a
branch, neither shown causal.**
