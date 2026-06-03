# Pi 4 USB FIX / parked-item ledger

Authoritative current status of the BCM2711/VL805 USB bring-up "FIX-NN" items
and related parked investigations. These were historically scattered across the
USB notes (`docs/notes/2026-05-2x-*`, `2026-06-01-usb-rearchitecture-plan.md`)
and the agent task tracker; this file consolidates them so the published repo
carries one honest record.

**Scope note:** the `FIX-NN` IDs are a USB-workstream-local numbering and are
*not* the same as the `TD-NN` technical-debt IDs in
[TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md).
Number gaps (FIX-2/5/6/16) reflect IDs that were merged or renumbered during the
investigation, not missing work. Where a fix landed in code, the sibling-repo
commit is cited; "manifest" refers to `manifests/*.md` snapshots.

**Honesty rule (project-wide):** an item is only "closed" when its mechanism is
either fixed-and-validated or affirmatively refuted on this hardware. Items
resting on a single uncontrolled observation stay open.

---

## Open

| ID | What | Status | Next |
|----|------|--------|------|
| **FIX-14** | Downstream (LS-kbd/mouse-behind-TT) DMA-completion reliability — the historical "inbound-DMA wall / completion never posts" family. | **OPEN.** Bring-up DMA now lands (controller reaches RUNNING, events post, hub enumerates deterministically). But the #129 wall — `AddressDevice`/ep0 transfers to the low-speed keyboard behind the VIA hub's TT intermittently get no completion — is the same inbound-completion family and is still flaky (~0–25% per boot). The "10/10 clean" boot study is one uncontrolled, post-P1 run; it did not prove kbd/mouse completions land every boot. | Close only when Phase-0 (#139) shows reliable downstream completion **and** TD-10 confirms the SError external abort is not the mechanism. Deep lead = TD-10. |
| **TD-10** | Masked SError / live PCIe-VL805 external abort. SError is masked (`NO_SERR`) everywhere; the dump-and-halt handler is committed (kernel `bcb64610`) but dormant because unmasking on real HW reveals a live external-abort source (`esr=0xbf000002`, imprecise/async) that fires exactly when USB bring-up begins (isolation-proven). | **OPEN.** Both the TD-10 unmask blocker and a primary mechanistic lead for FIX-14 (an externally-aborted controller→memory access ⇒ "completion never posts"). | Research-shaped (no JTAG). Step 1: re-attempt unmask on consolidated code, one boot, observe clean-vs-halt. If a source remains, bisect bring-up phases via the network-readable diag-udp path + RC error-latch regs. Wants a human in the loop. |
| **IRQ-driven events** (ex-FIX-17 perf) | `xhci.c` registers no IRQ handler; the event ring is poll-only. | **OPEN as perf, not correctness.** Polling works (Pi 4 uses legacy INTA, not MSI). A real IRQ-driven event path matters once USB is reliable and under SMP load. | Re-filed as a perf task; not a bring-up blocker. |

---

## Closed — fixed and in tree

| ID | What | Resolution |
|----|------|-----------|
| **FIX-1** | VL805 init enabled MEM + BUS_MASTER together. | Split MEM_ENABLE vs BUS_MASTER_ENABLE in the VL805 PCIe command-register program. |
| **FIX-3** | HCRST churns BCM2711 bridge outbound translation. | `xhci_reset` re-settles the bridge window after HCRST. |
| **FIX-4** | 64-bit MMIO pointer-register writes (CRCR/DCBAAP/ERDP) split incorrectly. | Audited + corrected the 64-bit register write helpers in `xhci.c`. |
| **FIX-8** | PCIe MPS/MRRS unset for VL805 + RC. | Program Max-Payload / Max-Read-Request size on both endpoints. |
| **FIX-9** | NO_SNOOP could route DMA around coherency. | Clear NO_SNOOP_EN in DCTL on both RC and VL805. |
| **FIX-10** | VL805 Device-Status sticky bits (URD/CED) set at entry. | Clear them before init. |
| **FIX-11** | RC_BAR2 readback + ASPM. | Verified RC_BAR2 readback; ASPM handling confirmed. |
| **FIX-12** | `bcm2711EncodeBar2Size` off-by-20 truncated the 4 GB inbound window to 1 MB. | Fixed the size encoding. |
| **FIX-13** | Event-ring command-completion not visible post-DMA-fix. | Event-ring consumer corrected; completions now land. |
| **FIX-15** | Audit Linux `pcie-brcmstb` for missing inbound config bits. | Audited; RC inbound config matches Linux (no missing bit). Residual live-abort thread folded into TD-10. |

---

## Closed — obsolete, superseded, or refuted

| ID | What | Verdict |
|----|------|---------|
| **FIX-7** | "Skip HCRST if the controller is already usable." | **SUPERSEDED.** Confirmed 2026-06-03: `xhci_reset` (`xhci.c`) issues exactly one canonical *halt-then-HCRST* at init, matching Linux/FreeBSD/NetBSD/Circle. In the one-process standalone daemon that single deterministic reset is correct — not the gratuitous cross-process re-reset FIX-7 was about. No code change needed. |
| **FIX-17** | Use MSI instead of poll-only events (correctness framing). | **OBSOLETE as correctness.** Pi 4 / Circle / Linux use legacy INTA; events DMA-write and poll fine. MSI is not the inbound-DMA enabler. Perf follow-up re-filed (see Open / IRQ-driven). |
| **FIX-18** | Dump bridge state *before* Phoenix init. | **DONE then removed.** The diagnostic read 10 RC registers before `SERDES_IDDQ` is cleared, each hitting the external-abort path (~10.8 s ea). Removed by P1 (devices `3f82638`); boot span 166 s → 66–74 s. |
| **FIX-99** | Flaky bridge pre-state / rapid-cycle bridge degradation; bring-up depended on the prior process's bridge state. | **CLOSED.** Dissolved by the standalone daemon owning bridge + controller in one long-lived process (#129). Genuine code-level close 2026-06-03: the dead `--bridge-only` path removed (usb `dcc5ea7`, devices `a2d6530`, manifest `2026-06-03-fix99-bridge-only-removed`) — no `--bridge-only` path remains. |
| **#26** | USB DMA pool physically aliasing the lwIP heap (kernel pmap/MMIO aliasing). | **REFUTED for this HW.** No permanent cacheable DRAM alias (kernel research dive); the containment test put the corruption victim in the inter-chunk gap, not inside any pool chunk. The mbox corruption was a CPU use-after-free of a reused lwIP memp slot, made moot for USB by process isolation. `USBPOOL pa=` probes deleted (usb `c4074b3`). Residual lwIP `sys_mbox_free` UAF is an lwIP bug, re-filed there. |
| **#111** (config portion) | Does Phoenix's RC/VL805 config diverge from Linux in a way that matters? | **CLOSED (config).** Cross-OS audits found our RC inbound config matches; the 4 GB-vs-3 GB `SCB_SIZE`/`dma-ranges` lead was closed (the inbound abort is address-independent). The one live thread — the `esr=0xbf000002` external abort — is folded into TD-10. |

---

## Related cleanup completed (not FIX-NN, same workstream)

- **A1 / dead `LWIP_EMBED_USB` rig** — fully removed 2026-06-03 (lwip `2462588` + `0581be3`, devices `0ab644e` + `9c588d6`). The incomplete first pass had silently broken the core build; see [TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md] and the session record.
- **#124 interrupt-IN ring producer** — root-caused + candidate fix committed (devices `255ce87`); awaiting live keypress/mouse validation before close. Shared keystone for reliable keyboard *and* mouse.

---

*Last updated 2026-06-03. If this ledger is to be the single authoritative
record, consider whether the open items (FIX-14, TD-10) should also gain TD-NN
entries in TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md, or whether that file should
link here — a small structural decision left for the maintainer.*
