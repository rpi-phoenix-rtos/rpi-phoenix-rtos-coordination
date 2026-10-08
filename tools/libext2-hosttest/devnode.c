/* Device nodes, FIFOs and sockets.
 *
 * These keep their rdev in the inode's block array, where a regular file keeps
 * block numbers. _ext2_file_truncate() guards against walking them -- "Walking
 * them would hand arbitrary values to ext2_block_destroy() and free blocks
 * belonging to other files" -- and nothing exercised that guard. A short
 * symlink is the same shape (target inside the block array) and is covered by
 * linkstress; these are not.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "ext2.h"
#include "ext2io.h"
#include "sb.h"
#include "gdt.h"
#include "obj.h"

#define ROOT_INO 2

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

struct node { const char *name; uint16_t mode; };

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    devFd = open(argv[1], O_RDWR);
    ext2_t *fs = mnt();
    if (!fs) { printf("mount FAILED\n"); return 2; }
    printf("devnode: blocksz=%u\n", fs->blocksz);

    /* A canary file whose blocks must survive everything below. */
    id_t canary;
    size_t csz = 64u * 1024u;
    unsigned char *pat = malloc(csz), *back = malloc(csz);
    for (size_t i = 0; i < csz; i++) pat[i] = (unsigned char)(i * 13 + 7);
    ck("create the canary", ext2_create(fs, ROOT_INO, "canary", 6, NULL, S_IFREG | 0644, &canary) >= 0);
    ck("write the canary", ext2_pwrite(fs, canary, 0, (const char *)pat, csz) == (ssize_t)csz);

    struct node nodes[] = {
        { "chr",  S_IFCHR | 0666 },
        { "blk",  S_IFBLK | 0666 },
        { "fifo", S_IFIFO | 0666 },
        { "sock", S_IFSOCK | 0666 },
    };

    for (size_t n = 0; n < sizeof(nodes) / sizeof(nodes[0]); n++) {
        oid_t dev;
        char label[64];
        dev.port = 7;
        dev.id = 0x12345678u + (unsigned)n;   /* a recognisable rdev */
        id_t id;

        snprintf(label, sizeof(label), "%s: create", nodes[n].name);
        ck(label, ext2_create(fs, ROOT_INO, nodes[n].name, strlen(nodes[n].name), &dev,
                              nodes[n].mode, &id) >= 0);

        /* Truncate must be REFUSED, not carried out: ext2_truncate() rejects
         * anything that is not a regular file (-EISDIR for a directory,
         * -EINVAL otherwise). That refusal is what keeps the rdev in the block
         * array from ever being walked as block numbers. Asserting the refusal
         * is the point -- my first version of this test asserted success and
         * "failed" against correct code. */
        snprintf(label, sizeof(label), "%s: truncate to 0 is REFUSED", nodes[n].name);
        ck(label, ext2_truncate(fs, id, 0) == -EINVAL);

        snprintf(label, sizeof(label), "%s: truncate to 4096 is REFUSED", nodes[n].name);
        ck(label, ext2_truncate(fs, id, 4096) == -EINVAL);

        snprintf(label, sizeof(label), "%s: unlink", nodes[n].name);
        ck(label, ext2_unlink(fs, ROOT_INO, nodes[n].name, strlen(nodes[n].name)) >= 0);
    }

    /* If any of that walked an rdev as block numbers, the canary loses blocks. */
    memset(back, 0, csz);
    ck("canary still readable", ext2_read(fs, canary, 0, (char *)back, csz) == (ssize_t)csz);
    ck("canary is byte-identical", memcmp(pat, back, csz) == 0);

    um(fs); fsync(devFd); close(devFd);
    free(pat); free(back);
    printf("%s\n", fails ? "DEVNODE: checks failed" : "DEVNODE: all checks passed");
    return fails ? 1 : 0;
}
