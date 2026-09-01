/*
 * Phoenix-RTOS — force-include shim for building CPython's _curses against the
 * NARROW ncurses port (libncurses.a).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The Phoenix cross-build's pyconfig.h falsely reports HAVE_NCURSESW=1 (a bad
 * configure probe — the target has no ncursesw), so _cursesmodule.c compiles
 * the wide-char paths (setcchar/wadd_wch/wget_wch/…) that narrow libncurses.a
 * does not provide. A command-line -UHAVE_NCURSESW cannot fix this: pyconfig.h
 * re-#defines it when Python.h is included.
 *
 * Compile with `-include curses_shim.h`: this pulls in Python.h (and thus
 * pyconfig.h with its include guard) FIRST, then undefs the false macro. The
 * module's own later `#include "Python.h"` is a guarded no-op, so the undef
 * sticks and _cursesmodule.c's `#ifdef HAVE_NCURSESW` narrow-build guards take
 * the narrow path. Pair with -DHAVE_NCURSES_H so py_curses.h includes
 * <ncurses.h> (pyconfig leaves HAVE_NCURSES_H unset, but only as a comment, so
 * a command-line define persists).
 */
#include <Python.h>
#undef HAVE_NCURSESW
