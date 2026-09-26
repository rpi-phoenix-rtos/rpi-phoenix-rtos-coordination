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

### E1 — pre-registered 15:20, before the build finished

Build 5 = build 4 + the uncommitted E1 kernel change (threads start with A clear; exception and
syscall dispatch unmask A with I and re-mask both before the context restore; spinlocks mask A too,
so the logging handler never runs under a held lock; handler logs and continues — full dump ×4, one
line to 256, then every 1024th). Cycle `p2e1`: `ls /dev`, `rpi4-wifi &` (netif auto-joins),
`cat /dev/thermal`, 16 MiB `dd` read of the SD card, `quakespasm -loadbench` (V3D), `wifi status`,
`ping` over WiFi, **then** `serrprobe fd506000 3`, then `ls /dev`.
- **Handler check first.** The injector must produce ≥ 1 `P2-E1 SError` line. If it produces none,
  the whole run's zero is uninformative (a Phoenix read of that address may simply not abort) and a
  different injector is needed before any conclusion.
- **Boot does not reach psh:** the last lines localise it; the handler no longer halts, so a hang
  is not the SError itself — record, then fall back to the build-4 manifest.
- **0 SError lines before the injector, injector fires:** hypothesis 1 (nothing left in the
  production paths) holds **for the drivers exercised** — the case for unmasking permanently.
- **SError lines before the injector:** attribute each by position between command echoes and by
  ELR (EL1 → addr2line on the kernel; EL0 → the process running then). That is the finding.

### E1 — result (cycle `p2e1`, `rpi4b-uart-20260926-152232-p2e1.log`, build 5)

- **Handler check: PASS.** `serrprobe fd506000 3` → exactly **3** SErrors, one per read, each
  `esr=0xbf000002 far=0` (identical to Linux on this board), taken at **EL0** (`psr=0x20000000`) with
  `pc` at the load in `serrprobe` — delivery is effectively precise for a load. Each read returned
  `0x00000000`; the handler logged and the program finished.
- **Production paths: 0 SErrors.** Boot to psh, USB (xHCI/VL805 bring-up, keyboard, mass storage),
  `rpi4-wifi` bring-up + WPA2 join + DHCP, `ping` over WiFi 3/3, `/dev/thermal` (mailbox), a 16 MiB
  SD read, `quakespasm -loadbench` (V3D power-up + load; it exits after loading, so rendering was
  barely exercised — the STK runs of `c1pad` on the same kernel cover that).
- After the injector the system stayed healthy: `ls /dev` complete, no other exceptions.
- ⇒ **Hypothesis 1 holds for everything exercised: nothing in today's production paths raises an
  SError.** The 2026-05 sources were diagnostic reads that no longer exist. The mask was hiding
  nothing — and it is not what makes an unclocked-V3D read hang (see §5).

### Final — kernel `df9da09d`, verified in cycle `p2final` (build 6)

SError is unmasked exactly as upstream (initial thread PSR, `hal_jmp`, `daifClr/Set #7` in exception
and syscall dispatch, `#4` in IRQ dispatch — diffed line-for-line against `origin/master`), and the
dedicated dump-and-halt handler is removed, so SError takes upstream's route: EL0 →
`process_exception` (dump + SIGKILL of that process), EL1 → assert. One deliberate deviation: a held
spinlock masks SError too (`daifSet #7`), so the handler, which prints under `console_common.lock`,
can never run on a CPU already holding it.

`p2final`: boot, USB, `rpi4-wifi` join, then `serrprobe fd506000 3` → **one** SError, dump reads
`process "/bin/serrprobe" (PID: 27)`, the probe was killed before its first `read` line, and the
system carried on — `ls /dev` complete, `wifi status` joined, `ping` 3/3.

**Coverage still thin:** GPU *rendering* (V3D was only powered up and loaded). The `c1pad` STK runs
use this kernel; an SError there would now kill STK with a named dump rather than pass silently.
