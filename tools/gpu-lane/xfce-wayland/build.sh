#!/usr/bin/env bash
#
# FRAMEWORK PORT: sources/phoenix-rtos-ports xfce_wayland (on gtk3_wayland + wayland_phoenix),
# branch feat/new-lane-wayland-ports; opt-in, not in the default image (docs/gpu-new-lane/
# MIGRATION.md section 4, "Ports (Wayland desktop)"). Every patch/glue file this script uses
# is also a file of the port; scripts/check-wayland-ports-sync.sh keeps the copies identical --
# a change here must be copied there. This script keeps working until the migration switch.
#
# xfce-wayland (new GPU lane, M7 stage 4): XFCE 4.20 on labwc, cross-built STATIC for
# aarch64-phoenix on top of the GTK 3 Wayland-only stack of tools/gpu-lane/gtk3-wayland
# (built with --usr: every package configured for /usr and /etc, installed with DESTDIR).
#
#   libxfce4util 4.20.1       (meson)     base library: paths, kiosk, i18n helpers
#   xfconf 4.20.0             (autotools) libxfconf + xfconfd (GDBus; per-channel XML backend)
#   libxfce4ui 4.20.2         (autotools) GTK 3 widgets + libxfce4kbd-private; no X11/SM/
#                                         startup-notification/libgtop/gudev/glade
#   garcon 4.20.0             (autotools) freedesktop menus (+ garcon-gtk3)
#   exo 4.20.0                (autotools) GTK 3 extensions (+ exo-desktop-item-edit, exo-open)
#   libxfce4windowing 4.20.7  (meson)     Wayland backend only (wlr-foreign-toplevel); no X11/wnck
#   Thunar 4.20.10            (autotools) + thunarx; no gudev/libnotify/exif/plugins
#   xfce4-panel 4.20.8        (meson)     Wayland + gtk-layer-shell; internal plugins linked in
#   xfdesktop 4.20.2          (meson)     Wayland: background through gtk-layer-shell
#   xfce4-settings 4.20.5     (autotools) settings manager + appearance; no X11
#   xfce4-appfinder 4.20.0    (autotools)
#   adwaita-icon-theme 3.38.0             the last release with PNG full-colour icons (no
#                                         librsvg on Phoenix); symbolic icons encoded to
#                                         .symbolic.png on the build host
#
# The GTK stack is SNAPSHOTTED from the gtk3-wayland --usr out dir into <out>/gtk/ (destdir +
# deps views), so a rebuild there cannot change this build. XFCE installs into
# <out>/destdir/usr (DESTDIR) with --prefix /usr --sysconfdir /etc: the compiled-in paths
# (/etc/xdg/xfce4, /usr/share/xfce4, /usr/lib/xfce4/...) are the target's.
#
#   <out>/dl/          pinned tarballs (sha256 below)
#   <out>/src/<pkg>/   extracted + patches/<pkg>/*.patch (git am)
#   <out>/gtk/         snapshot of the GTK stack (destdir/usr + deps)
#   <out>/destdir/usr  everything built here
#   <out>/bin/         the programs, unstripped (addr2line) and -stripped
#
# Writes only into <out> (default build-out/, gitignored). No Pi, no rebuild-rpi4b-fast.sh,
# no /srv. XFCE is GPL/LGPL: its sources live only in <out>/src (fetched, sha256-pinned
# tarballs), never in sources/.
#
# Host tools: meson, ninja, python3 (+ gi with GdkPixbuf for icon rendering), wayland-scanner
# 1.24.0, glib-compile-resources, gdbus-codegen, glib-mkenums, glib-genmarshal,
# gtk-encode-symbolic-svg, gtk-update-icon-cache, update-mime-database. No gettext: bin/msgfmt
# stands in (no translations are shipped).
#
# Usage: tools/gpu-lane/xfce-wayland/build.sh [--clean] [--out <dir>] [-j N]
#            [--gtk-out <gtk3-wayland --usr out dir>] [--until libs|thunar|panel|desktop|all]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
jobs="$(nproc)"
clean=0
until_stage=all
gtk_out="${root}/tools/gpu-lane/gtk3-wayland/build-out-usr"
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		--gtk-out) shift; gtk_out="${1:?}" ;;
		--gtk-out=*) gtk_out="${1#--gtk-out=}" ;;
		--until) shift; until_stage="${1:?}" ;;
		--until=*) until_stage="${1#--until=}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
case "${gtk_out}" in /*) ;; *) gtk_out="${PWD}/${gtk_out}" ;; esac
case "${until_stage}" in libs) n_stage=1 ;; thunar) n_stage=2 ;; panel) n_stage=3 ;; desktop) n_stage=4 ;;
	all) n_stage=5 ;; *) echo "build.sh: --until libs|thunar|panel|desktop|all" >&2; exit 2 ;; esac

if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
GTKW="${root}/tools/gpu-lane/gtk3-wayland"
PHXCC="${GTKW}/bin/phx-gcc"      # drops -pthread, also inside @response files
COMPAT_INC="${root}/tools/gpu-lane/weston-drm/compat/include"
GS="${out}/gtk"                  # the GTK snapshot
X="${out}/destdir"               # DESTDIR of this build
P="${X}/usr"
SYSD="${GS}/deps/sys"            # libintl stub, libiconv, resolv (from the GTK build)
XCOMPAT_INC="${here}/compat/include"   # libphoenix gaps (compat/src): first on the include path
XCOMPAT_A="${out}/compat/libxfphx-compat.a"

# name|file|url|sha256
PKGS=(
	"libxfce4util|libxfce4util-4.20.1.tar.bz2|https://archive.xfce.org/src/xfce/libxfce4util/4.20/libxfce4util-4.20.1.tar.bz2|84bfc4daab9e466193540c3665eee42b2cf4d24e3f38fc3e8d1e0a2bebe3b8f1"
	"xfconf|xfconf-4.20.0.tar.bz2|https://archive.xfce.org/src/xfce/xfconf/4.20/xfconf-4.20.0.tar.bz2|8bc43c60f1716b13cf35fc899e2a36ea9c6cdc3478a8f051220eef0f53567efd"
	"libxfce4ui|libxfce4ui-4.20.2.tar.bz2|https://archive.xfce.org/src/xfce/libxfce4ui/4.20/libxfce4ui-4.20.2.tar.bz2|5d3d67b1244a10cee0e89b045766c05fe1035f7938f0410ac6a3d8222b5df907"
	"garcon|garcon-4.20.0.tar.bz2|https://archive.xfce.org/src/xfce/garcon/4.20/garcon-4.20.0.tar.bz2|7fb8517c12309ca4ddf8b42c34bc0c315e38ea077b5442bfcc4509415feada8f"
	"exo|exo-4.20.0.tar.bz2|https://archive.xfce.org/src/xfce/exo/4.20/exo-4.20.0.tar.bz2|4277f799245f1efde01cd917fd538ba6b12cf91c9f8a73fe2035fd5456ec078d"
	"libxfce4windowing|libxfce4windowing-4.20.7.tar.bz2|https://archive.xfce.org/src/xfce/libxfce4windowing/4.20/libxfce4windowing-4.20.7.tar.bz2|01320b279648ab5b13263f8d260bc5958be599e1eaac77c4503598dfcf96c8bb"
	"thunar|thunar-4.20.10.tar.bz2|https://archive.xfce.org/src/xfce/thunar/4.20/thunar-4.20.10.tar.bz2|a5a32b51028dc821155e44cdec025fe70398bae193c619ae6ba8b1babf6f49f1"
	"xfce4-panel|xfce4-panel-4.20.8.tar.bz2|https://archive.xfce.org/src/xfce/xfce4-panel/4.20/xfce4-panel-4.20.8.tar.bz2|d69cb1f377953aeb1fb9bdbcef12c246bea66586e3f2868f3b758e0e8ce3d3fe"
	"xfdesktop|xfdesktop-4.20.2.tar.bz2|https://archive.xfce.org/src/xfce/xfdesktop/4.20/xfdesktop-4.20.2.tar.bz2|1d9bd76015fb6e9aca05e73cd998c7c66ed4fc8c10b626e08fc2eb7c39df3f7b"
	"xfce4-settings|xfce4-settings-4.20.5.tar.bz2|https://archive.xfce.org/src/xfce/xfce4-settings/4.20/xfce4-settings-4.20.5.tar.bz2|a5fbe0e511cce29d603320ade575ad4001bd570e60f37760233237ba478affe8"
	"xfce4-appfinder|xfce4-appfinder-4.20.0.tar.bz2|https://archive.xfce.org/src/xfce/xfce4-appfinder/4.20/xfce4-appfinder-4.20.0.tar.bz2|82ca82f77dc83e285db45438c2fe31df445148aa986ffebf2faabee4af9e7304"
	"adwaita-icon-theme|adwaita-icon-theme-3.38.0.tar.xz|https://download.gnome.org/sources/adwaita-icon-theme/3.38/adwaita-icon-theme-3.38.0.tar.xz|6683a1aaf2430ccd9ea638dd4bfe1002bc92b412050c3dba20e480f979faaf97"
	"shared-mime-info|shared-mime-info-2.4.tar.gz|https://gitlab.freedesktop.org/xdg/shared-mime-info/-/archive/2.4/shared-mime-info-2.4.tar.gz|531291d0387eb94e16e775d7e73788d06d2b2fdd8cd2ac6b6b15287593b6a2de"
)
WAYLAND_VERSION=1.24.0

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-gcc-ar" "${TC}-nm" "${TC}-strip" "${PHXCC}" \
		"${gtk_out}/destdir/usr/lib/libgtk-3.a" "${gtk_out}/destdir/usr/lib/libgtk-layer-shell.a" \
		"${gtk_out}/deps/wayland/lib/libwlphx-compat.a" "${gtk_out}/phoenix-aarch64-gtk.cross"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p} (gtk3-wayland/build.sh --usr --out ${gtk_out} first?)" >&2; exit 1; }
done
for t in meson ninja wayland-scanner python3 glib-compile-resources glib-mkenums glib-genmarshal gdbus-codegen \
		gtk-encode-symbolic-svg gtk-update-icon-cache update-mime-database; do
	command -v "${t}" > /dev/null || { echo "build.sh: host tool ${t} not found" >&2; exit 1; }
done
[ "$(wayland-scanner --version 2>&1 | awk '{print $2}')" = "${WAYLAND_VERSION}" ] \
	|| { echo "build.sh: host wayland-scanner is not ${WAYLAND_VERSION}" >&2; exit 1; }
# the GTK prefix must be a --usr one: its pkg-config files say prefix=/usr
grep -q '^prefix=/usr$' "${gtk_out}/destdir/usr/lib/pkgconfig/gtk+-3.0.pc" \
	|| { echo "build.sh: ${gtk_out} is not a gtk3-wayland --usr build" >&2; exit 1; }

mkdir -p "${out}/dl" "${out}/src" "${out}/hostbin" "${out}/bin" "${out}/obj" "${P}/lib/pkgconfig"
# msgfmt stand-in + xdt-gen-visibility (from the libxfce4util tarball, GPL: not committed)
export PATH="${here}/bin:${out}/hostbin:${PATH}"
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
		# its own git repository: `git am` inside a directory of ANOTHER repository skips paths
		git -C "${dir}" init -q
		git -C "${dir}" add -A -f
		git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "${file}"
		for p in "${here}/patches/${name}"/*.patch; do
			[ -e "${p}" ] || continue
			echo "  apply ${name}/$(basename "${p}")"
			git -C "${dir}" -c user.name=build -c user.email=build@invalid am -q --whitespace=nowarn "${p}"
		done
		git -C "${dir}" diff "$(git -C "${dir}" rev-list --max-parents=0 HEAD)" > "${out}/${name}-full.patch"
		echo "${stamp}" > "${dir}.stamp"
		rm -f "${out}/${name}.built"   # a changed source rebuilds the package
	fi
}

# --- the GTK snapshot, cross files, pkg-config ----------------------------------------------
snapshot_gtk() {
	local stamp
	stamp="$(sha256sum "${gtk_out}/SHA256SUMS" "${gtk_out}/destdir/usr/lib/libgtk-3.a" \
		"${gtk_out}/destdir/usr/lib/libglib-2.0.a" "${gtk_out}/deps/wayland/lib/libwlphx-compat.a" | sha256sum | cut -c1-16)"
	if [ "$(cat "${GS}.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		rm -rf "${GS}"
		mkdir -p "${GS}"
		cp -a --reflink=auto "${gtk_out}/destdir" "${gtk_out}/deps" "${GS}/"
		cp -a "${gtk_out}/SHA256SUMS" "${GS}/SHA256SUMS.gtk3-wayland"
		echo "${stamp}" > "${GS}.stamp"
		rm -f "${out}"/*.built   # everything is rebuilt on a new GTK stack
		echo "  snapshot of ${gtk_out} (stamp ${stamp})"
	fi
	# GLib's .pc tool variables are rewritten by pkg-config --define-prefix into this destdir
	# (a variable that starts with the old prefix /usr moves with it): the tools there must
	# be the HOST's (the target binaries crash under binfmt/qemu or do not run at all).
	local t
	for t in glib-compile-resources glib-compile-schemas glib-mkenums glib-genmarshal gdbus-codegen gobject-query; do
		[ -e "${GS}/destdir/usr/bin/${t}" ] || [ -L "${GS}/destdir/usr/bin/${t}" ] || continue
		command -v "${t}" > /dev/null || continue
		ln -sfn "$(command -v "${t}")" "${GS}/destdir/usr/bin/${t}"
	done
	{ echo "gtk3-wayland --usr: ${gtk_out} (stamp ${stamp})"
	  sha256sum "${GS}"/destdir/usr/lib/lib{gtk-3,gdk-3,glib-2.0,gio-2.0,gtk-layer-shell}.a "${GS}/deps/wayland/lib/libwlphx-compat.a" \
		| sed "s|${GS}/||"; } > "${out}/snapshots.txt"
}

write_cross() {
	local pkgc="${out}/pkg-config-phoenix" v libdirs="" c
	for v in zlib libffi expat pixman-1 libpng16 libjpeg freetype2 fontconfig wayland epoxy; do
		libdirs="${libdirs}:${GS}/deps/${v}/lib/pkgconfig"
	done
	# the HOST's wayland-scanner (xfce4-settings' configure asks pkg-config for it)
	mkdir -p "${out}/hostpc"
	printf '%s\n' "wayland_scanner=$(command -v wayland-scanner)" "Name: Wayland Scanner" \
		"Description: the build host's wayland-scanner" "Version: ${WAYLAND_VERSION}" > "${out}/hostpc/wayland-scanner.pc"
	libdirs="${libdirs}:${out}/hostpc"
	cat > "${pkgc}" <<EOF
#!/bin/sh
# pkg-config over this build's DESTDIR, the GTK snapshot and its private ports views. Every
# installed .pc says prefix=/usr; --define-prefix takes each file's prefix from where it lies.
export PKG_CONFIG_LIBDIR=${P}/lib/pkgconfig:${P}/share/pkgconfig:${GS}/destdir/usr/lib/pkgconfig:${GS}/destdir/usr/share/pkgconfig${libdirs}:${GS}/deps/wayland/share/pkgconfig
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
exec /usr/bin/pkg-config --static --define-prefix "\$@"
EOF
	chmod +x "${pkgc}"
	# the GTK build's cross files, pointed at the snapshot and this pkg-config
	for c in "" -wl -gtk; do
		# + the compat headers first and the compat archive whole (meson puts c_link_args
		# before the objects; --gc-sections drops what a program does not use)
		sed -e "s|${gtk_out}|${GS}|g" -e "s|^pkg-config = .*|pkg-config = '${pkgc}'|" \
			-e "s#^\(c\|cpp\)_args = \[#\1_args = ['-I${XCOMPAT_INC}', '-fmacro-prefix-map=../src/=', '-fmacro-prefix-map=${GS}/destdir/usr/include/=', #" \
			-e "s#^\(c\|cpp\)_link_args = \[#\1_link_args = ['-Wl,--whole-archive,${XCOMPAT_A},--no-whole-archive', '-Wl,--gc-sections', #" \
			-e "s|^# Generated by .*|# Generated by tools/gpu-lane/xfce-wayland/build.sh from gtk3-wayland's phoenix-aarch64${c}.cross|" \
			"${gtk_out}/phoenix-aarch64${c}.cross" > "${out}/phoenix-aarch64${c}.cross"
	done
}

# XFCE C code: gnu11 (GCC 16 defaults to C23, where `()` means `(void)` and bool is a keyword)
# (-fmacro-prefix-map: __FILE__ in g_return_if_fail() messages without build-host paths)
CF_BASE="-O2 -g -std=gnu11 ${TFLAGS[*]} -fmacro-prefix-map=${out}/src/= -fmacro-prefix-map=${GS}/destdir/usr/include/= -I${XCOMPAT_INC} -I${SYSD}/include"
CF_GTK="${CF_BASE} -I${COMPAT_INC} -I${GS}/deps/wayland/include"
LD_BASE="--sysroot=${S}/ -B${S}/lib/ -L${SYSD}/lib -Wl,-z,max-page-size=0x1000 -Wl,--gc-sections -Wl,--whole-archive,${XCOMPAT_A},--no-whole-archive"  # one token: libtool reorders a bare .a

meson_pkg() {  # [--cross <file>] name meson-args...
	local cross="${out}/phoenix-aarch64.cross"
	if [ "$1" = --cross ]; then cross="$2"; shift 2; fi
	local name="$1"
	shift
	local bd="${out}/${name}-build"
	if [ -f "${out}/${name}.built" ]; then
		echo "  ${name}: up to date"
		return 0
	fi
	rm -rf "${bd}"
	meson setup "${bd}" "${out}/src/${name}" --cross-file "${cross}" --prefix /usr --sysconfdir /etc \
		--localstatedir /var --libdir lib --buildtype=debugoptimized -Db_staticpic=false -Db_ndebug=if-release \
		--wrap-mode=nodownload "$@" > "${out}/${name}-setup.log" 2>&1 || { tail -40 "${out}/${name}-setup.log"; exit 1; }
	ninja -C "${bd}" -j"${jobs}" > "${out}/${name}-ninja.log" 2>&1 || { grep -E -A3 'error:|undefined reference|FAILED' "${out}/${name}-ninja.log" | cut -c1-400 | head -60; exit 1; }
	DESTDIR="${X}" ninja -C "${bd}" install > "${out}/${name}-install.log" 2>&1 || { tail -20 "${out}/${name}-install.log"; exit 1; }
	echo "  ${name}: built ($(grep -c 'warning:' "${out}/${name}-ninja.log" || true) warning line(s))"
	touch "${out}/${name}.built"
}

ac_pkg() {  # [--gtk] name configure-args...
	local cf="${CF_BASE}"
	if [ "$1" = --gtk ]; then cf="${CF_GTK}"; shift; fi
	local name="$1"
	shift
	local bd="${out}/${name}-build" src="${out}/src/${name}"
	if [ -f "${out}/${name}.built" ]; then
		echo "  ${name}: up to date"
		return 0
	fi
	rm -rf "${bd}"
	mkdir -p "${bd}"
	( cd "${bd}" && "${src}/configure" --host=aarch64-phoenix --build="$("${src}/config.guess")" \
		--prefix=/usr --sysconfdir=/etc --localstatedir=/var --libdir=/usr/lib --disable-shared --enable-static \
		--disable-nls --disable-silent-rules --disable-maintainer-mode \
		CC="${PHXCC}" CFLAGS="${cf}" LDFLAGS="${LD_BASE}" PKG_CONFIG="${out}/pkg-config-phoenix" \
		AR="${TC}-gcc-ar" RANLIB="${TC}-gcc-ranlib" NM="${TC}-nm" STRIP="${TC}-strip" "$@" ) \
		> "${out}/${name}-configure.log" 2>&1 || { tail -40 "${out}/${name}-configure.log"; exit 1; }
	make -C "${bd}" -j"${jobs}" > "${out}/${name}-make.log" 2>&1 || { grep -E -A3 'error:|undefined reference|\*\*\*' "${out}/${name}-make.log" | cut -c1-400 | head -60; exit 1; }
	make -C "${bd}" DESTDIR="${X}" install > "${out}/${name}-install.log" 2>&1 || { tail -20 "${out}/${name}-install.log"; exit 1; }
	# static archives only: libtool .la files would point later links at /usr/lib (the host's)
	find "${X}" -name '*.la' -delete
	echo "  ${name}: built ($(grep -c 'warning:' "${out}/${name}-make.log" || true) warning line(s))"
	touch "${out}/${name}.built"
}

echo "== sources"
for rec in "${PKGS[@]}"; do fetch_extract "${rec%%|*}"; done
install -m 755 "${out}/src/libxfce4util/xdt-gen-visibility" "${out}/hostbin/xdt-gen-visibility"

echo "== GTK snapshot (gtk3-wayland --usr)"
snapshot_gtk
write_cross

echo "== compat (compat/src: libphoenix gaps)"
mkdir -p "${out}/compat"
cobjs=()
for c in "${here}"/compat/src/*.c; do
	o="${out}/compat/$(basename "${c%.c}").o"
	"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${XCOMPAT_INC}" -c "${c}" -o "${o}"
	cobjs+=("${o}")
done
rm -f "${XCOMPAT_A}"
"${TC}-gcc-ar" rcs "${XCOMPAT_A}" "${cobjs[@]}"

# --- stage 1: the XFCE libraries ------------------------------------------------------------
echo "== libxfce4util"
meson_pkg libxfce4util -Dintrospection=false -Dvala=disabled -Dgtk-doc=false

echo "== xfconf (libxfconf + xfconfd; no GSettings backend module)"
ac_pkg xfconf --disable-gsettings-backend --disable-introspection --disable-vala --disable-checks \
	--disable-profiling --with-helper-path-prefix=/usr/lib --with-bash-completion-dir=no

echo "== libxfce4ui (GTK 3 Wayland; no X11/SM/startup-notification/libgtop/epoxy/gudev/glade)"
ac_pkg --gtk libxfce4ui --disable-x11 --enable-wayland --disable-libsm --disable-startup-notification \
	--disable-glibtop --disable-epoxy --disable-gudev --disable-introspection --disable-vala --disable-gladeui2 \
	--disable-tests --with-vendor-info=Phoenix-RTOS

echo "== garcon"
ac_pkg --gtk garcon --disable-introspection

echo "== exo"
ac_pkg --gtk exo --enable-gio-unix

echo "== libxfce4windowing (Wayland only)"
meson_pkg --cross "${out}/phoenix-aarch64-wl.cross" libxfce4windowing -Dx11=disabled -Dwayland=enabled \
	-Dintrospection=false -Dvala=disabled -Dgtk-doc=false -Dtests=false

# --- stage 2: Thunar --------------------------------------------------------------------------
if [ "${n_stage}" -ge 2 ]; then
	echo "== Thunar (+ thunarx; no gudev/libnotify/exif, no plugins, no X11/SM)"
	ac_pkg --gtk thunar --without-x --disable-gudev --disable-notifications --disable-exif --enable-pcre2 \
		--disable-apr-plugin --disable-sbr-plugin --disable-tpa-plugin --disable-uca-plugin \
		--disable-wallpaper-plugin --disable-introspection --with-helper-path-prefix=/usr/lib
fi

# --- stage 3: xfce4-panel ----------------------------------------------------------------------
if [ "${n_stage}" -ge 3 ]; then
	echo "== xfce4-panel (Wayland + gtk-layer-shell; the internal plugins linked in: patch 0001)"
	meson_pkg --cross "${out}/phoenix-aarch64-wl.cross" xfce4-panel -Dx11=disabled -Dwayland=enabled \
		-Dgtk-layer-shell=enabled -Ddbusmenu=disabled -Dintrospection=false -Dvala=disabled -Dgtk-doc=false \
		-Dbuiltin-plugins=true -Dhelper-path-prefix=/usr/lib
fi

# --- stage 4: xfdesktop -----------------------------------------------------------------------
if [ "${n_stage}" -ge 4 ]; then
	echo "== xfdesktop (Wayland: the backdrop on gtk-layer-shell; window icons, no file icons/thunarx/libnotify)"
	meson_pkg --cross "${out}/phoenix-aarch64-wl.cross" xfdesktop -Dx11=disabled -Dwayland=enabled \
		-Ddesktop-menu=enabled -Ddesktop-icons=true -Dfile-icons=false -Dthunarx=disabled -Dnotifications=disabled \
		-Dtests=false -Dfile-manager-fallback=/bin/thunar-wl \
		-Ddefault-backdrop-filename=backgrounds/phoenix/phoenix-gradient-1920x1080.png
fi

# --- stage 5: xfce4-settings, xfce4-appfinder -------------------------------------------------
if [ "${n_stage}" -ge 5 ]; then
	echo "== xfce4-settings (settings manager + the Wayland-capable dialogs; no X11/xrandr/xcursor/xklavier/libnotify/upower/colord)"
	ac_pkg --gtk xfce4-settings --disable-x11 --enable-wayland --disable-xrandr --disable-xcursor \
		--disable-xorg-libinput --disable-libxklavier --disable-libnotify --enable-gtk-layer-shell \
		--disable-upower-glib --disable-colord --disable-sound-settings --with-helper-path-prefix=/usr/lib

	echo "== xfce4-appfinder"
	ac_pkg --gtk xfce4-appfinder
fi

# --- data: PNG icon themes, MIME database -----------------------------------------------------
# (<out>/data/ mirrors the target: data/icons/<theme>, data/mime/mime.cache)
if [ "${n_stage}" -ge 2 ]; then
	echo "== icons (PNG only: no SVG loader on Phoenix) + shared-mime-info"
	stamp="$( { cat "${here}/tools/pngify-icon-theme.py"; sha256sum "${out}/dl/adwaita-icon-theme-3.38.0.tar.xz";
		find "${P}/share/icons/hicolor" -type f -printf '%P %s\n' | sort; } | sha256sum | cut -c1-16)"
	if [ "$(cat "${out}/data/icons.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
		mkdir -p "${out}/data/icons"
		# Adwaita 3.38: full-colour PNGs as shipped (menu/toolbar/dialog/panel sizes), the
		# symbolic SVGs encoded at 16 and 24 px
		python3 "${here}/tools/pngify-icon-theme.py" --name Adwaita --inherits hicolor --sizes 16,24 \
			--include-sizes 16x16,22x22,24x24,32x32,48x48 --jobs "${jobs}" \
			"${out}/data/icons/Adwaita" "${out}/src/adwaita-icon-theme/Adwaita"
		# hicolor: the XFCE programs' own icons (org.xfce.*); scalable ones rendered to PNG
		python3 "${here}/tools/pngify-icon-theme.py" --name hicolor --inherits "" --sizes 16,24,32,48 \
			--comment "Fallback icon theme (XFCE application icons, PNG only)" --jobs "${jobs}" \
			"${out}/data/icons/hicolor" "${P}/share/icons/hicolor"
		for t in Adwaita hicolor; do
			gtk-update-icon-cache -f -q -t "${out}/data/icons/${t}"
		done
		echo "${stamp}" > "${out}/data/icons.stamp"
	fi
	# shared-mime-info 2.4: GIO's content types (xdgmime reads mime.cache alone when it is valid)
	rm -rf "${out}/data/mime"
	mkdir -p "${out}/data/mime/packages"
	cp "${out}/src/shared-mime-info/data/freedesktop.org.xml.in" "${out}/data/mime/packages/freedesktop.org.xml"
	update-mime-database -n "${out}/data/mime"
	find "${out}/data/mime" -mindepth 1 -maxdepth 1 ! -name mime.cache -exec rm -rf {} +
	echo "  mime.cache: $(stat -c %s "${out}/data/mime/mime.cache") bytes"
fi

# --- programs: collect, strip, verify ---------------------------------------------------------
# name|installed path (under destdir/usr)|stage|symbols that must be linked in
PROGS=(
	"xfconfd|lib/xfce4/xfconf/xfconfd|1|g_bus_own_name xfconf_backend_factory_get_backend g_dbus_connection_register_object"
	"xfconf-query|bin/xfconf-query|1|xfconf_channel_get_property xfconf_init"
	"gdbus|bin/gdbus|1|g_dbus_connection_new_for_address_sync _g_dbus_auth_mechanism_anon_get_type"
	"xfce4-panel|bin/xfce4-panel|3|panel_builtin_plugins xfce_panel_builtin_applicationsmenu_init xfce_panel_builtin_clock_init xfce_panel_builtin_tasklist_init xfce_panel_builtin_windowmenu_init xfce_panel_builtin_launcher_init xfce_panel_builtin_separator_init xfce_panel_builtin_actions_init gtk_layer_init_for_window xfw_screen_get_default garcon_menu_new_for_path"
	"xfdesktop|bin/xfdesktop|4|xfce_desktop_new gtk_layer_init_for_window xfw_screen_get_default gdk_wayland_display_get_type"
	"xfce4-settings-manager|bin/xfce4-settings-manager|5|garcon_menu_new_for_path xfconf_channel_get gdk_wayland_display_get_type"
	"xfce4-appearance-settings|bin/xfce4-appearance-settings|5|xfconf_channel_get gtk_icon_theme_get_default gdk_wayland_display_get_type"
	"xfce4-appfinder|bin/xfce4-appfinder|5|garcon_menu_new_applications xfconf_channel_get gdk_wayland_display_get_type"
	"thunar|bin/thunar|2|thunar_application_get gdk_wayland_display_get_type xfconf_channel_get exo_icon_view_new xfce_dialog_show_error thunarx_provider_factory_get_default g_file_monitor_directory"
)
echo "== programs"
bad=0
: > "${out}/bin/SHA256SUMS.tmp"
for rec in "${PROGS[@]}"; do
	IFS='|' read -r name path stage syms <<< "${rec}"
	[ "${stage}" -le "${n_stage}" ] || continue
	f="${P}/${path}"
	[ "${name}" = gdbus ] && f="${GS}/destdir/usr/${path}"
	[ -f "${f}" ] || { echo "  ${name}: MISSING (${f})"; bad=1; continue; }
	cp -a "${f}" "${out}/bin/${name}"
	"${TC}-strip" -o "${out}/bin/${name}-stripped" "${out}/bin/${name}"
	und="$("${TC}-nm" -u "${out}/bin/${name}" || true)"
	n=$(grep -c . <<< "${und}" || true)
	interp="$("${TC}-readelf" -l "${out}/bin/${name}" | grep -c INTERP || true)"
	allsyms="$("${TC}-nm" "${out}/bin/${name}")"
	x11="$(grep -cE ' (XOpenDisplay|XInternAtom|xcb_connect|gdk_x11_display_get_type|SmcOpenConnection|wnck_screen_get_default)$' <<< "${allsyms}" || true)"
	echo "  ${name}: nm -u ${n}, PT_INTERP ${interp}, X11 symbols ${x11}; $("${TC}-size" "${out}/bin/${name}" | awk 'NR==2 {printf "text %d data %d bss %d", $1, $2, $3}'); stripped $(stat -c %s "${out}/bin/${name}-stripped")"
	[ "${n}" = 0 ] && [ "${interp}" = 0 ] && [ "${x11}" = 0 ] || { sed 's/^/    /' <<< "${und}" | head -10; bad=1; }
	for s in ${syms}; do
		grep -qE " [TtWwDdBbRr] ${s}\$" <<< "${allsyms}" || { echo "    ${name}: symbol ${s} missing"; bad=1; }
	done
	o="$(strings -a "${out}/bin/${name}-stripped" | grep -cE 'tools/gpu-lane|/home/' || true)"
	echo "    build-host path strings: ${o}"
	sha256sum "${out}/bin/${name}-stripped" | sed "s|${out}/bin/||" >> "${out}/bin/SHA256SUMS.tmp"
done
{ echo "# xfce-wayland build $(date -u +%Y-%m-%dT%H:%MZ) (stage ${until_stage})"; cat "${out}/bin/SHA256SUMS.tmp"; } > "${out}/bin/SHA256SUMS"
rm -f "${out}/bin/SHA256SUMS.tmp"
sed 's/^/  /' "${out}/bin/SHA256SUMS"
[ "${bad}" = 0 ] || { echo "build.sh: verification failed" >&2; exit 1; }

# --- the staging tree: <out>/stage mirrors the NFS root (new names only) -----------------------
# Stage with: for each file in stage/MANIFEST, `sudo -n install -D` to $EXPORT/<path> after
# checking nothing of that name exists (docs/gpu-new-lane/M7-wayland-desktop.md, stage 4).
echo "== staging tree"
ST="${out}/stage"
rm -rf "${ST}"
st() {  # mode source target-path
	install -D -m "$1" "$2" "${ST}/$3"
}
st 755 "${out}/bin/xfconfd-stripped" usr/lib/xfce4/xfconf/xfconfd
st 755 "${out}/bin/xfconf-query-stripped" bin/xfconf-query
st 644 "${P}/share/dbus-1/services/org.xfce.Xfconf.service" usr/share/dbus-1/services/org.xfce.Xfconf.service
st 755 "${here}/pi/xfce-desktop.sh" bin/xfce-desktop.sh
for f in rc.xml menu.xml autostart environment; do
	st 644 "${here}/conf/labwc-xfce/${f}" "etc/xdg/labwc-xfce/${f}"
done
for f in "${here}"/conf/xfconf/*.xml "${X}"/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/*.xml; do
	st 644 "${f}" "etc/xdg/xfce4/xfconf/xfce-perchannel-xml/$(basename "${f}")"
done
if [ "${n_stage}" -ge 2 ]; then
	st 755 "${out}/bin/thunar-stripped" bin/thunar-wl
	st 644 "${here}/conf/applications/thunar.desktop" usr/share/applications/thunar.desktop
	st 644 "${out}/data/mime/mime.cache" usr/share/mime/mime.cache
	mkdir -p "${ST}/usr/share/icons"
	cp -a "${out}/data/icons/Adwaita" "${out}/data/icons/hicolor" "${ST}/usr/share/icons/"
fi
if [ "${n_stage}" -ge 3 ]; then
	st 755 "${out}/bin/xfce4-panel-stripped" bin/xfce4-panel
	for f in "${P}"/share/xfce4/panel/plugins/*.desktop; do
		st 644 "${f}" "usr/share/xfce4/panel/plugins/$(basename "${f}")"
	done
	st 644 "${X}/etc/xdg/xfce4/panel/default.xml" etc/xdg/xfce4/panel/default.xml
	# garcon's menu (the applications menu plugin, xfce4-appfinder)
	st 644 "${X}/etc/xdg/menus/xfce-applications.menu" etc/xdg/menus/xfce-applications.menu
	for f in "${P}"/share/desktop-directories/*.directory; do
		st 644 "${f}" "usr/share/desktop-directories/$(basename "${f}")"
	done
fi
if [ "${n_stage}" -ge 4 ]; then
	st 755 "${out}/bin/xfdesktop-stripped" bin/xfdesktop
fi
if [ "${n_stage}" -ge 5 ]; then
	for p in xfce4-settings-manager xfce4-appearance-settings xfce4-appfinder; do
		st 755 "${out}/bin/${p}-stripped" "bin/${p}"
	done
	for f in xfce-settings-manager xfce-ui-settings xfce4-appfinder xfce4-run; do
		st 644 "${P}/share/applications/${f}.desktop" "usr/share/applications/${f}.desktop"
	done
	st 644 "${X}/etc/xdg/menus/xfce-settings-manager.menu" etc/xdg/menus/xfce-settings-manager.menu
	st 644 "${X}/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml" etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml
fi
# GIO's own gdbus (from the GTK snapshot) under a new name: m7f-dbus step 5 with GDBUS=/bin/gdbus-wl
st 755 "${out}/bin/gdbus-stripped" bin/gdbus-wl
( cd "${ST}" && find . -type f -printf '%P\n' | sort | xargs sha256sum ) > "${out}/stage.MANIFEST"
echo "  $(wc -l < "${out}/stage.MANIFEST") files ($(du -sh "${ST}" | cut -f1)); not icons:"
grep -v ' usr/share/icons/' "${out}/stage.MANIFEST" | awk '{printf "    %s  %s\n", substr($1,1,16), $2}'
echo "done (stage ${until_stage})"
