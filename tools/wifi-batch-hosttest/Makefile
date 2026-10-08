# Host test for the /dev/wifibatch frame format (no Pi, no cross-build).
#
#   make run                                   against sources/
#   make run DEVICES=<devices tree> LWIP=<lwip tree>   e.g. two worktrees
#
# Compiles the daemon's own wifi/rpi4-wifi/wifibatch.h, not a copy, under ASan
# and UBSan, after checking that the netif's drivers/wifibatch.h is identical.

DEVICES ?= ../../sources/phoenix-rtos-devices
LWIP    ?= ../../sources/phoenix-rtos-lwip
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all

HDR_DEV  := $(DEVICES)/wifi/rpi4-wifi/wifibatch.h
HDR_LWIP := $(LWIP)/drivers/wifibatch.h

batchtest: batchtest.c $(HDR_DEV)
	$(CC) $(CFLAGS) -I$(DEVICES)/wifi/rpi4-wifi -o $@ batchtest.c

run: batchtest
	cmp $(HDR_DEV) $(HDR_LWIP)
	./batchtest

clean:
	rm -f batchtest

.PHONY: run clean
