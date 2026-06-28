/*
 * xcolortest — draw labelled rectangles in known colours and log the X pixel
 * value allocated for each. A colour-correctness probe for the Phoenix fbdev X
 * server: compare the requested RGB -> the XAllocColor pixel (how X packed it
 * for the visual masks) -> what actually shows on HDMI. Greys (R==G==B) being
 * right while hues are wrong pinpoints a channel-order bug.
 *
 * Uses XAllocColor (the same path wmaker/xbill use), NOT raw pixel writes, so it
 * reproduces the real client colour path.
 *
 * Copyright 2026 Phoenix Systems. %LICENSE%
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct namedColor {
    const char *name;
    unsigned short r, g, b; /* 8-bit, scaled to 16-bit for XAllocColor */
};

static const struct namedColor colors[] = {
    { "red",     255,   0,   0 },
    { "green",     0, 255,   0 },
    { "blue",      0,   0, 255 },
    { "yellow",  255, 255,   0 },
    { "magenta", 255,   0, 255 },
    { "cyan",      0, 255, 255 },
    { "orange",  255, 165,   0 },
    { "pink",    255, 192, 203 },
    { "white",   255, 255, 255 },
    { "grey",    128, 128, 128 },
    { "black",     0,   0,   0 },
};
#define NCOLORS ((int)(sizeof(colors) / sizeof(colors[0])))

int
main(int argc, char **argv)
{
    Display *dpy;
    int screen;
    Window root, win;
    GC gc;
    XColor xc[NCOLORS];
    unsigned long px[NCOLORS];
    int W, H, cols, rows, cw, ch, i;
    Colormap cmap;
    XEvent ev;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "xcolortest: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    W = DisplayWidth(dpy, screen);
    H = DisplayHeight(dpy, screen);

    /* Report the visual masks the server advertises — the ground truth for how
     * X will pack each colour. */
    {
        Visual *v = DefaultVisual(dpy, screen);
        fprintf(stderr,
                "xcolortest: visual depth=%d  R=0x%06lx G=0x%06lx B=0x%06lx\n",
                DefaultDepth(dpy, screen),
                (unsigned long) v->red_mask,
                (unsigned long) v->green_mask,
                (unsigned long) v->blue_mask);
    }

    /* Allocate the colours and log the pixel value X computed for each. */
    for (i = 0; i < NCOLORS; i++) {
        xc[i].red = (unsigned short) (colors[i].r * 257);   /* 8->16 bit */
        xc[i].green = (unsigned short) (colors[i].g * 257);
        xc[i].blue = (unsigned short) (colors[i].b * 257);
        xc[i].flags = DoRed | DoGreen | DoBlue;
        if (!XAllocColor(dpy, cmap, &xc[i])) {
            fprintf(stderr, "xcolortest: XAllocColor(%s) failed\n",
                    colors[i].name);
            xc[i].pixel = 0;
        }
        px[i] = xc[i].pixel;
        fprintf(stderr,
                "xcolortest: %-8s req(%3u,%3u,%3u) -> pixel=0x%08lx\n",
                colors[i].name, colors[i].r, colors[i].g, colors[i].b,
                (unsigned long) px[i]);
    }

    win = XCreateSimpleWindow(dpy, root, 0, 0, (unsigned) W, (unsigned) H, 0,
                              BlackPixel(dpy, screen), BlackPixel(dpy, screen));
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask);
    XMapRaised(dpy, win);
    gc = XCreateGC(dpy, win, 0, NULL);

    /* Grid layout. */
    cols = 4;
    rows = (NCOLORS + cols - 1) / cols;
    cw = W / cols;
    ch = H / rows;

    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == ButtonPress || ev.type == KeyPress)
            break;
        if (ev.type != Expose || ev.xexpose.count != 0)
            continue;

        for (i = 0; i < NCOLORS; i++) {
            int col = i % cols, row = i / cols;
            int x = col * cw, y = row * ch;
            char label[64];

            XSetForeground(dpy, gc, px[i]);
            XFillRectangle(dpy, win, gc, x, y, (unsigned) cw, (unsigned) ch);

            /* Label in a contrasting colour (black on light, white on dark). */
            XSetForeground(dpy, gc,
                           (colors[i].r + colors[i].g + colors[i].b > 384)
                               ? BlackPixel(dpy, screen)
                               : WhitePixel(dpy, screen));
            snprintf(label, sizeof(label), "%s (%u,%u,%u) px=0x%06lx",
                     colors[i].name, colors[i].r, colors[i].g, colors[i].b,
                     (unsigned long) (px[i] & 0xffffffUL));
            XDrawString(dpy, win, gc, x + 8, y + 18, label, (int) strlen(label));
        }
        XFlush(dpy);
    }

    XCloseDisplay(dpy);
    (void) argc;
    (void) argv;
    return 0;
}
