# Phoenix-RTOS RPi4 — CPython curses smoke (Python layer)
# SPDX-License-Identifier: BSD-3-Clause
#
# Proves the _curses extension (dlopen'd .so, linked against the PIC ncurses
# port) works end-to-end on the Pi console. curses escape output garbles the
# UART, so curses.wrapper() restores the terminal (endwin) before the final
# verdict markers print.
import sys, os

print("PYVER", sys.version.split()[0])

# vt100 is in the ncurses port's compiled-in fallback set; set it before initscr.
os.environ["TERM"] = "vt100"

try:
    import _curses
    print("IMPORT-CURSES-OK")
except Exception as e:
    print("IMPORT-CURSES-FAIL", repr(e))
    sys.exit(1)

import curses

print("CURSES-BEGIN")


def _body(stdscr):
    stdscr.addstr(0, 0, "Phoenix python curses")
    stdscr.addstr(1, 0, "line two")
    stdscr.refresh()
    return stdscr.getmaxyx()


try:
    rows, cols = curses.wrapper(_body)   # sets up + tears down (endwin) cleanly
    print("CURSES-SIZE %dx%d" % (rows, cols))
    if rows > 0 and cols > 0:
        print("CURSES-PY-PASS")
    else:
        print("CURSES-PY-FAIL degenerate size")
        sys.exit(1)
except Exception as e:
    print("CURSES-PY-FAIL", repr(e))
    sys.exit(1)
