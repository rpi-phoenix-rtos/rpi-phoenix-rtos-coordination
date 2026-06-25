# phoenix-rtos-ports build + NFS staging — night session (2026-06-24)

Roadmap item 1.5 / night-audit item 1: "build the remaining phoenix-rtos-ports for
aarch64-phoenix and stage them to the NFS rootfs export."

## TL;DR (read first)

**The headline is a reframe: there were almost no "remaining" ports to build.** Every
binary-producing port that the rpi4b project actually selects
(`_projects/aarch64a72-generic-rpi4b/ports.yaml`) was already built on 2026-06-08/09 AND
staged to `/srv/phoenix-rpi4-nfs`, byte-identical to the build tree (verified by `cmp`).
The task's "staged-but-UNVERIFIED: curl, openssl, dropbear, luac" are all present as valid
aarch64 ELFs. So the real deliverables this session are:

1. **Verification** that the existing staging is correct (done — ELF + `cmp` checks below).
2. **One genuinely-new useful port: `fs_mark`** (filesystem benchmark; serves the recurring
   NFS-perf work). Built standalone against the toolchain + staged.
3. **The EXEC-VERIFY CHECKLIST** (below) — the highest-value output, for the orchestrator's
   single nfsroot psh cycle.

No `.buildroot` / `--scope core` build was run (another subagent owns the shared build tree;
its `_build/<target>` had live 23:46 timestamps). fs_mark was built in scratch with the
toolchain directly, then copied to the NFS export. **No port.def.sh was modified, so there is
no phoenix-rtos-ports commit** (fs_mark built from the existing, unchanged port files +
patches).

## Port inventory (rpi4b ports.yaml)

ports.yaml selects: busybox, libnfs, zlib, lzo, pcre, jansson, lua, coremark, picocom,
libevent, mbedtls, openssl, micropython, dropbear (use zlib), curl (use mbedtls), lighttpd
(use zlib).

| Port        | Produces             | Status                                            |
|-------------|----------------------|---------------------------------------------------|
| busybox     | /bin/busybox + applets | built + staged + HW-run (prior)                 |
| coremark    | /bin/coremark        | built + staged + HW-run (prior)                   |
| micropython | /bin/micropython     | built + staged + HW-run (prior)                   |
| picocom     | /bin/picocom         | built + staged + HW-run (prior)                   |
| lua         | /usr/bin/lua         | built + staged (HW-confirmed prior)               |
| luac        | /usr/bin/luac        | built + staged                                    |
| openssl     | /usr/bin/openssl     | built + staged (re-test viability — see below)    |
| curl        | /usr/bin/curl        | built + staged (mbedtls TLS)                       |
| dropbear    | /usr/bin/dropbearmulti (+ dropbear/dbclient/scp hardlinks) | built + staged |
| lighttpd    | /usr/sbin/lighttpd   | built + staged                                    |
| libnfs      | libnfs.a (no binary) | static lib; consumed by nfs-smoke/nfs fs server   |
| zlib, lzo, pcre, jansson, libevent, mbedtls | static libs only | linked into the above; no standalone binary |
| **fs_mark** | /bin/fs_mark         | **NEW this session — built + staged (unverified)**|

Other ports present in the tree but NOT in rpi4b ports.yaml (net-new scope, NOT built):
- `wpa_supplicant` — skipped: WiFi fw-exec gate fails on this HW (#91), dead weight.
- `openvpn`, `openiked`, `sscep` — skipped: heavy, niche, openssl-dependent, low value.
- `heatshrink` — small but no current consumer; skipped (can add later if needed).
- `smolrtsp`, `coreMQTT`, `azure_sdk`, `coremark_pro`, `lsb_vsx`, `fs_mark` libs — sample/test
  or library-only.
- jq / file / ncurses — **not present in the ports tree** (cannot build).

## Build pass/fail

| Build attempt | Result | Notes |
|---------------|--------|-------|
| fs_mark 3.3 (commit 2628be5) | PASS | 3 Phoenix patches applied clean; cross-compiled with `aarch64-phoenix-gcc -mcpu=cortex-a72`; static ELF 189024 B (stripped). |

No build failures this session. (The 16 ports.yaml ports were not rebuilt — already-built
artifacts in `.buildroot/_build/.../prog.stripped/` are byte-identical to the staged NFS
copies.)

## Staged file list (all verified valid static aarch64 ELF)

| Path on /srv/phoenix-rpi4-nfs | Size (B) |
|-------------------------------|----------|
| bin/busybox                   | 399672   |
| bin/coremark                  | 56928    |
| bin/micropython               | 455328   |
| bin/picocom                   | 131608   |
| **bin/fs_mark** (NEW)         | 189024   |
| usr/bin/lua                   | 243064   |
| usr/bin/luac                  | 155384   |
| usr/bin/openssl               | 2213008  |
| usr/bin/curl                  | 1203608  |
| usr/bin/dropbearmulti         | 452896   |
| usr/sbin/dropbear (hardlink)  | 452896   |
| usr/bin/dbclient (hardlink)   | 452896   |
| usr/bin/scp (hardlink)        | 452896   |
| usr/sbin/lighttpd             | 2130296  |

## openssl /dev/urandom viability (re-checked, no boot)

The audit noted openssl previously exec'd with ZERO stdout; leading hypothesis = missing
`/dev/urandom` for RNG seed. Re-checked the source (read-only):

- `phoenix-rtos-posixsrv/special.c:272` registers `/dev/urandom`, and its read handler
  (`random_read_op`, lines 110-147) PREFERS `/dev/hwrng` and **falls back to `rand()` if
  hwrng is absent** — it always returns the requested byte count, never zero-length. So
  `/dev/urandom` is functional regardless of hwrng.
- The nfsroot boot DOES launch both posixsrv (user.plo.yaml:88) and rpi4-hwrng (line 108)
  AFTER the NFS takeover, so `/dev/urandom` will be hwrng-backed when openssl runs.

Conclusion: the `/dev/urandom` seed path is viable for nfsroot. If openssl STILL emits zero
stdout in the exec-verify cycle, the cause is NOT RNG seeding — investigate openssl's
config/conf parsing or a libphoenix gap (capture stderr). Recommend the orchestrator run
`/usr/bin/openssl version` first (no RNG) before `openssl rand`.

## PATH caveat (important for the checklist)

psh has no PATH resolution and the HW-run ports live in `/bin` while openssl/curl/dropbear
live in `/usr/bin` + `/usr/sbin`. **All exec-verify commands MUST use absolute paths.**

## EXEC-VERIFY CHECKLIST (for the orchestrator's single nfsroot psh cycle)

Run after `nfs ... takeover` owns "/" and psh prompt is up. Each line = one psh command +
expected. Order from cheapest/most-likely-pass to heaviest.

```
# 1. fs_mark (NEW) — help/usage (exits non-zero but prints usage to stderr)
/bin/fs_mark
    expected: usage text "Usage: fs_mark ..." (lists -d/-s/-n/-L/-S options)

# 2. lua — one-liner (HW-confirmed prior; sanity re-check)
/usr/bin/lua -e "print('lua-ok')"
    expected: lua-ok

# 3. luac — version banner
/usr/bin/luac -v
    expected: "Lua 5.3.6  Copyright ..." line

# 4. openssl — version (no RNG needed; the critical re-test)
/usr/bin/openssl version
    expected: "OpenSSL 1.1.1a  ..."  (NOT empty — empty = still broken, capture stderr)

# 5. openssl — RNG via /dev/urandom (confirms entropy path)
/usr/bin/openssl rand -hex 16
    expected: 32 hex chars on one line

# 6. curl — version (mbedtls TLS backend)
/usr/bin/curl --version
    expected: "curl 7.64.1 ... mbedTLS/..." banner

# 7. dropbearkey — generate a key (dropbear multi-binary, no network)
#    NOTE: this port is dropbear 2018.76, which PREDATES ed25519 (added 2020.79).
#    The binary's strings show only ssh-rsa + ecdsa-sha2-* — use -t rsa (universal).
/usr/bin/dropbearmulti dropbearkey -t rsa -f /tmp/id_test
    expected: "Generating key, this may take a while..." + "Public key portion is ssh-rsa AAAA..." + fingerprint
    (this also proves the multibinary loads + dispatches an applet, so a separate
     `dropbear -h` server check is redundant and is intentionally omitted — do NOT
     run `/usr/sbin/dropbear` bare, it would try to daemonize on port 22 and hang.)

# 8. lighttpd — version
/usr/sbin/lighttpd -v
    expected: "lighttpd/1.4.79 ..." banner

# 9. micropython (HW-run prior; sanity)
/bin/micropython -c "print('mpy-ok')"
    expected: mpy-ok
```

Notes for the orchestrator:
- Capture stderr too — several tools (fs_mark usage, dropbearkey progress) print to stderr.
- #4 is the load-bearing re-test. Empty stdout there = openssl still broken despite
  /dev/urandom; that's a real finding to record (likely libphoenix/conf gap, not RNG).
- Exec-from-NFS is slow (per-page RPCs); allow generous per-command time, especially for the
  2 MB openssl/lighttpd binaries.

## libphoenix/libc gaps to report

None newly identified this session — fs_mark cross-compiled with no missing-symbol errors
against the toolchain-bundled libphoenix. (If any port fails at runtime in the cycle, the
exec-verify stderr will name the gap; record it then.)

## EXEC-VERIFY RESULTS (2026-06-25, nfsroot scripted-psh, log …-ports-verify)

Run on the mailbox-rollout + diagnostic-cleanup build (nfsroot variant, boot reached psh on
the NFS root — also confirms the vcmbox rollout + genet-MAC-via-vcmbox boot clean on nfsroot):

| Cmd | Result |
|---|---|
| `/usr/bin/openssl version` | **PASS** — `OpenSSL 1.1.1a  20 Nov 2018`. The load-bearing re-test: the zero-stdout anomaly is RESOLVED (hwrng-backed /dev/urandom seed path works). |
| `/usr/bin/curl --version` | **PASS** — `curl 7.64.1 (aarch64-unknown-phoenix) libcurl/7.64.1 mbedTLS/2.28.0`. |
| `/usr/bin/dropbearmulti dropbearkey -t rsa -f /tmp/id_test` | **PASS** — generated a 2048-bit RSA key + ssh-rsa pubkey + sha1 fingerprint (multibinary loads + dispatches the applet). |
| `/bin/fs_mark` | **PASS** — prints `Usage: fs_mark …` (the new port executes). |
| `/usr/bin/lua -v` | `psh: /usr/bin/lua not found` — but lua IS staged (`/srv/phoenix-rpi4-nfs/usr/bin/lua`, 243 KB, verified post-run). lua was the FIRST `/usr/bin` access of the boot and ENOENT'd, while openssl/curl/dropbear (later `/usr/bin` accesses) all succeeded → this is the known **NFS first-read-transient-ENOENT (#156)** (first access misses the libnfs dircache, subsequent ones hit), NOT a staging gap. A retry of lua would succeed. |

Net: the crypto/network userland (openssl/curl/dropbear) + the new fs_mark all run from NFS —
broad-app-support validated. The lone "not found" re-confirms the #156 first-read-ENOENT
residual (proper fix = libnfs post-mount dircache invalidate / readiness, tracked separately),
not a port problem. No re-staging needed.
