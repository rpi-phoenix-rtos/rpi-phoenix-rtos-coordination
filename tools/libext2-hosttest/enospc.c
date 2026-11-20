/* A write() that runs out of space part way.
 *
 *   ./enospc <img> <wsz> <skew> <end>
 *      wsz   bytes per write() while filling the disk
 *      skew  bytes written first, so every later write starts mid-block
 *      end   0 = unmount at the end, 1 = just stop (power cut)
 *
 * POSIX: a write() that runs out of space returns the bytes it did write, and
 * the file's size and i_blocks match them; one that writes nothing returns
 * -ENOSPC and leaves the inode, the free count and the bitmap as they were.
 *
 *  1. fill   append wsz-byte writes to "f" until one comes up short; check
 *            every return value, the size and i_blocks after each call, and
 *            read every accepted byte back.
 *  2. holes  with exactly K blocks free, write one block into a hole of a
 *            sparse file. The hole needs `need` blocks (the data block plus the
 *            missing indirect blocks above it): K >= need must succeed, and
 *            K < need must fail and change nothing.
 *  3. big    a long write into a hole inside a file, with too little space.
 *  4. append  with exactly K blocks free (K = 0..6), append 8 blocks across
 *            the start of the double indirect range: the write must take
 *            exactly as many blocks as fit, front to back, counting the
 *            indirect blocks they need -- no fewer.
 *
 * e2fsck -fn then judges the image (run-all.sh).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ext2.h"
#include "ext2io.h"
#include "sb.h"
#include "gdt.h"
#include "obj.h"

#define ROOT_INO 2

static int devFd = -1;
static ssize_t hr(id_t i, off_t o, char *b, size_t l) { (void)i; return pread(devFd, b, l, o); }
static ssize_t hw(id_t i, off_t o, const char *b, size_t l)
{
	(void)i;
	/* Nothing of the filesystem lives in the first 1 KiB of the device (the
	 * superblock starts at 1024), so a write there is a stray one: at 1 KiB
	 * blocks e2fsck would never notice it. */
	if (o < 1024) {
		printf("  [FAIL] write of %zu bytes at device offset %lld\n", l, (long long)o);
		abort();
	}
	return pwrite(devFd, b, l, o);
}

static ext2_t *fs;
static uint32_t bs, addr;
static int fails;

#define FAIL(...) do { printf("  [FAIL] " __VA_ARGS__); printf("\n"); fails++; } while (0)


static unsigned char pat(id_t f, off_t pos)
{
	return (unsigned char)((pos * 7) + (pos >> 11) + (f * 31) + 1);
}


static void fillPat(id_t f, off_t pos, unsigned char *b, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		b[i] = pat(f, pos + (off_t)i);
	}
}


typedef struct {
	long long size;
	uint32_t blocks;
	uint32_t freeBlocks;
} state_t;


static state_t stateOf(id_t f)
{
	state_t s;
	ext2_obj_t *o = ext2_obj_get(fs, f);
	s.size = o->inode->size;
	s.blocks = o->inode->blocks;
	ext2_obj_put(fs, o);
	s.freeBlocks = fs->sb->freeBlocks;
	return s;
}


/* i_blocks of a file with data blocks 0..n-1 and no holes */
static uint32_t expectBlocks(uint32_t n)
{
	uint32_t ind = 0;

	if (n > 12) {
		ind++;
	}
	if (n > 12 + addr) {
		ind += 1 + ((n - 12 - addr) + addr - 1) / addr;
	}

	return (n + ind) * (bs / 512);
}


static id_t mk(const char *nm)
{
	id_t f;
	if (ext2_create(fs, ROOT_INO, nm, strlen(nm), NULL, S_IFREG | 0644, &f) < 0) {
		printf("create %s failed\n", nm);
		exit(2);
	}
	return f;
}


static ssize_t put(id_t f, off_t pos, size_t len)
{
	unsigned char *b = malloc(len);
	fillPat(f, pos, b, len);
	ssize_t r = ext2_pwrite(fs, f, pos, (char *)b, len);
	free(b);
	return r;
}


/* Reads [pos, pos + len) back: the pattern where `written`, zeros elsewhere */
static void verify(const char *nm, id_t f, off_t pos, size_t len, int written)
{
	unsigned char *b = malloc(len), *w = malloc(len);
	ssize_t r = ext2_read(fs, f, pos, (char *)b, len);

	if (written) {
		fillPat(f, pos, w, len);
	}
	else {
		memset(w, 0, len);
	}

	if ((r != (ssize_t)len) || (memcmp(b, w, len) != 0)) {
		FAIL("%s: read back [%lld, +%zu) r=%zd: %s", nm, (long long)pos, len, r, written ? "not the data written" : "not zeros");
	}

	free(b);
	free(w);
}


/* A write that wrote nothing must have changed nothing */
static void unchanged(const char *what, state_t a, state_t b)
{
	if ((a.size != b.size) || (a.blocks != b.blocks) || (a.freeBlocks != b.freeBlocks)) {
		FAIL("%s changed state: size %lld->%lld i_blocks %u->%u free %u->%u", what,
			a.size, b.size, a.blocks, b.blocks, a.freeBlocks, b.freeBlocks);
	}
}


/* A pad file whose single indirect block exists, so each further block it
 * takes or gives back is exactly one block of free space. */
static id_t pad;

static void setFree(uint32_t k)
{
	if (ext2_truncate(fs, pad, 13 * bs) < 0) {
		FAIL("pad truncate");
	}
	if (fs->sb->freeBlocks < k) {
		printf("cannot make %u blocks free, only %u\n", k, fs->sb->freeBlocks);
		exit(2);
	}
	for (off_t pos = 13 * bs; fs->sb->freeBlocks > k; pos += bs) {
		if ((pos >= (off_t)(12 + addr) * bs) || (put(pad, pos, bs) != (ssize_t)bs)) {
			printf("pad failed at %lld (free %u, want %u)\n", (long long)pos, fs->sb->freeBlocks, k);
			exit(2);
		}
	}
}


typedef struct {
	const char *region;
	uint32_t hole;   /* logical block to write */
	uint32_t far;    /* logical block written beforehand, sharing the hole's upper indirect blocks */
	uint32_t need;   /* blocks the write must allocate */
	uint32_t cut;    /* if not 0, truncated to this many blocks after `far` is written */
} holecase_t;


int main(int argc, char **argv)
{
	if (argc < 5) {
		printf("usage: %s <img> <wsz> <skew> <end>\n", argv[0]);
		return 2;
	}
	size_t wsz = strtoul(argv[2], NULL, 0), skew = strtoul(argv[3], NULL, 0);
	int powercut = atoi(argv[4]);

	setvbuf(stdout, NULL, _IONBF, 0);
	devFd = open(argv[1], O_RDWR);
	fs = calloc(1, sizeof(ext2_t));
	fs->sectorsz = 512; fs->legacy.read = hr; fs->legacy.write = hw; fs->port = 1;
	if (ext2_sb_init(fs) < 0 || ext2_gdt_init(fs) < 0 || ext2_objs_init(fs) < 0) {
		return 2;
	}
	fs->root = ext2_obj_get(fs, ROOT_INO);
	bs = fs->blocksz;
	addr = bs / 4;
	const uint32_t dind = 12 + addr, tind = 12 + addr + addr * addr;

	/* Sparse files for step 2, made while there is room: one per (K, case).
	 * `far` puts the upper levels in place that the case wants to exist. */
	holecase_t cases[] = {
		{ "single", 20, 0, 2, 0 },                        /* single indirect + data */
		{ "double", dind + 5, 0, 3, 0 },                  /* double + single + data */
		{ "double/single", dind + 3 * addr + 5, dind + 5, 2, 0 },   /* single + data */
		/* Truncating away block 200 leaves the single indirect block, empty,
		 * referenced from the inode on the device: only data is needed, and a
		 * failure must not release that block. */
		{ "empty single", 150, 200, 1, 100 },
		{ "triple", tind + addr * addr + 5, tind + 5, 3, 0 },       /* double + single + data */
	};
	const int ncases = (bs == 1024) ? 5 : 4;   /* the triple region is past 4 GiB at 4 KiB */
	const uint32_t kmax = 4;
	id_t sp[5][5];

	for (uint32_t k = 0; k <= kmax; k++) {
		for (int c = 0; c < ncases; c++) {
			char nm[32];
			snprintf(nm, sizeof(nm), "s%u-%d", k, c);
			sp[k][c] = mk(nm);
			if ((put(sp[k][c], 0, bs) != (ssize_t)bs) ||
					((cases[c].far != 0) && (put(sp[k][c], (off_t)cases[c].far * bs, bs) != (ssize_t)bs)) ||
					((cases[c].cut != 0) && (ext2_truncate(fs, sp[k][c], (size_t)cases[c].cut * bs) < 0)) ||
					(ext2_truncate(fs, sp[k][c], (size_t)(cases[c].hole + 10) * bs) < 0)) {
				printf("setup of %s failed\n", nm);
				return 2;
			}
		}
	}
	id_t big = mk("big");
	if ((put(big, 0, bs) != (ssize_t)bs) || (put(big, (off_t)(dind + 4 * addr) * bs, bs) != (ssize_t)bs)) {
		return 2;
	}
	const uint32_t amax = 6;
	id_t ap[7];
	for (uint32_t k = 0; k <= amax; k++) {
		char nm[32];
		snprintf(nm, sizeof(nm), "a%u", k);
		ap[k] = mk(nm);
		if (put(ap[k], (off_t)(dind - 3) * bs, bs) != (ssize_t)bs) {
			return 2;
		}
	}
	pad = mk("pad");
	if (put(pad, 0, 13 * bs) != (ssize_t)(13 * bs)) {
		return 2;
	}

	/* 1. fill */
	id_t f = mk("f");
	off_t pos = 0;
	if ((skew > 0) && (put(f, 0, skew) != (ssize_t)skew)) {
		return 2;
	}
	pos = skew;
	int shortWrites = 0, enospc = 0;
	for (;;) {
		state_t a = stateOf(f);
		ssize_t r = put(f, pos, wsz);
		state_t b = stateOf(f);

		if (r == (ssize_t)wsz) {
			pos += r;
		}
		else if (r > 0) {
			shortWrites++;
			pos += r;
			printf("  short write: %zd of %zu at %lld, free %u\n", r, wsz, (long long)(pos - r), fs->sb->freeBlocks);
		}
		else {
			if (r != -ENOSPC) {
				FAIL("fill: write at %lld returned %zd, want a count or -ENOSPC", (long long)pos, r);
			}
			unchanged("fill: failed write", a, b);
			enospc++;
			break;
		}

		if (b.size != pos) {
			FAIL("fill: size %lld after %lld bytes accepted", b.size, (long long)pos);
		}
		uint32_t want = expectBlocks((uint32_t)((pos + bs - 1) / bs));
		if (b.blocks != want) {
			FAIL("fill: i_blocks %u, want %u (size %lld)", b.blocks, want, b.size);
		}
		if ((uint32_t)(a.freeBlocks - b.freeBlocks) != (b.blocks - a.blocks) / (bs / 512)) {
			FAIL("fill: free count fell by %u, i_blocks rose by %u blocks", a.freeBlocks - b.freeBlocks, (b.blocks - a.blocks) / (bs / 512));
		}
		if (r != (ssize_t)wsz) {
			/* a short write: the disk is full now, so the next one fails */
			continue;
		}
	}
	printf("fill: %lld bytes in %zu-byte writes, %d short, %d ENOSPC, free left %u\n",
		(long long)pos, wsz, shortWrites, enospc, fs->sb->freeBlocks);
	if (shortWrites + enospc == 0) {
		FAIL("fill: never ran out of space");
	}
	for (off_t p = 0; p < pos; p += 1 << 20) {
		size_t l = ((pos - p) < (1 << 20)) ? (size_t)(pos - p) : (size_t)(1 << 20);
		verify("fill", f, p, l, 1);
	}

	/* Make room for steps 2 and 3. */
	if (ext2_truncate(fs, f, (size_t)(pos - 128 * bs)) < 0) {
		FAIL("truncate f");
	}

	/* 2. one block into a hole, with exactly K blocks free */
	for (uint32_t k = 0; k <= kmax; k++) {
		for (int c = 0; c < ncases; c++) {
			id_t s = sp[k][c];
			off_t at = (off_t)cases[c].hole * bs;
			char nm[48];
			snprintf(nm, sizeof(nm), "hole %s K=%u", cases[c].region, k);

			setFree(k);
			state_t a = stateOf(s);
			ssize_t r = put(s, at, bs);
			state_t b = stateOf(s);

			if (k >= cases[c].need) {
				if (r != (ssize_t)bs) {
					FAIL("%s: r=%zd, want %u (needs %u blocks)", nm, r, bs, cases[c].need);
				}
				else if ((b.blocks - a.blocks) != cases[c].need * (bs / 512) || (a.freeBlocks - b.freeBlocks) != cases[c].need) {
					FAIL("%s: i_blocks +%u free -%u, want +%u blocks", nm, (b.blocks - a.blocks) / (bs / 512), a.freeBlocks - b.freeBlocks, cases[c].need);
				}
				verify(nm, s, at, bs, 1);
			}
			else {
				if (r != -ENOSPC) {
					FAIL("%s: r=%zd, want -ENOSPC (needs %u blocks)", nm, r, cases[c].need);
				}
				unchanged(nm, a, b);
				verify(nm, s, at, bs, 0);
			}
			verify(nm, s, 0, bs, 1);
		}
	}

	/* 3. a long write into a hole inside "big", 40 blocks free */
	{
		off_t at = (off_t)(dind - 100) * bs;
		size_t len = 600 * bs;
		setFree(40);
		state_t a = stateOf(big);
		ssize_t r = put(big, at, len);
		state_t b = stateOf(big);
		printf("big: %zd of %zu bytes into the hole, free %u -> %u\n", r, len, a.freeBlocks, b.freeBlocks);
		if ((r <= 0) || (r >= (ssize_t)len) || ((r % bs) != 0)) {
			FAIL("big: r=%zd, want a short count of whole blocks", r);
		}
		else {
			if (b.size != a.size) {
				FAIL("big: size %lld -> %lld for a write inside the file", a.size, b.size);
			}
			if ((b.blocks - a.blocks) / (bs / 512) != a.freeBlocks - b.freeBlocks) {
				FAIL("big: i_blocks +%u, free -%u", (b.blocks - a.blocks) / (bs / 512), a.freeBlocks - b.freeBlocks);
			}
			verify("big", big, at, (size_t)r, 1);
			verify("big", big, at + r, len - (size_t)r, 0);
		}
	}

	/* 4. append across the start of the double indirect range. Block dind - 3
	 * and its single indirect block exist; blocks dind - 2 and dind - 1 need one
	 * block each, block dind three (double + single + data), then one each. */
	for (uint32_t k = 0; k <= amax; k++) {
		static const uint32_t cost[8] = { 1, 2, 5, 6, 7, 8, 9, 10 };   /* blocks for the first n + 1 */
		uint32_t fit = 0;
		while ((fit < 8) && (cost[fit] <= k)) {
			fit++;
		}

		off_t at = (off_t)(dind - 2) * bs;
		char nm[32];
		snprintf(nm, sizeof(nm), "append K=%u", k);
		setFree(k);
		state_t a = stateOf(ap[k]);
		ssize_t r = put(ap[k], at, 8 * bs);
		state_t b = stateOf(ap[k]);
		if (fit == 0) {
			if (r != -ENOSPC) {
				FAIL("%s: r=%zd, want -ENOSPC", nm, r);
			}
			unchanged(nm, a, b);
		}
		else {
			if (r != (ssize_t)(fit * bs)) {
				FAIL("%s: r=%zd, want %u blocks' worth", nm, r, fit);
			}
			if ((b.size != at + r) || ((b.blocks - a.blocks) / (bs / 512) != cost[fit - 1]) || (a.freeBlocks - b.freeBlocks != cost[fit - 1])) {
				FAIL("%s: size %lld i_blocks +%u free -%u, want size %lld and %u blocks", nm, b.size,
					(b.blocks - a.blocks) / (bs / 512), a.freeBlocks - b.freeBlocks, (long long)(at + r), cost[fit - 1]);
			}
			if (r > 0) {
				verify(nm, ap[k], at, (size_t)r, 1);
			}
		}
	}

	printf("%s: %d failure(s)\n", powercut ? "power cut" : "unmount", fails);
	if (!powercut) {
		ext2_objs_destroy(fs);
		ext2_gdt_destroy(fs);
		ext2_sb_destroy(fs);
	}
	fsync(devFd);
	close(devFd);

	return (fails != 0) ? 1 : 0;
}
