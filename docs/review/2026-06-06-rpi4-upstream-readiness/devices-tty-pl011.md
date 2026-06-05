# devices-tty-pl011 — upstream-readiness review

- **Area:** `devices-tty-pl011`
- **Repo:** `phoenix-rtos-devices` (base `origin/master` `d511e0f` → head `master` `ebac8e4`)
- **Files reviewed (changed hunks only; both new files):**
  - `tty/pl011-tty/pl011-tty.c` (1187 lines, new)
  - `tty/pl011-tty/Makefile` (new)
- **Referents:** `tty/imx6ull-uart/imx6ull-uart.c`, `tty/libtty/libtty_disc.c`, `tty/libtty/libtty.h`

Driver = PL011 UART tty (libtty) + optional HDMI framebuffer console (`__CPU_GENERIC`)
+ a direct-attach kernel-klog drain thread (`pl011_klogthr`, kernel port {0,0}).
Open debt noted in CLAUDE/TD: TD-14 (two-owner UART: kernel console mirror vs this tty),
#147 (console backspace redraw), TD-15 (fbcon), TD-16 (cache-enable).

---

## Findings (ordered by severity)

### 1. `pl011-tty.c:1042-1053` · BUG · sev=med · kbd reader wake-loss across multi-byte reads
`pl011_kbdthr` accumulates `wake_reader` across the per-byte loop:
```c
libtty_putchar_lock(&uart->tty);
for (i = 0; i < len; ++i) {
    int woke = 0;
    (void)libtty_putchar_unlocked(&uart->tty, buf[i], &wake_reader);
    wake_reader |= woke;
}
```
`libtty_putchar_unlocked` → `libtty_putchar_helper` (libtty_disc.c:201-204) **unconditionally
writes `*wake_reader = 0` at entry**, then sets 1 only if *that* byte woke a reader. So when a
multi-byte USB report ends in a non-waking byte (e.g. a key that fills the canonical line but
isn't `\n`, or any byte after the one that satisfied a blocked reader), the final iteration
resets `wake_reader` to 0 and the `libtty_wake_reader` call is skipped — the queued input
stalls until the next read arrives.
**WHY:** the flag must be OR-accumulated, but the API overwrites it each call.
**REC:** accumulate explicitly:
```c
int woke = 0;
(void)libtty_putchar_unlocked(&uart->tty, buf[i], &woke);
wake_reader |= woke;
```
(This is exactly what the dead `int woke=0; wake_reader |= woke;` lines were *trying* to do —
the call just passes the wrong pointer.) **NEEDS-HW** (input-path timing; verify on Pi 4 USB kbd).

### 2. `pl011-tty.c:930-932` · BUG · sev=med · same wake-loss in the UART RX drain loop
`pl011_thr` drains the RX FIFO with the identical anti-pattern:
```c
while ((pl011_read(uart, fr) & fr_rxfe) == 0) {
    libtty_putchar(&uart->tty, pl011_read(uart, dr), &wake_reader);
}
```
`libtty_putchar` is the locked wrapper over the same helper (libtty_disc.c:340-342), so each
iteration resets `wake_reader`; a burst whose last char doesn't wake a reader loses the wakeup
for the whole burst. The bottom-of-loop `if (wake_reader != 0) libtty_wake_reader(...)` then
no-ops. imx6ull avoids this by passing `NULL` and waking unconditionally after the RX-process
pass (imx6ull-uart.c:475/480 + the cond signal at 559).
**REC:** OR-accumulate per char: `int woke=0; libtty_putchar(&uart->tty, c, &woke); wake_reader |= woke;`.
**NEEDS-HW** (serial-console input timing).

### 3. `pl011-tty.c:512-518` vs `188-195`/`487-501` · COMMENT · sev=med · contradictory cache-state comments (one is stale, load-bearing)
The init-clear comment (512-518) asserts "restored full-framebuffer clear **now that caches are
operational** … with caches on the per-pixel store cost is negligible." But `fill64` (188-195)
and the `mmap` attribute comment (487-501) both justify themselves with "kernel **D-cache
disabled** (Stage 1 parked) … every framebuffer write goes straight to DDR." Both cannot be
the current state. This is load-bearing: it is the stated rationale for the `MAP_UNCACHED`
choice and the 64-bit fill perf hack. A maintainer can't tell whether the fill64/NC design is
still required or a cache-off relic.
**WHY:** stale/contradictory TODO-hygiene; the rubric explicitly targets this.
**REC:** pick the true current state (head is on the cache-enable branch per CLAUDE.md / TD-16);
reconcile the three comments to one consistent story, and if caches are on, note whether
fill64/NC is kept for the VC4-scanout-is-DRAM reason (501) rather than the cache-off reason.
**APPLY-SAFE** (comment-only).

### 4. `pl011-tty.c:729` · ROLLBACK · sev=med · unmarked permanent diagnostic `fprintf`
```c
fprintf(stderr, "pl011-tty: klog attach rc=%d err=%d\n", rc, msg.o.err);
```
fires on **every** boot in the klog-drain thread and has **no** `TODO(TD-xx)` / issue marker
(unlike the kbd `#127` fprintf at 1025 and the mouse `#126` block). Per the ROLLBACK category
this is exactly "diagnostic-only code lacking a marker."
**REC:** drop it, or demote to the error branch only (the `if (rc<0 || err<0)` just below already
gates the failure case — log there, not on success). **APPLY-SAFE** (move/remove a stderr line;
gate on build + boot-to-psh smoke).

### 5. `pl011-tty.c:1062-1110, 1067-1104` · ROLLBACK · sev=low · marked throwaway mouse diagnostic thread
`pl011_mousethr` is self-described "throwaway bring-up diagnostic" `TODO(#126-mouse-validate)`,
opens `/dev/mouseN` purely to kick URB polling and dump raw HID reports to UART. It is marked
debt, so do **not** blind-delete — but it must not ship to maintainers as a tty driver feature.
**REC:** remove the thread + its `beginthread` (1179-1181), the `mousestack` field (140), and
`PL011_TTY_MOUSE_PATH` plumbing before upstreaming; track under #126. **NEEDS-HW** (removing it
changes whether usbmouse URBs ever start; document, don't auto-apply).

### 6. `pl011-tty.c:463-468, 350-452` · BUG · sev=low · fbcon parser state shared across tty/klog writers without cross-write protection
`fbLock` serializes each `pl011_fbcon_write` call, but the VT100 parser state
(`fbescState`, `fbescParams`, `fbcol/fbrow`) **persists across** lock acquisitions and is shared
by `pl011_thr` (tty mirror, 64-byte batches) and `pl011_klogthr` (klog, 256-byte reads). If an
escape sequence straddles a batch boundary in one writer, the other writer's interleaved batch
is consumed mid-CSI → corrupted render (and cursor/clear applied to the wrong stream). Purely
visual, but the rubric calls out "locking between tty + klog threads."
**WHY:** lock scope = per-write; parser is a cross-write state machine.
**REC:** accept as known cosmetic limitation and comment it, or (cleaner upstream) route klog
through the same single fbcon producer instead of a second writer. **NEEDS-HW** (render
behavior). Likely related to #147 backspace-redraw.

### 7. `pl011-tty.c:922-1000` · ARCH · sev=low · poll loop instead of interrupt+condWait tty model
This driver RX/TX-drains in a busy `pl011_thr` poll loop (`usleep(PL011_TTY_POLL_US)` when idle)
rather than the canonical interrupt-driven libtty model: imx6ull registers `interrupt()`
(imx6ull-uart.c:911) and the worker blocks on `condWait` (imx6ull-uart.c:552-565), waking on
RX/TX IRQ. The poll model wastes a CPU slice and adds input latency.
**WHY:** divergence from the established Phoenix tty idiom.
**REC:** this is *plausibly intentional* under TD-14: the kernel owns the PL011 IRQ and writes
the klog mirror to the same `DR`, so a second IRQ owner would conflict. Keep polling for now but
add a one-line comment citing TD-14 as the reason, so a maintainer doesn't read it as an
oversight. Revisit once the two-owner UART split (TD-14) is resolved. **NEEDS-HW** (model change).

### 8. `pl011-tty.c:136, 525` · COMMENT/STYLE · sev=low · leftover unused field + no-op cast
`uint8_t fbescPad;` (136) is an explicit padding byte never read/written — fine as struct
padding but undocumented (reads as a forgotten field). `(void)row;` at 525 with `uint16_t row;`
declared at 474 is a leftover: `row` is never used after the dead-code was removed; the cast
just silences `-Wunused`.
**REC:** delete `uint16_t row;` (474) and `(void)row;` (525); add a `/* padding */` comment to
`fbescPad` or drop it (struct is internal, alignment is satisfied by the `char stack[]` arrays).
**APPLY-SAFE** (dead-variable removal; build smoke).

### 9. `pl011-tty.c:563-607, 791-816` · BUG · sev=low · `pl011_init` error paths leak fbLock mutex on failure; createTty0 budget intentionally tiny
(a) `pl011_init` creates `fbLock` (791) then can `return -ENOMEM` from the `mmap`/`libtty_init`
failures (796/806) without `resourceDestroy(uart->fbLock)`. Caller treats any `<0` as fatal and
`return EXIT_FAILURE`s (main 1122), so the process exits and the leak is harmless in practice —
but it is a textbook error-path resource-lifetime miss for an upstream reviewer.
**REC:** on the post-mutex failure paths, `resourceDestroy(uart->fbLock)` before returning (or
defer `mutexCreate` until after the fallible mmaps).
(b) `createTty0` retry budget is 5×100ms (572-586) — documented under `TODO(TD-14-pl011-retry)`
and deliberately small because tty0 is non-fatal. This is *correctly marked* debt; flagging only
so the synthesis knows it resolves when TD-14 IPC slowness is fixed (comment says "restore to 50").
**NEEDS-HW** (a: trivial but on a fatal-exit path; b: leave as marked debt).

---

## Notes checked and explicitly NOT flagged

- **Framebuffer barrier visibility:** Normal-NC mapping (`MAP_UNCACHED`, 487-502) + a
  continuously-polling VC4 scanout engine has no producer/consumer handshake — NC stores drain
  and become visible without an explicit `dsb`. No missing-barrier bug. (Rubric prompts for it;
  there isn't one here.)
- **`pl011_fbcon_clearAll` fill extent (232):** fills the page-rounded `fbmemsz` (= full
  `pitch*height` rounded up), which is within the mmap; `fbrows*FBFONT_H ≤ height`, so no overrun.
- **`fbaddr` `volatile`-pointer fix (124):** correct and well-justified (cross-thread visibility
  of the mmap result published by `pl011_fbcon_init` after `pl011_thr` start). Good catch by author.
- **Direct kernel-port {0,0} klog attach (712-757):** documented TD-14 workaround for slow
  devfs `/dev/kmsg` lookup; mtOpen/mtRead protocol and -EPIPE handling are coherent. Marked debt.

---

## Summary

10 findings: **BUG 4** (2 med wake-loss in RX paths #1/#2, 1 low fbcon parser interleave #6, 1 low
init mutex leak #9), **ROLLBACK 2** (1 med unmarked klog fprintf #4, 1 low marked mouse-diag
thread #5), **COMMENT 2** (1 med stale/contradictory cache-state comments #3, 1 low leftover
field/cast #8), **ARCH 1** (low poll-vs-IRQ model #7), **STYLE** folded into #8.
By severity: med 4, low 6.

**Most important:** the `wake_reader` accumulation bug (#1 + #2). `libtty_putchar[_unlocked]`
resets `*wake_reader=0` on entry, so both the USB-kbd and serial-RX drain loops lose the reader
wakeup whenever a multi-byte burst ends on a non-waking byte — intermittent input stalls on the
console. The dead `int woke` lines show the author intended OR-accumulation; the fix is to pass a
per-char `&woke` and `|=` it. Of the APPLY-SAFE items, #3 (reconcile cache comments), #4 (drop/gate
unmarked klog fprintf), and #8 (delete `row`/`(void)row`) are safe overnight behind a `--scope core`
build + boot-to-psh smoke; everything touching the input/render control flow is NEEDS-HW.
