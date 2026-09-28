# psh `top` shows only `CPU0`: the cpuId fill lost in an upstream merge

**Date:** 2026-09-28. **Status:** root cause found in the source; fix and regression test pushed on
branches, not yet run on the Pi. **Pre-registered Pi check:** §4.

## 1. Symptom (owner report, 2026-09-28)

On the Pi, psh `top` prints a single `CPU0 [100%]` line instead of `CPU0` to `CPU3`. SMP itself
works: the kernel boots `Cortex-A72 … x4`, and `[idle]` shows about 235 %.

## 2. Cause

- Our kernel commit `a839db02` (2026-07-14, "threads: expose per-thread last-CPU (cpuId) for SMP
  observability") added `threadinfo_t.cpuId` (`include/sysinfo.h`). It also added
  `thread_t.cpuId`, which `_threads_schedule` sets on every schedule. The old threads-list
  function filled the new field with `tinfo.cpuId = (int)t->cpuId;`.
- Upstream `9c5199f7` ("!syscalls/threadsinfo: return more granular thread information",
  RTOS-1187) replaced that function with `proc_threadsInfo()` / `_proc_threadInfo()`.
- Merge **`00dd500a`** ("Merge remote-tracking branch 'origin/master' into
  agent/upstream-sweep-10", 2026-09-17) took the upstream function and did not carry the fill
  across. Its first parent `c88690cc` still has `tinfo.cpuId` (threads.c:2583). The merge result
  has none. `git log -m --first-parent -G'cpuId' -- proc/threads.c` names this merge as the
  last change to the field.
- Since then the scheduler still records `thread->cpuId`, but no code copies it out. The kernel
  never writes the field, so userspace reads whatever is in its buffer, which is 0 in practice.
- psh top (`psh/top/top.c`, around line 158) sizes its per-core view from the highest `cpuId` it
  sees, including the per-CPU `[idle]` threads. With every value at 0, it shows one CPU.

There is no upstream cpuId concept, so the fix does not duplicate one. `proc_threadsIter()` also
goes through `_proc_threadInfo()`, so the one-line fix covers both producers of `threadinfo_t`.

## 3. Fix and test

| repo | branch | commit | change |
|---|---|---|---|
| phoenix-rtos-kernel | `fix/threadinfo-cpuid` | `17a47f39` | `_proc_threadInfo`: `info->cpuId = (int)thread->cpuId;` in the `PH_THREADINFO_BASIC` block, under the spinlock |
| phoenix-rtos-tests | `fix/threadinfo-cpuid` | `2230061` | new `sys/threadinfo` group, which builds `/bin/test-sys-threadinfo` |

`test-sys-threadinfo` (test `threadinfo_smp.busy_threads_report_their_cpu`):

- starts 4 busy threads at priority 4, each spinning on `clock_gettime(CLOCK_MONOTONIC)` for
  200 ms;
- polls `threadsinfo()` every 2 ms until all 4 threads finish, and zeroes the buffer before each
  call so a kernel that never fills `cpuId` reads as 0 rather than as stale contents;
- asserts that every `cpuId` of every thread is in `0 … sysconf(_SC_NPROCESSORS_ONLN) - 1`, and
  that the busy threads were seen on **more than one** distinct core;
- skips (`IGNORE`) on a 1-CPU system, and fails if sysconf returns ≤ 0.

Both files pass `syntax-check.sh`, a full compile under the real flags. The check ran from a
private root whose `sources/` pointed at the worktrees and whose `.buildroot/<repo>` was a private
copy, so the live buildroot was not touched. A negative control (an injected unused variable)
failed with `-Werror=unused-variable`, which shows that `-Werror` is live. No image was built.

## 4. 📋 PRE-REGISTERED, 2026-09-28 08:45, before any data: `topcpu`

**Question:** does the kernel fix alone make `threadsinfo()` report real cores, and does `top`
then show four CPUs?

**Builds.** Both are netboot builds with `--scope core`: these are committed sibling changes, and
an `auto` rebuild reuses stale objects.

- **Build A:** tests `fix/threadinfo-cpuid` + kernel **master** (`f234ed3e`, without the fix).
- **Build B:** tests `fix/threadinfo-cpuid` + kernel `fix/threadinfo-cpuid` (`17a47f39`).

Both branches are still checked out in the authoring worktrees
(`/home/houp/.claude/jobs/c8f1289c/tmp/wt-{kernel,tests}-cpuid`), so
`git checkout fix/threadinfo-cpuid` in `sources/*` fails with "already checked out". Check out
the SHAs (detached) instead, or `git worktree remove` those paths first.

**Verify each build before booting it:**

- `grep -c 'info->cpuId' .buildroot/phoenix-rtos-kernel/proc/threads.c` gives 0 for A and 1 for
  B.
- `grep -a -c 'distinct cpuIds of' <rootfs>/bin/test-sys-threadinfo` gives at least 1 for both. The
  binary must be in the live NFS export's `/bin`.

**Cycle** (one per build, labels `topcpuA` and `topcpuB`):

```
./scripts/test-cycle-psh-interact.sh --label topcpuA --idle-secs 20 --inter-cmd-secs 5 -- \
  '/bin/test-sys-threadinfo' '/bin/test-sys-threadinfo' '/bin/test-sys-threadinfo' \
  'top -n 2 -d 1' 'top -H -n 2 -d 1'
```

`top` is a psh builtin. `-n 2` exits after two frames, and `-d 1` puts 1 s between them, so the
second frame's percentages are a 1 s delta. `-H` adds the per-thread `CPU` column. **Never pass
`-c`:** it forces the core count and would hide the bug.

**Expected, build A (stock kernel):**

- Each of the 3 test runs prints the following, with `<abs path>` being the real build's
  `.buildroot/phoenix-rtos-tests` path:
  `ASSERTION <abs path>/sys/threadinfo/threadinfo.c:184:FAIL: Expected 1 to be greater than 1. distinct cpuIds of 4 busy threads over <N> snapshots, 4 CPUs`
  It then prints `1 Tests 1 Failures 0 Ignored` and `FAIL`.
- `top` prints `CPU0 [` and no `CPU1 [`, which reproduces the owner's report. The `-H` CPU column
  is all `0`.

**Expected, build B (with the fix):**

- Each of the 3 test runs prints `1 Tests 0 Failures 0 Ignored` and `OK`.
- `top` prints `CPU0 [`, `CPU1 [`, `CPU2 [` and `CPU3 [` on the per-core line, with no `CPU4 [`.
  The `-H` CPU column has values in 0…3 and more than one distinct value.

**Grading.**

- Look for the expected lines positively. The UART corrupts about 1.3 % of lines, so a key line
  that is unreadable counts as missing data, not as a fail. In that case, rerun the one command.
- **Fix confirmed:** A shows 3/3 FAIL; B shows 3/3 PASS and four CPUs. The test is the
  discriminator for A. `top` reads `cpuId` from a malloc'd buffer that the stock kernel never
  writes, so a stale nonzero value could make stock `top` show extra CPUs. One CPU in A is
  expected, but it is not required.
- **Test does not detect the bug:** A shows any PASS. The test must then be fixed before the
  kernel change is merged.
- **Fix incomplete, or the test is flaky under this scheduler:** B shows any FAIL. Examine the
  `-H` CPU column before rerunning.
