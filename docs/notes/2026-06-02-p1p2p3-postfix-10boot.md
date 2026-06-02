# P1/P2/P3 fixes + post-fix 10-boot study (2026-06-02)

Follow-up to `2026-06-02-10-boot-consistency-study.md`. The user asked to solve
points 1–3 from that report's "next steps", then repeat the 10-boot study and
re-analyze. Done. All three landed; the post-fix 10-boot run is dramatically
better than the pre-fix baseline on every axis measured.

## The three fixes

**P1 — removed the PCIe pre-init bridge-state diagnostic dump.**
`usb/xhci/bcm2711-pcie.c`: deleted the USB-FIX-18 block that read 10 RC/MEM_WIN
registers *before* `bcm2711PrepareHostBridge` cleared `SERDES_IDDQ`. Each read hit
the PCIe external-abort/SError recovery path (~10.8 s) returning `0x0`, costing
~90–110 s/boot. Its hypothesis ("firmware leaves the bridge working") was
disproved by its own all-zero output.

**P2 — klog now reaches the HDMI fbcon (split sinks).** The kernel klog ring was
only ever drained to fbcon by libklog resolving `/dev/kmsg` through devfs, which
never attached on Pi 4 (TD-14 devfs fragility) — so HDMI showed only banner+psh.
- `kernel/log/log.c`: the kernel UART **mirror is now permanent** (raw per byte),
  not gated on `readers`. The UART is the kernel's own complete, deterministic
  log regardless of any userspace reader.
- `tty/pl011-tty/pl011-tty.c`: replaced libklog's devfs drain with
  `pl011_klogthr`, which attaches **directly to the kernel log port** (oid
  `{port:0,id:0}`) by message and writes the drained ring to the **fbcon only**
  (never the UART). So UART (mirror) and fbcon (drain) never double up.
- Reverted the earlier libklog retry hack (no longer used; keeps the shared lib
  upstream-clean).

**P3 — serialized kernel console writes.**
- `kernel/hal/aarch64/generic/console.c`: `hal_consolePrint` now takes
  `console_common.lock` for the whole (attr+string+reset) sequence, matching
  `hal_consolePutch`. Every userspace `debug()` (which routes through
  `hal_consolePrint`) and the SMP diagnostics are now atomic per line.
- `kernel/syscalls.c`: removed the `syscalls: psh root lookup` trace — the one
  kernel `lib_printf` still emitted *during* userspace activity, i.e. the only
  splittable (mirror) line that the atomic `hal_consolePrint` writers could
  splice into. With it gone the UART is fully un-garbled.
- (An intermediate attempt to line-buffer the mirror via `hal_consolePrint` was
  reverted: that call appends a `\033[0m` reset per flush, which a partial-line
  flush spliced into the log text — worse than the original. The raw per-byte
  mirror + removing the one contended line is the correct, faithful fix.)

## Post-fix 10-boot study (labels fix01..fix10, image SHA 37e6decd, unchanged)

| axis | pre-fix baseline (boot01–10) | post-fix (fix01–10) |
|------|------------------------------|---------------------|
| boot span | 165.9–177.5 s | **66–74 s** (~100 s faster) |
| UART line count | 506–529 (±2 %) | **exactly 520, all 10** |
| per-line stalls | ~136–150 s (PCIe dump) | ~37–45 s (firmware/net only) |
| HDMI final frame | 1 md5, but banner+psh only | **1 md5, full kernel log** (MATCH) |
| klog→fbcon | never attached | `klog attach rc=0` every boot |
| UART garble | char-level interleave | none (520 lines exact ⇒ no split/merge) |
| dummyfs init | 2 | 2 |
| **USB enum_fail** | **7/10 boots failed** | **0/10 (all clean)** |
| **xhci_timeout** | 0–3 (varied) | **0 all boots** |
| **usbpool_alloc** | 40–48 (varied) | **48 all boots** |

### What the stalls are now
Every remaining ≥5 s gap is **firmware/network**, not Phoenix: VideoCore
`USB MSD stopped. Timeout: 25 s` (~23 s), `DHCP src`/`LINK STATUS` settling
(~6–8 s), `brfs: loader.disk` read (~5 s), and the tail of the in-kernel D-8
`smp: tick+15s` diagnostic sleep (8 s). The old ~108 s PCIe-register stall is
completely gone.

### Determinism
Phoenix's internal timeline is again constant: `genet_linkup − plo_banner` ≈
8.6–8.7 s on every boot; the Δ=8.0 s landmark spread is entirely the pre-plo
firmware/DHCP offset (host/network side). UART line count is now *exactly* 520
on all ten boots — the strongest possible evidence the console is no longer
garbling (garble splits/merges lines and would vary the count).

### Notable: USB enumeration went 3/10 → 10/10 clean
This was the genuine boot-to-boot variance in the pre-fix baseline (7/10 boots
logged `Enumeration failed despite 3 attempts`). In the post-fix run all 10
enumerated the hub + PIXART mouse + Logitech keyboard with **zero** failures or
xhci timeouts, and `usbpool_alloc` is a constant 48.

**Same hardware, so this is a real shift — and P1 is the lead cause.** The
"different devices" confound is ruled out: the baseline `boot01–10` logs contain
the same device VIDs (VIA hub `2109:3431`, Logitech kbd `046d:c31c`, PIXART mouse
`093a:2510`), and baseline failures were `Enumeration failed despite 3 attempts`
(device present, enum incomplete — not `insertions=0`). The 2 baseline boots that
*did* succeed (boot03, boot10) show fuller enumeration. So the same hub+kbd+mouse
were attached throughout. The only image delta baseline→fix is P1/P2/P3; P2/P3
are console-only and cannot touch USB logic — which points at **P1**: deleting the
diagnostic removed 10 RC-register reads issued *before* `bcm2711PrepareHostBridge`
clears `SERDES_IDDQ`, each of which triggered a PCIe external-abort
(`project_pi4_serror_pcie_source`). Eliminating ~10 abort events immediately
before VL805 bring-up is a coherent mechanism for enum going flaky→deterministic.
A 7/10→0/10 shift is not sample noise (~1e-5 under the baseline failure rate).
**Confirm definitively** by toggling only P1 (restore the dump, hold P2/P3) and
re-running — but the evidence already strongly implicates P1.

## Status of the three points
- P1 ✅ done (boot ~100 s faster, deterministic).
- P2 ✅ done (HDMI carries the full kernel log, identical across 10 boots).
- P3 ✅ done (kernel boot log fully un-garbled — 520 lines exact ×10;
  psh-interaction-time interleave with pl011-tty remains, tracked separately).

## Follow-ups (flagged, not bundled)
- The D-8 `smp:` bring-up diagnostic block (main.c) — SMP is long-validated; it
  prints 5 lines and does an in-kernel 15 s sleep that tails every boot. Strong
  candidate for removal under the "remove disproved diagnostics" rule.
- Confirm whether the USB enum 3/10→10/10 improvement is causally P1 (re-run the
  pre-fix vs post-fix with identical physical USB devices).
- Residual UART caveat: the kernel console (mirror + `hal_consolePrint`) cannot
  serialize against pl011-tty's own UART writes (psh output, userspace, separate
  mmap). During psh interaction late prints could still interleave — the
  two-UART-owner issue (TD-14), out of scope for P3.
