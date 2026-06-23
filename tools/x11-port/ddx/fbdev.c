/*
 * Phoenix-RTOS kdrive fbdev DDX — a minimal software X server backend that
 * paints onto the Raspberry Pi 4 HDMI framebuffer exposed at /dev/fb0.
 *
 * Why this file exists: xorg-server >= 1.17 dropped the kdrive "Xfbdev" backend
 * (hw/kdrive/fbdev). This is a fresh, small reimplementation on the surviving
 * kdrive core (hw/kdrive/src), written against the Phoenix /dev/fb0 ABI rather
 * than the Linux fbdev ABI. It is modelled structurally on the Xephyr backend
 * (hw/kdrive/ephyr/ephyr*.c) — same KdCardFuncs lifecycle — but the "host" is
 * the bare framebuffer device, not another X server.
 *
 * Framebuffer model (Phoenix-specific, see sources/.../video/rpi4-fb):
 *   - Geometry comes from the RPI4FB_GETMODE devctl (width/height/bpp/pitch).
 *   - /dev/fb0 supports read()/write() at a file offset but NOT a live
 *     mmap(fd, 0) backing (issue #149). So we always use a SHADOW framebuffer
 *     (kdrive's KdShadowFbAlloc): X renders into ordinary RAM, and on each
 *     damage update we lseek()+write() the dirty scanlines out to /dev/fb0.
 *
 * Input is stubbed (no-op keyboard + pointer drivers) for this bring-up.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS. %LICENSE%
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include "kdrive.h"
#include "fb.h"
#include "shadow.h"

/*
 * /dev/fb0 client ABI (mirror of sources/phoenix-rtos-devices/video/rpi4-fb/
 * rpi4-fb.h). Kept self-contained so the X cross-build needs no path into the
 * device tree; keep in sync if that ABI changes.
 */
typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned short bpp;
    unsigned short pitch;
    unsigned long long smemlen;
    unsigned long long framebuffer;
} rpi4fb_mode_t;

#define RPI4FB_GETMODE _IOR('g', 1, rpi4fb_mode_t)

#define FBDEV_DEVICE "/dev/fb0"

/* Per-card state: the open fd + the geometry reported by the device. */
typedef struct _FbdevPriv {
    int fd;
    rpi4fb_mode_t mode;
} FbdevPriv;

/* Per-screen state. */
typedef struct _FbdevScrPriv {
    Rotation randr;
    Bool shadow;
} FbdevScrPriv;

extern KdCardFuncs fbdevFuncs;  /* defined at end of file */

static Bool
fbdevInitialize(KdCardInfo *card, FbdevPriv *priv)
{
    priv->fd = open(FBDEV_DEVICE, O_RDWR);
    if (priv->fd < 0) {
        ErrorF("[fbdev] cannot open %s: %s\n", FBDEV_DEVICE, strerror(errno));
        return FALSE;
    }
    if (ioctl(priv->fd, RPI4FB_GETMODE, &priv->mode) < 0) {
        ErrorF("[fbdev] RPI4FB_GETMODE failed on %s: %s\n",
               FBDEV_DEVICE, strerror(errno));
        close(priv->fd);
        priv->fd = -1;
        return FALSE;
    }
    ErrorF("[fbdev] %s: %ux%u bpp=%u pitch=%u smemlen=%llu\n",
           FBDEV_DEVICE, priv->mode.width, priv->mode.height,
           priv->mode.bpp, priv->mode.pitch,
           (unsigned long long) priv->mode.smemlen);
    return TRUE;
}

static Bool
fbdevCardInit(KdCardInfo *card)
{
    FbdevPriv *priv = calloc(1, sizeof(*priv));
    if (!priv)
        return FALSE;

    if (!fbdevInitialize(card, priv)) {
        free(priv);
        return FALSE;
    }
    card->driver = priv;
    return TRUE;
}

/*
 * Translate the device's bpp into the X depth/visual masks. The Pi 4 fb is
 * 32 bpp, 24-bit colour (BGRX in memory → standard X TrueColor masks).
 */
static Bool
fbdevScreenInitialize(KdScreenInfo *screen, FbdevScrPriv *scrpriv)
{
    FbdevPriv *priv = screen->card->driver;
    int depth, bpp;
    Pixel allbits;

    if (!screen->width || !screen->height) {
        screen->width = priv->mode.width;
        screen->height = priv->mode.height;
    }
    /* Conservative physical size if RandR/-screen gave none (96 dpi). */
    if (!screen->width_mm)
        screen->width_mm = (screen->width * 254) / 960;
    if (!screen->height_mm)
        screen->height_mm = (screen->height * 254) / 960;

    bpp = priv->mode.bpp;
    if (bpp == 32) {
        depth = 24;
        screen->fb.redMask = 0x00ff0000;
        screen->fb.greenMask = 0x0000ff00;
        screen->fb.blueMask = 0x000000ff;
    }
    else if (bpp == 16) {
        depth = 16;
        screen->fb.redMask = 0x0000f800;
        screen->fb.greenMask = 0x000007e0;
        screen->fb.blueMask = 0x0000001f;
    }
    else {
        ErrorF("[fbdev] unsupported bpp %d\n", bpp);
        return FALSE;
    }

    screen->fb.depth = depth;
    screen->fb.bitsPerPixel = bpp;
    screen->fb.visuals = (1 << TrueColor);
    allbits = screen->fb.redMask | screen->fb.greenMask | screen->fb.blueMask;
    (void) allbits;

    scrpriv->randr = screen->randr;

    /* No hardware cursor backend (initCursor is NULL) → use the software
     * cursor (mi) so the dix cursor path never dereferences a HW cursor. */
    screen->softCursor = TRUE;

    /*
     * Always shadow: /dev/fb0 has no live mmap backing (issue #149), so X must
     * draw into RAM and we flush to the device. KdShadowFbAlloc sets
     * screen->fb.frameBuffer + byteStride (= width*bpp/8, which for 1024x32
     * equals the device pitch 4096, so the blit is a straight copy).
     */
    screen->fb.shadow = FALSE;
    if (!KdShadowFbAlloc(screen, FALSE))
        return FALSE;
    scrpriv->shadow = TRUE;

    return TRUE;
}

static Bool
fbdevScreenInit(KdScreenInfo *screen)
{
    FbdevScrPriv *scrpriv = calloc(1, sizeof(*scrpriv));
    if (!scrpriv)
        return FALSE;
    screen->driver = scrpriv;
    if (!fbdevScreenInitialize(screen, scrpriv)) {
        screen->driver = NULL;
        free(scrpriv);
        return FALSE;
    }
    return TRUE;
}

/*
 * Damage flush: write the shadow framebuffer out to /dev/fb0. The shadow
 * buffer stride equals the device pitch, so each damaged scanline maps to a
 * contiguous device offset row*pitch. We coalesce to the damaged Y span and
 * write whole rows (full-width); this is correct and simple. (A future
 * optimisation can write only the damaged X sub-extent per row.)
 */
static void
fbdevShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevPriv *priv = screen->card->driver;
    RegionPtr damage = DamageRegion(pBuf->pDamage);
    int pitch = priv->mode.pitch;
    int fbHeight = priv->mode.height;
    CARD8 *shadow = (CARD8 *) screen->fb.frameBuffer;
    int shadowStride = screen->fb.byteStride;
    int y0, y1, rows;
    off_t off;
    size_t len;
    ssize_t w;
    BoxPtr extents;

    if (!shadow || priv->fd < 0)
        return;

    if (RegionNotEmpty(damage)) {
        extents = RegionExtents(damage);
        y0 = extents->y1;
        y1 = extents->y2;
    }
    else {
        /* No damage info → flush the whole frame. */
        y0 = 0;
        y1 = fbHeight;
    }
    if (y0 < 0)
        y0 = 0;
    if (y1 > fbHeight)
        y1 = fbHeight;
    if (y1 <= y0)
        return;
    rows = y1 - y0;

    /*
     * Shadow stride and device pitch are equal here, so the damaged band is a
     * single contiguous region in both buffers: write it in one syscall.
     */
    if (shadowStride == pitch) {
        off = (off_t) y0 * pitch;
        len = (size_t) rows * pitch;
        if (lseek(priv->fd, off, SEEK_SET) == (off_t) -1)
            return;
        while (len > 0) {
            w = write(priv->fd, shadow + off, len);
            if (w <= 0)
                break;
            off += w;
            len -= (size_t) w;
        }
    }
    else {
        /* Strides differ (rotation/reflection): write row by row. */
        int y;
        for (y = y0; y < y1; y++) {
            off = (off_t) y * pitch;
            if (lseek(priv->fd, off, SEEK_SET) == (off_t) -1)
                break;
            (void) write(priv->fd, shadow + (off_t) y * shadowStride,
                         (size_t) pitch);
        }
    }
}

/*
 * shadow window proc: return the address (in the shadow buffer) of scanline
 * `row`. kdrive's shadow layer hands this to update procs that copy
 * shadow→device-memory; we don't use it for the device copy (we write() the
 * shadow ourselves), but shadowAdd requires a non-NULL window proc.
 */
static void *
fbdevWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
                  CARD32 *size, void *closure)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    int stride = screen->fb.byteStride;

    *size = stride;
    return (void *) ((CARD8 *) screen->fb.frameBuffer + row * stride + offset);
}

static Bool
fbdevInitScreen(ScreenPtr pScreen)
{
    return TRUE;
}

static Bool
fbdevFinishInitScreen(ScreenPtr pScreen)
{
    if (!shadowSetup(pScreen))
        return FALSE;
    return TRUE;
}

static Bool
fbdevCreateResources(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    FbdevScrPriv *scrpriv = screen->driver;

    if (scrpriv->shadow)
        return KdShadowSet(pScreen, scrpriv->randr,
                           fbdevShadowUpdate, fbdevWindowLinear);
    return TRUE;
}

static void
fbdevScreenFini(KdScreenInfo *screen)
{
    if (screen->fb.shadow)
        KdShadowFbFree(screen);
}

static void
fbdevCloseScreen(ScreenPtr pScreen)
{
}

static void
fbdevCardFini(KdCardInfo *card)
{
    FbdevPriv *priv = card->driver;
    if (priv) {
        if (priv->fd >= 0)
            close(priv->fd);
        free(priv);
        card->driver = NULL;
    }
}

static void
fbdevGetColors(ScreenPtr pScreen, int n, xColorItem *pdefs)
{
    /* TrueColor only — no palette. */
    while (n--) {
        pdefs->red = pdefs->green = pdefs->blue = 0;
        pdefs++;
    }
}

static void
fbdevPutColors(ScreenPtr pScreen, int n, xColorItem *pdefs)
{
    /* TrueColor only — no palette. */
}

/* -------------------------------------------------------------------------
 * Stub input drivers. kdrive requires at least a keyboard + pointer driver to
 * exist; for this framebuffer bring-up they are no-ops (no /dev/kbd0,/dev/
 * mouse0 wiring yet — see report's gap list). They satisfy KdInitInput so the
 * server reaches the dispatch loop and paints.
 * ------------------------------------------------------------------------- */

static Bool
fbdevKeyboardInit(KdKeyboardInfo *ki)
{
    ki->minScanCode = KD_MIN_KEYCODE;
    ki->maxScanCode = KD_MAX_KEYCODE;
    return TRUE;
}

static Bool
fbdevKeyboardEnable(KdKeyboardInfo *ki)
{
    return TRUE;
}

static void
fbdevKeyboardDisable(KdKeyboardInfo *ki)
{
}

static void
fbdevKeyboardFini(KdKeyboardInfo *ki)
{
}

static void
fbdevKeyboardLeds(KdKeyboardInfo *ki, int leds)
{
}

static void
fbdevKeyboardBell(KdKeyboardInfo *ki, int volume, int frequency, int duration)
{
}

KdKeyboardDriver fbdevKeyboardDriver = {
    "fbdev",
    fbdevKeyboardInit,
    fbdevKeyboardEnable,
    fbdevKeyboardLeds,
    fbdevKeyboardBell,
    fbdevKeyboardDisable,
    fbdevKeyboardFini,
    NULL,
};

static Status
fbdevMouseInit(KdPointerInfo *pi)
{
    return Success;
}

static Status
fbdevMouseEnable(KdPointerInfo *pi)
{
    return Success;
}

static void
fbdevMouseDisable(KdPointerInfo *pi)
{
}

static void
fbdevMouseFini(KdPointerInfo *pi)
{
}

KdPointerDriver fbdevMouseDriver = {
    "fbdev",
    fbdevMouseInit,
    fbdevMouseEnable,
    fbdevMouseDisable,
    fbdevMouseFini,
    NULL,
};

/* -------------------------------------------------------------------------
 * DDX entry points (the symbols dix/os require a DDX backend to provide —
 * same set as hw/kdrive/ephyr/ephyrinit.c). main()/dix_main come from
 * dix/libmain.a; we deliberately do NOT define main().
 * ------------------------------------------------------------------------- */

void
InitCard(char *name)
{
    KdCardInfoAdd(&fbdevFuncs, 0);
}

void
InitOutput(ScreenInfo *pScreenInfo, int argc, char **argv)
{
    KdInitOutput(pScreenInfo, argc, argv);
}

void
InitInput(int argc, char **argv)
{
    KdKeyboardInfo *ki;
    KdPointerInfo *pi;

    KdAddKeyboardDriver(&fbdevKeyboardDriver);
    KdAddPointerDriver(&fbdevMouseDriver);

    ki = KdNewKeyboard();
    if (ki) {
        ki->driver = &fbdevKeyboardDriver;
        KdAddKeyboard(ki);
    }
    pi = KdNewPointer();
    if (pi) {
        pi->driver = &fbdevMouseDriver;
        KdAddPointer(pi);
    }

    KdInitInput();
}

void
CloseInput(void)
{
    KdCloseInput();
}

#if INPUTTHREAD
void
ddxInputThreadInit(void)
{
}
#endif

void
ddxUseMsg(void)
{
    KdUseMsg();
    ErrorF("\nXphoenix (kdrive fbdev) options:\n");
    ErrorF("-screen WIDTHxHEIGHT  Screen size (default: /dev/fb0 native mode)\n");
    ErrorF("\n");
}

int
ddxProcessArgument(int argc, char **argv, int i)
{
    return KdProcessArgument(argc, argv, i);
}

void
OsVendorInit(void)
{
    /*
     * Deliberately do NOT pre-create a card here. KdInitOutput() has its own
     * fallback (kdrive.c): when no card exists yet it calls InitCard(0) +
     * KdScreenInfoAdd() + KdParseScreen(screen, 0), giving a default screen
     * whose width/height our fbdevScreenInitialize() then fills from the
     * /dev/fb0 native mode. If we called InitCard(0) here, kdCardInfo would be
     * non-NULL and KdInitOutput would skip that fallback — registering a card
     * with NO screen, so cardinit/scrinit (the /dev/fb0 open + GETMODE +
     * shadow alloc) would never run. An explicit "-screen WxH" still works:
     * KdProcessArgument adds the card+screen on that path.
     */
}

KdCardFuncs fbdevFuncs = {
    fbdevCardInit,              /* cardinit */
    fbdevScreenInit,            /* scrinit */
    fbdevInitScreen,            /* initScreen */
    fbdevFinishInitScreen,      /* finishInitScreen */
    fbdevCreateResources,       /* createRes */
    fbdevScreenFini,            /* scrfini */
    fbdevCardFini,              /* cardfini */

    0,                          /* initCursor */

    0,                          /* initAccel */
    0,                          /* enableAccel */
    0,                          /* disableAccel */
    0,                          /* finiAccel */

    fbdevGetColors,             /* getColors */
    fbdevPutColors,             /* putColors */

    fbdevCloseScreen,           /* closeScreen */
};
