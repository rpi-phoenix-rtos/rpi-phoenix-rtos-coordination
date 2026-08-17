#!/usr/bin/env bash
# Build a CPython C extension as a dlopen-able .so for Phoenix-RTOS / aarch64.
#
# Phoenix has Phase-A dlopen (libphoenix dl/): a -shared -fPIC plugin's undefined
# symbols resolve at load time against the HOST executable's .symtab. So a Python
# extension can leave the Py C-API (and libc) UNDEFINED and have them resolved
# against the (non-stripped) python3 binary. HW-verified: `import spam` -> dlopen
# -> spam.add(3,4)==7.
#
# REQUIREMENTS:
#  - the python3 binary must be linked NON-STRIPPED (keep .symtab with Py* symbols).
#  - the extension must NOT link libpython or libc (leave them undefined -> resolve
#    to the host; linking them would embed a 2nd copy -> state/malloc corruption).
#  - -nostartfiles: drop crtbeginS/crtendS, whose __register_frame_info/
#    __deregister_frame_info EH-frame symbols aren't in the host .symtab.
#  - avoid __thread in the extension (Phase-A has no dynamic TLS; that's Phase B).
#  - name the output <module>.cpython-314.so and put it on sys.path.
set -euo pipefail
REPO=/home/houp/phoenix-rpi
GCC=${GCC:-$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}
PYSRC=${PYSRC:-/tmp/python-port-build/Python-3.14.4}   # CPython source tree (headers)
SRC=${1:?usage: build-extension.sh <module.c> <modname>}
MOD=${2:?usage: build-extension.sh <module.c> <modname>}
"$GCC" -shared -fPIC -nostartfiles -O2 \
	-I "$PYSRC/Include" -I "$PYSRC/Include/internal" -I "$PYSRC" \
	"$SRC" -o "${MOD}.cpython-314.so"
"$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm" "${MOD}.cpython-314.so" | grep -E "PyInit_${MOD}" || true
echo "OK -> ${MOD}.cpython-314.so  (stage on the Pi's sys.path; import ${MOD})"
