#!/bin/sh
# Copy vm/page.c (+ its headers) from a kernel tree into src-<name>/: ./sync.sh <name> <kernel tree>
H=$(dirname "$0"); N=$1; W=$2
mkdir -p $H/src-$N
cp $W/vm/page.c $W/vm/page.h $W/vm/types.h $H/src-$N/
