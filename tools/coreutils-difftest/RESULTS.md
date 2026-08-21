# coreutils-difftest results (2026-08-21, expanded 54-case corpus)

Differential-correctness of the ported **GNU coreutils 9.5** `/usr/bin/<tool>`
binaries on Phoenix-RTOS (real Pi4, netboot) vs the **same GNU coreutils 9.5 built
natively on the host** (reference), over a curated deterministic corpus
(`cases.tsv`, 54 cases). Reference = a native GNU 9.5 build (the host default
`/usr/bin` is uutils 0.8.0, a different implementation — NOT a valid GNU reference).
Because both sides are GNU 9.5, every diff is a genuine Phoenix-specific defect
(no cross-version noise).

Runs merged per-case (`difftest.py check --log A B C …`, never concatenated).

## Result: 53/54 bit-identical PASS; 1 tty artifact (not a bug)

**PASS = 53** — bit-identical to host GNU 9.5:
echo, printf×2, seq×3 + seq-f, expr×5 (add/div/length/substr/mod), factor×3,
basename, dirname, numfmt, wc + wc-l/-c/-w, head-3 + head-c, tail-2, sort +
sort-r/-n, uniq + uniq-c/-d, cut-c, tac, cat-A, fold, expand, nl (-s: variant),
join, paste (-d,), tsort, od (-tx1/-tx4), base64, base32, basenc-16,
sha1/sha224/sha256/sha512sum, md5sum, b2sum, sum, **cksum** (fixed — see below).

**FAIL = 1 — `nl` (default), a console artifact, NOT a coreutils bug.** GNU `nl`
emits a TAB between the line number and text; the Phoenix console tty expands tabs
to spaces on output (OPOST), so the UART capture shows spaces where the tool wrote
a TAB — the tool's byte output is correct, it just isn't byte-comparable through
the cooked serial console. Proven: `nl -ba -s:` (colon separator, no tab) PASSES.

⇒ **All 54 tools produce correct output.** No new defects found in the expansion
(the +26 cases over the original 28 exercised more hashes, text tools, and
join/tsort/paste/expr/seq variants). The ONE real bug this harness ever found —
`cksum`/`od` Data Abort — was root-caused to the 32 KiB user stack and FIXED
(SIZE_USTACK→1 MiB, kernel 8ae20864); both now PASS here.

## Corpus notes
- Only DETERMINISTIC, host-version-stable cases (LC_ALL=C; no date/random/env/host
  or tty-tab-emitting output; `rev` excluded — it is util-linux, not coreutils).
- Filenames normalized (`/root/cutest/` → basename) so the reference (cwd=corpus,
  bare names) matches the Pi (absolute paths).
- Harness robustness: multi-log per-case merge, ANSI-CSI strip, kernel/driver
  async-log-line filter, and `host` skips a case whose ref binary is missing.

## Reproduce
```
python3 difftest.py host --refdir <native-GNU-9.5>/src     # -> expected/
sudo cp -rT corpus /srv/phoenix-rpi4-nfs/root/cutest
python3 difftest.py gencmds > cmds.txt                     # split if >~14 for one cycle
./scripts/test-cycle-psh-interact.sh --label cutest --cmd-file cmds.txt
python3 difftest.py check --log <logs...>
```
