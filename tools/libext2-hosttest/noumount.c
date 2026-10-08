/* What does a workload leave behind when the filesystem is NEVER unmounted?
 *
 * The SD root is `/`, so it is never unmounted and the harness cuts power.
 * Every other harness here calls ext2_objs_destroy() at the end, which is an
 * unmount -- so none of them models that situation at all. This one does the
 * workload and then just stops, exactly like a power cut after a sync.
 *
 *   ./noumount <img> <mode>
 *      mode 1 = mkdir/rmdir only
 *      mode 2 = create/write/unlink files only
 *      mode 3 = both (what the SD gate ran)
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

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    int mode = atoi(argv[2]);
    devFd = open(argv[1], O_RDWR);

    ext2_t *fs = calloc(1, sizeof(ext2_t));
    fs->sectorsz = 512; fs->strg = NULL;
    fs->legacy.devId = 0; fs->legacy.read = hr; fs->legacy.write = hw; fs->port = 1;
    if (ext2_sb_init(fs) < 0 || ext2_gdt_init(fs) < 0 || ext2_objs_init(fs) < 0) { printf("mount FAILED\n"); return 2; }
    fs->root = ext2_obj_get(fs, ROOT_INO);
    printf("noumount: mode=%d blocksz=%u\n", mode, fs->blocksz);

    if (mode == 1 || mode == 3) {
        id_t d, s;
        ext2_create(fs, ROOT_INO, "c1", 2, NULL, S_IFDIR | 0755, &d);
        ext2_create(fs, d, "sub", 3, NULL, S_IFDIR | 0755, &s);
        printf("  mkdir c1 (ino %llu), c1/sub (ino %llu)\n", (unsigned long long)d, (unsigned long long)s);
        printf("  rmdir sub: %d\n", ext2_unlink(fs, d, "sub", 3));
        printf("  rmdir c1 : %d\n", ext2_unlink(fs, ROOT_INO, "c1", 2));
    }

    if (mode == 2 || mode == 3) {
        /* test_sparse's shape: a hole file and a tail file, each created,
         * written, then unlinked. */
        for (int i = 0; i < 2; i++) {
            char nm[24]; int l = snprintf(nm, sizeof(nm), "sp%d.bin", i);
            id_t f;
            if (ext2_create(fs, ROOT_INO, nm, (size_t)l, NULL, S_IFREG | 0644, &f) < 0) continue;
            char buf[4096];
            memset(buf, 0xAA, sizeof(buf));
            ext2_pwrite(fs, f, 0, buf, sizeof(buf));
            ext2_pwrite(fs, f, 1024 * 1024, buf, sizeof(buf));   /* a hole */
            printf("  file %s (ino %llu) unlink: %d\n", nm, (unsigned long long)f,
                   ext2_unlink(fs, ROOT_INO, nm, (size_t)l));
        }
    }

    /* NO ext2_objs_destroy(): this is the power cut. Only the data that has
     * already reached the device survives. */
    fsync(devFd);
    close(devFd);
    printf("  (no unmount -- stopping here)\n");
    return 0;
}
