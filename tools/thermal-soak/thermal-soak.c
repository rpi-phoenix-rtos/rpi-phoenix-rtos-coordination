/* SPDX-License-Identifier: BSD-3-Clause
 *
 * thermal-soak.c — run a program and sample SoC temperature + the VideoCore
 * throttle bitmask while it runs.
 *
 * Why this exists
 * ---------------
 * The demo build's stability evidence is a large sample of SHORT runs (132 boots
 * on 2026-09-08, every game gated for ~2 minutes). A live presentation may run a
 * game for tens of minutes, and nothing had ever checked whether the BCM2711
 * throttles under sustained GPU load — which would show up on stage as the frame
 * rate quietly degrading, not as a crash.
 *
 * psh runs commands sequentially and a game never exits, so temperature cannot be
 * sampled from a second psh command. Hence fork(): the child execs the target, the
 * parent polls /dev/thermal and /dev/throttled and prints one line per interval,
 * then reports min/max and whether any throttle bit was ever seen.
 *
 *   thermal-soak <interval-s> <prog> [args...]
 *
 * /dev/thermal is "<milli-Celsius>\n" and /dev/throttled is "0x%08x\n"
 * (devices/sensors/rpi4-thermal/rpi4-thermal.c: thermal_format).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

/* BCM2711 GET_THROTTLED bits. The low nibble is "happening now", the 16.. bits
 * are sticky "has occurred since boot" versions of the same conditions. */
#define THR_UNDERVOLT      (1u << 0)
#define THR_ARM_CAPPED     (1u << 1)
#define THR_THROTTLED      (1u << 2)
#define THR_SOFT_TEMPLIMIT (1u << 3)
#define THR_ANY_NOW        (THR_UNDERVOLT | THR_ARM_CAPPED | THR_THROTTLED | THR_SOFT_TEMPLIMIT)


/* Read a whole small text file. Returns -1 and leaves buf empty on any failure;
 * the caller decides whether that is fatal (it is not — a missing thermal driver
 * should not abort the program under test). */
static int read_text(const char *path, char *buf, size_t size)
{
	ssize_t n;
	int fd = open(path, O_RDONLY);

	buf[0] = '\0';
	if (fd < 0) {
		return -1;
	}
	n = read(fd, buf, size - 1);
	close(fd);
	if (n <= 0) {
		return -1;
	}
	buf[n] = '\0';
	return 0;
}


static void decode_throttle(unsigned v, char *out, size_t size)
{
	/* Deliberately spells out what is set: a bare hex value in a log is the kind
	 * of thing that gets mis-read as "fine" long after the run. */
	if (v == 0u) {
		snprintf(out, size, "none");
		return;
	}
	snprintf(out, size, "%s%s%s%s%s",
			((v & THR_UNDERVOLT) != 0u) ? "under-voltage " : "",
			((v & THR_ARM_CAPPED) != 0u) ? "arm-capped " : "",
			((v & THR_THROTTLED) != 0u) ? "THROTTLED " : "",
			((v & THR_SOFT_TEMPLIMIT) != 0u) ? "soft-temp-limit " : "",
			((v & ~THR_ANY_NOW) != 0u) ? "(sticky-since-boot bits set)" : "");
}


int main(int argc, char **argv)
{
	char buf[64], dec[96];
	unsigned t_mc = 0u, thr = 0u;
	unsigned t_min = ~0u, t_max = 0u, thr_seen = 0u;
	unsigned interval, elapsed = 0u;
	int status = 0;
	pid_t child;

	if (argc < 3) {
		fprintf(stderr, "usage: thermal-soak <interval-s> <prog> [args...]\n");
		return 2;
	}
	interval = (unsigned)strtoul(argv[1], NULL, 10);
	if (interval == 0u) {
		interval = 15u;
	}

	if (read_text("/dev/thermal", buf, sizeof(buf)) == 0) {
		t_mc = (unsigned)strtoul(buf, NULL, 10);
	}
	if (read_text("/dev/throttled", buf, sizeof(buf)) == 0) {
		thr = (unsigned)strtoul(buf, NULL, 0);
	}
	decode_throttle(thr, dec, sizeof(dec));
	printf("thermal-soak: baseline T=%u.%03u C throttle=0x%08x (%s), interval=%us, prog=%s\n",
			t_mc / 1000u, t_mc % 1000u, thr, dec, interval, argv[2]);
	fflush(stdout);

	child = fork();
	if (child < 0) {
		fprintf(stderr, "thermal-soak: fork: %s\n", strerror(errno));
		return 1;
	}
	if (child == 0) {
		execv(argv[2], &argv[2]);
		fprintf(stderr, "thermal-soak: execv '%s': %s\n", argv[2], strerror(errno));
		_exit(127);
	}

	for (;;) {
		pid_t r;

		sleep(interval);
		elapsed += interval;

		if (read_text("/dev/thermal", buf, sizeof(buf)) == 0) {
			t_mc = (unsigned)strtoul(buf, NULL, 10);
			if (t_mc < t_min) {
				t_min = t_mc;
			}
			if (t_mc > t_max) {
				t_max = t_mc;
			}
		}
		if (read_text("/dev/throttled", buf, sizeof(buf)) == 0) {
			thr = (unsigned)strtoul(buf, NULL, 0);
			thr_seen |= thr;
		}
		decode_throttle(thr, dec, sizeof(dec));
		printf("thermal-soak: t=%us T=%u.%03u C throttle=0x%08x (%s)\n",
				elapsed, t_mc / 1000u, t_mc % 1000u, thr, dec);
		fflush(stdout);

		/* WNOHANG so sampling continues for the whole life of the child rather
		 * than blocking here; the loop ends when the child is gone. */
		r = waitpid(child, &status, WNOHANG);
		if (r == child) {
			break;
		}
	}

	decode_throttle(thr_seen, dec, sizeof(dec));
	printf("thermal-soak: DONE after %us — T min %u.%03u C, max %u.%03u C; "
			"throttle bits ever seen 0x%08x (%s)\n",
			elapsed, t_min / 1000u, t_min % 1000u, t_max / 1000u, t_max % 1000u,
			thr_seen, dec);
	fflush(stdout);
	return 0;
}
