#!/usr/bin/env bash
#
# FRAMEWORK PORT: sources/phoenix-rtos-ports libxshmfence_phoenix + xorg_server_drm USE x11demo (eglx11-demo; shmsrv: wayland),
# opt-in, not in the default image (docs/gpu-new-lane/MIGRATION.md section 4, "Ports (graphics)").
# Every patch/glue file this script uses is also a file of the port; the copies are kept
# identical by scripts/check-gpu-lane-ports-sync.sh -- a change here must be copied there.
#
# x11-drm (new GPU lane, M4 part 2): GL clients inside Xorg-drm through DRI3/Present
# (docs/gpu-new-lane/M4-xorg-modesetting.md, "M4 part 2").
#
#   <out>/dl/                  libxshmfence 1.3.2 (sha256-pinned, the tarball xorg-drm uses)
#   <out>/xshmfence-prefix/    libxshmfence with the Phoenix-RTOS backend (patches/libxshmfence/):
#                              lib/libxshmfence.a, include/X11/xshmfence.h, lib/pkgconfig/xshmfence.pc
#                              -- the SAME archive must be linked into Xorg-drm and every client
#                              (struct xshmfence layout); xorg-drm/build.sh --xshmfence-prefix
#   <out>/eglx11-demo          static GLES2-in-an-X-window client (src/eglx11_demo.c), unstripped
#   <out>/eglx11-demo-stripped stage this;  <out>/BUILD-INFO.txt  inputs + sha256
#   <out>/shmsrv[-stripped]    the /shm server, compiled here from weston-drm/shmsrv/shmsrv.c
#                              (unchanged source) so the M4 part-2 staging is self-contained
#
# Inputs (read-only): the mesa-drm --x11 build (default tools/gpu-lane/mesa-drm/build-out-x11:
# EGL x11 platform + its link list x11-link.txt), the ports prefix (X11/xcb archives), the tree
# sysroot, the toolchain, and shmsrv's wire header (tools/gpu-lane/weston-drm/shmsrv/shm_proto.h:
# xshmfence allocates its fence objects from shmsrv, /shm).
# Writes only into <out> (default build-out/, gitignored). No Pi, no rebuild-rpi4b-fast.sh, no /srv.
#
# Usage: tools/gpu-lane/x11-drm/build.sh [--clean] [--out <dir>] [--mesa-out <dir>] [--xshmfence-only]
#   --xshmfence-only   build only the libxshmfence prefix (what xorg-drm and mesa-drm --x11 need)
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
mesa_out="${root}/tools/gpu-lane/mesa-drm/build-out-x11"
clean=0
shmf_only=0
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--mesa-out) shift; mesa_out="${1:?--mesa-out needs a directory}" ;;
		--xshmfence-only) shmf_only=1 ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
case "${mesa_out}" in /*) ;; *) mesa_out="${PWD}/${mesa_out}" ;; esac
if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

SHMF_VER=1.3.2
SHMF_URL="https://www.x.org/releases/individual/lib/libxshmfence-${SHMF_VER}.tar.xz"
SHMF_SHA=870df257bc40b126d91b5a8f1da6ca8a524555268c50b59c0acd1a27f361606f
B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"      # ports prefix (X11/xcb archives)
S="${B}/sysroot"                                            # tree sysroot (libphoenix)
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
SHMSRV_DIR="${root}/tools/gpu-lane/weston-drm/shmsrv"
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-gcc-ar" "${TC}-nm" "${TC}-strip" "${SHMSRV_DIR}/shm_proto.h" \
		"${B}/include/X11/Xfuncproto.h"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
mkdir -p "${out}/dl"

# --- libxshmfence with the Phoenix-RTOS backend --------------------------------------------
echo "== libxshmfence ${SHMF_VER} + $(ls "${here}"/patches/libxshmfence/*.patch | wc -l) patch(es) (Phoenix backend)"
tb="${out}/dl/libxshmfence-${SHMF_VER}.tar.xz"
if [ ! -f "${tb}" ]; then
	if [ -f "${root}/tools/gpu-lane/xorg-drm/build-out/dl/libxshmfence-${SHMF_VER}.tar.xz" ]; then
		cp "${root}/tools/gpu-lane/xorg-drm/build-out/dl/libxshmfence-${SHMF_VER}.tar.xz" "${tb}"
	else
		curl -sSfL -o "${tb}.part" "${SHMF_URL}"
		mv "${tb}.part" "${tb}"
	fi
fi
echo "${SHMF_SHA}  ${tb}" | sha256sum -c --quiet - || { echo "build.sh: sha256 mismatch for ${tb}" >&2; exit 1; }
ssrc="${out}/src/libxshmfence-${SHMF_VER}"
rm -rf "${ssrc}"
mkdir -p "${out}/src"
tar xJf "${tb}" -C "${out}/src"
for p in "${here}"/patches/libxshmfence/*.patch; do
	echo "  apply $(basename "${p}")"
	patch -s -d "${ssrc}" -p1 < "${p}"
done
SP="${out}/xshmfence-prefix"
rm -rf "${SP}" "${out}/xshmfence-obj"
mkdir -p "${SP}/lib/pkgconfig" "${SP}/include/X11" "${out}/xshmfence-obj"
# No configure: two sources, the backend chosen by HAVE_PHOENIX_FENCE (mkostemp/SHMDIR only
# for the warned /tmp fallback). -I the ports include/ is safe here: libc + X11/Xfuncproto.h only.
for f in xshmfence_alloc xshmfence_phoenix; do
	"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -DHAVE_PHOENIX_FENCE=1 -DHAVE_MKOSTEMP=1 \
		-DSHMDIR='"/tmp"' -I"${ssrc}/src" -I"${SHMSRV_DIR}" -idirafter "${B}/include" \
		-c "${ssrc}/src/${f}.c" -o "${out}/xshmfence-obj/${f}.o"
done
# D = deterministic: the same bytes every build (Xorg-drm and the clients record its sha256).
"${TC}-gcc-ar" rcsD "${SP}/lib/libxshmfence.a" "${out}"/xshmfence-obj/*.o
cp "${ssrc}/src/xshmfence.h" "${SP}/include/X11/"
printf '%s\n' "prefix=${SP}" 'libdir=${prefix}/lib' 'includedir=${prefix}/include' '' \
	'Name: xshmfence' 'Description: X shared memory fences (Phoenix-RTOS backend: polled word in shmsrv memory)' \
	"Version: ${SHMF_VER}" 'Libs: -L${libdir} -lxshmfence' 'Cflags: -I${includedir}' > "${SP}/lib/pkgconfig/xshmfence.pc"
ssyms="$("${TC}-nm" -g --defined-only "${SP}/lib/libxshmfence.a" 2>/dev/null || true)"
for s in xshmfence_alloc_shm xshmfence_map_shm xshmfence_await xshmfence_trigger xshmfence_phoenix_alloc_shm; do
	grep -qE " T ${s}\$" <<< "${ssyms}" || { echo "build.sh: ${s} missing from libxshmfence.a" >&2; exit 1; }
done
if grep -qE ' U pthread_' <(${TC}-nm "${SP}/lib/libxshmfence.a" 2>/dev/null); then
	echo "build.sh: libxshmfence.a still references pthread (wrong backend)" >&2; exit 1
fi
echo "  ${SP}/lib/libxshmfence.a: $(stat -c %s "${SP}/lib/libxshmfence.a") bytes, backend=phoenix (no pthread refs)"
[ "${shmf_only}" = 1 ] && exit 0

# --- shmsrv (the fence objects' backing; same source as weston-drm's) -----------------------
echo "== shmsrv (${SHMSRV_DIR}/shmsrv.c)"
mkdir -p "${out}/obj"
"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -c "${SHMSRV_DIR}/shmsrv.c" -o "${out}/obj/shmsrv.o"
"${TC}-gcc" "${TFLAGS[@]}" -static -Wl,--gc-sections -o "${out}/shmsrv" "${out}/obj/shmsrv.o"
"${TC}-strip" -o "${out}/shmsrv-stripped" "${out}/shmsrv"
echo "  ${out}/shmsrv: $(stat -c %s "${out}/shmsrv") bytes; stripped $(stat -c %s "${out}/shmsrv-stripped") bytes"

# --- eglx11-demo ---------------------------------------------------------------------------
echo "== eglx11-demo (Mesa ${mesa_out})"
[ -f "${mesa_out}/x11-link.txt" ] && [ -f "${mesa_out}/mesa-x11.txt" ] \
	|| { echo "build.sh: ${mesa_out} is not a finished mesa-drm --x11 build (x11-link.txt)" >&2; exit 1; }
LD_PREFIX="${mesa_out}/libdrm-prefix"
XP="${mesa_out}/x11-prefix"
mkdir -p "${out}/obj"
"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter "${TFLAGS[@]}" \
	-I"${mesa_out}/prefix/include" -I"${XP}/include" -c "${here}/src/eglx11_demo.c" -o "${out}/obj/eglx11_demo.o"
# The link list, with the Phoenix-backend xshmfence in place of whatever Mesa configured against
# (the API is opaque: Mesa never touches struct xshmfence).
LINK=()
whole=""
while IFS= read -r l; do
	case "${l}" in
		"--whole-archive "*) whole="${l#--whole-archive }" ;;
		*/libxshmfence.a) LINK+=("${SP}/lib/libxshmfence.a") ;;
		*) LINK+=("${l}") ;;
	esac
done < "${mesa_out}/x11-link.txt"
[ -n "${whole}" ] || { echo "build.sh: no --whole-archive entry in x11-link.txt" >&2; exit 1; }
# -Wl,--wrap=mmap: libdrm-phoenix resolves Mesa's BO tokens (M3 §2.6); -Wl,--wrap=ioctl: the
# in-process sync-file ioctls (M5 §9.3). Both exactly as vkcube/kmscube.
"${root}/tools/gpu-lane/e7-drm-build/bin/phx-g++" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 \
	-Wl,--wrap=mmap -Wl,--wrap=ioctl -Wl,-Map,"${out}/eglx11-demo.map" -o "${out}/eglx11-demo" "${out}/obj/eglx11_demo.o" \
	-Wl,--whole-archive "${whole}" -Wl,--no-whole-archive -Wl,--start-group "${LINK[@]}" -Wl,--end-group -lm \
	> "${out}/eglx11-demo-link.log" 2>&1 || { grep -v ': warning: ' "${out}/eglx11-demo-link.log" | head -60; exit 1; }
"${TC}-strip" -o "${out}/eglx11-demo-stripped" "${out}/eglx11-demo"
echo "  ${out}/eglx11-demo: $(stat -c %s "${out}/eglx11-demo") bytes; stripped $(stat -c %s "${out}/eglx11-demo-stripped") bytes"
[ -s "${out}/eglx11-demo-link.log" ] && sed 's/^/  link: /' "${out}/eglx11-demo-link.log" | sort -u | head -10

# --- verification --------------------------------------------------------------------------
echo "== verify"
"${TC}-size" "${out}/eglx11-demo" | sed 's/^/  /'
und="$("${TC}-nm" -u "${out}/eglx11-demo" || true)"
echo "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && sed 's/^/    /' <<< "${und}" | head -20
bad=0
syms="$("${TC}-nm" "${out}/eglx11-demo")"
for s in __wrap_mmap __wrap_ioctl drmPhoenixMmap drm_phoenix_ioctl dri2_initialize_x11 dri3_x11_connect \
		loader_dri3_swap_buffers_msc x11_dri3_open xcb_dri3_open xcb_dri3_pixmap_from_buffers xcb_present_pixmap \
		xshmfence_alloc_shm xshmfence_phoenix_alloc_shm kmsro_drm_screen_create v3d_drm_screen_create_renderonly; do
	if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then echo "  symbol ${s}: yes"; else echo "  symbol ${s}: NO"; bad=1; fi
done
if grep -qE ' [TtWw] pthread_condattr_setpshared$' <<< "${syms}" && grep -q 'xshmfence_pthread' "${out}/eglx11-demo.map"; then
	echo "  the pthread xshmfence backend got linked" >&2; bad=1
fi
strs="$(strings -a "${out}/eglx11-demo-stripped")"
for s in 'XDEMO ' /dev/dri/card0 /dev/dri/renderD128 /kmsbuf /shm 'libdrm-phoenix:' kmsro 'V3D 4.2' \
		EGL_KHR_platform_x11 EGL_EXT_platform_xcb 'xshmfence: no shmsrv'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  strings '${s}': ${n}"
done
for s in 'v3d-winsys:' phoenix_v3d_ioctl peek_next_scanout v3d-srv; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	[ "${n}" = 0 ] || { echo "  old-lane string '${s}': ${n}"; bad=1; }
done
{
	echo "eglx11-demo build $(date -Is)"
	echo "mesa: ${mesa_out} (patch set $(cat "${mesa_out}/mesa-src.stamp" 2>/dev/null || echo ?))"
	sed 's/^/libdrm-snapshot: /' "${mesa_out}/libdrm-snapshot.txt"
	echo "xshmfence patches: $(ls "${here}"/patches/libxshmfence/)"
	echo "shmsrv source: ${SHMSRV_DIR}/shmsrv.c $(sha256sum "${SHMSRV_DIR}/shmsrv.c" | cut -c1-16)"
	sha256sum "${SP}/lib/libxshmfence.a" "${out}/eglx11-demo" "${out}/eglx11-demo-stripped" "${out}/shmsrv-stripped" \
		| sed "s|${root}/||"
} > "${out}/BUILD-INFO.txt"
sed 's/^/  /' "${out}/BUILD-INFO.txt"
[ "${bad}" = 0 ] || { echo "build.sh: verification failed (see above)" >&2; exit 1; }
echo "done"
