#!/usr/bin/env bash
#
# Standalone build of the poll() wake-up test (docs/gpu-new-lane/poll-wake.md):
#   <out>/pollwake   server + client in one static binary
#
# Toolchain gcc against the tree sysroot, as tools/gpu-lane/ipcprobe. Writes only
# into this directory's out/ (or --out). Touches no .buildroot output, no sibling
# repo, no /srv.
#
# pollNotify(): from the sysroot's libphoenix.a when it has it, else from
# pollnotify_shim.S numbered for the kernel tree POLLWAKE_KERNEL (default
# sources/phoenix-rtos-kernel); see pollnotify-obj.sh.
#
# Usage: tools/gpu-lane/pollwake/build.sh [--clean] [--out <dir>]
#        POLLWAKE_KERNEL=<kernel tree> tools/gpu-lane/pollwake/build.sh ...
# Stage (coordinator only):
#   sudo cp tools/gpu-lane/pollwake/out/pollwake <live fsid=0 export>/bin/
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="out"
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

if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

S="${root}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
CC="${TC}-gcc"

for p in "${CC}" "${S}/lib/libphoenix.a"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done

mkdir -p "${out}/obj"
CFLAGS=(-O2 -g -std=gnu11 -Wall -Wextra -Werror -mcpu=cortex-a72 -mtune=cortex-a72
	--sysroot="${S}/" -B"${S}/lib/")

shim="$("${here}/pollnotify-obj.sh" "${out}/obj")"
extra=()
[ -z "${shim}" ] || extra+=("${shim}")

echo "== pollwake"
"${CC}" "${CFLAGS[@]}" -o "${out}/pollwake" "${here}/pollwake.c" "${extra[@]}" -lpthread
ls -l "${out}/pollwake"
echo "build.sh: OK"
