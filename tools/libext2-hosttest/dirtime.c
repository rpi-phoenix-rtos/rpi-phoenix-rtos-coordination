/* Directory timestamps (issue C10) against the real libext2.
 *
 * POSIX: creating, removing or renaming an entry marks the DIRECTORY's st_mtime
 * and st_ctime for update -- both parents on a rename. link() and unlink() of a
 * surviving name mark the FILE's st_ctime only; its st_mtime is its content's
 * and must not move. Anything that watches a directory by its mtime (make,
 * rsync, the GIO poll monitor) depends on the first; `mv` keeping a file's
 * mtime depends on the second (libphoenix rename() is link + unlink).
 *
 * The clock is mocked: this file defines time(), which the ext2 objects linked
 * into the same executable resolve to instead of libc's. Every operation runs
 * at a fresh, known second, so "did it stamp" is an equality test, not a sleep.
 *
 * Each stamp is checked twice: in memory (ext2_getattr, what stat() returns)
 * and ON DISK (ext2_inode_init re-reads the inode table, bypassing the object
 * cache), so a stamp that is set but never written back is a failure too.
 * e2fsck does not look at timestamps; run-all.sh runs it only to show these
 * operations leave the filesystem consistent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ext2.h"
#include "sb.h"
#include "gdt.h"
#include "inode.h"
#include "obj.h"

#define ROOT_INO 2

/* ---- the mock clock ---- */
static time_t fakeNow;

time_t time(time_t *t)
{
    if (t != NULL) *t = fakeNow;
    return fakeNow;
}

static time_t tick(void) { fakeNow += 10; return fakeNow; }

/* ---- mount ---- */
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

/* ---- timestamps, in memory and on disk ---- */
typedef struct { long long m, c; } stamps_t;

static stamps_t mem(ext2_t *fs, id_t id)
{
    stamps_t s = { -1, -1 };
    if (ext2_getattr(fs, id, atMTime, &s.m) < 0) s.m = -1;
    if (ext2_getattr(fs, id, atCTime, &s.c) < 0) s.c = -1;
    return s;
}

static stamps_t disk(ext2_t *fs, id_t id)
{
    stamps_t s = { -1, -1 };
    ext2_inode_t *in = ext2_inode_init(fs, (uint32_t)id);
    if (in != NULL) { s.m = in->mtime; s.c = in->ctime; free(in); }
    return s;
}

/* The directory's mtime AND ctime are exactly t, in memory and on disk. */
static int stamped(ext2_t *fs, id_t dir, time_t t)
{
    stamps_t a = mem(fs, dir), b = disk(fs, dir);
    return a.m == t && a.c == t && b.m == t && b.c == t;
}

static void ckDir(const char *what, ext2_t *fs, id_t dir, time_t t)
{
    stamps_t a = mem(fs, dir), b = disk(fs, dir);
    int ok = a.m == t && a.c == t && b.m == t && b.c == t;
    char line[256];
    snprintf(line, sizeof(line), "%s (want %lld; mem m=%+lld c=%+lld, disk m=%+lld c=%+lld)",
             what, (long long)t, a.m - t, a.c - t, b.m - t, b.c - t);
    ck(line, ok);
}

/* A control: this inode's stamps did not move. */
static void ckSame(const char *what, ext2_t *fs, id_t id, stamps_t before)
{
    stamps_t a = mem(fs, id);
    ck(what, a.m == before.m && a.c == before.c);
}

/* The entry itself on link/unlink: ctime is now, mtime is what it was. */
static void ckEntry(const char *what, ext2_t *fs, id_t id, long long mtime, time_t t)
{
    stamps_t a = mem(fs, id), b = disk(fs, id);
    char line[256];
    snprintf(line, sizeof(line), "%s (mtime kept: mem %+lld disk %+lld; ctime now: mem %+lld disk %+lld)",
             what, a.m - mtime, b.m - mtime, a.c - t, b.c - t);
    ck(line, a.m == mtime && b.m == mtime && a.c == t && b.c == t);
}

/* ---- every _ext2_dir_remove() branch: drain a multi-block directory ---- */
#define NENT 100

static void entName(char *nm, int i)
{
    /* 120 bytes: a 128-byte record, so 7 per 1 KiB block and 31 per 4 KiB --
     * enough blocks that removal hits the start-of-block, mid-block,
     * whole-last-block and whole-middle-block paths. */
    memset(nm, 'n', 120);
    snprintf(nm, 121, "e%04d", i);
    nm[5] = 'n';
    nm[120] = '\0';
}

static void drain(ext2_t *fs, const char *dname, int reverse)
{
    id_t d, f;
    char nm[128];
    int bad = 0, n = 0;

    if (ext2_create(fs, ROOT_INO, dname, strlen(dname), NULL, S_IFDIR | 0755, &d) < 0) {
        ck("create the drain directory", 0);
        return;
    }
    for (int i = 0; i < NENT; i++) {
        entName(nm, i);
        time_t t = tick();
        if (ext2_create(fs, d, nm, 120, NULL, S_IFREG | 0644, &f) < 0) { ck("populate", 0); return; }
        if (!stamped(fs, d, t)) bad++;
    }
    long long sz = -1;
    ext2_getattr(fs, d, atSize, &sz);
    printf("  %s: %d creates, directory is %lld bytes (%lld blocks); %d unstamped\n",
           dname, NENT, sz, sz / fs->blocksz, bad);
    ck("every create stamped the parent", bad == 0);

    bad = 0;
    /* Forward order exercises start-of-block and whole-middle-block removal;
     * reverse order exercises mid-block and whole-LAST-block (truncate). */
    for (int k = 0; k < NENT; k++) {
        int i = reverse ? NENT - 1 - k : k;
        entName(nm, i);
        time_t t = tick();
        if (ext2_unlink(fs, d, nm, 120) < 0) { ck("unlink", 0); return; }
        n++;
        if (!stamped(fs, d, t)) {
            stamps_t a = mem(fs, d), b = disk(fs, d);
            if (bad < 4)
                printf("    unstamped after unlink #%d (dir %lld bytes): mem m=%+lld c=%+lld, disk m=%+lld c=%+lld\n",
                       i, (long long)(ext2_getattr(fs, d, atSize, &sz), sz),
                       a.m - t, a.c - t, b.m - t, b.c - t);
            bad++;
        }
    }
    printf("  %s: %d unlinks (%s order), %d unstamped\n", dname, n, reverse ? "reverse" : "forward", bad);
    ck(reverse ? "every unlink stamped the parent (reverse)" : "every unlink stamped the parent (forward)", bad == 0);

    time_t t = tick();
    ck("rmdir the drained directory", ext2_unlink(fs, ROOT_INO, dname, strlen(dname)) >= 0);
    ckDir("root stamped by that rmdir", fs, ROOT_INO, t);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <ext2 image>\n", argv[0]); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    fakeNow = ts.tv_sec + 100000;  /* well clear of mke2fs's own stamps */

    devFd = open(argv[1], O_RDWR);
    if (devFd < 0) { perror("open"); return 2; }
    ext2_t *fs = mnt();
    if (fs == NULL) { printf("mount FAILED\n"); return 2; }
    printf("dirtime: blocksz=%u\n", fs->blocksz);

    id_t A, B, f, sub, sl, g;
    time_t t;
    stamps_t sA, sB, sF;

    t = tick();
    ck("mkdir /A", ext2_create(fs, ROOT_INO, "A", 1, NULL, S_IFDIR | 0755, &A) >= 0);
    ckDir("mkdir /A stamps /", fs, ROOT_INO, t);
    t = tick();
    ck("mkdir /B", ext2_create(fs, ROOT_INO, "B", 1, NULL, S_IFDIR | 0755, &B) >= 0);
    ckDir("mkdir /B stamps /", fs, ROOT_INO, t);

    /* create */
    sB = mem(fs, B);
    t = tick();
    ck("create /A/f", ext2_create(fs, A, "f", 1, NULL, S_IFREG | 0644, &f) >= 0);
    ckDir("create stamps A", fs, A, t);
    ckSame("...and leaves B alone", fs, B, sB);
    long long fM = mem(fs, f).m;
    ck("/A/f was created at that second", fM == t);

    /* mkdir */
    t = tick();
    ck("mkdir /A/sub", ext2_create(fs, A, "sub", 3, NULL, S_IFDIR | 0755, &sub) >= 0);
    ckDir("mkdir stamps A", fs, A, t);

    /* symlink: what libext2_create() does for otSymlink -- create, write target */
    t = tick();
    ck("symlink /A/sl", ext2_create(fs, A, "sl", 2, NULL, S_IFLNK | 0777, &sl) >= 0 &&
                        ext2_write(fs, sl, 0, "f", 1) == 1);
    ckDir("symlink stamps A", fs, A, t);

    /* hard link */
    sA = mem(fs, A);
    t = tick();
    ck("link /B/f2 -> f", ext2_link(fs, B, "f2", 2, f) >= 0);
    ckDir("link stamps B", fs, B, t);
    ckSame("...and leaves A alone", fs, A, sA);
    ckEntry("link: f's ctime moves, its mtime does not", fs, f, fM, t);

    /* unlink one of two names */
    sB = mem(fs, B);
    t = tick();
    ck("unlink /A/f (f survives as /B/f2)", ext2_unlink(fs, A, "f", 1) >= 0);
    ckDir("unlink stamps A", fs, A, t);
    ckSame("...and leaves B alone", fs, B, sB);
    ckEntry("unlink: f's ctime moves, its mtime does not", fs, f, fM, t);

    /* rmdir */
    t = tick();
    ck("rmdir /A/sub", ext2_unlink(fs, A, "sub", 3) >= 0);
    ckDir("rmdir stamps A", fs, A, t);

    /* rename across directories, as libphoenix rename() does it: link + unlink */
    t = tick();
    ck("rename /B/f2 -> /A/g", ext2_link(fs, A, "g", 1, f) >= 0 && ext2_unlink(fs, B, "f2", 2) >= 0);
    ckDir("rename stamps the NEW parent A", fs, A, t);
    ckDir("rename stamps the OLD parent B", fs, B, t);
    ckEntry("rename: f's mtime is untouched", fs, f, fM, t);

    /* rename within one directory */
    sB = mem(fs, B);
    t = tick();
    ck("rename /A/g -> /A/h", ext2_link(fs, A, "h", 1, f) >= 0 && ext2_unlink(fs, A, "g", 1) >= 0);
    ckDir("same-directory rename stamps A", fs, A, t);
    ckSame("...and leaves B alone", fs, B, sB);
    ckEntry("same-directory rename: f's mtime is untouched", fs, f, fM, t);

    /* a content change of a file is NOT a change of its directory */
    sA = mem(fs, A);
    t = tick();
    ck("write to /A/h", ext2_write(fs, f, 0, "data", 4) == 4);
    ckSame("writing a file leaves its directory alone", fs, A, sA);
    sF = mem(fs, f);
    ck("...and stamps the file itself", sF.m == t && sF.c == t);

    /* the last name goes: f is destroyed, A still changes */
    t = tick();
    ck("unlink /A/h (last name)", ext2_unlink(fs, A, "h", 1) >= 0);
    ckDir("unlinking the last name stamps A", fs, A, t);
    t = tick();
    ck("unlink /A/sl", ext2_unlink(fs, A, "sl", 2) >= 0);
    ckDir("unlinking a symlink stamps A", fs, A, t);

    /* every directory-entry removal path */
    drain(fs, "D1", 0);
    drain(fs, "D2", 1);

    /* the stamps survive a remount */
    t = mem(fs, A).m;
    um(fs);
    fs = mnt();
    ck("remounted", fs != NULL);
    if (fs != NULL) {
        oid_t r, dv;
        ck("lookup /A", ext2_lookup(fs, ROOT_INO, "A", 1, &r, &dv) > 0 && r.id == A);
        ckDir("A's stamps after remount", fs, A, t);
        um(fs);
    }

    fsync(devFd); close(devFd);
    printf("%s\n", fails ? "DIRTIME: checks failed" : "DIRTIME: all checks passed");
    return fails ? 1 : 0;
}
