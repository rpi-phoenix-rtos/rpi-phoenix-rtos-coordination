/*
 * xresizer -- a deterministic, mouse-free probe for the GPU X server's window
 * RESIZE path (owner bug #1: "resizing an xterm does not work, strange visual
 * artefacts").
 *
 * A resize is hard to test on this board because there is no way to drive the
 * pointer, and xterm's own XTWINOPS escape needs a shell inside a pty.  This
 * client resizes ITSELF on a timer and repaints a pattern that makes the two
 * failure modes we suspect unmistakable on an HDMI grab:
 *
 *   - a vertical MIRROR shows up as the red bar moving from the top edge to the
 *     bottom edge (the bars are asymmetric on purpose),
 *   - a stale/mistiled repaint shows up as a broken diagonal or as the previous
 *     size's border still visible inside the new one.
 *
 * Every other step is repaint-suppressed, so we can also see what the server
 * leaves behind in the newly exposed region when the client does NOT redraw.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* Report where the window really is: under a reparenting WM the client's own
 * coordinates are relative to the WM frame, so a window that is drawn but never
 * seen is either mis-placed by the WM or dropped by the server -- and only the
 * root-relative position tells the two apart. */
static void report_placement(Display *dpy, Window win)
{
	Window root, parent, *kids = NULL, child;
	unsigned int nkids = 0;
	int rx = -1, ry = -1;

	if (XQueryTree(dpy, win, &root, &parent, &kids, &nkids) != 0) {
		if (kids != NULL) {
			XFree(kids);
		}
		(void)XTranslateCoordinates(dpy, win, root, 0, 0, &rx, &ry, &child);
		fprintf(stderr, "xresizer: parent=0x%lx (root=0x%lx) root-relative %d,%d\n",
				(unsigned long)parent, (unsigned long)root, rx, ry);
	}
}

struct step {
	int w, h;
	int repaint; /* 0 = deliberately leave the new area untouched */
};

static const struct step steps[] = {
	{ 500, 360, 1 }, { 760, 520, 1 }, { 380, 260, 1 }, { 900, 620, 1 },
	{ 640, 200, 0 }, { 640, 700, 0 }, { 500, 360, 1 },
};
#define NSTEPS ((int)(sizeof(steps) / sizeof(steps[0])))

static unsigned long alloc_colour(Display *d, Colormap cm, int r, int g, int b)
{
	XColor c;

	c.red = (unsigned short)(r * 257);
	c.green = (unsigned short)(g * 257);
	c.blue = (unsigned short)(b * 257);
	c.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(d, cm, &c) == 0) {
		return 0;
	}

	return c.pixel;
}

int main(int argc, char **argv)
{
	Display *dpy;
	Window win;
	GC gc;
	Colormap cm;
	XSizeHints hints;
	int scr, step = 0, w = steps[0].w, h = steps[0].h;
	unsigned long white, red, green, blue, yellow;
	int period = (argc > 1) ? atoi(argv[1]) : 12; /* seconds between resizes */
	/* argv[2] selects which of the interactive-resize ops to exercise, so the
	 * two can be bisected against each other:
	 *   "band"  - only the XOR rubber band on the root
	 *   "copy"  - only the in-window XCopyArea
	 *   "both"  - both (default) */
	const char *mode = (argc > 2) ? argv[2] : "both";
	int do_band = (strcmp(mode, "copy") != 0);
	int do_copy = (strcmp(mode, "band") != 0);
	struct timeval next;
	GC rootgc;
	Window root;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xresizer: cannot open display\n");
		return 1;
	}

	scr = DefaultScreen(dpy);
	cm = DefaultColormap(dpy, scr);
	white = alloc_colour(dpy, cm, 255, 255, 255);
	red = alloc_colour(dpy, cm, 220, 30, 30);
	green = alloc_colour(dpy, cm, 30, 200, 30);
	blue = alloc_colour(dpy, cm, 40, 60, 230);
	yellow = alloc_colour(dpy, cm, 240, 220, 40);

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 260, 180, w, h, 2,
			BlackPixel(dpy, scr), white);

	/* NorthWest bit gravity: without it the default is ForgetGravity and the
	 * server may legally discard the old contents on every resize, which makes
	 * the repaint-suppressed steps say nothing about a real artefact. */
	{
		XSetWindowAttributes swa;

		swa.bit_gravity = NorthWestGravity;
		XChangeWindowAttributes(dpy, win, CWBitGravity, &swa);
	}

	/* USPosition so a window manager places it immediately instead of asking
	 * for an interactive rubber-band we have no pointer to drive. */
	memset(&hints, 0, sizeof(hints));
	hints.flags = USPosition | USSize;
	hints.x = 260;
	hints.y = 180;
	hints.width = w;
	hints.height = h;
	XSetNormalHints(dpy, win, &hints);
	XStoreName(dpy, win, "xresizer");
	{
		XClassHint cls;

		cls.res_name = "xresizer";
		cls.res_class = "Xresizer";
		XSetClassHint(dpy, win, &cls);
	}

	XSelectInput(dpy, win, ExposureMask | StructureNotifyMask);
	XMapWindow(dpy, win);
	XSync(dpy, False);
	report_placement(dpy, win);

	gc = XCreateGC(dpy, win, 0, NULL);

	/* An XOR GC on the ROOT window is what a reparenting WM uses to rubber-band
	 * an interactive resize (WindowMaker included).  GXxor has no GPU path in
	 * glamor, so every such line takes the CPU fallback: download the screen
	 * pixmap, do the op with fb, upload it again -- i.e. straight through the
	 * two functions our own glamor patches touch.  Drawing the same rectangle
	 * twice must restore the screen exactly; anything left behind is the
	 * artefact the owner reported. */
	root = RootWindow(dpy, scr);
	{
		XGCValues gcv;

		gcv.function = GXxor;
		gcv.foreground = white ^ blue;
		gcv.subwindow_mode = IncludeInferiors;
		rootgc = XCreateGC(dpy, root, GCFunction | GCForeground | GCSubwindowMode, &gcv);
	}

	gettimeofday(&next, NULL);
	next.tv_sec += period;

	for (;;) {
		struct timeval now, tv;
		fd_set rfds;
		int fd = ConnectionNumber(dpy);

		while (XPending(dpy) != 0) {
			XEvent ev;

			XNextEvent(dpy, &ev);
			if (ev.type == ConfigureNotify) {
				w = ev.xconfigure.width;
				h = ev.xconfigure.height;
				fprintf(stderr, "xresizer: configure %dx%d at %d,%d\n", w, h,
						ev.xconfigure.x, ev.xconfigure.y);
				report_placement(dpy, win);
			}
			if ((ev.type == Expose) && (ev.xexpose.count == 0)) {
				fprintf(stderr, "xresizer: expose, paint %dx%d\n", w, h);
				/* asymmetric bars: red only at the TOP, blue only at the
				 * BOTTOM, so a vertical mirror is unmistakable */
				XSetForeground(dpy, gc, white);
				XFillRectangle(dpy, win, gc, 0, 0, w, h);
				XSetForeground(dpy, gc, red);
				XFillRectangle(dpy, win, gc, 0, 0, w, 28);
				XSetForeground(dpy, gc, blue);
				XFillRectangle(dpy, win, gc, 0, h - 14, w, 14);
				XSetForeground(dpy, gc, green);
				XFillRectangle(dpy, win, gc, 0, 0, 10, h);
				XSetForeground(dpy, gc, yellow);
				XFillRectangle(dpy, win, gc, w - 40, 0, 40, h);
				/* diagonal: top-left -> bottom-right, plus a mid cross */
				XSetForeground(dpy, gc, BlackPixel(dpy, scr));
				XDrawLine(dpy, win, gc, 0, 0, w - 1, h - 1);
				XDrawLine(dpy, win, gc, 0, h / 2, w - 1, h / 2);
				XDrawRectangle(dpy, win, gc, 4, 4, w - 9, h - 9);
				{
					char label[64];
					int n = snprintf(label, sizeof(label), "xresizer %dx%d step %d",
							w, h, step);
					XDrawString(dpy, win, gc, 24, 60, label, n);
				}
				XFlush(dpy);
			}
		}

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		(void)select(fd + 1, &rfds, NULL, NULL, &tv);

		gettimeofday(&now, NULL);
		if (now.tv_sec < next.tv_sec) {
			continue;
		}

		if (step >= NSTEPS) {
			fprintf(stderr, "xresizer: all %d steps done\n", NSTEPS);
			break;
		}

		/* Rubber-band the target geometry the way a WM would: draw, settle,
		 * draw again to undo. */
		if (do_band != 0) {
			int i, rw = steps[step].w, rh = steps[step].h;

			for (i = 0; i < 2; i++) {
				XDrawRectangle(dpy, root, rootgc, 240, 160, rw + 40, rh + 40);
				XDrawRectangle(dpy, root, rootgc, 244, 164, rw + 32, rh + 32);
				XFlush(dpy);
				if (i == 0) {
					fprintf(stderr, "xresizer: rubber band %dx%d drawn\n", rw, rh);
					usleep(400000);
				}
			}
			fprintf(stderr, "xresizer: rubber band undone\n");
		}

		/* Scroll our own content with XCopyArea: a screen-source copy, the other
		 * op an interactive resize leans on. */
		if (do_copy != 0) {
			fprintf(stderr, "xresizer: XCopyArea scroll\n");
			XCopyArea(dpy, win, win, gc, 0, 40, w, h - 40, 0, 30);
			XFlush(dpy);
		}

		fprintf(stderr, "xresizer: STEP %d -> resize %dx%d (repaint=%d)\n",
				step, steps[step].w, steps[step].h, steps[step].repaint);
		if (steps[step].repaint == 0) {
			XSelectInput(dpy, win, StructureNotifyMask); /* drop Expose */
		}
		else {
			XSelectInput(dpy, win, ExposureMask | StructureNotifyMask);
		}
		XResizeWindow(dpy, win, (unsigned)steps[step].w, (unsigned)steps[step].h);
		XFlush(dpy);
		step++;
		next = now;
		next.tv_sec += period;
	}

	XCloseDisplay(dpy);
	return 0;
}
