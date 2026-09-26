#!/usr/bin/env bash
#
# Standalone build of the new-lane display server (M2 Stage A):
#   <out>/rpi4-kms   the server  (/dev/kms + /kmsbuf)
#   <out>/kmstest    the test tool (KMSTEST lines)
#
# Toolchain gcc against the tree sysroot, as tools/gpu-lane/v3d-async/build.sh.
# Writes only into this directory's out/. Touches no .buildroot output, no
# sibling repo, no /srv. libvcmbox.c is compiled from phoenix-rtos-devices (it is
# not in the sysroot); v3da_proto.h (render-server fence page) is read from
# ../v3d-async. memExport/memUnexport come from the tree sysroot's libphoenix.
#
# Usage: tools/gpu-lane/kms/build.sh [--clean] [--out <dir>]
# Stage (coordinator only):
#   sudo cp tools/gpu-lane/kms/out/{rpi4-kms,kmstest} <live fsid=0 export>/bin/
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${KMS_OUT:-out}"
clean=0
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in
	/*) ;;
	*) out="${here}/${out}" ;;
esac
obj="${out}/obj"

S="${root}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
CC="${TC}-gcc"
NM="${TC}-nm"
VCMBOX="${root}/sources/phoenix-rtos-devices/misc/rpi4-vcmbox"
V3DA="${root}/tools/gpu-lane/v3d-async"

if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

for p in "${CC}" "${S}/usr/include/sys/interrupt.h" "${S}/lib/libphoenix.a" \
		"${VCMBOX}/libvcmbox.c" "${V3DA}/v3da_proto.h"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
grep -q vcmbox_callXL "${VCMBOX}/libvcmbox.h" || { echo "build.sh: libvcmbox has no vcmbox_callXL (needs rpi4-vcmbox 79a4212)" >&2; exit 1; }
syms="$("${NM}" "${S}/lib/libphoenix.a" 2>/dev/null || true)"
grep -qw 'T memExport' <<<"${syms}" || { echo "build.sh: sysroot libphoenix has no memExport (E1 not merged/installed)" >&2; exit 1; }

mkdir -p "${obj}"

# -mno-outline-atomics: the SMI handler runs in kernel context and must not call
# into libgcc's LSE-dispatch helpers. -mstrict-align keeps MMIO accesses whole.
CFLAGS=(-O2 -g -std=gnu11 -Wall -Wextra -Werror -mno-outline-atomics
	-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/"
	-I"${here}" -I"${VCMBOX}" -I"${V3DA}")

compile() {   # compile <src> <obj>
	echo "  CC  $(basename "$1")"
	"${CC}" "${CFLAGS[@]}" -c "$1" -o "$2"
}

echo "== rpi4-kms"
srv_objs=()
for f in kms_main kms_fw kms_backend kms_bo kms_vblank; do
	compile "${here}/${f}.c" "${obj}/${f}.o"
	srv_objs+=("${obj}/${f}.o")
done
compile "${VCMBOX}/libvcmbox.c" "${obj}/libvcmbox.o"
"${CC}" "${CFLAGS[@]}" -Wl,--gc-sections -o "${out}/rpi4-kms" "${srv_objs[@]}" "${obj}/libvcmbox.o"

echo "== kmstest"
compile "${here}/kmstest.c" "${obj}/kmstest.o"
"${CC}" "${CFLAGS[@]}" -Wl,--gc-sections -o "${out}/kmstest" "${obj}/kmstest.o"

ls -l "${out}/rpi4-kms" "${out}/kmstest"
echo "build.sh: OK"
