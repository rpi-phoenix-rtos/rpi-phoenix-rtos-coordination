# Differential harness for libphoenix's numeric string conversions, on the host.
#
#   make run   compare strtol/strtoul/strtoll/strtoull/atoi/strtod/strtof
#              against glibc over ~3200 cases; strtod is compared BIT-EXACTLY.
#
# No Pi cycle and no cross-build: strtoul.c, strtoull.c and strtod.c include only
# standard headers, so they compile natively as-is. objcopy --redefine-sym renames
# only the symbols they define, leaving their calls into glibc intact.

LIBPH  ?= ../../sources/libphoenix
CFLAGS ?= -std=gnu11 -O1 -Wall
OBJS    = strtoul.o strtoull.o strtod.o

all: ndiff

%.o: $(LIBPH)/stdlib/%.c
	$(CC) -c $(CFLAGS) -o $@ $<

num_phx.o: $(OBJS)
	ld -r -o num_ph.o $(OBJS)
	nm num_ph.o | grep ' T ' | awk '{printf "--redefine-sym %s=ph_%s ", $$3, $$3}' > redef.txt
	objcopy $$(cat redef.txt) num_ph.o $@

ndiff: ndiff.c num_phx.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

run: ndiff
	./ndiff

clean:
	rm -f ndiff *.o redef.txt

.PHONY: all run clean
