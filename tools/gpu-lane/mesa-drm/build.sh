#!/usr/bin/env bash
#
# mesa-drm (new GPU lane, M3 part 3): upstream Mesa 26.2.0 on the DRM path
# (gallium v3d + vc4/kmsro, GBM with the dri backend linked in, EGL drm +
# surfaceless, GLES2/3), cross-built STATIC for aarch64-phoenix against
# libdrm-phoenix, plus upstream kmscube linked against it.
#
#   <out>/mesa-src/        `git clone -s` of external/mesa at mesa-26.2.0 + patches/mesa/*.patch
#                          (external/mesa, the old lane's fork checkout, is never touched)
#   <out>/mesa-build/      the meson build directory
#   <out>/prefix/          `ninja install`: EGL/GLES/KHR/gbm headers, libEGL.a, libgbm.a, ...
#   <out>/libdrm-prefix/   a SNAPSHOT of libdrm-phoenix's prefix (libdrm.a + headers) taken at
#                          build time, with its own libdrm.pc (see --libdrm-prefix)
#   <out>/compat/          libmesadrm-compat.a (compat/mesadrm_compat.c)
#   <out>/kmscube-src/     upstream kmscube (MIT), cloned once
#   <out>/kmscube          static, unstripped (addr2line); <out>/kmscube-stripped (stage this)
#   <out>/mesa-drm-full.patch  all Mesa patches as one diff against mesa-26.2.0
#
# Writes only into <out> (default: build-out/, gitignored). Reads the tree sysroot
# (.buildroot/_build/aarch64a72-generic-rpi4b/sysroot), the ports prefix (zlib), the
# toolchain, the E7 compiler wrappers (drop -pthread) and, read-only, libdrm-phoenix's
# build-out/prefix. No Pi, no rebuild-rpi4b-fast.sh, no /srv.
#
# Usage: tools/gpu-lane/mesa-drm/build.sh [--clean] [--out <dir>] [--relink] [-j N]
#                                         [--libdrm-prefix <dir>] [--opengl]
#   --relink   skip Mesa; re-snapshot libdrm-phoenix and relink kmscube only (every
#              library-side libdrm-phoenix fix needs this: kmscube embeds libdrm.a)
#   --opengl   ALSO build desktop OpenGL (-Dopengl=true; GLES2 stays on) and the static
#              GL entry-point archive src/mesa/glapi/glapi/libglapi_bridge.a (the gl*
#              symbols libGL would export; not built by default with glx=disabled).
#              Use it with --out <another dir>: the default build-out/ stays GLES-only.
#              Consumer: tools/gpu-lane/sdl2-drm (quakespasm-drm needs desktop GL).
#   --vulkan   build the v3dv Vulkan driver INSTEAD of GL (-Dvulkan-drivers=broadcom,
#              no gallium/EGL/GBM/GLES) into its OWN directory (default build-out-vulkan/,
#              --out still wins). The ICD becomes a static archive (patch 0010):
#              <out>/prefix/lib/libvulkan_broadcom.a exports vk_icdGetInstanceProcAddr,
#              which a program calls in place of a Vulkan loader (there is no loader
#              dlopen on Phoenix); <out>/prefix/include/vulkan = Mesa's Vulkan headers;
#              <out>/vulkan-link.txt = the archives to link, in order. No kmscube.
#              Consumer: tools/gpu-lane/vulkan-drm (vkcube on VK_KHR_display).
#   --wayland  ALSO build the EGL Wayland platform (-Dplatforms=wayland: wayland-egl
#              clients, EGL_EXT_image_dma_buf_import for compositors), GLES only, into
#              its OWN directory (default build-out-wayland/, --out still wins). Needs a
#              target libwayland + wayland-protocols: --wayland-pkgconfig <dirs> is the
#              PKG_CONFIG_LIBDIR list that finds them (default: the weston-drm build's).
#              No kmscube; <out>/egl-link.txt = the archives to link, in order (link
#              libwayland-client/-server after them). Consumer: tools/gpu-lane/weston-drm.
#   --x11      ALSO build the EGL X11 platform with DRI3/Present (-Dplatforms=x11, GLX off:
#              EGL_PLATFORM_X11/XCB clients inside Xorg-drm), GLES only, into its OWN
#              directory (default build-out-x11/, --out still wins). The X11/xcb libraries
#              come from the ports prefix through a private prefix (<out>/x11-prefix: only
#              X11/ and xcb/ headers -- the ports include/ also holds the old lane's GL/);
#              xshmfence from --x11-xshmfence <prefix> (default: the M4 part-2 xorg-drm
#              deps, see tools/gpu-lane/x11-drm). No kmscube; <out>/x11-link.txt = the
#              archives to link, in order (Mesa, xcb, xshmfence, libdrm). Consumer:
#              tools/gpu-lane/x11-drm (docs/gpu-new-lane/M4-xorg-modesetting.md, part 2).
# Stage (coordinator only):
#   sudo cp tools/gpu-lane/mesa-drm/build-out/kmscube-stripped <live NFS export>/bin/kmscube
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
clean=0
relink=0
jobs="$(nproc)"
libdrm_src_prefix="${root}/tools/gpu-lane/libdrm-phoenix/build-out/prefix"
opengl=false
vulkan=false
wayland=false
wayland_pc=""
x11=false
x11_shmf=""
out_given=0
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--relink) relink=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}"; out_given=1 ;;
		--out=*) out="${1#--out=}"; out_given=1 ;;
		--libdrm-prefix) shift; libdrm_src_prefix="${1:?--libdrm-prefix needs a directory}" ;;
		--libdrm-prefix=*) libdrm_src_prefix="${1#--libdrm-prefix=}" ;;
		--opengl) opengl=true ;;
		--vulkan) vulkan=true ;;
		--wayland) wayland=true ;;
		--wayland-pkgconfig) shift; wayland_pc="${1:?--wayland-pkgconfig needs a path list}" ;;
		--x11) x11=true ;;
		--x11-xshmfence) shift; x11_shmf="${1:?--x11-xshmfence needs a prefix}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
if [ "${vulkan}" = true ]; then
	[ "${opengl}" = false ] || { echo "build.sh: --vulkan and --opengl are separate builds (use two --out dirs)" >&2; exit 2; }
	[ "${relink}" = 0 ] || { echo "build.sh: --relink relinks kmscube, which a --vulkan build has not" >&2; exit 2; }
	[ "${out_given}" = 1 ] || out="${here}/build-out-vulkan"
fi
if [ "${wayland}" = true ]; then
	[ "${vulkan}" = false ] && [ "${opengl}" = false ] || { echo "build.sh: --wayland is a GLES build of its own (no --vulkan/--opengl)" >&2; exit 2; }
	[ "${relink}" = 0 ] || { echo "build.sh: --relink relinks kmscube, which a --wayland build has not" >&2; exit 2; }
	[ "${out_given}" = 1 ] || out="${here}/build-out-wayland"
	if [ -z "${wayland_pc}" ]; then
		wo="${root}/tools/gpu-lane/weston-drm/build-out"
		wayland_pc="${wo}/prefix/lib/pkgconfig:${wo}/prefix/share/pkgconfig:${wo}/deps/libffi/lib/pkgconfig"
	fi
fi
x11_pc=""
if [ "${x11}" = true ]; then
	[ "${vulkan}" = false ] && [ "${opengl}" = false ] && [ "${wayland}" = false ] \
		|| { echo "build.sh: --x11 is a GLES build of its own (no --vulkan/--opengl/--wayland)" >&2; exit 2; }
	[ "${relink}" = 0 ] || { echo "build.sh: --relink relinks kmscube, which an --x11 build has not" >&2; exit 2; }
	[ "${out_given}" = 1 ] || out="${here}/build-out-x11"
	if [ -z "${x11_shmf}" ]; then
		# xshmfence's API is opaque (struct xshmfence is never dereferenced by Mesa), so any
		# build configures Mesa; the program links the Phoenix-backend archive (x11-drm).
		x11_shmf="${root}/tools/gpu-lane/x11-drm/build-out/xshmfence-prefix"
		[ -f "${x11_shmf}/lib/pkgconfig/xshmfence.pc" ] || x11_shmf="${root}/tools/gpu-lane/xorg-drm/build-out/deps-prefix"
	fi
	case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
	x11_pc="${out}/x11-prefix/lib/pkgconfig"
fi
case "${out}" in
	/*) ;;
	*) out="${PWD}/${out}" ;;
esac

MESA_TAG=mesa-26.2.0
MESA_COMMIT=9f0a761020b      # "VERSION: bump for 26.2.0" -- the base of the old lane's fork
KMSCUBE_URL=https://gitlab.freedesktop.org/mesa/kmscube.git
KMSCUBE_COMMIT=f60e50e887d3c49e91ac9b06d8199b36152632fa
UPSTREAM="${root}/external/mesa"
B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCC="${root}/tools/gpu-lane/e7-drm-build/bin/phx-gcc"
PHXCXX="${root}/tools/gpu-lane/e7-drm-build/bin/phx-g++"
COMPAT_INC="${here}/compat/include"          # Mesa + applications
APP_INC="${here}/compat/app-include"         # applications only (kmscube)

if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

for p in "${UPSTREAM}/.git" "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-gcc-ar" "${TC}-nm" "${TC}-strip" \
		"${PHXCC}" "${PHXCXX}" "${B}/lib/libz.a"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
# (No `nm | grep -q` under pipefail: grep's early exit SIGPIPEs nm and fails the test.)
libphx_syms="$("${TC}-nm" -g --defined-only "${S}/lib/libphoenix.a" 2>/dev/null || true)"
has_libc() { grep -qE " [TW] $1\$" <<< "${libphx_syms}"; }
has_libc sys_fdpath || { echo "build.sh: ${S}/lib/libphoenix.a has no sys_fdpath (stale sysroot)" >&2; exit 1; }

mkdir -p "${out}"

# --- libdrm-phoenix snapshot -------------------------------------------------------------
# libdrm-phoenix's own build-out may be rebuilt at any time by its owner; link against a
# private copy so one build is self-consistent. Never rebuilds libdrm-phoenix in place.
echo "== libdrm-phoenix snapshot"
if [ ! -f "${libdrm_src_prefix}/lib/libdrm.a" ]; then
	echo "  ${libdrm_src_prefix} has no libdrm.a: building libdrm-phoenix into ${out}/libdrm-build"
	"${root}/tools/gpu-lane/libdrm-phoenix/build.sh" --lib-only --out "${out}/libdrm-build"
	libdrm_src_prefix="${out}/libdrm-build/prefix"
fi
LD_PREFIX="${out}/libdrm-prefix"
rm -rf "${LD_PREFIX}"
mkdir -p "${LD_PREFIX}/lib/pkgconfig"
cp -a "${libdrm_src_prefix}/include" "${LD_PREFIX}/"
cp -a "${libdrm_src_prefix}/lib/libdrm.a" "${LD_PREFIX}/lib/"
cat > "${LD_PREFIX}/lib/pkgconfig/libdrm.pc" <<EOF
prefix=${LD_PREFIX}
includedir=\${prefix}/include
libdir=\${prefix}/lib

Name: libdrm
Description: libdrm-phoenix snapshot (upstream libdrm + Phoenix backend)
Version: 2.4.134
Libs: -L\${libdir} -ldrm
Cflags: -I\${includedir} -I\${includedir}/libdrm
EOF
{
	echo "source: ${libdrm_src_prefix}"
	echo "libdrm.a: $(stat -c '%s bytes, %y' "${LD_PREFIX}/lib/libdrm.a")"
	sha256sum "${LD_PREFIX}/lib/libdrm.a"
} > "${out}/libdrm-snapshot.txt"
sed 's/^/  /' "${out}/libdrm-snapshot.txt"

# --- compat shim ------------------------------------------------------------------------
echo "== compat shim"
compat_defs=()
has_libc open_memstream || compat_defs+=(-DMESADRM_NEED_OPEN_MEMSTREAM)
has_libc getopt_long_only || compat_defs+=(-DMESADRM_NEED_GETOPT_LONG_ONLY)
has_libc sincos || compat_defs+=(-DMESADRM_NEED_SINCOS)
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")
mkdir -p "${out}/compat"
"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${COMPAT_INC}" -I"${APP_INC}" "${compat_defs[@]}" \
	-c "${here}/compat/mesadrm_compat.c" -o "${out}/compat/mesadrm_compat.o"
rm -f "${out}/compat/libmesadrm-compat.a"
"${TC}-gcc-ar" rcs "${out}/compat/libmesadrm-compat.a" "${out}/compat/mesadrm_compat.o"
echo "  stand-ins compiled: ${compat_defs[*]:-none}"

MB="${out}/mesa-build"
if [ "${relink}" = 0 ]; then
	# --- Mesa source ------------------------------------------------------------------------
	src="${out}/mesa-src"
	stamp="$(cat "${here}"/patches/mesa/*.patch | sha256sum | cut -c1-16)"
	echo "== Mesa source (${MESA_TAG} + $(ls "${here}"/patches/mesa/*.patch | wc -l) patches, set ${stamp})"
	if [ ! -d "${src}/.git" ]; then
		git clone -q -s --no-checkout "${UPSTREAM}" "${src}"
	fi
	if [ "$(cat "${out}/mesa-src.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		git -C "${src}" checkout -q -f --detach "${MESA_COMMIT}"
		git -C "${src}" clean -qfdx
		for p in "${here}"/patches/mesa/*.patch; do
			echo "  apply $(basename "${p}")"
			git -C "${src}" apply --whitespace=nowarn "${p}"
		done
		git -C "${src}" diff "${MESA_COMMIT}" > "${out}/mesa-drm-full.patch"
		echo "${stamp}" > "${out}/mesa-src.stamp"
	else
		echo "  unchanged since the last build (stamp matches)"
	fi

	# --- zlib (Mesa's one hard port dependency) ----------------------------------------------
	# The ports prefix ships libz.a + zlib.h but no zlib.pc, and its include dir also holds
	# other ports' GL/ and X11/ headers -- never put it on Mesa's include path (header
	# poisoning). A private prefix exposes exactly zlib. (--wrap-mode=nodownload below:
	# without this, meson silently downloads and builds its own zlib.)
	ZP="${out}/zlib-prefix"
	rm -rf "${ZP}"
	mkdir -p "${ZP}/include" "${ZP}/lib/pkgconfig"
	cp "${B}/include/zlib.h" "${B}/include/zconf.h" "${ZP}/include/"
	zver="$(sed -n 's/^#define ZLIB_VERSION "\(.*\)"/\1/p' "${ZP}/include/zlib.h")"
	printf '%s\n' "Name: zlib" "Description: zlib from the Phoenix ports prefix" "Version: ${zver}" \
		"Libs: -L${B}/lib -lz" "Cflags: -I${ZP}/include" > "${ZP}/lib/pkgconfig/zlib.pc"

	# --- X11/xcb for --x11 (same reason as zlib: never the ports include/ itself) ----------
	# A private prefix with only the X11/ and xcb/ header trees; the .pc files are the ports'
	# own with includedir moved here (libdir stays the ports lib/: archives only).
	if [ "${x11}" = true ]; then
		XP="${out}/x11-prefix"
		rm -rf "${XP}"
		mkdir -p "${XP}/include" "${XP}/lib/pkgconfig"
		cp -a "${B}/include/X11" "${B}/include/xcb" "${XP}/include/"
		for pc in "${B}"/lib/pkgconfig/xcb*.pc "${B}"/lib/pkgconfig/x11*.pc "${B}"/lib/pkgconfig/xau.pc \
				"${B}"/lib/pkgconfig/xdmcp.pc "${B}"/lib/pkgconfig/xext.pc "${B}"/lib/pkgconfig/xrandr.pc \
				"${B}"/lib/pkgconfig/xrender.pc "${B}"/lib/pkgconfig/pthread-stubs.pc \
				"${B}"/share/pkgconfig/*proto.pc; do
			sed -e "s|^prefix=.*|prefix=${XP}|" -e "s|^libdir=.*|libdir=${B}/lib|" \
				-e "s|^includedir=.*|includedir=${XP}/include|" "${pc}" > "${XP}/lib/pkgconfig/$(basename "${pc}")"
		done
		[ -f "${x11_shmf}/include/X11/xshmfence.h" ] && [ -f "${x11_shmf}/lib/libxshmfence.a" ] \
			|| { echo "build.sh: no xshmfence in ${x11_shmf} (--x11-xshmfence)" >&2; exit 1; }
		cp "${x11_shmf}/include/X11/xshmfence.h" "${XP}/include/X11/"
		printf '%s\n' "Name: xshmfence" "Description: X shared memory fences (${x11_shmf})" "Version: 1.3.2" \
			"Libs: ${x11_shmf}/lib/libxshmfence.a" "Cflags: -I${XP}/include" > "${XP}/lib/pkgconfig/xshmfence.pc"
		echo "x11 prefix: ${XP} (xshmfence from ${x11_shmf})"
	fi

	# --- meson cross file -------------------------------------------------------------------
	echo "== meson cross file"
	cross="${out}/phoenix-aarch64.cross"
	pkgc="${out}/pkg-config-phoenix"
	cat > "${pkgc}" <<EOF
#!/bin/sh
# pkg-config restricted to the libdrm-phoenix snapshot + the private zlib prefix
# (+ the target libwayland for --wayland, the private X11/xcb prefix for --x11).
export PKG_CONFIG_LIBDIR=${LD_PREFIX}/lib/pkgconfig:${ZP}/lib/pkgconfig${wayland_pc:+:${wayland_pc}}${x11_pc:+:${x11_pc}}
unset PKG_CONFIG_PATH
exec /usr/bin/pkg-config --static "\$@"
EOF
	chmod +x "${pkgc}"
	# compat/include goes on -I (never -include: a force-included header flips meson's
	# probes, E7 §3.2). Both C and C++ see it.
	flags="'--sysroot=${S}/', '-B${S}/lib/', '-mcpu=cortex-a72', '-mtune=cortex-a72', '-mstrict-align', '-mno-outline-atomics', '-ffunction-sections', '-fdata-sections', '-I${COMPAT_INC}'"
	lflags="'--sysroot=${S}/', '-B${S}/lib/', '-L${B}/lib', '-Wl,-z,max-page-size=0x1000'"
	cat > "${cross}" <<EOF
# Generated by tools/gpu-lane/mesa-drm/build.sh (aarch64-phoenix, Pi 4).
[binaries]
c = '${PHXCC}'
cpp = '${PHXCXX}'
ar = '${TC}-gcc-ar'
nm = '${TC}-nm'
strip = '${TC}-strip'
objcopy = '${TC}-objcopy'
pkg-config = '${pkgc}'

[host_machine]
system = 'phoenix'
cpu_family = 'aarch64'
cpu = 'cortex-a72'
endian = 'little'

[properties]
needs_exe_wrapper = true
# False YES otherwise: posix_memalign is a gcc builtin, so meson's __has_builtin probe
# passes although libphoenix has no posix_memalign (E7 §3.2). With NO, Mesa's
# os_memory_aligned.h uses its own over-allocating fallback.
has_function_posix_memalign = false

[built-in options]
c_args = [${flags}]
cpp_args = [${flags}]
c_link_args = [${lflags}]
cpp_link_args = [${lflags}]
default_library = 'static'
EOF

	# --- Mesa configure + build -------------------------------------------------------------
	echo "== Mesa meson setup + ninja (-j${jobs})"
	if [ "${vulkan}" = true ]; then
		# v3dv only. VK_KHR_display (wsi_common_display.c) is built whenever the system
		# has KMS/DRM (patch 0001 puts phoenix there); no x11/wayland platform.
		api_opts=(-Dgallium-drivers= -Dvulkan-drivers=broadcom -Dvulkan-layers= -Dvulkan-beta=false
			-Degl=disabled -Dgbm=disabled -Dglx=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled)
	else
		api_opts=(-Dgallium-drivers=v3d,vc4 -Dvulkan-drivers=
			-Degl=enabled -Dgbm=enabled -Dglx=disabled -Dopengl=${opengl} -Dgles1=disabled -Dgles2=enabled)
	fi
	platforms=
	[ "${wayland}" = true ] && platforms=wayland
	# --x11: EGL on X11 through DRI3/Present (loader_dri3); GLX stays off, and so does
	# xlib-lease (a Vulkan extension that would pull libXrandr).
	[ "${x11}" = true ] && { platforms=x11; api_opts+=(-Dxlib-lease=disabled); }
	# A GL build dir must never be reconfigured as a Vulkan one or the other way round.
	if [ -f "${MB}/build.ninja" ]; then
		if [ "${vulkan}" = true ] && [ ! -f "${out}/mesa-vulkan.txt" ]; then
			echo "build.sh: ${MB} is a GL build; --vulkan needs its own --out" >&2; exit 1
		elif [ "${vulkan}" = false ] && [ -f "${out}/mesa-vulkan.txt" ]; then
			echo "build.sh: ${MB} is a --vulkan build; use another --out for GL" >&2; exit 1
		fi
	fi
	if [ ! -f "${MB}/build.ninja" ]; then
		meson setup "${MB}" "${src}" --cross-file "${cross}" --prefix "${out}/prefix" \
			--buildtype=debugoptimized -Db_ndebug=true --wrap-mode=nodownload \
			"${api_opts[@]}" -Dplatforms="${platforms}" \
			-Dllvm=disabled -Dspirv-tools=disabled -Dvideo-codecs= -Dgallium-va=disabled \
			-Dshader-cache=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dzstd=disabled \
			-Dlibunwind=disabled -Dvalgrind=disabled -Dlmsensors=disabled -Dperfetto=false \
			-Dbuild-tests=false -Dtools= \
			> "${out}/mesa-setup.log" 2>&1 || { tail -40 "${out}/mesa-setup.log"; exit 1; }
		[ "${vulkan}" = true ] && echo "vulkan=true" > "${out}/mesa-vulkan.txt"
		[ "${wayland}" = true ] && echo "wayland=true" > "${out}/mesa-wayland.txt"
		[ "${x11}" = true ] && echo "x11=true" > "${out}/mesa-x11.txt"
	fi
	if [ "${x11}" = true ] && [ ! -f "${out}/mesa-x11.txt" ]; then
		echo "build.sh: ${MB} is not an --x11 build; use another --out" >&2; exit 1
	elif [ "${x11}" = false ] && [ -f "${out}/mesa-x11.txt" ]; then
		echo "build.sh: ${MB} is an --x11 build; use another --out" >&2; exit 1
	fi
	if [ "${wayland}" = true ] && [ ! -f "${out}/mesa-wayland.txt" ]; then
		echo "build.sh: ${MB} is not a --wayland build; use another --out" >&2; exit 1
	elif [ "${wayland}" = false ] && [ -f "${out}/mesa-wayland.txt" ]; then
		echo "build.sh: ${MB} is a --wayland build; use another --out" >&2; exit 1
	fi
	# A build dir configured the other way round would silently keep its old option. A dir
	# from before this label existed gets it from its own meson summary first.
	if [ ! -f "${out}/mesa-opengl.txt" ]; then
		if grep -qE '^ *OpenGL *: *YES' "${out}/mesa-setup.log" 2>/dev/null; then
			echo "opengl=true" > "${out}/mesa-opengl.txt"
		elif grep -qE '^ *OpenGL *: *NO' "${out}/mesa-setup.log" 2>/dev/null; then
			echo "opengl=false" > "${out}/mesa-opengl.txt"
		fi
	fi
	grep -q "^opengl=${opengl}\$" "${out}/mesa-opengl.txt" 2>/dev/null || [ ! -f "${out}/mesa-opengl.txt" ] \
		|| { echo "build.sh: ${MB} was configured with $(cat "${out}/mesa-opengl.txt"); use --clean or another --out" >&2; exit 1; }
	echo "opengl=${opengl}" > "${out}/mesa-opengl.txt"
	extra_targets=()
	[ "${opengl}" = true ] && extra_targets+=(src/mesa/glapi/glapi/libglapi_bridge.a)
	ninja -C "${MB}" -j"${jobs}" all "${extra_targets[@]}" > "${out}/mesa-ninja.log" 2>&1 || { grep -E 'error|FAILED' "${out}/mesa-ninja.log" | head -40; exit 1; }
	ninja -C "${MB}" install > "${out}/mesa-install.log" 2>&1 || { tail -20 "${out}/mesa-install.log"; exit 1; }
	nwarn=$(grep -c 'warning:' "${out}/mesa-ninja.log" || true)
	echo "  Mesa built: ${nwarn} compiler warning line(s) (${out}/mesa-ninja.log)"
fi
[ -f "${MB}/build.ninja" ] || { echo "build.sh: no Mesa build in ${MB} (run without --relink first)" >&2; exit 1; }

if [ "${vulkan}" = true ]; then
	# --- Vulkan: the static ICD + what a program links with it ------------------------------
	echo "== v3dv static ICD"
	icd="${out}/prefix/lib/libvulkan_broadcom.a"
	[ -f "${icd}" ] || { echo "build.sh: ${icd} not installed (patch 0010 missing?)" >&2; exit 1; }
	rm -rf "${out}/prefix/include/vulkan" "${out}/prefix/include/vk_video"
	mkdir -p "${out}/prefix/include"
	cp -a "${src}/include/vulkan" "${src}/include/vk_video" "${out}/prefix/include/"
	# The installed archive bundles meson's internal static libraries (vulkan runtime,
	# wsi, util, NIR, SPIR-V, broadcom compiler); list the rest in link order.
	{
		echo "${icd}"
		echo "${LD_PREFIX}/lib/libdrm.a"
		echo "${out}/compat/libmesadrm-compat.a"
		echo "${B}/lib/libz.a"
	} > "${out}/vulkan-link.txt"
	isyms="$("${TC}-nm" -g --defined-only "${icd}" 2>/dev/null || true)"
	for s in vk_icdGetInstanceProcAddr vk_icdNegotiateLoaderICDInterfaceVersion vk_icdGetPhysicalDeviceProcAddr \
			v3dv_GetInstanceProcAddr wsi_CreateDisplayPlaneSurfaceKHR wsi_display_init_wsi vk_drm_syncobj_get_type; do
		if grep -qE " [TW] ${s}\$" <<< "${isyms}"; then echo "  symbol ${s}: yes"; else echo "  symbol ${s}: NO"; fi
	done
	echo "  ${icd}: $(stat -c %s "${icd}") bytes; $(head -c 8 "${icd}" | tr -d '\n<>!' ) archive"
	echo "  link list: ${out}/vulkan-link.txt"
	echo "done"
	exit 0
fi

# The archives a program links for EGL/GBM/GLES, in order (the kmscube link below).
A=(src/egl/libEGL.a src/gbm/libgbm.a src/gbm/backends/dri/dri_gbm.a src/mesa/glapi/es2api/libGLESv2.a
	src/mesa/glapi/shared-glapi/libglapi.a src/gallium/drivers/v3d/libv3d.a
	src/gallium/drivers/v3d/libv3d-v42.a src/gallium/drivers/v3d/libv3d-v71.a
	src/broadcom/libbroadcom-v42.a src/broadcom/libbroadcom-v71.a src/broadcom/qpu/libbroadcom_qpu.a
	src/broadcom/libv3d_neon.a src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a
	src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a src/gallium/winsys/kmsro/drm/libkmsrowinsys.a
	src/gallium/winsys/v3d/drm/libv3dwinsys.a src/gallium/winsys/vc4/drm/libvc4winsys.a
	src/gallium/winsys/sw/kms-dri/libswkmsdri.a src/gallium/winsys/sw/dri/libswdri.a
	src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a
	src/c11/impl/libmesa_util_c11.a)

if [ "${wayland}" = true ]; then
	# --- Wayland: the link list for consumers (weston, wayland-egl clients) -----------------
	echo "== EGL wayland platform: link list"
	{
		echo "--whole-archive $(ls "${MB}"/src/gallium/targets/dri/libgallium-*.a)"
		for a in "${A[@]}" src/egl/wayland/wayland-drm/libwayland_drm.a; do
			[ -f "${MB}/${a}" ] && echo "${MB}/${a}"
		done
		echo "${LD_PREFIX}/lib/libdrm.a"
		echo "${out}/compat/libmesadrm-compat.a"
		echo "${B}/lib/libz.a"
	} > "${out}/egl-link.txt"
	syms="$("${TC}-nm" -g --defined-only "${MB}"/src/egl/libEGL.a 2>/dev/null || true)"
	for s in dri2_initialize_wayland dri2_initialize_drm; do
		if grep -qE " [Tt] ${s}\$" <<< "${syms}"; then echo "  symbol ${s}: yes"; else echo "  symbol ${s}: NO"; fi
	done
	grep -q 'EGL_EXT_image_dma_buf_import' <(strings -a "${MB}"/src/egl/libEGL.a) && echo "  EGL_EXT_image_dma_buf_import: yes"
	echo "  link list: ${out}/egl-link.txt"
	echo "done"
	exit 0
fi

if [ "${x11}" = true ]; then
	# --- X11: the link list for EGL-on-X11 clients (tools/gpu-lane/x11-drm) -----------------
	# libEGL.a bundles the platform_x11/_dri3 objects; loader_dri3_helper sits in the gallium
	# target (DRI frontend), libloader_x11 in libEGL. The xcb/X11 archives follow Mesa; the
	# program adds its own xshmfence archive (the Phoenix backend) in place of the one Mesa
	# configured against when they differ.
	echo "== EGL x11 platform: link list"
	{
		echo "--whole-archive $(ls "${MB}"/src/gallium/targets/dri/libgallium-*.a)"
		for a in "${A[@]}" src/x11/libloader_x11.a; do
			[ -f "${MB}/${a}" ] && echo "${MB}/${a}"
		done
		for l in X11-xcb X11 xcb-dri3 xcb-present xcb-sync xcb-xfixes xcb-randr xcb-shm xcb-render xcb-shape \
				xcb-keysyms xcb Xau Xdmcp; do
			[ -f "${B}/lib/lib${l}.a" ] && echo "${B}/lib/lib${l}.a"
		done
		echo "${x11_shmf}/lib/libxshmfence.a"
		echo "${LD_PREFIX}/lib/libdrm.a"
		echo "${out}/compat/libmesadrm-compat.a"
		echo "${B}/lib/libz.a"
	} > "${out}/x11-link.txt"
	syms="$("${TC}-nm" -g --defined-only "${MB}"/src/egl/libEGL.a 2>/dev/null || true)"
	for s in dri2_initialize_x11 dri3_x11_connect dri2_initialize_drm; do
		if grep -qE " [Tt] ${s}\$" <<< "${syms}"; then echo "  symbol ${s}: yes"; else echo "  symbol ${s}: NO"; fi
	done
	gsyms="$("${TC}-nm" -g --defined-only "${MB}"/src/gallium/targets/dri/libgallium-*.a 2>/dev/null || true)"
	for s in loader_dri3_swap_buffers_msc loader_dri3_drawable_init; do
		if grep -qE " [Tt] ${s}\$" <<< "${gsyms}"; then echo "  symbol ${s}: yes"; else echo "  symbol ${s}: NO"; fi
	done
	grep -q 'EGL_EXT_platform_xcb' <(strings -a "${MB}"/src/egl/libEGL.a) && echo "  EGL_EXT_platform_xcb: yes"
	echo "  link list: ${out}/x11-link.txt"
	echo "done"
	exit 0
fi

# --- kmscube ----------------------------------------------------------------------------
echo "== kmscube"
ksrc="${out}/kmscube-src"
if [ ! -d "${ksrc}/.git" ]; then
	git clone -q "${KMSCUBE_URL}" "${ksrc}"
fi
git -C "${ksrc}" checkout -q -f "${KMSCUBE_COMMIT}"
# Source list = kmscube's meson.build with GLES3 (shadertoy) and without libpng/gstreamer.
KSRCS=(common.c cube-smooth.c cube-gears.c cube-tex.c cube-shadertoy.c drm-atomic.c drm-common.c
	drm-legacy.c drm-offscreen.c esTransform.c frame-512x512-NV12.c frame-512x512-RGBA.c kmscube.c perfcntrs.c)
KFLAGS=(-O2 -g -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
	"${TFLAGS[@]}" -DHAVE_GLES3 -I"${COMPAT_INC}" -I"${APP_INC}" -I"${out}/prefix/include"
	-I"${LD_PREFIX}/include" -I"${LD_PREFIX}/include/libdrm")
kobj="${out}/kmscube-obj"
rm -rf "${kobj}"
mkdir -p "${kobj}"
: > "${out}/kmscube-cc.log"
for f in "${KSRCS[@]}"; do
	"${TC}-gcc" "${KFLAGS[@]}" -c "${ksrc}/${f}" -o "${kobj}/${f%.c}.o" 2>> "${out}/kmscube-cc.log" \
		|| { grep -A3 'error' "${out}/kmscube-cc.log" | head -30; exit 1; }
done
echo "  compiled ${#KSRCS[@]} files ($(grep -c 'warning:' "${out}/kmscube-cc.log" || true) warning line(s), ${out}/kmscube-cc.log)"

# Link = E7's proven link-kmscube.sh shape. meson makes internal static libraries THIN
# archives and bundles their objects into the archives of the installed targets, so
# libgallium-26.2.0.a holds dri_target + the DRI frontend (link_whole) + libmesa, NIR,
# GLSL, the v3d/vc4 drivers and winsyses, util...: it goes in whole-archive (as E7 linked
# the target's objects + libdri whole); EGL/GBM/dri_gbm/GLESv2 (which bundle their own
# copies of loader/util objects -- never pulled twice, their symbols are already defined)
# and the small per-version archives follow in one group.
# -Wl,--wrap=mmap: Mesa's BO maps (v3d_bufmgr.c, vc4_bufmgr.c, gbm dumb maps) do
# mmap(drm_fd, token); libdrm-phoenix's __wrap_mmap resolves the tokens (M3 §2.6).
AA=()
for a in "${A[@]}"; do
	if [ -f "${MB}/${a}" ]; then AA+=("${MB}/${a}"); else echo "  (archive not built: ${a})"; fi
done
# Report archives meson built that the list does not name (a new dependency would show here).
while IFS= read -r a; do
	rel="${a#"${MB}"/}"
	case " ${A[*]} " in *" ${rel} "*) ;; *)
		case "${rel}" in src/gallium/targets/dri/libgallium-*.a|src/gallium/frontends/dri/libdri.a) ;;
			*) echo "  (built but not linked: ${rel})" ;; esac ;;
	esac
done < <(find "${MB}/src" -name '*.a' | sort)
GALLIUM_A="$(ls "${MB}"/src/gallium/targets/dri/libgallium-*.a)"
"${PHXCXX}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,--wrap=mmap \
	-Wl,-Map,"${out}/kmscube.map" -o "${out}/kmscube" "${kobj}"/*.o \
	-Wl,--whole-archive "${GALLIUM_A}" -Wl,--no-whole-archive \
	-Wl,--start-group "${AA[@]}" "${LD_PREFIX}/lib/libdrm.a" "${out}/compat/libmesadrm-compat.a" \
	"${B}/lib/libz.a" -Wl,--end-group -lm > "${out}/kmscube-link.log" 2>&1 \
	|| { head -60 "${out}/kmscube-link.log"; exit 1; }
"${TC}-strip" -o "${out}/kmscube-stripped" "${out}/kmscube"
echo "  ${out}/kmscube: $(stat -c %s "${out}/kmscube") bytes; stripped $(stat -c %s "${out}/kmscube-stripped") bytes"
[ -s "${out}/kmscube-link.log" ] && sed 's/^/  link: /' "${out}/kmscube-link.log" | head -20

# --- verification -----------------------------------------------------------------------
echo "== verify"
"${TC}-size" "${out}/kmscube" | sed 's/^/  /'
und="$("${TC}-nm" -u "${out}/kmscube" || true)"
echo "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && sed 's/^/    /' <<< "${und}" | head -20
syms="$("${TC}-nm" "${out}/kmscube")"
for s in __wrap_mmap drmPhoenixMmap drm_phoenix_ioctl gbmint_get_backend v3d_drm_screen_create_renderonly \
		vc4_drm_screen_create kmsro_drm_screen_create; do
	if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then echo "  symbol ${s}: yes"; else echo "  symbol ${s}: NO"; fi
done
strs="$(strings -a "${out}/kmscube-stripped")"
for s in /dev/kms /dev/v3d-async /kmsbuf /dev/dri/card0 'libdrm-phoenix:' kmsro '"v3d"' v3d vc4 \
		'V3D 4.2' EGL_KHR_platform_gbm; do
	n=$(grep -cF -- "${s//\"/}" <<< "${strs}" || true)
	echo "  strings '${s}': ${n}"
done
bad=0
for s in 'v3d-winsys:' phoenix_v3d_ioctl peek_next_scanout v3d-srv; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  old-lane string '${s}': ${n}"
	[ "${n}" = 0 ] || bad=1
done
# dlopen must not be reachable from the GBM path (patch 0003); report who references it.
if grep -qE ' [Tt] dlopen$' <<< "${syms}"; then
	echo "  note: dlopen is linked (referenced by: $(grep -B1 -E '^ +0x[0-9a-f]+ +dlopen$' "${out}/kmscube.map" | head -1 | tr -s ' ' | cut -c1-120))"
fi
[ "${bad}" = 0 ] || { echo "build.sh: old-lane strings present" >&2; exit 1; }
echo "done"
