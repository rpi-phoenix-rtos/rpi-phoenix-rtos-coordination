/* Isolate the i_blocks accounting defect: which truncate loses the count? */
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
static ssize_t hr(id_t i, off_t o, char *b, size_t l){(void)i;return pread(devFd,b,l,o);}
static ssize_t hw(id_t i, off_t o, const char *b, size_t l){(void)i;return pwrite(devFd,b,l,o);}
static ext2_t *mnt(void){
    ext2_t *fs = calloc(1,sizeof(ext2_t));
    fs->sectorsz=512; fs->strg=NULL; fs->legacy.devId=0; fs->legacy.read=hr; fs->legacy.write=hw; fs->port=1;
    if(ext2_sb_init(fs)<0||ext2_gdt_init(fs)<0||ext2_objs_init(fs)<0){return NULL;}
    fs->root=ext2_obj_get(fs,ROOT_INO); return fs->root?fs:NULL;
}
static void um(ext2_t*fs){ext2_objs_destroy(fs);ext2_gdt_destroy(fs);ext2_sb_destroy(fs);free(fs);}

static void trial(const char *img, const char *label, size_t writeSz, size_t truncTo, int twoStep)
{
    devFd = open(img, O_RDWR);
    ext2_t *fs = mnt();
    if (!fs) { printf("%-42s MOUNT FAILED\n", label); return; }
    id_t id = 0;
    char nm[24]; int n = snprintf(nm, sizeof(nm), "t%zu_%zu%s", writeSz, truncTo, twoStep ? "b" : "");
    int cerr = ext2_create(fs, ROOT_INO, nm, (size_t)n, NULL, S_IFREG | 0644, &id);
    if (cerr < 0) { printf("%-42s CREATE FAILED err=%d\n", label, cerr); um(fs); close(devFd); return; }
    char *buf = calloc(writeSz ? writeSz : 1, 1);
    if (writeSz) ext2_pwrite(fs, id, 0, buf, writeSz);
    long long blocksAfterWrite = 0;
    ext2_getattr(fs, id, atBlocks, &blocksAfterWrite);
    if (twoStep) ext2_truncate(fs, id, 4096);
    ext2_truncate(fs, id, truncTo);
    long long blocksAfterTrunc = 0, sizeAfter = 0;
    ext2_getattr(fs, id, atBlocks, &blocksAfterTrunc);
    ext2_getattr(fs, id, atSize, &sizeAfter);
    /* Expected i_blocks for a file of `truncTo` bytes with no indirects. */
    long long expect = (long long)((truncTo + fs->blocksz - 1) / fs->blocksz) * (long long)(fs->blocksz / 512);
    printf("%-42s wrote=%-8zu after_write=%-6lld trunc->%-8zu i_blocks=%-6lld expect=%-5lld size=%-8lld %s\n",
           label, writeSz, blocksAfterWrite, truncTo, blocksAfterTrunc, expect, sizeAfter,
           (blocksAfterTrunc == expect) ? "ok" : "<-- MISMATCH");
    free(buf);
    um(fs); fsync(devFd); close(devFd);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* so a crash cannot swallow the output */
    const char *img = argv[1];
    trial(img, "small file (no indirect), trunc 0",        2048,              0, 0);
    trial(img, "12 blocks (direct only), trunc 0",         12u*1024,          0, 0);
    trial(img, "13 blocks (1 single indirect), trunc 0",   13u*1024,          0, 0);
    trial(img, "300 blocks (single indirect), trunc 0",    300u*1024,         0, 0);
    trial(img, "2 MiB (double indirect), trunc 0",         2u*1024*1024,      0, 0);
    trial(img, "2 MiB, trunc 4096 then 0",                 2u*1024*1024,      0, 1);
    trial(img, "2 MiB, trunc to 4096",                     2u*1024*1024,   4096, 0);
    return 0;
}
