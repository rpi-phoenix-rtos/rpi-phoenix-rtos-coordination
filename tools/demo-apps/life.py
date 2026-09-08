#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Conway's Game of Life, rendered with curses.

Why this is in the tree
-----------------------
It is a showcase piece for the Phoenix-RTOS Raspberry Pi 4 port. In one small
program it exercises CPython 3.14, the ncurses port and the terminal layer
(pl011 fbcon on HDMI, or xterm under X), and unlike a static banner it produces
continuous visible motion -- which is what a demo recording needs.

Deliberately dependency-free: standard library only, no numpy, no colours
required. It sizes itself to whatever terminal it is given and runs until the
generation limit or a keypress.

    python3 /usr/share/demo/life.py [generations] [--ansi] [--log PATH]

Keys (curses mode): q quits, space pauses/resumes, r reseeds.

Two renderers
-------------
curses is the default and gives the status bar and keys. But curses needs a
usable TERM and a terminfo entry, and the HDMI framebuffer console is not
guaranteed to supply either -- so there is a second renderer that writes plain
ANSI (cursor-home + erase) and needs nothing but a tty. It is used
automatically when curses cannot start, and can be forced with --ansi. A demo
that dies with `setupterm: could not find terminal` on the console it was meant
to run on is worse than one that draws with two escape sequences.
"""

import curses
import os
import random
import shutil
import sys
import time
import traceback

# --log PATH: append "gen <n> t <secs>" every LOG_EVERY generations, and the
# traceback if the loop dies. Exists because this program froze after ~80 s under
# X while the rest of the desktop stayed live (GoL canvas 0.09 % changed over 75 s
# vs 49.9 % for the GL window next to it), and there was no evidence to work
# from: a Python-level exception goes to the xterm's stderr, which nothing
# captures, and the UART log showed no fault. With a log on the NFS root the
# failure says where and when it stopped, and whether it raised.
LOG_EVERY = 20
_logf = None


def _log(msg):
    if _logf is None:
        return
    try:
        _logf.write(msg + "\n")
        _logf.flush()
        os.fsync(_logf.fileno())   # the interesting case is a process that stops
    except Exception:              # writing; an unflushed buffer would hide it
        pass

# Live-cell glyph. A solid block reads clearly on both the HDMI framebuffer
# console and a scaled-down screen recording; '#' is the fallback if the
# terminal cannot encode it.
GLYPH = "#"

# Two gliders, injected on top of the random soup so there is always something
# travelling across the field rather than only local churn settling into
# still-lifes.
GLIDER = ((0, 1), (1, 2), (2, 0), (2, 1), (2, 2))

# Conway's Life on a bounded field ALWAYS dies down: random soup collapses into
# still-lifes and period-2 oscillators, after which the screen stops changing.
# That is what looked like a hang -- the program was reported "frozen" after ~80 s
# under X, and its own log later showed it running happily at generation 3640
# with the population pinned at 76 since generation ~360. The simulation was fine;
# it had simply finished. A showcase needs continuous motion, so inject a fresh
# glider whenever the population has not moved for STALE_GENS generations.
STALE_GENS = 60


def inject_glider(grid, rows, cols):
    """Drop one glider at a random position, oriented so it travels into the
    field. Cheap (5 cells) and enough to restart local activity."""
    if rows < 6 or cols < 6:
        return
    base_r = random.randrange(1, rows - 4)
    base_c = random.randrange(1, cols - 4)
    for dr, dc in GLIDER:
        grid[base_r + dr][base_c + dc] = 1


def seed(rows, cols, density=0.22):
    """A random field with a couple of gliders placed in open space."""
    grid = [[1 if random.random() < density else 0 for _ in range(cols)]
            for _ in range(rows)]
    for base_r, base_c in ((2, 2), (rows // 2, cols // 2)):
        if base_r + 3 < rows and base_c + 3 < cols:
            for dr, dc in GLIDER:
                grid[base_r + dr][base_c + dc] = 1
    return grid


def step(grid, rows, cols):
    """One generation. Toroidal wrap, so patterns leave and re-enter rather
    than dying at a hard edge -- keeps the field active for a long recording."""
    new = [[0] * cols for _ in range(rows)]
    for r in range(rows):
        up, down = (r - 1) % rows, (r + 1) % rows
        row_u, row_c, row_d = grid[up], grid[r], grid[down]
        for c in range(cols):
            left, right = (c - 1) % cols, (c + 1) % cols
            n = (row_u[left] + row_u[c] + row_u[right]
                 + row_c[left] + row_c[right]
                 + row_d[left] + row_d[c] + row_d[right])
            # B3/S23
            new[r][c] = 1 if (n == 3 or (n == 2 and row_c[c])) else 0
    return new


def run(stdscr, limit):
    curses.curs_set(0)
    stdscr.nodelay(True)
    height, width = stdscr.getmaxyx()

    # Leave the bottom line for the status readout, and one column spare so a
    # write to the last cell cannot wrap and raise.
    rows, cols = max(1, height - 1), max(1, width - 1)
    grid = seed(rows, cols)
    gen = 0
    last_pop, stale = -1, 0
    paused = False
    started = time.time()

    while limit == 0 or gen < limit:
        key = stdscr.getch()
        if key in (ord("q"), ord("Q")):
            break
        if key == ord(" "):
            paused = not paused
        if key in (ord("r"), ord("R")):
            grid, gen = seed(rows, cols), 0

        population = 0
        for r in range(rows):
            row = grid[r]
            # Build the whole line then write once: far fewer curses calls than
            # addch per cell, which matters on a serial-backed console.
            line = "".join(GLYPH if v else " " for v in row)
            population += sum(row)
            try:
                stdscr.addstr(r, 0, line)
            except curses.error:
                pass

        rate = gen / max(1e-6, time.time() - started)
        status = (f" Game of Life on Phoenix-RTOS  |  {cols}x{rows}  "
                  f"gen {gen}  pop {population}  {rate:4.1f} gen/s  "
                  f"[q]uit [space]pause [r]eseed ")
        try:
            stdscr.addstr(height - 1, 0, status[:width - 1], curses.A_REVERSE)
        except curses.error:
            pass

        stdscr.refresh()
        if not paused:
            if population == last_pop:
                stale += 1
                if stale >= STALE_GENS:
                    inject_glider(grid, rows, cols)
                    stale = 0
                    _log(f"curses gen {gen} population static at {population}"
                         f" for {STALE_GENS} gens — injected a glider")
            else:
                stale = 0
            last_pop = population
            grid = step(grid, rows, cols)
            gen += 1
            if gen % LOG_EVERY == 0:
                _log(f"curses gen {gen} pop {population} t {time.time() - started:.1f}")
        time.sleep(0.05)


def run_ansi(limit):
    """Plain-ANSI renderer: cursor-home + erase-down each frame, no terminfo, no
    input handling (the generation limit is the exit). Deliberately minimal so it
    works on any tty, including the pl011 fbcon console."""
    try:
        cols_t, rows_t = shutil.get_terminal_size(fallback=(100, 30))
    except Exception:
        cols_t, rows_t = 100, 30
    # Leave the last line for the status bar, and keep a column of slack so a
    # terminal that wraps on the final glyph does not scroll the field away.
    rows = max(8, rows_t - 2)
    cols = max(16, cols_t - 1)

    grid = seed(rows, cols)
    gen = 0
    last_pop, stale = -1, 0
    started = time.time()
    out = sys.stdout
    out.write("\033[2J")            # erase once; afterwards just redraw in place

    while limit == 0 or gen < limit:
        population = 0
        lines = []
        for r in range(rows):
            row = grid[r]
            population += sum(row)
            lines.append("".join(GLYPH if v else " " for v in row))

        rate = gen / max(1e-6, time.time() - started)
        status = (f" Game of Life on Phoenix-RTOS  |  {cols}x{rows}  "
                  f"gen {gen}  pop {population}  {rate:4.1f} gen/s ")
        # \033[H homes the cursor; \033[J erases from there down, so the frame is
        # replaced rather than scrolled.
        out.write("\033[H\033[J" + "\n".join(lines) + "\n" + status[:cols])
        out.flush()

        if population == last_pop:
            stale += 1
            if stale >= STALE_GENS:
                inject_glider(grid, rows, cols)
                stale = 0
        else:
            stale = 0
        last_pop = population
        grid = step(grid, rows, cols)
        gen += 1
        if gen % LOG_EVERY == 0:
            _log(f"ansi gen {gen} pop {population} t {time.time() - started:.1f}")
        time.sleep(0.05)

    out.write("\n")
    out.flush()


def main():
    global _logf
    limit = 0
    force_ansi = False
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--ansi":
            force_ansi = True
        elif a == "--log" and i + 1 < len(args):
            i += 1
            try:
                _logf = open(args[i], "a", buffering=1)
            except OSError as exc:
                print(f"life: cannot open log {args[i]}: {exc}", file=sys.stderr)
        else:
            try:
                limit = int(a)
            except ValueError:
                print(f"usage: {sys.argv[0]} [generations] [--ansi] [--log PATH]",
                      file=sys.stderr)
                return 2
        i += 1

    _log(f"start pid {os.getpid()} limit {limit} ansi {force_ansi} "
         f"term {os.environ.get('TERM', '<unset>')}")

    if force_ansi:
        try:
            run_ansi(limit)
        except BaseException:
            _log("ansi renderer raised:\n" + traceback.format_exc())
            raise
        _log("ansi renderer returned normally")
        return 0

    try:
        curses.wrapper(run, limit)
        _log("curses renderer returned normally")
    except Exception as exc:
        _log("curses renderer raised:\n" + traceback.format_exc())
        # curses could not start (no TERM, no terminfo, not a tty it recognises).
        # Say so once and draw anyway -- on a demo machine, falling back beats
        # exiting with a traceback.
        print(f"life: curses unavailable ({exc}); falling back to plain ANSI",
              file=sys.stderr)
        run_ansi(limit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
