# C9: USB enumeration fails on ~1.7 % of boots — root cause, fix, Pi check

Register row: [KNOWN-ISSUES C9](../KNOWN-ISSUES.md). Fix: phoenix-rtos-devices branch
`c9-usb-addr-retry` (`10de890` re-address on a fresh slot, `fd7901d` drop the unreachable
2026-09-07 recovery).

## What the logs say (offline, 5 937 boots since July)

- **Rate:** 112 of 5 937 boots fail, and the rate is flat over time: 2.34 % before 09-07, 1.59 % from 09-07 to 09-19, 1.72 % since 09-20. There is no correlation with the build or the day.
- **Always the same shape.** The first Address Device of the VL805's internal hub (root port 1, slot 1) completes with code **36** (Split Transaction Error). Every retry then completes with **19** (Context State Error). After that the boot has no keyboard, mouse or USB stick. Passing boots never print a completion code.
- **Not D5.** The controller is healthy in all 19 failing boots since 09-20. The capability probe works, the probe-time PORTSC values match those of 1 012 passing boots, and No-Op, Enable Slot and the first Address Device step all complete.
- **One failing boot traced port status.** The first port reset trained the hub at full speed (`speed=1`), and the retry's reset trained it at high speed. Every traced passing boot trained at high speed on the first reset.

## Mechanism

Addressing is two-step:

1. BSR=1 moves the slot from Enabled to Default without bus traffic.
2. BSR=0 sends SET_ADDRESS.

When step 2 fails on the wire, the slot stays in Default. The framework's retry finds the same slot-table entry with `addressed == 0` and issues BSR=1 again. That command is valid only in the Enabled state (xHCI 1.2 §4.6.5), so it fails with code 19 every time, however healthy the link has become.

The 2026-09-07 recovery (`e371967`, Disable Slot on ep0 teardown) never ran, because the control pipe does not carry the private data that the teardown checks for. Its "6/6 verified" result was chance: at a 1.7 % failure rate, 6 clean boots happen about 90 % of the time anyway.

## Fix

`xhci_addressSlot` now handles both Address Device sites. On failure, `xhci_slotRenew` does what Linux's `xhci_setup_device` does on a transaction error:

1. Disable Slot.
2. Clear the slot's DCBAA entry.
3. Enable Slot and rebind the context.

The retry then addresses a slot in the Enabled state, with the speed its own port reset negotiated. Each command is bounded by `XHCI_CMD_TIMEOUT_MS`, and the framework's retry counts still apply, so the fix cannot turn a failure into a hang.

The recovery prints:

    xhci: Address Device failed on slot N (port P, speed=S), retrying on slot M

## Pi check (pre-registered 2026-10-01)

**Step 1: forced (test build only).** Branch `c9-fault-inject` (`0ba0c17`, never merged) makes the first successful Address Device of each boot fail once, and prints `xhci: C9-FAULT injected`.

- PASS requires all of the following in every one of 5 boots:
  - `C9-FAULT injected`, then `retrying on slot`;
  - then `interrupt-IN pipe ready` and the keyboard and mouse bridges opening;
  - no `Enumeration failed despite`, and no `command completion code 19`.
- FAIL is any boot without input after the injected fault. That would mean the renewed slot does not recover, and the fix is wrong.

**Step 2: natural (the merged build).** Over the following gate boots:

- no boot without keyboard or mouse;
- every boot with `retrying on slot` also has its keyboard, mouse and stick.

At 1.7 %, ruling out the old rate needs about 175 clean boots at 5 % significance, or about 270 at 1 %. Expect roughly one natural firing per 60 boots.

## Result

**Step 1 (forced) PASS, 5/5 boots, 2026-10-01 11:39–11:52.** Test build with devices `0ba0c17` (`c9-fault-inject`); bench `c9fault-T1..T5`. Every boot printed `C9-FAULT injected`, then `xhci: Address Device failed on slot 1 (port 1, speed=3), retrying on slot 1`, then both `interrupt-IN pipe ready` lines, the keyboard bridge and the mouse. No boot printed `completion code 19` or `Enumeration failed despite`. Enable Slot handed back the same id (1), which the xHCI spec allows.

Only the recovery path is proven. The trigger that occurs in the wild (a full-speed link after the first reset) is not reproduced; step 2 below covers that.

**Step 2 (natural):** the merged build 13 onwards.

- Ongoing tally: boots without input, and natural `retrying on slot` firings.
- Old rate: 1.7 %.

| up to | boots with the fix | without USB input | natural `retrying on slot` | `completion code 36` |
|---|---|---|---|---|
| 2026-10-01 19:35 (gates 13–17, SD gates, C10, bench `c9nat` ×20) | 63 | 0 | 0 | 0 |

63 clean boots do not yet rule out the old rate: at 1.7 %, 0.983^63 ≈ 34 %. The trigger itself has not occurred, so the natural recovery path has still not been exercised.
