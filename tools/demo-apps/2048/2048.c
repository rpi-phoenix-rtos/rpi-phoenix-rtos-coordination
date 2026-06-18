/*
 * 2048 — a tiny terminal demo game for Phoenix-RTOS (Raspberry Pi 4 port).
 *
 * A self-contained VT100/ANSI implementation of the 2048 sliding-tile game,
 * written to exercise nothing but a stdio terminal: it is the Tier-D "visual
 * flourish" demo from docs/todo/userspace-demo-apps.md. Controls are WASD or
 * the arrow keys; 'q' quits. Randomness is seeded from /dev/urandom (the
 * Phoenix Pi 4 hwrng-backed entropy source) with a deterministic fallback.
 *
 * Builds for aarch64-phoenix with the cross toolchain (see build.sh) and runs
 * from the NFS/SD rootfs like the other demo binaries (lua, busybox, ...).
 *
 * License: MIT. Author: Phoenix-RTOS Pi 4 bring-up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define N 4

static unsigned int g_state = 0x12345678u;

/* xorshift32 — small, dependency-free PRNG; seeded from /dev/urandom below. */
static unsigned int rng_next(void)
{
	unsigned int x = g_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g_state = x;
	return x;
}

static void rng_seed(void)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		unsigned int s = 0;
		if (read(fd, &s, sizeof(s)) == (ssize_t)sizeof(s) && s != 0) {
			g_state = s;
		}
		close(fd);
	}
	/* Mix in the pid so two launches on a no-RTC system still differ. */
	g_state ^= (unsigned int)(getpid() * 2654435761u);
	if (g_state == 0) {
		g_state = 0x9e3779b9u;
	}
}

static int board[N][N];
static unsigned long score;

static void spawn(void)
{
	int empties[N * N], n = 0, i, j;

	for (i = 0; i < N; i++) {
		for (j = 0; j < N; j++) {
			if (board[i][j] == 0) {
				empties[n++] = i * N + j;
			}
		}
	}
	if (n == 0) {
		return;
	}
	int cell = empties[rng_next() % (unsigned int)n];
	board[cell / N][cell % N] = (rng_next() % 10u == 0u) ? 4 : 2;
}

/* ANSI colour per tile value, for a readable board on a colour terminal. */
static const char *tile_color(int v)
{
	switch (v) {
		case 2:    return "\033[97;100m";
		case 4:    return "\033[97;44m";
		case 8:    return "\033[30;46m";
		case 16:   return "\033[30;42m";
		case 32:   return "\033[30;43m";
		case 64:   return "\033[30;45m";
		case 128:  return "\033[30;41m";
		case 256:  return "\033[97;41m";
		case 512:  return "\033[97;45m";
		case 1024: return "\033[97;43m";
		default:   return "\033[30;107m"; /* 2048+ */
	}
}

static void draw(void)
{
	int i, j;

	fputs("\033[2J\033[H", stdout); /* clear + home */
	printf("  2048 on Phoenix-RTOS   score: %lu\r\n", score);
	printf("  (w/a/s/d or arrows; q to quit)\r\n\r\n");
	for (i = 0; i < N; i++) {
		fputs("  ", stdout);
		for (j = 0; j < N; j++) {
			if (board[i][j] == 0) {
				fputs("\033[2m[    ]\033[0m", stdout);
			}
			else {
				printf("%s%5d \033[0m", tile_color(board[i][j]), board[i][j]);
			}
		}
		fputs("\r\n\r\n", stdout);
	}
	fflush(stdout);
}

/*
 * Slide+merge one row to the left (the canonical 2048 line operation).
 * All four directions are expressed as this single op over transposed/
 * reversed copies. Returns non-zero if the row changed.
 */
static int slide_row(int *row)
{
	int tmp[N], k = 0, i, moved = 0;

	for (i = 0; i < N; i++) {
		tmp[i] = 0;
	}
	for (i = 0; i < N; i++) {
		if (row[i] != 0) {
			tmp[k++] = row[i];
		}
	}
	for (i = 0; i < N - 1; i++) {
		if (tmp[i] != 0 && tmp[i] == tmp[i + 1]) {
			tmp[i] *= 2;
			score += (unsigned long)tmp[i];
			tmp[i + 1] = 0;
		}
	}
	int out[N];
	for (i = 0; i < N; i++) {
		out[i] = 0;
	}
	k = 0;
	for (i = 0; i < N; i++) {
		if (tmp[i] != 0) {
			out[k++] = tmp[i];
		}
	}
	for (i = 0; i < N; i++) {
		if (row[i] != out[i]) {
			moved = 1;
		}
		row[i] = out[i];
	}
	return moved;
}

/* dir: 0=left 1=right 2=up 3=down. Returns non-zero if anything moved. */
static int move_board(int dir)
{
	int i, j, moved = 0, row[N];

	for (i = 0; i < N; i++) {
		for (j = 0; j < N; j++) {
			switch (dir) {
				case 0: row[j] = board[i][j]; break;
				case 1: row[j] = board[i][N - 1 - j]; break;
				case 2: row[j] = board[j][i]; break;
				case 3: row[j] = board[N - 1 - j][i]; break;
			}
		}
		if (slide_row(row)) {
			moved = 1;
		}
		for (j = 0; j < N; j++) {
			switch (dir) {
				case 0: board[i][j] = row[j]; break;
				case 1: board[i][N - 1 - j] = row[j]; break;
				case 2: board[j][i] = row[j]; break;
				case 3: board[N - 1 - j][i] = row[j]; break;
			}
		}
	}
	return moved;
}

static int has_moves(void)
{
	int i, j;

	for (i = 0; i < N; i++) {
		for (j = 0; j < N; j++) {
			if (board[i][j] == 0) {
				return 1;
			}
			if (j < N - 1 && board[i][j] == board[i][j + 1]) {
				return 1;
			}
			if (i < N - 1 && board[i][j] == board[i + 1][j]) {
				return 1;
			}
		}
	}
	return 0;
}

static int reached_2048(void)
{
	int i, j;

	for (i = 0; i < N; i++) {
		for (j = 0; j < N; j++) {
			if (board[i][j] >= 2048) {
				return 1;
			}
		}
	}
	return 0;
}

/*
 * Read one move. Returns 0=left 1=right 2=up 3=down, -1=quit, -2=ignore.
 * Handles both WASD and the ESC [ A/B/C/D arrow sequences. Works whether the
 * terminal hands us bytes immediately or line-buffered (a trailing newline
 * just maps to "ignore").
 */
static int read_move(void)
{
	int c = getchar();

	if (c == EOF || c == 'q' || c == 'Q') {
		return -1;
	}
	if (c == 0x1b) { /* ESC — maybe an arrow sequence */
		int c2 = getchar();
		if (c2 != '[') {
			return -2;
		}
		switch (getchar()) {
			case 'A': return 2; /* up */
			case 'B': return 3; /* down */
			case 'C': return 1; /* right */
			case 'D': return 0; /* left */
			default:  return -2;
		}
	}
	switch (c) {
		case 'a': case 'A': return 0;
		case 'd': case 'D': return 1;
		case 'w': case 'W': return 2;
		case 's': case 'S': return 3;
		default:  return -2;
	}
}

int main(void)
{
	rng_seed();
	memset(board, 0, sizeof(board));
	score = 0;
	spawn();
	spawn();

	for (;;) {
		draw();
		if (reached_2048()) {
			printf("  *** You reached 2048! Keep going or press q. ***\r\n");
			fflush(stdout);
		}
		if (!has_moves()) {
			printf("  *** Game over. Final score: %lu ***\r\n", score);
			fflush(stdout);
			return 0;
		}
		int dir = read_move();
		if (dir == -1) {
			printf("\r\n  Bye! Score: %lu\r\n", score);
			fflush(stdout);
			return 0;
		}
		if (dir < 0) {
			continue;
		}
		if (move_board(dir)) {
			spawn();
		}
	}
}
