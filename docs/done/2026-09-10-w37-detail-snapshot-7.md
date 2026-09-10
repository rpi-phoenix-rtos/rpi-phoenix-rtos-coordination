# W37 detail snapshot 7 (2026-09-10) — pre-main() hang investigation

Carved out of the weekly log to keep it readable. Superseded content is kept verbatim.

### 2c. ⚠ NEW, MEASURED: an app hangs BEFORE `main()` about 1 run in 7

QuakeSpasm's gate cycle produced **no output at all** after the command echo. I first called it a
regression, then "1 failure in 2" — both wrong, and both were 2-sample readings. Measured properly
with a 5-trial bench: **5/5 launched**, ~5150 frames each (35 fps). Across all 7 post-`strncmp`
QuakeSpasm runs the rate is **1 failure in 7 (~14%)**.

What it is, from three cheap observations:

* A passing run prints `quakespasm: main() entered` on the **very next line** after the echo. The
  failing run printed **nothing** → it never reached `main()`.
* The failing run had a **longer** window (gate: `idle 240 / max 300`) than the five passing trials
  (`120 / 150`) → **not** slow-exec of the 18.5 MB ELF, a genuine hang.
* No `(psh)%` prompt ever returns after the echo → psh is still blocked, so the process **hung**
  rather than exiting silently.

So the hang is in libphoenix's pre-`main` startup: `crt0-common.c:_startc()` runs `_libc_init()`,
then `_init_array()` (every static constructor), then `main`. Two candidates, no third.

⚠ **PARTLY RETRACTED — I claimed a cause I had not tested.** The observation holds: the Pi's own
`rpi4-sysinfo` prints `/etc/build-versions appeared after 5000 ms (root filesystem mounted late)`,
and in the logs **1** line between `registered / (takeover)` and the command echo ⇒ hung, **23**
lines ⇒ ran. But I then asserted the gate's missing `--inter-cmd-secs` was the *cause*, and a direct
test refutes it: a cycle run with **`--inter-cmd-secs 0`** still produced **gap=23 and reached
`main()`** (2964 frames). The knob does sleep before the first command (`psh-interact.py:216`), so
it is not that the flag was inert — the echo's position is dominated by serial/scheduling latency,
not by that delay. psh prompts at log line ~94 while the takeover lands at ~116, so the command can
in principle arrive pre-takeover, but the harness cannot be made to do it on demand.

**Where that leaves it:** racing the root settle is still the best explanation of the one failure
(4 data points, all consistent), but it is **not reproducible on demand** and the gate change is
**added margin, not a demonstrated fix**. Rate stands at 1 in 8 (~12%).

⚠ **The underlying defect is still open and is mine to fix:** an exec that races the root mount
should **fail cleanly, not wedge**. It produces no diagnostic at all and leaves psh without its
prompt. Localised to `crt0-common.c:_startc()`'s pre-`main` steps (`_libc_init()`, then
`_init_array()`); note `debug()` is a raw syscall (`ID(debug)`) and so is usable that early, while
**psh cannot set env vars**, so any trace must be compile-time gated.

