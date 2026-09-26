#!/usr/bin/env bash
#
# Standalone build of the new-lane V3D server skeleton (M1 part 1):
#   <out>/rpi4-v3d-async   the server  (/dev/v3d-async)
#   <out>/v3dasync-ping    the probe (part 1 tests + part 2 cl/tfu/csd smoke tests)
#   <out>/libv3da-client.a the client library
#   <out>/obj/v3da_winsys.o + obj/libv3da-client.o
#                          the Mesa adapter objects that build-quakespasm-v3da.sh
#                          swaps into a COPY of libv3d-phoenix.a
#
# Toolchain gcc against the tree sysroot, as tools/serrprobe/README.md. Writes only
# into this directory's out/. Touches no .buildroot output, no sibling repo, no /srv.
# libvcmbox.c is compiled from phoenix-rtos-devices (it is not in the sysroot);
# v3d_drm.h (MIT uapi) comes from the old lane's vendored copy, read-only.
#
# Usage: tools/gpu-lane/v3d-async/build.sh [--clean] [--out <dir>]
#   Output directory: --out <dir>, else $V3DA_OUT, else out/ (relative paths are
#   taken relative to this directory). M1 part 2 builds into out-p2/ while a
#   queued Pi cycle stages the part-1 binaries from out/.
# Stage (coordinator only):
#   sudo cp tools/gpu-lane/v3d-async/out/{rpi4-v3d-async,v3dasync-ping} /srv/phoenix-rpi4-nfs-gcc16/bin/
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${V3DA_OUT:-out}"
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
AR="${TC}-ar"
VCMBOX="${root}/sources/phoenix-rtos-devices/misc/rpi4-vcmbox"
UAPI="${root}/sources/phoenix-rtos-devices/gpu/rpi4-v3d/uapi"

if [ "${clean}" = 1 ]; then
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
# -mcpu/-mstrict-align/-ffunction-sections: the project's own target flags (the
# adapter objects end up inside a game link that uses them; -mstrict-align also
# keeps the compiler from merging MMIO accesses into unaligned ones).
CFLAGS=(-O2 -g -std=gnu11 -Wall -Wextra -Werror -mno-outline-atomics
	-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/"
	-I"${here}" -I"${VCMBOX}" -I"${UAPI}")

compile() {   # compile <src> <obj>
	echo "  CC  $(basename "$1")"
	"${CC}" "${CFLAGS[@]}" -c "$1" -o "$2"
}

echo "== libv3da-client.a + the Mesa adapter object"
compile "${here}/libv3da-client.c" "${obj}/libv3da-client.o"
rm -f "${out}/libv3da-client.a"
"${AR}" rcs "${out}/libv3da-client.a" "${obj}/libv3da-client.o"
compile "${here}/v3da_winsys.c" "${obj}/v3da_winsys.o"

echo "== rpi4-v3d-async"
srv_objs=()
for f in v3da_main v3da_hw v3da_sched v3da_jobs v3da_bo v3da_param; do
	compile "${here}/${f}.c" "${obj}/${f}.o"
	srv_objs+=("${obj}/${f}.o")
done
compile "${VCMBOX}/libvcmbox.c" "${obj}/libvcmbox.o"
"${CC}" "${CFLAGS[@]}" -o "${out}/rpi4-v3d-async" "${srv_objs[@]}" "${obj}/libvcmbox.o"

echo "== v3dasync-ping"
compile "${here}/v3dasync-ping.c" "${obj}/v3dasync-ping.o"
ping_objs=("${obj}/v3dasync-ping.o")
if [ -e "${here}/v3da_clgen.c" ]; then
	compile "${here}/v3da_clgen.c" "${obj}/v3da_clgen.o"
	ping_objs+=("${obj}/v3da_clgen.o")
fi
"${CC}" "${CFLAGS[@]}" -o "${out}/v3dasync-ping" "${ping_objs[@]}" "${out}/libv3da-client.a" -lpthread

ls -l "${out}/rpi4-v3d-async" "${out}/v3dasync-ping" "${out}/libv3da-client.a"
echo "build.sh: OK"
