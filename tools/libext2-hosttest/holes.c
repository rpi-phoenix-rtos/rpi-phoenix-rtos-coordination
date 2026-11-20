/* Reading a hole must not allocate.
 *
 *   ./holes <img> <end>      end: 0 = unmount at the end, 1 = just stop
 *
 * A sparse file with one block at 0 and one past 8 MiB: an 8 MiB hole that
 * crosses the single and double indirect ranges. Reading it -- block by block,
 * unaligned, and in one call -- must return zeros and leave the free count,
 * the file's i_blocks and its size unchanged. So must truncating the file to
 * a point inside the hole, apart from releasing the far block. e2fsck -fn
 * then judges the image (run-all.sh).
 */
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
#define HOLE     (8u << 20)

static int devFd = -1;
static ssize_t hr(id_t i, off_t o, char *b, size_t l) { (void)i; return pread(devFd, b, l, o); }
static ssize_t hw(id_t i, off_t o, const char *b, size_t l) { (void)i; return pwrite(devFd, b, l, o); }

static ext2_t *fs;
static int fails;


static void check(const char *what, id_t f, uint32_t free0, uint32_t blocks0)
{
	ext2_obj_t *o = ext2_obj_get(fs, f);
	uint32_t blocks = o->inode->blocks;
	ext2_obj_put(fs, o);

	if ((fs->sb->freeBlocks != free0) || (blocks != blocks0)) {
		printf("  [FAIL] %s: free blocks %u -> %u, i_blocks %u -> %u\n", what, free0, fs->sb->freeBlocks, blocks0, blocks);
		fails++;
	}
	else {
		printf("  [ ok ] %s: free blocks %u, i_blocks %u\n", what, free0, blocks0);
	}
}


static int zeros(const char *b, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (b[i] != 0) {
			return 0;
		}
	}
	return 1;
}


int main(int argc, char **argv)
{
	if (argc < 3) {
		printf("usage: %s <img> <end>\n", argv[0]);
		return 2;
	}
	int powercut = atoi(argv[2]);

	setvbuf(stdout, NULL, _IONBF, 0);
	devFd = open(argv[1], O_RDWR);
	fs = calloc(1, sizeof(ext2_t));
	fs->sectorsz = 512; fs->legacy.read = hr; fs->legacy.write = hw; fs->port = 1;
	if (ext2_sb_init(fs) < 0 || ext2_gdt_init(fs) < 0 || ext2_objs_init(fs) < 0) {
		return 2;
	}
	fs->root = ext2_obj_get(fs, ROOT_INO);
	const uint32_t bs = fs->blocksz;

	id_t f;
	char *b = malloc(HOLE + 2 * bs), *w = malloc(bs);
	memset(w, 0x5a, bs);
	if ((ext2_create(fs, ROOT_INO, "h", 1, NULL, S_IFREG | 0644, &f) < 0) ||
			(ext2_pwrite(fs, f, 0, w, bs) != (ssize_t)bs) ||
			(ext2_pwrite(fs, f, bs + HOLE, w, bs) != (ssize_t)bs)) {
		printf("setup failed\n");
		return 2;
	}

	ext2_obj_t *o = ext2_obj_get(fs, f);
	const uint32_t blocks0 = o->inode->blocks;
	ext2_obj_put(fs, o);
	const uint32_t free0 = fs->sb->freeBlocks;

	/* block by block */
	int bad = 0;
	for (off_t p = bs; p < (off_t)(bs + HOLE); p += bs) {
		if ((ext2_read(fs, f, p, b, bs) != (ssize_t)bs) || !zeros(b, bs)) {
			bad++;
		}
	}
	if (bad != 0) {
		printf("  [FAIL] %d hole blocks did not read as zeros\n", bad);
		fails++;
	}
	check("read the hole block by block", f, free0, blocks0);

	/* unaligned, crossing block boundaries */
	bad = 0;
	for (off_t p = bs + 100; p + 3000 < (off_t)(bs + HOLE); p += 777 * 13) {
		if ((ext2_read(fs, f, p, b, 3000) != 3000) || !zeros(b, 3000)) {
			bad++;
		}
	}
	if (bad != 0) {
		printf("  [FAIL] %d unaligned reads of the hole were not zeros\n", bad);
		fails++;
	}
	check("read the hole unaligned", f, free0, blocks0);

	/* in one call, with the data on both sides */
	if ((ext2_read(fs, f, 0, b, HOLE + 2 * bs) != (ssize_t)(HOLE + 2 * bs)) ||
			(memcmp(b, w, bs) != 0) || !zeros(b + bs, HOLE) || (memcmp(b + bs + HOLE, w, bs) != 0)) {
		printf("  [FAIL] one read of the whole file is wrong\n");
		fails++;
	}
	check("read the whole file in one call", f, free0, blocks0);

	/* Truncating into the hole releases the far block (and the indirect
	 * blocks that map only it), and allocates nothing. */
	if (ext2_truncate(fs, f, HOLE / 2 + 100) < 0) {
		printf("  [FAIL] truncate\n");
		fails++;
	}
	o = ext2_obj_get(fs, f);
	uint32_t blocks1 = o->inode->blocks;
	ext2_obj_put(fs, o);
	if ((fs->sb->freeBlocks < free0) || ((fs->sb->freeBlocks - free0) * (bs / 512) != (blocks0 - blocks1))) {
		printf("  [FAIL] truncate into the hole: free %u -> %u, i_blocks %u -> %u\n", free0, fs->sb->freeBlocks, blocks0, blocks1);
		fails++;
	}
	else {
		printf("  [ ok ] truncate into the hole released %u block(s), allocated none\n", fs->sb->freeBlocks - free0);
	}
	if ((ext2_read(fs, f, 0, b, HOLE / 2 + 100) != (ssize_t)(HOLE / 2 + 100)) || (memcmp(b, w, bs) != 0) || !zeros(b + bs, HOLE / 2 + 100 - bs)) {
		printf("  [FAIL] read after truncate\n");
		fails++;
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
