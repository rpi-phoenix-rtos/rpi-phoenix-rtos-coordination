#!/usr/bin/env bash
#
# Standalone build of the new-lane V3D server skeleton (M1 part 1):
#   out/rpi4-v3d-async   the server  (/dev/v3d-async)
#   out/v3dasync-ping    the first-contact probe
#   out/libv3da-client.a the client library
#
# Toolchain gcc against the tree sysroot, as tools/serrprobe/README.md. Writes only
# into this directory's out/. Touches no .buildroot output, no sibling repo, no /srv.
# libvcmbox.c is compiled from phoenix-rtos-devices (it is not in the sysroot);
# v3d_drm.h (MIT uapi) comes from the old lane's vendored copy, read-only.
#
# Usage: tools/gpu-lane/v3d-async/build.sh [--clean]
# Stage (coordinator only):
#   sudo cp tools/gpu-lane/v3d-async/out/{rpi4-v3d-async,v3dasync-ping} /srv/phoenix-rpi4-nfs-gcc16/bin/
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/out"
obj="${out}/obj"

S="${root}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
CC="${TC}-gcc"
AR="${TC}-ar"
VCMBOX="${root}/sources/phoenix-rtos-devices/misc/rpi4-vcmbox"
UAPI="${root}/sources/phoenix-rtos-devices/gpu/rpi4-v3d/uapi"

if [ "${1:-}" = "--clean" ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

for p in "${CC}" "${AR}" "${S}/usr/include/sys/interrupt.h" "${S}/lib/libphoenix.a" \
		"${VCMBOX}/libvcmbox.c" "${UAPI}/v3d_drm.h"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done

mkdir -p "${obj}"

# -mno-outline-atomics: the IRQ handler runs in kernel context and must not call
# into libgcc's LSE-dispatch helpers for its __atomic builtins.
CFLAGS=(-O2 -g -std=gnu11 -Wall -Wextra -Werror -mno-outline-atomics
	--sysroot="${S}/" -B"${S}/lib/"
	-I"${here}" -I"${VCMBOX}" -I"${UAPI}")

compile() {   # compile <src> <obj>
	echo "  CC  $(basename "$1")"
	"${CC}" "${CFLAGS[@]}" -c "$1" -o "$2"
}

echo "== libv3da-client.a"
compile "${here}/libv3da-client.c" "${obj}/libv3da-client.o"
rm -f "${out}/libv3da-client.a"
"${AR}" rcs "${out}/libv3da-client.a" "${obj}/libv3da-client.o"

echo "== rpi4-v3d-async"
srv_objs=()
for f in v3da_main v3da_hw v3da_sched v3da_bo v3da_param; do
	compile "${here}/${f}.c" "${obj}/${f}.o"
	srv_objs+=("${obj}/${f}.o")
done
compile "${VCMBOX}/libvcmbox.c" "${obj}/libvcmbox.o"
"${CC}" "${CFLAGS[@]}" -o "${out}/rpi4-v3d-async" "${srv_objs[@]}" "${obj}/libvcmbox.o"

echo "== v3dasync-ping"
compile "${here}/v3dasync-ping.c" "${obj}/v3dasync-ping.o"
"${CC}" "${CFLAGS[@]}" -o "${out}/v3dasync-ping" "${obj}/v3dasync-ping.o" "${out}/libv3da-client.a" -lpthread

ls -l "${out}/rpi4-v3d-async" "${out}/v3dasync-ping" "${out}/libv3da-client.a"
echo "build.sh: OK"
