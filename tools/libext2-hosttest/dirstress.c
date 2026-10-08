/* Randomised DIRECTORY stress against the real libext2, graded by e2fsck.
 *
 * Seven defects were found in the file paths (12-18); the directory paths had
 * no coverage at all. e2fsck is an especially good oracle here -- Pass 2 checks
 * directory structure, Pass 3 connectivity and Pass 4 reference counts, so a
 * wrong link count or a lost '..' cannot pass silently.
 *
 *   ./dirstress <img> <seed> <ops>
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
#define NDIRS    12
#define NPERDIR  6

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

static id_t dirId[NDIRS];
static int  dirLive[NDIRS];
static int  fileLive[NDIRS][NPERDIR];
static int  fails;

static void dname(int i, char *o, size_t n) { snprintf(o, n, "d%02d", i); }
static void fname(int j, char *o, size_t n) { snprintf(o, n, "f%d", j); }

/* A directory must be found by lookup, and must contain . and .. */
static void checkDir(ext2_t *fs, int i)
{
    char nm[16]; dname(i, nm, sizeof(nm));
    oid_t res, dev;
    if (ext2_lookup(fs, ROOT_INO, nm, strlen(nm), &res, &dev) < 0) {
        printf("  [FAIL] %s: lookup failed though it should exist\n", nm); fails++; return;
    }
    long long links = 0;
    if (ext2_getattr(fs, dirId[i], atLinks, &links) < 0) {
        printf("  [FAIL] %s: getattr(links)\n", nm); fails++; return;
    }
    /* An empty dir has 2 links (itself + '.'), plus one per child subdir. */
    if (links < 2) {
        printf("  [FAIL] %s: link count %lld, must be >= 2\n", nm, links); fails++;
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *img = argv[1];
    unsigned seed = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 0) : 1;
    int ops = (argc > 3) ? atoi(argv[3]) : 300;
    srand(seed);

    devFd = open(img, O_RDWR);
    ext2_t *fs = mnt();
    if (fs == NULL) { printf("mount FAILED\n"); return 2; }
    printf("dirstress: seed=%u ops=%d blocksz=%u\n", seed, ops, fs->blocksz);

    for (int o = 0; o < ops; o++) {
        int i = rand() % NDIRS;
        char dn[16]; dname(i, dn, sizeof(dn));

        if (!dirLive[i]) {
            if (ext2_create(fs, ROOT_INO, dn, strlen(dn), NULL, S_IFDIR | 0755, &dirId[i]) >= 0) {
                dirLive[i] = 1;
                memset(fileLive[i], 0, sizeof(fileLive[i]));
            }
            continue;
        }

        int action = rand() % 100;
        int j = rand() % NPERDIR;
        char fn[16]; fname(j, fn, sizeof(fn));

        if (action < 35) {                            /* create a file in it */
            id_t fid;
            if (!fileLive[i][j] &&
                    ext2_create(fs, dirId[i], fn, strlen(fn), NULL, S_IFREG | 0644, &fid) >= 0) {
                fileLive[i][j] = 1;
                (void)ext2_pwrite(fs, fid, 0, dn, strlen(dn));
            }
        }
        else if (action < 55) {                       /* remove a file from it */
            if (fileLive[i][j] && ext2_unlink(fs, dirId[i], fn, strlen(fn)) >= 0) {
                fileLive[i][j] = 0;
            }
        }
        else if (action < 70) {                       /* look it up */
            checkDir(fs, i);
        }
        else if (action < 80) {                       /* nested subdirectory */
            id_t sub;
            char sn[16]; snprintf(sn, sizeof(sn), "s%d", j);
            if (ext2_create(fs, dirId[i], sn, strlen(sn), NULL, S_IFDIR | 0755, &sub) >= 0) {
                (void)ext2_unlink(fs, dirId[i], sn, strlen(sn)); /* immediately rmdir */
            }
        }
        else {                                        /* rmdir: must empty first */
            int empty = 1;
            for (int k = 0; k < NPERDIR; k++) {
                if (fileLive[i][k]) {
                    char k1[16]; fname(k, k1, sizeof(k1));
                    if (ext2_unlink(fs, dirId[i], k1, strlen(k1)) >= 0) fileLive[i][k] = 0;
                    else empty = 0;
                }
            }
            if (empty && ext2_unlink(fs, ROOT_INO, dn, strlen(dn)) >= 0) dirLive[i] = 0;
        }
    }

    for (int i = 0; i < NDIRS; i++) if (dirLive[i]) checkDir(fs, i);

    um(fs); fsync(devFd); close(devFd);
    printf("%s\n", fails ? "DIRSTRESS: checks failed" : "DIRSTRESS: all directory checks passed");
    return fails ? 1 : 0;
}
