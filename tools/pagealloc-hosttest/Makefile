# Page allocator host simulation: sim-old / sim-new from src-old/ and src-new/ (see sync.sh)
CFLAGS ?= -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-format
all: $(patsubst src-%,sim-%,$(wildcard src-*))
sim-%: sim.c src-%/page.c stub/kstub.h
	$(CC) $(CFLAGS) -iquote src-$* -I stub -o $@ sim.c
clean:
	rm -f sim-*
