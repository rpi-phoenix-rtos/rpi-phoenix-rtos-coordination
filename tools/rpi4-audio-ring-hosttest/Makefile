# Host test for rpi4-audio's streaming-ring bookkeeping (no Pi, no cross-build).
#
#   make run                          against sources/phoenix-rtos-devices
#   make run DEVICES=<devices tree>   against another checkout, e.g. a worktree
#
# Compiles the driver's own audio/rpi4-audio/rpi4-audio-ring.h, not a copy. The
# first thing it runs is a canary: the same checker against a model of the old
# driver, which must report the replay.

DEVICES ?= ../../sources/phoenix-rtos-devices
CFLAGS  ?= -std=gnu11 -O2 -Wall -Wextra -Werror

ringtest: ringtest.c $(DEVICES)/audio/rpi4-audio/rpi4-audio-ring.h
	$(CC) $(CFLAGS) -I$(DEVICES)/audio/rpi4-audio -o $@ ringtest.c

run: ringtest
	./ringtest

clean:
	rm -f ringtest

.PHONY: run clean
