#!/usr/bin/env bash
#
# FRAMEWORK PORT: sources/phoenix-rtos-ports dbus,
# branch feat/new-lane-wayland-ports; opt-in, not in the default image (docs/gpu-new-lane/
# MIGRATION.md section 4, "Ports (Wayland desktop)"). Every patch/glue file this script uses
# is also a file of the port; scripts/check-wayland-ports-sync.sh keeps the copies identical --
# a change here must be copied there. This script keeps working until the migration switch.
#
# dbus (new GPU lane, M7 stage 3): D-Bus 1.16 -- dbus-daemon, libdbus-1 (static), dbus-send,
# dbus-monitor, dbus-run-session, dbus-uuidgen -- cross-built STATIC for aarch64-phoenix, with
# the unix transport only (docs/gpu-new-lane/M7-wayland-desktop.md, "D-Bus session bus").
#
#   <out>/dl/            the pinned tarball (sha256 below)
#   <out>/src/dbus/      extracted + patches/dbus/*.patch applied (git apply)
#   <out>/deps/expat/    a private view of the ports prefix's expat (headers + archive only:
#                        the ports include dir also holds GL/ and X11/, never on a search path)
#   <out>/dbus-build/    meson build dir (target)
#   <out>/destdir/       `meson install` of the target build (libdbus-1.a, headers, dbus-1.pc)
#   <out>/<prog>         static, unstripped (addr2line); <prog>-stripped (stage this)
#   <out>/host-build/    the same source and options built NATIVELY (--host, for hosttest/)
#
# Configuration: no systemd, launchd, X11 autolaunch, SELinux/AppArmor/libaudit, no epoll /
# kqueue / inotify (the daemon's main loop is poll(); config reload on SIGHUP only), no tests,
# no docs; traditional (fork/exec) bus activation stays on (XFCE starts xfconfd that way).
# Phoenix has no SO_PEERCRED / SCM_CREDS / getpeereid, so dbus-sysdeps-unix.c compiles its
# "no credentials mechanism" branch (a #warning): the daemon learns no peer uid and EXTERNAL
# cannot succeed. conf/session-phoenix.conf therefore offers ANONYMOUS (lab only). Once the
# kernel reports SO_PEERCRED, a rebuild against that sysroot compiles the Linux path (the
# macro is tested with #ifdef, no configure probe) and EXTERNAL works unchanged.
#
# Writes only into <out> (default build-out/, gitignored). Reads the tree sysroot, the ports
# prefix (expat), the toolchain and the E7 compiler wrappers. No Pi, no rebuild-rpi4b-fast.sh,
# no /srv.
#
# Usage: tools/gpu-lane/dbus/build.sh [--clean] [--out <dir>] [-j N] [--host]
#   --host   also build the native copy into <out>/host-build (needs host expat-dev)
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
jobs="$(nproc)"
clean=0
host=0
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--host) host=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
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
PHXCC="${root}/tools/gpu-lane/e7-drm-build/bin/phx-gcc"
# NB: nothing is force-included (-include) into c_args: meson's has_function() probes
# declare `char f(void)` and a real prototype in scope turns every probe into NO. The
# constants libphoenix lacks are in patches/dbus/0001 instead.
D="${out}/deps"

# name|file|url|sha256
DBUS_REC="dbus|dbus-1.16.2.tar.xz|https://dbus.freedesktop.org/releases/dbus/dbus-1.16.2.tar.xz|0ba2a1a4b16afe7bceb2c07e9ce99a8c2c3508e5dec290dbb643384bd6beb7e2"
PROGS=(dbus-daemon dbus-send dbus-monitor dbus-run-session dbus-uuidgen)

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-gcc-ar" "${TC}-nm" "${TC}-strip" "${TC}-readelf" "${PHXCC}" \
		"${B}/lib/libexpat.a" "${B}/include/expat.h"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
for t in meson ninja pkg-config; do
	command -v "${t}" > /dev/null || { echo "build.sh: host tool ${t} not found" >&2; exit 1; }
done

mkdir -p "${out}/dl" "${out}/src" "${D}"

# --- source ----------------------------------------------------------------------------------
IFS='|' read -r name file url sum <<< "${DBUS_REC}"
if [ ! -f "${out}/dl/${file}" ]; then
	echo "  fetch ${file}"
	curl -sSfL -o "${out}/dl/${file}.part" "${url}"
	mv "${out}/dl/${file}.part" "${out}/dl/${file}"
fi
echo "${sum}  ${out}/dl/${file}" | sha256sum -c --quiet - || { echo "build.sh: ${file}: sha256 mismatch" >&2; exit 1; }
SRC="${out}/src/${name}"
stamp="$( { echo "${sum}"; cat "${here}/patches/${name}"/*.patch 2>/dev/null || true; } | sha256sum | cut -c1-16)"
if [ "$(cat "${SRC}.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
	echo "== source ${file}"
	rm -rf "${SRC}" "${SRC}.tmp"
	mkdir -p "${SRC}.tmp"
	tar -xf "${out}/dl/${file}" -C "${SRC}.tmp" --strip-components=1
	mv "${SRC}.tmp" "${SRC}"
	# its own repository: `git apply` inside another repository applies relative to THAT root
	git -C "${SRC}" init -q
	git -C "${SRC}" add -A
	git -C "${SRC}" -c user.name=build -c user.email=build@invalid commit -q -m "${file}"
	for p in "${here}/patches/${name}"/*.patch; do
		[ -e "${p}" ] || continue
		echo "  apply ${name}/$(basename "${p}")"
		git -C "${SRC}" apply --whitespace=nowarn "${p}"
		git -C "${SRC}" add -A
		git -C "${SRC}" -c user.name=build -c user.email=build@invalid commit -q -m "$(basename "${p}")"
	done
	echo "${stamp}" > "${SRC}.stamp"
	rm -f "${out}/dbus.configured" "${out}/host.configured"
fi

# The meson options shared by the target and the host build (the host test proves THIS
# configuration, not Phoenix).
COMMON_OPTS=(-Dmessage_bus=true -Dtools=true -Dtraditional_activation=true -Duser_session=false
	-Depoll=disabled -Dkqueue=disabled -Dinotify=disabled -Dlaunchd=disabled -Dsystemd=disabled
	-Dx11_autolaunch=disabled -Dselinux=disabled -Dapparmor=disabled -Dlibaudit=disabled
	-Dmodular_tests=disabled -Dinstalled_tests=false -Dintrusive_tests=false -Dvalgrind=disabled
	-Ddoxygen_docs=disabled -Dxml_docs=disabled -Dducktype_docs=disabled -Dqt_help=disabled
	-Drelocation=disabled -Dasserts=false -Dchecks=true -Dverbose_mode=true -Dstats=true
	-Dsession_socket_dir=/tmp -Druntime_dir=/var/run -Ddbus_user=root -Dtest_user=root)

# --- expat view + meson cross file -----------------------------------------------------------
echo "== expat view (ports prefix) + cross file"
expat_ver="$(sed -n 's/^Version: //p' "${B}/lib/pkgconfig/expat.pc")"
mkdir -p "${D}/expat/include" "${D}/expat/lib/pkgconfig"
cp "${B}/include/expat.h" "${B}/include/expat_external.h" "${D}/expat/include/"
[ -f "${B}/include/expat_config.h" ] && cp "${B}/include/expat_config.h" "${D}/expat/include/"
cp "${B}/lib/libexpat.a" "${D}/expat/lib/"
printf '%s\n' "prefix=${D}/expat" "Name: expat" "Description: expat from the Phoenix ports prefix" \
	"Version: ${expat_ver}" "Libs: -L\${prefix}/lib -lexpat" "Libs.private: -lm" "Cflags: -I\${prefix}/include" \
	> "${D}/expat/lib/pkgconfig/expat.pc"
echo "  expat ${expat_ver}"

PKGC="${out}/pkg-config-phoenix"
cat > "${PKGC}" <<EOF
#!/bin/sh
# pkg-config restricted to the private expat view.
export PKG_CONFIG_LIBDIR=${D}/expat/lib/pkgconfig
unset PKG_CONFIG_PATH
exec /usr/bin/pkg-config --static "\$@"
EOF
chmod +x "${PKGC}"
CROSS="${out}/phoenix-aarch64.cross"
cat > "${CROSS}" <<EOF
# Generated by tools/gpu-lane/dbus/build.sh (aarch64-phoenix, Pi 4).
[binaries]
c = '${PHXCC}'
ar = '${TC}-gcc-ar'
nm = '${TC}-nm'
strip = '${TC}-strip'
objcopy = '${TC}-objcopy'
pkg-config = '${PKGC}'

[host_machine]
system = 'phoenix'
cpu_family = 'aarch64'
cpu = 'cortex-a72'
endian = 'little'

[properties]
needs_exe_wrapper = true

[built-in options]
c_args = ['--sysroot=${S}/', '-B${S}/lib/', '-mcpu=cortex-a72', '-mtune=cortex-a72', '-mstrict-align', '-mno-outline-atomics', '-ffunction-sections', '-fdata-sections']
c_link_args = ['--sysroot=${S}/', '-B${S}/lib/', '-static', '-Wl,--gc-sections', '-Wl,-z,max-page-size=0x1000']
default_library = 'static'
EOF

# --- target build ----------------------------------------------------------------------------
BD="${out}/dbus-build"
echo "== dbus (target, static)"
if [ ! -f "${out}/dbus.configured" ]; then
	rm -rf "${BD}"
	meson setup "${BD}" "${SRC}" --cross-file "${CROSS}" --prefix /usr --sysconfdir /etc --localstatedir /var \
		--libdir lib --buildtype=debugoptimized -Db_staticpic=false -Db_pie=false --wrap-mode=nodownload \
		"${COMMON_OPTS[@]}" > "${out}/dbus-setup.log" 2>&1 || { tail -40 "${out}/dbus-setup.log"; exit 1; }
	touch "${out}/dbus.configured"
fi
# A probe that silently answers NO changes the code compiled (socketpair() for activation,
# accept4() for CLOEXEC): refuse the build rather than ship it.
for f in socket socketpair accept4 poll getpwnam_r setenv; do
	grep -qE "Checking for function \"${f}\" : YES" "${BD}/meson-logs/meson-log.txt" \
		|| { echo "build.sh: meson probe for ${f}() answered NO (libphoenix has it): see ${BD}/meson-logs" >&2; exit 1; }
done
ninja -C "${BD}" -j"${jobs}" > "${out}/dbus-ninja.log" 2>&1 || { grep -E -A6 'error|FAILED' "${out}/dbus-ninja.log" | head -80; exit 1; }
echo "  built ($(grep -c 'warning:' "${out}/dbus-ninja.log" || true) warning line(s))"
grep -E 'warning: #warning' "${out}/dbus-ninja.log" | sort -u | sed 's/^/  /' || true
rm -rf "${out}/destdir"
DESTDIR="${out}/destdir" ninja -C "${BD}" install > "${out}/dbus-install.log" 2>&1 || { tail -20 "${out}/dbus-install.log"; exit 1; }
for o in "${PROGS[@]}"; do
	f="$(find "${BD}" -maxdepth 3 -type f -name "${o}" -perm -u+x | head -1)"
	[ -n "${f}" ] || { echo "build.sh: ${o} not built" >&2; exit 1; }
	cp "${f}" "${out}/${o}"
	"${TC}-strip" -o "${out}/${o}-stripped" "${out}/${o}"
done
grep -E 'HAVE_UNIX_FD_PASSING|HAVE_SOCKETPAIR|HAVE_ACCEPT4|HAVE_POLL|DBUS_HAVE_LINUX|HAVE_GETPEEREID|HAVE_CMSGCRED|HAVE_SYSLOG|DBUS_ENABLE_INOTIFY|DBUS_BUS_ENABLE_' \
	"${BD}/config.h" | sed 's/^/  config.h: /'

# --- verification ----------------------------------------------------------------------------
echo "== verify"
bad=0
for o in "${PROGS[@]}"; do
	und="$("${TC}-nm" -u "${out}/${o}" || true)"
	n=$(grep -c . <<< "${und}" || true)
	interp="$("${TC}-readelf" -l "${out}/${o}" | grep -c 'INTERP' || true)"
	dyn="$("${TC}-readelf" -d "${out}/${o}" 2>&1 | grep -c 'NEEDED' || true)"
	echo "  ${o}: nm -u ${n}, PT_INTERP ${interp}, DT_NEEDED ${dyn}, $(stat -c %s "${out}/${o}") bytes, stripped $(stat -c %s "${out}/${o}-stripped")"
	[ "${n}" = 0 ] && [ "${interp}" = 0 ] && [ "${dyn}" = 0 ] || { sed 's/^/    /' <<< "${und}" | head -10; bad=1; }
done
strs="$(strings -a "${out}/dbus-daemon-stripped")"
for s in 'ANONYMOUS' 'EXTERNAL' 'DBUS_COOKIE_SHA1' 'allow_anonymous' 'DBUS_VERBOSE' 'unix:path=' 'org.freedesktop.DBus'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  dbus-daemon strings '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
# The credentials path compiled in: SO_PEERCRED appears as a verbose message only when the
# sysroot defines it (stage 1: 0).
echo "  dbus-daemon strings 'SO_PEERCRED' (1 = compiled against a SO_PEERCRED sysroot): $(grep -cF 'SO_PEERCRED' <<< "${strs}" || true)"
[ -f "${out}/destdir/usr/lib/libdbus-1.a" ] || { echo "  libdbus-1.a not installed"; bad=1; }
echo "  libdbus-1.a: $(stat -c %s "${out}/destdir/usr/lib/libdbus-1.a" 2>/dev/null || echo missing) bytes"
( cd "${out}" && sha256sum "${PROGS[@]/%/-stripped}" ) | sed 's/^/  /'
[ "${bad}" = 0 ] || { echo "build.sh: verification failed" >&2; exit 1; }

# --- host build (same source + options, native) ----------------------------------------------
if [ "${host}" = 1 ]; then
	echo "== dbus (host, same options)"
	HB="${out}/host-build"
	if [ ! -f "${out}/host.configured" ]; then
		rm -rf "${HB}"
		meson setup "${HB}" "${SRC}" --prefix /usr --sysconfdir /etc --localstatedir /var --buildtype=debugoptimized \
			--wrap-mode=nodownload -Ddefault_library=static "${COMMON_OPTS[@]}" \
			> "${out}/host-setup.log" 2>&1 || { tail -40 "${out}/host-setup.log"; exit 1; }
		touch "${out}/host.configured"
	fi
	ninja -C "${HB}" -j"${jobs}" > "${out}/host-ninja.log" 2>&1 || { grep -E -A6 'error|FAILED' "${out}/host-ninja.log" | head -60; exit 1; }
	echo "  host build ok: $(ls "${HB}/bus/dbus-daemon" "${HB}/tools/dbus-send" 2>/dev/null | tr '\n' ' ')"
fi
echo "done"
