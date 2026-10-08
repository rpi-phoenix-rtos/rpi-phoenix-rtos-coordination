/*
 * Host test: usbmouse HID report-descriptor parser + report translation
 * (phoenix-rtos-devices tty/usbmouse/hidmouse.c).
 *
 * Descriptor BYTES below are data taken from public sources (cited per
 * descriptor); no code is copied from them.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hidmouse.h"


static int failures;
static int checks;

#define CHECK(cond, ...) \
	do { \
		checks++; \
		if (!(cond)) { \
			failures++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)


/* --- descriptors ------------------------------------------------------- */

/* PixArt 093a:2510 -- the owner's lab mouse. Linux Documentation/hid/hidintro.rst
 * ("hexdump -C /sys/bus/hid/devices/0003:093A:2510.0002/report_descriptor"). */
static const uint8_t pixart_093a_2510[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x75, 0x05, 0x95, 0x01, 0x81, 0x01,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03,
	0x81, 0x06, 0xc0, 0xc0
};

/* Logitech Unifying receiver, mouse report (ID 2, 16 buttons, 12-bit X/Y,
 * wheel, AC Pan). Linux drivers/hid/hid-logitech-dj.c, mse_descriptor[]. */
static const uint8_t logitech_unifying[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01,
	0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x95, 0x10, 0x75, 0x01, 0x81, 0x02, 0x05, 0x01, 0x16, 0x01,
	0xf8, 0x26, 0xff, 0x07, 0x75, 0x0c, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x06, 0x15, 0x81,
	0x25, 0x7f, 0x75, 0x08, 0x95, 0x01, 0x09, 0x38, 0x81, 0x06, 0x05, 0x0c, 0x0a, 0x38, 0x02, 0x95,
	0x01, 0x81, 0x06, 0xc0, 0xc0
};

/* 16-bit-delta gaming mouse (Logitech G-series layout: 16 buttons, 16-bit
 * X/Y, wheel, AC Pan, no Report ID). Linux tools/testing/selftests/hid/tests/
 * test_mouse.py, TwoWheelMouse. */
static const uint8_t gaming_16bit[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x10,
	0x15, 0x00, 0x25, 0x01, 0x95, 0x10, 0x75, 0x01, 0x81, 0x02, 0x05, 0x01, 0x16, 0x01, 0x80, 0x26,
	0xff, 0x7f, 0x75, 0x10, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x06, 0x15, 0x81, 0x25, 0x7f,
	0x75, 0x08, 0x95, 0x01, 0x09, 0x38, 0x81, 0x06, 0x05, 0x0c, 0x0a, 0x38, 0x02, 0x95, 0x01, 0x81,
	0x06, 0xc0, 0xc0
};

/* Xiaomi MI Dongle MI Wireless Mouse 2717:003b: buttons + wheel in report 1,
 * 12-bit X/Y in report 2, consumer keys in report 3. Linux selftests
 * test_mouse.py, MIDongleMIWirelessMouse. */
static const uint8_t mi_dongle[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x95, 0x05, 0x75, 0x01,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03,
	0x81, 0x01, 0x75, 0x08, 0x95, 0x01, 0x05, 0x01, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x81, 0x06,
	0x05, 0x0c, 0x0a, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06, 0xc0, 0x85, 0x02, 0x09, 0x01, 0xa1, 0x00,
	0x75, 0x0c, 0x95, 0x02, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0xf8, 0x26, 0xff, 0x07,
	0x81, 0x06, 0xc0, 0xc0, 0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x01, 0x09, 0xcd, 0x81, 0x06, 0x0a, 0x83, 0x01, 0x81, 0x06, 0x09, 0xb5, 0x81,
	0x06, 0x09, 0xb6, 0x81, 0x06, 0x09, 0xea, 0x81, 0x06, 0x09, 0xe9, 0x81, 0x06, 0x0a, 0x25, 0x02,
	0x81, 0x06, 0x0a, 0x24, 0x02, 0x81, 0x06, 0xc0
};

/* Microsoft Notebook Optical Mouse 3000 (196 bytes): a Consumer Control
 * application carrying AC Pan in report 0x13 first, then the mouse in report
 * 0x11 with Resolution Multiplier feature reports. FreeBSD PR usb/125941
 * (freebsd-usb list, 2008-08, krepdump output). Bytes 83..195 must equal the
 * Linux selftest ResolutionMultiplierMouse (checked below). */
static const uint8_t ms_notebook_3000[] = {
	0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x05, 0x01, 0x09, 0x02, 0xa1, 0x02, 0x85, 0x13, 0x05, 0x0c,
	0x0a, 0x38, 0x02, 0x95, 0x01, 0x75, 0x08, 0x15, 0x81, 0x25, 0x7f, 0x81, 0x06, 0x85, 0x17, 0x06,
	0x00, 0xff, 0x0a, 0x06, 0xff, 0x15, 0x00, 0x25, 0x01, 0x35, 0x01, 0x45, 0x04, 0x95, 0x01, 0x75,
	0x02, 0xb1, 0x02, 0x35, 0x00, 0x45, 0x00, 0xb1, 0x01, 0x0a, 0x04, 0xff, 0x75, 0x01, 0xb1, 0x02,
	0x75, 0x03, 0xb1, 0x01, 0x85, 0x18, 0x0a, 0x08, 0xff, 0x75, 0x01, 0xb1, 0x02, 0x75, 0x07, 0xb1,
	0x01, 0xc0, 0xc0, 0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x05, 0x01, 0x09, 0x02, 0xa1, 0x02, 0x85,
	0x11, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x95, 0x03, 0x75, 0x01, 0x25,
	0x01, 0x81, 0x02, 0x95, 0x01, 0x81, 0x01, 0x09, 0x05, 0x81, 0x02, 0x95, 0x03, 0x81, 0x01, 0x05,
	0x01, 0x09, 0x30, 0x09, 0x31, 0x95, 0x02, 0x75, 0x08, 0x15, 0x81, 0x25, 0x7f, 0x81, 0x06, 0xa1,
	0x02, 0x85, 0x12, 0x09, 0x48, 0x95, 0x01, 0x75, 0x02, 0x15, 0x00, 0x25, 0x01, 0x35, 0x01, 0x45,
	0x04, 0xb1, 0x02, 0x35, 0x00, 0x45, 0x00, 0x75, 0x06, 0xb1, 0x01, 0x85, 0x11, 0x09, 0x38, 0x15,
	0x81, 0x25, 0x7f, 0x75, 0x08, 0x81, 0x06, 0xc0, 0x05, 0x0c, 0x75, 0x08, 0x0a, 0x38, 0x02, 0x81,
	0x06, 0xc0, 0xc0, 0xc0
};

/* Linux selftests test_mouse.py, ResolutionMultiplierMouse (113 bytes). */
static const uint8_t linux_resmult[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x05, 0x01, 0x09, 0x02, 0xa1, 0x02, 0x85, 0x11, 0x09, 0x01,
	0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x95, 0x03, 0x75, 0x01, 0x25, 0x01, 0x81, 0x02,
	0x95, 0x01, 0x81, 0x01, 0x09, 0x05, 0x81, 0x02, 0x95, 0x03, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30,
	0x09, 0x31, 0x95, 0x02, 0x75, 0x08, 0x15, 0x81, 0x25, 0x7f, 0x81, 0x06, 0xa1, 0x02, 0x85, 0x12,
	0x09, 0x48, 0x95, 0x01, 0x75, 0x02, 0x15, 0x00, 0x25, 0x01, 0x35, 0x01, 0x45, 0x04, 0xb1, 0x02,
	0x35, 0x00, 0x45, 0x00, 0x75, 0x06, 0xb1, 0x01, 0x85, 0x11, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f,
	0x75, 0x08, 0x81, 0x06, 0xc0, 0x05, 0x0c, 0x75, 0x08, 0x0a, 0x38, 0x02, 0x81, 0x06, 0xc0, 0xc0,
	0xc0
};

/* Hi-res wheel mouse, 16-bit X/Y/wheel/AC Pan in report 0x1a, multiplier
 * feature in report 0x12. Linux selftests, ResolutionMultiplierHWheelMouse. */
static const uint8_t hires_16bit_ids[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x05, 0x01, 0x09, 0x02, 0xa1, 0x02, 0x85, 0x1a, 0x09, 0x01,
	0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x15, 0x00, 0x25, 0x01,
	0x81, 0x02, 0x75, 0x03, 0x95, 0x01, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x95, 0x02,
	0x75, 0x10, 0x16, 0x01, 0x80, 0x26, 0xff, 0x7f, 0x81, 0x06, 0xa1, 0x02, 0x85, 0x12, 0x09, 0x48,
	0x95, 0x01, 0x75, 0x02, 0x15, 0x00, 0x25, 0x01, 0x35, 0x01, 0x45, 0x0c, 0xb1, 0x02, 0x85, 0x1a,
	0x09, 0x38, 0x35, 0x00, 0x45, 0x00, 0x95, 0x01, 0x75, 0x10, 0x16, 0x01, 0x80, 0x26, 0xff, 0x7f,
	0x81, 0x06, 0xc0, 0xa1, 0x02, 0x85, 0x12, 0x09, 0x48, 0x75, 0x02, 0x15, 0x00, 0x25, 0x01, 0x35,
	0x01, 0x45, 0x0c, 0xb1, 0x02, 0x35, 0x00, 0x45, 0x00, 0x75, 0x04, 0xb1, 0x01, 0x85, 0x1a, 0x05,
	0x0c, 0x95, 0x01, 0x75, 0x10, 0x16, 0x01, 0x80, 0x26, 0xff, 0x7f, 0x0a, 0x38, 0x02, 0x81, 0x06,
	0xc0, 0xc0, 0xc0, 0xc0
};

/* Syzbot-generated garbage (features of no size, 4-byte usage min/max).
 * Linux selftests test_mouse.py, BadReportDescriptorMouse. */
static const uint8_t syzbot_bad[] = {
	0x96, 0x01, 0x00, 0x06, 0x01, 0x00, 0x2a, 0x90, 0xa0, 0x27, 0x00, 0x00, 0x00, 0x00, 0xb3, 0x81,
	0x3e, 0x25, 0x03, 0x1b, 0xdd, 0xe8, 0x40, 0x50, 0x3b, 0x5d, 0x8c, 0x3d, 0xda
};

/* Boot keyboard, HID 1.11 Appendix E.6: no pointer at all. */
static const uint8_t boot_keyboard[] = {
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
	0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
	0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0
};


/* --- helpers ----------------------------------------------------------- */

static void putBits(uint8_t *buf, unsigned int off, unsigned int size, int32_t value)
{
	unsigned int i;

	for (i = 0; i < size; ++i, ++off) {
		if ((((uint32_t)value >> i) & 1u) != 0u) {
			buf[off / 8u] |= (uint8_t)(1u << (off % 8u));
		}
		else {
			buf[off / 8u] &= (uint8_t)~(1u << (off % 8u));
		}
	}
}


static int parseOk(const char *name, const uint8_t *d, size_t len, hidmouse_layout_t *l, const char *expect)
{
	const char *reason = NULL;
	char line[160];
	int err = hidmouse_parse(d, len, l, &reason);

	CHECK(err == 0, "%s: parse failed: %d (%s)", name, err, (reason != NULL) ? reason : "?");
	if (err != 0) {
		return -1;
	}
	hidmouse_describe(l, line, sizeof(line));
	CHECK(strcmp(line, expect) == 0, "%s:\n   got  '%s'\n   want '%s'", name, line, expect);
	printf("  %-22s usbmouse: report protocol %s\n", name, line);
	return 0;
}


static void parseFails(const char *name, const uint8_t *d, size_t len, const char *wantReason)
{
	hidmouse_layout_t l;
	const char *reason = NULL;
	int err = hidmouse_parse(d, len, &l, &reason);

	CHECK(err < 0, "%s: parse should fail", name);
	CHECK((reason != NULL) && (strcmp(reason, wantReason) == 0), "%s: reason '%s', want '%s'", name, (reason != NULL) ? reason : "(null)", wantReason);
	CHECK(l.x.size == 0 && l.y.size == 0 && l.wheel.size == 0, "%s: layout not cleared on failure", name);
	printf("  %-22s usbmouse: boot protocol (%s)\n", name, (reason != NULL) ? reason : "?");
}


/* Translate and compare against an expected packet stream. */
static void expectPkts(const char *what, const hidmouse_layout_t *l, uint8_t *btn, const uint8_t *rep, size_t len, const int8_t *want, size_t nwant)
{
	uint8_t out[HIDMOUSE_MAX_SPLIT * HIDMOUSE_PKT_SIZE];
	size_t n = hidmouse_translate(l, btn, rep, len, out, sizeof(out));
	size_t i;

	CHECK(n == nwant * HIDMOUSE_PKT_SIZE, "%s: %zu packets, want %zu", what, n / HIDMOUSE_PKT_SIZE, nwant);
	for (i = 0; (i < nwant * HIDMOUSE_PKT_SIZE) && (i < n); ++i) {
		CHECK((int8_t)out[i] == want[i], "%s: byte %zu = %d, want %d", what, i, (int8_t)out[i], want[i]);
	}
}


/* --- tests ------------------------------------------------------------- */

static void testPixart(void)
{
	hidmouse_layout_t l;
	uint8_t btn = 0;

	if (parseOk("pixart 093a:2510", pixart_093a_2510, sizeof(pixart_093a_2510), &l,
			"id=none buttons=3 len=4 x=8@8 y=8@16 wheel=8@24 hwheel=none") != 0) {
		return;
	}

	{
		const uint8_t rep[] = { 0x01, 0x05, 0xfb, 0x01 };
		const int8_t want[] = { 1, 5, -5, 1 };
		expectPkts("pixart left+move+wheel up", &l, &btn, rep, sizeof(rep), want, 1);
	}
	{
		const uint8_t rep[] = { 0x00, 0x00, 0x00, 0xff };
		const int8_t want[] = { 0, 0, 0, -1 };
		expectPkts("pixart wheel down", &l, &btn, rep, sizeof(rep), want, 1);
	}
	{
		/* a short (boot-style) report: the wheel past the end reads as 0 */
		const uint8_t rep[] = { 0x02, 0x81, 0x7f };
		const int8_t want[] = { 2, -127, 127, 0 };
		expectPkts("pixart 3-byte report", &l, &btn, rep, sizeof(rep), want, 1);
	}
	{
		const uint8_t rep[] = { 0x02 };
		const int8_t want[] = { 2, 0, 0, 0 };
		expectPkts("pixart 1-byte report", &l, &btn, rep, sizeof(rep), want, 1);
	}
	CHECK(hidmouse_translate(&l, &btn, NULL, 0, (uint8_t[4]) { 0 }, 4) == 0, "pixart: empty report must give nothing");
}


static void testLogitech(void)
{
	hidmouse_layout_t l;
	uint8_t btn = 0;
	uint8_t rep[8];

	if (parseOk("logitech unifying", logitech_unifying, sizeof(logitech_unifying), &l,
			"id=2 buttons=16 len=8 x=12@16#2 y=12@28#2 wheel=8@40#2 hwheel=8@48#2") != 0) {
		return;
	}

	/* left + button 9 (beyond the packet), dx=+300, dy=-5, wheel +1 */
	memset(rep, 0, sizeof(rep));
	rep[0] = 0x02;
	putBits(rep + 1, 0, 1, 1);
	putBits(rep + 1, 8, 1, 1);
	putBits(rep + 1, 16, 12, 300);
	putBits(rep + 1, 28, 12, -5);
	putBits(rep + 1, 40, 8, 1);
	{
		const int8_t want[] = { 1, 127, -5, 1, 1, 127, 0, 0, 1, 46, 0, 0 };
		expectPkts("logitech 12-bit dx=300 split", &l, &btn, rep, sizeof(rep), want, 3);
	}

	/* most negative 12-bit value */
	memset(rep, 0, sizeof(rep));
	rep[0] = 0x02;
	putBits(rep + 1, 16, 12, -2047);
	putBits(rep + 1, 28, 12, 2047);
	{
		uint8_t out[HIDMOUSE_MAX_SPLIT * HIDMOUSE_PKT_SIZE];
		size_t n = hidmouse_translate(&l, &btn, rep, sizeof(rep), out, sizeof(out));
		int sx = 0, sy = 0;
		size_t i;
		for (i = 0; i < n; i += 4) {
			sx += (int8_t)out[i + 1];
			sy += (int8_t)out[i + 2];
		}
		CHECK(sx == -2047 && sy == 2047, "logitech: motion lost in split: %d,%d", sx, sy);
		CHECK(btn == 0, "logitech: buttons not released (0x%x)", btn);
	}

	/* another Report ID on the same endpoint (HID++ short report 0x10) */
	{
		const uint8_t hidpp[] = { 0x10, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
		uint8_t out[64];
		CHECK(hidmouse_translate(&l, &btn, hidpp, sizeof(hidpp), out, sizeof(out)) == 0, "logitech: foreign report id must be ignored");
	}
}


static void testGaming16(void)
{
	hidmouse_layout_t l;
	uint8_t btn = 0;
	uint8_t rep[8];
	uint8_t out[HIDMOUSE_MAX_SPLIT * HIDMOUSE_PKT_SIZE];
	size_t n, i;
	int sx = 0;

	if (parseOk("gaming 16-bit", gaming_16bit, sizeof(gaming_16bit), &l,
			"id=none buttons=16 len=8 x=16@16 y=16@32 wheel=8@48 hwheel=8@56") != 0) {
		return;
	}

	/* right button, dx=-1000, dy=+1, wheel -2 */
	memset(rep, 0, sizeof(rep));
	putBits(rep, 1, 1, 1);
	putBits(rep, 16, 16, -1000);
	putBits(rep, 32, 16, 1);
	putBits(rep, 48, 8, -2);
	n = hidmouse_translate(&l, &btn, rep, sizeof(rep), out, sizeof(out));
	CHECK(n == 8 * 4, "gaming: dx=-1000 -> %zu packets, want 8", n / 4);
	for (i = 0; i < n; i += 4) {
		sx += (int8_t)out[i + 1];
		CHECK(out[i] == 0x02, "gaming: packet %zu buttons 0x%x", i / 4, out[i]);
		CHECK((int8_t)out[i + 3] == ((i == 0) ? -2 : 0), "gaming: wheel only in the first packet");
	}
	CHECK(sx == -1000, "gaming: dx sum %d", sx);
	CHECK((int8_t)out[2] == 1, "gaming: dy in first packet");

	/* beyond the split cap: clamped to 16 packets of 127 */
	memset(rep, 0, sizeof(rep));
	putBits(rep, 16, 16, 30000);
	n = hidmouse_translate(&l, &btn, rep, sizeof(rep), out, sizeof(out));
	CHECK(n == HIDMOUSE_MAX_SPLIT * 4, "gaming: dx=30000 -> %zu packets, want cap %u", n / 4, HIDMOUSE_MAX_SPLIT);

	/* a small output buffer is never overrun */
	n = hidmouse_translate(&l, &btn, rep, sizeof(rep), out, 6);
	CHECK(n == 4, "gaming: 6-byte buffer -> %zu bytes, want 4", n);
}


static void testMiDongle(void)
{
	hidmouse_layout_t l;
	uint8_t btn = 0;

	if (parseOk("mi dongle (split ids)", mi_dongle, sizeof(mi_dongle), &l,
			"id=2 buttons=5 len=4 x=12@0#2 y=12@12#2 wheel=8@8#1 hwheel=8@16#1") != 0) {
		return;
	}

	{
		/* report 1: left button + wheel down */
		const uint8_t rep[] = { 0x01, 0x01, 0xff, 0x00 };
		const int8_t want[] = { 1, 0, 0, -1 };
		expectPkts("mi report 1 (buttons+wheel)", &l, &btn, rep, sizeof(rep), want, 1);
	}
	{
		/* report 2: dx=10 dy=-3 -- left button stays latched */
		uint8_t rep[4] = { 0x02, 0, 0, 0 };
		const int8_t want[] = { 1, 10, -3, 0 };
		putBits(rep + 1, 0, 12, 10);
		putBits(rep + 1, 12, 12, -3);
		expectPkts("mi report 2 (motion)", &l, &btn, rep, sizeof(rep), want, 1);
	}
	{
		/* report 3: consumer keys, not ours */
		const uint8_t rep[] = { 0x03, 0x10 };
		uint8_t out[64];
		CHECK(hidmouse_translate(&l, &btn, rep, sizeof(rep), out, sizeof(out)) == 0, "mi: consumer report must be ignored");
	}
}


static void testMicrosoft(void)
{
	hidmouse_layout_t l;
	uint8_t btn = 0;

	CHECK(sizeof(ms_notebook_3000) == 196, "ms 3000: %zu bytes, want 196", sizeof(ms_notebook_3000));
	CHECK(memcmp(ms_notebook_3000 + 83, linux_resmult, sizeof(linux_resmult)) == 0 && sizeof(linux_resmult) == 113,
		"ms 3000: bytes 83.. differ from the Linux ResolutionMultiplierMouse");

	if (parseOk("ms notebook 3000", ms_notebook_3000, sizeof(ms_notebook_3000), &l,
			"id=17 buttons=4 len=6 x=8@8#17 y=8@16#17 wheel=8@24#17 hwheel=8@0#19") != 0) {
		return;
	}
	CHECK(l.button[4].size == 1 && l.button[4].off == 4, "ms 3000: button 5 at bit 4");
	CHECK(l.button[3].size == 0, "ms 3000: no button 4");

	{
		/* report 0x11: middle + button 5, dx=-1, dy=2, wheel +3, pan 0 */
		const uint8_t rep[] = { 0x11, 0x14, 0xff, 0x02, 0x03, 0x00 };
		const int8_t want[] = { 0x14, -1, 2, 3 };
		expectPkts("ms report 0x11", &l, &btn, rep, sizeof(rep), want, 1);
	}
	{
		/* report 0x13: horizontal tilt only -- nothing for the 4-byte packet */
		const uint8_t rep[] = { 0x13, 0x01 };
		uint8_t out[64];
		CHECK(hidmouse_translate(&l, &btn, rep, sizeof(rep), out, sizeof(out)) == 0, "ms: tilt report gives no packet (no hwheel byte)");
	}

	parseOk("linux resmult", linux_resmult, sizeof(linux_resmult), &l,
		"id=17 buttons=4 len=6 x=8@8#17 y=8@16#17 wheel=8@24#17 hwheel=8@32#17");
	/* the AC Pan after the nested collection is still in report 0x11: a Report ID
	 * set inside a collection outlives its End Collection (it is a global item) */
}


static void testHires16(void)
{
	hidmouse_layout_t l;
	uint8_t btn = 0;
	uint8_t rep[10];

	if (parseOk("hi-res 16-bit ids", hires_16bit_ids, sizeof(hires_16bit_ids), &l,
			"id=26 buttons=5 len=10 x=16@8#26 y=16@24#26 wheel=16@40#26 hwheel=16@56#26") != 0) {
		return;
	}

	memset(rep, 0, sizeof(rep));
	rep[0] = 0x1a;
	putBits(rep + 1, 8, 16, 5);
	putBits(rep + 1, 24, 16, -200);
	putBits(rep + 1, 40, 16, -300); /* a 16-bit wheel value clamps to the int8 byte */
	{
		const int8_t want[] = { 0, 5, -127, -127, 0, 0, -73, 0 };
		expectPkts("hires dy=-200 wheel=-300", &l, &btn, rep, sizeof(rep), want, 2);
	}
}


static void testMalformed(void)
{
	uint8_t d[256];
	size_t n;

	parseFails("syzbot", syzbot_bad, sizeof(syzbot_bad), "no relative X/Y");
	parseFails("boot keyboard", boot_keyboard, sizeof(boot_keyboard), "no relative X/Y");
	parseFails("empty", pixart_093a_2510, 0, "empty descriptor");
	parseFails("truncated item", pixart_093a_2510, 43, "truncated item");
	parseFails("missing end coll", pixart_093a_2510, sizeof(pixart_093a_2510) - 1, "unbalanced collection");

	memcpy(d, pixart_093a_2510, sizeof(pixart_093a_2510));
	d[sizeof(pixart_093a_2510)] = 0xc0;
	parseFails("extra end coll", d, sizeof(pixart_093a_2510) + 1, "unbalanced collection");

	/* absolute X/Y (tablet-like): Input (Data,Var,Abs) */
	memcpy(d, pixart_093a_2510, sizeof(pixart_093a_2510));
	d[49] = 0x02;
	parseFails("absolute X/Y", d, sizeof(pixart_093a_2510), "absolute X/Y");

	{
		const uint8_t pop[] = { 0xb4 };
		parseFails("pop underflow", pop, sizeof(pop), "pop underflow");
	}
	{
		const uint8_t push[] = { 0xa4, 0xa4, 0xa4, 0xa4, 0xa4 };
		parseFails("push overflow", push, sizeof(push), "push overflow");
	}
	{
		/* Report Size 32 x Report Count 65535 */
		const uint8_t big[] = { 0x05, 0x01, 0x09, 0x30, 0x75, 0x20, 0x96, 0xff, 0xff, 0x81, 0x06 };
		parseFails("huge report", big, sizeof(big), "report too long");
	}
	{
		/* Report Size 0xffffffff x Count 2: the multiplication must not wrap */
		const uint8_t big[] = { 0x77, 0xff, 0xff, 0xff, 0xff, 0x95, 0x02, 0x81, 0x06 };
		parseFails("size overflow", big, sizeof(big), "report too long");
	}
	{
		const uint8_t id0[] = { 0x85, 0x00 };
		parseFails("report id 0", id0, sizeof(id0), "bad report id");
	}
	{
		/* an input without ID, then one with ID 1 */
		const uint8_t mixed[] = { 0x75, 0x08, 0x95, 0x01, 0x81, 0x01, 0x85, 0x01, 0x81, 0x01 };
		parseFails("mixed ids", mixed, sizeof(mixed), "mixed report ids");
	}
	{
		const uint8_t lng[] = { 0xfe, 0x10, 0x00, 0x01 };
		parseFails("truncated long item", lng, sizeof(lng), "truncated long item");
	}
	{
		const uint8_t rsv[] = { 0x0c };
		parseFails("reserved item type", rsv, sizeof(rsv), "reserved item");
	}
	{
		uint8_t deep[40];
		for (n = 0; n < sizeof(deep); n += 2) {
			deep[n] = 0xa1;
			deep[n + 1] = 0x00;
		}
		parseFails("collections too deep", deep, sizeof(deep), "collections too deep");
	}
	{
		/* 17 distinct input Report IDs */
		uint8_t many[17 * 4 + 4];
		size_t k = 0;
		many[k++] = 0x75;
		many[k++] = 0x08;
		many[k++] = 0x95;
		many[k++] = 0x01;
		for (n = 1; n <= 17; ++n) {
			many[k++] = 0x85;
			many[k++] = (uint8_t)n;
			many[k++] = 0x81;
			many[k++] = 0x01;
		}
		parseFails("too many ids", many, k, "too many report ids");
	}
	{
		/* a valid long item is skipped, the mouse after it still parses */
		hidmouse_layout_t l;
		memset(d, 0, sizeof(d));
		d[0] = 0xfe;
		d[1] = 0x02;
		d[2] = 0x55;
		memcpy(d + 5, pixart_093a_2510, sizeof(pixart_093a_2510));
		parseOk("long item + pixart", d, 5 + sizeof(pixart_093a_2510), &l,
			"id=none buttons=3 len=4 x=8@8 y=8@16 wheel=8@24 hwheel=none");
	}
}


/* Configuration descriptor walk (USB 2.0 ch. 9 layout, HID 1.11 6.2.1). */
static void testConfig(void)
{
	/* single-interface boot mouse, report descriptor 52 bytes, mps 4 */
	static const uint8_t mouse[] = {
		0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xa0, 0x32,
		0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
		0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x34, 0x00,
		0x07, 0x05, 0x81, 0x03, 0x04, 0x00, 0x0a
	};
	/* receiver: keyboard iface 0, mouse iface 1 (with an alternate setting 1
	 * that must be ignored), HID descriptor listing a physical descriptor first */
	static const uint8_t combo[] = {
		0x09, 0x02, 0x5e, 0x00, 0x02, 0x01, 0x00, 0xa0, 0x32,
		0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
		0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x3b, 0x00,
		0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x08,
		0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
		0x0c, 0x21, 0x11, 0x01, 0x00, 0x02, 0x23, 0x10, 0x00, 0x22, 0x94, 0x00,
		0x07, 0x05, 0x01, 0x03, 0x40, 0x00, 0x02, /* interrupt OUT: skipped */
		0x07, 0x05, 0x82, 0x03, 0x14, 0x00, 0x02,
		0x09, 0x04, 0x01, 0x01, 0x01, 0x03, 0x01, 0x02, 0x00,
		0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0xff, 0x00,
		0x07, 0x05, 0x83, 0x03, 0x40, 0x00, 0x02
	};
	size_t rlen, mps;
	uint8_t bad[sizeof(mouse)];

	CHECK(hidmouse_findInterface(mouse, sizeof(mouse), 0, &rlen, &mps) == 0 && rlen == 52 && mps == 4,
		"config mouse: rlen=%zu mps=%zu", rlen, mps);
	CHECK(hidmouse_findInterface(mouse, sizeof(mouse), 1, &rlen, &mps) < 0, "config mouse: no interface 1");
	CHECK(hidmouse_findInterface(combo, sizeof(combo), 1, &rlen, &mps) == 0 && rlen == 0x94 && mps == 0x14,
		"config combo iface 1: rlen=%zu mps=%zu", rlen, mps);
	CHECK(hidmouse_findInterface(combo, sizeof(combo), 0, &rlen, &mps) == 0 && rlen == 0x3b && mps == 8,
		"config combo iface 0: rlen=%zu mps=%zu", rlen, mps);
	CHECK(hidmouse_findInterface(mouse, sizeof(mouse) - 5, 0, &rlen, &mps) < 0, "config truncated endpoint");

	memcpy(bad, mouse, sizeof(bad));
	bad[18] = 0x30; /* HID descriptor bLength past the end */
	CHECK(hidmouse_findInterface(bad, sizeof(bad), 0, &rlen, &mps) < 0, "config: overlong descriptor");
	bad[18] = 0x00; /* zero length must not loop forever */
	CHECK(hidmouse_findInterface(bad, sizeof(bad), 0, &rlen, &mps) < 0, "config: zero-length descriptor");
	printf("  config descriptor walk: %s\n", "ok");
}


/* Random descriptors and bit-flipped real ones under ASan/UBSan: the parser and
 * the translator must never read out of bounds, whatever the device sends. */
static void testFuzz(void)
{
	static const struct {
		const uint8_t *d;
		size_t len;
	} seeds[] = {
		{ pixart_093a_2510, sizeof(pixart_093a_2510) },
		{ logitech_unifying, sizeof(logitech_unifying) },
		{ gaming_16bit, sizeof(gaming_16bit) },
		{ mi_dongle, sizeof(mi_dongle) },
		{ ms_notebook_3000, sizeof(ms_notebook_3000) },
		{ hires_16bit_ids, sizeof(hires_16bit_ids) },
	};
	uint8_t *d;
	uint8_t rep[64];
	uint8_t out[HIDMOUSE_MAX_SPLIT * HIDMOUSE_PKT_SIZE];
	hidmouse_layout_t l;
	uint8_t btn = 0;
	unsigned int it, k, accepted = 0;
	size_t len, n;

	srand(12345);
	for (it = 0; it < 200000u; ++it) {
		if ((it & 1u) != 0u) {
			const unsigned int s = (unsigned int)rand() % (sizeof(seeds) / sizeof(seeds[0]));
			len = seeds[s].len;
			d = malloc(len);
			memcpy(d, seeds[s].d, len);
			for (k = 0; k < 1u + (unsigned int)rand() % 4u; ++k) {
				d[(size_t)rand() % len] ^= (uint8_t)(1u << (rand() % 8));
			}
			if ((rand() % 4) == 0) {
				len = (size_t)rand() % (len + 1u); /* truncate too */
			}
		}
		else {
			len = (size_t)rand() % 96u;
			d = malloc(len + 1u);
			for (k = 0; k < len; ++k) {
				d[k] = (uint8_t)rand();
			}
		}

		{
			size_t rl2, mps2;
			(void)hidmouse_findInterface(d, len, (unsigned int)rand() % 3u, &rl2, &mps2);
		}
		if (hidmouse_parse(d, len, &l, NULL) == 0) {
			accepted++;
			CHECK(l.x.size >= 2 && l.x.size <= 32 && l.y.size >= 2 && l.y.size <= 32, "fuzz: bad X/Y size");
			for (k = 0; k < 8u; ++k) {
				size_t rl = (size_t)rand() % (sizeof(rep) + 1u);
				uint8_t *r = malloc(rl + 1u);
				size_t j;
				for (j = 0; j < rl; ++j) {
					r[j] = (uint8_t)rand();
				}
				n = hidmouse_translate(&l, &btn, r, rl, out, sizeof(out));
				CHECK(n % 4u == 0u && n <= sizeof(out), "fuzz: translate returned %zu", n);
				free(r);
			}
			{
				char line[160];
				hidmouse_describe(&l, line, sizeof(line));
				hidmouse_describe(&l, line, 7); /* truncation is safe */
			}
		}
		free(d);
	}
	printf("  fuzz: 200000 descriptors, %u accepted, no OOB under ASan/UBSan\n", accepted);
}


int main(void)
{
	printf("usbmouse HID parser host test\n");
	testPixart();
	testLogitech();
	testGaming16();
	testMiDongle();
	testMicrosoft();
	testHires16();
	testMalformed();
	testConfig();
	testFuzz();

	printf("%s: %d checks, %d failures\n", (failures == 0) ? "PASS" : "FAIL", checks, failures);
	return (failures == 0) ? 0 : 1;
}
