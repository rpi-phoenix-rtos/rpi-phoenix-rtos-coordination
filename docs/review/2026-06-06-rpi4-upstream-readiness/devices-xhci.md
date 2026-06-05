# devices-xhci — upstream-readiness review

- **Area:** `devices-xhci`
- **Repo:** `phoenix-rtos-devices` (base `origin/master` `d511e0f` → head `master` `ebac8e4`)
- **File:** `usb/xhci/xhci.c` (3688 lines, entirely new — xHCI HCD for the VL805 behind the BCM2711 PCIe bridge)
- **Referents used:** sibling EHCI HCD `usb/ehci/{ehci.c,ehci.h,ehci-hub.c}` (same repo, same `hcd_ops_t` class); USB framework allocator `phoenix-rtos-usb/usb/mem.c`.

Reviewed the whole file as one new hunk. Coherence note up front (so it is not re-flagged): driver ring/context memory and `t->buffer` all come from `usb_allocAligned`/`usb_alloc`, which map `MAP_UNCACHED | MAP_CONTIGUOUS` (`mem.c:62-63`), so no cache maintenance is needed; the `dsb sy` in `xhci_dbWrite32` (548) and `xhci_enterRunState` (1386) correctly orders Normal-NC ring stores before the Device-memory doorbell. The MMIO ordering and DMA coherence are **correct** — no barrier finding is raised.

---

## Findings (ordered by severity)

### 1. `usb/xhci/xhci.c:2041` · BUG · sev=high — `inputCtx` re-allocated per slot, leaking the previous buffer
`xhci_allocSlotSpace` unconditionally does `xhci->inputCtx = usb_allocAligned(xhci->inputCtxSize, ...)`. It runs once for `slots[0]` from `xhci_init` (3080), then **again** from `xhci_allocSlotForDev` (2382) for every device behind a non-root hub (the keyboard). Each re-alloc overwrites `xhci->inputCtx` with a new pointer and **does not free the old one** → leak. `xhci_destroy` (645) frees only the last pointer. This directly contradicts the struct comment (357-361): "the shared input context … stays on xhci_t — commands are serialised, so a single input context is reused across slots."
**WHY:** resource leak on every behind-hub enumeration; also a latent correctness trap — the field is documented as singleton but the alloc treats it per-slot.
**REC:** guard the allocation — `if (xhci->inputCtx == NULL) { xhci->inputCtx = usb_allocAligned(...); if (...NULL) return -ENOMEM; }` — and recompute `inputCtxPhys` from the existing buffer; or hoist input-context allocation out of `xhci_allocSlotSpace` into init. **NEEDS-HW** (touches the live enumeration path; cannot be regression-checked overnight).

### 2. `usb/xhci/xhci.c:3338,3342,3346,3350` · BUG · sev=med — `clearPortFeature` over-clears sibling RW1C change bits
The four `C_CONNECTION` / `C_ENABLE` / `C_OVER_CURRENT` / `C_RESET` cases write `(portsc & ~PED) | <changebit>`. Because CSC/PEC/OCC/PRC are write-1-to-clear, writing back the *read* value of the other change bits clears them too. The `ENABLE` (3354) and `POWER` (3358) cases in the same switch already do the right thing — they mask `~(... | XHCI_REG_OP_PORT_PORTSC_RW1C)` so unrelated change bits are preserved (written as 0). The four `C_*` cases are inconsistent with that and with the `RW1C` mask the file itself defines (123-124).
**WHY:** clearing one port-change feature silently acks/discards any other change bit that happened to be set at read time → a missed connect/reset/over-current change. Latent because the hub driver typically clears one feature at a time, but it is a real RW1C-handling bug.
**REC:** for each `C_*` case write `(portsc & ~(XHCI_REG_OP_PORT_PORTSC_PED | XHCI_REG_OP_PORT_PORTSC_RW1C)) | <thatChangeBit>`. **NEEDS-HW** (port state machine; document only).

### 3. `usb/xhci/xhci.c:1274-1327` · ROLLBACK · sev=med — dead `xhci_runStateSelftest`
`xhci_runStateSelftest` is `__attribute__((unused))`, never called, and `xhci_init` (3051-3061) carries a 10-line comment explaining why it is deliberately skipped on VL805. Dead experiment code.
**WHY:** ~54 lines of unreachable code plus a justification comment for not calling it; pure noise for a public reader.
**REC:** delete the function and trim the init comment to one sentence ("no R/S self-test — `xhci_cmdNoopSelftest` is the liveness check"). **APPLY-SAFE** (dead-code removal; build + boot smoke).

### 4. `usb/xhci/xhci.c` (multiple) · ROLLBACK · sev=med — bring-up `debug(dbgbuf)` diagnostic dumps with no TD marker
`snprintf(dbgbuf, …); debug(dbgbuf);` register dumps left from the FIX-NN bring-up, none gated, none carrying a `TODO(TD-xx)`:
- `xhci_reset` enter/HCRST-timeout/post-HCRST USBSTS dumps (878-881, 919-921, 932-934)
- `xhci_enterRunState` per-attempt HSE dump, "recovered on attempt", and "GAVE UP" 4-register dump (1422-1437, 1448-1457)
- `xhci_capProbe` per-attempt `fprintf` of caplength/HCIVERSION (806-807)
- stash-full `debug` (1693) and recover CRR-timeout `debug` (1759) are arguably keepers (true error conditions).
**WHY:** these are diagnostic instrumentation, not steady-state driver output; the comments even date-stamp them ("USB-DBG (2026-05-26)") and reference the obsolete rig-vs-PoC investigation. They diverge from the EHCI logging idiom (next finding).
**REC:** remove the pure register-trace dumps; for any worth keeping, convert to a gated `log_debug`-style macro (see finding 5). **APPLY-SAFE** for the trace dumps (no control-flow change).

### 5. `usb/xhci/xhci.c:483-833` (and throughout) · STYLE · sev=med — no `LOG_TAG`/`log_*` macro; raw `fprintf(stderr, …)` + `debug()` mix
The sibling EHCI HCD defines (`ehci.h:21-27`) `#define LOG_TAG "ehci: "`, `log_msg`, `log_error(fmt) → log_msg("error: " fmt)`, and `log_debug(fmt) → if (EHCI_DEBUG) log_msg(...)`, and uses them throughout (`ehci.c:659,956,1017`; `ehci-hub.c:98,127,...`). xhci.c instead open-codes `fprintf(stderr, "xhci: …\n")` for errors and `debug()`/`snprintf` for diagnostics, with the `"xhci: "` prefix repeated by hand on every line and no compile-time debug gate.
**WHY:** upstream Phoenix convention for this exact driver class is the tagged-macro logging in `ehci.h`; the divergence makes diagnostic output ungateable and inconsistent.
**REC:** add an `xhci.h` (or top-of-file block) with `LOG_TAG "xhci: "` + `log_msg`/`log_error`/`log_debug` mirroring `ehci.h:21-27`, route error sites through `log_error`, and put the surviving diagnostics behind `log_debug` (folds finding 4's keepers). **APPLY-SAFE** (mechanical; build + boot smoke).

### 6. `usb/xhci/xhci.c:780-833,3015-3017` · COMMENT/STYLE · sev=low — `xhci_capProbe` returns `-ENOSYS` to signal *success*
On valid cap space `xhci_capProbe` `return -ENOSYS` (814); the init chain then proceeds only `if (err == -ENOSYS)` (3017). An error code used as the success sentinel is an inverted contract that reads as a bug at a glance.
**WHY:** readability/maintainability trap for an upstream reviewer; not a functional defect (caller matches the value).
**REC:** return `EOK` on success and a real error otherwise, or rename to make the sentinel explicit (e.g. `XHCI_CAP_PROBED`); at minimum document the inversion at both the return and the call site. **NEEDS-HW** to change the contract safely (alters the init branch); the comment-only clarification is **APPLY-SAFE**.

### 7. `usb/xhci/xhci.c:235` · COMMENT · sev=low — stale `XHCI_CMD_TIMEOUT_MS` comment references the disproved #129 inbound-write hypothesis
The macro comment ends "…the current usb-hcd cmd-completion gap is NOT timing … it's a process-context inbound-write issue." Per `MEMORY.md` / `project_usb_merged_config_wall_gone`, that inbound-DMA-wall framing was an artifact and is obsolete; the doc trail says enumeration now works end-to-end.
**WHY:** a load-bearing-looking comment that asserts a since-disproved root cause; misleads readers about controller behaviour.
**REC:** trim to the timing rationale only ("commands can need ~hundreds of ms; 1000 ms budget"). Sweep the file for the same obsolete "rig-vs-PoC" / "zero events land" framing in `xhci_reset` (954-958), `xhci_enterRunState` (1357-1368) and `xhci_programEventRing` (2947-2955) and reduce to the spec-level reason that justifies the code. **APPLY-SAFE** (comments only).

### 8. `usb/xhci/xhci.c:382,656-721,3102` · ARCH · sev=low (document-only) — polled event ring vs EHCI's IRQ-driven model
EHCI attaches an interrupt handler (`ehci.c:956` "attaching handler to irq=%d") and reaps from the ISR/condvar. xhci.c deliberately leaves `IMAN.IE` masked (comment 2947-2955) and polls the event ring from a 100 ms `xhci_roothubStatusThread` (656) on a fixed `statusStack[2048]` embedded in `xhci_t` (382).
**WHY:** divergence from the same-class EHCI referent. It is a *justified* bring-up choice (matches the known-good lwip 'X' path; iPXE-style poll), so this is recorded, not recommended for change.
**REC:** none for now; note in the upstreaming dossier that interrupt-driven event handling is the eventual target and that the polled thread + masked interrupter are an interim, spec-legal design. **NEEDS-HW** if ever revisited.

---

## Summary

- **Counts:** BUG 2 (1 high, 1 med), ROLLBACK 2 (med), STYLE 1 (med) + 1 low component, COMMENT 2 (low), ARCH 1 (low, document-only). 8 findings total.
- **APPLY-SAFE (overnight-eligible):** #3 (delete dead selftest), #4 (strip diagnostic `debug()` dumps), #5 (add `log_*` macros), #7 (stale comments). #6 comment clarification only.
- **NEEDS-HW (document only):** #1 (`inputCtx` leak — touches live enumeration), #2 (RW1C over-clear — port state machine), #6 contract change.
- **Most important issue:** **#1** — `xhci->inputCtx` is re-allocated (no free) on every behind-hub slot setup, leaking the prior buffer and contradicting its own "allocated once, shared" contract. Highest-value fix; must be reviewed before upstreaming.
