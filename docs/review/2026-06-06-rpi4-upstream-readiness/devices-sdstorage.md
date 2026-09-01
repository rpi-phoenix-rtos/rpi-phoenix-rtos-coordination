# devices-sdstorage — upstream-readiness review

- **Area:** `devices-sdstorage` (libstorage-based server layer for the BCM2711 EMMC2 SD driver)
- **Repo:** `phoenix-rtos-devices`, base `origin/master` `d511e0f` → head `master` `ebac8e4`
- **Files reviewed:**
  - `storage/bcm2711-emmc/sdstorage_dev.c`
  - `storage/bcm2711-emmc/sdstorage_dev.h`
  - `storage/bcm2711-emmc/sdstorage_srv.c`
  - `storage/bcm2711-emmc/Makefile`
- **Method:** these four files are whole-file copies of `storage/zynq7000-sdcard/{sdstorage_dev.c,sdstorage_dev.h,sdstorage_srv.c,Makefile}`. Because the area diff is "new file" against `origin/master`, I diffed each file against its `zynq7000-sdcard` original to isolate the genuine RPi4 delta, and reviewed **only the hunks that differ from the zynq baseline**. Inherited-from-zynq code (the malloc chains in `sdstorage_handleInsertion`, `calculateSizeWithSaturation`, the `oid` DEVTYPE bit-masking, the `min(len, SDCARD_MAX_TRANSFER)` clamp, `storage_read/write/sync` dispatch) is upstream code, not RPi4 contribution, and is **not flagged**.

Referents used: `storage/zynq7000-sdcard/` (same filenames — the copy source), `storage/zynq-flash/zynq-flash.c` (root-mount-by-id pattern), `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` (debt-marker convention).

---

## Findings (by severity)

### 1. Makefile:8-18 / sdstorage_dev.c, .h / sdstorage_srv.c · **ARCH** · sev=high · whole-file duplication of `zynq7000-sdcard`

**WHAT:** `sdstorage_dev.c` (832 lines), `sdstorage_dev.h`, `sdstorage_srv.c`, and the Makefile structure are copied verbatim from `storage/zynq7000-sdcard/` with only the RPi4 deltas below. The `NAME := libsdcard-bcm2711` / `bcm2711-emmc` targets are otherwise identical to `libsdcard-zynq` / `zynq7000-sdcard`. The Makefile's own `NOTE(rpi4b-emmc)` acknowledges this and says "De-duplicate into a shared lib before upstreaming."

**WHY:** This is the single largest upstream blocker for the area. Two near-identical copies of a generic SDHCI/libstorage server is exactly the kind of duplication a Phoenix maintainer will reject. Referent: `storage/zynq7000-sdcard/sdstorage_dev.c` and `sdstorage_srv.c` are byte-for-byte the parents; the only BCM2711-specific source is `bcm2711-sdio.c` (out of this area). The bulk (`sdstorage_*`, `sdcard.c`, `sdhost_defs.h`) is platform-agnostic and should live in a shared `libsdstorage`/`libsdcard-generic` with the SoC layer (`*-sdio.c`) as the only per-target source.

**REC:** Before upstream, factor the generic core (`sdstorage_dev.c`, `sdstorage_srv.c`, `sdcard.c`, `sdhost_defs.h`, `sdstorage_dev.h`) into one shared static lib consumed by both `zynq7000-sdcard` and `bcm2711-emmc`, each providing only its `*-sdio.c` platform glue. The `setRootDev`/`getRootStorageId` mount-by-id extension (finding 4) and the `ssize_t` callback fix (finding 6) should land in that shared core so zynq benefits too. **NEEDS-HW** (large structural change touching two drivers; document, do not apply overnight).

---

### 2. sdstorage_srv.c:1348-1355 (`main`) · **ARCH** · sev=med · root-mount failure made non-fatal, with a bring-up-specific justification

**WHAT:** The RPi4 diff changes the root-mount failure path from the zynq original's `LOG_ERROR(...); exit(EXIT_FAILURE);` to a non-fatal `LOG_ERROR(... continuing ...)` that lets the daemon keep running. The justifying comment explicitly cites "during #120 bring-up, for a live diag-udp SD-read probe."

**WHY:** Diverges from **both** same-class referents: `storage/zynq7000-sdcard/sdstorage_srv.c` (the parent) and `storage/zynq-flash/zynq-flash.c` treat a failed `-r` root mount as fatal. When an init script passes `-r /dev/mmcblk0p2:ext2`, root is expected to mount; silently continuing yields a system that boots to the wrong (dummyfs) root with no hard failure — surprising for an upstream consumer. The stated rationale (diag-udp SD probe, "#120 bring-up") evaporates for upstream.

**REC:** For upstream, revert to fatal-on-root-mount-failure to match the referents, or gate the soft-fail behind an explicit opt-in flag (e.g. `-k`/keep-alive) rather than making it the default. Update the comment to drop the `#120`/diag-udp bring-up rationale. **NEEDS-HW** (boot control-flow / semantics; document only).

---

### 3. sdstorage_dev.c:169-184, 201-212 · **COMMENT/ROLLBACK** · sev=med · single-block CMD17/CMD24 sidestep uses non-upstream `TODO(#120)` marker, no debt-doc entry

**WHAT:** The `sdcard_readCb`/`sdcard_writeCb` loops force single-block transfers as a workaround for unproven multi-block CMD18/CMD25, marked `TODO(#120)`. This is **load-bearing temporary code** (multi-block is still broken per project memory) — correctly NOT dead code, must NOT be deleted. But the marker form is the issue.

**WHY:** Step-4 reconciliation: the project's debt convention (`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`, line 39) is `TODO(TD-NN): <hint>` linked to a `TD-NN` entry. This sidestep has **no `TD-NN` entry** and uses a bare GitHub-issue number `#120`. Confirmed `git grep "TODO(#"` on `origin/master` returns **zero** hits — upstream uses plain `/* TODO: ... */` (e.g. `zynq-flash.c:467 "/* TODO: add umount() support ... */"`). So `#120` is meaningful only to this project, not to a Phoenix maintainer, and it dodges the project's own debt-tracking.

**REC:** (a) Register this sidestep (and finding 1's duplication, and finding 4's mount-by-id workaround) as `TD-NN` entries in `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` and switch the inline markers to `TODO(TD-NN)`; or (b) for the public drop, rewrite as a plain self-contained `/* TODO: ... */` that does not lean on an issue number. Keep the technical explanation — it is good. **NEEDS-HW for code-path removal** (depends on the multi-block fix); the marker/doc reconciliation itself is **APPLY-SAFE** (comment + doc edit).

---

### 4. sdstorage_dev.c:116-128, 367-405, 846-873; sdstorage_srv.c:1226-1266 · **ARCH/BUG** · sev=med · root-mount-by-id state machine — design is sound, but path-string match is fragile and uses a poll-loop

**WHAT:** The #120 fix replaces the removed `storage_oidResolve` (which did a `devfs/<name>` `lookup` that cannot work before `/` exists / devfs is bound) with: record the matching partition's storage id in `sdstorage_createDeviceFile` by `strcmp(path, sdcard_common.rootDev)`, then in `sdstorage_mountRootFs` poll `getRootStorageId` up to 50×100 ms (~5 s) and `storage_mountfs(storage_get(rootId), ...)`.

**WHY:** Mounting by storage id is **correct and matches the referent** — `zynq-flash.c:446-459` mounts the just-created partition by the id returned from `flash_partAdd`, never re-resolving a path. So the core idea is the right Phoenix pattern and the removal of `storage_oidResolve` is justified. Two residual concerns: (a) matching by exact `/dev/...` *string* is more brittle than zynq-flash's "use the id you just created" (a trailing-slash or alias difference in the `-r` arg silently fails to match); (b) the ~5 s poll-loop is a soft synchronization — it works because the boot-time insertion is synchronous in the common case, but it is a busy-wait substitute for an explicit "node ready" signal. Lifetime note: `sdstorage_handleRemoval` never clears `rootFound`/`rootStorageId`; this is **harmless** because `getRootStorageId` is read exactly once at boot from `mountRootFs` (verified) — call out, not a bug.

**REC:** Acceptable for now given the pre-rootfs constraint; for upstream, prefer returning the created storage id directly from the insertion path to the mount caller (zynq-flash style) instead of a string match + poll, eliminating both the `strcmp` fragility and the timed loop. At minimum document why the poll bound is 5 s. **NEEDS-HW** (boot-sequence timing; document only).

---

### 5. sdstorage_srv.c:1262-1264 · **STYLE/ROLLBACK** · sev=low · diagnostic-flavored success log line on the root-mount path

**WHAT:** The success path prints `LOG_TAG ": mounted %s (%s, id=%u) as / after %d tr%s, portRegister=%d"` including the retry count and an "tr"/"tries" pluralization helper. The zynq original simply `return portRegister(...)` with no log.

**WHY:** The "after N tries, portRegister=%d" telemetry is bring-up instrumentation (it exposes the finding-4 poll-loop's iteration count), not steady-state operational logging, and the cutesy `(tries == 1) ? "y" : "ies"` is below the bar for the file's own terse `LOG_ERROR`/`TRACE` idiom (referent: the `TRACE`/`LOG_ERROR` macros defined at the top of this same file). Contrast the legitimately-operational new lines that should stay: `sdstorage_dev.c:664 "ready: %u MiB, %d partition(s)"` and `:534 "no card present in slot %u"` are useful and match the file idiom.

**REC:** Trim to a plain one-line success message (or drop entirely, as zynq does) and remove the retry-count/pluralization. Keep the two `sdstorage_dev.c` operational lines. **APPLY-SAFE** (log-line text only; build + boot smoke).

---

### 6. sdstorage_dev.c:42-53, 880-891; sdstorage_srv.c:944-950; .h:885-891 · **STYLE** · sev=low · stale copyright/author on copied-and-modified files

**WHAT:** All three source files retain `Copyright 2023 Phoenix Systems / Author: Jacek Maksymowicz` (correct for the zynq origin) while the Makefile — the same commit — now reads `Copyright 2026 Phoenix Systems / Author: Witold Bołt`.

**WHY:** These files were substantively modified (root-mount state machine, single-block sidestep, callback signatures). Phoenix convention is to credit both the original and modifying authors; the inconsistency between the Makefile header and the source headers is a readiness nit a maintainer will notice. Referent: the file's own header block format.

**REC:** Add the 2026 / RPi4 author line alongside the original 2023 / Maksymowicz attribution in the three source/header files (matching what the Makefile already does). **APPLY-SAFE** (header comment only).

---

### 7. sdstorage_dev.c:154, 155 · **STYLE** · sev=low · `ssize_t` callback comment is good; verify it lands in shared core

**WHAT:** `sdcard_readCb`/`sdcard_writeCb` return type changed `int` → `ssize_t` with the comment "to match cache_readCb_t (libcache); on aarch64 ssize_t is 64-bit, so the original 'int' tripped -Wincompatible-pointer-types." This is a correct portability fix.

**WHY:** Legitimate and well-explained — flagged only to note it is a fix the zynq parent also needs (the parent still uses `int`, so it compiles only because zynq is 32-bit). Tie-in to finding 1: when de-duplicated, this fix must live in the shared core, not be RPi4-only.

**REC:** No change to the RPi4 code; fold into the dedup (finding 1) so both arches use the `ssize_t` signature. **APPLY-SAFE** (no-op for this area; tracked under finding 1).

---

## Summary

- **Counts:** 7 findings — ARCH ×2 (1 high, 1 med), ARCH/BUG ×1 (med), COMMENT/ROLLBACK ×1 (med), STYLE ×3 (low), STYLE/ROLLBACK overlap. By severity: 1 high, 3 med, 3 low. APPLY-SAFE: findings 5, 6, 7 plus the comment/doc half of 3. NEEDS-HW (document only): 1, 2, 4, and the code-removal half of 3.
- **Most important issue:** the whole-file duplication of `zynq7000-sdcard` (finding 1) — the area is ~95% copied generic code with only `bcm2711-sdio.c` (out of area) being SoC-specific. This is the dominant upstream blocker and is self-acknowledged in the Makefile's `NOTE(rpi4b-emmc)`, but that NOTE is not tracked as a `TD-NN` debt item.
- **Positive:** the #120 root-mount-by-id fix is the *correct* Phoenix pattern (matches `zynq-flash.c`'s mount-by-returned-id), and removing the pre-rootfs `devfs/` `lookup` (`storage_oidResolve`) was justified. The single-block sidestep is load-bearing, correctly retained, and clearly explained — only its marker form (`TODO(#120)`, no `TD-NN`) is off-convention. Error-path malloc cleanup and locking in `sdstorage_handleInsertion` are inherited from zynq and sound.
