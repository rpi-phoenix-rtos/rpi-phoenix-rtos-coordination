/*
 * render_test.c -- host test of the Poppler configuration Atril uses on Phoenix-RTOS
 * (tools/gpu-lane/atril-wayland/hosttest/run.sh).
 *
 * Opens a PDF through poppler-glib, as Atril's PDF backend does (poppler_document_new_from_file,
 * poppler_page_render onto a cairo image surface), and checks what the sample document
 * (tools/make-sample-pdf.py) is known to contain: the page count, the title, the text of page 1,
 * and the colour at given points of given pages.
 *
 *   render_test FILE.pdf PAGES TITLE TEXT [page:x:y:rrggbb]...
 *
 * x, y in PDF points from the top-left; each page is rendered at 2x (144 dpi) on white.
 * A point passes when every channel is within 24 of the expected value. Prints one
 * "PASS"/"FAIL" line per check and exits 1 on any failure. Optionally writes the renders
 * as PNG when RENDER_TEST_PNG is set (a path prefix).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <poppler.h>
#include <cairo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCALE 2.0
#define TOLERANCE 24

static int failures;

static void check(int ok, const char *what, const char *detail)
{
	printf("  %s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail[0] ? ": " : "", detail);
	if (!ok)
		failures++;
}

static cairo_surface_t *render(PopplerDocument *doc, int index)
{
	PopplerPage *page = poppler_document_get_page(doc, index);
	double w, h;
	cairo_surface_t *surface;
	cairo_t *cr;

	poppler_page_get_size(page, &w, &h);
	surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)(w * SCALE), (int)(h * SCALE));
	cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_scale(cr, SCALE, SCALE);
	poppler_page_render(page, cr);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	g_object_unref(page);
	return surface;
}

int main(int argc, char **argv)
{
	GError *error = NULL;
	PopplerDocument *doc;
	PopplerPage *page;
	cairo_surface_t *pages[16] = { NULL };
	char detail[256], *uri, *title, *text;
	int n, want, i;

	if (argc < 5) {
		fprintf(stderr, "usage: render_test FILE.pdf PAGES TITLE TEXT [page:x:y:rrggbb]...\n");
		return 2;
	}
	uri = g_filename_to_uri(argv[1], NULL, NULL);
	doc = poppler_document_new_from_file(uri, NULL, &error);
	g_free(uri);
	snprintf(detail, sizeof(detail), "poppler %s, %s", poppler_get_version(), doc ? "opened" : error->message);
	check(doc != NULL, "open", detail);
	if (doc == NULL)
		return 1;

	n = poppler_document_get_n_pages(doc);
	want = atoi(argv[2]);
	snprintf(detail, sizeof(detail), "%d (want %d)", n, want);
	check(n == want, "pages", detail);

	title = poppler_document_get_title(doc);
	snprintf(detail, sizeof(detail), "'%s'", title ? title : "(none)");
	check(title != NULL && strcmp(title, argv[3]) == 0, "title", detail);
	g_free(title);

	page = poppler_document_get_page(doc, 0);
	text = poppler_page_get_text(page);
	snprintf(detail, sizeof(detail), "'%s' in the text of page 1 (%zu chars)", argv[4], text ? strlen(text) : 0);
	check(text != NULL && strstr(text, argv[4]) != NULL, "text", detail);
	g_free(text);
	g_object_unref(page);

	for (i = 5; i < argc; i++) {
		int p, rgb, stride, px, py;
		double x, y;
		unsigned char *data;
		unsigned int v, r, g, b, er, eg, eb;

		if (sscanf(argv[i], "%d:%lf:%lf:%x", &p, &x, &y, &rgb) != 4 || p < 1 || p > n || p > 16) {
			check(0, argv[i], "bad point");
			continue;
		}
		if (pages[p - 1] == NULL) {
			pages[p - 1] = render(doc, p - 1);
			if (getenv("RENDER_TEST_PNG") != NULL) {
				char path[512];
				snprintf(path, sizeof(path), "%s-%d.png", getenv("RENDER_TEST_PNG"), p);
				cairo_surface_write_to_png(pages[p - 1], path);
			}
		}
		data = cairo_image_surface_get_data(pages[p - 1]);
		stride = cairo_image_surface_get_stride(pages[p - 1]);
		px = (int)(x * SCALE);
		py = (int)(y * SCALE);
		v = *(unsigned int *)(data + py * stride + px * 4);
		r = (v >> 16) & 0xff;
		g = (v >> 8) & 0xff;
		b = v & 0xff;
		er = (rgb >> 16) & 0xff;
		eg = (rgb >> 8) & 0xff;
		eb = rgb & 0xff;
		snprintf(detail, sizeof(detail), "page %d (%.1f,%.1f) = %02x%02x%02x, want %06x", p, x, y, r, g, b, rgb);
		check(abs((int)r - (int)er) <= TOLERANCE && abs((int)g - (int)eg) <= TOLERANCE &&
		      abs((int)b - (int)eb) <= TOLERANCE, "pixel", detail);
	}
	for (i = 0; i < 16; i++) {
		if (pages[i] != NULL)
			cairo_surface_destroy(pages[i]);
	}
	g_object_unref(doc);
	printf("render_test: %d failure(s)\n", failures);
	return failures ? 1 : 0;
}
