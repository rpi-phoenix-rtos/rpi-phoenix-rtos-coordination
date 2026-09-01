#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Build CPython's _curses as a dlopen-able .so for Phoenix and stage it (plus a
# freshly restaged python3 + stdlib) into the netboot NFS export.
#
# Two Phoenix-specific fixes make this work (both HW-proven 2026-09-01):
#   1. The ncurses port must be built -fPIC (port.def.sh) so its static
#      libncurses.a can be folded into a shared object. Rebuild it first if
#      needed:  ./scripts/build-port.sh ncurses
#   2. pyconfig.h falsely reports HAVE_NCURSESW=1 (bad configure probe, no
#      ncursesw on target). curses_shim.h (force-included) undefs it AFTER
#      Python.h so _cursesmodule.c takes its narrow-build path; -DHAVE_NCURSES_H
#      makes py_curses.h include <ncurses.h>.
#
# The .so leaves the Py C-API + libc undefined (resolved at load against the
# non-stripped python3), per tools/python-port/build-extension.sh.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
CC=${GCC:-$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}
BR=$REPO/.buildroot/_build/aarch64a72-generic-rpi4b
PYSRC=${PYSRC:-/tmp/python-port-build/Python-3.14.4}
NFS=${NFS_ROOT:-/srv/phoenix-rpi4-nfs-gcc16}
PYLIB=$NFS/usr/local/lib/python3.14

[ -f "$PYSRC/Modules/_cursesmodule.c" ] || { echo "ERROR: CPython source at $PYSRC missing"; exit 1; }
[ -f "$BR/lib/libncurses.a" ] || { echo "ERROR: build ncurses port first (./scripts/build-port.sh ncurses)"; exit 1; }

echo "build-curses.sh: compiling _curses.cpython-314.so (narrow ncurses, PIC)"
"$CC" -shared -fPIC -nostartfiles -O2 \
	-include "$HERE/curses_shim.h" \
	-I "$PYSRC/Include" -I "$PYSRC/Include/internal" -I "$PYSRC" \
	-I "$BR/sysroot/include/ncurses" \
	-DPy_BUILD_CORE_MODULE -DHAVE_NCURSES_H -DHAVE_TERM_H \
	"$PYSRC/Modules/_cursesmodule.c" "$BR/lib/libncurses.a" \
	-o "$HERE/_curses.cpython-314.so"
"$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm" -D "$HERE/_curses.cpython-314.so" | grep -q "PyInit__curses" \
	|| { echo "ERROR: PyInit__curses missing"; exit 1; }

echo "build-curses.sh: staging python3 + stdlib + _curses.so -> $NFS"
mkdir -p "$PYLIB" "$NFS/root"
cp "$PYSRC/python" "$NFS/bin/python3"   # NON-stripped: keep the symtab for dlopen
rsync -a --no-owner --no-group --exclude 'test/' --exclude 'tests/' --exclude '__pycache__/' "$PYSRC/Lib/" "$PYLIB/"
cp "$HERE/_curses.cpython-314.so" "$PYLIB/"
cp "$HERE/curses_smoke.py" "$HERE/selftest.py" "$NFS/root/" 2>/dev/null || true

echo "build-curses.sh: done. Test on the Pi:"
echo "  /bin/python3 -S /root/selftest.py     # PYVER + ALL-OK"
echo "  /bin/python3 -S /root/curses_smoke.py  # IMPORT-CURSES-OK + CURSES-PY-PASS"
