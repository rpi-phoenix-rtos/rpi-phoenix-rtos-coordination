/*
 * xfontprobe — minimal libX11-only font-loading probe for the Phoenix X11 port.
 *
 * Purpose (task #56, "named bitmap fonts don't load"): isolate WHICH layer fails
 * when an app requests a named disk font (e.g. "8x13"). It deliberately uses ONLY
 * libX11 + libphoenix — no Xt/Xaw, no resource converters, no pixmaps — so its
 * output is free of the client-side noise (and the xcalc double-free) that masked
 * the real failure in the earlier xcalc run.
 *
 * It connects to the server, prints the server's font path (XGetFontPath), then
 * runs XLoadQueryFont on a discriminator set chosen so the PASS/FAIL pattern
 * pinpoints the failing layer:
 *
 *   "fixed"   built-in alias  -> 6x13.builtin   (proves built-in FPE only)
 *   "6x13"    built-in        (proves built-in FPE only)
 *   "cursor"  built-in        (proves built-in FPE only)
 *   "8x13"    DISK, via fonts.alias -> XLFD -> 8x13-ISO8859-1.pcf.gz
 *   "10x20"   DISK, direct fonts.dir file name (no alias hop)
 *   "-misc-fixed-medium-r-normal--13-120-75-75-c-80-iso8859-1"  DISK, direct XLFD (no alias)
 *
 * Interpreting the result on HW:
 *   built-ins OK + every disk name NULL  -> server per-font disk load is broken
 *                                           (FontFileOpenBitmap/pcfReadFont path).
 *   all disk names OK                    -> font serving works; the headline bug is
 *                                           the client-side (xcalc/Xaw) double-free.
 *   alias ("8x13") NULL but direct XLFD OK (or vice-versa) -> alias resolution issue.
 *
 * Prints a single PASS/FAIL summary line per font and a final verdict, then exits 0
 * (0 even on font failures: a non-zero exit would be indistinguishable from a crash
 * on the UART). Degrades cleanly with a printed line + exit 0 if no server.
 *
 * Build: tools/x11-port/apps/build.sh (cross aarch64-phoenix, static).
 *
 * License: MIT.
 */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct probe {
	const char *name;
	int is_disk; /* 1 = can only come from a disk FPE; 0 = built-in */
};

static const struct probe probes[] = {
	{ "fixed", 0 },
	{ "6x13", 0 },
	{ "cursor", 0 },
	{ "8x13", 1 },
	{ "10x20", 1 },
	{ "-misc-fixed-medium-r-normal--13-120-75-75-c-80-iso8859-1", 1 },
};
#define NPROBES ((int)(sizeof(probes) / sizeof(probes[0])))

int main(void)
{
	Display *dpy;
	char **fp;
	int nfp = 0, i;
	int builtin_ok = 0, builtin_n = 0, disk_ok = 0, disk_n = 0;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		printf("xfontprobe: cannot open display (no server?) — exiting cleanly\n");
		fflush(stdout);
		return 0;
	}
	printf("xfontprobe: connected to display\n");

	/* What font path did the server actually end up with? */
	fp = XGetFontPath(dpy, &nfp);
	printf("xfontprobe: server font path has %d element(s):\n", nfp);
	for (i = 0; i < nfp; i++)
		printf("xfontprobe:   fp[%d] = %s\n", i, fp[i]);
	if (fp != NULL)
		XFreeFontPath(fp);

	for (i = 0; i < NPROBES; i++) {
		XFontStruct *fs = XLoadQueryFont(dpy, probes[i].name);
		const char *kind = probes[i].is_disk ? "disk   " : "builtin";
		int ok = (fs != NULL);

		printf("xfontprobe: [%s] %-58s -> %s\n", kind, probes[i].name,
			ok ? "PASS" : "FAIL (NULL)");
		fflush(stdout);

		if (probes[i].is_disk) {
			disk_n++;
			if (ok)
				disk_ok++;
		}
		else {
			builtin_n++;
			if (ok)
				builtin_ok++;
		}
		if (fs != NULL)
			XFreeFont(dpy, fs);
	}

	printf("xfontprobe: SUMMARY builtin=%d/%d disk=%d/%d\n",
		builtin_ok, builtin_n, disk_ok, disk_n);
	if (builtin_ok == builtin_n && disk_ok == 0)
		printf("xfontprobe: VERDICT server DISK font load is broken (per-font path)\n");
	else if (disk_ok == disk_n)
		printf("xfontprobe: VERDICT disk fonts LOAD — font serving works\n");
	else
		printf("xfontprobe: VERDICT partial — see per-font lines above\n");
	fflush(stdout);

	XCloseDisplay(dpy);
	return 0;
}
