#include <glib.h>
#include <locale.h>
#include <stdio.h>

/* Mimic mc's early init: setlocale + a GOptionContext with translated help
 * strings + parse. This is the path that diverges from the clean glibtest. */
static gboolean opt_version = FALSE;
static char *opt_palette = NULL;

static GOptionEntry entries[] = {
	{ "version", 'V', 0, G_OPTION_ARG_NONE, &opt_version,
	  "Displays the current version (output of 'mc -V' for bug reports)", NULL },
	{ "colors", 'C', 0, G_OPTION_ARG_STRING, &opt_palette,
	  "Specify a set of colors: black, gray, red, brightred, green, brightgreen, brown", "<string>" },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

int main(int argc, char **argv)
{
	char *m;
	GError *err = NULL;
	GOptionContext *ctx;

	printf("GT2: setlocale(LC_ALL,\"\")=%s\n", setlocale(LC_ALL, "") ? "ok" : "NULL");
	m = setlocale(LC_MESSAGES, NULL);
	printf("GT2: setlocale(LC_MESSAGES,NULL)=%s\n", m ? m : "NULL");

	ctx = g_option_context_new("- minimal mc-init repro");
	g_option_context_add_main_entries(ctx, entries, NULL);
	printf("GT2: parsing...\n");
	if (!g_option_context_parse(ctx, &argc, &argv, &err)) {
		printf("GT2: parse error: %s\n", err ? err->message : "?");
	}
	char *help = g_option_context_get_help(ctx, TRUE, NULL);
	printf("GT2: help len=%zu\n", help ? strlen(help) : 0);
	g_free(help);
	g_option_context_free(ctx);
	printf("GT2-OK\n");
	return 0;
}
