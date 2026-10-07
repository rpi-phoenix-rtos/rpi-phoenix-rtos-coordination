# Host harness for posixsrv's timed requests (pty VMIN/VTIME reads, event-queue
# reads with a timeout) and the timeout thread that expires them.
#
# Builds posixsrv's REAL posixsrv.c, event.c and pty.c, with the real libtty,
# libphoenix's rb/list/idtree and the kernel's msg_t, for the host; shim.c
# models the kernel underneath (messages, kernel mutexes, the clock). main.c
# runs posixsrv's server threads and timeout thread and drives them with
# messages as client processes would.
#
#   make run                          build + run, under AddressSanitizer
#   make run SAN=thread               ... under ThreadSanitizer (lock-order inversions)
#   make run POSIXSRV=/path/to/wt     ... against another posixsrv checkout
#   make run ARGS='-n 50 pty_race'    harness arguments: iterations, cases

ROOT     ?= ../..
POSIXSRV ?= $(ROOT)/sources/phoenix-rtos-posixsrv
LIBTTY   ?= $(ROOT)/sources/phoenix-rtos-devices/tty/libtty
LIBPH    ?= $(ROOT)/sources/libphoenix
KERNEL   ?= $(ROOT)/sources/phoenix-rtos-kernel
HCC      ?= gcc
SAN      ?= address,undefined
OPT      ?= -O1

COMMA := ,
OUT := build$(if $(SAN),-$(subst $(COMMA),-,$(SAN)))

CFLAGS := -std=gnu11 $(OPT) -g -Wall -D_GNU_SOURCE -U_FORTIFY_SOURCE -fno-omit-frame-pointer \
	-Ifakeinc -I$(OUT)/inc -I$(LIBTTY) -I$(POSIXSRV) $(if $(SAN),-fsanitize=$(SAN))
PHFLAGS := $(CFLAGS) -include fakeinc/hostcompat.h -Wno-unused-function

PHSRC := $(POSIXSRV)/posixsrv.c $(POSIXSRV)/event.c $(POSIXSRV)/pty.c \
	$(LIBTTY)/libtty.c $(LIBTTY)/libtty_disc.c \
	$(LIBPH)/sys/rb.c $(LIBPH)/sys/list.c $(LIBPH)/posix/idtree.c

all: $(OUT)/posixsrv-host

# Headers taken as they are: the kernel's (as <phoenix/...>) and libphoenix's data structures
$(OUT)/inc/.done:
	mkdir -p $(OUT)/inc/sys $(OUT)/inc/posix
	ln -sfn $(abspath $(KERNEL))/include $(OUT)/inc/phoenix
	for h in rb list minmax events; do ln -sfn $(abspath $(LIBPH))/include/sys/$$h.h $(OUT)/inc/sys/$$h.h; done
	ln -sfn $(abspath $(LIBPH))/include/posix/idtree.h $(OUT)/inc/posix/idtree.h
	touch $@

$(OUT)/posixsrv-host: $(OUT)/inc/.done $(PHSRC) main.c shim.c shim.h $(wildcard fakeinc/*.h fakeinc/*/*.h)
	$(HCC) $(PHFLAGS) -c -o $(OUT)/posixsrv.o $(POSIXSRV)/posixsrv.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/event.o $(POSIXSRV)/event.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/pty.o $(POSIXSRV)/pty.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/libtty.o $(LIBTTY)/libtty.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/libtty_disc.o $(LIBTTY)/libtty_disc.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/rb.o $(LIBPH)/sys/rb.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/list.o $(LIBPH)/sys/list.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/idtree.o $(LIBPH)/posix/idtree.c
	$(HCC) $(CFLAGS) -o $@ main.c shim.c $(OUT)/posixsrv.o $(OUT)/event.o $(OUT)/pty.o \
		$(OUT)/libtty.o $(OUT)/libtty_disc.o $(OUT)/rb.o $(OUT)/list.o $(OUT)/idtree.o -lpthread

run: $(OUT)/posixsrv-host
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 TSAN_OPTIONS="halt_on_error=1 $(TSAN_OPTIONS)" ./$(OUT)/posixsrv-host $(ARGS)

clean:
	rm -rf build build-*

.PHONY: all run clean
