#!/bin/bash
# Compile ONE v3d mesa-port source under the real build flags, producing no artifact.
#
# WHY THIS EXISTS. `scripts/syntax-check.sh` is the project's answer to "validate a
# core change while a Pi bench is running", because a full build ends in the image
# stage and overwrites the TFTP loader.disk. But it cannot check the v3d mesa-port
# sources at all: they are not built by phoenix-rtos-devices' Makefile, they are
# compiled by mesa/build-v3d-phoenix.py from a HOST Mesa compile_commands.json, and
# the include paths that implies are nowhere in the devices build. Pointed at
# v3d_phoenix_winsys.c, syntax-check.sh dies on the first line:
#
#     fatal error: drm-uapi/v3d_drm.h: No such file or directory
#
# So the one file class where the bench is most often busy -- the GPU driver, during
# a C1 hunt -- was the one class that could not be validated without a build.
#
# ⚠ THE FLAGS ARE THE BUILD'S, NOT A GUESS. This imports build-v3d-phoenix.py and
# calls its own transform() on its own template entry, so if the port's flags change
# this follows them. Reconstructing the flag list by hand would drift silently, and a
# check that compiles under the wrong flags is worse than no check.
#
# Requires the host Mesa build tree the port builds from (/tmp/mesa-v3d-build). If it
# is missing, this says so rather than passing vacuously.
#
# Usage: scripts/syntax-check-v3d.sh <path-to-.c>        (absolute, or repo-relative)
# Exit:  0 = compiles clean, 1 = compile errors, 2 = usage / environment
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="${1:-}"

if [ -z "$src" ]; then
	echo "usage: $(basename "$0") <path-to-.c>" >&2
	exit 2
fi
case "$src" in
	/*) ;;
	*) src="$repo/$src" ;;
esac
if [ ! -f "$src" ]; then
	echo "syntax-check-v3d: no such file: $src" >&2
	exit 2
fi

builder="$repo/sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/build-v3d-phoenix.py"
if [ ! -f "$builder" ]; then
	echo "syntax-check-v3d: builder not found: $builder" >&2
	exit 2
fi

echo "syntax-check-v3d: $(basename "$src")  (real mesa-port flags, no artifact)"

PY_SRC="$src" PY_BUILDER="$builder" python3 - <<'PYEOF'
import importlib.util, os, subprocess, sys

src = os.environ["PY_SRC"]
builder = os.environ["PY_BUILDER"]

spec = importlib.util.spec_from_file_location("bv", builder)
mod = importlib.util.module_from_spec(spec)
saved, sys.argv = sys.argv, ["bv"]   # the module reads sys.argv at import
try:
    spec.loader.exec_module(mod)     # top level only defines constants + functions
finally:
    sys.argv = saved

if not os.path.isdir(mod.HOSTBUILD):
    print(f"syntax-check-v3d: SKIPPED -- host Mesa build tree missing ({mod.HOSTBUILD}).")
    print("                  Nothing was checked; do not read this as a pass.")
    raise SystemExit(2)

cmd = mod.transform(mod.template_entry(), src, "/dev/null")
# -fsyntax-only in place of -c/-o: no object file, so this cannot disturb a build.
cmd = [c for c in cmd if c not in ("-c", "-o", "/dev/null")] + ["-fsyntax-only"]

r = subprocess.run(cmd, cwd=mod.HOSTBUILD, capture_output=True, text=True)
out = (r.stderr or "") + (r.stdout or "")
if out.strip():
    print(out.rstrip())
if r.returncode == 0:
    print("syntax-check-v3d: CLEAN")
else:
    print(f"syntax-check-v3d: FAILED (rc={r.returncode}) -- fix before spending a build on it")
raise SystemExit(0 if r.returncode == 0 else 1)
PYEOF
