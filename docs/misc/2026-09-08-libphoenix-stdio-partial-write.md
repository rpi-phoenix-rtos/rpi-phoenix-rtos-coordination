# libphoenix stdio: a partial write duplicates output and wedges the stream (found 2026-09-08)

**Status: root-caused, fix proposed, NOT implemented.** It is a `--scope core` change to the
most-used code path in the system, so it wants its own turn plus a broad rebuild + soak.


---

## ⚠️ CORRECTION (2026-09-08, later the same day): the flood was NOT this bug

The 234k-line UART flood described below as the motivating symptom is a **host-side capture
artifact**, not target output. The stdio defect and its fix are still real and are proven by the
regression test (`phoenix-rtos-tests 9eb0343`: FAILS without the fix with
`Expected 109 Was 97`, PASSES with it) — but they are **not** what produced the flood, and the
"open question" this document originally ended on (why a *tail* rather than a *prefix* repeats)
is answered by the artifact, not by stdio.

Evidence, all from logs already on disk:

* **The flood recurred with the fix in place** — `rpi4b-uart-20260908-043854-stdiotest.log`,
  235 121 lines, captured on a build that already contained `libphoenix 9029813`.
* **It appears in logs of runs that never executed the flooding program.**
  `rpi4b-uart-20260907-223725-wmexit2.log` is a Window Maker test; it is flooded with 360 275
  copies of `vkvid: present 4170` — a **vkQuake** line. vkQuake was not running.
* **It sits entirely before the boot banner in every case** (e.g. flood lines 1..367 764, banner
  at 367 978), i.e. in the window where `capture-rpi4b-uart.sh` has the serial tool open but the
  Pi is still powered off.
* **A spliced variant proves host-side re-reading**: 7 489 copies of
  `vkvid: pvkvid: present 4170` — the same buffer re-served at a shifted offset. A target
  writing that string would never produce it.
* ~7.5 MB of identical content is far more than any USB-UART adapter buffers, so the driver is
  re-serving its last buffer in a loop rather than blocking or reporting EOF while the device is
  unpowered.

Mitigation shipped: `scripts/collapse-uart-log-floods.py`, called non-fatally at the end of
`capture-rpi4b-uart.sh`. It collapses runs of >=200 identical consecutive lines to one line plus
an explicit `[collapse-uart-log-floods: previous line repeated N more times]` marker, so every
distinct line and the repeat count survive and the artifact labels itself. Verified on real
flooded logs (7.79 MB -> 386 KB; 7.32 MB -> 16 KB with `uart-summary.sh` output unchanged) and
harmless on clean ones (no-op). It has **not** yet been observed firing in a live cycle — the
artifact is intermittent (~10 of ~60 recent captures) and did not recur in two deliberate
attempts.

**Lesson:** the flood was mistaken for a target bug twice — first as a vkQuake per-frame
diagnostic, then as this stdio defect. The discriminator that settles it is cheap and was
available the whole time: *check whether the flooded line could have been produced by the program
that was actually running, and where the flood sits relative to the boot banner.*

---
## How it surfaced

A Quake II soak log was **7 MB / 228k lines**. Almost all of it was one 30-byte fragment
repeated verbatim:

```
youtPC=0 | vrect=1920x1080@0,0\r\n     x 234113
```

That is the **tail** of a vkQuake per-frame diagnostic whose full text is

```
vkvid: 2D draws=0 drawIdx=8 drawIndirect=80 outsideRP=0 nullLayoutPC=0 | vrect=1920x1080@0,0
```

(`tools/vkquake-port/platform/pl_phoenix_vk_vid.c:1171`, followed immediately by `fflush(stdout)`).

Two facts rule out the obvious explanations:

* **It is not a logging-gate bug.** The gate is `present_count < 30 || (present_count % 60) == 0`.
  Both compared trials reached `present_count = 2580` with 0 `vkQueueSubmit` errors, and both
  contain exactly **73** *intact* diagnostic lines — precisely `30 + 2580/60`. The gate is correct.
* **It is intermittent.** `torchD-T1`'s log holds 234k fragments; `torchD-T2`'s holds none
  (718 lines total). The fragments always begin at **log line 1**, i.e. they are the tail of the
  *previous* trial, captured before the power-cycle. Roughly 1 vkQuake run in 4.

`Sys_Printf` is a bare `vprintf` (`pl_phoenix_sys.c:88-94`), so this is **libphoenix stdio /
the tty write path**, not port glue — it can hit any program that prints under load.

## Root cause

`sources/libphoenix/stdio/file.c`:

```c
static ssize_t full_write(int fd, const void *ptr, size_t size)      /* :379 */
{
        while (size > 0) {
                err = __safe_write_nb(fd, ptr, size);
                if (err < 0) {
                        return (errno == EAGAIN) ? total : -1;   /* SHORT return */
                }
                ptr += err; total += err; size -= err;
        }
        return total;
}

static int __fflush_one(FILE *stream)                                /* :398 */
{
        if ((stream->flags & F_WRITING) != 0) {
                if (stream->bufpos != 0) {
                        err = full_write(stream->fd, stream->buffer, stream->bufpos);
                        if (err != stream->bufpos) {
                                stream->flags |= F_ERROR;       /* bufpos NOT advanced */
                                ret = -1;
                        }
                        else {
                                stream->bufpos = 0;
                        }
                }
        }
        ...
```

`full_write` returns a **short count** when the descriptor is non-blocking and the tty FIFO is
full — exactly what happens when a program prints faster than the UART drains. `__fflush_one`
then treats "short" identically to "keep everything": `bufpos` is left spanning the whole buffer.

Consequences, all real:

1. **Output is duplicated.** The prefix that *was* written goes out again on the next flush,
   because the next `full_write` starts from `stream->buffer` with the same `bufpos`.
2. **The remainder is never retried** as a remainder — only as part of the whole buffer.
3. **`F_ERROR` sticks forever** on a stream that merely hit backpressure, so `ferror()` reports a
   failure that did not happen. EAGAIN is not an error.

## Proposed fix

```c
err = full_write(stream->fd, stream->buffer, stream->bufpos);
if (err < 0) {
        stream->flags |= F_ERROR;
        ret = -1;
}
else if ((size_t)err < stream->bufpos) {
        /* Non-blocking fd hit EAGAIN mid-buffer: drop what went out, keep the rest so the
         * next flush resumes instead of re-transmitting the prefix. Backpressure is not an
         * error, so F_ERROR is NOT set. */
        memmove(stream->buffer, stream->buffer + err, stream->bufpos - (size_t)err);
        stream->bufpos -= (size_t)err;
        ret = -1;   /* flush incomplete */
}
else {
        stream->bufpos = 0;
}
```

## Caveats — read before implementing

* **Not fully closed:** this mechanism explains *duplication of the prefix*. The observed symptom
  is the **tail** repeated, which needs one more step (likely how `__safe_write_nb` / the pl011
  tty report a partial accept, or a second flush path). Instrument
  `full_write`'s short return (log `total` vs `size` once) on the next vkQuake run before
  claiming the fix resolves the flood.
* `ret = -1` on a partial flush keeps `fflush()` reporting incomplete, which is right, but check
  no caller treats that as fatal — `fseek_unlocked` (`:920`) bails on `__fflush_one < 0`.
* Also worth deciding whether `full_write`'s EAGAIN short-return should instead poll/retry; the
  current contract pushes the decision to every caller, and `__fflush_one` is only one of them.
* Requires `./scripts/rebuild-rpi4b-fast.sh --scope core` (stale-core hazard) and a soak across
  the games + desktop, since every printing program is affected.

## Separate, smaller cleanup

`pl_phoenix_vk_vid.c` still carries bring-up markers whose own comment says to remove them "once
frames sustain on HW" (`:1201` TODO, plus the `present_count < 8` submit/wait brackets at
`:1202-1218`). Frames now sustain (2580 presents/run). Removing them shrinks the UART load that
triggers this bug in the first place — worth doing with, or before, the stdio fix.
