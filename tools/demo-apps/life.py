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

    python3 /usr/share/demo/life.py [generations]

Keys: q quits, space pauses/resumes, r reseeds.
"""

import curses
import random
import sys
import time

# Live-cell glyph. A solid block reads clearly on both the HDMI framebuffer
# console and a scaled-down screen recording; '#' is the fallback if the
# terminal cannot encode it.
GLYPH = "#"

# Two gliders, injected on top of the random soup so there is always something
# travelling across the field rather than only local churn settling into
# still-lifes.
GLIDER = ((0, 1), (1, 2), (2, 0), (2, 1), (2, 2))


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
            grid = step(grid, rows, cols)
            gen += 1
        time.sleep(0.05)


def main():
    limit = 0
    if len(sys.argv) > 1:
        try:
            limit = int(sys.argv[1])
        except ValueError:
            print(f"usage: {sys.argv[0]} [generations]", file=sys.stderr)
            return 2
    curses.wrapper(run, limit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
