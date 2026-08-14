#!/usr/bin/env bash
# Build Lua 5.4.7 (the `lua` interpreter + `luac` compiler) for Phoenix-RTOS / aarch64.
#
# Lua is MIT-licensed (permissive; no GPL concern for the Phoenix repos).
#
# Lua is the cleanest possible port: pure C89, no autoconf, no external deps.
# It cross-compiles with a single set of gcc calls. -DLUA_USE_POSIX enables the
# POSIX bits of the os/io libraries (popen, gmtime_r, ...), which libphoenix
# provides. (Dynamic loading via require of .so and readline are left off: a
# static interpreter, and psh has no line editing anyway.)
set -euo pipefail

VER=5.4.7
TARBALL=lua-${VER}.tar.gz
URL=https://www.lua.org/ftp/${TARBALL}
SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30

REPO=/home/houp/phoenix-rpi
GCC=${GCC:-$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}
AR=${AR:-$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-ar}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/tmp/lua-port-build}

mkdir -p "$WORK"; cd "$WORK"
[ -f "$TARBALL" ] || curl -fsSL -o "$TARBALL" "$URL"
echo "$SHA256  $TARBALL" | sha256sum -c -
rm -rf "lua-${VER}"; tar xzf "$TARBALL"
cd "lua-${VER}/src"

CFLAGS="-O2 -static -DLUA_USE_POSIX"
CORE="lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes \
lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib \
ldblib liolib lmathlib loadlib loslib lstrlib ltablib lutf8lib linit"

echo "compiling Lua core+libs for aarch64-phoenix ..."
for u in $CORE; do "$GCC" $CFLAGS -c "$u.c" -o "$u.o"; done
"$AR" rcs liblua.a $(for u in $CORE; do echo "$u.o"; done)

echo "linking lua + luac ..."
"$GCC" $CFLAGS lua.c  liblua.a -lm -o lua
"$GCC" $CFLAGS luac.c liblua.a -lm -o luac

"$GCC" --version | head -1
ls -l lua luac
cp lua luac "$HERE/"
echo "OK -> $HERE/lua , $HERE/luac  (stage lua into the netboot NFS root as /bin/lua)"
