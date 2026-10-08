/* Run the REAL libext2 against a file-backed device on the host, then let
 * e2fsck judge the result.
 *
 * Why: eleven silent ext2 defects were found in one week, every one of them
 * passing by return code, and the only oracle was `dd` the device back off the
 * Pi and run `e2fsck -fn` host-side -- a 5-10 minute Pi cycle per attempt.
 * libext2 reaches storage through two plain callbacks (fs->legacy.read/write),
 * so the same code can run against a local file in under a second.
 *
 * It also COUNTS device operations, which is what plan item A2 asked for:
 * how many device writes does one small file write actually cost?
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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
static unsigned long long rdOps, wrOps, rdBytes, wrBytes;
static int countingOn = 1;

static ssize_t hostRead(id_t id, off_t offs, char *buff, size_t len)
{
    (void)id;
    ssize_t r = pread(devFd, buff, len, offs);
    if (r > 0 && countingOn) { rdOps++; rdBytes += (unsigned long long)r; }
    return r;
}

static unsigned long long noopWrites, noopBytes;

static ssize_t hostWrite(id_t id, off_t offs, const char *buff, size_t len)
{
    (void)id;
    /* Count write-backs that store bytes already on the device. A device
     * command that changes nothing is pure wear, and on an overwrite the
     * superblock write is exactly that -- measured here, not assumed. */
    if (countingOn && len <= 4096) {
        char cur[4096];
        if (pread(devFd, cur, len, offs) == (ssize_t)len && memcmp(cur, buff, len) == 0) {
            noopWrites++; noopBytes += (unsigned long long)len;
        }
    }
    ssize_t r = pwrite(devFd, buff, len, offs);
    if (r > 0 && countingOn) { wrOps++; wrBytes += (unsigned long long)r; }
    return r;
}

/* libext2_mount() without the sys/msg.h dependency -- same sequence. */
static ext2_t *mountFs(unsigned sectorsz)
{
    ext2_t *fs = calloc(1, sizeof(ext2_t));
    fs->sectorsz = sectorsz;
    fs->strg = NULL;
    fs->legacy.devId = 0;
    fs->legacy.read = hostRead;
    fs->legacy.write = hostWrite;
    fs->port = 1;
    if (ext2_sb_init(fs) < 0)   { printf("  sb_init FAILED\n");   free(fs); return NULL; }
    if (ext2_gdt_init(fs) < 0)  { printf("  gdt_init FAILED\n");  free(fs); return NULL; }
    if (ext2_objs_init(fs) < 0) { printf("  objs_init FAILED\n"); free(fs); return NULL; }
    if ((fs->root = ext2_obj_get(fs, ROOT_INO)) == NULL) { printf("  root get FAILED\n"); free(fs); return NULL; }
    return fs;
}

static void umountFs(ext2_t *fs)
{
    ext2_objs_destroy(fs);
    ext2_gdt_destroy(fs);
    ext2_sb_destroy(fs);
    free(fs);
}

static int fails;
static void ck(const char *what, int cond)
{
    printf("  [%s] %s\n", cond ? " ok " : "FAIL", what);
    if (!cond) fails++;
}

int main(int argc, char **argv)
{
    const char *img = (argc > 1) ? argv[1] : "ext2.img";
    unsigned blocksz = (argc > 2) ? (unsigned)atoi(argv[2]) : 1024;

    devFd = open(img, O_RDWR);
    if (devFd < 0) { perror("open image"); return 2; }

    ext2_t *fs = mountFs(512);
    if (fs == NULL) return 2;
    printf("mounted: blocksz=%u groups=%u (image made with -b %u)\n\n", fs->blocksz, fs->groups, blocksz);

    /* ---- A2: how many device ops does ONE small write cost? ---- */
    id_t f1;
    ck("create a file", ext2_create(fs, ROOT_INO, "small", 5, NULL, S_IFREG | 0644, &f1) >= 0);
    rdOps = wrOps = rdBytes = wrBytes = 0;
    ssize_t w = ext2_pwrite(fs, f1, 0, "hello world", 11);
    ck("write 11 bytes", w == 11);
    printf("     -> ONE 11-byte write that ALLOCATES a block: %llu device writes (%llu bytes), %llu reads\n",
           wrOps, wrBytes, rdOps);

    /* Now an OVERWRITE in place: allocates nothing, so it should cost strictly
     * less. This separates the allocator's cost from the write() tail, and is
     * the case where the superblock write-back is provably a no-op. */
    rdOps = wrOps = rdBytes = wrBytes = noopWrites = noopBytes = 0;
    ck("overwrite in place", ext2_pwrite(fs, f1, 0, "HELLO WORLD", 11) == 11);
    printf("     -> ONE 11-byte OVERWRITE (no allocation):     %llu device writes (%llu bytes), %llu reads\n",
           wrOps, wrBytes, rdOps);
    printf("     -> of those, %llu wrote bytes ALREADY on the device (%llu bytes) = pure wear\n\n",
           noopWrites, noopBytes);

    /* ---- correctness: read back ---- */
    char buf[64];
    memset(buf, 0, sizeof(buf));
    ck("read back 11 bytes", ext2_read(fs, f1, 0, buf, 11) == 11);
    /* The overwrite above replaced the original text; compare against what was
     * written last, not what was written first. */
    ck("content matches last write", memcmp(buf, "HELLO WORLD", 11) == 0);

    /* ---- the write contract: the new offset comes back, O_APPEND ----
     * ext2_write() takes the offset by pointer and returns the new one (the
     * server answers mtWrite with it); O_APPEND writes at the end of the file
     * whatever offset is passed. ext2_pwrite() checks the plain case on every
     * write; these are the cases it does not reach. */
    id_t fa;
    off_t pos;
    ck("create append file", ext2_create(fs, ROOT_INO, "append", 6, NULL, S_IFREG | 0644, &fa) >= 0);
    pos = 0;
    ck("write \"abc\" at 0", ext2_write(fs, fa, &pos, "abc", 3, 0) == 3);
    ck("  ...offset reported as 3", pos == 3);
    pos = 0;
    ck("O_APPEND \"de\" passing offset 0", ext2_write(fs, fa, &pos, "de", 2, O_APPEND) == 2);
    ck("  ...offset reported as the new end, 5", pos == 5);
    pos = 100;
    ck("O_APPEND \"f\" passing offset 100 (past the end)", ext2_write(fs, fa, &pos, "f", 1, O_APPEND) == 1);
    ck("  ...offset reported as 6", pos == 6);
    long long asz = 0;
    ck("  ...size is 6, no hole at 100", ext2_getattr(fs, fa, atSize, &asz) >= 0 && asz == 6);
    memset(buf, 0, sizeof(buf));
    ck("  ...reads back \"abcdef\"", ext2_read(fs, fa, 0, buf, sizeof(buf)) == 6 && memcmp(buf, "abcdef", 6) == 0);
    pos = 1;
    ck("overwrite \"X\" at 1 without O_APPEND", ext2_write(fs, fa, &pos, "X", 1, 0) == 1);
    ck("  ...offset 2, size still 6", pos == 2 && ext2_getattr(fs, fa, atSize, &asz) >= 0 && asz == 6);
    pos = 3;
    ck("zero-length write", ext2_write(fs, fa, &pos, "", 0, 0) == 0);
    ck("  ...leaves the offset at 3", pos == 3);
    pos = 7;
    ck("write to a directory fails", ext2_write(fs, ROOT_INO, &pos, "x", 1, 0) < 0);
    ck("  ...and leaves the offset alone", pos == 7);
    pos = 7;
    ck("O_APPEND to a directory fails", ext2_write(fs, ROOT_INO, &pos, "x", 1, O_APPEND) < 0);
    ck("  ...and leaves the offset alone", pos == 7);

    /* ---- a hole must read as zeros ---- */
    id_t f2;
    ck("create sparse file", ext2_create(fs, ROOT_INO, "sparse", 6, NULL, S_IFREG | 0644, &f2) >= 0);
    ck("write at offset 1 MiB", ext2_pwrite(fs, f2, 1024 * 1024, "tail", 4) == 4);
    /* O_APPEND goes by the size, not by the allocated blocks: past the hole */
    pos = 0;
    ck("O_APPEND to the sparse file", ext2_write(fs, f2, &pos, "+app", 4, O_APPEND) == 4);
    ck("  ...lands at 1 MiB + 4", pos == 1024 * 1024 + 8);
    memset(buf, 0, sizeof(buf));
    ck("  ...reads back \"tail+app\"", ext2_read(fs, f2, 1024 * 1024, buf, 8) == 8 && memcmp(buf, "tail+app", 8) == 0);
    char z[512];
    memset(z, 0xFF, sizeof(z));
    ck("hole reads as zeros", ext2_read(fs, f2, 4096, z, sizeof(z)) == (ssize_t)sizeof(z));
    int allZero = 1;
    for (size_t i = 0; i < sizeof(z); i++) if (z[i] != 0) { allZero = 0; break; }
    ck("  ...and really is zeros", allZero);

    /* ---- a large contiguous file: the read-coalescing path ---- */
    id_t f3;
    ck("create big file", ext2_create(fs, ROOT_INO, "big", 3, NULL, S_IFREG | 0644, &f3) >= 0);
    size_t big = 2u * 1024u * 1024u;
    unsigned char *src = malloc(big), *back = malloc(big);
    for (size_t i = 0; i < big; i++) src[i] = (unsigned char)(i * 7 + (i >> 11));
    ck("write 2 MiB", ext2_pwrite(fs, f3, 0, (const char *)src, big) == (ssize_t)big);
    memset(back, 0, big);
    ck("read 2 MiB back", ext2_read(fs, f3, 0, (char *)back, big) == (ssize_t)big);
    ck("2 MiB byte-identical", memcmp(src, back, big) == 0);

    /* ---- create many, delete half: the allocator + i_dtime path ---- */
    for (int i = 0; i < 60; i++) {
        char nm[32]; int n = snprintf(nm, sizeof(nm), "f%03d", i);
        id_t id;
        if (ext2_create(fs, ROOT_INO, nm, (size_t)n, NULL, S_IFREG | 0644, &id) < 0) { ck("bulk create", 0); break; }
        if (ext2_pwrite(fs, id, 0, nm, (size_t)n) != n) { ck("bulk write", 0); break; }
    }
    ck("created 60 files", 1);
    for (int i = 0; i < 60; i += 2) {
        char nm[32]; int n = snprintf(nm, sizeof(nm), "f%03d", i);
        if (ext2_unlink(fs, ROOT_INO, nm, (size_t)n) < 0) { ck("bulk unlink", 0); break; }
    }
    ck("unlinked 30 files", 1);

    /* ---- truncate ---- */
    ck("truncate big to 4096", ext2_truncate(fs, f3, 4096) >= 0);
    ck("truncate big to 0", ext2_truncate(fs, f3, 0) >= 0);

    umountFs(fs);
    fsync(devFd);
    close(devFd);

    printf("\ntotals: %llu device writes (%llu bytes), %llu reads (%llu bytes)\n",
           wrOps, wrBytes, rdOps, rdBytes);
    printf("\n%s\n", fails ? "RESULT: libext2 FAILED a check" : "RESULT: all libext2 checks passed");
    free(src); free(back);
    return fails ? 1 : 0;
}
