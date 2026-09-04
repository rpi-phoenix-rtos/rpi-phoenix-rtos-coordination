/*
 * fbprobe — write known bytes to /dev/fb0 and say what was written, so the
 * framebuffer's CHANNEL ORDER can be read off the screen instead of inferred.
 *
 * Why this exists: three places in the tree disagree about whether the Pi 4
 * framebuffer is RGB (byte0 = red) or BGR (byte0 = blue).
 *   - plo asks the VideoCore mailbox for SET_PIXEL_ORDER = 1
 *     (sources/plo/hal/aarch64/generic/video.c) and the X DDX cites that to
 *     declare redMask = 0x000000ff, i.e. red at byte 0 (tools/x11-port/ddx/fbdev.c);
 *   - the V3D scanout winsys applies a swap_color_rb to make GL's RGBA look
 *     right on screen, which only makes sense if the fb is BGR;
 *   - and on 2026-09-04 Window Maker's root background, configured
 *     rgb:50/50/75 (blue-grey), came out mauve (~75/50/50) — R and B exchanged.
 *
 * Guessing between them wastes Pi cycles, so this writes four labelled bands of
 * unambiguous bytes and prints the byte pattern of each. Look at the HDMI frame:
 * whichever band is RED tells you where red lives.
 *
 *   band 0 (top)    bytes FF 00 00 00   -> red iff byte0 is R (RGB fb)
 *   band 1          bytes 00 00 FF 00   -> red iff byte2 is R (BGR fb)
 *   band 2          bytes 00 FF 00 00   -> green either way (control)
 *   band 3 (bottom) bytes FF FF FF 00   -> white either way (control)
 *
 * Usage: fbprobe            (uses /dev/fb0 and RPI4FB geometry defaults)
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define FB_PATH   "/dev/fb0"
#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#define FB_BPP    4
#define BANDS     4

static const unsigned char band_bytes[BANDS][FB_BPP] = {
	{ 0xff, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0xff, 0x00 },
	{ 0x00, 0xff, 0x00, 0x00 },
	{ 0xff, 0xff, 0xff, 0x00 },
};

static const char *band_meaning[BANDS] = {
	"byte0=FF -> RED here means the fb is RGB (byte0 is red)",
	"byte2=FF -> RED here means the fb is BGR (byte2 is red)",
	"byte1=FF -> green in both orders (control)",
	"all FF   -> white in both orders (control)",
};

int main(void)
{
	int fd;
	int band;
	size_t row_bytes = (size_t)FB_WIDTH * FB_BPP;
	size_t band_rows = FB_HEIGHT / BANDS;
	unsigned char *row;

	fd = open(FB_PATH, O_RDWR);
	if (fd < 0) {
		perror("fbprobe: open " FB_PATH);
		return 1;
	}

	row = malloc(row_bytes);
	if (row == NULL) {
		fprintf(stderr, "fbprobe: out of memory\n");
		close(fd);
		return 1;
	}

	for (band = 0; band < BANDS; band++) {
		size_t x;
		size_t y;

		for (x = 0; x < (size_t)FB_WIDTH; x++) {
			memcpy(row + x * FB_BPP, band_bytes[band], FB_BPP);
		}

		/* /dev/fb0 is a plain byte device here: seek to the band's first row. */
		if (lseek(fd, (long)(band * band_rows * row_bytes), SEEK_SET) < 0) {
			perror("fbprobe: lseek");
			break;
		}
		for (y = 0; y < band_rows; y++) {
			if (write(fd, row, row_bytes) != (ssize_t)row_bytes) {
				perror("fbprobe: write");
				band = BANDS;
				break;
			}
		}

		if (band < BANDS) {
			printf("fbprobe: band %d rows %zu..%zu bytes %02x %02x %02x %02x  (%s)\n",
				band, band * band_rows, (band + 1) * band_rows - 1,
				band_bytes[band][0], band_bytes[band][1],
				band_bytes[band][2], band_bytes[band][3],
				band_meaning[band]);
			fflush(stdout);
		}
	}

	free(row);
	close(fd);
	printf("fbprobe: done — read the channel order off the screen, top band first\n");

	return 0;
}
