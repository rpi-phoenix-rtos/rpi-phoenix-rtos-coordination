# Exec-from-NFS reliability — root cause + foundational fix (2026-06-27)

> **STATUS (2026-06-28): reported failures FIXED + HW-validated; one ELUSIVE residual OPEN
> (so #43 stays in_progress, doc kept in inprogress/).** Read integrity is proven byte-exact
> (md5 of NFS reads matches host across many boots). RESIDUAL: an INTERMITTENT, clustered,
> concurrent-heavy-load exec `-ENOMEM` that is NOT reliably reproducible — it hit in 2 clusters
> under heavy load (6 rapid 3 MB execs; 20 MB md5sum + execs) yet ~6 identical-load boots after
> were clean. It is NOT read corruption (md5 always matches) and NOT the now-fixed header map.
> Mechanism not yet pinned: `-12` still merges ≥4 alloc sites (header mmap / `process_forceRange`
> / eager segment maps / user stack). Diagnostics ARMED (kernel `542e4f1c`, `TEMP-NOMEM-DIAG`
> markers; revert once root-caused) to name the failing allocation on the next occurrence —
> most likely the parallel X-app demo (`ico & oclock & xlogo & xclock & xcalc & xbill &`), true
> concurrent 6×3 MB exec that sequential psh commands can't replicate. Leading (unconfirmed)
> candidate: eager segment population under concurrent pressure → proper fix would be
> demand-paged segments (lazy exec is off on MMU, `process->lazy=0`), a separate multi-day
> project. NEXT: capture `mem` + the marker at one live failure → classify → scope.
>
> ---
> **Earlier status (now partially superseded): the two fixes below are REAL and stand** — they
> resolve the *reported* failures (short-read corruption + the "startx returns immediately at
> boot" eager-header-map ENOMEM). The heavy-concurrent-load residual above was found afterward.
> 1. **Short-read corruption** — `object_fetch` short-read loop + EOF zero-fill (section (b)),
>    kernel `f145658f`, HW-validated (micropython/busybox/startx exec reliably).
> 2. **Large-binary -ENOMEM** — kernel `d30fd33a`. ROOT CAUSE was *not* "validation needs the
>    whole file mapped" but **eager population**: on an MMU target `vm_mmap` force-populates
>    every page of a mapping (`_map_force` → `vm_objectPage` → `object_fetch`/`proc_read`), so
>    the whole-file header map at `process_load` (`proc/process.c:704`) pulled the ENTIRE ELF
>    in one page-read per page and pinned it — 163 NFS reads for a 652 KB launcher, far worse
>    for the 3 MB X clients — intermittently `-ENOMEM` under boot memory pressure, even though
>    only the ELF/program/section headers + the section-name string table are ever
>    *dereferenced* (the segment data between is pointer-range-checked only). Fix: map the
>    image **demand-paged** (toggle the process `lazy` flag across just that mmap — exec is
>    single-threaded) and explicitly fault in only the metadata pages the parser touches
>    (phdr/shdr/shstrtab ranges via `process_forceElf64Headers`/`process_forceRange`; other
>    ELF classes fall back to forcing the whole image). Validation + TLS detection unchanged
>    (the contiguous `iehdr/size` contract is preserved — no validation refactor needed). The
>    PT_TLS-based cleanup is a legitimate *separate* follow-up, deliberately not bundled here.
>    HW-validated: ~20 large (2–3 MB) NFS execs + 3 startx launches across 3 boots, **0
>    `-ENOMEM`** (was ~2/3 failing), and exec is faster (a handful of page reads, not the whole
>    file). Manifest `2026-06-27-nfs-large-exec-demand-page`.
>
> (NOTE: the separate `busybox sh -c '<script>'` ENOMEM is a psh argv-parsing artifact, NOT
> this loader path. The ~19 MB Quake/vkQuake images can now also be exec'd from NFS rather than
> bundled, though `loader.disk` bundling remains for boot-without-NFS convenience.)

Host-side investigation (no Pi boots; orchestrator owns HW). User mandate: NFS must
ALWAYS work and support direct exec of large binaries with NO workarounds (no
copy-to-RAM, no retry loops). Find and implement the real root-cause fix.

**Status of claims:** the orchestrator's HW boots are the only thing that can confirm a
fix. Below, items are tagged **CONFIRMED (from code, file:line)** vs **HYPOTHESIS**.
The fix is implemented and builds; it is **NOT** claimed resolved.

---

## (a) Confirmed mechanism — the kernel ELF loader mishandles short reads

### The exec-from-NFS read path (CONFIRMED, file:line)

1. `proc_fileSpawn` (`proc/process.c:1266`) does `proc_lookup` + `vm_objectGet`, then
   `proc_spawn(... object->size ...)`. Loading is **demand-paged**, not eager: the file
   object is mmap'd and pages are pulled on fault.
2. `process_load` (`proc/process.c:686`) maps the whole file (`vm_mmap(..., o, base, ...)`
   at `:704`) and `process_load64` (`:594`) maps each PT_LOAD segment file-backed via
   `vm_mmap(map, vaddr, NULL, round_page(filesz), prot, o, base+offs, ...)` (`:669`).
3. A page fault on a file-backed page calls `object_fetch` (`vm/object.c:174`), which:
   - `vm_pageAlloc(SIZE_PAGE, ...)` — **does NOT zero page contents** (CONFIRMED:
     `vm/page.c:41` `_page_alloc` only updates bookkeeping/flags; no memset of page data).
   - `proc_read(oid, offs, v, SIZE_PAGE, 0)` once, and checked only `if (... < 0)`.
4. `proc_read` (`proc/name.c:613`) returns `msg->o.err` (`:636`). For an NFS read the
   server sets `msg.o.err = nfs_ops_read(...)` (`filesystems/nfs/srv.c:255-256`), which
   returns the **positive byte count** on success (possibly short) or a negative errno
   (`filesystems/nfs/nfs_ops.c:266-313`). This is the **canonical Phoenix mtRead
   contract** — dummyfs does the same (`filesystems/dummyfs/srv.c:293`,
   `dummyfs.c:1011`), and the kernel reader reads the count from `o.err`. **NFS is
   conformant.**

### The bug (CONFIRMED)

`object_fetch` issued **one** `proc_read` per page and only treated a **negative** return
as failure. A **short positive** return (fewer than `SIZE_PAGE` bytes — which is *normal*
for NFS; a single READ RPC may legitimately come back short) was treated as success. The
page was never zeroed and never re-read, so the **tail of the page held stale heap data**.

- For a **mid-file** code/data page that comes back short → the page has a correct head and
  a garbage tail → corrupt instructions/data → the new process crashes or misbehaves.
- The first page (ELF header) is usually read whole, so magic validation often passes; the
  corruption lands deeper in the binary. Larger binaries span more pages → more chances for
  a short read → the observed "intermittent, size-correlated" failure.

### Why it surfaces as a SILENT exec failure (CONFIRMED)

When `process_load` returns an error, `process_exec` sets `current->process->exit = err`
and calls `proc_threadEnd()` (`proc/process.c:1160-1163`) — but this runs **inside the
freshly-created process**, after the parent's `vfork` has already returned. So the shell
sees a process that started and exited; there is **no error at the prompt, no fault, and
the program's `main()` never runs.** This is an exact match for the reported symptom.

Note: the alternative #156 "first-read ENOENT" path fails earlier in `proc_fileSpawn`
(`proc_lookup`/`vm_objectGet` return negative) and **does** propagate an error to the
shell — so it does *not* produce the silent no-output signature, and is a *different* bug
this fix does not address.

### The discriminator (strong, near-proof without HW)

**copy-to-RAM works, direct-exec fails — because of who loops.** libphoenix's safe copy
path (`__safe_read`, `libphoenix/unistd/file.c:173`) **loops on short reads**; plain
`read()` does not, but `cp`/copy uses the safe looping path. The kernel ELF loader's
`object_fetch` was the **lone reader that did not loop**. If the bytes on the wire were
actually corrupt, or the errors were hard, copy-to-RAM would fail too — but it has been the
reliable workaround. That asymmetry points squarely at the missing read-loop, not at data
corruption or hard RPC errors. (HYPOTHESIS to confirm on HW: that copy-to-RAM of the same
binary is reliably byte-correct — the test recipe below checks this directly.)

---

## (b) The fix — loop on short reads + zero-fill EOF tail (the standard read contract)

`vm/object.c` `object_fetch` now takes the object size and loops `proc_read` until the
file end within this page is reached, zero-filling the page tail beyond EOF. The loop is
**bounded by the object size**, so it never issues a read at/past EOF — the call is
`object_fetch(o->oid, offs, o->size)` and `offs < o->size` is already guaranteed by the
caller (`vm_objectPage` checks `offs >= o->size` at `object.c:231`):

```c
target = ((u64)osize - offs < SIZE_PAGE) ? (size_t)((u64)osize - offs) : SIZE_PAGE;
while (total < target) {
    r = proc_read(oid, (off_t)(offs + total), (char *)v + total, target - total, 0);
    if (r < 0)  { /* unmap, free, close, return NULL */ }
    if (r == 0) break;            /* anomalous early EOF; trailing memset covers it */
    total += (size_t)r;
}
if (total < SIZE_PAGE) { hal_memset((char *)v + total, 0, SIZE_PAGE - total); }
```

**Why this is the proper fix, not a workaround:**

- It is the **standard correctness contract every read path needs**: read() may return
  fewer bytes than requested; you loop until satisfied. libphoenix already does this
  (`__safe_read`, `libphoenix/unistd/file.c:173`); `object_fetch` was the outlier. This
  makes the kernel loader *conform*.
- Progress is guaranteed (`r > 0` advances `total`; the loop is size-bounded) — **no
  infinite loop**, including on the last page of a file.
- **Size-bounding (not EOF-probing).** An earlier draft looped to `SIZE_PAGE` and relied on
  a final `proc_read` at `offset == size` returning `0`. That would issue a *new* read at
  EOF on the last page of **every** demand-paged exec from **every** filesystem (including
  the ones that boot today), creating a regression risk if any server returns an error at
  `offset == size`. The size-bound form removes that dependency entirely: it never reads at
  or past EOF, drops one RPC per last page, and `o->size` is invariant mid-load so passing
  it is lock-safe.
- The **page-tail zero-fill** closes a second latent correctness gap: pages are not
  pre-zeroed, and the whole-file mapping at `process.c:704` (used for ELF header / section
  header / string table validation) reads the EOF page, whose tail must read as zero.
- It is filesystem-agnostic: dummyfs/ext2/SD return full reads, so the loop runs exactly
  once and reads the same bytes as the old single-read path for any non-last page — **no
  behavior change for full-read filesystems on full pages**. The only changed behavior is
  on a file's last (partial) page, where the read is now correctly clamped to the file end
  and the tail zeroed — which is the fix.

**Self-diagnosing print (so HW can confirm which branch fired).** `process_exec` now prints
on the silent-failure path (`proc/process.c:1160`):

```c
lib_printf("proc: exec '%s' failed (err=%d)\n", ...path..., err);
```

This converts the previously-invisible exec failure into a console line naming the binary
and errno — turning the orchestrator's loop from "count silent failures" into "see exactly
why and how often." `err == -ENOEXEC` ⇒ bad/garbled ELF (validation); other negative ⇒
read/RPC error.

### What was investigated and deliberately NOT changed

**The poll()-readiness hole is real but is REFUTED as the exec cause** (this corrects the
task's prime hypothesis):

- Cross-process `poll()`/`select()` on a socket is **snapshot-based**, confirmed: libc
  `poll` → kernel `posix_poll` (`posix/posix.c:2418`) does a caller-side retry loop —
  `do_poll_iteration` sends `mtGetAttr`/`atPollStatus` (non-blocking) to the socket server,
  sleeps `min(remaining, POLL_INTERVAL)`, retries. The socket server services it with
  `poll_one(..., timeout=0)` (`lwip/port/sockets.c:831`) — an instantaneous readiness
  check. **Socket readiness does not wake a cross-process poll**; the only blocking is the
  caller-side sleep. (Agent analysis that said lwip poll is "event-driven" looked at
  *in-process* `lwip_select` — the wrong layer; the NFS server is a separate process.)
- BUT: with `nfs_set_poll_timeout(nfs, 1)` already set (`filesystems/nfs/srv.c:355`), each
  RPC re-polls at ~1 ms granularity, so RPCs **complete** (measured ~8 MB/s elsewhere). The
  poll hole is a **latency** matter, already mitigated — it does not make a read return
  short or error, so it is **not** the exec-correctness bug.
- A true readiness-woken cross-process poll (kernel posix + every fs/socket server change)
  is a large, separate **performance** project. It is intentionally **out of scope** here
  and should not be bundled with the correctness fix.

---

## (c) Build status

`./scripts/rebuild-rpi4b-fast.sh --scope core` — **OK** (clean build, netboot image
`loader.disk` exported, SHA in artifacts/rpi4b/). The new diagnostic string
`exec '%s' failed` is present in `loader.disk` (grep count = 1), confirming the kernel
object recompiled with the change (stale-core hazard avoided).

Files changed (kernel, sibling repo — uncommitted, for orchestrator to commit after HW
validation):
- `sources/phoenix-rtos-kernel/vm/object.c` — `object_fetch` short-read loop + EOF zero-fill.
- `sources/phoenix-rtos-kernel/proc/process.c` — diagnostic print on silent exec failure.

---

## (d) Decisive HW test recipe (for the orchestrator)

Goal: measure exec-from-NFS failure rate before/after, and discriminate branch A
(short-read corruption — this fix) vs branch B/C (RPC error / ENOENT — NOT this fix),
WITHOUT the X/`:0`-already-in-use confound. Use a small, fast NFS binary.

Pick a tiny static NFS binary that prints one unmistakable line on success. `/nfstest/bin/uname`
is ideal (prints a sysname line). If unavailable, any tiny binary whose `main()` prints a
known string works.

### 1. Exec loop (50x) from psh

At the `(psh)%` prompt (interactive or via `test-cycle-psh-interact.sh`):

```sh
i=0
while [ $i -lt 50 ]; do echo "RUN $i"; /nfstest/bin/uname -a; i=$((i+1)); done
```

(If psh lacks `while`/`$(())`, just paste `/nfstest/bin/uname -a` 50 times, each preceded
by a distinct `echo RUN-N` marker.)

### 2. cp-vs-exec discriminator (decisive for which branch)

```sh
cp /nfstest/bin/uname /tmp/uname
/tmp/uname -a
```

- **cp succeeds + /tmp exec succeeds, but direct /nfstest exec fails** ⇒ branch A
  (short-read corruption) — exactly what this fix targets.
- **cp itself fails / /tmp exec also fails** ⇒ branch B/C (RPC error or ENOENT) — this fix
  will NOT help; revisit poll/timeout/#156.

### 3. What to grep in the UART log

```
./scripts/uart-summary.sh <label>
grep -a "RUN "                 <log>   # how many of the 50 iterations ran
grep -a "exec .* failed"       <log>   # NEW: names each silent exec failure + errno
grep -a -i "uname\|aarch64\|Phoenix"  <log>   # success lines from the binary's main()
```

**Failure signatures:**
- A `RUN N` with **no** following success line **and** a `proc: exec '...' failed (err=-8)`
  (`-8 == -ENOEXEC`) ⇒ a corrupt-ELF load (branch A). Count these.
- `proc: exec '...' failed (err=-5)` (`-EIO`) or other negative ⇒ an RPC/read error
  (branch B). These were never the silent-no-output symptom; if they appear, that's a
  different problem.
- Pre-fix expectation: some fraction of the 50 runs fail silently (no success line); a
  matching count of `exec ... failed` lines (now visible thanks to the diagnostic).
- Post-fix expectation (to confirm): all 50 runs print the success line; zero `exec ...
  failed` lines. Repeat across ≥3 boots — the failure is intermittent, so a single clean
  boot is not sufficient evidence.

### 4. Large-binary confirmation (the real target)

After the small-binary loop is clean, exec a large NFS binary several times to confirm the
multi-page case:

```sh
i=0; while [ $i -lt 10 ]; do echo "BIGRUN $i"; /nfstest/bin/startx; i=$((i+1)); done
```

(startx prints an unconditional banner on entry to `main()` — grep for it.) For the X
server itself, avoid the `:0`-in-use confound by launching the small/medium client, or kill
any prior X between runs; the `startx` launcher's first-line print is the load proof
regardless of whether X then comes up.

---

## Open items / honesty notes

- **NOT confirmed resolved** — only the orchestrator's HW boots can confirm. The fix is
  correct-by-construction and risk-free to working paths, but the *active* branch (A vs
  B/C) must be confirmed by the cp-vs-exec test above.
- If post-fix failures persist with `err=-2`/`-EIO` *before* any `exec ... failed` line for
  the binary (i.e. failing in `proc_fileSpawn`), that is #156 ENOENT / RPC-timeout, a
  separate bug — do not attribute it to this fix.
- Performance follow-up (separate): make cross-process `poll()`/`select()` readiness-woken
  (kernel `posix_poll` + server-side blocking poll) so NFS throughput stops depending on a
  1 ms re-poll floor. Large change; benefits all poll/select apps; explicitly out of scope
  for this correctness fix.
