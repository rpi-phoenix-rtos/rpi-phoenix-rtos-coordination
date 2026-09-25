# dczid-probe — settle the Pi 4 DC-ZVA question in one boot

TD-20 / P3 disabled the `dc zva` fast path in `hal_memset` on Cortex-A72 "pending proof of
the EL2 DC-ZVA trap state", which does not reproduce in QEMU. That state is directly
readable from EL0 — `DCZID_EL0` is an unprivileged read-only system register — so the proof
costs one boot and no kernel change.

```
cp dczid /srv/phoenix-rpi4-nfs-gcc16/bin/
./scripts/test-cycle-psh-interact.sh --label dczid --idle-secs 25 -- "/bin/dczid"
```

It does two things, because a register read alone can be misread:

1. decodes `DZP` (bit 4 — 1 means DC ZVA prohibited) and `BS` (bits 3:0 — log2 block size in
   words);
2. **executes** `dc zva` on a block-aligned buffer and checks the whole block zeroed *and*
   that the byte immediately before it survived. That second check matters for this
   instruction specifically: `dc zva` zeroes the block **containing** the address, so a
   misaligned pointer silently clobbers neighbours.

## Result on the real Pi 4 (2026-09-25)

```
DCZID raw=0x4
DCZID DZP=0 (PERMITTED -- dc zva is usable from EL0/EL1)
DCZID BS=4 (block = 64 bytes)
DCZID EXEC=ok (64 bytes zeroed, neighbours intact)
```

**`HCR_EL2.TDZ` is not set.** The trap TD-20 was waiting on does not exist, and 64 bytes
matches the A72 cache line. ⛔ Do not re-run this probe to answer that question again.

## …and why the gate still stands anyway

The measurement disproves the hypothesis but does **not** license lifting the gate.
`hal_memset`'s own header says it *"may not work for uncached memory"*, and the recorded
failure is a **hang with no exception output** in `_log_init`'s first large zeroing right
after the D-cache is enabled. A trap would have raised an exception; a hang fits `dc zva`
issued against memory that is not Normal cacheable at that moment.

So lifting it now needs a whole-kernel argument — that **no caller of `hal_memset` can pass
uncached memory** — rather than another measurement. Until someone makes that argument,
enabling the fast path trades correctness for a performance-only gain.
