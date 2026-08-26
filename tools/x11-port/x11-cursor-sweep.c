/*
 * x11-cursor-sweep.c — cursor-motion test driver for the shadow-RAM cursor.
 *
 * No physical mouse is attached during a netboot test, so this client warps the
 * X pointer along a deliberate path (XWarpPointer → the DDX MoveCursor sprite
 * func) to exercise the shadow-RAM cursor over STATIC content — the exact repro
 * for the old miDC GPU-texture save-under smear (moving the pointer over xcalc's
 * buttons tore up their labels). It sweeps slowly in small steps (so any trail
 * would be obvious on an HDMI grab) and parks over the calc region between
 * sweeps so a periodic HDMI tick lands with the arrow on top of static buttons.
 *
 * Build (host): see the one-liner in the accompanying commit / repro script.
 * Run on the Pi:  DISPLAY=:0 /bin/x11-cursor-sweep
 */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static Display *dpy;
static Window root;

static void
warp(int x, int y)
{
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
    XFlush(dpy);
}

/* Sweep from (x0,y0) to (x1,y1) in ~`steps` small hops, `ms` apart. */
static void
sweep(int x0, int y0, int x1, int y1, int steps, int ms)
{
    int i;
    for (i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        warp(x, y);
        usleep(ms * 1000);
    }
}

int
main(void)
{
    int W, H, rep;

    dpy = XOpenDisplay(NULL);
    if (!dpy)
        dpy = XOpenDisplay(":0");
    if (!dpy) {
        fprintf(stderr, "x11-cursor-sweep: cannot open display\n");
        return 1;
    }
    root = DefaultRootWindow(dpy);
    W = DisplayWidth(dpy, DefaultScreen(dpy));
    H = DisplayHeight(dpy, DefaultScreen(dpy));
    printf("x11-cursor-sweep: display %dx%d — driving the pointer\n", W, H);
    fflush(stdout);

    /* The xcalc window is -geometry +1120+330 in the gpudesk scenario; its buttons
     * span roughly x[1120..1330] y[360..620]. Center a park spot on them. */
    for (rep = 0; rep < 6; rep++) {
        /* 1) drag slowly ACROSS the calc buttons left↔right (static content). */
        sweep(1330, 470, 1120, 470, 40, 40);
        warp(1200, 470);
        sleep(3);                       /* hold on the buttons for an HDMI tick */

        /* 2) diagonal sweep across the whole desktop (over every window). */
        sweep(1200, 470, 120, 120, 80, 25);
        sleep(2);

        /* 3) sweep back over the GPU window / clock region and re-park on calc. */
        sweep(120, 120, 1200, 470, 80, 25);
        sleep(2);
    }

    printf("x11-cursor-sweep: done\n");
    fflush(stdout);
    XCloseDisplay(dpy);
    return 0;
}
