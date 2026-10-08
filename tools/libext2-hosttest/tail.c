/* Does truncating to a NON-block-aligned size leave stale bytes in the tail of
 * the partial block, which a later extend then exposes?
 *
 * POSIX: after ftruncate() down and back up, the gap reads as zeros. If the
 * old bytes come back, previously-deleted content becomes readable -- a data
 * leak, not just a correctness bug.
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

static void trial(const char *img, size_t initial, size_t truncTo, size_t reExtendAt)
{
    devFd = open(img, O_RDWR);
    ext2_t *fs = mnt();
    if (!fs) { printf("MOUNT FAILED\n"); return; }
    id_t id;
    char nm[32]; int n = snprintf(nm, sizeof(nm), "t%zu_%zu", initial, truncTo);
    if (ext2_create(fs, ROOT_INO, nm, (size_t)n, NULL, S_IFREG | 0644, &id) < 0) { printf("create failed\n"); return; }

    unsigned char *fill = malloc(initial);
    memset(fill, 0xAA, initial);
    ext2_pwrite(fs, id, 0, (const char *)fill, initial);

    ext2_truncate(fs, id, truncTo);
    /* extend again by writing past the truncation point */
    const char *mark = "END";
    ext2_pwrite(fs, id, (off_t)reExtendAt, mark, 3);

    size_t gap = reExtendAt - truncTo;
    unsigned char *got = malloc(gap ? gap : 1);
    memset(got, 0x55, gap);
    ssize_t r = ext2_read(fs, id, (off_t)truncTo, (char *)got, gap);
    size_t firstBad = gap; unsigned char badVal = 0;
    for (size_t i = 0; i < gap; i++) if (got[i] != 0) { firstBad = i; badVal = got[i]; break; }

    printf("  wrote %zu of 0xAA, trunc->%zu (block offs %zu), re-extend at %zu: read %zd of gap %zu -> %s",
           initial, truncTo, truncTo % fs->blocksz, reExtendAt, r, gap,
           (firstBad == gap) ? "all zeros (correct)\n" : "");
    if (firstBad != gap) {
        printf("STALE at +%zu (0x%02x) == leaked old content\n", firstBad, badVal);
        fails++;
    }
    free(fill); free(got);
    um(fs); fsync(devFd); close(devFd);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *img = argv[1];
    printf("truncate-tail leak test (blocksz from image):\n");
    trial(img, 8192, 4987, 6000);   /* non-aligned truncation, the stress case */
    trial(img, 8192, 5000, 7000);   /* another non-aligned */
    trial(img, 8192, 4096, 6000);   /* ALIGNED truncation -- control */
    trial(img, 8192, 1, 4000);      /* truncate to 1 byte */
    printf("%s\n", fails ? "RESULT: stale tail bytes ARE exposed" : "RESULT: no stale bytes exposed");
    return fails ? 1 : 0;
}
