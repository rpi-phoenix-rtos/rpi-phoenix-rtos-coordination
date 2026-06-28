/*
 * Phoenix-RTOS — xinit-style launcher for the Xphoenix kdrive fbdev server.
 *
 * Phoenix psh runs ONE foreground command and cannot background (`&` is a
 * redirect, not job control), so the X server and an X client cannot be
 * started from a single psh session. This launcher is the single foreground
 * binary psh runs: it forks the server, waits for the server's listening
 * socket to appear, then forks the client with DISPLAY=:0 in its environment,
 * and blocks waiting on both children. If the server dies, the client is
 * killed and the launcher exits.
 *
 * All paths are taken from argv so the same binary works on any variant
 * (e.g. NFS export mounted at /nfstest):
 *
 *   pl_phoenix_xlaunch <Xphoenix-path> <fontdir> <client-path> [client-args...]
 *
 * e.g. on netboot/NFS:
 *   pl_phoenix_xlaunch /nfstest/bin/Xphoenix \
 *                      /nfstest/usr/share/fonts/X11/misc \
 *                      /nfstest/bin/xeyes
 *
 * The server is always launched as ":0" with "-ac" (disable access control;
 * a local client has no xauth cookie) and "-nolisten tcp".
 *
 * MULTI-CLIENT / DESKTOP MODE. The launcher can also bring up a window manager
 * AND one or more client apps in a single session — useful so the screen shows
 * a managed, decorated window the user can drag, rather than a bare black root.
 * In "startx" convenience mode (argc < 4) the special client name `desktop`
 * expands to the list [twm, xeyes]: twm comes up as the window manager first,
 * then xeyes launches as a managed (titlebar-decorated, draggable) window. The
 * server is brought up first in every mode; the clients are forked after the
 * listening socket appears. The supervisor keeps the server (and the other
 * clients) alive when any single client exits.
 *
 * SPAWN IDIOM: this mirrors the proven Phoenix pattern (psh/pshapp.c,
 * psh/runfile.c): vfork() + exec*() + waitpid(). Under vfork() the child
 * shares the parent's address space until exec, so the child does NOTHING
 * but exec (and _exit on failure) — in particular it never calls setenv(),
 * which would corrupt the parent. The client's environment (with DISPLAY=:0
 * appended) is therefore built in the PARENT before forking and passed via
 * execve().
 *
 * Host-side build only (build-xlaunch.sh). Links against libc only.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

/* The kdrive server, invoked as ":0", creates this AF_UNIX listening socket
 * once it is past framebuffer + XKB init and entering its dispatch loop. We
 * poll for it as the "server is ready" signal. */
#define DISPLAY_NUM       "0"
#define X_SOCKET_DIR      "/tmp/.X11-unix"
#define X_SOCKET_PATH     X_SOCKET_DIR "/X" DISPLAY_NUM
#define DISPLAY_VALUE     ":" DISPLAY_NUM

/* Bounded readiness poll: ~10 ms per tick, ~10 s total. */
#define POLL_INTERVAL_MS  10
#define POLL_TIMEOUT_MS   10000
#define POLL_MAX_TICKS    (POLL_TIMEOUT_MS / POLL_INTERVAL_MS)

/* Most clients we ever launch in one session (window manager + apps). */
#define MAX_CLIENTS       4

extern char **environ;

static void msleep(long ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	(void)nanosleep(&ts, NULL);
}

/* Build a NULL-terminated argv: prog + the trailing args verbatim. */
static char **build_argv(char *prog, char *const extra[], int nextra)
{
	char **argv = malloc((size_t)(nextra + 2) * sizeof(char *));
	int i;

	if (argv == NULL)
		return NULL;

	argv[0] = prog;
	for (i = 0; i < nextra; i++)
		argv[i + 1] = extra[i];
	argv[nextra + 1] = NULL;

	return argv;
}

/* Copy environ, override DISPLAY=:0, and supply HOME/PATH defaults when the
 * inherited environment lacks them. psh exports neither HOME nor PATH, but X
 * clients need both: a window manager (e.g. Window Maker) writes its per-user
 * state under $HOME (~/GNUstep) and PATH-searches for helper programs (wmsetbg,
 * menu apps). Defaults are derived from the install prefix ("" => "/" on the
 * nfsroot default and sd; the legacy /nfstest subtree mount otherwise): HOME=<prefix>/root,
 * PATH=<prefix>/bin:/bin. An inherited HOME/PATH (if psh ever sets one) wins.
 * Built in the parent so the vfork'd child never touches the shared address
 * space beyond exec. */
static char **build_client_env(const char *prefix)
{
	static char display_var[] = "DISPLAY=" DISPLAY_VALUE;
	static char home_var[256];
	static char path_var[256];
	int n = 0, i, j, have_home = 0, have_path = 0;
	char **env;

	for (i = 0; environ[i] != NULL; i++)
		n++;

	/* worst case: all inherited vars + DISPLAY + HOME + PATH + NULL */
	env = malloc((size_t)(n + 4) * sizeof(char *));
	if (env == NULL)
		return NULL;

	j = 0;
	for (i = 0; i < n; i++) {
		if (strncmp(environ[i], "DISPLAY=", 8) == 0)
			continue; /* drop inherited DISPLAY, we set our own */
		if (strncmp(environ[i], "HOME=", 5) == 0)
			have_home = 1;
		if (strncmp(environ[i], "PATH=", 5) == 0)
			have_path = 1;
		env[j++] = environ[i];
	}
	env[j++] = display_var;
	if (have_home == 0) {
		snprintf(home_var, sizeof(home_var), "HOME=%s/root", prefix);
		env[j++] = home_var;
	}
	if (have_path == 0) {
		snprintf(path_var, sizeof(path_var), "PATH=%s/bin:/bin", prefix);
		env[j++] = path_var;
	}
	env[j] = NULL;

	return env;
}

/* Resolve a client name to a path under cp_buf: absolute names pass through,
 * bare names are taken relative to <prefix>/bin. */
static void
resolve_client(char *cp_buf, size_t cp_sz, const char *prefix, const char *client)
{
	if (client[0] == '/')
		snprintf(cp_buf, cp_sz, "%s", client);
	else
		snprintf(cp_buf, cp_sz, "%s/bin/%s", prefix, client);
}

int main(int argc, char *argv[])
{
	const char *server_path, *font_dir;
	/* volatile: these are live across the vfork() calls below. The children
	 * only execve/_exit (never write parent memory), so no real clobber can
	 * occur, but at -O2 GCC inlines the env/argv builders into main() and its
	 * vfork-clobber analysis can't prove that — volatile silences the
	 * -Wclobbered warning and documents the constraint for upstreaming. */
	char **server_argv;
	char ** volatile client_env;
	char *server_extra[6];
	pid_t srv_pid;
	pid_t w;
	int status, tick;
	/* volatile: live across the vfork() calls below (see server_argv note).
	 * The children only execve/_exit, so no real clobber occurs, but -O2
	 * vfork-clobber analysis can't prove it — volatile silences -Wclobbered. */
	volatile int c;
	volatile int n_clients = 0;
	static char sp_buf[256], fd_buf[256];
	/* Install prefix used to derive client HOME/PATH defaults (see
	 * build_client_env). "" => root install; set to the auto-detected prefix
	 * in startx convenience mode below. */
	const char *client_prefix = "";

	/* The session's clients (window manager + apps). Each entry has its own
	 * resolved path, its own pre-built argv (vfork-safe), and a live pid that
	 * the supervisor watches. The DISPLAY=:0 environment is shared by all. */
	static char cp_bufs[MAX_CLIENTS][256];
	char ** volatile client_argv[MAX_CLIENTS] = { 0 };
	volatile pid_t cli_pid[MAX_CLIENTS];
	const char *client_path[MAX_CLIENTS];
	char *const *client_extra[MAX_CLIENTS] = { 0 };
	int n_client_extra[MAX_CLIENTS] = { 0 };

	for (c = 0; c < MAX_CLIENTS; c++)
		cli_pid[c] = -1;

	/* Make the whole X session visible on the console. psh wires a launched
	 * program's stdout (fd 1) to the console but NOT its stderr (fd 2), so every
	 * diagnostic below — and, crucially, the forked server's and clients' own
	 * stderr (kdrive/X write their logs there) — would otherwise vanish, leaving
	 * `startx` looking like it "did nothing". Point fd 2 at fd 1 here, in the
	 * parent, before any fork: the children inherit the redirected fd table, so
	 * the server banner, socket-wait status, and any client error all reach the
	 * UART. Unbuffer stdout so nothing is lost if a child dies mid-init. */
	dup2(STDOUT_FILENO, STDERR_FILENO);
	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc >= 4) {
		/* Explicit form: <Xphoenix> <fontdir> <client> [client-args...] */
		server_path = argv[1];
		font_dir    = argv[2];
		client_path[0]    = argv[3];
		client_extra[0]   = &argv[4];
		n_client_extra[0] = argc - 4;
		n_clients = 1;
	}
	else {
		/* "startx" convenience: 0 or 1 args. Auto-detect the install prefix
		 * ("/" on the nfsroot default and sd; else the legacy /nfstest subtree
		 * mount) and default the client to xeyes. Optional argv[1] picks the client
		 * (bare name under <prefix>/bin, or an absolute path), with one
		 * reserved name `desktop` that brings up a window manager + an app:
		 *   startx           -> <prefix>/bin/xeyes
		 *   startx twm       -> <prefix>/bin/twm        (WM only, bare root)
		 *   startx /bin/2048 -> /bin/2048
		 *   startx desktop   -> twm (WM) + xeyes (managed window)
		 *   startx term      -> twm (WM) + xterm (managed terminal window)
		 */
		const char *prefix = ""; /* root install (nfsroot default / sd) */
		const char *client = (argc >= 2) ? argv[1] : "xeyes";
		struct stat stx;

		snprintf(sp_buf, sizeof(sp_buf), "%s/bin/Xphoenix", prefix);
		if (stat(sp_buf, &stx) != 0) {
			prefix = "/nfstest"; /* legacy netboot subtree-mount fallback */
			snprintf(sp_buf, sizeof(sp_buf), "%s/bin/Xphoenix", prefix);
		}
		snprintf(fd_buf, sizeof(fd_buf), "%s/usr/share/fonts/X11/misc", prefix);

		if (strcmp(client, "desktop") == 0) {
			/* WM first so it adopts the app's window when it maps; then the
			 * app comes up as a managed, decorated, draggable window.
			 *
			 * xeyes is given an explicit -geometry so twm AUTO-PLACES it. The
			 * compiled-in twm config has no RandomPlacement, so a window that
			 * carries no position hint triggers twm's INTERACTIVE placement (a
			 * rubber-band outline that follows the pointer until the user
			 * clicks to drop it). A geometry supplies a USPosition hint, which
			 * twm honours by placing the window immediately — so the user sees
			 * a decorated xeyes at once rather than an outline they must click. */
			static char *const xeyes_geom[2] = { "-geometry", "300x200+360+240" };
			resolve_client(cp_bufs[0], sizeof(cp_bufs[0]), prefix, "twm");
			resolve_client(cp_bufs[1], sizeof(cp_bufs[1]), prefix, "xeyes");
			client_path[0] = cp_bufs[0];
			client_path[1] = cp_bufs[1];
			client_extra[1] = xeyes_geom;
			n_client_extra[1] = 2;
			n_clients = 2;
		}
		else if (strcmp(client, "term") == 0) {
			/* twm (WM) + xterm (managed terminal). The window manager is
			 * essential here, not cosmetic: with no WM the server uses
			 * PointerRoot focus, so keystrokes go to whatever window the
			 * pointer happens to be over — making a keyboard test
			 * non-deterministic. twm gives xterm a titlebar and (with the
			 * compiled-in config) click-to-focus, so typed keys reliably
			 * reach the shell running inside xterm. A -geometry supplies a
			 * USPosition hint so twm places the window immediately (see the
			 * desktop-mode note) instead of an interactive rubber-band. */
			static char *const xterm_geom[2] = { "-geometry", "80x24+48+48" };
			resolve_client(cp_bufs[0], sizeof(cp_bufs[0]), prefix, "twm");
			resolve_client(cp_bufs[1], sizeof(cp_bufs[1]), prefix, "xterm");
			client_path[0] = cp_bufs[0];
			client_path[1] = cp_bufs[1];
			client_extra[1] = xterm_geom;
			n_client_extra[1] = 2;
			n_clients = 2;
		}
		else {
			resolve_client(cp_bufs[0], sizeof(cp_bufs[0]), prefix, client);
			client_path[0] = cp_bufs[0];
			n_clients = 1;
		}

		server_path = sp_buf;
		font_dir    = fd_buf;
		client_prefix = prefix; /* HOME/PATH defaults track the install prefix */
		fprintf(stderr, "xlaunch: startx mode — prefix=%s client=%s (%d client%s)\n",
			prefix[0] ? prefix : "/", client, n_clients,
			n_clients == 1 ? "" : "s");
	}

	/* The X server + clients need /tmp (for the .xkm + the listening socket).
	 * Create /tmp and /tmp/.X11-unix up front; EEXIST is fine. */
	if (mkdir("/tmp", 0777) != 0 && errno != EEXIST)
		fprintf(stderr, "xlaunch: warning: mkdir /tmp: %s\n", strerror(errno));
	if (mkdir(X_SOCKET_DIR, 0777) != 0 && errno != EEXIST)
		fprintf(stderr, "xlaunch: warning: mkdir %s: %s\n", X_SOCKET_DIR, strerror(errno));

	/* Build the server argv:
	 *   Xphoenix :0 -ac -nolisten tcp -fp <fontdir>
	 * -ac: disable access control (local client has no xauth cookie).
	 * -nolisten tcp: a failed TCP bind can never matter (we use the unix socket). */
	server_extra[0] = (char *)DISPLAY_VALUE;
	server_extra[1] = "-ac";
	server_extra[2] = "-nolisten";
	server_extra[3] = "tcp";
	server_extra[4] = "-fp";
	server_extra[5] = (char *)font_dir;
	server_argv = build_argv((char *)server_path, server_extra, 6);
	if (server_argv == NULL) {
		fprintf(stderr, "xlaunch: out of memory building server argv\n");
		return EXIT_FAILURE;
	}

	/* Build each client's argv (prog + any trailing args) and the shared client
	 * environment (with DISPLAY=:0) BEFORE forking — vfork-safe. */
	for (c = 0; c < n_clients; c++) {
		client_argv[c] = build_argv((char *)client_path[c],
			client_extra[c], n_client_extra[c]);
		if (client_argv[c] == NULL) {
			fprintf(stderr, "xlaunch: out of memory building client argv\n");
			return EXIT_FAILURE;
		}
	}
	client_env = build_client_env(client_prefix);
	if (client_env == NULL) {
		fprintf(stderr, "xlaunch: out of memory building client env\n");
		return EXIT_FAILURE;
	}

	/* --- fork the X server --- */
	fprintf(stderr, "xlaunch: starting server: %s %s -ac -nolisten tcp -fp %s\n",
		server_path, DISPLAY_VALUE, font_dir);
	srv_pid = vfork();
	if (srv_pid < 0) {
		fprintf(stderr, "xlaunch: vfork (server) failed: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}
	if (srv_pid == 0) {
		/* child: exec only — shares parent address space under vfork */
		execve(server_path, server_argv, environ);
		_exit(127);
	}

	/* --- wait for the server's listening socket, watching for early death --- */
	fprintf(stderr, "xlaunch: waiting for %s (server pid %d)\n", X_SOCKET_PATH, (int)srv_pid);
	for (tick = 0; tick < POLL_MAX_TICKS; tick++) {
		struct stat st;

		/* server already exited? abort early with its status */
		w = waitpid(srv_pid, &status, WNOHANG);
		if (w == srv_pid) {
			fprintf(stderr, "xlaunch: server exited during init (status=0x%x) — aborting\n",
				(unsigned)status);
			return EXIT_FAILURE;
		}

		if (stat(X_SOCKET_PATH, &st) == 0) {
			fprintf(stderr, "xlaunch: server socket present after ~%d ms\n",
				tick * POLL_INTERVAL_MS);
			break;
		}

		msleep(POLL_INTERVAL_MS);
	}
	if (tick >= POLL_MAX_TICKS) {
		/* The server reaching its dispatch loop is HW-proven, so proceed
		 * best-effort even if the socket node didn't appear in time. */
		fprintf(stderr, "xlaunch: warning: server socket %s not seen within %d ms; "
			"launching client anyway\n", X_SOCKET_PATH, POLL_TIMEOUT_MS);
	}

	/* --- fork the clients with DISPLAY=:0, in order --- */
	for (c = 0; c < n_clients; c++) {
		fprintf(stderr, "xlaunch: starting client[%d]: %s (DISPLAY=%s)\n",
			c, client_path[c], DISPLAY_VALUE);
		cli_pid[c] = vfork();
		if (cli_pid[c] < 0) {
			fprintf(stderr, "xlaunch: vfork (client[%d]) failed: %s\n",
				c, strerror(errno));
			cli_pid[c] = -1;
			continue; /* a failed client must not abort the session */
		}
		if (cli_pid[c] == 0) {
			/* child: exec only */
			execve(client_path[c], client_argv[c], client_env);
			_exit(127);
		}
		/* When launching a window manager + an app together, let the WM
		 * settle into its dispatch loop before the app maps its window, so
		 * the WM is ready to reparent/decorate it. Cheap, only between
		 * multiple clients. */
		if (n_clients > 1 && c + 1 < n_clients)
			msleep(250);
	}

	/* --- supervise: block in waitpid; server death kills all clients --- */
	for (;;) {
		w = waitpid(-1, &status, 0);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "xlaunch: waitpid failed: %s\n", strerror(errno));
			break;
		}

		if (w == srv_pid) {
			fprintf(stderr, "xlaunch: server exited (status=0x%x) — killing clients\n",
				(unsigned)status);
			/* Only signal live clients; kill(-1,...) would broadcast. */
			for (c = 0; c < n_clients; c++) {
				if (cli_pid[c] > 0) {
					kill(cli_pid[c], SIGTERM);
					(void)waitpid(cli_pid[c], NULL, 0);
					cli_pid[c] = -1;
				}
			}
			break;
		}

		/* A client exited. End the session when the session leader (client[0] —
		 * the window manager, launched first) exits OR when all clients are gone,
		 * exactly like startx ending with its .xinitrc foreground client. Tear the
		 * server down so the fbdev DDX restores the text console (fbdevCardFini ->
		 * FBCON_ENABLED) and psh resumes. Without this, exiting the WM left the
		 * server running on a blank, WM-less root with psh still blocked here — a
		 * dead end (no X, no shell). */
		{
			int leaderGone = 0, live = 0;
			for (c = 0; c < n_clients; c++) {
				if (w == cli_pid[c]) {
					fprintf(stderr, "xlaunch: client[%d] exited (status=0x%x)\n", c, (unsigned)status);
					cli_pid[c] = -1;
					if (c == 0) {
						leaderGone = 1;
					}
				}
			}
			for (c = 0; c < n_clients; c++) {
				if (cli_pid[c] > 0) {
					live++;
				}
			}
			if ((leaderGone != 0) || (live == 0)) {
				fprintf(stderr, "xlaunch: session ended (WM/last client exited) — shutting down X\n");
				for (c = 0; c < n_clients; c++) {
					if (cli_pid[c] > 0) {
						kill(cli_pid[c], SIGTERM);
						(void)waitpid(cli_pid[c], NULL, 0);
						cli_pid[c] = -1;
					}
				}
				kill(srv_pid, SIGTERM);
				(void)waitpid(srv_pid, NULL, 0);
				break;
			}
		}
	}

	return EXIT_SUCCESS;
}
