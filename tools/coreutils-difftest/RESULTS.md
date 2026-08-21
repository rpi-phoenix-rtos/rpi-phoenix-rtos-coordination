# coreutils-difftest results (2026-08-21)

Differential-correctness of the ported **GNU coreutils 9.5** `/usr/bin/<tool>`
binaries on Phoenix-RTOS (real Pi4, netboot) vs the **same GNU coreutils 9.5
built natively on the host** (reference), over a curated deterministic corpus
(`cases.tsv`, 28 cases). Reference is a native GNU 9.5 build — the host default
`/usr/bin` is uutils 0.8.0 (Rust reimpl), which is NOT a valid GNU reference.

Run as three netboot cycles (the full 28 + boot exceed one 10-min cap):
`cutest-A` (cases 1–14), `cutest-B` (15–28), `cutest-C` (re-run base64/od/cksum
in isolation). Merged per-case with `difftest.py check --log A B C` (NOT
concatenated — a concat lets one log's last case bleed into the next log's boot).

## Result: 25 bit-identical PASS, 2 crash-FAIL (one bug class), 1 tty artifact

| verdict | count | cases |
|---|---|---|
| **PASS** (bit-identical to host GNU 9.5) | 25 | echo, printf×2, seq×3, expr×2, factor×2, basename, dirname, numfmt, wc, wc-l, head-3, tail-2, sort, sort-r, uniq, cut-c, tac, sha256sum, md5sum, base64 |
| **FAIL — real bug** | 2 | **cksum**, **od** (see below) |
| **FAIL — harness/tty artifact (output actually correct)** | 1 | **nl** (see below) |

So **26 of 28 tools produce byte-correct output** (25 clean + `nl` whose bytes are
correct); `od` also computes correctly but crashes on exit; `cksum` crashes before
emitting its line.

## Real bug: `cksum` and `od` → Data Abort (EL0), kernel double-faults on dump

Both fault the same way (`piout/cksum.out`, `piout/od.out`):
- **EL0 Data Abort** #36: `esr=0x92000047` (WnR, stack write), `far=0x7ffffefce0`
  just above `sp=0x7ffffefcd0`, `pc=0x403e88 lr=0x404080`.
- Immediately followed by an **EL1 Data Abort** #37 (`pc=0xffffffffc000a0e8`,
  `far=0x7ffffef9a0`): the KERNEL fault-handler itself faults reading the user
  stack while dumping → the printed EL0 register dump is **UNRELIABLE** (corrupted
  by the double-fault; identical byte-for-byte across two different binaries,
  which is impossible for a genuine per-process fault state).
- `od` faults *after* printing its (correct) hex dump; `cksum` faults before its
  line. `addr2line` maps 0x403e88 to *different* functions per binary (od: getopt
  `exchange`; cksum: `cksum_slice8`/`crc_sum_stream`) — so it is NOT one shared
  function; the common thread is a stack-write fault + a non-robust kernel dump.
- NOT fadvise: `wc` also uses `fdadvise` and PASSES.

**Root-cause is a follow-on dig** — the printed dump can't be trusted, so it needs
a TRUE backtrace: run od/cksum under QEMU+gdbstub or the in-process libdbg, and
separately fix the **kernel fault-handler double-fault** (dumping must not fault on
an unmapped user stack). Candidate directions: user-stack sizing/guard for these
binaries; a libphoenix/crt path both hit near exit/CRC.

## Harness/tty artifact: `nl`

`nl` emits a TAB between the line number and text (GNU default). The Phoenix
console tty expands tabs to spaces on output (OPOST), so the UART capture shows
spaces where the tool wrote a TAB — the tool's byte output is correct; it is not
byte-comparable through the cooked UART. (Future: test `nl -s' '`, or compare over
a raw channel.)

## Reproduce

```
# 1. native GNU 9.5 host reference (once): build coreutils-9.5 natively -> <ref>/src
python3 difftest.py host --refdir <ref>/src      # -> expected/
# 2. stage inputs on the Pi + run (split to fit the 10-min cap):
sudo cp -rT corpus /srv/phoenix-rpi4-nfs/root/cutest
python3 difftest.py gencmds > cmds.txt           # (or split into halves)
./scripts/test-cycle-psh-interact.sh --label cutest --cmd-file cmds.txt
# 3. compare (pass every cycle log; merged per-case):
python3 difftest.py check --log <A.log> <B.log> <C.log>
```
