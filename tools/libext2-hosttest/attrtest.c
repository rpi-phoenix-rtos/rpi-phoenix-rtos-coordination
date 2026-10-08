/* setattr paths, and specifically atSize on things that are not regular files.
 *
 * ext2_truncate() rejects a non-regular file (-EISDIR / -EINVAL), but
 * ext2_setattr(atSize) calls _ext2_file_truncate() DIRECTLY and so never sees
 * that check. The inner function guards device nodes and short symlinks (their
 * block array holds an rdev or a target, not block numbers) -- but nothing
 * there mentions directories, whose block array DOES hold real block numbers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
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

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    devFd = open(argv[1], O_RDWR);
    ext2_t *fs = mnt();
    if (!fs) { printf("mount FAILED\n"); return 2; }
    printf("attrtest: blocksz=%u\n", fs->blocksz);

    /* --- ordinary setattr on a regular file --- */
    id_t f;
    ck("create a file", ext2_create(fs, ROOT_INO, "af", 2, NULL, S_IFREG | 0644, &f) >= 0);
    ck("write 8 KiB", ext2_pwrite(fs, f, 0, (const char *)"x", 1) == 1);
    ck("setattr mode", ext2_setattr(fs, f, atMode, S_IFREG | 0600, NULL, 0) >= 0);
    long long v = 0;
    ck("getattr mode reads it back", ext2_getattr(fs, f, atMode, &v) >= 0 && (v & 0777) == 0600);
    ck("setattr uid", ext2_setattr(fs, f, atUid, 1234, NULL, 0) >= 0);
    ck("getattr uid", ext2_getattr(fs, f, atUid, &v) >= 0 && v == 1234);
    ck("setattr size (grow)", ext2_setattr(fs, f, atSize, 4096, NULL, 0) >= 0);
    ck("getattr size", ext2_getattr(fs, f, atSize, &v) >= 0 && v == 4096);

    /* --- a directory with real entries in it --- */
    id_t d;
    ck("create a directory", ext2_create(fs, ROOT_INO, "ad", 2, NULL, S_IFDIR | 0755, &d) >= 0);
    for (int i = 0; i < 40; i++) {
        char nm[32]; int l = snprintf(nm, sizeof(nm), "child-%02d", i);
        id_t c;
        if (ext2_create(fs, d, nm, (size_t)l, NULL, S_IFREG | 0644, &c) < 0) { ck("populate dir", 0); break; }
    }
    ck("populated the directory", 1);

    ck("ext2_truncate on a directory is refused (-EISDIR)",
       ext2_truncate(fs, d, 0) == -EISDIR);

    /* THE QUESTION: does setattr(atSize) bypass that and eat the directory? */
    int r = ext2_setattr(fs, d, atSize, 0, NULL, 0);
    printf("  ext2_setattr(atSize=0) on a DIRECTORY returned %d\n", r);

    /* Whatever it returned, the directory must still work. */
    int found = 0;
    for (int i = 0; i < 40; i++) {
        char nm[32]; int l = snprintf(nm, sizeof(nm), "child-%02d", i);
        oid_t res, dev;
        if (ext2_lookup(fs, d, nm, (size_t)l, &res, &dev) >= 0) found++;
    }
    printf("  entries still found after that: %d of 40\n", found);
    ck("the directory survived setattr(atSize)", found == 40);

    /* --- statfs must TRACK allocation, not just return plausible numbers --- */
    {
        struct statvfs a, b;
        ck("statfs works", ext2_statfs(fs, &a, sizeof(a)) >= 0);
        ck("statfs rejects a wrong size", ext2_statfs(fs, &b, sizeof(b) - 1) == -EINVAL);
        ck("f_bsize is the block size", a.f_bsize == fs->blocksz);
        ck("f_bfree <= f_blocks", a.f_bfree <= a.f_blocks);
        ck("f_bavail <= f_bfree", a.f_bavail <= a.f_bfree);
        ck("f_ffree <= f_files", a.f_ffree <= a.f_files);

        /* Allocate a known amount and see the free count move by about that. */
        id_t big;
        size_t want = 256u * 1024u;
        char *z = calloc(want, 1);
        ck("create a file to consume space", ext2_create(fs, ROOT_INO, "sf", 2, NULL, S_IFREG | 0644, &big) >= 0);
        ck("write 256 KiB", ext2_pwrite(fs, big, 0, z, want) == (ssize_t)want);
        free(z);
        ck("statfs again", ext2_statfs(fs, &b, sizeof(b)) >= 0);

        long long used = (long long)a.f_bfree - (long long)b.f_bfree;
        long long expect = (long long)(want / fs->blocksz);
        printf("  256 KiB written: f_bfree dropped by %lld blocks, data alone needs %lld\n",
               used, expect);
        /* >= expect because indirect blocks count too; a small margin above. */
        ck("free blocks dropped by at least the data size", used >= expect);
        ck("...and not absurdly more (indirect blocks only)", used <= expect + 64);

        /* f_fsid must be an IDENTIFIER, not our own heap address.
         *
         * It used to be `(unsigned long)fs`, which leaked a pointer into every
         * statvfs() reply and changed on every mount of the same filesystem --
         * so it identified the mount attempt, not the filesystem. The remount
         * below is the check that actually distinguishes the two: a pointer
         * would (almost certainly) come back different. */
        ck("f_fsid is not the heap pointer", a.f_fsid != (unsigned long)fs);
        ck("f_fsid is stable within a mount", a.f_fsid == b.f_fsid);

        unsigned long first = a.f_fsid;
        um(fs);
        fs = mnt();
        ck("remounted", fs != NULL);
        ck("statfs after remount", ext2_statfs(fs, &a, sizeof(a)) >= 0);
        ck("f_fsid survives a remount", a.f_fsid == first);
        ck("...and is still not the (new) heap pointer", a.f_fsid != (unsigned long)fs);
    }

    um(fs); fsync(devFd); close(devFd);
    printf("%s\n", fails ? "ATTRTEST: checks failed" : "ATTRTEST: all checks passed");
    return fails ? 1 : 0;
}
