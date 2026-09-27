# Ports relink when libphoenix.a changes (2026-09-28)

Follow-up to P11 ([2026-09-27-xterm-pty-einval.md](2026-09-27-xterm-pty-einval.md)). The xterm on
the image was a static binary linked on 2026-09-18, because `make xterm` in the kept work
directory found it up to date: libphoenix is not a make prerequisite of any port. No Pi cycle and
no image build were run for this. The evidence below comes from a scratch buildroot.

## Rule

The port build state (`_build/<target>/.port_state/<port>.json`) gets a new key, `link`. It holds
the sha256 of each library the compiler driver links into every port without the port naming it:
the sysroot `libphoenix.a` (which `libc.a`, `libm.a` and `libpthread.a` point to), and the
toolchain `libgcc.a`, `libstdc++.a` and `libsupc++.a`.

- `link` is compared **separately** from the rest of the state. If only `link` changed, the port
  is **relinked**, not cleaned: `INFO: Link inputs changed for <port> (<libs>), relinking`.
- A state file without the key, or no state file at all, also relinks (`(not recorded)`). So the
  first run after the merge relinks every port once. It does not rebuild the whole stage, which
  adding a normal state key would have done.
- The hash is of the file content, not its mtime. The core stage rewrites the sysroot
  `libphoenix.a` on every build, but the archive is deterministic (`ar D`), so an unchanged libc
  triggers nothing.
- The relink runs in `port_prepare.sh`, before extraction. It calls the port's `p_relink` if the
  port defines one. Otherwise it calls `b_port_relink`, which deletes every **target ET_EXEC** ELF
  under `port-sources/<port>-<ver>/`, so the port's own make, ninja, cmake or hand link rebuilds
  it from the objects it already has. Objects, archives, `.so` files, host helpers and CMake probe
  programs are not touched. A port whose build cannot recreate a deleted program can fall back to
  a full rebuild with `p_relink() { rm -rf "${PREFIX_PORT_BUILD}"; }`.
- xterm defines `p_relink() { rm -f "${PREFIX_PORT_WORKDIR}/xterm"; }` as the explicit first
  client. A read of all 72 recipes found none where the default is unsafe, so no other port needs
  a hook.

| repo | branch | commits |
|---|---|---|
| phoenix-rtos-build | `feat/port-relink-on-libc` | `c880865` port_manager (state + relink set + 6 tests, 66/66 pass), `73ffe0e` shell relink step |
| phoenix-rtos-ports | `feat/port-relink-on-libc` | `faa81fb` xterm `p_relink` |

**Not covered:**
- libphoenix **header** changes still invalidate nothing. `b_port_invalidate_stale_configure`
  re-runs configure only when the exported symbol list changes.
- Hand-built binaries outside the ports framework (`tools/*`, `build-diag-tool.sh`) are not
  covered either.

## Scratch-build evidence (xterm plus its dependency xorg_libs)

The scratch buildroot is `make-scratch-buildroot.sh`, with `phoenix-rtos-build` re-pointed at the
branch worktree and `RPI4B_PORTS_DIR` set to the ports worktree. Every run used
`build-port.sh --incremental xterm`.

| run | scratch `libphoenix.a` | log | `main.o` mtime | work-tree `xterm` mtime |
|---|---|---|---|---|
| 2: no state recorded yet | `94a3e1e6…` | `relinking (not recorded)`; xterm `running p_relink`; xorg_libs `removed 3 linked program(s)` | 01:28:16 (unchanged) | 01:29:28 (relinked) |
| 3: unchanged | `94a3e1e6…` | no relink line, no `Build state changed` | 01:28:16 | 01:29:28 (**not** relinked) |
| 4: `ar rD` of an empty `.o` | `ac65ca2e…` | `Link inputs changed … (libphoenix.a), relinking`; `running p_relink`; plink line; no `Build state changed`, no `CLEAN:` | 01:28:16 (no recompile) | 01:30:54 (relinked); state `link` updated to `ac65ca2e…` |

Each run took about 35 s, xorg_libs included. The scratch xterm has the **new** `tcsetattr`. The
live `.buildroot/.../prog/xterm` still has the old one.

## Stale-binary census

The tool is `scripts/check-tcsetattr-abi.py`. It matches raw instruction bytes, so stripped
binaries are classified as well. The P11 objdump-by-symbol method found 15 stale binaries on the
export. The byte census finds **18**. The extra three are stripped port binaries: picocom, openssl
and wpa_supplicant, which are also stale in the live `prog/`.

| binary on the export | origin | covered? |
|---|---|---|
| bin/xterm | port `xterm` | yes, `p_relink` |
| bin/picocom | port `picocom` | yes, default |
| usr/bin/openssl | port `openssl111` | yes, default (`make all` + `install_sw`) |
| usr/bin/wpa_supplicant | port `wpa_supplicant` | yes, default (`make install`) |
| bin/fileperf, bin/udprtt | `scripts/build-diag-tool.sh` (tools/fileperf, tools/udprtt) | no, hand-built |
| bin/boshare-probe, bin/memprobe | tools/boshare-probe, tools/memprobe | no, hand-built |
| bin/gl-bo-import, bin/gl-fbo-orient | tools/v3d-driver-port/gl_bo_import.c, gl_fbo_orientation.c | no, hand-built |
| bin/hevc-play | tools/hevc-decode/build-hevc-play.sh (also staged in `_fs/root`) | no, hand-built |
| bin/xresizer | tools/x11-port/build-xresizer.sh | no, hand-built |
| bin/rpi4-ipcprobe | devices `misc/rpi4-ipcprobe`, commented out of `DEFAULT_COMPONENTS` | no, not built |
| bin/cxxprobe, cxxprobebr, cxxprobeold, dfprobe, dfprobe2 | ad hoc session builds, no source in tree | no |

**Side effects of the first sweep (not breakage):**
- redis `deps/lua/src/{lua,luac}` and xorg_fonts' in-tree `fc-*`/`xmlwf` tools are deleted and
  not rebuilt, because their builds are guarded on other files. Neither is shipped.
- coreutils runs `make -k … || true` and then a glob copy, so a tool that fails to relink is
  dropped silently. Its only backstop is the `n>=100` check. The recipe's own mtime relink already
  carries this risk.

The four port binaries that ship still contain the old libc after this change. The kernel fix
`f20e96a0` stops the pty EINVAL for them, but they need the relink to pick up libphoenix fixes in
general. The now-redundant per-port mtime relinks (`sed:67`, `tar:78`, `coreutils:78`, `gzip:95`)
are left in place. They are harmless.

## Cost on the next ports stage

There is one relink of every target executable in every port, and no recompile, except where a
recipe always recompiles anyway. Every `p_build` already runs on each ports stage. Expect minutes,
not hours. The biggest links are python, STK and the Quakes, which always link anyway. After that,
each real libphoenix change costs one relink sweep. A no-op core rebuild costs nothing.

## Pre-registered check

Run it after merging both branches into `master`. The ports stage must run: `--scope core` alone
does **not** include `ports`. So use `./scripts/rebuild-rpi4b-fast.sh --scope core --with-ports`,
without `--skip-prepare`, because the buildroot's `phoenix-rtos-build` copy must be refreshed.

1. `scripts/check-tcsetattr-abi.py .buildroot/_build/aarch64a72-generic-rpi4b prog prog.stripped`
   gives **OLD (0)**. Today it gives 7: openssl, picocom, wpa_supplicant and xterm.
2. `aarch64-phoenix-objdump -d --disassemble=tcsetattr .buildroot/_build/aarch64a72-generic-rpi4b/prog/xterm`
   shows no `sxtw` instruction (`mov x0,#0x7402` form).
3. The ports log has `Link inputs changed for … (not recorded), relinking` for every port that
   has a state file, `running p_relink` for xterm, and **no** `Build state changed` (unless a
   recipe was really edited).
4. After the usual export sync, run `scripts/check-tcsetattr-abi.py /srv/phoenix-rpi4-nfs-gcc16`.
   xterm, picocom, openssl and wpa_supplicant must be **absent** from OLD, and every remaining
   OLD name must appear among the hand-built rows above (14 today). The export is maintained by
   hand, so its exact count can drift for unrelated reasons.
5. A second `--scope core --with-ports` with no libphoenix change prints **no** `Link inputs
   changed` line.
