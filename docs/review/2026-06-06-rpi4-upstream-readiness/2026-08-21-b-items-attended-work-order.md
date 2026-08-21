# B-items attended-pass work order — 2026-08-21

Supersedes the location table in `2026-08-10-b1-b14-reverification.md` for the
**5 still-real** items. Owner decision E8/G-UPSTREAM is an *attended* pass whose
first half is "re-verify relevance." This document discharges that: every
still-real item is re-checked against **today's** tree (line numbers moved since
2026-08-10), the exact diff is pre-written, and the single HW test that would
actually confirm the fix is stated. **None of these should be merged unattended**
— each behavioral delta is invisible to an unattended netboot smoke, so a green
boot proves nothing (see the per-item "confirms" line). Apply + verify these in a
live/HW session, then push.

Status unchanged since 2026-08-10: FIXED = B1(live),B4,B6,B7a,B9,B10,B11;
not-actionable = B3 (deliberate #121 crash-guard), B12 (POSIX-correct divergence),
B13 (justified). The 5 below are the whole remaining upstream-defect surface.

---

## B14 — xHCI PORTSC RW1C over-clear (correct-by-construction; LOW value)

`sources/phoenix-rtos-devices/usb/xhci/xhci.c:3191,3195,3199,3203`
(RW1C mask = CSC|PEC|OCC|PRC, defined 124-125). The four `C_*` clear cases write
`(portsc & ~PED) | <one C-bit>`, leaving the *other* RW1C change-bits set in the
written value → any sibling change-bit reading 1 gets re-cleared (event lost).
The ENABLE/POWER cases (3207/3211) already mask `~RW1C` correctly.

```c
// 3191 (C_CONNECTION):
-  xhci_portWrite32(xhci, port, XHCI_REG_OP_PORT_PORTSC, (portsc & ~XHCI_REG_OP_PORT_PORTSC_PED) | XHCI_REG_OP_PORT_PORTSC_CSC);
+  xhci_portWrite32(xhci, port, XHCI_REG_OP_PORT_PORTSC, (portsc & ~(XHCI_REG_OP_PORT_PORTSC_PED | XHCI_REG_OP_PORT_PORTSC_RW1C)) | XHCI_REG_OP_PORT_PORTSC_CSC);
// 3195 (C_ENABLE):        ...| XHCI_REG_OP_PORT_PORTSC_PEC);   (same ~(PED|RW1C) mask)
// 3199 (C_OVER_CURRENT):  ...| XHCI_REG_OP_PORT_PORTSC_OCC);
// 3203 (C_RESET):         ...| XHCI_REG_OP_PORT_PORTSC_PRC);
```

Correctness note: strictly more correct — preserving a real sibling change-bit is
never worse than dropping it. Blast radius: the hub driver (keys on RW1C at
3529-3533) will now *process* port-change events it previously dropped, in the
#121-sensitive subsystem — so verify, don't assume.

**Confirms:** a hub with ≥2 devices, two ports changing state near-simultaneously
(e.g. hot-unplug two at once), then check both `C_*` events are reported to the
hub driver (no lost event). Single-device enumeration emits the identical write
to the old code, so kbd/mouse boot enumeration does NOT exercise the delta.

---

## B2 — xHCI shared inputCtx realloc leak (small; MEDIUM value)

`sources/phoenix-rtos-devices/usb/xhci/xhci.c:1902` (in `xhci_allocSlotSpace`,
1882; called per-device from 2243 and 2933). `xhci->inputCtx` is a shared
per-controller scratch buffer, re-`usb_allocAligned`'d on every call with no free
of the prior pointer → each device behind a hub leaks one input context (freed
only at teardown). The `memset(xhci->inputCtx, 0, ...)` at 1915 re-zeros it every
call, so reuse is safe.

```c
-  xhci->inputCtx = usb_allocAligned(xhci->inputCtxSize, XHCI_CONTEXT_ALIGN);
-  if (xhci->inputCtx == NULL) {
-      fprintf(stderr, "xhci: failed to allocate input context\n");
-      return -ENOMEM;
-  }
+  /* Shared per-controller scratch buffer reused across slot setups (memset
+   * below re-zeros each call); allocate once or every device behind a hub
+   * leaks the previous one. */
+  if (xhci->inputCtx == NULL) {
+      xhci->inputCtx = usb_allocAligned(xhci->inputCtxSize, XHCI_CONTEXT_ALIGN);
+      if (xhci->inputCtx == NULL) {
+          fprintf(stderr, "xhci: failed to allocate input context\n");
+          return -ENOMEM;
+      }
+  }
```

**Confirms:** attach a hub with ≥2 devices and enumerate several times; watch
`usb_alloc` / heap accounting stay flat per slot setup (currently grows by one
`inputCtxSize` per device). A single kbd+mouse boot calls this only twice, below
the noise floor.

---

## B7b — genet missing DMA barriers (defensive; netboot-CRITICAL, verify hard)

`sources/phoenix-rtos-lwip/drivers/bcm-genet.c`. `genet_write` (215) is a bare
`volatile` store; the only `dsb sy` (254/258) sits inside the disabled
`#if GENET_RX_CACHEABLE` block (=0 at line 97). The TX producer-index doorbell at
**1160** is written without fencing the preceding descriptor + Normal-NC payload
writes; the RX-done→payload-read edge is the mirror case.

```c
// ~1159-1160 (TX doorbell):
   state->tx_prod_index = (state->tx_prod_index + 1u) & 0xFFFFu;
+  /* Fence descriptor + Normal-NC payload writes before the Device-MMIO
+   * producer-index doorbell (bare volatile store has no implicit barrier). */
+  __asm__ volatile("dsb sy" ::: "memory");
   genet_write(state, ring_off + GENET_TDMA_RING_PROD_INDEX, state->tx_prod_index);
```

Plus a `dsb sy` after the RX DMA-done check, before reading the Normal-NC payload.
Empirically netboot NFS-root is reliable (Normal-NC ordering is looser-but-adequate
on this SoC), so this is latent/defensive.

**Confirms:** sustained TX/RX stress (bulk NFS read + write concurrently, or a
throughput bench) over many cycles looking for a payload-corruption that the
current code does NOT exhibit at normal load — i.e. proving the *absence* of a
rare corruption, which needs a stress rig. **CAUTION: genet is the
netboot-critical path** — after applying, run a full netboot + NFS bulk-read
regression before trusting it.

---

## B8 — libtty batch-wake collapse (small edit, SHARED libtty; MEDIUM value)

`sources/phoenix-rtos-devices/tty/libtty/libtty_disc.c:204`.
`libtty_putchar_helper` does `*wake_reader = 0` at entry (204) and only ever sets
`= 1` (317, 326). pl011-tty's unlocked batch path calls `libtty_putchar_unlocked`
in a loop then one wake check, so the entry-reset makes only the LAST char's wake
decision survive → a completed line mid-burst can lose its reader wakeup. (The
locked path is saved by per-char `condSignal`.)

Fix: remove the entry-reset; make the helper *only ever raise* `*wake_reader`, and
have each caller initialize `*wake_reader = 0` once before its batch loop.

```c
static int libtty_putchar_helper(...) {
-  if (wake_reader != NULL) {
-      *wake_reader = 0;
-  }
   ...
}
```

**Caller audit REQUIRED before applying** (shared libtty, all arches): confirm
every caller of `libtty_putchar`/`libtty_putchar_unlocked` initializes
`*wake_reader = 0` before the (possibly single-iteration) batch — `libtty_putchar`
(340) and the pl011-tty batch loop (pl011-tty.c ~1187-1194) at minimum. Higher
blast radius than the others (every tty on every target).

**Confirms:** feed a multi-char burst that completes a line partway through the
burst (line + trailing chars in one write) and assert the reader wakes on the
completed line. Hard to repro on the netboot console (input arrives char-at-a-time
from the harness, which never batches).

---

## B5 — kernel early-console alias (medium fix; WEAK verify on rpi4)

`sources/phoenix-rtos-kernel/hal/aarch64/generic/console.c`.
`hal_consolePrint` (58) → `_hal_consoleEarlyPrint` (42) → `_hal_consoleEarlyPutch`
(30) writes the hardcoded alias `0xffffffffffe00000` (32); only `hal_consolePutch`
(klog mirror) uses the DTB-discovered `console_common.uart`. The alias is
load-bearing: `hal_consolePrint` must work *before* `_hal_consoleInit` (comment
66-68), so a naive redirect drops early boot output.

Fix (conditionalize early-vs-discovered):
```c
static void _hal_consoleEarlyPutch(char c) {
-  volatile u32 *uart = (volatile u32 *)0xffffffffffe00000ull;
+  /* Pre-init (console_common.uart not yet discovered) use the fixed alias so
+   * early boot output survives; after _hal_consoleInit, use the DTB base. */
+  volatile u32 *uart = (console_common.uart != NULL)
+      ? console_common.uart
+      : (volatile u32 *)0xffffffffffe00000ull;
   ...
}
```

**Confirms — WEAK on rpi4:** on the Pi4 the alias == the discovered UART base, so
a boot cannot distinguish the paths (output is identical either way). Real
verification needs a target where the DTB UART base differs from the alias. On
rpi4, only a non-regression check (boot banner + klog intact) is possible; the
actual fix value is for *other* aarch64-generic boards. Consider deferring B5
below the others (lowest rpi4-relevance).

---

## Recommended attended order

1. **B2** — clean, self-contained, real leak, testable with a hub. Do first.
2. **B14** — trivial + correct-by-construction; bundle with B2 (same file), one hub test covers both.
3. **B8** — do the caller audit, then apply; test on a real terminal.
4. **B7b** — defensive; apply with a full netboot + NFS stress regression (netboot-critical).
5. **B5** — lowest rpi4 value (alias==discovered); apply for cross-board upstreamability only.
