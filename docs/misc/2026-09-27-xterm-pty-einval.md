# xterm dies at start: `fatal pty error errno=22, error=23` (2026-09-27)

Symptom: in the showcase gate's X arm (old lane `startx_gpu action`) and in the new lane (mig-x,
Xorg-drm), both xterms exit at start with

```
xterm: fatal pty error errno=22, error=23 device "/dev/pts/10"
xterm: fatal pty error errno=22, error=23 device "/dev/pts/12"
```

This doc was written from the log archive, the sources and the binaries. **No Pi cycle was run.**
The hardware effect of the fix is a prediction, with a pre-registered check at the end.

## Verdict

| | |
|---|---|
| Failing call | `tcsetattr(slave, TCSANOW, &tio)` in xterm's child. xterm error 23 is `ERROR_TIOCSETP` (`error.h:70`), raised at `main.c:4705` when `ttySetAttr()` = `tcsetattr(fd, TCSANOW, …)` (`xterm_io.h:298`) fails. |
| Why EINVAL | The xterm on the NFS root is a **stale static binary**. Its `tcsetattr` is the one from before libphoenix `2b6b552`, which kept the command in an `int`. On aarch64 that sign-extends: TCSETS `0x805c7402` goes out as `0xffffffff805c7402`. The kernel forwards it unchanged. libtty used to take the command as `unsigned int`, which truncated it back, so `case TCSETS` matched. Since devices `1657d7d` it takes `unsigned long`, the value misses every `case`, and `default:` returns `-EINVAL`. |
| Culprit | phoenix-rtos-devices **`1657d7d` "!libtty: synchronize libtty_ioctl data access"** (the `unsigned int cmd` → `unsigned long cmd` hunk). Upstream, RTOS-1355. It came into our fork with the sweep-11 merges on 2026-09-21 22:00: devices `bba323d`, posixsrv `df2f604` (adapts pty.c, `b59d350`) and libphoenix `32dcee7` (the matching client fix `2b6b552`). The commit is correct once every client is rebuilt. It breaks any binary built earlier, and any code that stores a request in an `int`. |
| Contributing defect | The xterm port never relinks. The port framework rebuilt the port on 2026-09-27 17:21, but `make xterm` in the kept workdir found `xterm` newer than its objects: libphoenix is not a make prerequisite. So the 2026-09-18 01:07 ELF was just copied again. `prog/xterm` has a fresh mtime and is byte-identical (`cmp`) to that old ELF, as is `/srv/phoenix-rpi4-nfs-gcc16/bin/xterm`. |
| Fix | Kernel: `posix_ioctl()` canonicalises the request to 32 bits (`request = (unsigned long)(u32)request;`). This matches Linux, which takes the ioctl command as `unsigned int`. Every `_IOC()` encoding fits in 32 bits (`IOC_IN` = bit 31, `IOC_NESTED` = bit 29). On 32-bit targets, where `unsigned long` is 32 bits, the line does nothing. |
| `/dev/pts/10`, `/12` | Not a leak. The pty number is posixsrv's shared idtree object id (`pty.c:170`, `posixsrv_object_create`). Each pty uses two ids (master, slave) in the same id space as ptmx, null, zero, urandom and pipes. The first xterm gets master 9 / slave 10, the second 11 / 12. The numbering is deterministic. |

## Evidence

### 1. The archive dates the regression

`grep -c 'fatal pty error'` over `artifacts/rpi4b-uart/*gate-x*.log`: every log up to
`20260920-091610-sdrootgate-x` has 0 hits, and every log from `20260922-045126-gate-x` on has 2,
through `20260927-181626-b18-gate-x`.

### 2. The two boundary boots (`rpi4-sysinfo: build components`)

| component | 2026-09-20 09:16 (good) | 2026-09-22 04:51 (bad) |
|---|---|---|
| phoenix-rtos-kernel | `7348dd999d40` | `7348dd999d40` (same) |
| libphoenix | `5b954c8228f5` | `fa8521d3e979` |
| phoenix-rtos-devices | `876e9605fa2d` | `ad21abf6f356` |
| phoenix-rtos-posixsrv | `211d49a7a3f5` | `df2f60491455` |
| phoenix-rtos-ports | `dd3628a4b79d` | `057891a98152` |
| corelibs / filesystems / usb / build / project / tests / coordination | changed | changed |

The kernel is identical in both boots, so the kernel did not cause the regression.

Relevant commits in the range:

- libphoenix: `2b6b552` *termios: fix tcsetattr* (`int cmd` → `unsigned long cmd`); `a65ff99`
  *ioctl_setResponse: get data size from msg*; `1b8d987` `__INLINE` for `cf*speed` (no change in
  behaviour).
- devices `tty/libtty`: `1657d7d` (cmd `unsigned int` → `unsigned long`, out-buffer contract);
  `5d7c6e4` and `a96afe0` (locking); `1b0f9de` (style). The TCSETS body (ispeed/ospeed check) is
  unchanged apart from style.
- posixsrv: `b59d350` *pty: adapt to driver interface change* (`ioctl_unpackEx`, `_libtty_*`
  under the pty mutex).
- ports: grep, sed, tar, gzip, xz only. Nothing touches xterm or the X libs.

### 3. The xterm binary still sends the sign-extended request

`aarch64-phoenix-objdump -d --disassemble=tcsetattr` of the running binary
(`/srv/phoenix-rpi4-nfs-gcc16/bin/xterm`, identical to `.buildroot/.../prog/xterm`):

```
6586a4: mov  w0, #0x7402
6586a8: movk w0, #0x805c, lsl #16      ; 32-bit TCSETS
6586ac: add  w1, w1, w0                ; + optional_actions, in an int
6586b0: sxtw x20, w1                   ; sign-extend -> 0xffffffff805c7402
...
6586dc: bl   ioctl
```

The current `libphoenix.a` (sysroot and toolchain copies) builds the request in a 64-bit register
instead (`mov x0,#0x7402; movk x0,#0x805c,lsl #16; add x20, x0, w1, sxtw`), so the result is
`0x805c7402`.

The port-built ELF `.buildroot/_build/.../port-sources/xterm-396/xterm-snapshots-xterm-396/xterm`
and its `main.o` are both dated **2026-09-18 01:07**. Only the copies in `prog/` and `root/bin/`
are dated 2026-09-27 17:21 (the `cp -v` at the end of `p_build`).

A census of the NFS root (`bin`, `usr/bin`, `sbin`) finds 15 static binaries with the old
`tcsetattr` and 61 with the new one. xterm is the only desktop program among the 15. The rest are
probes: boshare-probe, cxxprobe*, dfprobe*, fileperf, gl-bo-import, gl-fbo-orient, hevc-play,
memprobe, rpi4-ipcprobe, udprtt, xresizer.

### 4. The server no longer matches it

`_libtty_ioctl(…, unsigned long cmd, …)` is a `switch (cmd)` over `case TCSETS:` etc., with
`default: ret = -EINVAL`. The kernel's `posix_ioctl` uses `IOCPARM_LEN(request)`, which masks, so
the size is right. It then copies `request` verbatim into `ioctl_in_t.request`.
`ioctl_unpackEx` hands that value to posixsrv's `pts_devctl_op`, which passes it to
`_libtty_ioctl`. Host model of the three variants (`/tmp/ioctl-signext.c`, gcc on the dev host):

```
TCSETS=0x805c7402 wire=0xffffffff805c7402
old libtty (unsigned int cmd):  TCSETS
new libtty (unsigned long cmd): default -> -EINVAL
new libtty + kernel mask:       TCSETS
```

`tcgetattr` (TCGETS, `_IOR`, `0x405c7401`, bit 31 clear) is not affected, and neither is any
request passed directly as a macro. That is why xterm gets past `ERROR_TIOCGETP` and fails only
at the first *set*.

## Fix

**phoenix-rtos-kernel `fix/xterm-pty-einval` `f20e96a0`** (based on master `e2f51273`, pushed to
`publish`). At the top of `posix_ioctl()`:

```c
request = (unsigned long)(u32)request;
size = IOCPARM_LEN(request);
```

Why the kernel rather than libtty or libphoenix:

- POSIX declares `int ioctl(int, int request, ...)`, so a request that arrives sign-extended is a
  legitimate encoding, not only a stale-binary artefact. The kernel is the one choke point every
  server sits behind: posixsrv ptys, pl011-tty, lwip, and the kernel's own `log_devctl`, which
  compares `request == TCGETS` (`log/log.c:352`).
- It fixes the binaries already on the NFS root without relinking them. The kernel is fetched over
  TFTP fresh on every netboot, so the Pi check below tests exactly the root that fails today, with
  one variable changed.
- Other architectures: on every 32-bit target the cast does nothing. On 64-bit targets it drops
  only bits that no `_IOC()` encoding sets.
- The fork's own drivers lose nothing either. A grep of the headers in devices, lwip, usb,
  filesystems, posixsrv, `libphoenix/include` and `kernel/include` finds no ioctl request (or
  `ioctl()` call site) with a value wider than 32 bits: no 9+-digit hex literal, and no `<< 32` or
  wider shift, in any ioctl-related define. All fork requests use `_IO*()`. A hand-rolled 32-bit
  literal would be unaffected anyway.

Compile check: `posix/posix.c` from the worktree, compiled with the command recovered from
`make -n -W posix/posix.c` in `.buildroot/phoenix-rtos-kernel` (the `syntax-check.sh` recipe,
pointed at the worktree): **CLEAN** under `-std=gnu17 -Werror`. As a negative control, the same
command on a copy with an unused variable fails with `-Werror=unused-variable`.

**Not done here, recommended:** make the xterm port relink, e.g. `rm -f "${PREFIX_PORT_WORKDIR}/xterm"`
before `make xterm`, or add the sysroot `libphoenix.a` as a prerequisite. More generally, the ports
staleness model should treat libphoenix as an input. After that, a forced xterm rebuild should turn
the census line for xterm from OLD to new.

## Test

**phoenix-rtos-tests `fix/xterm-pty-einval` `c9e6e47`** adds a new group `pty` in
`libc/posixsrv/pty.c`, run by `test-libc-posixsrv`. Setup follows xterm: `open("/dev/ptmx")` →
`grantpt` → `unlockpt` → `ptsname` → `open(slave)`. The test is ignored if there is no
`/dev/ptmx`.

| case | what |
|---|---|
| `tcsetattr_slave` | plain `tcsetattr` TCSANOW/TCSADRAIN/TCSAFLUSH on the slave (regression guard) |
| `tcsets_int_request` | `ioctl(slave, <int>TCSETS, &tio)` with ECHO cleared, then `tcgetattr` checks that it was applied |
| `tiocswinsz_int_request` | `ioctl(<int>TIOCSWINSZ)` on the master and on the slave, then `TIOCGWINSZ` readback |

- The cross-compiled object (real test flags, `-Werror`) loads `x1 = 0xffffffffffff7402` +
  `movk #0x805c, lsl #16` before `bl ioctl`. That is exactly the stale xterm's wire value.
- Expected on the Pi: `tcsets_int_request` and `tiocswinsz_int_request` **FAIL (EINVAL) on the
  current kernel and PASS with `f20e96a0`**. `tcsetattr_slave` passes on both, because the current
  libphoenix is fixed. This follows from §3–§4. It has not been observed on the Pi.
- Host (`host-generic-pc`, Linux): the 3 tests pass (`3 Tests 0 Failures`). Linux truncates the
  request itself, so the host run shows the test is sound, not that the fix works.

## Pre-registered Pi check

Follow the `rpi4-core-change` skill loop. The branch is checked out in the job worktree, so detach
the sibling instead:

1. `git -C sources/phoenix-rtos-kernel checkout --detach f20e96a0`
2. `./scripts/rebuild-rpi4b-fast.sh --scope core`
3. Verify on the boot. The change adds no string, so `strings loader.disk | grep` cannot confirm
   it. The `rpi4-sysinfo: build components` block must show `phoenix-rtos-kernel  f20e96a0…`.
4. Leave the NFS root **unchanged**, so the stale xterm stays. Run the showcase gate's X arm.
5. `git -C sources/phoenix-rtos-kernel checkout master`, or merge the branch if it passes.

- **PASS:** 2 `xterm` windows on HDMI, 0 `fatal pty error` in the UART log, and no early
  `xlaunch: client[2] exited` / `client[5] exited`. In the failing boots these lines follow each
  error within about 6 lines. In the good boot of 2026-09-20 they are absent.
- **FAIL:** any `fatal pty error` line. If one appears, the request reached libtty by another path.
  Check `ioctl_unpackEx`'s request in posixsrv.
- Optionally, run `test-libc-posixsrv -g pty` on the unfixed and the fixed kernel: expect 2
  failures, then 0. This needs the tests sibling at `c9e6e47` for the build.

## Proposed KNOWN-ISSUES row

| ID | Issue | Status |
|---|---|---|
| X-PTY | xterm dies at start with `fatal pty error errno=22, error=23`. A stale static xterm sends TCSETS sign-extended (`0xffffffff805c7402`), and libtty ≥ devices `1657d7d` no longer truncates the command. Since 2026-09-22 (sweep 11). | Fix on kernel `fix/xterm-pty-einval` `f20e96a0` (32-bit request canonicalisation), test on tests `c9e6e47`. Awaiting the Pi gate. Follow-up: make the xterm port relink. |
