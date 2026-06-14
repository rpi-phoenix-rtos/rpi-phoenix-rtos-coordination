/*
 * pl_phoenix_in.c — Phoenix input shim for Quakespasm: replaces in_sdl.c.
 *
 * Reads the USB keyboard via /dev/kbd0 and feeds Quake's input. The usbkbd driver
 * (phoenix-rtos-devices/tty/usbkbd) delivers a COOKED byte stream (like a TTY):
 *   - printable keys      -> their ASCII byte
 *   - Enter               -> '\n'      Backspace -> '\b'      Tab -> '\t'
 *   - Escape              -> '\033' (standalone)
 *   - arrows / nav keys   -> ANSI CSI escape sequences ("\033[A" = Up, etc.)
 * and ONLY on key-DOWN (there are no key-up reports). So we synthesize a Quake
 * down+up pair per byte. This is enough for menu navigation, the console, and
 * single-shot actions; sustained held-key movement (+forward held) needs real
 * key-up events from a richer kbd event interface (a future, attended USB change).
 *
 * Quake's two input entry points (mirroring in_sdl.c's SDL_KEYDOWN / SDL_TEXTINPUT):
 *   - Key_Event(quakekey, down)  -> bindings, menu nav, console control keys
 *   - Char_Event(ascii)          -> console / message text entry
 * We emit Key_Event for every key and additionally Char_Event for printables.
 *
 * KNOWN BLOCKER (needs an attended ownership decision): /dev/kbd0 is opened
 * EXCLUSIVELY by pl011-tty's pl011_kbdthr (sources/.../tty/pl011-tty/pl011-tty.c),
 * which bridges USB keystrokes into the serial console so you can type in psh.
 * usbkbd is single-opener, so our open() here returns EBUSY (errno 16) and input
 * stays disabled (logged; demos still render). To give Quake the keyboard, EITHER
 * make usbkbd multi-reader (per-client FIFOs), OR don't start the pl011-tty kbd
 * bridge on the Quake boot variant (PL011_TTY_KBD_PATH), OR arbitrate by foreground
 * app. The decoder below is hardware-independent and is self-tested at boot, so it
 * is ready the moment /dev/kbd0 becomes openable.
 */
#include "quakedef.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static int kbd_fd = -1;

/* Emit one decoded key. quakekey = Quake key code (K_* or lowercase ascii for
 * letter/printable keys); ch = the printable ascii to feed Char_Event, or 0. */
static void pl_in_emit(int quakekey, int ch)
{
	if (quakekey <= 0)
		return;
	Key_Event(quakekey, true);
	if (ch > 0 && ch < 128)
		Char_Event(ch);
	Key_Event(quakekey, false);
}

/*
 * Decode a cooked kbd byte buffer into Quake key events, dispatched through emit().
 * Factored out (emit as a parameter) so the escape-sequence decoding can be
 * unit-tested at boot without real hardware (see pl_in_selftest).
 */
static void pl_in_decode(const unsigned char *b, int n, void (*emit)(int, int))
{
	int i = 0;

	while (i < n) {
		unsigned char c = b[i];

		if (c == 0x1bu) {		/* ESC: standalone, or start of a CSI sequence */
			if (i + 1 < n && b[i + 1] == '[') {
				unsigned char k = (i + 2 < n) ? b[i + 2] : 0u;
				int key = 0;
				int seqlen = 3;	/* ESC '[' <final> */

				switch (k) {
				case 'A': key = K_UPARROW; break;
				case 'B': key = K_DOWNARROW; break;
				case 'C': key = K_RIGHTARROW; break;
				case 'D': key = K_LEFTARROW; break;
				case 'H': key = K_HOME; break;
				case 'F': key = K_END; break;
				/* "ESC [ <digit> ~" forms (driver: Del/PgDn/etc.) */
				case '3': key = K_DEL;  seqlen = 4; break;
				case '4': key = K_END;  seqlen = 4; break;
				case '5': key = K_PGUP; seqlen = 4; break;
				case '6': key = K_PGDN; seqlen = 4; break;
				default:  key = 0;      break;
				}
				emit(key, 0);
				i += seqlen;
				continue;
			}
			emit(K_ESCAPE, 0);
			i += 1;
			continue;
		}

		if (c == '\n' || c == '\r') { emit(K_ENTER, 0); i++; continue; }
		if (c == '\b' || c == 0x7fu) { emit(K_BACKSPACE, 0); i++; continue; }
		if (c == '\t') { emit(K_TAB, 0); i++; continue; }

		if (c >= 0x20u && c <= 0x7eu) {		/* printable: key=lowercase, char=as-typed */
			int key = c;
			if (key >= 'A' && key <= 'Z')
				key = key - 'A' + 'a';
			emit(key, c);
			i++;
			continue;
		}

		i++;	/* ignore other control bytes (raw ctrl chars, etc.) */
	}
}

/* --- boot-time self-test of the decoder (no hardware needed) --- */
static int       st_n;
static int       st_keys[16];
static void st_emit(int key, int ch) { (void)ch; if (st_n < 16) st_keys[st_n++] = key; }

static int st_check(const char *in, int len, const int *want, int wantn)
{
	int i;
	st_n = 0;
	pl_in_decode((const unsigned char *)in, len, st_emit);
	if (st_n != wantn)
		return 0;
	for (i = 0; i < wantn; i++)
		if (st_keys[i] != want[i])
			return 0;
	return 1;
}

static void pl_in_selftest(void)
{
	int pass = 0, total = 0;
	struct { const char *in; int len; int want[4]; int wantn; } cases[] = {
		{ "w",        1, { 'w' },                 1 },
		{ "AB",       2, { 'a', 'b' },            2 },	/* uppercase -> lowercase key */
		{ "\033",     1, { K_ESCAPE },            1 },
		{ "\033[A",   3, { K_UPARROW },           1 },
		{ "\033[B",   3, { K_DOWNARROW },         1 },
		{ "\033[C",   3, { K_RIGHTARROW },        1 },
		{ "\033[D",   3, { K_LEFTARROW },         1 },
		{ "\n",       1, { K_ENTER },             1 },
		{ "\b",       1, { K_BACKSPACE },         1 },
		{ "\033[3~",  4, { K_DEL },               1 },
		{ "a\033[A\n",5, { 'a', K_UPARROW, K_ENTER }, 3 },	/* mixed in one read */
	};
	int i, nc = (int)(sizeof(cases) / sizeof(cases[0]));

	for (i = 0; i < nc; i++) {
		total++;
		if (st_check(cases[i].in, cases[i].len, cases[i].want, cases[i].wantn))
			pass++;
		else
			Sys_Printf("PL_IN selftest FAIL case %d\n", i);
	}
	Sys_Printf("PL_IN: kbd decoder selftest %d/%d passed\n", pass, total);
}

void IN_Init(void)
{
	pl_in_selftest();

	kbd_fd = open("/dev/kbd0", O_RDONLY | O_NONBLOCK);
	if (kbd_fd < 0)
		Sys_Printf("PL_IN: /dev/kbd0 open failed (errno=%d) — keyboard input disabled\n", errno);
	else
		Sys_Printf("PL_IN: /dev/kbd0 opened (fd=%d) — keyboard input active\n", kbd_fd);
}

void IN_Shutdown(void)
{
	if (kbd_fd >= 0) {
		close(kbd_fd);
		kbd_fd = -1;
	}
}

void IN_Commands(void)
{
}

void IN_Move(usercmd_t *cmd)
{
	(void)cmd;
}

void IN_SendKeyEvents(void)
{
	unsigned char buf[64];
	ssize_t r;

	if (kbd_fd < 0)
		return;

	/* Drain whatever the cooked kbd FIFO has this frame (non-blocking). */
	while ((r = read(kbd_fd, buf, sizeof(buf))) > 0)
		pl_in_decode(buf, (int)r, pl_in_emit);
}

void IN_UpdateInputMode(void)
{
}

void IN_ClearStates(void)
{
}

void IN_Activate(void)
{
}

void IN_Deactivate(qboolean free_cursor)
{
	(void)free_cursor;
}

void IN_MouseMotion(int dx, int dy)
{
	(void)dx;
	(void)dy;
}
