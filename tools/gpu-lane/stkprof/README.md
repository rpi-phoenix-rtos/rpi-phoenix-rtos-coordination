# stkprof — a cloned SuperTuxKart with a profiled V3D submit path (experiment E2)

`supertuxkart-prof` is SuperTuxKart linked against a copy of `libv3d-phoenix.a` whose in-process
winsys (`sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c`) is compiled with
`-DV3D_PHX_SUBMIT_PROFILE`. It prints, every flipstat window (5 s), where the frame went: GPU spins
(bin / render / TFU), cache and TLB maintenance around each job, BO ioctls, the flip, and the CPU
time outside the winsys. Design and pre-registered analysis:
[`docs/gpu-new-lane/E2-stk-submit-breakdown.md`](../../../docs/gpu-new-lane/E2-stk-submit-breakdown.md).

**The shipped `/usr/bin/supertuxkart` is not touched.** The instrumentation is compiled out by
default (`V3D_SP()` expands to nothing), the build script proves the default object is
byte-identical to the pristine one, and the prof binary gets a different name. C1 is
layout-sensitive, so never stage this binary under the shipped name, and never count its runs in a
C1 rate.

## Build

```
tools/gpu-lane/stkprof/build-stkprof.sh            # ~10 s: compile, archive copy, 2 links, launcher
tools/gpu-lane/stkprof/build-stkprof.sh --verify-only   # only the default-build identity proof
```

Needs an existing buildroot where the `supertuxkart` port has been built (it reuses that port's
`link.txt`, objects and SDL2 glue objects), `tools/.gpu-libs/{libv3d,libGL}-phoenix.a`, and the
host Mesa tree `/tmp/mesa-v3d-build`. It **writes only** under `$STKPROF_OUT` (default
`artifacts/stkprof/`, gitignored) and fails if any guarded shared file changed while it ran
(`tools/.gpu-libs/libv3d-phoenix.a`, the shipped binaries, the port's `link.txt`, the
`build-v3d-phoenix.py` INCR object cache). It does not run a Pi cycle, stage anything, or touch the
TFTP loader — safe while a bench is running.

What it proves on every run:

| check | meaning |
|---|---|
| PROOF 1 | working-tree winsys compiled **without** the macro == pristine `HEAD` compile (byte-identical) → the default build is unchanged |
| PROOF 1b | that object == the `v3d_phoenix_winsys.o` member inside the shipped `libv3d-phoenix.a` (warning if not: the archive is older than the source) |
| PROOF 2 | a control relink with the unmodified archive == shipped `prog/supertuxkart` (byte-identical) → the prof binary differs from shipped by the one object only (warning if not: the port tree moved on) |
| marker | `SUBMIT PROFILE build` is in the prof ELF and absent from the shipped `usr/bin/supertuxkart` |

Outputs:

| file | stage as |
|---|---|
| `artifacts/stkprof/supertuxkart-prof.stripped` | `/usr/bin/supertuxkart-prof` |
| `artifacts/stkprof/stk-prof` | `/bin/stk-prof` — `stk-launcher.c` with only the exec path and banner rewritten (same env, same seeded `config.xml`, incl. `scale_rtts_factor=0.75`) |
| `artifacts/stkprof/supertuxkart-prof` | not staged; unstripped, for `addr2line` |
| `artifacts/stkprof/BUILD-INFO.txt` | input SHAs (libphoenix, archives, link.txt) — a libc or archive drift between the prof and shipped builds confounds any fps comparison |

Re-run the script after any rebuild of the GPU libs or the STK port, otherwise PROOF 1b / 2 warn
and the prof binary is no longer "shipped + instrumentation".

## Stage (coordinator only)

```
export_dir="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports /etc/exports.d/*.exports)"
install -m 755 artifacts/stkprof/supertuxkart-prof.stripped "$export_dir/usr/bin/supertuxkart-prof"
install -m 755 artifacts/stkprof/stk-prof "$export_dir/bin/stk-prof"
strings -a "$export_dir/usr/bin/supertuxkart-prof" | grep -c 'SUBMIT PROFILE build'   # must be >= 1
strings -a "$export_dir/usr/bin/supertuxkart"      | grep -c 'SUBMIT PROFILE build'   # must be 0
```

(The live export is the `fsid=0` one — `-gcc16` today; never a hard-coded `/srv/phoenix-rpi4-nfs`.)
`sync-netboot-tree.sh` is additive, so both files survive a normal cycle; `SYNC_DELETE=1` or
`make-pristine-nfs-export.sh` removes them — restage afterwards. The prof archive is **not** in
`tools/.gpu-libs`, so staging it does not change the driver fingerprint and does **not** clear the
Mesa shader cache.

## Run

At the psh prompt (same arguments as the C1 benches and the showcase gate):

```
stk-prof --track=hacienda --numkarts=4 --profile-laps=2
```

e.g. `./scripts/test-cycle-psh-interact.sh --label e2prof-warm1 --idle-secs 60 --max-cmd-secs 420 -- 'stk-prof --track=hacienda --numkarts=4 --profile-laps=2'`
with a Bash `timeout` of 600000. Assert the arm from the log, not from the command: the log must
contain `stk-prof: DATADIR=` and `v3d-winsys: SUBMIT PROFILE build (V3D_PHX_SUBMIT_PROFILE) cntfrq=…`.

**Shader cache — both states matter.** The cache is keyed by shader source only and lives at the
cwd-relative `/.mesa-shader-cache`, so `stk-prof` shares it with the shipped `stk` (same Mesa
compiler; only the winsys differs).
* **warm** (steady state, the main arm): the cycle's `sync-netboot-tree.sh` line says
  `Mesa shader disk cache KEPT`.
* **cold**: `sudo rm -rf "$export_dir/.mesa-shader-cache"` before the cycle; the first run then
  compiles every shader. ⚠ A cold cache also makes the next shipped-`stk` C1 trial cold, which
  changes the C1 fire rate — schedule cold E2 runs where that does not bias the C1 series.

Optional knobs, set with psh `export` before the command (psh has `export`; it reaches children):
* `export V3D_SUBPROF_FRAMES=N` / `export V3D_SUBPROF_FRAMES_FROM=S` — one `subprof-f` line per
  frame for frames S..S+N-1 (`per=` frame period, `gpu=` GPU spin, µs). ~1 ms of UART per line;
  it is booked to `rep`, but keep N small.
* `V3D_FLIPSTAT_MS` changes the window length (the subprof lines follow it). `V3D_FLIPSTAT=0`
  disables flipstat **and** the subprof report.

## Read the result

```
python3 tools/gpu-lane/stkprof/e2-summarize.py artifacts/rpi4b-uart/<log>
```

Prints a per-window table, the gameplay-window aggregate (G GPU spins / M maintenance / C CPU /
B BO ioctls / P flip+lock / R instrument), the self-checks (CL phase-sum vs ioctl, counter vs
`clock_gettime` wall, instrument share), the three upper-bound models and the pre-registered
verdict. Output line formats:

```
v3d-winsys: subprof-a  t=<ms> fr= wall= cg= hz= ioc= lock= flip= rep= cpu= gap=<sum>/<n> gapmax= fmin= fmax= gpumax=
v3d-winsys: subprof-cl t=<ms> fr= n= ioc= sum= pre= tlb= l2t= fixa= bin= hand= rend= post= oom= wedge= l2tto= tlbto=
v3d-winsys: subprof-x  t=<ms> fr= tfu=<n>/<us>[pre spin diag post] csd=<n>/<us>[pre spin post] create= close= mmap= unl= other=
```

All times are µs summed over the window; divide by `fr` for per-frame figures. `cpu` is computed
(`wall − ioc − lock − flip − rep`), `cg` is flipstat's `clock_gettime` window for the same period.
