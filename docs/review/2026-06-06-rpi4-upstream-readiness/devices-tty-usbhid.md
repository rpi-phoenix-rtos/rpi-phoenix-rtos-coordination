# Area: devices-tty-usbhid

- **Repo:** `phoenix-rtos-devices` (base `origin/master` d511e0f → head `master` ebac8e4)
- **Files reviewed (all newly added by RPi4 bring-up):**
  - `tty/usbkbd/usbkbd.c`, `tty/usbkbd/srv.c`, `tty/usbkbd/Makefile`
  - `tty/usbmouse/usbmouse.c`, `tty/usbmouse/srv.c`, `tty/usbmouse/Makefile`
- **Phoenix class referent:** there is no prior in-tree USB-HID driver. The closest
  accepted referent for an interrupt/bulk USB device-driver-server of this class is
  `tty/usbacm/usbacm.c` + `tty/usbacm/srv.c` (Maciej Purski / Adam Greloch). Both new
  drivers are clearly modeled on it; STYLE/ARCH findings below cite it.
- All files are brand-new, so the two-dot diff is the full file content; every line is
  in-scope (no pre-existing upstream code).

---

## Findings (ordered by severity)

### 1. usbkbd.c:91-93 / usbmouse.c:87-88 · BUG · sev=med · `dev->report[]` buffer passed to `usb_urbAlloc` is dead under the procdriver path
**WHAT:** Both drivers store a per-URB report buffer in the dev struct
(`uint8_t report[N_URBS][reportSize]`) and pass `dev->report[i]` as the `data` arg to
`usb_urbAlloc(... dev->report[i], usb_dir_in, ...)` (usbkbd.c:458, usbmouse.c:272).
These drivers run via `usb_driverProcRun` (srv.c), i.e. the **procdriver** pipe-ops.
`libusb/procdriver.c:usbprocdrv_urbAlloc` (lines 212-235) **ignores its `data`
argument entirely** — the URB/transfer buffer is owned server-side and the received
payload is delivered to the completion handler as `msg.i.data` (procdriver.c:74 →
`handlers.completion(..., data, len)`). `handleCompletion` correctly consumes that
`data` param, never `dev->report`. So the `report[]` member and the non-NULL arg are
dead: nothing the driver allocates is ever DMA'd into or read.
**WHY:** Misleading to a maintainer (implies a driver-owned DMA buffer that doesn't
exist), wastes the dev struct, and diverges from the accepted referent. `usbacm.c`
passes **`NULL`** for exactly this arg (usbacm.c:428 ctrl, :434 bulk-IN) precisely
because the framework owns the buffer.
**REC:** Pass `NULL` for the urbAlloc `data` arg and delete the `report[N_URBS][...]`
member (and the now-unused `report` indexing). Confirms identical behavior — the read
path already uses the completion `data` pointer.
**NEEDS-HW** (touches the URB-alloc call; behaviorally a no-op given procdriver.c
ignores the arg, but validate one boot-to-keypress to be safe).

### 2. usbkbd.c:775-784 / usbmouse.c:587-595 · BUG · sev=med · insertion error path leaks fifo + lock + cond
**WHAT:** On any failure inside the `do { } while (0)` in `handleInsertion`, the cleanup
is `_usbkbd_put(dev)` (which only `idtree_remove`s, no free) followed by a bare
`free(dev)`. But `_usbkbd_devAlloc()` allocates `dev->fifo` (malloc), `dev->lock`
(mutexCreate) and `dev->cond` (condCreate) **up front**. The bare `free(dev)` leaks all
three on every insertion that fails after devAlloc (e.g. `usb_open`/`setConfiguration`/
`setProtocol`/`create_dev` failure). Same in usbmouse.
**WHY:** Resource leak on a real error path (failed enumeration is exactly when this
fires). The referent `usbacm` avoids it two ways: its `_usbacm_devAlloc` creates none of
fifo/lock/cond (they're created later in handleInsertion), and its create_dev-failure
break explicitly `resourceDestroy`s lock+cond before the put (usbacm.c:711-713).
**REC:** The error path should release what devAlloc created — i.e. call the equivalent
of `usbkbd_free()` minus the `remove(dev->path)` (path/dev-node not yet created):
`free(dev->fifo); resourceDestroy(dev->cond); resourceDestroy(dev->lock); free(dev);`
after the `_usbkbd_put`. Mirror in usbmouse.
**NEEDS-HW** (error-path control flow; document, do not blind-apply).

### 3. usbkbd.c:119-133, 513, 651-654, 793 · ROLLBACK · sev=med · `usbkbd_diag*` counters are dead and their TODO(#127) removal condition is already met
**WHAT:** Non-static globals `usbkbd_diagInsertions/Opens/Reports/LastReport` (decls
130-133), incremented at :513 (`Opens`), :651-654 (`Reports` + the `usbkbd_diagLastReport`
memcpy in handleCompletion) and :793 (`Insertions`). The TODO(#127) comment (119-129)
says they are "Non-static so diag-udp.c ... can extern them" and to "Remove once the
keyboard input path is confirmed working end-to-end."
**WHY:** This is rubric case (b) "a TD item the diff shows is already resolved", not naive
deletion: (a) the keyboard path **is** HW-confirmed end-to-end (#122/#124,
`MEMORY: USB KEYBOARD WORKS END-TO-END`), so the marker's own removal condition is met;
and (b) **no consumer exists** — `diag-udp.c`'s `'k'` command was repurposed to an
SD-card read probe (diag-udp.c:5264, :5489) and nothing in `phoenix-rtos-lwip` externs or
references any `usbkbd_diag*` symbol. The globals are dead even by their own stated
purpose, and forcing them non-static defeats `-fdata-sections` GC. The mouse driver
(correctly) has no such counters — they should be symmetric.
**REC:** Delete the four global decls (130-133), the comment block (119-129), and the
three increment/memcpy sites (513; 651-654 keeping `usbkbd_handleReport`; 793). No
behavior change.
**APPLY-SAFE** (pure dead-code removal; gate on `--scope core` + boot-to-psh smoke).
Note per rubric line 67 this review phase is read-only — classification only, do not edit now.

### 4. usbmouse.c:330-365 · BUG · sev=low · `usbmouse_read` silently returns 0 for a sub-packet buffer
**WHAT:** `want = min(len, avail); want -= want % reportSize;` — if a caller passes
`len < 4` (e.g. a 1- or 2-byte read), `want` rounds to 0 and the function returns 0 after
having blocked until data was available, without consuming anything.
**WHY:** A 0-return on a blocking read is ambiguous (looks like EOF to some callers) and
the data stays queued forever for that undersized reader. The packetized contract is
reasonable, but a too-small buffer should be a hard error, not a silent 0.
**REC:** If `len < usbmouse_reportSize` return `-EINVAL` (or `-ERANGE`) before/instead of
the wait, so the caller learns its buffer is too small.
**NEEDS-HW** (read-path semantics; document).

### 5. usbkbd.c:838-844 / usbmouse.c:648-654 · BUG · sev=low · init partial-failure leaks port/mutex
**WHAT:** `usbkbd_init` returns `-ENOMEM` if `mutexCreate` fails *after* `portCreate`
succeeded, without destroying the port; symmetric for the threads loop. Same in usbmouse.
**WHY:** Leak on init failure. Low severity because init failure is fatal to the daemon
anyway, and the referent `usbacm_init` has the identical pattern (usbacm.c:783-801,
returns `1` and leaks) — so this matches accepted in-tree behavior.
**REC:** Optional: `portDestroy`/`resourceDestroy` on the partial-init paths. Low
priority given referent parity.
**NEEDS-HW** (document; trivial but on init control flow).

### 6. usbmouse.c:11-20 · COMMENT/ARCH · sev=low · "factor into libhidboot" TODO has no tracking ID
**WHAT:** The file-top TODO accurately describes that usbkbd.c and usbmouse.c duplicate
~90% of their scaffolding (driver registration, idtree mgmt, pipe open, URB
alloc/submit/re-arm, rx fifo, msgport/read/poll loop) and proposes a shared `libhidboot`
core, but it is a bare `TODO:` with no `#NNN`/`TD-xx` tracking ID.
**WHY:** The duplication is real and substantial (the two .c files are near-identical
modulo the HID-usage→ASCII translation in kbd vs raw passthrough in mouse). Phoenix TODO
hygiene (and this project's `TODO(TD-xx)`/`TODO(#NNN)` convention, e.g. usbkbd.c:119
`TODO(#127)`) wants a tracked marker. This is correctly an ARCH note, not a blocker — a
small mirror of the accepted usbkbd was the right way to land the mouse without
regressing the keyboard.
**REC:** Give the TODO a tracking ID (open an issue for the `libhidboot` refactor and
cite it). Do not refactor pre-upstream.
**APPLY-SAFE** (comment only).

### 7. usbkbd.c:721,787 / usbmouse.c:533,599 · ROLLBACK/COMMENT · sev=low · bring-up `debug()` traces, one with a hardcoded/stale string
**WHAT:** `debug("usbkbd: handleInsertion fired\n")` (721) and
`debug("usbkbd: New /dev/kbd0 device created\n")` (787) — the latter hardcodes `kbd0`
even though the path is `kbd%d`. usbmouse mirrors this: `debug("usbmouse: handleInsertion
fired\n")` (533) and `debug("usbmouse: New /dev/mouse device created\n")` (599). These
`debug()` (raw kernel-log) calls are redundant with the adjacent
`fprintf(stdout, "...: New device: %s\n", dev->path)` which already logs the correct path.
**WHY:** Bring-up tracing left in; "fired" / hardcoded-`kbd0` are diagnostic noise not
present in the referent (usbacm uses a gated `TRACE()` for handleInsertion and a single
`fprintf(stdout,... %s, dev->path)` for the success line, no `debug()`). The drivers
already define a gated `TRACE()` macro (usbkbd.c:80) — these should use it or be dropped.
**REC:** Remove the four `debug()` calls (the `fprintf(stdout, ... %s, dev->path)` lines
already cover the useful info), or convert "fired" to `TRACE()`.
**APPLY-SAFE** (log-line removal; gate on build + boot smoke).

### 8. usbkbd.c:80-81 / usbmouse.c:75-76 · STYLE · sev=low · `TRACE` macro form differs from referent
**WHAT:** `#define TRACE(fmt, ...) do { if (0) printf(...); } while (0)`.
**WHY:** The accepted referent `usbacm.c:59` uses `#define TRACE(fmt, ...) if (0)
printf(...)` (no `do/while`). Minor — the `do/while` form is actually the safer idiom,
but it's a gratuitous divergence from the one class referent. Not worth changing on its
own; flag only for consistency if the file is touched.
**REC:** Optional: match `usbacm.c:59` form, or leave (the new form is defensible).
**APPLY-SAFE** (formatting only).

---

## Things checked and found CORRECT (no finding — recorded to bound the review)

- **Keymap / symbol tables (usbkbd.c:141-377):** the 0x2d–0x38 symbol range correctly
  includes the 0x32 slot (`#`/`~`) between 0x31 and 0x33 — the #124 keymap fix; both
  tables are 12 long as the comment requires. Modifier bit map (73-77: LCtrl 0x01,
  LShift 0x02, RCtrl 0x10, RShift 0x20) matches the HID boot-report modifier byte. Ctrl
  collapsing (271) and caps-lock XOR (268) are correct.
- **Mouse 4-byte framing (usbmouse.c:188-205, 330-365):** fifo size 256 (power-of-2, as
  `fifo.h` requires), all pushes/pops move head/tail by exactly 4, eviction is in
  whole-packet multiples — framing stays aligned. Read rounds down to whole packets.
- **N_URBS == 1 (usbkbd.c:46-63, usbmouse.c:58-63):** the constant and its long rationale
  comment correctly document the Pi4 xHCI HCD single-in-flight-per-interrupt-pipe limit;
  this is a documented HCD constraint (#124), not a driver bug. Keep as-is.
- **#126 throwaway reader:** NOT in usbmouse.c. It lives in `pl011-tty` with a
  `TODO(#126)` marker (board_config.h:37) — out of this area's scope; the mouse driver
  itself is clean of any throwaway reader, as #126 intended.
- **srv.c main() with no return after `usb_driverProcRun`:** matches the referent
  `usbacm/srv.c:37` exactly (procRun does not return). usbkbd/usbmouse srv.c correctly
  add `#include <stdio.h>` (usbacm's srv.c relies on a transitive include for fprintf —
  the HID ones are tidier here).

---

## Summary

Eight findings: **BUG** ×4 (1 med + 1 med + 2 low), **ROLLBACK** ×2 (1 med diag-counters,
1 low debug traces), **COMMENT/ARCH** ×1 (low), **STYLE** ×1 (low). No high-severity
issues; both drivers are well-structured mirrors of the accepted `usbacm` referent and
the keymap/framing/N_URBS logic is sound. Two findings are APPLY-SAFE (dead diag counters
#3, redundant debug() traces #7) and would be the overnight-eligible batch; everything
touching URB-alloc, error-path, or read semantics is NEEDS-HW.

**Most important issue:** the dead `dev->report[]` URB buffer (#1) — the drivers pass a
driver-owned buffer to `usb_urbAlloc` that the procdriver path silently ignores, unlike
the `usbacm` referent which passes `NULL`. It is harmless today but actively misleading
to an upstream maintainer about who owns the DMA buffer, so it should be cleaned before
presentation. The insertion-path resource leak (#2) is the most concrete correctness bug.
