/*
 * mouseprobe — minimal standalone HID-device read probe (no X server involved).
 *
 * Opens a Phoenix HID char device (default /dev/mouse0) O_NONBLOCK and, for ~30 s,
 * prints every packet it reads as hex. Used to isolate "the X mouse cursor doesn't
 * move" into one of:
 *   - packets DO arrive here  -> the device + USB path work; the X input driver's
 *     poll()-based fd wakeup is the culprit (Phoenix poll() not waking on the HID
 *     fd) -> fix = periodic non-blocking drain in the DDX input driver.
 *   - NO packets arrive        -> the USB mouse isn't delivering to /dev/mouse0
 *     (issue #24, a USB/usbmouse problem, upstream of X) -> the DDX can't help.
 *
 * Run from psh on its own (NOT while Xphoenix holds the device):
 *   /bin/mouseprobe            # probes /dev/mouse0
 *   /bin/mouseprobe /dev/kbd0  # (kbd0 is usually EBUSY: console holds it)
 * Move the mouse during the 30 s window.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv)
{
	const char *dev = (argc > 1) ? argv[1] : "/dev/mouse0";
	unsigned char buf[64];
	unsigned long total_bytes = 0, total_pkts = 0;
	int i, iter;

	int fd = open(dev, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf("mouseprobe: open %s FAILED: %s\n", dev, strerror(errno));
		return 1;
	}
	printf("mouseprobe: %s opened (fd=%d) — MOVE THE MOUSE for ~30s...\n", dev, fd);
	fflush(stdout);

	/* ~30 s: 1500 iterations x ~20 ms. read() is non-blocking, so on no-data we
	 * sleep; on data we print immediately and keep draining. */
	for (iter = 0; iter < 1500; iter++) {
		ssize_t r = read(fd, buf, sizeof(buf));
		if (r > 0) {
			total_bytes += (unsigned long)r;
			total_pkts++;
			printf("mouseprobe: +%ld bytes:", (long)r);
			for (i = 0; i < r && i < 16; i++)
				printf(" %02x", buf[i]);
			printf("\n");
			fflush(stdout);
			continue; /* drain fast while data is flowing */
		}
		usleep(20000); /* 20 ms */
		if ((iter % 250) == 249)
			printf("mouseprobe: waiting... (%lu pkts, %lu bytes so far)\n",
			       total_pkts, total_bytes);
	}

	printf("mouseprobe: DONE — %lu packets, %lu bytes from %s. %s\n",
	       total_pkts, total_bytes, dev,
	       total_bytes ? "DEVICE DELIVERS DATA (X poll-wakeup is the bug)"
	                   : "NO DATA (USB mouse delivery / #24 — upstream of X)");
	close(fd);
	return 0;
}
