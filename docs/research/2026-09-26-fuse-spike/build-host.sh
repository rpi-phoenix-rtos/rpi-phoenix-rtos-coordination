#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Rebuild + run the host spike. Needs external/openbsd-src (lib/libfuse, sys/sys),
# external/exfat, gcc, mkfs.exfat, fsck.exfat. Work dir: $1 (default /tmp/fuse-spike).
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd); H=$(cd "$(dirname "$0")" && pwd); W=${1:-/tmp/fuse-spike}
O=$R/external/openbsd-src; E=$R/external/exfat
mkdir -p $W/obsd $W/inc/sys $W/exfat && cd $W
cp $O/lib/libfuse/*.c $O/lib/libfuse/*.h obsd/; (cd obsd && patch -p3 < $H/openbsd-libfuse-phoenix.patch)
sed -i 's/^#define\tPROTO(x).*/#define PROTO(x)/; s/^#define\tDEF(x).*/#define DEF(x)/' obsd/fuse_private.h
cp $O/sys/sys/tree.h $O/sys/sys/queue.h inc/sys/; echo '#include <stddef.h>' > inc/sys/_null.h; echo '#include <dirent.h>' > inc/sys/dirent.h
cp $H/host-compat.h inc/compat.h
cat > inc/sys/fusebuf.h <<'X'
#ifndef FB_STUB
#define FB_STUB
#include <stdint.h>
#include <stddef.h>
#define FUSEBUFMAXSIZE (4096*32)
struct fuse_dirent { uint64_t ino; uint64_t off; uint32_t namelen; uint32_t type; char name[]; };
#define FUSE_NAME_OFFSET offsetof(struct fuse_dirent, name)
#define FUSE_DIRENT_ALIGN(x) (((x) + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) - 1))
#define FUSE_DIRENT_SIZE(d) FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + (d)->namelen)
#endif
X
CF="-O1 -g -w -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -include $W/inc/compat.h -I$W/inc -I$W/obsd -I$H"
for f in fuse fuse_ops fuse_opt fuse_subr tree dict debug; do gcc -c $CF obsd/$f.c -o obsd/$f.o; done
gcc -c $CF $H/phx_fuse.c -o phx_fuse.o
gcc -c -O1 -g -D_FILE_OFFSET_BITS=64 -I$H $H/driver.c -o driver.o
EX="-O1 -g -w -D_FILE_OFFSET_BITS=64 -DPACKAGE=\"exfat\" -DVERSION=\"1.4\""
gcc -c $EX -DFUSE_USE_VERSION=26 -Dmain=exfat_main -include $W/inc/compat.h -I$W/inc -I$W/obsd -I$E/libexfat $E/fuse/main.c -o exfat/main.o
for f in $E/libexfat/*.c; do gcc -c $EX -I$E/libexfat $f -o exfat/$(basename ${f%.c}).o; done
gcc -o spike obsd/*.o phx_fuse.o driver.o exfat/*.o -lpthread
rm -f t.img; truncate -s 64M t.img; mkfs.exfat t.img >/dev/null
./spike t.img && fsck.exfat -n t.img && ./spike t.img verify
