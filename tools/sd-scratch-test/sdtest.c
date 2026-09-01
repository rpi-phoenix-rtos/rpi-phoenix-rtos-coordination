/*
 * Phoenix-RTOS RPi4 — SD raw-device scratch read/write test
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Exercises the bcm2711-emmc driver's block read/write path on a SCRATCH
 * region of /dev/mmcblk0 (far past any filesystem), measures throughput, and
 * checks correctness with a deterministic pattern the HOST can regenerate for
 * an independent cache-cold read-back (`sdtest --verify-host` prints the
 * expected pattern parameters). Default offset is 512 MiB in — well clear of
 * the small ext2 boot partition at the start of the card.
 *
 * Pattern: 32-bit word k at scratch holds (uint32_t)(k ^ 0xA5A5A5A5). The host
 * verifier (tools/sd-scratch-test/verify-host.sh) regenerates + compares.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define DEV        "/dev/mmcblk0"
#define SCRATCH_OFF (512ull * 1024 * 1024)   /* 512 MiB: past the boot/root partition */
#define XFER_BYTES  (16u * 1024 * 1024)      /* 16 MiB transfer */
#define PATTERN_XOR 0xA5A5A5A5u
#define WIPE_BYTES  (4u * 1024 * 1024)       /* boot-sector wipe span (MBR + FAT header) */

static double now_s(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0.0;
	}
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void fill_pattern(uint32_t *w, size_t nwords)
{
	size_t k;
	for (k = 0; k < nwords; ++k) {
		w[k] = (uint32_t)k ^ PATTERN_XOR;
	}
}

int main(int argc, char **argv)
{
	static uint8_t wbuf[XFER_BYTES];
	static uint8_t rbuf[XFER_BYTES];
	int fd;
	ssize_t n;
	off_t off;
	double t0, t1, wsec, rsec;
	size_t i, firstbad = (size_t)-1;
	unsigned long long scratch = SCRATCH_OFF;
	int do_write = 1, do_read = 1;
	const char *mode = (argc > 1) ? argv[1] : "both";

	/* Modes: `write` and `verify` split the test across two boots so the
	 * read-back is CACHE-COLD (a power cycle wipes every Pi-side cache) --
	 * the definitive on-disk check without needing a host card reader.
	 *   boot 1: sdtest write   -> writes the pattern, reports write MB/s
	 *   power cycle
	 *   boot 2: sdtest verify  -> reads + compares, reports read MB/s
	 * Default (no arg) does both in one run (in-Pi readback; may be cached). */
	if (strcmp(mode, "write") == 0) {
		do_read = 0;
	}
	else if (strcmp(mode, "verify") == 0) {
		do_write = 0;
	}

	/* Destructive, owner-approved (D5 2026-09-01): zero the first WIPE_BYTES of
	 * the card so the Pi firmware finds nothing bootable and ALWAYS falls
	 * through to netboot -- removes the TFTP-vs-card-FAT boot-source variance
	 * that made measurements non-comparable. Requires an explicit confirmation
	 * token so it can never fire by accident. Owner reflashes the card when he
	 * wants to test SD boot again. */
	if (strcmp(mode, "wipe-boot") == 0) {
		if (argc < 3 || strcmp(argv[2], "CONFIRM-DESTROY-BOOT") != 0) {
			printf("SDTEST-WIPE refused: need `sdtest wipe-boot CONFIRM-DESTROY-BOOT`\n");
			return 2;
		}
		fd = open(DEV, O_RDWR);
		if (fd < 0) {
			printf("SDTEST-WIPE-FAIL open(%s) errno=%d\n", DEV, errno);
			return 1;
		}
		memset(wbuf, 0, XFER_BYTES);
		if (lseek(fd, 0, SEEK_SET) != 0) {
			printf("SDTEST-WIPE-FAIL lseek errno=%d\n", errno);
			close(fd);
			return 1;
		}
		{
			size_t total = WIPE_BYTES, done = 0;
			while (done < total) {
				size_t chunk = ((total - done) > XFER_BYTES) ? XFER_BYTES : (total - done);
				ssize_t w = write(fd, wbuf, chunk);
				if (w <= 0) {
					printf("SDTEST-WIPE-FAIL write at %zu errno=%d\n", done, errno);
					close(fd);
					return 1;
				}
				done += (size_t)w;
			}
		}
		(void)fsync(fd);
		close(fd);
		printf("SDTEST-WIPE OK zeroed first %u MiB (MBR+boot partition gone; card is now raw)\n",
			(unsigned)(WIPE_BYTES / (1024 * 1024)));
		printf("SDTEST-RESULT WIPED (next boot should be a deterministic netboot)\n");
		return 0;
	}

	if (argc > 1 && strcmp(argv[1], "--verify-host") == 0) {
		printf("SDTEST-PATTERN dev=%s off=%llu bytes=%u word_xor=0x%08x\n",
			DEV, scratch, XFER_BYTES, PATTERN_XOR);
		return 0;
	}

	printf("SDTEST: %s mode=%s scratch_off=%llu MiB xfer=%u MiB\n",
		DEV, mode, scratch / (1024 * 1024), XFER_BYTES / (1024 * 1024));

	fd = open(DEV, O_RDWR);
	if (fd < 0) {
		printf("SDTEST-FAIL open(%s) errno=%d\n", DEV, errno);
		return 1;
	}

	fill_pattern((uint32_t *)wbuf, XFER_BYTES / 4);

	/* ---- WRITE ---- */
	wsec = 0.0;
	if (do_write) {
	off = lseek(fd, (off_t)scratch, SEEK_SET);
	if (off != (off_t)scratch) {
		printf("SDTEST-FAIL lseek(write) off=%lld errno=%d\n", (long long)off, errno);
		close(fd);
		return 1;
	}
	t0 = now_s();
	{
		size_t done = 0;
		while (done < XFER_BYTES) {
			n = write(fd, wbuf + done, XFER_BYTES - done);
			if (n <= 0) {
				printf("SDTEST-FAIL write at %zu n=%zd errno=%d\n", done, n, errno);
				close(fd);
				return 1;
			}
			done += (size_t)n;
		}
	}
	(void)fsync(fd);
	t1 = now_s();
	wsec = t1 - t0;
	}

	/* ---- READ BACK (in-Pi; may be cache-served — host read-back is definitive) ---- */
	rsec = 0.0;
	if (do_read) {
	off = lseek(fd, (off_t)scratch, SEEK_SET);
	if (off != (off_t)scratch) {
		printf("SDTEST-FAIL lseek(read) off=%lld errno=%d\n", (long long)off, errno);
		close(fd);
		return 1;
	}
	memset(rbuf, 0, XFER_BYTES);
	t0 = now_s();
	{
		size_t done = 0;
		while (done < XFER_BYTES) {
			n = read(fd, rbuf + done, XFER_BYTES - done);
			if (n <= 0) {
				printf("SDTEST-FAIL read at %zu n=%zd errno=%d\n", done, n, errno);
				close(fd);
				return 1;
			}
			done += (size_t)n;
		}
	}
	t1 = now_s();
	rsec = t1 - t0;
	}
	close(fd);

	/* ---- COMPARE ---- */
	if (do_read) {
	for (i = 0; i < XFER_BYTES; ++i) {
		if (rbuf[i] != wbuf[i]) {
			firstbad = i;
			break;
		}
	}

	}
	{
		double mb = (double)XFER_BYTES / (1024.0 * 1024.0);
		if (do_write) {
			printf("SDTEST-WRITE %.2f MiB in %.3f s = %.2f MB/s\n", mb, wsec, wsec > 0 ? mb / wsec : 0.0);
		}
		if (do_read) {
			printf("SDTEST-READ  %.2f MiB in %.3f s = %.2f MB/s\n", mb, rsec, rsec > 0 ? mb / rsec : 0.0);
		}
	}
	if (!do_read) {
		printf("SDTEST-RESULT WRITE-DONE (power-cycle, then: sdtest verify)\n");
		return 0;
	}
	if (firstbad == (size_t)-1) {
		/* NOTE: this tool cannot know whether a power cycle happened, so it must
		 * not claim cache-coldness itself. `verify` only proves the bytes match;
		 * coldness holds ONLY when the matching `write` ran in an EARLIER boot. */
		printf("SDTEST-COMPARE OK%s\n", do_write ? " (same-boot readback; may be cache-served)" : " (read-back of a previously-written pattern; cache-cold IFF the write ran in an earlier boot)");
		printf("SDTEST-RESULT PASS\n");
		return 0;
	}
	printf("SDTEST-COMPARE MISMATCH firstbad_byte=%zu block=%zu got=0x%02x exp=0x%02x\n",
		firstbad, firstbad / 512, rbuf[firstbad], wbuf[firstbad]);
	printf("SDTEST-RESULT FAIL\n");
	return 1;
}
