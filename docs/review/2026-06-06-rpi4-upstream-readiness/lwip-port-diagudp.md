# lwip-port-diagudp — upstream-readiness review

- **Area:** `lwip-port-diagudp`
- **Repo:** `phoenix-rtos-lwip` — base `fc152cb` (origin/master) → head `a078a5c` (master)
- **Files reviewed (net diff vs origin/master):**
  - `port/diag-udp.c` — **5534 lines, entirely new**
  - `port/main.c` — +13 (genet registration + diag-udp init hook)
  - `port/mbox.c` — +88 (corruption-forensic diagnostics)
  - `.gitignore` — +10 (generated WiFi-fw blobs + `artifacts/`)
- **Diff command:**
  `git -C sources/phoenix-rtos-lwip diff origin/master master -- port/diag-udp.c port/main.c port/mbox.c .gitignore`

## What `diag-udp.c` is (characterization)

A single-file UDP responder bound to `IP_ANY:9999`, registered via
`tcpip_callback` from `init_diag_udp()`. Each inbound datagram's first byte
selects one of ~30 single-shot text "format" handlers (dispatch table at
`diag_udp_recv`, lines 5383–5503). It is the accreted triage console for the
entire Pi 4 bring-up: SMP burn threads (`b`), thread/mem stats (`t`/`m`),
clocks (`c`), GPIO (`g`), framebuffer (`V`), device-node probes (`D`/`R`/`k`),
PCIe error regs (`P`), **reboot/halt via platformctl (`r`/`h`)**, WiFi
power-cycle + the full BCM43455 SDIO firmware-load investigation (`w`, `e`,
`f`, `F`, `E`, `A`, `S`, `B`, `W`, `L`, `M`, `I`, `H`, `G`, `i`, `s`).

It is **diagnostic-only and self-contained**: every symbol is `static` except
the single entry point `init_diag_udp()`. No production code path calls into
it; no external file references its helpers (verified — the only out-of-file
reference is `main.c`'s forward-decl + call). It *consumes* real devices
(opens `/dev/thermal`, `/dev/hwrng`, `/dev/mmcblk0p2`; pokes mailbox/SDIO/GPIO
MMIO) but contains **no driver logic that exists only here** — the actual
drivers (thermal, hwrng, emmc) live in `phoenix-rtos-devices`. The one
genuinely-reusable helper, `diag_mboxProp1in1out` (mailbox property channel),
is **already productionized** as `rpi4_mboxProp1in1out` in
`phoenix-rtos-devices/sensors/rpi4-thermal/rpi4-thermal.c:73`. So removing
diag-udp.c preserves nothing that is not already in a real driver.

External consumer: `scripts/diag-udp-probe.sh` (coordination repo) sends one
command char via `nc -u` and saves the reply to `artifacts/diag-udp/`. It is a
host-side dev tool; it survives diag-udp removal harmlessly (just stops getting
replies).

---

## Findings (ordered by severity)

### 1. `port/diag-udp.c:1–5534` · **ROLLBACK** · sev=**high** · entire file is unauthenticated diagnostic-control code — remove fully before presentation
**WHAT:** A 5534-line UDP responder on `IP_ANY:9999` that, beyond read-only
counters, exposes **unauthenticated control + write primitives** to anyone on
the network: `r`/`h` reboot & halt the board (`diag_pmReboot` →
`platformctl`/PM reset, lines 562–667); `w` power-cycles the WiFi chip; the
SDIO handlers issue live **CMD52/CMD53 register writes** (`diag_sdioCmd52`,
`diag_sdioCmd53Write`) and clock/voltage changes against the SDHCI controller;
`V`/`R`/`k` read framebuffer and block-device contents; the mbox path dumps
**physical-memory windows**. There is no auth, no build gate, no rate limit on
the dangerous ops.
**WHY:** This is the headline rollback candidate. It is not upstream material
on two independent grounds: (a) it is accreted single-task diagnostic scaffold
with a stale file header (the doc-block at lines 1–31 documents only `t` +
default netif stats, but the dispatch grew to ~30 commands — COMMENT debt
compounding the ROLLBACK); (b) shipping an unauthenticated network
reboot/halt/register-write/memory-dump surface is a security non-starter for a
maintainer review, independent of code quality.
**REC:** **Full removal, not a build flag.** Delete `port/diag-udp.c` entirely.
A `#ifdef`-guarded retention still places dangerous control code in the
upstream-facing tree for maintainers to read and is the wrong trade; dev
retention belongs in git history / a private dev branch, not a compile flag.
Because `port/Makefile` is `SRCS := $(wildcard $(LOCAL_PATH)*.c)`, **deleting
the file removes it from the build with no Makefile edit**. The only paired
edit required is reverting the `main.c` hook (finding #2).
**NEEDS-HW** — removal must be confirmed by a `--scope core` rebuild (green)
plus a netboot boot-to-psh smoke (the daemon must still come up and serve the
netif with the responder gone). Phase 2.

### 2. `port/main.c:153–160` · **ROLLBACK** · sev=high · revert the diag-udp init hook (paired with #1)
**WHAT:** The block
```c
if (have_intfs > 0) {
    void init_diag_udp(void);
    init_diag_udp();
}
```
is the sole live caller of the diag responder.
**WHY:** It is the one external dependency on diag-udp.c; without removing it
the build breaks (undefined `init_diag_udp`) once the file is deleted.
**REC:** Delete lines 153–160 (the comment + the `if`/forward-decl/call). No
other main.c change is needed for rollback. **Note:** the separate hunk at
main.c:84/100–102 (`register_driver_genet()`) is **legitimate production code**
matching the existing `register_driver_enet`/`_greth` pattern — it belongs to
the `lwip-genet` review area, is NOT a diag finding, and must be **kept**.
**NEEDS-HW** — same rebuild + netboot smoke as #1 (they are one atomic revert).

### 3. `port/mbox.c:57–217` · **ROLLBACK** · sev=high · separate corruption-forensic diagnostics that survive diag-udp removal — must be reverted independently
**WHAT:** A distinct set of #121/#129 heap-corruption forensics edited *in
place* into mbox.c — unrelated to diag-udp.c (verified: `mbox_diagFreed`
appears only in mbox.c, never in diag-udp.c). Three parts:
- the `mbox NEW:` `debug()` block in `sys_mbox_new` (va2pa PA logging, lines ~57–72);
- the `mbox_diagFreed[32]` / `mbox_diagFreedIdx` globals + record-on-free in
  `sys_mbox_free` (lines 79–90);
- the corruption **guard** in `mbox_tryfetch` (lines ~155–213): a
  `ring==NULL || sz==0 || head>=sz` check that on detection logs PA/free-state +
  a 16×u64 memory dump and **returns "empty" instead of dereferencing**.

Plus a **duplicate `#include <stdio.h>`** added twice in the header block —
direct accretion evidence; both copies go when the block reverts.
**WHY:** All three are diagnostic scaffold carrying `TODO(#121)`/`TODO(#129)`
markers for an *unconfirmed* root cause. They will not be deleted by removing
diag-udp.c (different file, in-place edits), so they are an independent rollback
item that is easy to miss.
**REC:** Revert mbox.c to `origin/master` (drops all three blocks, both
`<stdio.h>` dupes, and the `<sys/mman.h>`/`<sys/debug.h>` diagnostic includes).
Split the apply into two risk classes:
- The `sys_mbox_new` PA-log block and the `mbox_diagFreed` record/dump are
  **pure-diagnostic prints** — APPLY-SAFE once the build is green.
- The `mbox_tryfetch` **guard is NEEDS-HW**: it is a survive-not-crash
  band-aid masking the unconfirmed #121/#129 corruption. Removing it is correct
  for upstream, but the boot must be shown to survive without it. This is the
  one mbox.c change that needs the netboot smoke before applying. Do not
  blind-apply overnight.

### 4. `.gitignore:6–13` · **ROLLBACK/COMMENT** · sev=med · revert dangling generated-blob + artifacts ignore entries
**WHAT:** Added ignores for `port/wifi-fw-43455.{c,h}`, `port/wifi-nvram-43455.{c,h}`
(Cypress-EULA generated firmware blobs) and `artifacts/`.
**WHY:** diag-udp.c is the **sole in-tree consumer** of `wifi-fw-43455.h` /
`wifi-nvram-43455.h` (verified by grep). Those files do not exist in a clean
checkout, the target Makefile states WiFi is "parked," and the `artifacts/`
dir is a coordination-repo concept. Once diag-udp.c is removed these ignore
lines reference nothing real and only advertise the parked WiFi diagnostic
effort to upstream.
**REC:** Revert the added `.gitignore` lines together with #1. (If a future
real WiFi driver needs the generated blobs, the ignore can be re-added then,
scoped to that driver.) **APPLY-SAFE** (no build/boot impact; .gitignore only).

### 5. `port/diag-udp.c:1–31` · **COMMENT** · sev=low · stale file header (subsumed by #1)
**WHAT:** The file doc-block documents only the `t` command and the default
netif-stats wire format; the responder grew to ~30 commands including
reboot/halt/SDIO-write.
**WHY:** Misleading if the file were ever kept. Recorded for completeness.
**REC:** Moot under full removal (#1); no separate action. **APPLY-SAFE.**

---

## Production code accidentally living here? — none lost on removal
- Every helper is `static`; only `init_diag_udp` is external → blast radius of
  deletion = the file itself + the main.c hook. Nothing external links the helpers.
- The only reusable primitive, the VideoCore mailbox-property helper
  (`diag_mboxProp1in1out`), is **already** present as `rpi4_mboxProp1in1out` in
  `sensors/rpi4-thermal/rpi4-thermal.c` — removal duplicates nothing.
- All device-touching handlers (`/dev/thermal`, `/dev/hwrng`, `/dev/mmcblk0p2`,
  framebuffer) merely *read* nodes whose drivers live in other repos; no driver
  logic is unique to this file.

---

## Rollback plan (atomic, phase 2 / NEEDS-HW)
1. `git rm port/diag-udp.c` (no Makefile edit — `wildcard *.c` auto-drops it).
2. main.c: delete the diag-udp hook (153–160); **keep** the genet registration.
3. mbox.c: revert to origin/master (all three diagnostic blocks, both `<stdio.h>`
   dupes, the two diagnostic includes). The `mbox_tryfetch` guard removal is the
   single NEEDS-HW step.
4. .gitignore: revert the added wifi-fw/nvram + `artifacts/` lines.
5. Verify: `--scope core` rebuild green; `strings loader.disk | grep -i diag-udp`
   shows nothing; netboot boot-to-psh smoke shows the lwip daemon still serves
   the netif and port 9999 no longer answers.

---

## Summary
- **Counts:** ROLLBACK ×4 (3 high, 1 med), COMMENT ×1 (low). All NEEDS-HW
  except the `.gitignore` revert and the two pure-print mbox blocks
  (APPLY-SAFE), and pending the phase-2 build+netboot smoke.
- **Most important issue:** `port/diag-udp.c` is a 5534-line, unauthenticated
  UDP control/diagnostic surface on `IP_ANY:9999` exposing reboot/halt, SDIO
  register writes, and physical-memory dumps — **full removal**, not a build
  flag. It is self-contained (`wildcard *.c` build, all-static), so removal is
  clean: delete the file + the main.c hook.
- **Don't-miss item:** mbox.c is a **separate** rollback edited in place;
  deleting diag-udp.c does not touch it. Its `mbox_tryfetch` corruption guard
  is the one band-aid whose removal genuinely needs the netboot smoke.
- **Don't double-count:** the main.c `register_driver_genet()` hunk is
  legitimate production (lwip-genet area), not a diag finding.
