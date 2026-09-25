# Differential harness for libphoenix's sscanf, on the host.
#
#   make run   1564 comparisons against glibc: return value, parsed values and
#              the %n position, over integer/float/string/scanset conversions.
#
# scanf is pure string parsing, so it is fully testable off-target. Its numeric
# leaves (strtod/strtoll/...) are left bound to glibc on purpose, so a difference
# here is scanf's OWN parsing rather than a conversion routine that
# tools/libnum-hosttest/ covers separately.
#
# glibc's headers rename sscanf to __isoc99_sscanf, so that is the symbol
# objcopy renames to ph_sscanf.

LIBPH  ?= ../../sources/libphoenix
CFLAGS ?= -std=gnu11 -O1

all: scdiff

scanf_ph.o: $(LIBPH)/stdio/scanf.c
	$(CC) -c $(CFLAGS) -w -I$(LIBPH)/stdio -o scanf_raw.o $<
	objcopy --redefine-sym __isoc99_sscanf=ph_sscanf \
	        --redefine-sym __isoc99_vsscanf=ph_vsscanf \
	        --redefine-sym __isoc99_scanf=ph_scanf \
	        --redefine-sym __isoc99_vscanf=ph_vscanf \
	        --redefine-sym __isoc99_fscanf=ph_fscanf \
	        --redefine-sym __isoc99_vfscanf=ph_vfscanf scanf_raw.o $@

scdiff: scdiff.c scanf_ph.o
	$(CC) $(CFLAGS) -Wall -o $@ $^ -lm

run: scdiff
	./scdiff

clean:
	rm -f scdiff *.o

.PHONY: all run clean
