#!/usr/bin/env bash
#
# Where does a standalone tool get pollNotify() from?
#
# Prints nothing when the tree sysroot's libphoenix.a already has pollNotify (a
# core build of the gpu-lane/poll-wake kernel + libphoenix installed it).
# Otherwise assembles pollnotify_shim.S into <obj_dir>/pollnotify_shim.o, numbered
# from the syscall table of POLLWAKE_KERNEL (default sources/phoenix-rtos-kernel,
# the kernel an image build compiles), and prints that path for the link line.
# Refuses when that table minus pollNotify is not exactly the sysroot's table:
# the binary's other syscalls, taken from the sysroot libphoenix.a, would then be
# numbered for a different kernel.
#
# Usage: pollnotify-obj.sh <obj_dir>     (messages on stderr, the path on stdout)
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
objdir="${1:?usage: pollnotify-obj.sh <obj_dir>}"

S="${root}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
KTREE="${POLLWAKE_KERNEL:-${root}/sources/phoenix-rtos-kernel}"

# grep -c, not -q: under pipefail an early-exiting grep -q SIGPIPEs nm and the test reads false.
if "${TC}-nm" "${S}/lib/libphoenix.a" 2>/dev/null | grep -cw 'T pollNotify' >/dev/null; then
	echo "pollnotify-obj: pollNotify from the sysroot libphoenix.a" >&2
	exit 0
fi

khdr="${KTREE}/include/syscalls.h"
shdr="${S}/usr/include/phoenix/syscalls.h"
[ -f "${khdr}" ] || { echo "pollnotify-obj: no ${khdr}" >&2; exit 1; }
[ -f "${shdr}" ] || { echo "pollnotify-obj: no ${shdr}" >&2; exit 1; }

ids() { grep -oE 'ID\([A-Za-z0-9_]+\)' "$1" | sed -e 's/ID(\(.*\))/\1/'; }
kids="$(ids "${khdr}")"
grep -qx pollNotify <<<"${kids}" || {
	echo "pollnotify-obj: ${khdr} has no pollNotify -- set POLLWAKE_KERNEL to a gpu-lane/poll-wake kernel tree" >&2
	exit 1
}
if [ "$(grep -vx pollNotify <<<"${kids}")" != "$(ids "${shdr}")" ]; then
	echo "pollnotify-obj: ${khdr} minus pollNotify differs from the sysroot's syscall table;" >&2
	echo "  the shim would misnumber the binary. Rebuild the core first." >&2
	exit 1
fi

mkdir -p "${objdir}"
"${TC}-gcc" -mcpu=cortex-a72 -DPW_SYSCALLS_H="\"${khdr}\"" -c "${here}/pollnotify_shim.S" -o "${objdir}/pollnotify_shim.o"
echo "pollnotify-obj: shim numbered from ${khdr} (the sysroot libphoenix.a predates pollNotify)" >&2
echo "${objdir}/pollnotify_shim.o"
