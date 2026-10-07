#!/usr/bin/env bash
# Host test of GIO's "poll" file monitor (ports gtk3_wayland, GLib patch 0004).
#
# Builds the ports' GLib (tarball + patches/glib/*.patch, as gtk3_wayland's
# _gtk3wl_extract applies them) for the HOST with -Dfile_monitor_backend=poll,
# then runs polltest with statshim.so preloaded, both with the host's
# nanosecond timestamps and with SHIM_COARSE=1 (one-second timestamps, as
# Phoenix-RTOS stat() reports them). No Pi, about a minute and a half
# (plus a few minutes for the first GLib build).
#
# usage: run.sh [count]   "count" also measures the calls per second an idle
#                         60-entry directory costs (about 100 s per run)
#
#   PORTS_SRC     the phoenix-rtos-ports tree to take the patches from
#                 (default: the sibling checkout; set it to a worktree)
#   GLIB_TARBALL  default ~/.phoenix-distfiles/newlane/glib-2.88.3.tar.xz
#   BUILD         default /tmp/gio-poll-hosttest
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
ports="${PORTS_SRC:-$root/sources/phoenix-rtos-ports}"
tarball="${GLIB_TARBALL:-$HOME/.phoenix-distfiles/newlane/glib-2.88.3.tar.xz}"
build="${BUILD:-/tmp/gio-poll-hosttest}"
patches="$ports/gtk3_wayland/patches/glib"

[ -f "$tarball" ] || { echo "no GLib tarball at $tarball (set GLIB_TARBALL)"; exit 2; }
ls "$patches"/*.patch >/dev/null 2>&1 || { echo "no patches in $patches (set PORTS_SRC)"; exit 2; }

# --- GLib, re-extracted when the tarball or a patch changes ---
src="$build/glib"
stamp="$( { sha256sum "$tarball"; cat "$patches"/*.patch; } | sha256sum | cut -c1-16)"
if [ "$(cat "$src.stamp" 2>/dev/null || true)" != "$stamp" ]; then
	echo "=== GLib: extract + patches from $patches ==="
	rm -rf "$src" && mkdir -p "$src"
	tar -xf "$tarball" -C "$src" --strip-components=1 || exit 2
	git -C "$src" init -q && git -C "$src" add -A -f &&
		git -C "$src" -c user.name=t -c user.email=t@invalid commit -q -m glib || exit 2
	for p in "$patches"/*.patch; do
		git -C "$src" -c user.name=t -c user.email=t@invalid am -q --whitespace=nowarn "$p" ||
			{ echo "PATCH FAILED: $p"; exit 2; }
	done
	(cd "$src" && meson setup _build -Dfile_monitor_backend=poll -Dnls=disabled -Dlibmount=disabled \
		-Dselinux=disabled -Dxattr=false -Dlibelf=disabled -Dsysprof=disabled -Dintrospection=disabled \
		-Dtests=false -Dinstalled_tests=false -Ddocumentation=false -Dman-pages=disabled -Ddtrace=disabled \
		-Dsystemtap=disabled -Dglib_debug=disabled --buildtype=debugoptimized >"$build/meson.log" 2>&1) ||
		{ tail -20 "$build/meson.log"; echo "MESON FAILED"; exit 2; }
	echo "$stamp" >"$src.stamp"
fi
echo "=== GLib: build ==="
nice ninja -C "$src/_build" -j8 >"$build/ninja.log" 2>&1 || { tail -30 "$build/ninja.log"; echo "BUILD FAILED"; exit 2; }
if grep -q "gpolllocalfilemonitor.c.*warning" "$build/ninja.log"; then
	grep -A4 "gpolllocalfilemonitor.c.*warning" "$build/ninja.log"
	echo "WARNINGS in gpolllocalfilemonitor.c"
	exit 1
fi

# --- the shim and the test, against that GLib ---
gcc -O1 -g -Wall -Wextra -Werror -shared -fPIC "$here/statshim.c" -o "$build/statshim.so" -ldl -lpthread || exit 2
b="$src/_build"
gcc -O1 -g -Wall -Wextra -Werror "$here/polltest.c" -o "$build/polltest" \
	-I"$src" -I"$src/glib" -I"$src/gmodule" -I"$b" -I"$b/glib" -I"$b/gio" \
	-L"$b/gio" -L"$b/gobject" -L"$b/glib" -lgio-2.0 -lgobject-2.0 -lglib-2.0 -ldl || exit 2
libs="$b/gio:$b/gobject:$b/glib:$b/gmodule"

fails=0
run() {  # run <test> <coarse 0|1> [args...]
	local t="$1" coarse="$2" d="$build/dir-$1-$2"
	shift 2
	rm -rf "$d"
	local label="$t$([ "$coarse" = 1 ] && echo ' (1-second timestamps)' || echo ' (ns timestamps)')"
	echo "--- $label"
	env LD_LIBRARY_PATH="$libs" LD_PRELOAD="$build/statshim.so" SHIM_WATCH="$d" \
		$([ "$coarse" = 1 ] && echo SHIM_COARSE=1) GIO_USE_FILE_MONITOR=poll \
		"$build/polltest" "$t" "$d" "$@" || fails=1
}

if [ "${1:-}" = count ]; then
	run count 1 60 30 64 3
	exit $fails
fi
for c in 1 0; do
	run functional "$c"
	run samesecond "$c"
	run frozen "$c"
done
echo
[ "$fails" -eq 0 ] && echo "ALL GREEN" || echo "FAILURES -- see above"
exit "$fails"
