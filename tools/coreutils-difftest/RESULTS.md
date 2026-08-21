# coreutils-difftest results (2026-08-21, after the SIZE_USTACK fix)

Differential-correctness of the ported **GNU coreutils 9.5** `/usr/bin/<tool>`
binaries on Phoenix-RTOS (real Pi4, netboot) vs the **same GNU coreutils 9.5
built natively on the host** (reference), over a curated deterministic 28-case
corpus (`cases.tsv`). Reference = a native GNU 9.5 build — the host default
`/usr/bin` is uutils 0.8.0 (Rust reimpl), NOT a valid GNU reference.

Runs merged per-case (`difftest.py check --log A B …`, never concatenated):
`cutest-A` (cases 1–14) + `cutest-B3` (cases 15–28, after the fix).

## Result: 27/28 bit-identical PASS; 1 tty artifact (not a bug)

**PASS = 27** — every tool except `nl` produces byte-identical output to host
GNU 9.5: echo, printf×2, seq×3, expr×2, factor×2, basename, dirname, numfmt, wc,
wc-l, head, tail, sort, sort-r, uniq, cut, tac, sha256sum, md5sum, **cksum**,
base64, **od**.

**FAIL = 1 — `nl`, a console artifact, NOT a coreutils bug.** `nl` emits a TAB
between the line number and text; the Phoenix console tty expands tabs to spaces
on output (OPOST), so the UART capture shows spaces where the tool wrote a TAB.
The tool's byte output is correct; it just isn't byte-comparable through the
cooked serial console. (Future: test `nl -s' '`, or compare over a raw channel.)

## `cksum` + `od` Data Abort — ROOT-CAUSED + FIXED (this run confirms it)

Earlier runs showed `cksum` + `od` crashing with a **Data Abort (EL0)** (od
computed correct output then crashed on exit; cksum crashed before its line), and
the kernel fault-dump double-faulted at EL1 pushing the signal frame onto the
exhausted user stack (`hal_cpuPushSignal` → `hal_memcpy`), corrupting the dump.

**Root cause:** the aarch64 main-thread user stack (`SIZE_USTACK`) was only 8
pages = **32 KiB**, fixed-size, with **no auto-stack-growth**. cksum/od exceed it
→ overflow → SIGSEGV → signal-push double-fault. wc/base64 stay under 32 KiB.

**Fix:** raised `SIZE_USTACK` to 1 MiB (256 pages; demand-paged `MAP_NONE`, so it
costs only touched pages; Linux's main-thread default is 8 MiB). kernel commit
`8ae20864`; manifest `manifests/2026-08-21-ustack-1mib-cksum-od-fix.md`. After the
fix both **cksum and od PASS** — Data Abort gone, output bit-identical to host.

**Open defense-in-depth follow-up:** even with a bigger stack, a process that does
overflow still double-faults the kernel on signal delivery — `hal_cpuPushSignal`
should validate the signal-frame target against the process VM and terminate the
process cleanly instead. See memory `project_coreutils_cksum_od_dataabort`.

## Harness notes

- `difftest.py check` accepts multiple `--log` files and merges **per-case** (each
  case taken from the first log that captured it) — do NOT concatenate logs (one
  log's last case would bleed into the next log's boot output).
- The UART parser strips ANSI-CSI, drops psh prompt lines, normalizes the
  `/root/cutest/` path to bare filenames, and filters interleaved kernel/driver
  async log lines (`nfs-fs:`, `lwip:`, …) out of a command's region.
- Large batches of file-reading commands in one netboot cycle can flake (a psh/NFS
  timing stall); split into cycles + use `--inter-cmd-secs 8–10`.

## Reproduce

```
python3 difftest.py host --refdir <native-GNU-9.5>/src     # -> expected/
sudo cp -rT corpus /srv/phoenix-rpi4-nfs/root/cutest
python3 difftest.py gencmds > cmds.txt
./scripts/test-cycle-psh-interact.sh --label cutest --cmd-file cmds.txt
python3 difftest.py check --log <A.log> <B.log>
```
