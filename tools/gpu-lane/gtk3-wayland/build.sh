#!/usr/bin/env bash
#
# gtk3-wayland (new GPU lane, M7): GTK 3.24 with ONLY the Wayland GDK backend,
# cross-built STATIC for aarch64-phoenix, with the libraries it needs that the ports
# prefix lacks or has too old, plus gtk-layer-shell (for the XFCE panel/desktop) and
# a test program, gtk3-hello.
#
#   pcre2 10.47 (BSD)            GLib >= 2.74 needs PCRE2; ports has only PCRE1
#   GLib 2.88.3 (LGPL) + GIO     ports GLib is 2.56.4 without GIO; GTK needs >= 2.57.2 + GIO
#   fribidi 1.0.16 (LGPL)        pango, GTK
#   atk 2.38.0 (LGPL)            GTK 3 still links ATK (no at-spi bridge: X11 only)
#   gdk-pixbuf 2.42.12 (LGPL)    PNG + JPEG loaders built in (no loader modules)
#   harfbuzz 14.4.0 (MIT)        = the ports release, rebuilt with meson: no C++ runtime needed
#   pango 1.54.0 (LGPL)          the last pango that accepts the ports fontconfig 2.14
#   cairo 1.18.4 (LGPL/MPL)      ports cairo 1.16 has no PDF/PS surfaces (GTK prints) nor cairo-gobject
#   GTK 3.24.52 (LGPL)           Wayland backend only; no X11/broadway, no introspection,
#                                no print backends, no colord/cloudproviders/tracker
#   gtk-layer-shell 0.10.1 (MIT) layer-shell for GTK 3 (xfce4-panel, xfdesktop on Wayland)
#
# Reused, not rebuilt: the M6 Wayland client stack (libwayland 1.24 client/cursor/egl,
# wayland-protocols 1.45, libxkbcommon 1.7, the compat layer: epoll & co. over poll,
# memfd_create over shmsrv) from a weston-drm build-out, and libepoxy 1.5.10 (static-EGL
# dispatch) from xorg-drm -- both SNAPSHOTTED into <out> so a rebuild there cannot
# change this build. From the ports prefix, through private per-library views:
# fontconfig 2.14, freetype, pixman, libpng16, libjpeg, libffi, expat, zlib, libiconv.
#
#   <out>/dl/          pinned tarballs (sha256 below)
#   <out>/src/<pkg>/   extracted + patches/<pkg>/*.patch (git apply, one commit each)
#   <out>/deps/        private views of the ports prefix + the snapshots
#   <out>/prefix/      everything built here: static .a, headers, *.pc (the XFCE
#                      follow-up builds on it with <out>/phoenix-aarch64-wl.cross and
#                      <out>/pkg-config-phoenix)
#   <out>/gtk3-hello[-stripped]
#
# Writes only into <out> (default build-out/, gitignored). Reads the tree sysroot, the
# ports prefix, the toolchain, the E7 compiler wrappers, the snapshot sources. No Pi,
# no rebuild-rpi4b-fast.sh, no /srv. LGPL/GPL sources live only here (fetched,
# sha256-pinned tarballs), never in sources/.
#
# Host tools: meson, ninja, cmake, wayland-scanner 1.24.0, python3, and GLib's host
# tools (glib-compile-resources, glib-mkenums, glib-genmarshal, gdbus-codegen,
# glib-compile-schemas; the host has 2.88.0 = the target GLib series),
# gtk-update-icon-cache/gtk-encode-symbolic-svg are NOT needed (GTK's cross build
# ships its pre-rendered icons).
#
# Usage: tools/gpu-lane/gtk3-wayland/build.sh [--clean] [--out <dir>] [-j N] [--usr]
#            [--wayland-prefix <dir>] [--epoxy-prefix <dir>] [--relink]
#   --relink   skip the libraries; relink gtk3-hello only
#   --usr      configure every package for the TARGET's paths (--prefix /usr --sysconfdir /etc
#              --localstatedir /var) and install with DESTDIR=<out>/destdir: the library set then
#              lives in <out>/destdir/usr (instead of <out>/prefix) and the binaries carry
#              /usr, /etc paths instead of build-host ones. pkg-config-phoenix runs with
#              --define-prefix (each .pc's prefix = the directory it was found in). The XFCE
#              libraries (tools/gpu-lane/xfce-wayland) build on such a prefix: libxfce4util,
#              xfconf and garcon read their compiled-in /etc/xdg and /usr/share paths.
#              Use it with a NEW --out (the default build-out/ is what m7e's binaries came from).
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
jobs="$(nproc)"
clean=0
relink=0
usr=0
wl_src_prefix="${root}/tools/gpu-lane/weston-drm/build-out-g6/prefix"
epoxy_src_prefix="${root}/tools/gpu-lane/xorg-drm/build-out/deps-prefix"
egl_hdr_prefix="${root}/tools/gpu-lane/mesa-drm/build-out-wayland/prefix"
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--relink) relink=1 ;;
		--usr) usr=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		--wayland-prefix) shift; wl_src_prefix="${1:?}" ;;
		--wayland-prefix=*) wl_src_prefix="${1#--wayland-prefix=}" ;;
		--epoxy-prefix) shift; epoxy_src_prefix="${1:?}" ;;
		--epoxy-prefix=*) epoxy_src_prefix="${1#--epoxy-prefix=}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac

if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCC="${here}/bin/phx-gcc"      # drops -pthread, also inside @response files
PHXCXX="${here}/bin/phx-g++"
COMPAT_INC="${root}/tools/gpu-lane/weston-drm/compat/include"      # M6: epoll/timerfd/signalfd/memfd...
# (mesa-drm's compat/include is NOT used: libphoenix has had what it shims since build 10,
# and its duplicate declarations break -Werror=redundant-decls builds such as pango's.)
if [ "${usr}" = 1 ]; then
	DESTDIR_="${out}/destdir"
	P="${DESTDIR_}/usr"          # where the installed files are, on the build host
	PREFIX_ARGS=(--prefix /usr --sysconfdir /etc --localstatedir /var)
	INSTALL_ENV=(env DESTDIR="${DESTDIR_}")
	CMAKE_PREFIX=/usr
	PKGC_DEFINE_PREFIX=" --define-prefix"
else
	P="${out}/prefix"
	PREFIX_ARGS=(--prefix "${P}")
	INSTALL_ENV=()
	CMAKE_PREFIX="${P}"
	PKGC_DEFINE_PREFIX=""
fi
D="${out}/deps"
SYSD="${D}/sys"      # libintl stub + libiconv: on every compile/link of this build (meson's
                     # builtin intl/iconv dependencies look for them as system libraries)

# name|file|url|sha256
PKGS=(
	"pcre2|pcre2-10.47.tar.bz2|https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.47/pcre2-10.47.tar.bz2|47fe8c99461250d42f89e6e8fdaeba9da057855d06eb7fc08d9ca03fd08d7bc7"
	"glib|glib-2.88.3.tar.xz|https://download.gnome.org/sources/glib/2.88/glib-2.88.3.tar.xz|ab24d24e698dfa1e408b7bcdb508f4aafc906185a8b8ce72fdf79bbbdc9b383b"
	"fribidi|fribidi-1.0.16.tar.xz|https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz|1b1cde5b235d40479e91be2f0e88a309e3214c8ab470ec8a2744d82a5a9ea05c"
	"atk|atk-2.38.0.tar.xz|https://download.gnome.org/sources/atk/2.38/atk-2.38.0.tar.xz|ac4de2a4ef4bd5665052952fe169657e65e895c5057dffb3c2a810f6191a0c36"
	"gdk-pixbuf|gdk-pixbuf-2.42.12.tar.xz|https://download.gnome.org/sources/gdk-pixbuf/2.42/gdk-pixbuf-2.42.12.tar.xz|b9505b3445b9a7e48ced34760c3bcb73e966df3ac94c95a148cb669ab748e3c7"
	"harfbuzz|harfbuzz-14.4.0.tar.xz|https://github.com/harfbuzz/harfbuzz/releases/download/14.4.0/harfbuzz-14.4.0.tar.xz|2357ed966c6ced7bfa720b0640c0231065af01158fbea215093ffa15aed44371"
	"pango|pango-1.54.0.tar.xz|https://download.gnome.org/sources/pango/1.54/pango-1.54.0.tar.xz|8a9eed75021ee734d7fc0fdf3a65c3bba51dfefe4ae51a9b414a60c70b2d1ed8"
	"cairo|cairo-1.18.4.tar.xz|https://cairographics.org/releases/cairo-1.18.4.tar.xz|445ed8208a6e4823de1226a74ca319d3600e83f6369f99b14265006599c32ccb"
	"gtk|gtk-3.24.52.tar.xz|https://download.gnome.org/sources/gtk/3.24/gtk-3.24.52.tar.xz|80931fa472a77b9a164f6740e3c0b444fac6770054632d35a7ff9d679e5e7b9f"
	"gtk-layer-shell|gtk-layer-shell-0.10.1.tar.gz|https://github.com/wmww/gtk-layer-shell/archive/refs/tags/v0.10.1.tar.gz|88c3a3e0a5300532f3d368d5df64838a87f1fb85273f22d41df0a6b8d0ec59c6"
)
WAYLAND_VERSION=1.24.0

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-gcc-ar" "${TC}-nm" "${TC}-strip" "${PHXCC}" "${PHXCXX}" \
		"${B}/lib/libfontconfig.a" "${B}/lib/libfreetype.a" \
		"${B}/lib/libpixman-1.a" "${B}/lib/libpng16.a" "${B}/lib/libjpeg.a" "${B}/lib/libffi.a" "${B}/lib/libexpat.a" \
		"${B}/lib/libz.a" "${B}/lib/libiconv.a" \
		"${wl_src_prefix}/lib/libwayland-client.a" "${wl_src_prefix}/lib/libwlphx-compat.a" \
		"${wl_src_prefix}/lib/libxkbcommon.a" "${epoxy_src_prefix}/lib/libepoxy.a" "${egl_hdr_prefix}/include/EGL/egl.h"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
for t in meson ninja cmake wayland-scanner python3 glib-compile-resources glib-mkenums glib-genmarshal gdbus-codegen \
		glib-compile-schemas; do
	command -v "${t}" > /dev/null || { echo "build.sh: host tool ${t} not found" >&2; exit 1; }
done
[ "$(wayland-scanner --version 2>&1 | awk '{print $2}')" = "${WAYLAND_VERSION}" ] \
	|| { echo "build.sh: host wayland-scanner is not ${WAYLAND_VERSION}" >&2; exit 1; }

mkdir -p "${out}/dl" "${out}/src" "${P}/lib/pkgconfig" "${P}/include" "${D}" "${SYSD}/include" "${SYSD}/lib"
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

# --- sources -----------------------------------------------------------------------------
fetch_extract() {  # name -> ${out}/src/<name>, patched
	local name="$1" rec file url sum dir stamp
	for rec in "${PKGS[@]}"; do
		[ "${rec%%|*}" = "${name}" ] && break
	done
	IFS='|' read -r _ file url sum <<< "${rec}"
	if [ ! -f "${out}/dl/${file}" ]; then
		echo "  fetch ${file}"
		curl -sSfL -o "${out}/dl/${file}.part" "${url}"
		mv "${out}/dl/${file}.part" "${out}/dl/${file}"
	fi
	echo "${sum}  ${out}/dl/${file}" | sha256sum -c --quiet - || { echo "build.sh: ${file}: sha256 mismatch" >&2; exit 1; }
	dir="${out}/src/${name}"
	stamp="$( { echo "${sum}"; cat "${here}/patches/${name}"/*.patch 2>/dev/null || true; } | sha256sum | cut -c1-16)"
	if [ "$(cat "${dir}.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		rm -rf "${dir}" "${dir}.tmp"
		mkdir -p "${dir}.tmp"
		tar -xf "${out}/dl/${file}" -C "${dir}.tmp" --strip-components=1
		mv "${dir}.tmp" "${dir}"
		# Its own git repository: `git apply` inside a directory of ANOTHER repository
		# (this one) silently skips every path.
		git -C "${dir}" init -q
		git -C "${dir}" add -A -f
		git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "${file}"
		for p in "${here}/patches/${name}"/*.patch; do
			[ -e "${p}" ] || continue
			echo "  apply ${name}/$(basename "${p}")"
			# git am keeps each patch's own message, so `git format-patch` in the tree
			# regenerates patches/<pkg>/ unchanged
			git -C "${dir}" -c user.name=build -c user.email=build@invalid am -q --whitespace=nowarn "${p}"
		done
		git -C "${dir}" diff "$(git -C "${dir}" rev-list --max-parents=0 HEAD)" > "${out}/${name}-full.patch"
		echo "${stamp}" > "${dir}.stamp"
		rm -f "${out}/${name}.built"   # a changed source rebuilds the package
	fi
}

# --- private views of the ports prefix ------------------------------------------------------
# view name version "cflags-subdirs" "libs" "requires" "requires.private" inc:<path>|lib:<path>...
view() {
	local name="$1" ver="$2" subs="$3" libs="$4" req="$5" reqp="$6" it cf="" s
	shift 6
	rm -rf "${D:?}/${name}"
	mkdir -p "${D}/${name}/include" "${D}/${name}/lib/pkgconfig"
	for it in "$@"; do
		case "${it}" in
			inc:*) mkdir -p "$(dirname "${D}/${name}/include/${it#inc:}")"
				cp -a "${B}/include/${it#inc:}" "${D}/${name}/include/${it#inc:}" ;;
			lib:*) mkdir -p "$(dirname "${D}/${name}/lib/${it#lib:}")"
				cp -a "${B}/lib/${it#lib:}" "${D}/${name}/lib/${it#lib:}" ;;
		esac
	done
	for s in ${subs}; do
		case "${s}" in
			.) cf="${cf} -I\${prefix}/include" ;;
			*) cf="${cf} -I\${prefix}/include/${s}" ;;
		esac
	done
	printf '%s\n' "prefix=${D}/${name}" "Name: ${name}" "Description: ${name} from the Phoenix ports prefix" \
		"Version: ${ver}" "Requires: ${req}" "Requires.private: ${reqp}" "Libs: -L\${prefix}/lib ${libs}" \
		"Cflags:${cf}" > "${D}/${name}/lib/pkgconfig/${name}.pc"
}
pc_alias() {  # alias-name view version requires
	printf '%s\n' "Name: $1" "Description: alias within the $2 view" "Version: $3" "Requires: $4" "Libs:" "Cflags:" \
		> "${D}/$2/lib/pkgconfig/$1.pc"
}
pcver() { sed -n 's/^Version: *//p' "${B}/lib/pkgconfig/$1.pc"; }

# --- meson cross files + pkg-config ---------------------------------------------------------
write_cross() {
	local pkgc="${out}/pkg-config-phoenix" v libdirs=""
	for v in zlib libffi expat pixman-1 libpng16 libjpeg freetype2 fontconfig wayland epoxy; do
		libdirs="${libdirs}:${D}/${v}/lib/pkgconfig"
	done
	cat > "${pkgc}" <<EOF
#!/bin/sh
# pkg-config restricted to this build's prefix, the private ports views and the snapshots.
export PKG_CONFIG_LIBDIR=${P}/lib/pkgconfig:${P}/share/pkgconfig${libdirs}:${D}/wayland/share/pkgconfig
unset PKG_CONFIG_PATH
exec /usr/bin/pkg-config --static${PKGC_DEFINE_PREFIX} "\$@"
EOF
	chmod +x "${pkgc}"
	local flags="'--sysroot=${S}/', '-B${S}/lib/', '-mcpu=cortex-a72', '-mtune=cortex-a72', '-mstrict-align', '-mno-outline-atomics', '-ffunction-sections', '-fdata-sections', '-I${SYSD}/include'"
	local lflags="'--sysroot=${S}/', '-B${S}/lib/', '-L${SYSD}/lib', '-Wl,-z,max-page-size=0x1000'"
	# GTK and gtk-layer-shell (Wayland clients) additionally see the M6 compat layer
	# (memfd_create over shmsrv, epoll & co.) -- never GLib: its configure would find the
	# emulated eventfd/epoll headers and build its main loop on them.
	# (-I the Wayland snapshot too: GTK's configure looks for <linux/input.h> -- the M6 shim
	# over FreeBSD's evdev codes -- with the base flags only.)
	local flags_wl="'--sysroot=${S}/', '-B${S}/lib/', '-mcpu=cortex-a72', '-mtune=cortex-a72', '-mstrict-align', '-mno-outline-atomics', '-ffunction-sections', '-fdata-sections', '-I${SYSD}/include', '-I${COMPAT_INC}', '-I${D}/wayland/include'"
	local lflags_wl="${lflags}, '-Wl,-u,__wrap_close', '-Wl,-u,__wrap_write', '${D}/wayland/lib/libwlphx-compat.a', '-Wl,--wrap=close', '-Wl,--wrap=write'"
	local c
	# GTK itself: the wl flags + the built-in keymap header (GTK patch 0003)
	local flags_gtk="${flags_wl}, '-DGDK_WAYLAND_BUILTIN_XKB_KEYMAP_H=\"${D}/gdk_builtin_keymap.h\"'"
	for c in "" -wl -gtk; do
		local f="${flags}" l="${lflags}"
		[ "${c}" = -wl ] && { f="${flags_wl}"; l="${lflags_wl}"; }
		[ "${c}" = -gtk ] && { f="${flags_gtk}"; l="${lflags_wl}"; }
		cat > "${out}/phoenix-aarch64${c}.cross" <<EOF
# Generated by tools/gpu-lane/gtk3-wayland/build.sh (aarch64-phoenix, Pi 4).
[binaries]
c = '${PHXCC}'
cpp = '${PHXCXX}'
ar = '${TC}-gcc-ar'
nm = '${TC}-nm'
strip = '${TC}-strip'
objcopy = '${TC}-objcopy'
pkg-config = '${pkgc}'
glib-compile-resources = '$(command -v glib-compile-resources)'
glib-compile-schemas = '$(command -v glib-compile-schemas)'
glib-mkenums = '$(command -v glib-mkenums)'
glib-genmarshal = '$(command -v glib-genmarshal)'
gdbus-codegen = '$(command -v gdbus-codegen)'
wayland-scanner = '$(command -v wayland-scanner)'

[host_machine]
system = 'phoenix'
cpu_family = 'aarch64'
cpu = 'cortex-a72'
endian = 'little'

[properties]
needs_exe_wrapper = true
# GLib's run-time probes (cross answers). The printf family is libphoenix's: GLib's
# gnulib replacement needs frexpl(), which libphoenix lacks. libphoenix's vsnprintf
# is C99 (returns the full length for a short buffer); positional %1\$s arguments
# appear only in translations, and this build has none (no NLS).
have_c99_vsnprintf = true
have_c99_snprintf = true
have_unix98_printf = true
growing_stack = false
va_val_copy = true
have_strlcpy = true
have_proc_self_cmdline = false

[built-in options]
c_args = [${f}]
cpp_args = [${f}]
c_link_args = [${l}]
cpp_link_args = [${l}]
default_library = 'static'
EOF
	done
}

meson_pkg() {  # [--cross <file>] name builddir-name meson-args...
	local cross="${out}/phoenix-aarch64.cross"
	if [ "$1" = --cross ]; then cross="$2"; shift 2; fi
	local name="$1" bname="$2"
	shift 2
	local bd="${out}/${bname}"
	if [ -f "${out}/${name}.built" ]; then
		echo "  ${name}: up to date"
		return 0
	fi
	rm -rf "${bd}"
	meson setup "${bd}" "${out}/src/${name}" --cross-file "${cross}" "${PREFIX_ARGS[@]}" \
		--libdir lib --buildtype=debugoptimized -Db_staticpic=false -Db_ndebug=if-release --wrap-mode=nodownload "$@" \
		> "${out}/${bname}-setup.log" 2>&1 || { tail -40 "${out}/${bname}-setup.log"; exit 1; }
	ninja -C "${bd}" -j"${jobs}" > "${out}/${bname}-ninja.log" 2>&1 || { grep -E -A8 'error|FAILED' "${out}/${bname}-ninja.log" | head -80; exit 1; }
	"${INSTALL_ENV[@]}" ninja -C "${bd}" install > "${out}/${bname}-install.log" 2>&1 || { tail -20 "${out}/${bname}-install.log"; exit 1; }
	echo "  ${name}: built ($(grep -c 'warning:' "${out}/${bname}-ninja.log" || true) warning line(s))"
	touch "${out}/${name}.built"
}

# GLib's .pc files name its tools under ${bindir} (target binaries here); consumers that
# read them (pkg-config --variable) must get the host's tools.
fix_glib_pc() {
	local pc v
	for pc in "${P}/lib/pkgconfig/glib-2.0.pc" "${P}/lib/pkgconfig/gio-2.0.pc"; do
		[ -f "${pc}" ] || continue
		for v in glib_genmarshal gobject_query glib_mkenums glib_compile_schemas glib_compile_resources gdbus_codegen; do
			sed -i "s|^${v}=.*|${v}=$(command -v "$(echo "${v}" | tr _ -)" || echo /bin/false)|" "${pc}"
		done
	done
}

if [ "${relink}" = 0 ]; then
	echo "== sources"
	for rec in "${PKGS[@]}"; do fetch_extract "${rec%%|*}"; done

	echo "== dependency views (ports prefix) + snapshots"
	view zlib "$(sed -n 's/^#define ZLIB_VERSION "\(.*\)"/\1/p' "${B}/include/zlib.h")" . -lz "" "" inc:zlib.h inc:zconf.h lib:libz.a
	view libffi "$(pcver libffi)" . -lffi "" "" inc:ffi.h inc:ffitarget.h lib:libffi.a
	view expat "$(pcver expat)" . -lexpat "" "" inc:expat.h inc:expat_config.h inc:expat_external.h lib:libexpat.a
	view pixman-1 "$(pcver pixman-1)" pixman-1 -lpixman-1 "" "" inc:pixman-1 lib:libpixman-1.a
	view libpng16 "$(pcver libpng16)" libpng16 -lpng16 "" zlib inc:libpng16 lib:libpng16.a
	pc_alias libpng libpng16 "$(pcver libpng16)" libpng16
	view libjpeg "$(pcver libjpeg)" . -ljpeg "" "" inc:jpeglib.h inc:jconfig.h inc:jerror.h inc:jmorecfg.h lib:libjpeg.a
	view freetype2 "$(pcver freetype2)" freetype2 -lfreetype "" "" inc:freetype2 lib:libfreetype.a
	view fontconfig "$(pcver fontconfig)" . -lfontconfig freetype2 expat inc:fontconfig lib:libfontconfig.a
	# libiconv + the libintl stub: system-library style (meson's builtin deps look there)
	cp "${B}/include/iconv.h" "${B}/include/libcharset.h" "${B}/include/localcharset.h" "${SYSD}/include/"
	cp "${B}/lib/libiconv.a" "${SYSD}/lib/"
	cp "${here}/src/intl/libintl.h" "${SYSD}/include/"
	mkdir -p "${out}/obj"
	"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SYSD}/include" \
		-c "${here}/src/intl/intl_stub.c" -o "${out}/obj/intl_stub.o"
	rm -f "${SYSD}/lib/libintl.a"
	"${TC}-gcc-ar" rcs "${SYSD}/lib/libintl.a" "${out}/obj/intl_stub.o"
	# resolver headers + stand-ins for GIO's DNS record queries (src/resolv/)
	mkdir -p "${SYSD}/include/arpa"
	cp "${here}/src/resolv/resolv.h" "${SYSD}/include/"
	cp "${here}/src/resolv/arpa/nameser.h" "${SYSD}/include/arpa/"
	"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SYSD}/include" \
		-c "${here}/src/resolv/resolv_stub.c" -o "${out}/obj/resolv_stub.o"
	rm -f "${SYSD}/lib/libresolv.a"
	"${TC}-gcc-ar" rcs "${SYSD}/lib/libresolv.a" "${out}/obj/resolv_stub.o"

	# The M6 Wayland client stack (snapshot).
	rm -rf "${D}/wayland"
	mkdir -p "${D}/wayland/lib/pkgconfig" "${D}/wayland/share"
	cp -a "${wl_src_prefix}/include" "${D}/wayland/"
	for l in libwayland-client.a libwayland-cursor.a libwayland-egl.a libxkbcommon.a libwlphx-compat.a; do
		cp -a "${wl_src_prefix}/lib/${l}" "${D}/wayland/lib/"
	done
	cp -a "${wl_src_prefix}/share/pkgconfig" "${wl_src_prefix}/share/wayland-protocols" "${D}/wayland/share/"
	[ -d "${wl_src_prefix}/share/wayland" ] && cp -a "${wl_src_prefix}/share/wayland" "${D}/wayland/share/"
	for pc in wayland-client wayland-cursor wayland-egl wayland-egl-backend xkbcommon wlphx-compat; do
		sed "s|${wl_src_prefix}|${D}/wayland|g" "${wl_src_prefix}/lib/pkgconfig/${pc}.pc" > "${D}/wayland/lib/pkgconfig/${pc}.pc"
	done
	sed -i "s|${wl_src_prefix}|${D}/wayland|g" "${D}/wayland/share/pkgconfig/wayland-protocols.pc"
	# libepoxy 1.5.10 (xorg-drm, static-EGL dispatch; snapshot) + the Khronos EGL headers
	# it includes, and the no-EGL stand-in (src/gtkphx_noegl.c) as its default EGL.
	rm -rf "${D}/epoxy"
	mkdir -p "${D}/epoxy/lib/pkgconfig" "${D}/epoxy/include"
	cp -a "${epoxy_src_prefix}/include/epoxy" "${D}/epoxy/include/"
	cp -a "${egl_hdr_prefix}/include/EGL" "${egl_hdr_prefix}/include/KHR" "${D}/epoxy/include/"
	cp -a "${epoxy_src_prefix}/lib/libepoxy.a" "${D}/epoxy/lib/"
	"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -c "${here}/src/gtkphx_noegl.c" -o "${out}/obj/gtkphx_noegl.o"
	rm -f "${D}/epoxy/lib/libgtkphx-noegl.a"
	"${TC}-gcc-ar" rcs "${D}/epoxy/lib/libgtkphx-noegl.a" "${out}/obj/gtkphx_noegl.o"
	printf '%s\n' "prefix=${D}/epoxy" "epoxy_has_glx=0" "epoxy_has_egl=1" "epoxy_has_wgl=0" "Name: epoxy" \
		"Description: libepoxy 1.5.10 (xorg-drm static-EGL build) + no-EGL stand-in" "Version: 1.5.10" \
		"Libs: -L\${prefix}/lib -lepoxy -lgtkphx-noegl" "Cflags: -I\${prefix}/include -DEGL_NO_X11" \
		> "${D}/epoxy/lib/pkgconfig/epoxy.pc"
	# GDK's default keymap before (or without) a wl_keyboard: evdev/pc105/us compiled on the
	# build host (the M6 weston-drm build's keymap-us.xkb; Phoenix has no xkeyboard-config).
	# GTK patch 0003 includes it when GDK_WAYLAND_BUILTIN_XKB_KEYMAP_H names this header.
	km="$(dirname "${wl_src_prefix}")/keymap-us.xkb"
	grep -q 'xkb_keymap' "${km}" 2>/dev/null || { echo "build.sh: ${km} is not a keymap (build weston-drm first)" >&2; exit 1; }
	cp "${km}" "${D}/keymap-us.xkb"
	python3 - "${D}/keymap-us.xkb" "${D}/gdk_builtin_keymap.h" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
with open(dst, 'w') as f:
    f.write('/* Generated by tools/gpu-lane/gtk3-wayland/build.sh from keymap-us.xkb\n')
    f.write(' * (xkbcli compile-keymap --rules evdev --model pc105 --layout us). */\n')
    f.write('static const char gdk_wayland_builtin_xkb_keymap[] =\n')
    for line in open(src).read().splitlines():
        f.write('\t"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"\n')
    f.write('\t;\n')
PY
	{ echo "wayland: ${wl_src_prefix}"; echo "epoxy: ${epoxy_src_prefix}"; echo "EGL headers: ${egl_hdr_prefix}"
	  echo "keymap: ${km}"; sha256sum "${D}/keymap-us.xkb" | sed "s|${D}/||"
	  sha256sum "${D}"/wayland/lib/*.a "${D}/epoxy/lib/libepoxy.a" | sed "s|${D}/||"; } > "${out}/snapshots.txt"

	write_cross
fi

# --- libraries -------------------------------------------------------------------------------
if [ "${relink}" = 0 ]; then
	echo "== pcre2 (8-bit only, no JIT; cmake)"
	if [ ! -f "${out}/pcre2.built" ]; then
		rm -rf "${out}/pcre2-build"
		cmake -S "${out}/src/pcre2" -B "${out}/pcre2-build" -G Ninja \
			-DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
			-DCMAKE_C_COMPILER="${TC}-gcc" -DCMAKE_AR="${TC}-gcc-ar" -DCMAKE_RANLIB="${TC}-gcc-ranlib" \
			-DCMAKE_C_FLAGS="${TFLAGS[*]} -O2 -g" -DCMAKE_EXE_LINKER_FLAGS="--sysroot=${S}/ -B${S}/lib/" \
			-DCMAKE_INSTALL_PREFIX="${CMAKE_PREFIX}" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release \
			-DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DPCRE2_BUILD_PCRE2_8=ON -DPCRE2_BUILD_PCRE2_16=OFF \
			-DPCRE2_BUILD_PCRE2_32=OFF -DPCRE2_SUPPORT_JIT=OFF -DPCRE2_SUPPORT_UNICODE=ON -DPCRE2_BUILD_PCRE2GREP=OFF \
			-DPCRE2_BUILD_TESTS=OFF -DPCRE2_SUPPORT_LIBBZ2=OFF -DPCRE2_SUPPORT_LIBZ=OFF -DPCRE2_SUPPORT_LIBEDIT=OFF \
			-DPCRE2_SUPPORT_LIBREADLINE=OFF -DPCRE2_STATIC_PIC=OFF \
			> "${out}/pcre2-setup.log" 2>&1 || { tail -30 "${out}/pcre2-setup.log"; exit 1; }
		"${INSTALL_ENV[@]}" ninja -C "${out}/pcre2-build" -j"${jobs}" install > "${out}/pcre2-ninja.log" 2>&1 \
			|| { grep -E -A6 'error|FAILED' "${out}/pcre2-ninja.log" | head -40; exit 1; }
		[ -f "${P}/lib/pkgconfig/libpcre2-8.pc" ] || { echo "build.sh: pcre2 installed no libpcre2-8.pc" >&2; exit 1; }
		touch "${out}/pcre2.built"
	fi

	echo "== GLib 2.88 + GIO (no NLS, no xattr/libmount/selinux, no introspection)"
	meson_pkg glib glib-build -Dnls=disabled -Dlibmount=disabled -Dselinux=disabled -Dxattr=false \
		-Dlibelf=disabled -Dsysprof=disabled -Dintrospection=disabled -Dtests=false -Dinstalled_tests=false \
		-Ddocumentation=false -Dman-pages=disabled -Ddtrace=disabled -Dsystemtap=disabled \
		-Dbsymbolic_functions=false -Dglib_debug=disabled -Dfile_monitor_backend=auto
	fix_glib_pc
fi

if [ "${relink}" = 0 ]; then
	echo "== fribidi"
	meson_pkg fribidi fribidi-build -Ddocs=false -Dbin=false -Dtests=false

	echo "== atk (no introspection)"
	meson_pkg atk atk-build -Dintrospection=false -Ddocs=false

	echo "== gdk-pixbuf (PNG + JPEG loaders built in; sniffing by loader signatures, not GIO)"
	meson_pkg gdk-pixbuf gdk-pixbuf-build -Dpng=enabled -Djpeg=enabled -Dtiff=disabled -Dgif=disabled \
		-Dothers=disabled -Dbuiltin_loaders=png,jpeg -Dintrospection=disabled -Dman=false -Dgtk_doc=false \
		-Ddocs=false -Dtests=false -Dinstalled_tests=false -Dgio_sniffing=false -Drelocatable=false

	# The same release as the ports HarfBuzz, rebuilt: the ports (CMake) objects reference
	# __gxx_personality_v0, so every C program linking them would need libstdc++ (whose
	# hypotf stub then collides with libphoenix libm). HarfBuzz's meson build is
	# -fno-exceptions/-fno-rtti and links from C with no C++ runtime; it also gives hb-glib.
	echo "== harfbuzz 14.4 (freetype + glib; no C++ runtime)"
	meson_pkg harfbuzz harfbuzz-build -Dfreetype=enabled -Dglib=enabled -Dgobject=disabled -Dcairo=disabled \
		-Dchafa=disabled -Dpng=disabled -Dzlib=disabled -Dicu=disabled -Dgraphite=disabled -Dgraphite2=disabled \
		-Dfontations=disabled -Dharfrust=disabled -Dkbts=disabled -Dwasm=disabled -Draster=disabled \
		-Dvector=disabled -Dgpu=disabled -Dgpu_demo=disabled -Dsubset=disabled -Dtests=disabled \
		-Dintrospection=disabled -Ddocs=disabled -Ddoc_tests=false -Dutilities=disabled -Dbenchmark=disabled
	und="$("${TC}-nm" -u "${P}/lib/libharfbuzz.a" | grep -E '__gxx_personality|_Znw|_Zdl|__cxa' || true)"
	[ -z "${und}" ] || { echo "build.sh: libharfbuzz.a still needs the C++ runtime:"; echo "${und}" | sort -u | head; exit 1; }

	# cairo 1.18 rebuilt here: the ports cairo 1.16 has no PDF/PS surfaces (GTK's print
	# operation code includes cairo-pdf.h/cairo-ps.h unconditionally) and no cairo-gobject.
	echo "== cairo 1.18 (image, png, ft, fc, pdf/ps/svg, gobject; no xlib/xcb)"
	meson_pkg cairo cairo-build -Dfontconfig=enabled -Dfreetype=enabled -Dpng=enabled -Dzlib=enabled \
		-Dglib=enabled -Dxcb=disabled -Dxlib=disabled -Dxlib-xcb=disabled -Dquartz=disabled -Ddwrite=disabled \
		-Dtee=disabled -Dtests=disabled -Dlzo=disabled -Dgtk2-utils=disabled -Dspectre=disabled \
		-Dsymbol-lookup=disabled -Dgtk_doc=false

	echo "== pango 1.54 (cairo + fontconfig/freetype + harfbuzz; no libthai/xft)"
	meson_pkg pango pango-build -Dintrospection=disabled -Dgtk_doc=false -Ddocumentation=false \
		-Dbuild-testsuite=false -Dbuild-examples=false -Dfontconfig=enabled -Dfreetype=enabled -Dcairo=enabled \
		-Dlibthai=disabled -Dxft=disabled -Dsysprof=disabled

	echo "== GTK 3.24 (Wayland backend only)"
	meson_pkg --cross "${out}/phoenix-aarch64-gtk.cross" gtk gtk-build -Dx11_backend=false -Dwayland_backend=true \
		-Dbroadway_backend=false -Dwin32_backend=false -Dquartz_backend=false -Dxinerama=no -Dcloudproviders=false \
		-Dprofiler=false -Dtracker3=false -Dprint_backends=none -Dcolord=no -Dgtk_doc=false -Dman=false \
		-Dintrospection=false -Ddemos=true -Dexamples=false -Dtests=false -Dinstalled_tests=false \
		-Dbuiltin_immodules=all

	echo "== gtk-layer-shell 0.10 (layer-shell for GTK 3; no introspection/vapi)"
	meson_pkg --cross "${out}/phoenix-aarch64-wl.cross" gtk-layer-shell gtk-layer-shell-build -Dexamples=false \
		-Ddocs=false -Dtests=false -Dintrospection=false -Dvapi=false

	# GSettings schemas (GTK's org.gtk.Settings.*, GLib's): compiled on the host (the format is
	# architecture-independent) for /usr/share/glib-2.0/schemas/gschemas.compiled on the Pi.
	mkdir -p "${out}/data/glib-2.0/schemas"
	cp "${P}"/share/glib-2.0/schemas/*.xml "${out}/data/glib-2.0/schemas/"
	glib-compile-schemas --strict "${out}/data/glib-2.0/schemas"
	echo "  gschemas.compiled: $(ls "${out}"/data/glib-2.0/schemas/*.xml | wc -l) schema file(s)"
fi

# --- programs --------------------------------------------------------------------------------
# gtk3-hello: linked by hand with --gc-sections (meson's links of GTK's own programs keep
# every section); the whole static closure comes from the installed .pc files.
echo "== link"
mkdir -p "${out}/obj"
PKGC="${out}/pkg-config-phoenix"
"${PHXCC}" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${SYSD}/include" -I"${COMPAT_INC}" \
	$("${PKGC}" --cflags gtk+-3.0 gtk+-wayland-3.0 gtk-layer-shell-0) -c "${here}/src/gtk3-hello.c" -o "${out}/obj/gtk3-hello.o"
link_prog() {  # output objects...
	local o="$1"
	shift
	"${PHXCC}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,-Map,"${out}/${o}.map" \
		-o "${out}/${o}" "$@" -L"${SYSD}/lib" -Wl,--start-group $("${PKGC}" --libs gtk-layer-shell-0 gtk+-3.0 gtk+-wayland-3.0) \
		-Wl,--end-group > "${out}/${o}-link.log" 2>&1 \
		|| { grep -v 'warning: .* is not fully supported' "${out}/${o}-link.log" | head -40; exit 1; }
	"${TC}-strip" -o "${out}/${o}-stripped" "${out}/${o}"
	echo "  ${o}: $(stat -c %s "${out}/${o}") bytes, stripped $(stat -c %s "${out}/${o}-stripped")"
}
link_prog gtk3-hello "${out}/obj/gtk3-hello.o"
# GTK's own demos, as meson linked them
for d in gtk-demo/gtk3-demo widget-factory/gtk3-widget-factory; do
	b="$(basename "${d}")"
	cp "${out}/gtk-build/demos/${d}" "${out}/${b}"
	"${TC}-strip" -o "${out}/${b}-stripped" "${out}/${b}"
	echo "  ${b}: $(stat -c %s "${out}/${b}") bytes, stripped $(stat -c %s "${out}/${b}-stripped")"
done

# --- verification ----------------------------------------------------------------------------
echo "== verify"
bad=0
for o in gtk3-hello gtk3-demo gtk3-widget-factory; do
	und="$("${TC}-nm" -u "${out}/${o}" || true)"
	n=$(grep -c . <<< "${und}" || true)
	interp="$("${TC}-readelf" -l "${out}/${o}" | grep -c INTERP || true)"
	syms="$("${TC}-nm" "${out}/${o}")"
	x11="$(grep -cE ' (XOpenDisplay|XInternAtom|xcb_connect|gdk_x11_display_get_type|_gdk_broadway_display_open)$' <<< "${syms}" || true)"
	echo "  ${o}: nm -u ${n}, PT_INTERP ${interp}, X11/broadway symbols ${x11}; $("${TC}-size" "${out}/${o}" | awk 'NR==2 {printf "text %d data %d bss %d", $1, $2, $3}')"
	[ "${n}" = 0 ] && [ "${interp}" = 0 ] && [ "${x11}" = 0 ] || { sed 's/^/    /' <<< "${und}" | head -10; bad=1; }
	for s in gdk_wayland_display_get_type _gdk_wayland_display_open memfd_create __wrap_close eglGetProcAddress \
			wl_display_connect xkb_keymap_new_from_string g_vfs_get_local pango_cairo_font_map_get_default \
			gdk_pixbuf_new_from_file; do
		grep -qE " [TtWw] ${s}\$" <<< "${syms}" || { echo "    ${o}: symbol ${s} missing"; bad=1; }
	done
done
n="$(grep -c '/org/gtk/libgtk/theme/Adwaita' <<< "$(strings -a "${out}/gtk3-hello-stripped")" || true)"
m="$(grep -cE ' [Tt] _gtk_register_resource$' <<< "$("${TC}-nm" "${out}/gtk3-hello")" || true)"
echo "  gtk3-hello: GTK resource bundle (built-in Adwaita theme + icons): paths ${n}, _gtk_register_resource ${m}"
[ "${n}" != 0 ] && [ "${m}" = 1 ] || bad=1
n="$(grep -c 'Using the built-in XKB keymap' <<< "$(strings -a "${out}/gtk3-hello-stripped")" || true)"
m="$(grep -c 'xkb_keymap {' <<< "$(strings -a "${out}/gtk3-hello-stripped")" || true)"
echo "  gtk3-hello: built-in XKB keymap message ${n}, keymap text ${m} (GTK patch 0003)"
[ "${n}" != 0 ] && [ "${m}" != 0 ] || bad=1
n="$(grep -cE ' [Tt] (gtk_layer_init_for_window|gtk_layer_is_supported)$' <<< "$("${TC}-nm" "${out}/gtk3-hello")" || true)"
echo "  gtk-layer-shell: libgtk-layer-shell.a + gtk-layer-shell-0.pc installed, linked into gtk3-hello (--layer): ${n}/2 symbols"
[ -f "${P}/lib/libgtk-layer-shell.a" ] && [ -f "${P}/lib/pkgconfig/gtk-layer-shell-0.pc" ] && [ "${n}" = 2 ] || bad=1
strs="$(strings -a "${out}/gtk3-hello-stripped")"
for s in 'GTK3HELLO' 'gdk-wayland' 'wayland-0' 'Adwaita' '/shm'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  gtk3-hello strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
{ echo "# gtk3-wayland build $(date -u +%Y-%m-%dT%H:%MZ)"; sha256sum "${out}"/*-stripped "${out}/data/glib-2.0/schemas/gschemas.compiled" | sed "s|${out}/||"; } \
	> "${out}/SHA256SUMS"
sed 's/^/  /' "${out}/SHA256SUMS"
[ "${bad}" = 0 ] || { echo "build.sh: verification failed" >&2; exit 1; }
echo "done"
