#!/usr/bin/env bash
#
# Host test of libxshmfence's Phoenix-RTOS backend (x11-drm patch 0001): the
# upstream libxshmfence 1.3.2 tarball + the patch, native gcc with ASan/UBSan,
# the fence shared by forked processes through an fd passed over AF_UNIX.
# Seconds, no Pi. Writes only <out>/hosttest (default build-out/).
#
# Usage: tools/gpu-lane/x11-drm/hosttest/run.sh
#
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
x11drm="$(cd "${here}/.." && pwd)"
out="${X11DRM_OUT:-${x11drm}/build-out}/hosttest"
root="$(cd "${x11drm}/../../.." && pwd)"
tarball="${x11drm}/build-out/dl/libxshmfence-1.3.2.tar.xz"
[ -f "${tarball}" ] || tarball="${root}/tools/gpu-lane/xorg-drm/build-out/dl/libxshmfence-1.3.2.tar.xz"
[ -f "${tarball}" ] || { echo "hosttest: no libxshmfence-1.3.2.tar.xz (run tools/gpu-lane/x11-drm/build.sh first)" >&2; exit 1; }

rm -rf "${out}"
mkdir -p "${out}"
tar xJf "${tarball}" -C "${out}"
src="${out}/libxshmfence-1.3.2"
for p in "${x11drm}"/patches/libxshmfence/*.patch; do
	patch -s -d "${src}" -p1 < "${p}"
done
# X11/Xfuncproto.h for xshmfence.h: a two-line stand-in (the host may have no X headers).
mkdir -p "${out}/inc/X11"
printf '#define _X_EXPORT\n' > "${out}/inc/X11/Xfuncproto.h"
gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
	-D_GNU_SOURCE -DHAVE_PHOENIX_FENCE=1 -DHAVE_MKOSTEMP=1 -DSHMDIR='"/tmp"' -I"${out}/inc" -I"${src}/src" \
	-o "${out}/xshmfence_host" "${here}/xshmfence_host.c" "${src}/src/xshmfence_phoenix.c" "${src}/src/xshmfence_alloc.c"
"${out}/xshmfence_host"
