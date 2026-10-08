/* rmdir of a NON-EMPTY directory larger than one block (KNOWN-ISSUES P28).
 *
 * _ext2_dir_empty() answered -EBUSY for any directory larger than a block and
 * ext2_unlink() tested it as `!_ext2_dir_empty()`, so the error read as
 * "empty": rmdir removed a non-empty multi-block directory and orphaned its
 * contents (e2fsck: unconnected directory, unattached inodes). Fixed in
 * filesystems 0a9a12e, which walks every block.
 *
 *   ./rmdirfull <img>
 *
 * Each case builds a directory spanning >= 3 blocks, then:
 *   1  full: rmdir must fail with ENOTEMPTY and every entry must survive
 *   2  one survivor in the LAST block
 *   3  one survivor in a MIDDLE block (the emptied blocks after it are
 *      truncated, the ones before it stay -- the walk must cross them)
 *   4  one survivor in a middle block that is a SUBDIRECTORY
 * and in every case: rmdir fails, then removing the last entry makes the
 * directory empty and rmdir succeeds. e2fsck -fn (run-all.sh) judges the image.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "ext2.h"
#include "sb.h"
#include "gdt.h"
#include "obj.h"

#define ROOT_INO 2
#define MAXN     2048

static int devFd = -1;
static ssize_t hr(id_t i, off_t o, char *b, size_t l) { (void)i; return pread(devFd, b, l, o); }
static ssize_t hw(id_t i, off_t o, const char *b, size_t l) { (void)i; return pwrite(devFd, b, l, o); }
static ext2_t *mnt(void)
{
	ext2_t *fs = calloc(1, sizeof(ext2_t));
	fs->sectorsz = 512; fs->strg = NULL;
	fs->legacy.devId = 0; fs->legacy.read = hr; fs->legacy.write = hw; fs->port = 1;
	if (ext2_sb_init(fs) < 0 || ext2_gdt_init(fs) < 0 || ext2_objs_init(fs) < 0) return NULL;
	fs->root = ext2_obj_get(fs, ROOT_INO); return fs->root ? fs : NULL;
}
static void um(ext2_t *fs) { ext2_objs_destroy(fs); ext2_gdt_destroy(fs); ext2_sb_destroy(fs); free(fs); }
static int fails;
static void ck(const char *w, int ok) { printf("  [%s] %s\n", ok ? " ok " : "FAIL", w); if (!ok) fails++; }

static int nmOf(int i, char *nm, size_t sz) { return snprintf(nm, sz, "entry-%05d-padding-to-a-longer-name", i); }

static long long sizeOf(ext2_t *fs, id_t id)
{
	long long sz = -1;
	ext2_getattr(fs, id, atSize, &sz);
	return sz;
}


static int exists(ext2_t *fs, id_t dir, const char *nm)
{
	oid_t res, dev;
	return ext2_lookup(fs, dir, nm, strlen(nm), &res, &dev) >= 0;
}


/* Fill <dir> until it spans >= 3 blocks, with entry <subdirAt> a subdirectory
 * (-1: none). Returns the entry count. */
static int fill(ext2_t *fs, id_t dir, int subdirAt)
{
	int n;
	for (n = 0; n < MAXN && sizeOf(fs, dir) < 3LL * fs->blocksz; n++) {
		char nm[64];
		int l = nmOf(n, nm, sizeof(nm));
		id_t f;
		uint16_t mode = (n == subdirAt) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
		if (ext2_create(fs, dir, nm, (size_t)l, NULL, mode, &f) < 0) {
			printf("  create %s failed\n", nm);
			fails++;
			break;
		}
	}
	return n;
}


/* The block each entry's record lies in, from one readdir scan. */
static void blocksOf(ext2_t *fs, id_t dir, int *blk, int n)
{
	static struct { struct dirent d; char name[256]; } ent;
	off_t pos = 0, next;
	int k;
	for (k = 0; k < n; k++) blk[k] = -1;
	while (next = -1, ext2_readdir(fs, dir, pos, &ent.d, sizeof(ent), &next) >= 0 && next > pos) {
		if (sscanf(ent.d.d_name, "entry-%d", &k) == 1 && k >= 0 && k < n)
			blk[k] = (int)((next - 1) / fs->blocksz);
		pos = next;
	}
}


static int rmdirOf(ext2_t *fs, const char *dn) { return ext2_unlink(fs, ROOT_INO, dn, strlen(dn)); }


/* rmdir of a non-empty directory: fails with ENOTEMPTY, and changes nothing */
static void mustRefuse(ext2_t *fs, const char *dn, id_t dir, int n, int survivor)
{
	char what[160];
	int r = rmdirOf(fs, dn);
	snprintf(what, sizeof(what), "rmdir %s refused with ENOTEMPTY (got %d)", dn, r);
	ck(what, r == -ENOTEMPTY);
	ck("  ...the directory is still there", exists(fs, ROOT_INO, dn));
	int lost = 0;
	for (int i = 0; i < n; i++) {
		if (survivor >= 0 && i != survivor) continue;
		char nm[64];
		nmOf(i, nm, sizeof(nm));
		if (!exists(fs, dir, nm)) lost++;
	}
	snprintf(what, sizeof(what), "  ...its entries are all still there (%d lost)", lost);
	ck(what, lost == 0);
}


static void unlinkAllBut(ext2_t *fs, id_t dir, int n, int keep)
{
	int bad = 0;
	for (int i = 0; i < n; i++) {
		if (i == keep) continue;
		char nm[64];
		int l = nmOf(i, nm, sizeof(nm));
		if (ext2_unlink(fs, dir, nm, (size_t)l) < 0) bad++;
	}
	if (bad) { printf("  %d unlink(s) failed\n", bad); fails++; }
}


static void mustRemove(ext2_t *fs, const char *dn, id_t dir, int survivor)
{
	char nm[64];
	int l = nmOf(survivor, nm, sizeof(nm));
	ck("remove the last entry", ext2_unlink(fs, dir, nm, (size_t)l) >= 0);
	ck("rmdir of the now-empty directory succeeds", rmdirOf(fs, dn) >= 0);
	ck("  ...and it is gone", !exists(fs, ROOT_INO, dn));
}


/* which: 0 = full, 1 = last block, 2 = middle block, 3 = middle block, a subdirectory */
static void runCase(ext2_t *fs, int which)
{
	static const char *title[] = {
		"full directory", "one survivor in the LAST block",
		"one survivor in a MIDDLE block", "one survivor in a MIDDLE block, a subdirectory",
	};
	char dn[16];
	snprintf(dn, sizeof(dn), "full%d", which + 1);
	printf("case %d: %s\n", which + 1, title[which]);

	id_t dir;
	ck("create the directory", ext2_create(fs, ROOT_INO, dn, strlen(dn), NULL, S_IFDIR | 0755, &dir) >= 0);

	/* the subdirectory sits in block 1: ~1.5 blocks' worth of entries in */
	int perBlock = (int)(fs->blocksz / 48);   /* a 35-byte name takes a 48-byte record */
	int sub = (which == 3) ? perBlock + perBlock / 2 : -1;
	int n = fill(fs, dir, sub);
	long long sz = sizeOf(fs, dir);
	printf("  %d entries, %lld blocks\n", n, sz / fs->blocksz);
	ck("(precondition) the directory spans >= 3 blocks", sz >= 3LL * fs->blocksz);

	static int blk[MAXN];
	blocksOf(fs, dir, blk, n);
	int lastBlk = (int)(sz / fs->blocksz) - 1;
	int survivor = -1;
	if (which == 1) {
		for (int i = n - 1; i >= 0; i--) if (blk[i] == lastBlk) { survivor = i; break; }
	}
	else if (which == 2) {
		for (int i = 0; i < n; i++) if (blk[i] > 0 && blk[i] < lastBlk) { survivor = i + 3; break; }
	}
	else if (which == 3) {
		survivor = sub;
	}
	if (which != 0) {
		char what[128];
		snprintf(what, sizeof(what), "(precondition) survivor %d is in block %d of %d",
			survivor, (survivor >= 0) ? blk[survivor] : -1, lastBlk + 1);
		ck(what, survivor >= 0 && ((which == 1) ? blk[survivor] == lastBlk : (blk[survivor] > 0 && blk[survivor] < lastBlk)));
		if (survivor < 0) return;
	}

	if (which == 0) {
		mustRefuse(fs, dn, dir, n, -1);
		unlinkAllBut(fs, dir, n, -1);
		ck("rmdir of the emptied directory succeeds", rmdirOf(fs, dn) >= 0);
		ck("  ...and it is gone", !exists(fs, ROOT_INO, dn));
		return;
	}

	unlinkAllBut(fs, dir, n, survivor);
	sz = sizeOf(fs, dir);
	printf("  one entry left, directory now %lld blocks\n", sz / fs->blocksz);
	ck("(precondition) still larger than one block", sz > (long long)fs->blocksz);
	mustRefuse(fs, dn, dir, n, survivor);
	mustRemove(fs, dn, dir, survivor);
}


int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 2) { printf("usage: rmdirfull <img>\n"); return 2; }

	devFd = open(argv[1], O_RDWR);
	if (devFd < 0) { perror("open image"); return 2; }
	ext2_t *fs = mnt();
	if (!fs) { printf("mount FAILED\n"); return 2; }
	printf("rmdirfull: blocksz=%u\n", fs->blocksz);

	for (int c = 0; c < 4; c++) runCase(fs, c);

	um(fs); fsync(devFd); close(devFd);
	printf("%s\n", fails ? "RMDIRFULL: checks failed" : "RMDIRFULL: all checks passed");
	return fails ? 1 : 0;
}
