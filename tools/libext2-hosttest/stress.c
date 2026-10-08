/* Randomised op stress against the real libext2, graded by a shadow model
 * (content) and e2fsck (structure).
 *
 * The fixed-scenario harness found two defects in two runs; this explores
 * shapes nobody thought to write down. Seeded, so any failure is replayable:
 *   ./stress <img> <seed> <ops>
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
#define NFILES   24
#define MAXSZ    (192u * 1024u)

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

/* shadow model: what each file should contain */
static unsigned char *model[NFILES];
static size_t modelLen[NFILES];
static int live[NFILES];
static id_t ino[NFILES];
static int fails;

static void name_of(int i, char *out, size_t n) { snprintf(out, n, "s%02d", i); }

static int verify(ext2_t *fs, int i)
{
    if (!live[i]) return 0;
    char nm[16]; name_of(i, nm, sizeof(nm));
    long long sz = 0;
    if (ext2_getattr(fs, ino[i], atSize, &sz) < 0) { printf("  [FAIL] %s getattr\n", nm); return 1; }
    if ((size_t)sz != modelLen[i]) {
        printf("  [FAIL] %s size %lld, model says %zu\n", nm, sz, modelLen[i]); return 1;
    }
    if (modelLen[i] == 0) return 0;
    unsigned char *buf = malloc(modelLen[i]);
    ssize_t r = ext2_read(fs, ino[i], 0, (char *)buf, modelLen[i]);
    int bad = 0;
    if (r != (ssize_t)modelLen[i]) { printf("  [FAIL] %s read %zd of %zu\n", nm, r, modelLen[i]); bad = 1; }
    else if (memcmp(buf, model[i], modelLen[i]) != 0) {
        size_t k = 0; while (k < modelLen[i] && buf[k] == model[i][k]) k++;
        printf("  [FAIL] %s content differs at +%zu (got %02x want %02x)\n", nm, k, buf[k], model[i][k]);
        /* How far does the divergence run, and does the data we got belong to
         * another file? Block-level aliasing looks very different from a lost
         * write, and the distinction says which code path to look at. */
        size_t run = 0;
        while (k + run < modelLen[i] && buf[k + run] != model[i][k + run]) run++;
        printf("         divergence runs %zu bytes (%.2f blocks)\n", run, (double)run / 1024.0);
        for (int o = 0; o < NFILES; o++) {
            if (o == i || !live[o] || modelLen[o] < k + 64) continue;
            if (memcmp(buf + k, model[o] + k, 64) == 0) {
                char on[16]; name_of(o, on, sizeof(on));
                printf("         >>> those bytes are %s's content at the SAME offset = block aliasing\n", on);
            }
        }
        bad = 1;
    }
    free(buf);
    return bad;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *img = argv[1];
    unsigned seed = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 0) : 1;
    int ops = (argc > 3) ? atoi(argv[3]) : 400;
    /* Max write length. Defaults to 4096, which at a 4 KiB block size fits in
     * ONE block and so barely exercises ext2_block_sync()'s multi-block run
     * logic -- scale it with the block size to compare like with like. */
    size_t maxw = (argc > 4) ? (size_t)strtoul(argv[4], NULL, 0) : 4096;
    srand(seed);

    devFd = open(img, O_RDWR);
    ext2_t *fs = mnt();
    if (fs == NULL) { printf("mount FAILED\n"); return 2; }
    printf("stress: seed=%u ops=%d blocksz=%u\n", seed, ops, fs->blocksz);

    for (int o = 0; o < ops; o++) {
        int i = rand() % NFILES;
        char nm[16]; name_of(i, nm, sizeof(nm));
        int action = rand() % 100;

        if (!live[i]) {
            if (ext2_create(fs, ROOT_INO, nm, strlen(nm), NULL, S_IFREG | 0644, &ino[i]) < 0) continue;
            live[i] = 1; modelLen[i] = 0;
            free(model[i]); model[i] = malloc(MAXSZ); memset(model[i], 0, MAXSZ);
            continue;
        }

        if (action < 45) {                       /* write at a random offset */
            size_t off = (size_t)(rand() % (int)(MAXSZ - 1024));
            size_t len = 1 + (size_t)(rand() % (int)maxw);
            if (off + len > MAXSZ) len = MAXSZ - off;
            unsigned char *b = malloc(len);
            for (size_t k = 0; k < len; k++) b[k] = (unsigned char)(rand());
            if (ext2_pwrite(fs, ino[i], (off_t)off, (const char *)b, len) == (ssize_t)len) {
                memcpy(model[i] + off, b, len);
                if (off + len > modelLen[i]) modelLen[i] = off + len;
            }
            free(b);
        }
        else if (action < 60) {                  /* append, as O_APPEND does it */
            size_t len = 1 + (size_t)(rand() % (int)(maxw * 2));
            if (modelLen[i] + len > MAXSZ) len = MAXSZ - modelLen[i];
            if (len == 0) continue;
            unsigned char *b = malloc(len);
            for (size_t k = 0; k < len; k++) b[k] = (unsigned char)(rand());
            /* O_APPEND writes at the end of the file whatever offset is passed
             * (0 here), and reports the new end as the offset. */
            off_t pos = 0;
            if (ext2_write(fs, ino[i], &pos, (const char *)b, len, O_APPEND) == (ssize_t)len) {
                if (pos != (off_t)(modelLen[i] + len)) {
                    printf("  [FAIL] %s O_APPEND of %zu at size %zu reported offset %lld\n", nm, len, modelLen[i], (long long)pos);
                    fails++;
                }
                memcpy(model[i] + modelLen[i], b, len);
                modelLen[i] += len;
            }
            free(b);
        }
        else if (action < 75) {                  /* truncate to a random size */
            size_t ns = (size_t)(rand() % (int)(modelLen[i] + 1));
            if (ext2_truncate(fs, ino[i], ns) >= 0) {
                if (ns < modelLen[i]) memset(model[i] + ns, 0, modelLen[i] - ns);
                modelLen[i] = ns;
            }
        }
        else if (action < 85) {                  /* verify now */
            fails += verify(fs, i);
        }
        else {                                   /* unlink */
            if (ext2_unlink(fs, ROOT_INO, nm, strlen(nm)) >= 0) live[i] = 0;
        }
    }

    for (int i = 0; i < NFILES; i++) fails += verify(fs, i);

    /* ---- REMOUNT and verify again ----
     * Everything above is checked while the filesystem is still mounted, so it
     * reads back through the same in-memory objects that were just written.
     * A change that never reached the DEVICE looks perfect there -- defect 19
     * was exactly that, and only e2fsck could see it. Unmounting and mounting
     * again makes Phoenix's own read path the judge of what actually persisted. */
    um(fs);
    fsync(devFd);
    fs = mnt();
    if (fs == NULL) {
        printf("  [FAIL] REMOUNT failed\n");
        fails++;
    }
    else {
        int before = fails;
        for (int i = 0; i < NFILES; i++) {
            if (!live[i]) continue;
            /* ext2_lookup re-resolves the name from disk rather than trusting
             * the id we cached before the unmount. */
            char nm[16]; name_of(i, nm, sizeof(nm));
            oid_t res, dev;
            if (ext2_lookup(fs, ROOT_INO, nm, strlen(nm), &res, &dev) < 0) {
                printf("  [FAIL] %s: gone after remount\n", nm);
                fails++;
                continue;
            }
            ino[i] = res.id;
            fails += verify(fs, i);
        }
        if (fails == before) {
            int n = 0;
            for (int i = 0; i < NFILES; i++) {
                if (live[i]) n++;
            }
            printf("  remount: all %d surviving file(s) verified from disk\n", n);
        }
        um(fs);
    }

    fsync(devFd); close(devFd);
    printf("%s\n", fails ? "STRESS: content mismatches found" : "STRESS: all surviving files match the model");
    return fails ? 1 : 0;
}
