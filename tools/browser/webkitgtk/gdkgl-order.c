/*
 * gdkgl-order: B10 host test for the G0 failure of build 65 (docs/browser/B10-WEBKITGTK.md §3.1).
 *
 * GDK 3 reads GDK_GL once, in gdk_pre_parse(), which GTK's option group runs as its pre-parse
 * hook. A program that parses gtk_get_option_group() BEFORE it sets GDK_GL=gles therefore gets
 * desktop GL (EGL_OPENGL_API), which our GLES-only Mesa refuses: "No GL implementation is
 * available". webkit-browser did exactly that until ports ac790dc.
 *
 * Build and run on the host (any GTK 3.24 with a Wayland session):
 *   gcc -o gdkgl-order gdkgl-order.c $(pkg-config --cflags --libs gtk+-3.0)
 *   GDK_BACKEND=wayland ./gdkgl-order after    ->  after: use_es=0 realize=ok   (the bug)
 *   GDK_BACKEND=wayland ./gdkgl-order before   ->  before: use_es=1 realize=ok  (the fix)
 * (2026-10-08, host GTK 3.24.52: exactly these two lines.) It realizes an unmapped popup window.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int before = argc > 1 && !strcmp(argv[1], "before");
    if (before)
        g_setenv("GDK_GL", "gles", TRUE);
    GOptionContext *options = g_option_context_new(NULL);
    g_option_context_add_group(options, gtk_get_option_group(FALSE));
    int ac = 1;
    char **av = argv;
    g_option_context_parse(options, &ac, &av, NULL);
    if (!before)
        g_setenv("GDK_GL", "gles", TRUE);
    gtk_init(NULL, NULL);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_realize(window);
    GError *error = NULL;
    GdkGLContext *context = gdk_window_create_gl_context(gtk_widget_get_window(window), &error);
    if (context && gdk_gl_context_realize(context, &error))
        printf("%s: use_es=%d realize=ok\n", before ? "before" : "after", gdk_gl_context_get_use_es(context));
    else
        printf("%s: realize=error %s\n", before ? "before" : "after", error ? error->message : "?");
    return 0;
}
