# usb-framework — upstream-readiness review

- **Area:** `usb-framework`
- **Repo:** `phoenix-rtos-usb` (base `origin/master` `3ffbe3c` → head `master` `b3e97dc`)
- **Files reviewed (changed hunks only):** `usb/hub.c`, `usb/usb.c`, `usb/mem.c`, `usb/dev.c`, `usb/dev.h`, `usb/hcd.c`, `usb/usbhost.h`, `README.md`
- **Net delta:** 8 files, +342/−20

> **Scope note on the task brief.** The brief described these hunks as
> "multi-slot/route-string/TT enumeration … generic class handlers … address!=slotId".
> The actual diff contains **none** of that: there is no route-string, no TT-hub-descriptor,
> no per-class-handler, and no `address != slotId` code in this two-dot diff (those changes,
> if they exist, are in already-merged upstream history or in `devices/usb/xhci`, not here).
> The real `hub.c +152` is (a) a bounded per-port enum-failure give-up counter and (b) a
> **disabled / dead** "initial full-port scan" experiment. Reviewed what is present; did not
> manufacture findings for absent code.

These are **shared** USB-stack files compiled for **every** Phoenix USB target (ia32, imx6ull,
…), so each finding is weighed for ia32/imx6ull regression risk, not just Pi4.

---

## Findings (ordered by severity)

### 1. `usb/hub.c:49,437-475,482-511,557-589` · **ROLLBACK** · sev=high
**WHAT:** The entire "initial full-port scan" (`scanAll`) machinery is **dead code**, not merely
"disabled". `hub_notifyScan()` is referenced only by `(void)hub_notifyScan; /* hub_notifyScan(hub); */`
at hub.c:587 — it is never called. Dead-by-construction therefore are: the `scanAll` field in
`hub_event_t` (line 49), the whole `if (ev->scanAll) { … usleep(2000000u); … }` branch in
`hub_thread` (lines 437-475), `hub_notifyScan` itself (lines 506-511), the `scanAll` parameter
plumbing through `hub_postEvent`, and ~40 lines of parked-approach `TODO(#129)` commentary
(lines 557-586) that narrate four abandoned scan variants.
**WHY:** This is the single biggest upstream blocker in the area. A maintainer reading this
sees an unreachable 2-second `usleep` in the hub thread, a struct field that is always 0, and a
function suppressed only by a `(void)` cast to dodge `-Wunused-function`. It is experiment
scaffolding, not shippable code. The `hub_postEvent`/`hub_notify` split exists *solely* to feed
the dead path; with the scan gone, `hub_notify` can revert to the original direct body.
**REC:** Remove the `scanAll` field, the `if (ev->scanAll)` branch (restore `hub_thread` to the
original `hub_getStatus` + bit-loop), `hub_notifyScan`, the `scanAll` parameter, the `(void)hub_notifyScan;`
line, and the parked TODO block — restoring `hub_conf` to `return hub_requestStatus(hub);`. If the
team wants to keep the approach for a future session, move it to a branch/notes file, not the
shared tree. **NOTE:** removing this also removes the only consumer of the `usleep`/scan logic, so
it is mechanically a pure deletion — but it is intertwined with the live enum-give-up logic
(findings 2,7), so apply as one reviewed patch. · **APPLY-SAFE** (pure dead-code deletion; gate on
`--scope core` build + boot-to-psh smoke since it touches the live hub-event path).

### 2. `usb/usb.c:186-195` · **BUG/ROLLBACK** · sev=high
**WHAT:** In `usb_transferFinished`, before `LIST_ADD(&usb_common.finished, t)` the code does
`if ((usb_common.finished != NULL) && (usb_common.finished->prev == NULL)) { fprintf(...); usb_common.finished = NULL; }`
— i.e. on detecting a malformed completion ring it **silently discards the entire pending
URB-completion list**. The comment itself states "Proper fix: the URB-completion … subsystem
(tracked separately)", i.e. this is a corruption-masking band-aid, not a fix.
**WHY:** This runs on **all** targets, not just Pi4. On ia32/imx6ull, a transient list malformation
(or any future bug that trips the heuristic) would now drop every in-flight completion and leak
them, instead of faulting visibly — converting a loud crash into silent data loss / hangs that are
far harder to diagnose. It masks the very class of memory corruption the `mem.c` guards (finding 5)
are also chasing.
**REC:** Do **not** blind-delete — it currently guards a live crash on Pi4. Document as a known
band-aid with a `TODO(#nnn)` and a clear `#if`/runtime gate so it does not silently alter ia32/imx6ull
behavior; the real fix is the per-slot interrupt-pipe/URB-ring rework the comment references. At
minimum the reset should be loud + counted, not a quiet `fprintf` to a buffered-then-lost stream.
· **NEEDS-HW** (control-flow + completion-path semantics; cannot be validated overnight).

### 3. `usb/mem.c:55-64` · **BUG (latent), ARCH** · sev=med
**WHAT:** `usb_allocUncached` adds `MAP_PRIVATE | MAP_CONTIGUOUS` to the `mmap` flags for **every**
USB target. The comment calls this "just code hygiene … verified to not affect rig flakiness."
**WHY:** It is not behaviourally inert. `MAP_PRIVATE` is `0x0` (no-op), but `MAP_CONTIGUOUS` is
not: in the shared kernel mmap path it routes anonymous allocations through
`vm_objectContiguous(size)` (`phoenix-rtos-kernel/syscalls.c:89-93`), which **can return `-ENOMEM`
under physical-memory fragmentation** where the previous plain-anonymous object could not. So on
ia32/imx6ull this change can make a previously-succeeding USB buffer allocation fail at runtime,
for zero functional gain (the comment admits no benefit). The referent that *does* need contiguity
is the kernel DMA helper `phoenix-rtos-lwip/drivers/physmmap.c:28` (`dmammap`) — but USB pool
buffers are `USB_BUF_SIZE` = 4096 = one page, so contiguity is a tautology for a single page and
buys nothing while adding the fragmentation-failure mode.
**WHY it matters for upstream:** a shared-file change justified only by Pi4-rig parity, imposed on
all arches, with a self-admitted "code hygiene" rationale, is exactly the kind of Pi4-leak a
maintainer will reject.
**REC:** Either (a) revert to the original `MAP_ANONYMOUS | MAP_UNCACHED` (single-page pool buffers
don't need `MAP_CONTIGUOUS`), or (b) if contiguity is genuinely required for the >4K
`usb_allocAligned(size, USB_BUF_SIZE)` path, add `MAP_CONTIGUOUS` only there and document the
DMA-contiguity requirement against the `physmmap.c` referent. Drop `MAP_PRIVATE` (no-op noise).
· **NEEDS-HW** (per-arch allocation behavior; document, do not blind-flip overnight).

### 4. `usb/hcd.c:198,203,208,210,213,218` + `:15` · **ROLLBACK** · sev=med
**WHAT:** Six raw `debug("usb-hcd: …")` diagnostic prints bracketing `ops->init` and
`usb_devEnumerate`, including a `char buf[64]; (void)snprintf(buf,…,"… rc=%d\n",ret); debug(buf);`
formatting dance, plus the new `#include <sys/debug.h>`. No `TODO(TD-xx)` marker.
**WHY:** Pure bring-up tracing on the shared HCD init path; every USB target would emit these on
boot. The existing idiom in this very file is `log_error(...)` (e.g. the adjacent
"Fail to initialize hcd type" line, kept and even improved to add `rc=%d`). `debug()` is the
low-level kernel-console primitive, not the stack's logging idiom (see `usb/log.h` /
`log_error`/`log_msg` used everywhere else in hub.c, dev.c, usb.c).
**REC:** Delete all six `debug(...)` calls and the `char buf[64]` block; keep the genuinely useful
`log_error("… rc=%d", info[i].type, ret)` improvement. Remove the now-unused
`#include <sys/debug.h>`. · **APPLY-SAFE** (diagnostic-only removal; build + boot smoke).

### 5. `usb/mem.c:130-157,168-187,241-250` · **BUG (defensive band-aid)** · sev=med
**WHAT:** `usb_chunkSane()` + its three call sites validate free-list headers before deref and, on
failure, `fprintf(stderr, "… overflow upstream")` and treat the buffer as full / leak the chunk.
The guard logic itself is **correct** (bounds, alignment, size all checked; NULL treated as valid
list-end; the alloc-walk re-checks each node and the post-loop check catches a bad terminal node).
**WHY:** This is again corruption-masking, not root-cause repair, and it runs on all targets. The
`TODO(USB-MEM)` marker is good, but two concerns for upstreaming: (a) it permanently converts a
detectable heap-overflow bug into a silent bounded leak on ia32/imx6ull too, and (b) it is paired
with finding 2 chasing the same underlying corruption from a different file — a maintainer will ask
"where is the overflow that smashes the free list, and is it fixed?" The `dev.c` `USBDEV_SETUP_SIZE`
change (finding 8) plausibly *is* one such overflow source now fixed; if so, these guards may be
load-bearing only against an already-fixed bug.
**REC:** Keep for now (correct + marked), but in the upstream write-up tie it explicitly to the
overflow root cause (finding 8) and state whether the guard is still required once that is fixed.
If the overflow is fixed, downgrade these to debug-assert-only so production ia32/imx6ull don't pay
the per-alloc validation cost. · **NEEDS-HW** (correctness depends on whether the root overflow is
gone; document).

### 6. `usb/usb.c:39` · **ROLLBACK** · sev=low
**WHAT:** `#include <sys/debug.h>` added to usb.c, but **no `debug()` call exists** in any usb.c
hunk (the new code uses `fprintf(stderr, …)`).
**WHY:** Leftover from removed/relocated tracing; an unused include on a shared file.
**REC:** Remove the include. · **APPLY-SAFE**.

### 7. `usb/hub.c:545` · **BUG (leak)** · sev=low
**WHAT:** In `hub_conf`, the `hub_setPortPower(...) < 0` error path does `free(hub->devs); return -EINVAL;`
but does **not** free the newly-allocated `hub->portEnumFails` (allocated at line 536). New leak
introduced by this diff. (The `-ENOMEM` path at 538 correctly frees `devs` only because
`portEnumFails` failed to allocate; that one is fine.)
**WHY:** Affects all targets that hit a port-power failure; small but a clean correctness defect
that a reviewer will spot. Note: `devs`/`portEnumFails` are not freed on *normal* hub teardown
either, but that `devs` leak is pre-existing upstream and out of scope; only the new
`portEnumFails` sibling-leak on the error path is in-scope.
**REC:** `free(hub->devs); free(hub->portEnumFails); return -EINVAL;` (and ideally NULL both).
· **APPLY-SAFE** (local error-path fix; build smoke).

### 8. `usb/usb.c:46` (`N_STATUSTHRS 1→2`) · **ARCH/COMMENT** · sev=low
**WHAT:** `N_STATUSTHRS` bumped 1→2 so `usb_init` spawns one URB-completion consumer
unconditionally. The comment correctly explains the embedding (lwip-port) host never ran the Nth
consumer itself.
**WHY:** Motivated purely by the Pi4 lwip-embed PoC but imposed on **all** targets: the standalone
ia32/imx6ull daemon now runs 2 status threads (one extra, "harmlessly" per the comment) where it
ran 1. Low risk but a semantic change to shared behavior driven by a Pi4-specific embedding.
**REC:** Acceptable as-is given the consumer-must-run invariant, but for upstream note it in the
commit message as an intentional shared-behavior change, or gate the embed-only consumer behind the
same `USB_NO_MAIN` story that already partitions `main()`/`usb_init()`. · **NEEDS-HW** on
ia32/imx6ull (thread-count change; document).

### 9. `usb/dev.c:686-696` · **COMMENT/ROLLBACK** · sev=low
**WHAT:** The `usb_drvBind` failure log was expanded to dump device + per-interface class/subclass/
protocol triples in a loop. No TD marker.
**WHY:** Useful triage detail, but it is bring-up diagnostics on the shared enumeration path and
fires on any unmatched device on every target. Borderline between "good error context" and "debug
noise."
**REC:** Keep the one-line `devClass` summary (genuinely useful); consider dropping the per-interface
loop or guarding it, to match the terse `log_msg` idiom used elsewhere in dev.c. Low priority.
· **APPLY-SAFE** if trimmed.

---

## Correct / low-risk changes (noted, no action)

- `usb/dev.c:37-43,689-693,872-879` — `USBDEV_SETUP_SIZE 32` + sizing the shared setup/ctrl
  allocation to `USBDEV_SETUP_SIZE + USBDEV_BUF_SIZE`. **Correct** and well-commented: the prior
  `usb_alloc(USBDEV_BUF_SIZE)` with `ctrlBuf = setupBuf + 32` left the ctrl buffer 32 bytes short,
  so a full `USBDEV_BUF_SIZE` transfer overran into the adjacent free-chunk header — exactly the
  overflow the `mem.c` guards (finding 5) defend against. This is a real fix; the magic `+ 32` is
  now a named constant. Good upstream-quality change.
- `usb/usb.c:158-167` — idempotency guard in `usb_transferFinished` (`if (t->finished) return;`).
  Correct and clearly commented; defends the finished-ring against double completion. Low risk.
- `usb/usb.c:319,484` — `(unsigned)(uintptr_t)arg` / `(void *)(uintptr_t)` port cast fixes.
  Correct 64-bit-clean pointer-int round-trip (the old `(int)arg` truncates on aarch64). Good.
- `usb/usb.c:417-545`, `usb/usbhost.h:142-148` — `main()`/`usb_init()` split + `USB_NO_MAIN`,
  and the `setvbuf` stdout-buffering (`TD-12`) block. The `#ifndef USB_NO_MAIN` partition is a clean,
  documented way to embed the stack; it does not affect ia32/imx6ull (they build with `main`).
  Reasonable.
- `README.md` — fork-warning banner. Fine for a fork; should be dropped/rewritten before any actual
  upstream PR (it advertises "not been fully reviewed/tested").

---

## Summary

- **Findings:** 9 actionable + 5 noted-correct. By category: ROLLBACK 3 (1 high, 1 med, 1 low) +
  parts of 2/9; BUG 4 (2 high/med semantic, 2 low local); ARCH 2 (mem.c flags, N_STATUSTHRS);
  COMMENT 1. By severity: **high 2**, med 3, low 4.
- **APPLY-SAFE overnight:** #1 (dead scanAll machinery — pure deletion, but touches the live
  hub-event path so gate on build+boot smoke), #4 (hcd.c `debug()` removal), #6 (unused include),
  #7 (portEnumFails leak fix). **NEEDS-HW / document-only:** #2 (finished-list reset band-aid),
  #3 (MAP_CONTIGUOUS on all arches), #5 (mem free-list guards), #8 (thread count).
- **Most important issue:** the `scanAll`/`hub_notifyScan` initial-scan apparatus (finding 1) is
  **dead code** — never called, kept alive only by a `(void)` cast, dragging an unreachable 2 s
  `usleep` and ~40 lines of abandoned-experiment commentary into a shared file. It is the clearest
  upstream blocker and should be removed (or branched out) before presentation.
- **Cross-cutting theme for `_SYNTHESIS.md`:** three independent corruption-masking band-aids
  (#2 finished-list reset, #5 free-list sanity guards) plus the now-fixed root overflow (#8
  `USBDEV_SETUP_SIZE`) all chase the same heap-corruption story. A maintainer will want a single
  coherent narrative: "overflow root cause = ctrlBuf undersize, fixed; the defensive guards are
  belt-and-suspenders and can be downgraded once that fix is confirmed." Also recurring: Pi4-only
  motivations (MAP_CONTIGUOUS parity, N_STATUSTHRS for lwip-embed) imposed unconditionally on
  shared code rather than conditionalized.
