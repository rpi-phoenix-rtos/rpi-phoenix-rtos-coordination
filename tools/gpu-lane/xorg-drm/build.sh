#!/usr/bin/env bash
#
# FRAMEWORK PORT: sources/phoenix-rtos-ports xorg_server_drm (+ libepoxy, + libxshmfence_phoenix),
# opt-in, not in the default image (docs/gpu-new-lane/MIGRATION.md section 4, "Ports (graphics)").
# Every patch/glue file this script uses is also a file of the port; the copies are kept
# identical by scripts/check-gpu-lane-ports-sync.sh -- a change here must be copied there.
#
# xorg-drm (new GPU lane, M4 preparation): a statically linked X.Org server
# `Xorg-drm` -- hw/xfree86 + the modesetting DDX + glamor on GBM/EGL + DRI2/DRI3/
# Present -- cross-built for aarch64-phoenix against libdrm-phoenix and the
# mesa-drm static Mesa (docs/gpu-new-lane/M4-xorg-modesetting.md).
#
#   <out>/dl/              pinned source archives (sha256-checked)
#   <out>/src/             extracted + patched sources (xorg-server, libepoxy, libxcvt, libxshmfence)
#   <out>/deps-prefix/     static libepoxy (static-EGL dispatch), libxcvt, libxshmfence + .pc files
#   <out>/libdrm-prefix/   the libdrm-phoenix snapshot the Mesa build was linked with (copied)
#   <out>/xorg-build/      meson build dir of xorg-server (static archives only; Xorg is hand-linked)
#   <out>/obj/             Xorg-drm's own objects (builtin-module table, phxhid input driver, compat)
#   <out>/Xorg-drm         static, unstripped (addr2line);  <out>/Xorg-drm-stripped (stage this)
#   <out>/Xorg-drm.map     link map;  <out>/xorg-drm-full.patch  all xorg-server patches as one diff
#
# The old lane is never touched: the X.Org tarball is read from the ports tree
# (sources/phoenix-rtos-ports/xorg_server/, read-only, same sha256 as its recipe),
# the X11 libraries (pixman, Xfont2, xkbfile, Xau, freetype, fontenc, xorgproto,
# xtrans, libmd, zlib) are linked from the ports prefix the image build staged,
# and nothing of tools/x11-port/ or the kdrive DDX is compiled.
#
# Writes only into <out> (default build-out/, gitignored). No Pi, no
# rebuild-rpi4b-fast.sh, no /srv.
#
# Usage: tools/gpu-lane/xorg-drm/build.sh [--clean] [--out <dir>] [-j N] [--relink]
#                                         [--mesa-out <dir>] [--libdrm-prefix <dir>]
#   --relink         skip deps + meson; recompile Xorg-drm's own objects and relink
#   --mesa-out       the mesa-drm build-out to link (default <out>/mesa: a private Mesa build,
#                    made by tools/gpu-lane/mesa-drm/build.sh --out <out>/mesa on first use);
#                    its libdrm-prefix snapshot is used too, so headers and archives agree.
#                    A Mesa built with mesa-drm's --opengl makes glamor use desktop GL.
#   --libdrm-prefix  the libdrm-phoenix prefix a private Mesa build links
#                    (default tools/gpu-lane/libdrm-phoenix/build-out-m3p3/prefix)
#   --xshmfence-prefix  link this libxshmfence (lib/libxshmfence.a + include/X11/xshmfence.h)
#                    instead of building upstream's pthread backend: M4 part 2 passes
#                    tools/gpu-lane/x11-drm/build-out/xshmfence-prefix (the Phoenix-RTOS backend,
#                    G16) -- DRI3 clients must link the same archive (struct xshmfence layout)
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
clean=0
relink=0
jobs="$(nproc)"
mesa_out=""
libdrm_src_prefix="${root}/tools/gpu-lane/libdrm-phoenix/build-out-m3p3/prefix"
shmf_prefix=""
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--relink) relink=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		--mesa-out) shift; mesa_out="${1:?--mesa-out needs a directory}" ;;
		--mesa-out=*) mesa_out="${1#--mesa-out=}" ;;
		--libdrm-prefix) shift; libdrm_src_prefix="${1:?--libdrm-prefix needs a directory}" ;;
		--libdrm-prefix=*) libdrm_src_prefix="${1#--libdrm-prefix=}" ;;
		--xshmfence-prefix) shift; shmf_prefix="${1:?--xshmfence-prefix needs a directory}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
[ -n "${mesa_out}" ] || mesa_out="${out}/mesa"
case "${mesa_out}" in /*) ;; *) mesa_out="${PWD}/${mesa_out}" ;; esac

if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

# --- pinned inputs ------------------------------------------------------------------------
XORG_VER=21.1.24
XORG_TARBALL="${root}/sources/phoenix-rtos-ports/xorg_server/xorg-server-${XORG_VER}.tar.xz"
XORG_SHA=1a4eb36ca65cc3b1b936566d677a9786e13c11cd5806e951ac55f3f5ce3984af   # = the old lane's recipe
EPOXY_VER=1.5.10
EPOXY_URL="https://github.com/anholt/libepoxy/archive/refs/tags/${EPOXY_VER}.tar.gz"
EPOXY_SHA=a7ced37f4102b745ac86d6a70a9da399cc139ff168ba6b8002b4d8d43c900c15
XCVT_VER=0.1.2
XCVT_URL="https://www.x.org/releases/individual/lib/libxcvt-${XCVT_VER}.tar.xz"
XCVT_SHA=0561690544796e25cfbd71806ba1b0d797ffe464e9796411123e79450f71db38
SHMF_VER=1.3.2
SHMF_URL="https://www.x.org/releases/individual/lib/libxshmfence-${SHMF_VER}.tar.xz"
SHMF_SHA=870df257bc40b126d91b5a8f1da6ca8a524555268c50b59c0acd1a27f361606f

B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"      # ports prefix (X11 libs)
S="${B}/sysroot"                                            # tree sysroot (libphoenix)
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCC="${root}/tools/gpu-lane/e7-drm-build/bin/phx-gcc"     # drops -pthread (E7)
PHXCXX="${root}/tools/gpu-lane/e7-drm-build/bin/phx-g++"
MESA_PREFIX="${mesa_out}/prefix"
MESA_BUILD="${mesa_out}/mesa-build"
COMPAT_INC="${here}/compat/include"
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

# --- Mesa-DRM (GBM/EGL/GLES for glamor) --------------------------------------------------
# A private build by default: mesa-drm's shared build-out is rebuilt by other work at any
# time, and Xorg-drm must link the archives its headers came from.
if [ ! -f "${mesa_out}/prefix/lib/libEGL.a" ]; then
	echo "== Mesa-DRM: building into ${mesa_out} (libdrm-phoenix ${libdrm_src_prefix})"
	"${root}/tools/gpu-lane/mesa-drm/build.sh" --out "${mesa_out}" --libdrm-prefix "${libdrm_src_prefix}" \
		> "${out}/mesa-build.log" 2>&1 || { tail -30 "${out}/mesa-build.log"; exit 1; }
fi
# The DRM_CAP_PRIME fix (mesa-drm patch 0008): without it u_init_pipe_screen_caps() never
# asks the kernel-side for PRIME, caps.dmabuf stays 0, GBM makes scan-out buffers without
# a DRI image and Mesa NULL-dereferences in dri2_allocate_textures -- glamor on GBM
# allocates exactly that way. A call to drmGetCap in the function (its CALL26 relocation) = fixed.
u_screen_o="$(find "${mesa_out}/mesa-build/src" -name '*u_screen.c.o' 2>/dev/null | head -1)"
n_getcap=0
[ -n "${u_screen_o}" ] && n_getcap=$("${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-objdump" -dr "${u_screen_o}" \
	| awk '/<u_init_pipe_screen_caps>:/,/^$/' | grep -c 'CALL26.*drmGetCap' || true)
[ "${n_getcap}" -ge 1 ] || { echo "build.sh: ${mesa_out} lacks the DRM_CAP_PRIME fix (mesa-drm 0008): rebuild it" >&2; exit 1; }
echo "== Mesa-DRM ${mesa_out}: DRM_CAP_PRIME query present (${n_getcap} call(s) to drmGetCap in u_init_pipe_screen_caps)"

for p in "${XORG_TARBALL}" "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-nm" "${TC}-strip" "${PHXCC}" "${PHXCXX}" \
		"${B}/lib/libpixman-1.a" "${B}/lib/libXfont2.a" "${B}/lib/libxkbfile.a" "${B}/lib/libmd.a" \
		"${B}/share/pkgconfig/xproto.pc" "${B}/share/pkgconfig/xtrans.pc" \
		"${MESA_PREFIX}/lib/libEGL.a" "${MESA_PREFIX}/lib/libgbm.a" "${mesa_out}/libdrm-prefix/lib/libdrm.a" \
		"${mesa_out}/zlib-prefix/lib/pkgconfig/zlib.pc"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
libphx_syms="$("${TC}-nm" -g --defined-only "${S}/lib/libphoenix.a" 2>/dev/null || true)"
has_libc() { grep -qE " [TW] $1\$" <<< "${libphx_syms}"; }

# The X server's config scanner reads numbers with isdigit(c = buf[pos++]); libphoenix's ctype
# macros used to evaluate their argument more than once, which parsed "DefaultDepth 24" as 2
# (m4b). libphoenix 156422a fixed them at source; refuse a sysroot that predates the fix.
ctype_probe="$(printf '#include <ctype.h>\nint f(const char *p) { return isdigit(*p++); }\n' \
	| "${TC}-gcc" "${TFLAGS[@]}" -I"${COMPAT_INC}" -E -P -x c - 2>/dev/null | sed -n '/^int f(/,$p' | tr -s ' \n' ' ')"
if [ "$(grep -o '\*p++' <<< "${ctype_probe}" | wc -l)" -ne 1 ]; then
	echo "build.sh: sysroot <ctype.h> evaluates the argument more than once (need libphoenix >= 156422a): ${ctype_probe}" >&2
	exit 1
fi

mkdir -p "${out}/dl" "${out}/src"
DP="${out}/deps-prefix"
LDP="${out}/libdrm-prefix"
XS="${out}/src/xorg-server-${XORG_VER}"
XB="${out}/xorg-build"

fetch() {  # fetch <url> <file> <sha256>
	local f="${out}/dl/$2"
	if [ ! -f "${f}" ]; then
		echo "  fetch $1"
		curl -sSfL -o "${f}.part" "$1"
		mv "${f}.part" "${f}"
	fi
	echo "$3  ${f}" | sha256sum -c --quiet - || { echo "build.sh: sha256 mismatch for ${f}" >&2; exit 1; }
}

# The pkg-config every configure step sees: only these prefixes, in this order (never the
# shared sysroot; the ports prefix only for the X11 libraries -- its include/ has no GL/EGL/
# GLES/gbm/drm headers that could shadow ours, checked below).
pkgc="${out}/pkg-config-phoenix"
PKG_LIBDIR="${DP}/lib/pkgconfig:${DP}/share/pkgconfig:${LDP}/lib/pkgconfig:${MESA_PREFIX}/lib/pkgconfig"
PKG_LIBDIR="${PKG_LIBDIR}:${mesa_out}/zlib-prefix/lib/pkgconfig:${B}/lib/pkgconfig:${B}/share/pkgconfig"
cat > "${pkgc}" <<EOF
#!/bin/sh
export PKG_CONFIG_LIBDIR=${PKG_LIBDIR}
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
exec /usr/bin/pkg-config --static "\$@"
EOF
chmod +x "${pkgc}"
for h in EGL GLES2 GLES3 KHR gbm.h xf86drm.h libdrm epoxy; do
	[ ! -e "${B}/include/${h}" ] || { echo "build.sh: ${B}/include/${h} would shadow the new-lane headers" >&2; exit 1; }
done

# meson cross file (same recipe as mesa-drm: E7 wrappers, tree sysroot, compat on -I)
cross="${out}/phoenix-aarch64.cross"
flags="'--sysroot=${S}/', '-B${S}/lib/', '-mcpu=cortex-a72', '-mtune=cortex-a72', '-mstrict-align', '-mno-outline-atomics', '-ffunction-sections', '-fdata-sections', '-I${COMPAT_INC}'"
lflags="'--sysroot=${S}/', '-B${S}/lib/', '-L${B}/lib', '-Wl,-z,max-page-size=0x1000'"
cat > "${cross}" <<EOF
# Generated by tools/gpu-lane/xorg-drm/build.sh (aarch64-phoenix, Pi 4).
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

[built-in options]
c_args = [${flags}]
cpp_args = [${flags}]
c_link_args = [${lflags}]
cpp_link_args = [${lflags}]
default_library = 'static'
EOF

if [ "${relink}" = 0 ]; then
	# --- libdrm-phoenix snapshot (the one mesa-drm linked) ------------------------------
	echo "== libdrm-phoenix snapshot (from ${mesa_out}/libdrm-prefix)"
	rm -rf "${LDP}"
	cp -a "${mesa_out}/libdrm-prefix" "${LDP}"
	sed -i "s|^prefix=.*|prefix=${LDP}|" "${LDP}/lib/pkgconfig/libdrm.pc"
	{ cat "${mesa_out}/libdrm-snapshot.txt"; sha256sum "${LDP}/lib/libdrm.a"; } > "${out}/libdrm-snapshot.txt"
	sed 's/^/  /' "${out}/libdrm-snapshot.txt"

	# --- dependencies the ports tree does not have ---------------------------------------
	fetch "${EPOXY_URL}" "libepoxy-${EPOXY_VER}.tar.gz" "${EPOXY_SHA}"
	fetch "${XCVT_URL}" "libxcvt-${XCVT_VER}.tar.xz" "${XCVT_SHA}"
	fetch "${SHMF_URL}" "libxshmfence-${SHMF_VER}.tar.xz" "${SHMF_SHA}"
	mkdir -p "${DP}"

	if [ ! -f "${DP}/lib/libxcvt.a" ]; then
		echo "== libxcvt ${XCVT_VER} (MIT)"
		rm -rf "${out}/src/libxcvt-${XCVT_VER}"
		tar xJf "${out}/dl/libxcvt-${XCVT_VER}.tar.xz" -C "${out}/src"
		# One source file, and its meson.build hard-codes shared_library(): compile it directly.
		mkdir -p "${DP}/lib/pkgconfig" "${DP}/include/libxcvt" "${out}/xcvt-obj"
		"${TC}-gcc" -O2 -g -Wall "${TFLAGS[@]}" -I"${out}/src/libxcvt-${XCVT_VER}/include" \
			-c "${out}/src/libxcvt-${XCVT_VER}/lib/libxcvt.c" -o "${out}/xcvt-obj/libxcvt.o"
		rm -f "${DP}/lib/libxcvt.a"
		"${TC}-gcc-ar" rcs "${DP}/lib/libxcvt.a" "${out}/xcvt-obj/libxcvt.o"
		cp "${out}/src/libxcvt-${XCVT_VER}/include/libxcvt/"*.h "${DP}/include/libxcvt/"
		printf '%s\n' "prefix=${DP}" 'libdir=${prefix}/lib' 'includedir=${prefix}/include' '' \
			'Name: libxcvt' 'Description: VESA CVT modelines (static, new GPU lane)' "Version: ${XCVT_VER}" \
			'Libs: -L${libdir} -lxcvt -lm' 'Cflags: -I${includedir}' > "${DP}/lib/pkgconfig/libxcvt.pc"
	fi

	# Which libxshmfence this out dir links; switching backends rebuilds (never a stale mix).
	shmf_want="upstream-pthread"
	if [ -n "${shmf_prefix}" ] && [ ! -f "${shmf_prefix}/lib/libxshmfence.a" ]; then
		echo "build.sh: no lib/libxshmfence.a in --xshmfence-prefix ${shmf_prefix}" >&2; exit 1
	fi
	[ -n "${shmf_prefix}" ] && shmf_want="prefix:${shmf_prefix}:$(sha256sum "${shmf_prefix}/lib/libxshmfence.a" | cut -c1-16)"
	if [ "$(cat "${DP}/xshmfence-backend.txt" 2>/dev/null || echo upstream-pthread)" != "${shmf_want}" ]; then
		rm -f "${DP}/lib/libxshmfence.a"
	fi
	if [ -n "${shmf_prefix}" ] && [ ! -f "${DP}/lib/libxshmfence.a" ]; then
		echo "== libxshmfence from ${shmf_prefix}"
		mkdir -p "${DP}/lib/pkgconfig" "${DP}/include/X11"
		cp "${shmf_prefix}/lib/libxshmfence.a" "${DP}/lib/"
		cp "${shmf_prefix}/include/X11/xshmfence.h" "${DP}/include/X11/"
		printf '%s\n' "prefix=${DP}" 'libdir=${prefix}/lib' 'includedir=${prefix}/include' '' \
			'Name: xshmfence' "Description: X shared memory fences (from ${shmf_prefix})" "Version: ${SHMF_VER}" \
			'Libs: -L${libdir} -lxshmfence' 'Cflags: -I${includedir}' > "${DP}/lib/pkgconfig/xshmfence.pc"
		echo "${shmf_want}" > "${DP}/xshmfence-backend.txt"
	fi
	if [ ! -f "${DP}/lib/libxshmfence.a" ]; then
		echo "== libxshmfence ${SHMF_VER} (MIT; pthread backend, SHMDIR=/tmp)"
		rm -rf "${out}/src/libxshmfence-${SHMF_VER}"
		tar xJf "${out}/dl/libxshmfence-${SHMF_VER}.tar.xz" -C "${out}/src"
		for p in "${here}"/patches/libxshmfence/*.patch; do
			[ -e "${p}" ] || continue
			echo "  apply $(basename "${p}")"
			patch -s -d "${out}/src/libxshmfence-${SHMF_VER}" -p1 < "${p}"
		done
		( cd "${out}/src/libxshmfence-${SHMF_VER}" \
		  && PKG_CONFIG="${pkgc}" ./configure --host=aarch64-phoenix --prefix="${DP}" \
			--disable-shared --enable-static --disable-futex --with-shared-memory-dir=/tmp \
			CC="${PHXCC}" AR="${TC}-gcc-ar" RANLIB="${TC}-gcc-ranlib" \
			CFLAGS="-O2 -g ${TFLAGS[*]} -I${COMPAT_INC}" LDFLAGS="--sysroot=${S}/ -B${S}/lib/" \
			> "${out}/xshmfence-configure.log" 2>&1 \
		  && make -j"${jobs}" > "${out}/xshmfence-make.log" 2>&1 \
		  && make install > "${out}/xshmfence-install.log" 2>&1 ) \
			|| { tail -30 "${out}"/xshmfence-*.log; exit 1; }
		echo "upstream-pthread" > "${DP}/xshmfence-backend.txt"
	fi

	if [ ! -f "${DP}/lib/libepoxy.a" ]; then
		echo "== libepoxy ${EPOXY_VER} (MIT; static-EGL dispatch patch)"
		rm -rf "${out}/src/libepoxy-${EPOXY_VER}" "${out}/epoxy-build"
		tar xzf "${out}/dl/libepoxy-${EPOXY_VER}.tar.gz" -C "${out}/src"
		for p in "${here}"/patches/libepoxy/*.patch; do
			echo "  apply $(basename "${p}")"
			patch -s -d "${out}/src/libepoxy-${EPOXY_VER}" -p1 < "${p}"
		done
		meson setup "${out}/epoxy-build" "${out}/src/libepoxy-${EPOXY_VER}" --cross-file "${cross}" \
			--prefix "${DP}" --libdir lib --buildtype=debugoptimized -Db_ndebug=true -Db_staticpic=false \
			-Dglx=no -Degl=yes -Dx11=false -Dtests=false -Ddocs=false \
			> "${out}/epoxy-setup.log" 2>&1 || { tail -30 "${out}/epoxy-setup.log"; exit 1; }
		ninja -C "${out}/epoxy-build" > "${out}/epoxy-ninja.log" 2>&1 || { grep -B2 -A6 -E 'error|FAILED' "${out}/epoxy-ninja.log" | head -40; exit 1; }
		ninja -C "${out}/epoxy-build" install > "${out}/epoxy-install.log" 2>&1 || { tail -20 "${out}/epoxy-install.log"; exit 1; }
	fi

	# --- xorg-server source ----------------------------------------------------------------
	stamp="$(cat "${here}"/patches/xorg-server/*.patch | sha256sum | cut -c1-16)"
	echo "== xorg-server ${XORG_VER} ($(ls "${here}"/patches/xorg-server/*.patch | wc -l) patches, set ${stamp})"
	echo "${XORG_SHA}  ${XORG_TARBALL}" | sha256sum -c --quiet - || { echo "build.sh: xorg tarball sha256 mismatch" >&2; exit 1; }
	if [ "$(cat "${out}/xorg-src.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		rm -rf "${XS}" "${XB}"
		tar xJf "${XORG_TARBALL}" -C "${out}/src"
		git -C "${XS}" init -q
		git -C "${XS}" add -A
		git -C "${XS}" -c user.email=build@localhost -c user.name=build commit -qm "xorg-server-${XORG_VER}"
		for p in "${here}"/patches/xorg-server/*.patch; do
			echo "  apply $(basename "${p}")"
			git -C "${XS}" apply --whitespace=nowarn "${p}"
		done
		git -C "${XS}" add -A
		git -C "${XS}" diff --cached > "${out}/xorg-drm-full.patch"
		echo "${stamp}" > "${out}/xorg-src.stamp"
	else
		echo "  unchanged since the last build (stamp matches)"
	fi

	# --- xorg-server configure + build (static archives; the executable is linked below) ----
	deps_stamp="$(printf '%s\n' "${mesa_out}" "$(sha256sum "${LDP}/lib/libdrm.a" "${DP}/lib/libepoxy.a" | cut -c1-64)" | sha256sum | cut -c1-16)"
	if [ "$(cat "${out}/xorg-deps.stamp" 2>/dev/null || true)" != "${deps_stamp}" ]; then
		rm -rf "${XB}"   # headers/pkg-config paths of another Mesa or libdrm: reconfigure
	fi
	if [ ! -f "${XB}/build.ninja" ]; then
		echo "== xorg-server meson setup"
		meson setup "${XB}" "${XS}" --cross-file "${cross}" \
			--prefix /usr --sysconfdir /etc --localstatedir /var \
			--buildtype=debugoptimized -Db_ndebug=false -Db_staticpic=false --wrap-mode=nodownload \
			-Dxorg=true -Dxephyr=false -Dxnest=false -Dxvfb=false -Dxwin=false -Dxquartz=false \
			-Dglamor=true -Dglx=false -Ddri1=false -Ddri2=true -Ddri3=true -Ddrm=true \
			-Dudev=false -Dudev_kms=false -Dhal=false -Dsystemd_logind=false -Dpciaccess=false \
			-Dint10=false -Dvgahw=false -Ddga=false -Dagp=false -Dlinux_apm=false -Dlinux_acpi=false \
			-Dxdmcp=false -Dxdm-auth-1=false -Dsecure-rpc=false -Dxselinux=false -Dxcsecurity=false \
			-Dmitshm=false -Dxv=true -Dxvmc=false -Dxinerama=false -Dxf86-input-inputtest=false \
			-Dsha1=libmd -Dinput_thread=false -Dlisten_tcp=false -Dsuid_wrapper=false -Dlibunwind=false \
			-Ddocs=false -Ddevel-docs=false -Ddocs-pdf=false \
			-Ddefault_font_path=/usr/share/fonts/X11/misc,/usr/share/fonts/X11/75dpi \
			-Dxkb_dir=/usr/share/X11/xkb -Dxkb_output_dir=/tmp -Dlog_dir=/tmp \
			-Dfallback_input_driver=phxhid \
			-Dbuilder_string="Phoenix-RTOS new GPU lane (Xorg-drm)" \
			> "${out}/xorg-setup.log" 2>&1 || { tail -40 "${out}/xorg-setup.log"; exit 1; }
		echo "${deps_stamp}" > "${out}/xorg-deps.stamp"
	fi
	echo "== xorg-server ninja (-j${jobs}): static archives"
	# Every static library the server + the builtin modules need; the meson Xorg
	# executable itself is never linked (Xorg-drm is linked below with the modules).
	XTARGETS=$(ninja -C "${XB}" -t targets all | sed -n 's/^\([^:]*\.a\): .*/\1/p' | sort -u)
	# shellcheck disable=2086
	ninja -C "${XB}" -k 0 -j"${jobs}" ${XTARGETS} > "${out}/xorg-ninja.log" 2>&1 \
		|| { grep -B2 -A8 -E 'error|FAILED' "${out}/xorg-ninja.log" | head -60; exit 1; }
	nwarn=$(grep -c 'warning:' "${out}/xorg-ninja.log" || true)
	echo "  xorg-server archives built: ${nwarn} compiler warning line(s) (${out}/xorg-ninja.log)"
fi
[ -f "${XB}/build.ninja" ] || { echo "build.sh: no xorg build in ${XB} (run without --relink first)" >&2; exit 1; }

# --- Xorg-drm's own objects: builtin-module table, phxhid input driver, compat --------------
echo "== Xorg-drm objects"
OBJ="${out}/obj"
rm -rf "${OBJ}"
mkdir -p "${OBJ}"
# Compile exactly like the modesetting driver (a module of this server): take its command
# from meson's compile database, minus the output/dependency arguments.
mapfile -t XCFLAGS < <(python3 - "${XB}/compile_commands.json" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if e['file'].endswith('hw/xfree86/drivers/modesetting/driver.c'):
        a = shlex.split(e['command'])[1:]
        out, skip = [], False
        for x in a:
            if skip: skip = False; continue
            if x in ('-o', '-MQ', '-MF', '-c'): skip = True; continue
            if x in ('-MD',) or x.endswith('driver.c') or x.startswith('-fdiagnostics-color'): continue
            out.append(x)
        print('\n'.join(out)); break
PY
)
[ "${#XCFLAGS[@]}" -gt 10 ] || { echo "build.sh: no compile command for modesetting driver.c in ${XB}" >&2; exit 1; }
: > "${out}/xorg-drm-cc.log"
for f in xorg_drm_builtin.c phxhid.c; do
	( cd "${XB}" && "${PHXCC}" "${XCFLAGS[@]}" -Wextra -Wno-unused-parameter -Wno-sign-compare \
		-Wno-missing-field-initializers -Werror -I"${here}/src" -c "${here}/src/${f}" -o "${OBJ}/${f%.c}.o" ) \
		2>> "${out}/xorg-drm-cc.log" || { cat "${out}/xorg-drm-cc.log"; exit 1; }
done
# libphoenix gaps that only show at link time (stand-ins compiled only while libphoenix
# lacks the symbol; see compat/xorg_drm_compat.c for the list)
compat_defs=()
for fn in ${XORG_DRM_COMPAT_FNS:-}; do
	has_libc "${fn}" || compat_defs+=(-DXORG_DRM_NEED_"$(tr '[:lower:]' '[:upper:]' <<< "${fn}")")
done
"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${COMPAT_INC}" "${compat_defs[@]}" \
	-c "${here}/compat/xorg_drm_compat.c" -o "${OBJ}/xorg_drm_compat.o"
echo "  compat stand-ins: ${compat_defs[*]:-none}"

# --- link --------------------------------------------------------------------------------
echo "== link Xorg-drm"
MB="${mesa_out}/mesa-build"
MESA_A=(src/egl/libEGL.a src/gbm/libgbm.a src/gbm/backends/dri/dri_gbm.a src/mesa/glapi/es2api/libGLESv2.a
	src/mesa/glapi/shared-glapi/libglapi.a src/mesa/glapi/glapi/libglapi_bridge.a
	src/gallium/drivers/v3d/libv3d.a src/gallium/drivers/v3d/libv3d-v42.a src/gallium/drivers/v3d/libv3d-v71.a
	src/broadcom/libbroadcom-v42.a src/broadcom/libbroadcom-v71.a src/broadcom/qpu/libbroadcom_qpu.a
	src/broadcom/libv3d_neon.a src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a
	src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a src/gallium/winsys/kmsro/drm/libkmsrowinsys.a
	src/gallium/winsys/v3d/drm/libv3dwinsys.a src/gallium/winsys/vc4/drm/libvc4winsys.a
	src/gallium/winsys/sw/kms-dri/libswkmsdri.a src/gallium/winsys/sw/dri/libswdri.a
	src/util/libmesa_util.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a
	src/c11/impl/libmesa_util_c11.a)
MA=()
for a in "${MESA_A[@]}"; do [ -f "${MB}/${a}" ] && MA+=("${MB}/${a}"); done
GALLIUM_A="$(ls "${MB}"/src/gallium/targets/dri/libgallium-*.a)"
XSRV=("${XB}/hw/xfree86/libxorgserver_static.a")
XLIBC=("${XB}/os/liblibxlibc.a")   # the server's own fallbacks for functions libc lacks (timingsafe_memcmp)
XMOD=("${XB}/hw/xfree86/drivers/modesetting/libmodesetting_drv.a" "${XB}/hw/xfree86/glamor_egl/libglamoregl.a"
	"${XB}/hw/xfree86/dixmods/libshadow.a")
# The server archive goes in whole (as meson's link_whole for the Xorg executable: modules
# resolve server symbols the server itself never calls); Mesa's gallium target whole as
# for kmscube; --gc-sections drops what nobody references.
# -Wl,--wrap=mmap: libdrm-phoenix's __wrap_mmap resolves the MAP_DUMB/MMAP_BO tokens of
# modesetting's dumb buffers, GBM's and Mesa's BO maps (M3 §2.6).
"${PHXCXX}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,--wrap=mmap \
	-Wl,-Map,"${out}/Xorg-drm.map" -o "${out}/Xorg-drm" "${OBJ}"/xorg_drm_builtin.o "${OBJ}"/phxhid.o \
	-Wl,--whole-archive "${XSRV[@]}" "${GALLIUM_A}" -Wl,--no-whole-archive \
	-Wl,--start-group "${XMOD[@]}" "${XLIBC[@]}" "${DP}/lib/libepoxy.a" "${DP}/lib/libxcvt.a" "${DP}/lib/libxshmfence.a" \
	"${MA[@]}" "${LDP}/lib/libdrm.a" "${mesa_out}/compat/libmesadrm-compat.a" "${OBJ}/xorg_drm_compat.o" \
	"${B}/lib/libpixman-1.a" "${B}/lib/libXfont2.a" "${B}/lib/libfontenc.a" "${B}/lib/libfreetype.a" \
	"${B}/lib/libxkbfile.a" "${B}/lib/libXau.a" "${B}/lib/libmd.a" "${B}/lib/libz.a" \
	-Wl,--end-group -lm > "${out}/Xorg-drm-link.log" 2>&1 \
	|| { grep -v '^/.*: warning: ' "${out}/Xorg-drm-link.log" | head -80; exit 1; }
"${TC}-strip" -o "${out}/Xorg-drm-stripped" "${out}/Xorg-drm"
echo "  ${out}/Xorg-drm: $(stat -c %s "${out}/Xorg-drm") bytes; stripped $(stat -c %s "${out}/Xorg-drm-stripped") bytes"
[ -s "${out}/Xorg-drm-link.log" ] && sed 's/^/  link: /' "${out}/Xorg-drm-link.log" | sort -u | head -20

# --- verification ------------------------------------------------------------------------
echo "== verify"
"${TC}-size" "${out}/Xorg-drm" | sed 's/^/  /'
und="$("${TC}-nm" -u "${out}/Xorg-drm" || true)"
echo "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && sed 's/^/    /' <<< "${und}" | head -20
syms="$("${TC}-nm" "${out}/Xorg-drm")"
for s in modesettingModuleData glamoreglModuleData shadowModuleData phxhidModuleData xf86BuiltinModules \
		LoaderBuiltinFind glamor_egl_init glamor_init ms_present_screen_init dri3_screen_init \
		present_screen_init __wrap_mmap drmPhoenixMmap drm_phoenix_ioctl gbmint_get_backend \
		kmsro_drm_screen_create v3d_drm_screen_create_renderonly epoxy_static_proc_address \
		xshmfence_map_shm libxcvt_gen_mode_info ReadFdFromClient WriteFdToClient _XSERVTransRecvFd; do
	if grep -qE " [TtDdRrBbWw] ${s}\$" <<< "${syms}"; then echo "  symbol ${s}: yes"; else echo "  symbol ${s}: NO"; fi
done
# DRI3 (open, PixmapFromBuffers, FenceFromFD) passes descriptors over the X socket: xtrans must
# have been built with fd passing.
grep -q '^#define XTRANS_SEND_FDS 1' "${XB}/include/dix-config.h" \
	|| { echo "build.sh: xorg-server built without XTRANS_SEND_FDS (DRI3 fd passing)" >&2; exit 1; }
echo "  XTRANS_SEND_FDS: 1"
if [ -n "${shmf_prefix}" ]; then
	# The Phoenix-RTOS xshmfence backend (G16), not upstream's pthread one: which archive members linked.
	nphx=$(grep -c 'libxshmfence.a(xshmfence_phoenix.o)' "${out}/Xorg-drm.map" || true)
	npth=$(grep -c 'libxshmfence.a(xshmfence_pthread.o)' "${out}/Xorg-drm.map" || true)
	echo "  xshmfence members: phoenix=${nphx} pthread=${npth} (want >0 / 0)"
	{ [ "${nphx}" -gt 0 ] && [ "${npth}" = 0 ]; } || { echo "build.sh: wrong xshmfence backend linked" >&2; exit 1; }
fi
strs="$(strings -a "${out}/Xorg-drm-stripped")"
for s in 'modesetting' 'glamor' 'PHXHID dev=' 'linked into the server' 'builtin keymap' \
		/dev/dri/card0 /dev/dri/renderD128 /kmsbuf 'libdrm-phoenix:' DRMPHX_TRACE kmsro 'V3D 4.2' \
		EGL_KHR_platform_gbm EGL_MESA_platform_gbm 'DRI3' 'Present' 'X.Org X Server' 'Xorg-drm'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  strings '${s}': ${n}"
done
bad=0
# The old lane's X server (kdrive Xphoenix + fbdev DDX + glamor shim) and in-process winsys
for s in Xphoenix '[fbdev]' fbdevKeyboardDriver fbdevMouseDriver 'FBCONSETMODE(' glamor_phoenix phxgl \
		'v3d-winsys:' phoenix_v3d_ioctl peek_next_scanout v3d-srv /dev/v3d-srv; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  old-lane string '${s}': ${n}"
	[ "${n}" = 0 ] || bad=1
done
# xorg-server patch 0008: without libpciaccess xf86PostProbe() must not abort a framebuffer-slot
# (legacy Probe) screen -- the m4a failure; the FatalError string is then dead code and gone.
n=$(grep -cF -- 'Cannot run in framebuffer mode' <<< "${strs}" || true)
echo "  fb-slot abort string (must be 0, patch 0008): ${n}"
[ "${n}" = 0 ] || bad=1
if grep -qE ' [Tt] dlopen$' <<< "${syms}"; then
	echo "  note: dlopen is linked (the loader's fallback for modules outside the builtin table; libepoxy never calls it)"
fi
[ "${bad}" = 0 ] || { echo "build.sh: verification failed (see above)" >&2; exit 1; }
sha256sum "${out}/Xorg-drm" "${out}/Xorg-drm-stripped" | sed "s|${out}/||; s/^/  /"
echo "done"
