# Host benchmark for libphoenix's allocator. See bench.c.
#
#   make run                      bench the allocator in sources/libphoenix
#   make run SRC=/path/malloc_dl.c LABEL=variant
#                                 bench another copy of the file (a branch
#                                 checkout, a deliberately broken variant, ...)
#   make test [SRC=...] [TESTS=...]
#                                 run the Unity group stdlib_malloc_retain
#                                 (phoenix-rtos-tests libc/stdlib) on the host
#                                 against SRC

LIBPH  ?= ../../sources/libphoenix
SRC    ?= $(LIBPH)/stdlib/malloc_dl.c
LABEL  ?= libphoenix
PAIRS  ?= 200000
OPT    ?= -O2
CFLAGS  = -std=gnu11 $(OPT) -g -Wall -Wno-unused-function -pthread -Istubs -iquote $(LIBPH)/stdlib
BIN     = bench-$(LABEL)

all: $(BIN)

$(BIN): bench.c $(SRC) $(LIBPH)/sys/ulock.c $(LIBPH)/sys/rb.c $(LIBPH)/sys/list.c
	$(CC) $(CFLAGS) -c -o .ulock.o $(LIBPH)/sys/ulock.c
	$(CC) $(CFLAGS) -c -o .rb.o $(LIBPH)/sys/rb.c
	$(CC) $(CFLAGS) -c -o .list.o $(LIBPH)/sys/list.c
	$(CC) $(CFLAGS) '-DMPH_SRC="$(abspath $(SRC))"' -o $@ bench.c .ulock.o .rb.o .list.o

run: $(BIN)
	./$(BIN) $(LABEL) $(PAIRS)

UNITY ?= ../../sources/phoenix-rtos-tests/unity
TESTS ?= ../../sources/phoenix-rtos-tests/libc/stdlib/stdlib_malloc_retain.c
TCFLAGS = -std=gnu11 -O1 -g -Wall -Wno-unused-function -Istubs -I$(UNITY)

test-$(LABEL): alloc.c rename.h test_main.c $(SRC) $(TESTS)
	$(CC) $(CFLAGS) '-DMPH_SRC="$(abspath $(SRC))"' -c -o .alloc-$(LABEL).o alloc.c
	$(CC) $(CFLAGS) -c -o .ulock.o $(LIBPH)/sys/ulock.c
	$(CC) $(CFLAGS) -c -o .rb.o $(LIBPH)/sys/rb.c
	$(CC) $(CFLAGS) -c -o .list.o $(LIBPH)/sys/list.c
	$(CC) $(TCFLAGS) -D__phoenix__ -include rename.h -c -o .tests-$(LABEL).o $(TESTS)
	$(CC) $(TCFLAGS) -o $@ test_main.c $(UNITY)/unity.c $(UNITY)/unity_fixture.c \
		.tests-$(LABEL).o .alloc-$(LABEL).o .ulock.o .rb.o .list.o -pthread

test: test-$(LABEL)
	./test-$(LABEL) -v

clean:
	rm -f bench-* test-* .*.o perf.data*

.PHONY: all run test clean
