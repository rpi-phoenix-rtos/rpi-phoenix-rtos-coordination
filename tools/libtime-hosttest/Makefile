# Differential + round-trip harness for libphoenix's calendar code, on the host.
#
#   make run   gmtime_r fields and every strftime conversion vs glibc, plus the
#              closed identity timegm(gmtime_r(t)) == t, over ~10.9M comparisons
#              spanning roughly 1600..2400. Runs in a few seconds, no Pi cycle.
#            + tzdiff: POSIX TZ strings -- tzset/localtime_r/mktime/ctime_r/
#              strftime %Z %z vs glibc, every DST change 1970..2100 probed at
#              -1/0/+1 s and across the skipped and repeated hours.
#
#   make run LIBPH=<libphoenix checkout>   test another tree (a worktree, or an
#              old revision to prove the tests can fail).
#
#   make unity [TESTS=<phoenix-rtos-tests checkout>]   build and run the Pi's
#              time_tz Unity group (libc/time/tz.c) natively against the same
#              time.c, to see it pass or fail before a Pi cycle.
#
# time.c needs three Phoenix syscalls (gettime/settime/nsleep) and two macros
# (EOK, SET_ERRNO). shim.h supplies the macros exactly as the target defines
# them; the syscalls are stubbed in stubs.c and abort() if ever reached, since
# no calendar function calls them. The time zone mutex is modelled in stubs.c.
#
# Every symbol time.c DEFINES is renamed ph_*, data included: tzname, timezone
# and daylight must not interpose on glibc's, or the comparison reads one
# library's variables through both names.

LIBPH  ?= ../../sources/libphoenix
TESTS  ?= ../../sources/phoenix-rtos-tests
CFLAGS ?= -std=gnu11 -O1

all: tdiff tzdiff

time_ph.o: $(LIBPH)/time/time.c shim.h
	$(CC) -c $(CFLAGS) -w -include shim.h -Ishim -I$(LIBPH) -o time_raw.o $<
	nm time_raw.o | grep -E ' [TBDC] ' | awk '{printf "--redefine-sym %s=ph_%s ", $$3, $$3}' > redef.txt
	objcopy $$(cat redef.txt) time_raw.o $@

tdiff: tdiff.c time_ph.o stubs.c
	$(CC) $(CFLAGS) -Wall -o $@ $^

tzdiff: tzdiff.c time_ph.o stubs.c
	$(CC) $(CFLAGS) -Wall -o $@ $^

run: tdiff tzdiff
	./tdiff
	./tzdiff

# The group's calls are renamed to the ph_* symbols; __phoenix__ enables its
# libphoenix-only assertions.
UNITY_RENAME := -Dtzset=ph_tzset -Dlocaltime_r=ph_localtime_r -Dmktime=ph_mktime 	-Dstrftime=ph_strftime -Dctime_r=ph_ctime_r -Dtzname=ph_tzname 	-Dtimezone=ph_timezone -Ddaylight=ph_daylight

tzunity: unity_main.c time_ph.o stubs.c $(TESTS)/libc/time/tz.c
	$(CC) $(CFLAGS) -w -D__phoenix__ $(UNITY_RENAME) -I$(TESTS)/unity -o $@ 		$(TESTS)/libc/time/tz.c unity_main.c $(TESTS)/unity/unity.c $(TESTS)/unity/unity_fixture.c 		time_ph.o stubs.c

unity: tzunity
	./tzunity -v

clean:
	rm -f tdiff tzdiff tzunity *.o redef.txt

.PHONY: all run unity clean
