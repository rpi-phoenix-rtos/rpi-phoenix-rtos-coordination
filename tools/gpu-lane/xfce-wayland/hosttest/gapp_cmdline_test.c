/*
 * Host test of the gtk3-wayland GLib patch 0003 (gapplication: send stdin only over a
 * connection that passes fds): the shape of `xfdesktop --quit` and of a second `thunar`.
 *
 *   gapp_cmdline_test            the primary instance: owns org.phoenix.GappCmdlineTest on
 *                                the session bus and waits
 *   gapp_cmdline_test --quit     a remote instance: forwards its command line to the
 *                                primary (org.gtk.Application.CommandLine), which prints
 *                                what it got and quits
 *
 * Output lines start with "GAPP " (hosttest/gapp-cmdline.sh greps them).
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>

#include <gio/gio.h>


static int on_command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer data)
{
	gchar **argv;
	gint argc, i;
	gboolean quit = FALSE;
	GInputStream *in;

	(void)data;

	argv = g_application_command_line_get_arguments(cmdline, &argc);
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--quit") == 0) {
			quit = TRUE;
		}
	}
	g_strfreev(argv);

	if (!g_application_command_line_get_is_remote(cmdline)) {
		/* the primary's own start: stay up until a remote --quit */
		g_application_hold(app);
		g_print("GAPP primary up\n");
		return 0;
	}

	in = g_application_command_line_get_stdin(cmdline);
	g_print("GAPP primary got remote quit=%d stdin=%s\n", quit, (in != NULL) ? "passed" : "none");
	g_clear_object(&in);

	if (quit) {
		g_application_release(app);
		g_application_quit(app);
	}
	return 0;
}


int main(int argc, char **argv)
{
	GApplication *app;
	int rc;

	app = g_application_new("org.phoenix.GappCmdlineTest", G_APPLICATION_HANDLES_COMMAND_LINE);
	g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), NULL);
	rc = g_application_run(app, argc, argv);
	g_print("GAPP exit rc=%d\n", rc);
	g_object_unref(app);

	return rc;
}
