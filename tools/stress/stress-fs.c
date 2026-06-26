/*
 * Phoenix-RTOS Raspberry Pi 4 — FILE-I/O stress / micro-benchmark util (task #39).
 *
 * Exercises the file path on NFS (the working rootfs path, /nfstest — NOT the SD
 * card, whose large-read EIO is a separate known issue). All test files live
 * under <base>/stress/, created at startup (mkdir, EEXIST ignored).
 *
 *   stress-fs seq    [MB]      sequential write then read-back of one large file;
 *                              verifies the written pattern, reports MB/s.
 *   stress-fs rand   [OPS]     random-offset pwrite/pread over a file; each read
 *                              verifies the last value written at that offset.
 *   stress-fs many   [COUNT]   create+write+read+unlink COUNT small files.
 *   stress-fs conc   [THREADS] N threads, each hammering its OWN file (seq write
 *                              + verify read-back) concurrently.
 *   stress-fs all               seq + rand + many + conc at default intensities.
 *
 *   Optional final argv: a base dir overriding /nfstest (e.g. stress-fs seq 8 /tmp).
 *
 * Integrity is the headline: every read verifies the exact bytes written; a
 * mismatch is a FAULT (silent corruption / lost update). A short read or write
 * (fewer bytes than requested, with no error) is also a FAULT — the POSIX
 * contract for a regular file is no short transfer absent EOF/signal/error.
 * ENOSPC is a legitimate LIMIT; EIO and other errnos are classified per
 * sr_errno_is_limit(). Throughput is reported so repeated runs reveal slowdown.
 *
 * KNOWN HAZARD (see project memory): NFS large WRITES stall past a few KB on the
 * current port and flood getservbyport — it manifests as a HANG/flood, not a
 * clean errno. Heartbeats bracket every write so a wedge localizes to the exact
 * op, and the default seq size is kept modest so the orchestrator's HW run does
 * not lock up. A stall surfaces as a missing heartbeat, to be read as a finding.
 *
 * Generous (>=16 KB) thread stacks — the Pi4 fs pool-thread stack-overflow
 * history (#120/#152).
 *
 * Host-side build only (build-stress-pfi.sh). Static aarch64-phoenix.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/threads.h>

#include "stress.h"

#define SUITE "fs"

#define DEFAULT_BASE "/nfstest"

/* Defaults sized to stress meaningfully without wedging the HW run given the
 * known NFS large-write stall. seq writes 4 MB in 64 KB chunks. */
#define DEFAULT_SEQ_MB     4
#define DEFAULT_RAND_OPS   256
#define DEFAULT_MANY_COUNT 128
#define DEFAULT_CONC_THR   4

#define IO_CHUNK    (64u * 1024u)   /* write/read unit for seq + conc */
#define RAND_RECORD 256u            /* fixed-size record for random-offset I/O */
#define RAND_SLOTS  64u             /* number of record slots in the rand file */
#define MANY_BYTES  512u            /* per-file payload in `many` mode */

/* Per-thread stack for the concurrent mode. 64 KB — well above the >=16 KB floor
 * the #120/#152 fs pool-thread stack-overflow history demands. */
#define CONC_STACKSZ (64u * 1024u)
#define MAX_CONC     16

/* A reproducible byte pattern keyed by a 64-bit position so any read can verify
 * exactly what should be there — catches lost updates and cross-file bleed. */
static unsigned char pat_byte(unsigned long long pos, unsigned int salt)
{
	unsigned long long x = pos * 1099511628211ULL + 1469598103934665603ULL + salt;
	x ^= x >> 33;
	return (unsigned char)x;
}

static void fill_pattern(unsigned char *buf, size_t n, unsigned long long base, unsigned int salt)
{
	size_t i;
	for (i = 0; i < n; i++)
		buf[i] = pat_byte(base + i, salt);
}

/* Returns -1 and the offset of the first mismatch in *bad, or 0 if all match. */
static int check_pattern(const unsigned char *buf, size_t n, unsigned long long base,
	unsigned int salt, size_t *bad)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (buf[i] != pat_byte(base + i, salt)) {
			if (bad != NULL)
				*bad = i;
			return -1;
		}
	}
	return 0;
}

static long ms_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* full write: loop until all `n` bytes are out, or an error. Returns bytes
 * written (== n on success), or -1 with errno set. A 0-return from write() with
 * bytes still pending is treated as a short-write fault (errno left 0). */
static ssize_t write_full(int fd, const void *buf, size_t n)
{
	const unsigned char *p = buf;
	size_t off = 0;
	while (off < n) {
		ssize_t w = write(fd, p + off, n - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (w == 0) {
			errno = 0; /* signal "short write, no error" to the caller */
			return (ssize_t)off;
		}
		off += (size_t)w;
	}
	return (ssize_t)off;
}

static ssize_t read_full(int fd, void *buf, size_t n)
{
	unsigned char *p = buf;
	size_t off = 0;
	while (off < n) {
		ssize_t r = read(fd, p + off, n - off);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)
			break; /* EOF */
		off += (size_t)r;
	}
	return (ssize_t)off;
}


/* ---- seq: write a large file sequentially, read it back, verify ---- */
static void run_seq(sr_tally_t *t, const char *base, int mb)
{
	char path[256];
	unsigned char *buf;
	int fd;
	long total = (long)mb * 1024 * 1024;
	long done = 0;
	long t0, t1, t2;

	snprintf(path, sizeof(path), "%s/stress/seq.dat", base);
	buf = malloc(IO_CHUNK);
	if (buf == NULL) {
		SR_FAULT(t, SUITE, "seq", "cannot allocate %u-byte IO buffer", IO_CHUNK);
		return;
	}

	SR_HB(SUITE, "seq write open %s (%ld bytes)", path, total);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		if (sr_errno_is_limit(errno))
			SR_LIMIT(t, SUITE, "seq", "open(write) errno=%d (%s)", errno, strerror(errno));
		else
			SR_FAULT(t, SUITE, "seq", "open(write) errno=%d (%s)", errno, strerror(errno));
		free(buf);
		return;
	}

	t0 = ms_now();
	while (done < total) {
		size_t chunk = (total - done < (long)IO_CHUNK) ? (size_t)(total - done) : IO_CHUNK;
		ssize_t w;
		fill_pattern(buf, chunk, (unsigned long long)done, 0);
		SR_HB(SUITE, "seq write off=%ld", done); /* a stall pins to this offset */
		w = write_full(fd, buf, chunk);
		if (w < 0) {
			if (errno == ENOSPC)
				SR_LIMIT(t, SUITE, "seq", "ENOSPC at off=%ld (disk full)", done);
			else if (sr_errno_is_limit(errno))
				SR_LIMIT(t, SUITE, "seq", "write errno=%d (%s) at off=%ld",
					errno, strerror(errno), done);
			else
				SR_FAULT(t, SUITE, "seq", "write errno=%d (%s) at off=%ld",
					errno, strerror(errno), done);
			close(fd);
			free(buf);
			return;
		}
		if ((size_t)w != chunk) {
			SR_FAULT(t, SUITE, "seq", "short write %zd/%zu at off=%ld (no error)",
				w, chunk, done);
			close(fd);
			free(buf);
			return;
		}
		done += w;
	}
	close(fd);
	t1 = ms_now();
	SR_HB(SUITE, "seq write done %ld bytes", done);

	/* Read back + verify the pattern. */
	fd = open(path, O_RDONLY, 0);
	if (fd < 0) {
		SR_FAULT(t, SUITE, "seq", "reopen(read) errno=%d (%s)", errno, strerror(errno));
		free(buf);
		return;
	}
	done = 0;
	while (done < total) {
		size_t chunk = (total - done < (long)IO_CHUNK) ? (size_t)(total - done) : IO_CHUNK;
		size_t bad = 0;
		ssize_t r;
		SR_HB(SUITE, "seq read off=%ld", done);
		r = read_full(fd, buf, chunk);
		if (r < 0) {
			SR_FAULT(t, SUITE, "seq", "read errno=%d (%s) at off=%ld",
				errno, strerror(errno), done);
			close(fd);
			free(buf);
			return;
		}
		if ((size_t)r != chunk) {
			SR_FAULT(t, SUITE, "seq", "short read %zd/%zu at off=%ld (file truncated?)",
				r, chunk, done);
			close(fd);
			free(buf);
			return;
		}
		if (check_pattern(buf, chunk, (unsigned long long)done, 0, &bad) != 0) {
			SR_FAULT(t, SUITE, "seq", "pattern mismatch at byte off=%ld (corruption)",
				done + (long)bad);
			close(fd);
			free(buf);
			return;
		}
		done += r;
	}
	close(fd);
	t2 = ms_now();

	{
		double wsec = (t1 - t0) / 1000.0;
		double rsec = (t2 - t1) / 1000.0;
		double wmbps = wsec > 0 ? (mb / wsec) : 0;
		double rmbps = rsec > 0 ? (mb / rsec) : 0;
		SR_OK(t, SUITE, "seq", "%d MB write=%.2f MB/s read=%.2f MB/s verified",
			mb, wmbps, rmbps);
	}
	free(buf);
}


/* ---- rand: random-offset pwrite/pread, verify last value per slot ---- */
static void run_rand(sr_tally_t *t, const char *base, int ops)
{
	char path[256];
	unsigned char wbuf[RAND_RECORD], rbuf[RAND_RECORD];
	/* track the salt last written to each slot so reads verify the live value */
	unsigned int *slot_salt = calloc(RAND_SLOTS, sizeof(unsigned int));
	char *slot_written = calloc(RAND_SLOTS, 1);
	int fd, i;
	unsigned int seed = 0x1234abcdu;

	snprintf(path, sizeof(path), "%s/stress/rand.dat", base);
	if (slot_salt == NULL || slot_written == NULL) {
		SR_FAULT(t, SUITE, "rand", "cannot allocate slot tables");
		free(slot_salt);
		free(slot_written);
		return;
	}

	SR_HB(SUITE, "rand open %s", path);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		if (sr_errno_is_limit(errno))
			SR_LIMIT(t, SUITE, "rand", "open errno=%d (%s)", errno, strerror(errno));
		else
			SR_FAULT(t, SUITE, "rand", "open errno=%d (%s)", errno, strerror(errno));
		free(slot_salt);
		free(slot_written);
		return;
	}

	for (i = 0; i < ops; i++) {
		unsigned int slot, salt;
		off_t off;
		ssize_t n;

		seed = seed * 1103515245u + 12345u;
		slot = (seed >> 8) % RAND_SLOTS;
		off = (off_t)slot * RAND_RECORD;

		if ((i & 1) == 0 || slot_written[slot] == 0) {
			/* WRITE this slot a fresh salted pattern. */
			salt = seed ^ (unsigned int)i;
			fill_pattern(wbuf, RAND_RECORD, slot, salt);
			if ((i % 32) == 0)
				SR_HB(SUITE, "rand op=%d pwrite slot=%u", i, slot);
			n = pwrite(fd, wbuf, RAND_RECORD, off);
			if (n < 0) {
				if (errno == ENOSPC) { SR_LIMIT(t, SUITE, "rand", "ENOSPC at op=%d", i); goto done; }
				if (sr_errno_is_limit(errno)) { SR_LIMIT(t, SUITE, "rand", "pwrite errno=%d (%s) op=%d", errno, strerror(errno), i); goto done; }
				SR_FAULT(t, SUITE, "rand", "pwrite errno=%d (%s) op=%d", errno, strerror(errno), i);
				goto done;
			}
			if (n != (ssize_t)RAND_RECORD) {
				SR_FAULT(t, SUITE, "rand", "short pwrite %zd/%u op=%d (no error)", n, RAND_RECORD, i);
				goto done;
			}
			slot_salt[slot] = salt;
			slot_written[slot] = 1;
		}
		else {
			/* READ this slot and verify the last value written there. */
			size_t bad = 0;
			if ((i % 32) == 0)
				SR_HB(SUITE, "rand op=%d pread slot=%u", i, slot);
			n = pread(fd, rbuf, RAND_RECORD, off);
			if (n < 0) {
				if (sr_errno_is_limit(errno)) { SR_LIMIT(t, SUITE, "rand", "pread errno=%d (%s) op=%d", errno, strerror(errno), i); goto done; }
				SR_FAULT(t, SUITE, "rand", "pread errno=%d (%s) op=%d", errno, strerror(errno), i);
				goto done;
			}
			if (n != (ssize_t)RAND_RECORD) {
				SR_FAULT(t, SUITE, "rand", "short pread %zd/%u op=%d (no error)", n, RAND_RECORD, i);
				goto done;
			}
			if (check_pattern(rbuf, RAND_RECORD, slot, slot_salt[slot], &bad) != 0) {
				SR_FAULT(t, SUITE, "rand", "pread slot=%u mismatch at +%zu op=%d (lost update/corruption)",
					slot, bad, i);
				goto done;
			}
		}
	}

	SR_OK(t, SUITE, "rand", "%d random-offset ops verified over %u slots", ops, RAND_SLOTS);
done:
	close(fd);
	free(slot_salt);
	free(slot_written);
}


/* ---- many: create/write/read/unlink many small files ---- */
static void run_many(sr_tally_t *t, const char *base, int count)
{
	char path[256];
	unsigned char wbuf[MANY_BYTES], rbuf[MANY_BYTES];
	int i, made = 0;
	long t0, t1;

	SR_HB(SUITE, "many start count=%d", count);
	t0 = ms_now();

	for (i = 0; i < count; i++) {
		int fd;
		ssize_t n;
		size_t bad = 0;

		snprintf(path, sizeof(path), "%s/stress/many_%d.dat", base, i);
		fill_pattern(wbuf, MANY_BYTES, (unsigned long long)i << 20, (unsigned int)i);

		fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
			if (sr_errno_is_limit(errno)) {
				SR_LIMIT(t, SUITE, "many", "open errno=%d (%s) at file=%d (created=%d)",
					errno, strerror(errno), i, made);
				goto done;
			}
			SR_FAULT(t, SUITE, "many", "open errno=%d (%s) at file=%d", errno, strerror(errno), i);
			goto done;
		}
		n = write_full(fd, wbuf, MANY_BYTES);
		if (n < 0) {
			if (errno == ENOSPC) { close(fd); SR_LIMIT(t, SUITE, "many", "ENOSPC at file=%d (created=%d)", i, made); goto done; }
			SR_FAULT(t, SUITE, "many", "write errno=%d (%s) at file=%d", errno, strerror(errno), i);
			close(fd);
			goto done;
		}
		if (n != (ssize_t)MANY_BYTES) {
			SR_FAULT(t, SUITE, "many", "short write %zd/%u at file=%d (no error)", n, MANY_BYTES, i);
			close(fd);
			goto done;
		}
		if (lseek(fd, 0, SEEK_SET) != 0) {
			SR_FAULT(t, SUITE, "many", "lseek errno=%d (%s) at file=%d", errno, strerror(errno), i);
			close(fd);
			goto done;
		}
		n = read_full(fd, rbuf, MANY_BYTES);
		if (n != (ssize_t)MANY_BYTES) {
			SR_FAULT(t, SUITE, "many", "short/err read %zd/%u at file=%d errno=%d",
				n, MANY_BYTES, i, errno);
			close(fd);
			goto done;
		}
		if (check_pattern(rbuf, MANY_BYTES, (unsigned long long)i << 20, (unsigned int)i, &bad) != 0) {
			SR_FAULT(t, SUITE, "many", "pattern mismatch file=%d at +%zu (corruption)", i, bad);
			close(fd);
			goto done;
		}
		close(fd);
		made++;

		if (unlink(path) != 0) {
			SR_FAULT(t, SUITE, "many", "unlink errno=%d (%s) at file=%d", errno, strerror(errno), i);
			goto done;
		}
		if ((i % 32) == 0)
			SR_HB(SUITE, "many file=%d made=%d", i, made);
	}

	t1 = ms_now();
	SR_OK(t, SUITE, "many", "%d files create+write+read+unlink verified in %ld ms",
		made, t1 - t0);
	return;
done:
	/* Best-effort cleanup of whatever survived. */
	for (i = 0; i < count; i++) {
		snprintf(path, sizeof(path), "%s/stress/many_%d.dat", base, i);
		(void)unlink(path);
	}
}


/* ---- conc: N threads, each hammering its own file ---- */
typedef struct {
	const char *base;
	int idx;
	int mb;          /* size per thread file, in MB */
	volatile int rc; /* 0=ok, 1=fault, 2=limit; set before exit */
	char detail[160];
} conc_arg_t;

static void conc_thread(void *arg)
{
	conc_arg_t *a = arg;
	char path[256];
	unsigned char *buf = malloc(IO_CHUNK);
	int fd;
	long total = (long)a->mb * 1024 * 1024;
	long done;

	a->rc = 0;
	a->detail[0] = '\0';
	if (buf == NULL) {
		a->rc = 1;
		snprintf(a->detail, sizeof(a->detail), "thr=%d malloc failed", a->idx);
		endthread();
	}

	snprintf(path, sizeof(path), "%s/stress/conc_%d.dat", a->base, a->idx);

	/* write own file */
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		a->rc = sr_errno_is_limit(errno) ? 2 : 1;
		snprintf(a->detail, sizeof(a->detail), "thr=%d open(w) errno=%d (%s)", a->idx, errno, strerror(errno));
		free(buf);
		endthread();
	}
	for (done = 0; done < total; ) {
		size_t chunk = (total - done < (long)IO_CHUNK) ? (size_t)(total - done) : IO_CHUNK;
		ssize_t w;
		/* salt = thread idx so a cross-file bleed (thread A reading B's bytes)
		 * is caught by the per-thread salt on read-back. */
		fill_pattern(buf, chunk, (unsigned long long)done, (unsigned int)(a->idx + 1));
		w = write_full(fd, buf, chunk);
		if (w < 0) {
			a->rc = (errno == ENOSPC || sr_errno_is_limit(errno)) ? 2 : 1;
			snprintf(a->detail, sizeof(a->detail), "thr=%d write errno=%d (%s) off=%ld", a->idx, errno, strerror(errno), done);
			close(fd); free(buf); endthread();
		}
		if ((size_t)w != chunk) {
			a->rc = 1;
			snprintf(a->detail, sizeof(a->detail), "thr=%d short write %zd/%zu off=%ld", a->idx, w, chunk, done);
			close(fd); free(buf); endthread();
		}
		done += w;
	}
	close(fd);

	/* read back + verify own file */
	fd = open(path, O_RDONLY, 0);
	if (fd < 0) {
		a->rc = 1;
		snprintf(a->detail, sizeof(a->detail), "thr=%d open(r) errno=%d (%s)", a->idx, errno, strerror(errno));
		free(buf); endthread();
	}
	for (done = 0; done < total; ) {
		size_t chunk = (total - done < (long)IO_CHUNK) ? (size_t)(total - done) : IO_CHUNK;
		size_t bad = 0;
		ssize_t r = read_full(fd, buf, chunk);
		if (r != (ssize_t)chunk) {
			a->rc = 1;
			snprintf(a->detail, sizeof(a->detail), "thr=%d short/err read %zd/%zu off=%ld errno=%d", a->idx, r, chunk, done, errno);
			close(fd); free(buf); endthread();
		}
		if (check_pattern(buf, chunk, (unsigned long long)done, (unsigned int)(a->idx + 1), &bad) != 0) {
			a->rc = 1;
			snprintf(a->detail, sizeof(a->detail), "thr=%d mismatch off=%ld (corruption/cross-file bleed)", a->idx, done + (long)bad);
			close(fd); free(buf); endthread();
		}
		done += r;
	}
	close(fd);
	(void)unlink(path);
	free(buf);
	endthread();
}

static void run_conc(sr_tally_t *t, const char *base, int nthr)
{
	static conc_arg_t args[MAX_CONC];
	void *stacks[MAX_CONC] = { 0 };
	handle_t tids[MAX_CONC];
	int i, started = 0, faults = 0, limits = 0;
	long t0, t1;
	/* 1 MB per thread keeps total NFS write traffic bounded on the HW run. */
	const int per_thr_mb = 1;

	if (nthr > MAX_CONC)
		nthr = MAX_CONC;
	if (nthr < 1)
		nthr = 1;

	SR_HB(SUITE, "conc start threads=%d (%d MB each)", nthr, per_thr_mb);
	t0 = ms_now();

	for (i = 0; i < nthr; i++) {
		args[i].base = base;
		args[i].idx = i;
		args[i].mb = per_thr_mb;
		args[i].rc = -1;
		stacks[i] = malloc(CONC_STACKSZ);
		if (stacks[i] == NULL) {
			SR_LIMIT(t, SUITE, "conc", "cannot allocate %u-byte stack for thr=%d (started=%d)",
				CONC_STACKSZ, i, started);
			break;
		}
		{
			/* beginthreadex returns 0/EOK on success and writes the new tid into
			 * the handle_t out-param; threadJoin() then waits on that tid. */
			int rc = beginthreadex(conc_thread, 4, stacks[i], CONC_STACKSZ, &args[i],
				&tids[i]);
			if (rc < 0) {
				SR_LIMIT(t, SUITE, "conc", "beginthreadex failed rc=%d for thr=%d (started=%d)",
					rc, i, started);
				free(stacks[i]);
				stacks[i] = NULL;
				break;
			}
		}
		started++;
	}

	/* Join every started thread, then classify its result. */
	for (i = 0; i < started; i++) {
		(void)threadJoin(tids[i], 0);
		if (args[i].rc == 1) {
			faults++;
			SR_FAULT(t, SUITE, "conc", "%s", args[i].detail);
		}
		else if (args[i].rc == 2) {
			limits++;
			SR_LIMIT(t, SUITE, "conc", "%s", args[i].detail);
		}
		free(stacks[i]);
		stacks[i] = NULL;
	}

	t1 = ms_now();
	if (faults == 0 && started > 0) {
		double sec = (t1 - t0) / 1000.0;
		double agg = sec > 0 ? ((double)started * per_thr_mb / sec) : 0;
		SR_OK(t, SUITE, "conc", "%d threads each %d MB write+verify, aggregate=%.2f MB/s (%ld ms)%s",
			started, per_thr_mb, agg, t1 - t0, limits ? " [some limited]" : "");
	}
}


int main(int argc, char *argv[])
{
	sr_tally_t tally = { 0 };
	const char *mode = (argc >= 2) ? argv[1] : "all";
	long intensity = (argc >= 3) ? strtol(argv[2], NULL, 10) : 0;
	const char *base = (argc >= 4) ? argv[3] : DEFAULT_BASE;
	char dir[256];
	int do_seq = 0, do_rand = 0, do_many = 0, do_conc = 0;

	sr_init();

	/* Create <base>/stress up front (EEXIST is fine). */
	snprintf(dir, sizeof(dir), "%s/stress", base);
	if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
		if (sr_errno_is_limit(errno))
			SR_LIMIT(&tally, SUITE, "setup", "mkdir %s errno=%d (%s)", dir, errno, strerror(errno));
		else
			SR_FAULT(&tally, SUITE, "setup", "mkdir %s errno=%d (%s)", dir, errno, strerror(errno));
		SR_SUMMARY(&tally, SUITE, "mode=%s base=%s (setup failed)", mode, base);
		return 0;
	}
	SR_HB(SUITE, "base=%s dir=%s ready", base, dir);

	if (strcmp(mode, "seq") == 0)
		do_seq = 1;
	else if (strcmp(mode, "rand") == 0)
		do_rand = 1;
	else if (strcmp(mode, "many") == 0)
		do_many = 1;
	else if (strcmp(mode, "conc") == 0)
		do_conc = 1;
	else
		do_seq = do_rand = do_many = do_conc = 1; /* "all" */

	if (do_seq)
		run_seq(&tally, base, intensity > 0 && (mode[0] == 's') ? (int)intensity : DEFAULT_SEQ_MB);
	if (do_rand)
		run_rand(&tally, base, intensity > 0 && (mode[0] == 'r') ? (int)intensity : DEFAULT_RAND_OPS);
	if (do_many)
		run_many(&tally, base, intensity > 0 && (mode[0] == 'm') ? (int)intensity : DEFAULT_MANY_COUNT);
	if (do_conc)
		run_conc(&tally, base, intensity > 0 && (mode[0] == 'c') ? (int)intensity : DEFAULT_CONC_THR);

	SR_SUMMARY(&tally, SUITE, "mode=%s base=%s", mode, base);
	return 0;
}
