#!/bin/sh
# Copy the code under test from the kernel worktree
W=${1:-$(cd "$(dirname "$0")/../.." && pwd)/sources/phoenix-rtos-kernel}
H=$(dirname "$0"); mkdir -p $H/src
cp $W/posix/uchannel.c $W/posix/uchannel.h $W/posix/pipe.c $W/posix/pipe.h $H/src/
cp $W/lib/cbuffer.c $W/lib/cbuffer.h $H/src/
