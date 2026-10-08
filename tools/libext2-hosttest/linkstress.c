/* Hard links and symlinks against the real libext2, graded by e2fsck.
 *
 * Two untested areas, picked because each has a specific reason to be fragile:
 *  * HARD LINKS -- defect 19 was that unlink() adjusted link counts without
 *    marking the object dirty. A file normally hides that (count reaches 0 and
 *    the inode is destroyed), but a file with TWO names survives with a count
 *    that has to be right on disk. This is the case that proves the obj half
 *    of that fix, not just the parent half.
 *  * SYMLINKS -- ext2 stores a target of up to MAX_SYMLINK_LEN_IN_INODE (60)
 *    bytes inside the inode's block array and anything longer in a real block.
 *    Two completely different code paths behind one operation.
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
    fs->root = ext2_obj_get(fs, ROOT_INO);
    return fs->root ? fs : NULL;
}
static void um(ext2_t *fs) { ext2_objs_destroy(fs); ext2_gdt_destroy(fs); ext2_sb_destroy(fs); free(fs); }

static int fails;
static void ck(const char *w, int ok) { printf("  [%s] %s\n", ok ? " ok " : "FAIL", w); if (!ok) fails++; }

static long long links_of(ext2_t *fs, id_t id)
{
    long long v = -1;
    if (ext2_getattr(fs, id, atLinks, &v) < 0) return -1;
    return v;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    devFd = open(argv[1], O_RDWR);
    ext2_t *fs = mnt();
    if (fs == NULL) { printf("mount FAILED\n"); return 2; }
    printf("linkstress: blocksz=%u\n", fs->blocksz);

    /* ---- hard links ---- */
    id_t f;
    ck("create a file", ext2_create(fs, ROOT_INO, "h1", 2, NULL, S_IFREG | 0644, &f) >= 0);
    ck("write to it", ext2_pwrite(fs, f, 0, "payload", 7) == 7);
    ck("links == 1", links_of(fs, f) == 1);

    ck("add a second name", ext2_link(fs, ROOT_INO, "h2", 2, f) >= 0);
    ck("links == 2", links_of(fs, f) == 2);
    ck("add a third name", ext2_link(fs, ROOT_INO, "h3", 2, f) >= 0);
    ck("links == 3", links_of(fs, f) == 3);

    /* Both names must read the same content. */
    oid_t r1, r2, dv;
    ck("lookup h1", ext2_lookup(fs, ROOT_INO, "h1", 2, &r1, &dv) >= 0);
    ck("lookup h3", ext2_lookup(fs, ROOT_INO, "h3", 2, &r2, &dv) >= 0);
    ck("both names are the same inode", r1.id == r2.id);

    ck("remove one name", ext2_unlink(fs, ROOT_INO, "h2", 2) >= 0);
    ck("links == 2 after unlink", links_of(fs, f) == 2);
    char buf[16] = { 0 };
    ck("surviving name still readable", ext2_read(fs, f, 0, buf, 7) == 7 && memcmp(buf, "payload", 7) == 0);

    /* ---- symlinks: short (in-inode) and long (needs a block) ---- */
    const char *shortTgt = "/a/short/target";                 /* < 60 */
    char longTgt[200];
    memset(longTgt, 'L', sizeof(longTgt) - 1);
    longTgt[0] = '/'; longTgt[sizeof(longTgt) - 1] = '\0';     /* > 60 */

    id_t sl;
    ck("create short symlink", ext2_create(fs, ROOT_INO, "sl-short", 8, NULL, S_IFLNK | 0777, &sl) >= 0);
    ck("write short target", ext2_pwrite(fs, sl, 0, shortTgt, strlen(shortTgt)) == (ssize_t)strlen(shortTgt));
    char got[256];
    memset(got, 0, sizeof(got));
    ck("read short target back", ext2_read(fs, sl, 0, got, strlen(shortTgt)) == (ssize_t)strlen(shortTgt));
    ck("short target matches", memcmp(got, shortTgt, strlen(shortTgt)) == 0);

    id_t ll;
    ck("create long symlink", ext2_create(fs, ROOT_INO, "sl-long", 7, NULL, S_IFLNK | 0777, &ll) >= 0);
    ssize_t w = ext2_pwrite(fs, ll, 0, longTgt, strlen(longTgt));
    ck("write long target", w == (ssize_t)strlen(longTgt));
    memset(got, 0, sizeof(got));
    ck("read long target back", ext2_read(fs, ll, 0, got, strlen(longTgt)) == (ssize_t)strlen(longTgt));
    ck("long target matches", memcmp(got, longTgt, strlen(longTgt)) == 0);

    /* Removing symlinks must not leak the target block. */
    ck("unlink short symlink", ext2_unlink(fs, ROOT_INO, "sl-short", 8) >= 0);
    ck("unlink long symlink", ext2_unlink(fs, ROOT_INO, "sl-long", 7) >= 0);

    /* DELIBERATELY leave h1 and h3 in place. The in-memory count is checked
     * above by getattr; what matters is the count e2fsck reads off the DISK
     * after unmount, and a file whose links reach 0 is destroyed, so only a
     * surviving multi-link file can expose a count that was never synced.
     * That is exactly the obj half of defect 19. */
    ck("h1 and h3 survive with links == 2", links_of(fs, f) == 2);

    um(fs); fsync(devFd); close(devFd);
    printf("%s\n", fails ? "LINKSTRESS: checks failed" : "LINKSTRESS: all checks passed");
    return fails ? 1 : 0;
}
