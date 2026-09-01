# small-repos — upstream-readiness review

**Area:** small-repos (libphoenix, phoenix-rtos-build, phoenix-rtos-filesystems, phoenix-rtos-utils)
**Repos / base → head SHAs:**
- libphoenix: `250383d` → `5674368` (4 files, +51/-16)
- phoenix-rtos-build: `556394d` → `30f6867` (5 files, +95/-1)
- phoenix-rtos-filesystems: `fc027f3` → `c7a1401` (4 files, +77/-27)
- phoenix-rtos-utils: `34b00a1` → `34f87c4` (7 files, +94/-8)

---

## Findings (ordered by severity)

---

### F1 · `dummyfs/srv.c` · **ROLLBACK** · sev=high

**What:** The diff adds a complete debug-tracing subsystem to `dummyfs/srv.c`
with no `TODO(TD-xx)` marker: `enum { traceNone, traceRoot, traceDevfs }`,
`static void dummyfs_trace(...)`, two `int` state variables (`traceKind`,
`lookupTrace`), and four `debug()` call sites — one on the post-registration
path, one on the main-loop entry, and two inside the `mtLookup` case (one
pre-dispatch, one post-dispatch, the latter behind a one-shot gate).

**Why this is a problem:**
1. **No TD marker.** Temporary diagnostic code without a `TODO(TD-xx)` tag
   cannot be tracked or scheduled for removal. Every other diagnostic in the
   Pi 4 bringup has a marker (e.g., `TODO(TD-14-console-open-fastpath)` in
   `libphoenix/unistd/file.c`). This block has none.
2. **Blast radius is every platform.** `dummyfs` is used as rootfs and devfs
   on ia32-generic, zynqmp, and every new aarch64-generic target. The
   `debug()` syscall emits to klog on all of them whenever dummyfs is used as
   root or devfs — noise that upstream reviewers will immediately notice.
3. **`dummyfs_trace` is dead infrastructure.** After the last cleanup commit
   (`1ae1cbf`, `1884043`) the function is only called with `traceRoot` (two
   calls that print unconditionally at startup) and `traceDevfs` (one call).
   The `traceNone` branch is never reachable. The `lookupTrace` gate fires
   only during the first-ever `"devfs"` lookup, at most once per run — its
   purpose was already achieved by observing it in boot logs during the TD-14
   investigation, documented in `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.
4. **Mixed concern.** Two separate debug levels (`traceKind` enum + separate
   `lookupTrace` one-shot) with no compile-time gate means the instrumentation
   runs in production on every `msgRecv` iteration.

**REC:** Remove the entire diagnostic block: the `enum`, `dummyfs_trace()`,
the two state variables, all four `debug()` call sites, and the
`#include <sys/debug.h>`. Restore the `switch` body to the upstream tab
indentation (see F2). If any probe must be kept for the remainder of TD-14
investigation, add `TODO(TD-14-dummyfs-trace)` and record it in
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.

**NEEDS-HW** — removing this before a full boot smoke is a build-only
verification. However, the probes are unconditional, so their absence cannot
make a regression invisible. A single `--scope core` rebuild + netboot
smoke is sufficient gating.

---

### F2 · `dummyfs/srv.c` · **STYLE** · sev=med

**What:** The main initialization block (lines 235–261 in the current file)
switches from the file's native tab indentation to 8-space indentation for the
outer `if/else` branches, while the inner block bodies (`portRegister`, the
`dummyfs_trace` call, `mountpt = NULL`) revert to tabs. This creates a
three-way mix: surrounding code uses tabs, outer braces use 8 spaces, inner
statements use tabs again.

**Referent:** Every other function in `dummyfs/srv.c` uses tabs
consistently (verified: the `fetch_modules`, `dummyfs_mount_sync`, and the
`msgRecv` switch block). The upstream version of this block (`origin/master`)
also uses tabs throughout.

**Why:** The space indentation was introduced when the Pi 4 tracing
variables were added and the block was reformatted incrementally across
several commits without a final clang-format pass.

**REC:** After removing the tracing state variables (F1), reformat the
`if (mountpt == NULL) { ... }` block back to tabs, matching the rest of the
file and the upstream original. A `clang-format --style=file` pass on
`dummyfs/srv.c` before submission is the cleanest fix.

**APPLY-SAFE** — pure indentation fix, no semantic change. Gate on
`--scope core` rebuild (confirmed no behavior change).

---

### F3 · `dummyfs/srv.c` · **BUG** · sev=med · NEEDS-HW

**What:** Both `write(1, "", 0)` readiness-poll loops present in the upstream
`origin/master` are removed without explanation. These loops spin until fd 1
(stdout) becomes writable — they act as a synchronization barrier that
prevents `portCreate` + `portRegister` from running until the parent process's
stdio is ready. Upstream has them in two paths: the root-mount path
(`mountpt == NULL`) and the non-filesystem namespace path (`-N` / devfs).
The diff removes both.

**Why this is a concern:** If the callers of `portRegister("/")` or
`portRegister(mountpt)` (the kernel syspage spawner) close or redirect fd 1
before dummyfs is done registering, the poll would have caught that condition.
More concretely, on Pi 4 the syspage-spawned `dummyfs -N devfs` registers the
`devfs` namespace that `pl011-tty` and other drivers depend on. If the kernel
stdout side races (closes before dummyfs loops), the removal could silently
cause `portRegister` to run before stdio is settled. The fact that the Pi 4
boot currently works does **not** prove this is safe — the sync may never have
been needed on Pi 4 and may be a no-op POSIX artifact on this platform.

**REC:** Add a code comment explaining *why* the removal is correct on Pi 4
(e.g., "posixsrv fd1 is always open by the time syspage programs run on this
platform" or "fd1 is not used as a readiness signal on generic targets").
Without that rationale the change is invisible to upstream reviewers and will
trigger a review question. If the rationale cannot be confirmed, restore the
loops behind an `#ifdef __CPU_GENERIC__` guard or a compile-time option, to
avoid regressing other platforms.

**NEEDS-HW** — cannot be validated without a boot cycle that exercises the
sync race; document for the attended session.

---

### F4 · `build-core-ia32-generic.sh` · **ARCH** · sev=med

**What:** The diff adds `libusbdrv-usbkbd` to the ia32 `USB_HOSTDRV_LIBS`
line, changing it from `"libusbdrv-umass libext2"` to
`"libusbdrv-umass libusbdrv-usbkbd libext2"`. This modifies a non-Pi target
to include the USB keyboard driver.

**Why:** The ia32 build already shipped without `usbkbd` upstream. Adding it
is likely an accidental spillover from aligning all build-core scripts during
the Pi 4 USB keyboard work. The Pi 4 `build-core-aarch64a72-generic.sh`
correctly has `libusbdrv-usbkbd libusbdrv-usbmouse` (no `libusbdrv-umass`,
no `libext2`) — those ia32-specific entries were preserved, so `usbkbd` is the
only new addition to ia32.

**Referent:** The upstream ia32 build-core script
(`origin/master:build-core-ia32-generic.sh`) does not include `usbkbd`.

**REC:** Verify whether the ia32-generic image is still tested/maintained. If
yes, confirm that the ia32 USB keyboard driver compiles and is intentionally
added (otherwise revert the one-line change). If no, document the rationale
in a comment. Either way, the ia32 change should be a deliberate, explicit
commit — not a side effect of Pi 4 work.

**NEEDS-HW** — whether ia32 keyboard is intended is an architecture/product
decision, not a code-only call.

---

### F5 · `psh/pshapp/pshapp.c` · **COMMENT** · sev=low

**What:** The `PSH_TTYOPEN_RETRIES` macro defaults to `50` (50 × 10 ms =
500 ms). The `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` entry for
**TD-14-psh-retry** (§ near line 1019) documents the value as "bumped
20 → 200" (20 retries × 100 ms = 2 s → 200 retries × 100 ms = 20 s). The
code does not match: retries are 50, not 200; delay is 10 ms, not 100 ms; and
the `TODO(TD-14-psh-retry)` marker required by the doc is absent.

Additionally, the TD doc says the pre-Pi value was 20 retries × 100 ms = 2 s
(`PSH_TTYOPEN_RETRY_US = 100000`). The current code uses 50 × 10 ms = 500 ms
— a shorter total budget than even the pre-Pi value. This is inconsistent with
the documented intent of extending the budget for Pi 4's slow devfs path.

**REC:**
1. Add `TODO(TD-14-psh-retry)` immediately above the `#define PSH_TTYOPEN_RETRIES` block.
2. Update `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` § TD-14-psh-retry to
   record the actual values (50 × 10 ms = 500 ms), or change the code to the
   documented values (200 × 100 ms = 20 s) if the longer budget is still
   needed.
3. Remove the misleading "Restore to 20" resolution note in the doc if 20 is
   no longer the upstream default.

**APPLY-SAFE** — adding the TD marker and updating the doc are comment/doc
changes only.

---

### F6 · `psh/psh.c`, `psh/pshapp/pshapp.c` · **ROLLBACK** · sev=low

**What:**
- `psh/psh.c`: adds `#include <sys/debug.h>` and a `rootRetries` counter that
  is incremented in the root-wait loop but immediately suppressed with
  `(void)rootRetries`. No `debug()` call site uses the header.
- `psh/pshapp/pshapp.c`: adds `#include <sys/debug.h>` with no `debug()` call
  site in the file.

**Why:** The `sys/debug.h` header is not needed unless `debug()` is called.
Both inclusions are vestigial from an earlier probe-strip round. The `rootRetries`
variable was presumably intended to be logged via `debug(...)` but the call
was removed while the counter and `(void)` suppressor were left behind.

The TD-14 doc entry **TD-14-psh-debug-probes** (§ near line 926) says these
probes were "active diagnostic" as of `da2f541` and should be removed "after
interactive shell smoke passes." The smoke has passed (#124 keyboard
validated). Both files should be clean.

**REC:** Remove `#include <sys/debug.h>` from both files. Remove the
`rootRetries` counter and the `(void)rootRetries` line from `psh/psh.c`. Mark
TD-14-psh-debug-probes as RESOLVED in `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.

**APPLY-SAFE** — dead code removal; confirmed no debug() calls exist in either
file.

---

### F7 · `libphoenix/stdio/fprintf.c` · **COMMENT** · sev=low

**What:** The `feed_ctx_s.buff` field grows from 16 bytes to 256 bytes.
The comment explaining the change is accurate but verbose (5 lines for what
could be 2). More importantly: `feed_ctx_s` is allocated on the stack for
every `vfprintf` and `vdprintf` call (lines 90, 121). The 240-byte growth
applies on every `fprintf`, `printf`, `dprintf`, and `vfprintf` call on
all targets — including constrained embedded platforms in the Phoenix family.

**REC:** Add a one-line note in the comment: "Stack allocation: each
`vfprintf`/`vdprintf` call allocates this struct; the 256-byte buff is
therefore 240 B more stack per printf call." This makes the trade-off visible
to upstream reviewers and allows per-platform re-evaluation. Trim the comment
to 2-3 lines. Consider whether `256` should be a named constant
(`FPRINTF_BUF_SZ` or similar) so it can be overridden per target.

**APPLY-SAFE** — comment-only change.

---

### F8 · `libphoenix/unistd/file.c` · **STYLE** · sev=low

**What:** The variable `traceConsole` (line 340) is declared `int` and used
only as a boolean predicate (`traceConsole != 0`). The name `traceConsole`
implies an active-trace role but the variable only checks whether the filename
is `/dev/console` — it controls a code path, not a tracing flag. No `debug()`
call uses it.

**Referent:** Throughout `libphoenix/unistd/file.c` predicates are declared
`int` and tested `!= 0` (consistent with the project style). The naming
concern is isolated: `traceConsole` is a misnomer for what should be named
`isConsole` or `isConsolePath`.

**REC:** Rename `traceConsole` → `isConsolePath` (or simply `isConsole`) to
clarify that this is a path-classification predicate, not a diagnostic flag.
The `TODO(TD-14-console-open-fastpath)` marker and the comment are otherwise
correctly in place and well-structured.

**APPLY-SAFE** — rename only, no logic change.

---

### F9 · `dummyfs/srv.c` · **COMMENT** · sev=low

**What:** The copyright header (line 6) was updated to add `2023` but the
Pi 4 modifications to this file are dated 2026 (first commit `ea03ccc` is
2026-03-20; latest cleanup `1884043` is 2026). Upstream `origin/master`
shows `2012, 2016, 2018, 2021` — the `2023` addition is unsubstantiated (no
commits from 2023 appear in the diff history).

**REC:** Replace `2021, 2023` with `2021, 2026` to accurately record the year
of the Pi 4 bringup contribution.

**APPLY-SAFE** — comment/metadata only.

---

## Positive observations

- **usbmouse present:** `build-core-aarch64a72-generic.sh` includes
  `libusbdrv-usbmouse` in `USB_HOSTDRV_LIBS` — the #126 mouse fix is correctly
  reflected in the build script. `libusbdrv-usbkbd` is also present.
- **console-open-fastpath properly guarded:** `libphoenix/unistd/file.c` TD-14
  fastpath is correctly gated to the `/dev/console` filename only, has a
  `TODO(TD-14-console-open-fastpath)` marker, and the marker grep documented in
  the TD doc resolves correctly.
- **reboot.c `__CPU_GENERIC__` handling is correct:** The generic `platformctl_t`
  union is named `task` (`.task.reboot.magic`), while zynqmp's is anonymous
  (`.reboot.magic`). The `#if defined(__CPU_ZYNQMP)` / `#else` split in
  `reboot.c` is structurally identical to the `arch/sparcv8leon/reboot.c`
  (uses `{ 0 }` + field-by-field assignment) and `arch/riscv64/reboot.c`
  (uses `.task.reboot.magic` direct init) patterns — no bug.
- **bind.c retry:** `psh_bindLookup` with 30 × 100 ms = 3 s retry window is
  a reasonable and well-commented workaround for the syspage concurrent-spawn
  race. Macro names are clear (`BIND_LOOKUP_RETRIES`, `BIND_LOOKUP_DELAY_US`).
- **pm.c signal fix:** The `sigint/sigquit/sigstop` reset + while-loop
  rewrite is a genuine correctness improvement. The `volatile unsigned char`
  fields are declared in `psh.h` and the handlers are installed in `pshapp.c`
  before any subcommand runs — the fix is correct in the shared-process model.
- **aarch64a53-generic scaffolding is not dead:** `_projects/aarch64a53-generic-rpi4b`
  and `_projects/aarch64a53-generic-qemu` board projects exist in
  `phoenix-rtos-project`, so the three new `_targets/Makefile.aarch64a53-generic`
  files across build/filesystems/utils are intentional.

---

## Summary

**Counts:** 2 ROLLBACK (1 high, 1 low), 1 BUG (med), 1 ARCH (med), 3 COMMENT
(1 low each), 2 STYLE (1 med, 1 low).

**Single most important issue:** The dummyfs debug-tracing subsystem
(`dummyfs_trace`, `traceKind`/`lookupTrace` state, four `debug()` call sites)
has no `TODO(TD-xx)` marker, fires unconditionally on every platform that uses
dummyfs as root or devfs, and its purpose (observing the first devfs lookup
during TD-14) has already been served. It must be removed and the indentation
fixed before any upstream submission.

**Cross-cutting theme:** Several changes touch shared code that builds for
all Phoenix targets — `dummyfs/srv.c` (debug calls), `libphoenix/stdio/fprintf.c`
(stack growth), and the `write(1,"",0)` removal. Reviewers will scrutinize
each for unintended cross-platform side effects. The blast radius should be
called out in commit messages and explicitly documented where the change is
intentional.
