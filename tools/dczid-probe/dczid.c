/*
 * dczid.c -- read DCZID_EL0 on the Pi 4 to settle whether `dc zva` is usable.
 *
 * P3 / TD-20 disabled the `dc zva` fast path in hal_memset() "pending proof of
 * the EL2 DC-ZVA trap state", which does not reproduce in QEMU. That state is
 * directly readable from EL0: DCZID_EL0 is an unprivileged read-only system
 * register.
 *
 *   DZP (bit 4) -- 1 means DC ZVA is PROHIBITED (trapped or disabled)
 *   BS  (bits 3:0) -- log2 of the zeroed block size in words
 *
 * DZP reflects HCR_EL2.TDZ as seen from EL0/EL1, so reading 0 here is the
 * hardware proof the row asks for. Reading it costs nothing and needs no kernel
 * change.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdint.h>

int main(void)
{
	uint64_t dczid = 0;
	unsigned dzp, bs;

	__asm__ volatile("mrs %0, dczid_el0" : "=r"(dczid));

	dzp = (unsigned)((dczid >> 4) & 1u);
	bs = (unsigned)(dczid & 0xfu);

	printf("DCZID raw=0x%llx\n", (unsigned long long)dczid);
	printf("DCZID DZP=%u (%s)\n", dzp,
			(dzp != 0) ? "PROHIBITED -- dc zva traps or is disabled" :
						 "PERMITTED -- dc zva is usable from EL0/EL1");
	printf("DCZID BS=%u (block = %u bytes)\n", bs, 4u << bs);

	if (dzp == 0) {
		/* Prove it by actually doing it: zero a block and check. Aligned to the
		 * reported block size, because dc zva zeroes the block CONTAINING the
		 * address and a misaligned pointer would zero neighbouring bytes. */
		static unsigned char buf[512] __attribute__((aligned(256)));
		unsigned blk = 4u << bs;
		unsigned i;
		unsigned char *p;

		if (blk <= 256u) {
			for (i = 0; i < sizeof(buf); i++) {
				buf[i] = 0xa5;
			}
			p = &buf[256];
			__asm__ volatile("dc zva, %0" : : "r"(p) : "memory");

			for (i = 0; i < blk; i++) {
				if (p[i] != 0) {
					printf("DCZID EXEC=fail byte %u not zeroed\n", i);
					return 1;
				}
			}
			/* the byte just before the block must be untouched */
			if (buf[255] != 0xa5) {
				printf("DCZID EXEC=fail zeroed past the block start\n");
				return 1;
			}
			printf("DCZID EXEC=ok (%u bytes zeroed, neighbours intact)\n", blk);
		}
		else {
			printf("DCZID EXEC=skip (block %u > probe buffer)\n", blk);
		}
	}

	printf("DCZID done\n");
	return 0;
}
