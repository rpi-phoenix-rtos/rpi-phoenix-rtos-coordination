/* Two threads driving one mounted libext2, with REAL mutexes.
 *
 * This is what production does: the umass driver dispatches filesystem
 * messages with UMASS_N_MSG_THREADS = 2. Every other harness here is
 * single-threaded, so nothing exercised the locking at all.
 *
 * ⚠ IT MUST HOLD fs->lock AROUND EACH OPERATION, exactly as the real message
 * handler does (libext2.c takes fs->lock for the whole of libext2_handler).
 * That is why the allocators in block.c/inode.c do not take it themselves.
 * Calling the public API directly WITHOUT that lock is not a stricter test,
 * it is a different program: the first version of this file did so and
 * produced content mismatches and e2fsck "multiply-claimed block(s) in inode
 * 24: 535" -- two files sharing a block. All of it an artefact of the test,
 * not a defect. Verify the harness models the caller before believing it.
 *
 * Each thread owns its own directory and its own files, so any interference is
 * a libext2 problem rather than the test racing itself. The shared state is
 * what matters: fs->objs (the rb tree, the LRU, the count), fs->sb free
 * counts, fs->gdt, and the block/inode bitmaps.
 *
 * Build with -DSHIM_REAL_MUTEX. ASan catches memory errors; a separate TSan
 * build catches data races on fields the locks do not actually protect.
 *
 *   ./concurrent <img> <threads> <ops-per-thread>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/threads.h>
#include "ext2.h"
#include "ext2io.h"
#include "sb.h"
#include "gdt.h"
#include "obj.h"

#define ROOT_INO  2
#define MAXFILES  8
#define MAXSZ     (32u * 1024u)

static int devFd = -1;
static ext2_t *fs;
static int fails;
static pthread_mutex_t reportLock = PTHREAD_MUTEX_INITIALIZER;

static ssize_t hr(id_t i, off_t o, char *b, size_t l) { (void)i; return pread(devFd, b, l, o); }
static ssize_t hw(id_t i, off_t o, const char *b, size_t l) { (void)i; return pwrite(devFd, b, l, o); }

static void fail(const char *fmt, int a, int b)
{
    pthread_mutex_lock(&reportLock);
    printf("  [FAIL] ");
    printf(fmt, a, b);
    printf("\n");
    fails++;
    pthread_mutex_unlock(&reportLock);
}

struct work {
    int id;
    int ops;
    id_t dir;
};

static void *worker(void *arg)
{
    struct work *w = arg;
    unsigned seed = (unsigned)(w->id * 7919 + 13);
    id_t ids[MAXFILES];
    size_t lens[MAXFILES];
    unsigned char *model[MAXFILES];
    int live[MAXFILES];

    memset(live, 0, sizeof(live));
    for (int i = 0; i < MAXFILES; i++) {
        model[i] = calloc(MAXSZ, 1);
        lens[i] = 0;
    }

    for (int o = 0; o < w->ops; o++) {
        int i = rand_r(&seed) % MAXFILES;
        char nm[32];
        int nl = snprintf(nm, sizeof(nm), "t%d-f%d", w->id, i);

        if (!live[i]) {
            mutexLock(fs->lock);
            int cr = ext2_create(fs, w->dir, nm, (size_t)nl, NULL, S_IFREG | 0644, &ids[i]);
            mutexUnlock(fs->lock);
            if (cr >= 0) {
                live[i] = 1;
                lens[i] = 0;
                /* A recreated file starts empty: clear the model too, or the
                 * previous incarnation's bytes get compared against what is
                 * now a hole. Omitting this produced 93 "content differs"
                 * failures -- identical at both block sizes, which is what
                 * gave it away: a race varies, a test bug does not. */
                memset(model[i], 0, MAXSZ);
            }
            continue;
        }

        int act = rand_r(&seed) % 100;

        if (act < 50) {                       /* write at a random offset */
            size_t off = (size_t)(rand_r(&seed) % (int)(MAXSZ - 2048));
            size_t len = 1 + (size_t)(rand_r(&seed) % 2048);
            unsigned char *b = malloc(len);
            for (size_t k = 0; k < len; k++) b[k] = (unsigned char)rand_r(&seed);
            mutexLock(fs->lock);
            ssize_t wr = ext2_pwrite(fs, ids[i], (off_t)off, (const char *)b, len);
            mutexUnlock(fs->lock);
            if (wr == (ssize_t)len) {
                memcpy(model[i] + off, b, len);
                if (off + len > lens[i]) lens[i] = off + len;
            }
            free(b);
        }
        else if (act < 75) {                  /* verify against the model */
            if (lens[i] == 0) continue;
            unsigned char *got = malloc(lens[i]);
            mutexLock(fs->lock);
            ssize_t r = ext2_read(fs, ids[i], 0, (char *)got, lens[i]);
            mutexUnlock(fs->lock);
            if (r != (ssize_t)lens[i]) {
                fail("t%d f%d: short read", w->id, i);
            }
            else if (memcmp(got, model[i], lens[i]) != 0) {
                fail("t%d f%d: content differs", w->id, i);
            }
            free(got);
        }
        else if (act < 88) {                  /* truncate */
            size_t ns = (size_t)(rand_r(&seed) % (int)(lens[i] + 1));
            mutexLock(fs->lock);
            int tr = ext2_truncate(fs, ids[i], ns);
            mutexUnlock(fs->lock);
            if (tr >= 0) {
                if (ns < lens[i]) memset(model[i] + ns, 0, lens[i] - ns);
                lens[i] = ns;
            }
        }
        else {                                /* unlink */
            mutexLock(fs->lock);
            int ul = ext2_unlink(fs, w->dir, nm, (size_t)nl);
            mutexUnlock(fs->lock);
            if (ul >= 0) live[i] = 0;
        }
    }

    /* Final verify of whatever survived. */
    for (int i = 0; i < MAXFILES; i++) {
        if (!live[i] || lens[i] == 0) continue;
        unsigned char *got = malloc(lens[i]);
        mutexLock(fs->lock);
        ssize_t fr = ext2_read(fs, ids[i], 0, (char *)got, lens[i]);
        mutexUnlock(fs->lock);
        if (fr != (ssize_t)lens[i]) fail("t%d f%d: final short read", w->id, i);
        else if (memcmp(got, model[i], lens[i]) != 0) fail("t%d f%d: final content differs", w->id, i);
        free(got);
    }

    for (int i = 0; i < MAXFILES; i++) free(model[i]);
    return NULL;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *img = argv[1];
    int nthreads = (argc > 2) ? atoi(argv[2]) : 2;
    int ops = (argc > 3) ? atoi(argv[3]) : 400;

    devFd = open(img, O_RDWR);
    fs = calloc(1, sizeof(ext2_t));
    fs->sectorsz = 512; fs->strg = NULL;
    fs->legacy.devId = 0; fs->legacy.read = hr; fs->legacy.write = hw; fs->port = 1;
    if (ext2_sb_init(fs) < 0 || ext2_gdt_init(fs) < 0 || ext2_objs_init(fs) < 0) { printf("mount FAILED\n"); return 2; }
    fs->root = ext2_obj_get(fs, ROOT_INO);
    if (fs->root == NULL) { printf("root FAILED\n"); return 2; }
    printf("concurrent: %d threads x %d ops, blocksz=%u\n", nthreads, ops, fs->blocksz);

    struct work *w = calloc((size_t)nthreads, sizeof(*w));
    pthread_t *th = calloc((size_t)nthreads, sizeof(*th));

    /* One directory per thread, created up front so the run itself only
     * exercises steady-state operations. */
    for (int i = 0; i < nthreads; i++) {
        char dn[32];
        int dl = snprintf(dn, sizeof(dn), "d%d", i);
        if (ext2_create(fs, ROOT_INO, dn, (size_t)dl, NULL, S_IFDIR | 0755, &w[i].dir) < 0) {
            printf("  [FAIL] could not create %s\n", dn);
            return 1;
        }
        w[i].id = i;
        w[i].ops = ops;
    }

    for (int i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, worker, &w[i]);
    for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);

    ext2_objs_destroy(fs); ext2_gdt_destroy(fs); ext2_sb_destroy(fs); free(fs);
    fsync(devFd); close(devFd);
    printf("%s\n", fails ? "CONCURRENT: checks failed" : "CONCURRENT: all threads consistent");
    return fails ? 1 : 0;
}
