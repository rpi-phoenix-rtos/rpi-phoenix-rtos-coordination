# Differential + round-trip harness for libphoenix's printf FORMATTING engine.
#
#   make run   663 conversion comparisons against glibc, plus 200k randomised
#              format/parse round-trips through libphoenix's OWN strtod.
#
# The round-trip arm needs no reference implementation to be authoritative:
# "%.17g" uniquely identifies a double, so formatting and reparsing a value with
# libphoenix must return the exact original bits. Any failure is a defect on one
# side or the other. That property is what caught a would-be "fix" to the
# scientific-form rounding which looked right and silently broke it.
#
# No Pi cycle, no cross-build. format.c needs one Phoenix header (sys/minmax.h),
# copied into shim/.

LIBPH  ?= ../../sources/libphoenix
CFLAGS ?= -std=gnu11 -O1 -Wall

all: fdiff

shim/sys/minmax.h:
	mkdir -p shim/sys
	cp $(LIBPH)/include/sys/minmax.h shim/sys/

format.o bignum.o: shim/sys/minmax.h

format.o: $(LIBPH)/stdio/format.c
	$(CC) -c $(CFLAGS) -Ishim -I$(LIBPH)/stdio -o $@ $<

bignum.o: $(LIBPH)/stdio/bignum.c
	$(CC) -c $(CFLAGS) -Ishim -I$(LIBPH)/stdio -o $@ $<

# strtod supplies the parse half of the round-trip; rename so it does not
# collide with glibc's.
strtod_ph.o: $(LIBPH)/stdlib/strtod.c
	$(CC) -c $(CFLAGS) -w -o strtod_raw.o $<
	objcopy --redefine-sym strtod=ph_strtod --redefine-sym strtof=ph_strtof \
	        --redefine-sym strtold=ph_strtold --redefine-sym atof=ph_atof \
	        strtod_raw.o $@

fdiff: fdiff.c format.o bignum.o strtod_ph.o
	$(CC) $(CFLAGS) -I$(LIBPH)/stdio -o $@ $^ -lm

run: fdiff
	./fdiff

clean:
	rm -f fdiff *.o
	rm -rf shim

.PHONY: all run clean
