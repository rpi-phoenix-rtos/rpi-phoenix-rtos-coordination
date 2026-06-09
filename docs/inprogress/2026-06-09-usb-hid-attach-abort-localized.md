# USB intermittent HID-attach Data Abort — root cause LOCALIZED (#152 bench result / #121-family)

**Date:** 2026-06-09
**Session type:** attended; netboot live (card in host).
**Bottom line:** the #152 audit's headline lead — *"the usb daemon's 2 KB thread
stacks overflow during HID attach"* — is **REFUTED as the mechanism** for the
intermittent kbd/mouse-attach `Data Abort (EL0)`. The abort is a **wild write
into the `hub_common.events` list head** (a #121-family heap/list corruption),
not a stack-depth overflow. Stack-size bumps cannot fix it; they were reverted.

## What was tested

Per the #152 audit recommendation, the usb daemon's message + status thread
stacks (`usb/usb.c:51,53`) were bumped 2 KB → 16 KB and a netboot bench run.

- **T1:** clean — `/dev/kbd0` + `/dev/mouse0` enumerated, kbd bridge opened, 0 faults.
- **T2:** **reproduced the abort** — two byte-identical `Exception #36: Data
  Abort (EL0)` in process `usb`, *with the status threads already at 16 KB*.

So the bump did **not** eliminate the abort. The byte-identical fault state
across both instances (same `pc`, same `far`) argued for *deterministic* shared
corruption, not random per-thread stack overflow — which the address resolution
below confirmed.

## Fault resolution (addr2line + nm on the unstripped `prog/usb`)

Fault registers (both instances identical):

```
pc =0x411944  lr =0x403414  esr=0x92000044 (Data Abort, DFSC=0x4 translation L0)
x0 =0x422780  x1 =0x405c04  x2 =0x0  x3 =0x8
x5 =far=0x3972e261d5033f9f   x12=0x632d643634302d62 x13=0x303066692d633133
```

- **pc 0x411944 → `lib_listRemove` (libphoenix `sys/list.c:46`)**. Disassembly:
  the faulting insn is `str x4, [x2, x5]` = `*(t->prev + noff) = t->next`.
- **lr 0x403414 → `hub_thread` (`usb/hub.c`)** — specifically the
  `LIST_REMOVE(&hub_common.events, ev)` at `hub.c:440`.
- Arg semantics (`lib_listRemove(list, t, noff, poff)`): `x0=list`, `x1=t`,
  `x2=noff=0`, `x3=poff=8`. `hub_event_t` is `{ next@0, prev@8, hub@16 }`.
- **`x0 = 0x422780 = &hub_common.events`** (confirmed: `nm` puts `hub_common`
  @ `0x421778`, and `events` is at offset `0x1008`).
- **`x1 = hub_common.events = 0x405c04`** — a **`.text` address**, not a heap
  `hub_event_t*`. The list head itself is corrupted to a code pointer.
- `x5 = t->prev = 0x3972e261d5033f9f` (garbage) → the `str` faults.
- `x12/x13` decode as ASCII `"b-046d-c" / "13c-if00"` — fragments of the
  Logitech keyboard's device string `usb-046d-c31c-if00`, i.e. the data being
  processed during HID attach is what landed in the corrupted region.

## Why it is NOT a stack overflow of the bumped threads (the geometry)

`.bss` layout from `nm -n -S prog/usb`:

```
0x421768  hcd_common (8)
0x421778  hub_common (0x1018)        <- stack[4096] @ +0 .. +0x1000, then lock/cond/events/tts
0x422780  hub_common.events         <- the corrupted word
0x422790  usb_mem_common (16)        <- next symbol; NOT a thread stack
```

- `hub_thread`'s stack is `hub_common.stack[4096]` = `0x421778..0x422778`. Its
  SP grows **down** from `0x422778`. At fault time `sp=0x4226b0`, `fp=0x422770`
  (shallow frame), so the *corruption happened earlier* and surfaced here.
- **`hub_common.events` (0x422780) is 8 bytes ABOVE the stack top (0x422778).**
  A downward stack overflow of `hub_thread` grows *away* from `events` (toward
  `0x421778` and below) — it can never reach it.
- The symbol directly **above** `hub_common` is `usb_mem_common` (16 B) +
  logging flags — **no thread stack** lives there, so no *other* thread's
  downward overflow passes through `events` either.
- Therefore growing any thread's stack **size** leaves the corruption geometry
  unchanged (the overrun source stays near the stack top, `events` stays just
  above it). This was decided by static layout analysis rather than a second
  40-minute HW bench, since the geometry makes the outcome certain.

**Conclusion:** the corruptor is a **forward out-of-bounds write** (a string /
buffer near the top of the attach call frame written past its end, into
`hub_common.events`), or an unrelated wild pointer store — i.e. a logic bug in
the enumeration / HID-attach path, the same class as the *already-guarded* URB
`finished`-ring corruption in `usb.c:184-196`. Not a stack-size problem.

## Status of the #152 stack changes

- **usb.c (msg+status 2 KB→16 KB) and hub.c (`hub_thread` 4 KB→16 KB): REVERTED.**
  They do not fix this abort and the bench gave no evidence they help anything;
  shipping them with an "abort fix" rationale would be misleading (and an
  unvalidated change to a shared upstream daemon).
- The #152 audit's *structural* observation (no guard pages on these stacks)
  still stands as hygiene, but is decoupled from this abort.

## Recommended next step (attended, USB-internals)

Find the forward overrun. Candidates, in the `hub_thread → usb_devEnumerate →
usb_drvBind → usbkbd insertion` chain that builds `usb-<vid>-<pid>-if<NN>`:
1. The device-path/string formatting in the usbkbd (or shared libusb) insertion
   handler — check for a fixed-size buffer written without bound (sprintf vs
   snprintf, off-by-one on the interface suffix).
2. HID report-descriptor parsing buffers.
A targeted, **temporary** guard mirroring `usb.c:192` could be added to
`hub_thread` (validate `hub_common.events` points into the heap before
`LIST_REMOVE`) to convert the crash into a logged recovery while the real
overrun is hunted — but the fix is to bound the offending write.

## Reliability baseline (2026-06-09, netboot bench, current daemon)

A 5-boot bench (`rel-T1..T5`, snprintf hardening committed) plus the session's
earlier boots:

| metric | result |
|---|---|
| USB enum (`/dev/kbd0` + `/dev/mouse0`) | **5/5** (100%) — and 11/11 across the day |
| boot → `(psh)%` | **5/5** |
| #121 HID-attach abort | **2/5** (rel-T2, rel-T5); ~**3/11 ≈ 27%** across the day |

Takeaways: (1) enumeration is fully reliable; the bug is purely the post-enum
HID-attach corruption and does not block kbd/mouse coming up. (2) The abort is
**more frequent than the previously-logged ~1/10** — small N, but it recurred
readily, so it deserves priority as a real reliability defect, not a rare edge.
(3) The snprintf hardening neither fixed nor worsened the rate (expected — it's a
different code path); enum/boot were unaffected, confirming no regression.

Relates to: [[project_usb_kbd_attach_abort]] (#155), [[project_usb_freelist_121_state]]
(#121), `docs/notes/2026-06-06-usb-intermittent-kbd-attach-abort.md`.
