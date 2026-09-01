# devices-pcie-server — upstream-readiness review

- **Area:** `devices-pcie-server`
- **Repo:** `phoenix-rtos-devices` (base `d511e0f` → head `ebac8e4`)
- **Files reviewed (changed hunks only):**
  - `pcie/server/pcie.c` (+853)
  - `pcie/server/Makefile` (+4)
- **What changed:** the generic ECAM PCIe enumeration server was refactored behind a
  `pcie_cfgio_t` vtable (read32/write32/destroy) so a second config-space backend can be
  plugged in, and a BCM2711 "indexed config" backend (`PCI_EXPRESS_BCM2711_INDEXED_CFG`)
  was added: host-bridge bring-up, link/PERST sequencing, outbound window + inbound
  RC_BAR2 programming, VL805 firmware-mailbox notify, BAR0 programming, and a large amount
  of `debug()` instrumentation.

---

## Findings (ordered by severity)

### 1. `pcie/server/pcie.c:452` (`bcm2711EncodeBar2Size`) · **BUG** · sev=high · NEEDS-HW
**WHAT:** The inbound RC_BAR2 size encoder seeds `shift = 20` and counts right-shifts:
```c
unsigned shift = 20;
uint64_t value = size;
while ((value > 1u) && ((value & 1u) == 0u)) { value >>= 1; shift++; }
...
return shift - 15u;
```
For the actual call `bcm2711SetRcBar2(bcm, 0u, 0x100000000ull)` (4 GiB, `size = 2^32`),
the loop runs 32 times → `shift = 52` → returns `52 - 15 = 37`. The caller masks with
`BCM2711_PCIE_RC_BAR2_SIZE_MASK = 0x1f`, so `37 & 0x1f = 5`, which the bridge interprets
as a **1 MiB** inbound window instead of 4 GiB.

**WHY:** This is the *same bug the sibling driver already fixed and documented.* The xHCI
copy `usb/xhci/bcm2711-pcie.c:594` (`bcm2711EncodeBar2Size`, USB-FIX-12, 2026-05-26)
states verbatim: "the previous implementation started `shift = 20` … for 4 GB input the
loop ran 32 times producing shift=52 and return value 37 … 37 silently truncated to 5 --
which the bridge interprets as a 1 MB window … VL805's inbound TLP had no valid PCIe-side
destination … root cause of the rc=-110 wedge." The PCIe-server copy carries the **old,
buggy** seed; the field encoding per Linux `pcie-brcmstb.c` is `log2(size) - 15`
(seed `shift = 0`). The `shift < 15u` guard is also effectively dead with the `+20` seed.
**REC:** Replace with the corrected form already in `usb/xhci/bcm2711-pcie.c:594`: seed
`shift = 0`, find `log2(size)` by counting trailing zeros, guard
`value != 1 || shift < 15 || shift > 46`, return `shift - 15`. (Better: factor the encoder
into a shared header so the two copies cannot drift again.) Cannot be HW-validated
overnight — document only. **Referent:** `usb/xhci/bcm2711-pcie.c:594-633`.

---

### 2. `pcie/server/pcie.c` (≈188-252, 273, 470, 685, 727, 752-902, 956-1031) · **ROLLBACK** · sev=high · mixed
**WHAT:** Pervasive diagnostic instrumentation: ~9 inline blocks of the form
```c
{ extern void debug(const char *s); char m[80]; snprintf(m, sizeof(m), "...", ...); debug(m); }
```
plus the standalone diag-outbound MMIO read in `scanFunc` (≈810) and the **30×100 ms
VL805 "warm-up" loop** in `main()` (≈1000-1031), and the mid-file
`#include <sys/debug.h>` (≈960) whose own comment says "Remove once VL805 BAR-programming
is fixed."

**WHY:** This is diagnostic-only code that must not ship to maintainers. The PCIe server's
established operational logging idiom is `printf`/`fprintf` (e.g. the unchanged
`print_capabilities` `printf("pcie: CAP id ...")` and `scanFunc` `printf("pcie: %02x:...")`).
The `debug()` blocks duplicate those prints (the `scanFunc` block at ≈727 is a verbatim
copy of the `printf` two lines above it).

**REC:** Be surgical — separate "delete the log line" from "delete the logic":
- **APPLY-SAFE (pure logging, safe with build+boot smoke):** the `debug()`/`snprintf`
  blocks that only print — `print_bars` `BAR%d raw` block (≈685), the `scanFunc` duplicate
  print (≈727), `bcm2711PrepareLinkState` `linkUp/rcMode` print (≈470), the cmd-readback
  print (≈835), and the `main()` enter/pre/post/exit traces — plus removing the mid-file
  `#include <sys/debug.h>` once no `debug()` callers remain.
- **NEEDS-HW (touches behavior — document only):** the diag-outbound MMIO read (≈810) and
  the `main()` warm-up loop (≈1000) are *diagnostic-flavored* but their comments claim
  they are empirically required for `xhci_capProbe` to succeed; removing them can change
  boot. These are hacks that need a principled fix (see finding #6), not a blind delete.
- The `debug()` lines *inside* the load-bearing VL805 block (finding #5) are APPLY-SAFE to
  strip; the writes around them are not.

---

### 3. `pcie/server/pcie.c` (759, 776, 956, 1000) TODO markers · **COMMENT** · sev=med · APPLY-SAFE
**WHAT:** The new code is tagged with `TODO`/marker strings `TD-USB` and
`TD-15 Stage 4 phase 2`. `TD-USB` is **not a tracked debt ID** —
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` has no `TD-USB` entry. `TD-15`'s documented
scope is "Pi 4 VideoCore VI memory hygiene + 4 GiB DRAM enablement" (doc line 1140), not
PCIe BAR programming, so "TD-15 Stage 4 phase 2" misattributes this PCIe diagnostic code
to an unrelated debt item.
**WHY:** Per the review rubric step 4(a), diagnostic/temporary code carrying **no valid
marker** must be surfaced; bogus markers also defeat the `grep -n "TD-xx"` reconciliation
the project relies on for cleanup. They will read as noise to maintainers.
**REC:** When the diagnostic code in finding #2 is removed, these markers go with it. Any
genuinely-transitional logic that survives (VL805 mailbox/BAR hack, finding #6) should get
a *real* tracked debt entry in `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` and a matching
`TODO(TD-nn)`. **Referent:** `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` (TD-15 §, line 1140).

---

### 4. `pcie/server/pcie.c:227` (and 8 more sites) · **STYLE** · sev=med · APPLY-SAFE
**WHAT:** `debug()` is re-declared inline at every call site with
`extern void debug(const char *s);` inside a local block — 9 occurrences — while the
prototype actually lives in `<sys/debug.h>` (which the file *does* include, but mid-file
at ≈960 behind a diagnostic comment rather than at the top with the other includes).
**WHY:** Re-declaring a libc/system prototype at each call site is not the Phoenix idiom.
Drivers that use `debug()` include `<sys/debug.h>` once at the top and call `debug()`
directly. **Referent:** `multi/grlib-multi/grspw2.c` and `multi/imxrt-multi/imxrt-multi.c`
both `#include <sys/debug.h>` at file top and call `debug()` without local re-declaration.
**REC:** Mostly moot once finding #2 removes the `debug()` calls. If any `debug()` survives,
move the include to the top include block and drop all inline `extern` re-declarations.

---

### 5. `pcie/server/pcie.c:745-755` (VL805 BME-before-reset) · **ARCH/BUG** · sev=med · NEEDS-HW
**WHAT:** Inside the `#if defined(PCI_EXPRESS_BCM2711_INDEXED_CFG)...` block, before the
firmware mailbox notify, the code force-enables MEM-space + Bus-Master on the VL805
(`write32(PCI_COMMAND, cmd | MEM | MASTER)`), with a comment that the ordering is
empirically required ("enabling BME *after* the firmware reset leaves VL805 in a state
where capability reads return 0xdead"). This duplicates the generic BME-enable that
`scanFunc` already does ~80 lines later, and embeds device-specific ordering magic in the
generic enumerator.
**WHY:** It is load-bearing (not just diagnostic), so it cannot be removed without HW; but
it also couples the generic server to one device's quirk. The `0xdead` symptom is the same
silicon path tracked in `project_bcm2711_pcie_64bit_bug` / the VL805 firmware-handoff race.
**REC:** Document; do not blind-apply. For upstreamability this VL805-specific sequence
(BME-before-reset + mailbox notify + fixed `usleep` + BAR0 program) belongs behind a clear
quirk hook rather than inline in `scanFunc`. Strip the `debug()` lines inside it (APPLY-SAFE)
but keep the writes pending HW review. **Referent:** the generic BME-enable in this same
`scanFunc` (≈848-862) shows the intended single point of MEM/MASTER enable.

---

### 6. `pcie/server/pcie.c:775` & 1000-1031 (fixed `usleep` settle + warm-up loop) · **ARCH** · sev=med · NEEDS-HW
**WHAT:** A hardcoded `usleep(200000)` "settle" after the firmware reset, and a separate
30-iteration × 100 ms MMIO read loop in `main()` to keep the bridge "warm" while xHCI's
`capProbe` runs. The comments themselves flag these as pragmatic stand-ins ("Future cleanup
should poll for stable Vendor ID / caplen reads instead of a fixed delay"; Linux waits for
the device to leave CRS).
**WHY:** Fixed delays + a polling "warm-up" loop racing another process are fragile and
will draw maintainer objection. They are behavior-affecting, so removal needs HW.
**REC:** Document. The principled fix is a CRS-aware Vendor-ID poll (the code already sets
`PCI_EXP_RTCTL_CRSSVE` in `bcm2711ExposeDownstreamBridge`, so CRS Software Visibility is
enabled — poll for it) instead of fixed sleeps and a cross-process warm-up loop.
**Referent:** `bcm2711ExposeDownstreamBridge` (≈540-544) already enables CRSSVE, so the
hardware support for the proper poll is present in this same file.

---

### 7. `pcie/server/pcie.c:534-535` (`PCI_MEMORY_LIMIT` = base) · **BUG** · sev=low · NEEDS-HW
**WHAT:** `bcm2711ExposeDownstreamBridge` programs the bridge memory window with
`MEMORY_BASE == MEMORY_LIMIT == OUTBOUND_PCIE_BASE >> 16`, i.e. a window that is `< 1 MiB`
(base == limit ⇒ a single 1 MiB granule).
**WHY:** Standard PCI-PCI bridge forwarding requires `limit >= base + size - 1`. This works
only because the single VL805 BAR is placed exactly at `OUTBOUND_PCIE_BASE`; a device with
a BAR higher in the window would not be forwarded. Fine for the current single-device
bring-up, surprising as generic code.
**REC:** Document. Set `MEMORY_LIMIT` from `OUTBOUND_PCIE_BASE + OUTBOUND_SIZE - 1` so the
forwarded window matches the outbound window actually programmed in
`bcm2711SetOutboundWindow0`. **Referent:** `bcm2711SetOutboundWindow0` (≈470-495) computes
a real base/limit pair from `+ size - 1`; the bridge MEMORY_BASE/LIMIT should match.

---

### 8. `pcie/server/pcie.c:928, 945` (`vendor_id == 0x0000` in generic scan) · **COMMENT/ARCH** · sev=low · NEEDS-HW
**WHAT:** `pcie_scanBus` now treats `vendor_id == 0x0000` as "no device" alongside the
spec-mandated `0xffff`, with a multi-line BCM2711-specific justification comment sitting in
the generic enumerator (shared with the ECAM and xilinx backends).
**WHY:** The behavior is harmless (vendor ID 0 is universally invalid, so no real device is
skipped), and on BCM2711 it is necessary (empty slots read all-zeros). But the
BCM2711-only rationale embedded in generic code is misleading to a maintainer reading the
ECAM/xilinx path. **Referent:** the original comment in this same function spoke only of
the spec `0xffff` "all ones" behavior; the new zeros case is backend-specific.
**REC:** Keep the `0x0000` check (it is safe and load-bearing for BCM2711), but trim the
comment to a backend-neutral note ("some root complexes return all-zeros for empty slots")
or move the rationale into the BCM2711 backend. Low priority.

---

### 9. `pcie/server/pcie.c:207, 221, 223` (mailbox `munmap` blocks) · **STYLE** · sev=low · APPLY-SAFE
**WHAT:** `bcm2711NotifyXhciReset` error-path `munmap`/`return` lines are indented with a
tab followed by 8 spaces instead of tabs, diverging from the file's tab indentation.
**WHY:** Phoenix uses clang-format with tab indentation; this is mechanical drift.
**REC:** Run clang-format / re-indent to tabs. **Referent:** the surrounding function body
(and the whole file) uses tab indentation; clang-format config in repo root.

---

## Summary

- **Counts:** BUG ×2 (1 high, 1 low) + 1 ARCH/BUG med · ROLLBACK ×1 (high) · ARCH ×1 (med)
  + 1 ARCH/COMMENT low · COMMENT ×1 (med) · STYLE ×2 (1 med, 1 low). 9 findings total.
- **Most important:** **Finding #1** — `bcm2711EncodeBar2Size` carries the *exact*
  `shift = 20` bug that the sibling `usb/xhci/bcm2711-pcie.c` already root-caused and fixed
  (USB-FIX-12): 4 GiB encodes to field 37 → truncates to 5 → a 1 MiB inbound window, so
  VL805's DMA target falls outside the window. The two copies have drifted; the PCIe-server
  one is wrong. NEEDS-HW to validate, but the in-tree referent makes the fix unambiguous.
- **Runner-up:** **Finding #2** — heavy `debug()`/warm-up diagnostic instrumentation tagged
  with bogus `TD-USB`/`TD-15` markers (finding #3) must be stripped before presentation;
  pure-log removal is APPLY-SAFE, but several blocks wrap load-bearing VL805 writes/delays
  and are NEEDS-HW.
- **Apply policy:** APPLY-SAFE overnight = the pure-logging `debug()` blocks (#2 safe
  subset), the inline `extern debug` cleanup (#4), the indentation fix (#9). Everything
  touching BAR/window/VL805 control flow (#1, #5, #6, #7, #8) is NEEDS-HW — document only.
