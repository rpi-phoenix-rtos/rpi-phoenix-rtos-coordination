# Host test for the usbmouse HID report-descriptor parser (no Pi, no cross-build).
#
#   make run                              against sources/phoenix-rtos-devices
#   make run DEVICES=<devices worktree>   e.g. the mouse-wheel branch
#
# Compiles the driver's own tty/usbmouse/hidmouse.c, not a copy, under ASan and
# UBSan with -Werror, and runs it against real mouse report descriptors plus
# malformed ones and a random/mutation fuzz pass.

DEVICES ?= ../../sources/phoenix-rtos-devices
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all

SRC := $(DEVICES)/tty/usbmouse/hidmouse.c
HDR := $(DEVICES)/tty/usbmouse/hidmouse.h

mousetest: mousetest.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) -I$(DEVICES)/tty/usbmouse -o $@ mousetest.c $(SRC)

run: mousetest
	./mousetest

clean:
	rm -f mousetest

.PHONY: run clean
