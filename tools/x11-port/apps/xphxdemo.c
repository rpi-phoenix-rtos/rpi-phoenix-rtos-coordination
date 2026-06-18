/*
 * xphxdemo — a minimal native Xlib drawing client for the Phoenix-RTOS X11 port.
 *
 * Where `xprobe` only opens (and gracefully fails to open) a display to prove
 * the Xlib connect path links + executes, this exercises the *drawing* surface
 * of the ported libX11: window create/map, graphics context, colour alloc,
 * line/rectangle/arc/text primitives, and an Expose/KeyPress event loop. It is
 * therefore the canonical "first visual app" to run once the kdrive fbdev DDX
 * server lands on /dev/fb0.
 *
 * It degrades cleanly when no server is reachable (prints a line and exits 0),
 * so it links and runs on real hardware today — confirming the drawing-path
 * symbols resolve against the built libX11 and that the client executes with no
 * runtime libc/Xlib fault.
 *
 * Build: tools/x11-port/apps/build.sh (cross aarch64-phoenix, static).
 * Controls when a server exists: q or Esc quits.
 *
 * License: MIT.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 480
#define WIN_H 320

static unsigned long alloc_color(Display *dpy, Colormap cmap, unsigned short r,
	unsigned short g, unsigned short b)
{
	XColor c;
	c.red = r;
	c.green = g;
	c.blue = b;
	c.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(dpy, cmap, &c) == 0) {
		return BlackPixel(dpy, DefaultScreen(dpy));
	}
	return c.pixel;
}

static void draw_scene(Display *dpy, Window win, GC gc, Colormap cmap)
{
	unsigned long bg = alloc_color(dpy, cmap, 0x1010, 0x2020, 0x3838);
	unsigned long accent = alloc_color(dpy, cmap, 0xff00, 0x8000, 0x2000);
	unsigned long fg = alloc_color(dpy, cmap, 0xf0f0, 0xf0f0, 0xf0f0);
	int i;

	/* Background wash. */
	XSetForeground(dpy, gc, bg);
	XFillRectangle(dpy, win, gc, 0, 0, WIN_W, WIN_H);

	/* A frame. */
	XSetForeground(dpy, gc, fg);
	XDrawRectangle(dpy, win, gc, 8, 8, WIN_W - 17, WIN_H - 17);

	/* A fan of accent lines from a corner — exercises XDrawLine. */
	XSetForeground(dpy, gc, accent);
	for (i = 0; i <= WIN_W; i += 24) {
		XDrawLine(dpy, win, gc, 16, 16, i, WIN_H - 16);
	}

	/* A filled disc — exercises XFillArc. */
	XFillArc(dpy, win, gc, WIN_W - 120, WIN_H - 120, 96, 96, 0, 360 * 64);

	/* Title text — exercises XDrawString (server-default font). */
	XSetForeground(dpy, gc, fg);
	XDrawString(dpy, win, gc, 24, 40, "Phoenix-RTOS X11", 16);
	XDrawString(dpy, win, gc, 24, 60, "native Xlib client (q to quit)", 30);

	XFlush(dpy);
}

int main(void)
{
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		/* No server yet (expected until the fbdev DDX runs). Proven: Xlib
		 * linked and the client executed without fault. */
		printf("xphxdemo: no X server reachable yet "
		       "(XOpenDisplay NULL) -- Xlib drawing client linked + ran.\n");
		return 0;
	}

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	Colormap cmap = DefaultColormap(dpy, screen);

	Window win = XCreateSimpleWindow(dpy, root, 0, 0, WIN_W, WIN_H, 0,
		BlackPixel(dpy, screen), BlackPixel(dpy, screen));

	XStoreName(dpy, win, "Phoenix-RTOS xphxdemo");
	XSelectInput(dpy, win, ExposureMask | KeyPressMask);
	XMapWindow(dpy, win);

	GC gc = XCreateGC(dpy, win, 0, NULL);

	for (;;) {
		XEvent ev;
		XNextEvent(dpy, &ev);
		if (ev.type == Expose) {
			if (ev.xexpose.count == 0) {
				draw_scene(dpy, win, gc, cmap);
			}
		}
		else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_q || ks == XK_Escape) {
				break;
			}
		}
	}

	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
