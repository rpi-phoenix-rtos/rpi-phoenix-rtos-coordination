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

/* Copy environ and append DISPLAY=:0 (overriding any inherited DISPLAY).
 * Built in the parent so the vfork'd child never has to touch the
 * (shared) address space beyond exec. */
static char **build_client_env(void)
{
	static char display_var[] = "DISPLAY=" DISPLAY_VALUE;
	int n = 0, i, j;
	char **env;

	for (i = 0; environ[i] != NULL; i++)
		n++;

	/* worst case: all inherited vars + our DISPLAY + NULL */
	env = malloc((size_t)(n + 2) * sizeof(char *));
	if (env == NULL)
		return NULL;

	j = 0;
	for (i = 0; i < n; i++) {
		if (strncmp(environ[i], "DISPLAY=", 8) == 0)
			continue; /* drop inherited DISPLAY, we set our own */
		env[j++] = environ[i];
	}
	env[j++] = display_var;
	env[j] = NULL;

	return env;
}

int main(int argc, char *argv[])
{
	const char *server_path, *font_dir, *client_path;
	/* volatile: these are live across the vfork() calls below. The children
	 * only execve/_exit (never write parent memory), so no real clobber can
	 * occur, but at -O2 GCC inlines the env/argv builders into main() and its
	 * vfork-clobber analysis can't prove that — volatile silences the
	 * -Wclobbered warning and documents the constraint for upstreaming. */
	char **server_argv;
	char ** volatile client_argv;
	char ** volatile client_env;
	char *server_extra[6];
	pid_t srv_pid;
	volatile pid_t cli_pid;
	pid_t w;
	int status, tick;
	char *const *client_extra;
	int n_client_extra;
	static char sp_buf[256], fd_buf[256], cp_buf[256];

	if (argc >= 4) {
		/* Explicit form: <Xphoenix> <fontdir> <client> [client-args...] */
		server_path = argv[1];
		font_dir    = argv[2];
		client_path = argv[3];
		client_extra = &argv[4];
		n_client_extra = argc - 4;
	}
	else {
		/* "startx" convenience: 0 or 1 args. Auto-detect the install prefix
		 * (NFS export mounted at /nfstest on netboot, else "/" on nfsroot/sd)
		 * and default the client to xeyes. Optional argv[1] picks the client
		 * (bare name under <prefix>/bin, or an absolute path). So:
		 *   startx          -> <prefix>/bin/xeyes
		 *   startx twm      -> <prefix>/bin/twm
		 *   startx /bin/2048-> /bin/2048
		 */
		const char *prefix = "/nfstest";
		const char *client = (argc >= 2) ? argv[1] : "xeyes";
		struct stat stx;

		snprintf(sp_buf, sizeof(sp_buf), "%s/bin/Xphoenix", prefix);
		if (stat(sp_buf, &stx) != 0) {
			prefix = ""; /* root install (nfsroot / sd) */
			snprintf(sp_buf, sizeof(sp_buf), "%s/bin/Xphoenix", prefix);
		}
		snprintf(fd_buf, sizeof(fd_buf), "%s/usr/share/fonts/X11/misc", prefix);
		if (client[0] == '/')
			snprintf(cp_buf, sizeof(cp_buf), "%s", client);
		else
			snprintf(cp_buf, sizeof(cp_buf), "%s/bin/%s", prefix, client);

		server_path = sp_buf;
		font_dir    = fd_buf;
		client_path = cp_buf;
		client_extra = NULL;
		n_client_extra = 0;
		fprintf(stderr, "xlaunch: startx mode — prefix=%s client=%s\n",
			prefix[0] ? prefix : "/", client);
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

	/* Build the client argv (prog + any trailing args from our argv[4..]) and
	 * the client environment (with DISPLAY=:0) BEFORE forking — vfork-safe. */
	client_argv = build_argv((char *)client_path, client_extra, n_client_extra);
	if (client_argv == NULL) {
		fprintf(stderr, "xlaunch: out of memory building client argv\n");
		return EXIT_FAILURE;
	}
	client_env = build_client_env();
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

	/* --- fork the client with DISPLAY=:0 --- */
	fprintf(stderr, "xlaunch: starting client: %s (DISPLAY=%s)\n", client_path, DISPLAY_VALUE);
	cli_pid = vfork();
	if (cli_pid < 0) {
		fprintf(stderr, "xlaunch: vfork (client) failed: %s\n", strerror(errno));
		kill(srv_pid, SIGTERM);
		return EXIT_FAILURE;
	}
	if (cli_pid == 0) {
		/* child: exec only */
		execve(client_path, client_argv, client_env);
		_exit(127);
	}

	/* --- supervise: block in waitpid for either child --- */
	for (;;) {
		w = waitpid(-1, &status, 0);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "xlaunch: waitpid failed: %s\n", strerror(errno));
			break;
		}

		if (w == srv_pid) {
			fprintf(stderr, "xlaunch: server exited (status=0x%x) — killing client\n",
				(unsigned)status);
			/* Guard against cli_pid == -1 (client already reaped below):
			 * kill(-1, ...) would broadcast. Only signal a live client. */
			if (cli_pid > 0) {
				kill(cli_pid, SIGTERM);
				(void)waitpid(cli_pid, NULL, 0);
			}
			break;
		}
		if (w == cli_pid) {
			fprintf(stderr, "xlaunch: client exited (status=0x%x); server still running\n",
				(unsigned)status);
			/* Keep the server up so the painted root persists and a new
			 * client could attach. Continue waiting on the server. */
			cli_pid = -1;
			continue;
		}
		/* some other child — ignore and keep waiting */
	}

	return EXIT_SUCCESS;
}
