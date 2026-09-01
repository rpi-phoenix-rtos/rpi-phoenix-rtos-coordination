/*
 * Phoenix-RTOS RPi4 — ncurses port HW smoke (C layer)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Validates that the ncurses framework port (static libncurses.a with
 * --with-fallbacks terminfo) actually drives the Pi console at runtime — the
 * port is otherwise only build-verified. This is the C half of the curses
 * bring-up: it isolates "does ncurses run on the pl011/fbcon tty via the
 * compiled-in vt100 fallback" from all the CPython/.so machinery.
 *
 * curses escape output garbles the UART, so the verdict markers are printed to
 * stderr AFTER endwin() restores the terminal.
 */
#include <curses.h>
#include <term.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	WINDOW *w;
	int rows, cols, rc_setup = 0;
	const char *term;

	/* vt100 is in the port's compiled-in fallback set; set it in-process so we
	 * do not depend on the shell exporting TERM. */
	setenv("TERM", "vt100", 1);
	term = getenv("TERM");

	fprintf(stderr, "CURSES-C-BEGIN TERM=%s\n", term ? term : "(null)");

	/* setupterm first: a clean, non-screen-clobbering probe of terminfo. */
	if (setupterm(NULL, 1 /*stdout*/, NULL) != 0 /*OK==0*/) {
		fprintf(stderr, "CURSES-C-FAIL setupterm failed (terminfo fallback not resolved)\n");
		return 1;
	}
	rc_setup = 1;

	w = initscr();
	if (w == NULL) {
		fprintf(stderr, "CURSES-C-FAIL initscr returned NULL (setupterm ok=%d)\n", rc_setup);
		return 1;
	}

	rows = LINES;
	cols = COLS;

	/* exercise the drawing path */
	(void)cbreak();
	(void)noecho();
	(void)clear();
	(void)mvaddstr(0, 0, "Phoenix ncurses smoke");
	(void)mvaddstr(1, 0, "line two");
	(void)refresh();

	/* restore the terminal BEFORE printing the verdict so the marker is clean */
	(void)endwin();

	fprintf(stderr, "CURSES-C: screen %dx%d (LINES x COLS)\n", rows, cols);
	if ((rows > 0) && (cols > 0)) {
		fprintf(stderr, "CURSES-C-PASS ncurses initscr/draw/endwin OK on the Pi console\n");
		return 0;
	}
	fprintf(stderr, "CURSES-C-FAIL degenerate screen size %dx%d\n", rows, cols);
	return 1;
}
