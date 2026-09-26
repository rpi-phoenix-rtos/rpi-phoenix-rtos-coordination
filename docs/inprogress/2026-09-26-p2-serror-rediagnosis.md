# P2 / TD-10 — asynchronous SError masking, re-diagnosed (2026-09-26)

Owner request 2026-09-26: re-evaluate P2 from scratch; experiments on netboot may break boot
(recoverable with a known-good kernel); understand what the masked SError hides; fix it or document
what is known. This file starts as the desk briefing (a read-only research pass over code, docs and
logs) and collects the experiment results below it.

## 1. How SError is masked today

- Routing: armstub `SCR_VAL` has no EA bit (`phoenix-armstub8-rpi4.S:56`); `_init.S:222` sets only
  HCR_EL2.RW (no AMO) → a physical SError is taken at EL1, vectors 0x380/0x580 →
  `_exceptions_dispatch` → `handler[0x2f]`.
- Every thread starts with `NO_SERR` (0x100, `arch/cpu.h:48`): `hal/aarch64/cpu.c:77,81`, EL0 **and**
  EL1. `hal_jmp` enters EL0 with `MODE_EL0|NO_SERR` (`_exceptions.S:392`). Exception/syscall dispatch
  unmask IRQ only (`daifClr #2`, `_exceptions.S:185,204`). All from kernel `0fdf20ca` (2026-04-30);
  upstream uses `daifClr #7` / `#4` there.
- One unmasked window: `_init.S:842` (`daifClr #7`) on each secondary core's idle loop until its first
  thread runs.
- Handler `exceptions_serrorHandler` (`exceptions.c:384-396`, registered `:579`, kernel `bcb64610`):
  dump, then `wfi` loop with IRQs on — parks the faulting thread only. No compile-time switch.

## 2. History

- 2026-05-29 unmask test: `esr=0xbf000002` (IDS=1), `far=0`. USB disabled → 0 SErrors; log-and-continue
  → 17, in two bursts. Concluded then: "no synchronous aborts ⇒ a posted write, not reads".
- 2026-05-30, Linux on the same board: ONE `devmem` read of `0xfd506000` panicked Linux with the
  identical `0xbf000002` (`2026-05-30-linux-usb-ref-fullcap.txt:625`). On this root complex a failed
  **read** completes (returns 0/0xdead) and the error arrives asynchronously — refuting the 05-29
  reasoning.
- The planned re-test on consolidated code (`docs/done/2026-06-02-usb-workstream-plan.md` §C step 1)
  never ran. No SError-tagged log exists after 2026-05-29.

## 3. Where the recorded SErrors came from (inferred, strong)

- Burst 1: each dump follows exactly one line of the USB-FIX-18 "Pi-firmware bridge state
  (pre-Phoenix init)" register dump — 10 reads of the PCIe core BEFORE the bridge left reset
  (devices `4d2e55c`); each stalled ~10.8 s and returned 0. Deleted by P1 (`3f82638`, 06-02).
- Burst 2: 7 SErrors at consecutive PCs right after `drive-only; link UP` — the 7 register reads of
  `xhci_capProbe` hitting 0xdead in the lwip drive-only instance, removed in `9781fbd`.
- Production boots today: across the last 398 boots `capProbe[0]` succeeded first time, 0 VL805
  firmware or BAR0 failures.
- ⇒ **Every recorded SError lines up with a diagnostic or poisoned read that no longer exists.** No
  evidence that production bring-up raises one.

## 4. PCIe path facts

- All userspace: the `usb` daemon runs `bcm2711_pcie_initVL805` (`usb/xhci/bcm2711-pcie.c:1156`) from
  `xhci_init` (`xhci.c:3423`). No PCIe code in plo or the kernel.
- Config access is already restricted like Linux (bus 0 dev/fn 0 only, link check, one device per
  bus). ⚠ The Linux `UBUS_CTRL` REPLY_ERR_DIS / DECERR_DIS / `AXI_READ_ERROR_DATA` bits are set only
  for **BCM2712** (`brcm_pcie_post_setup_bcm2712`), not BCM2711 — do not write them here.
- Real differences from Linux: Linux checks link-up on **every** config access; we cache `linkUp` once
  (:523) and keep accessing after the mailbox resets the VL805 (:965-996). We enable
  `SERR_ERR_ENABLE`/`PARITY_ERR_ENABLE`/bridge parity on the RC (:667, :699-700). We skip
  `brcm_config_clkreq` and the RBUS timeout extension (`SW_INIT_1-8`=0x9208).

## 5. Hypotheses, ranked

1. Nothing is left in the PCIe path (most likely, §3).
2. An access after the mailbox reset while the link is down (stale `linkUp`).
3. RC SERR/PERR reporting turns a VL805 error into an abort.
4. **Other drivers** — everything added since 05-29 (V3D, rpivid, SD ADMA2, WiFi, audio, HDMI) has
   never run with SError unmasked. This is the real unmask risk now.

⚠ **The P2 row's causal claim is probably wrong.** Pre-reset PCIe reads stalled ~10.8 s and then
*returned*; the unclocked-V3D read never returns at all. Masking cannot turn an abort into a hang, so
unmasking will not fix that hang.

## 6. Experiments (cheapest first; one core change each; `--scope core`; gate with `strings`)

- **E0** (no behaviour change): sample `ISR_EL1.A` (bit 8) per CPU in the timer tick; print the first
  tick where an SError is pending. Answers "does any SError happen at all, and when", while masked.
- **E1** bare unmask: drop `NO_SERR` at `cpu.c:77,81` and `_exceptions.S:392`; handler logs
  (rate-limited: ESR, ELR, process) and returns. Boot through the full app set, not only to psh.
- **E2** synthetic injector: map + read `0xfd506000` (proven SError source on this board) to validate
  E1's handler and decode `ISS=0x2` on the A72.
- **E3** only if E1 fires in USB: live link check in `bcm2711Read32/Write32`; MISC_STATUS around the
  mailbox call; then drop RC SERR/PERR enables.
- **E4** read-only: `0x9208` / 216 MHz vs the 10.8 s stalls.

## 7. Results

(none yet — experiments start after the WiFi runtime-join / D2 cycle)
