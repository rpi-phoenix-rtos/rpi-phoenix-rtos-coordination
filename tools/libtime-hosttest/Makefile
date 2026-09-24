# Differential + round-trip harness for libphoenix's calendar code, on the host.
#
#   make run   gmtime_r fields and every strftime conversion vs glibc, plus the
#              closed identity timegm(gmtime_r(t)) == t, over ~10.9M comparisons
#              spanning roughly 1600..2400. Runs in a few seconds, no Pi cycle.
#
# time.c needs three Phoenix syscalls (gettime/settime/nsleep) and two macros
# (EOK, SET_ERRNO). shim.h supplies the macros exactly as the target defines
# them; the syscalls are stubbed in tdiff.c and abort() if ever reached, since
# no calendar function calls them.

LIBPH  ?= ../../sources/libphoenix
CFLAGS ?= -std=gnu11 -O1

all: tdiff

time_ph.o: $(LIBPH)/time/time.c shim.h
	$(CC) -c $(CFLAGS) -w -include shim.h -I$(LIBPH) -o time_raw.o $<
	nm time_raw.o | grep ' T ' | awk '{printf "--redefine-sym %s=ph_%s ", $$3, $$3}' > redef.txt
	objcopy $$(cat redef.txt) time_raw.o $@

tdiff: tdiff.c time_ph.o
	$(CC) $(CFLAGS) -Wall -o $@ $^

run: tdiff
	./tdiff

clean:
	rm -f tdiff *.o redef.txt

.PHONY: all run clean
