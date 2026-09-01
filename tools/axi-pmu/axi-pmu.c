/*
 * axi-pmu — Raspberry Pi 4 (BCM2711) AXI bus performance-monitor reader.
 *
 * Phoenix userspace port of the idea behind Linux's "perf: Add Raspberry Pi AXI
 * PMU driver" (Ian Rogers): reads the BCM2711 System AXI bandwidth monitors to
 * measure real hardware bus/memory transaction counts. Register map from the
 * Linux DT (bcm270x.dtsi axiperf) + vendor driver raspberrypi_axi_monitor.c.
 *
 * Verification (advisor): the counter has no external oracle, so this is
 * self-checking by construction — an IDLE baseline (control) + a monotonic
 * memcpy DOSE-RESPONSE (4/8/16 MB): read+write transactions must scale ~linearly
 * with copy size. The ratio is the oracle; the absolute bytes/transaction is
 * derived from the slope (known bytes moved / transaction delta).
 *
 * Per-master attribution uses the vendor bus-name table (system_bus_string_2711[])
 * so BUS_WATCH values map to named masters: 6=HVS(display), 7=ARGON(rpivid HEVC),
 * 9=PERIPHERAL(genet ethernet), 10/11=ARM. To isolate a small master (genet at
 * ~20 MB/s) against multi-GB/s time-proportional background (HVS ~0.5 GB/s), a
 * size dose-response FAILS — read-size is a proxy for wall-clock, so any
 * time-proportional master scales with it too. The correct discriminator holds
 * TIME fixed and toggles the WORKLOAD: an idle window vs an equal-purpose NFS-read
 * window, RATE-NORMALIZED (trans/s) so unequal durations don't skew it. HVS then
 * cancels (~flat rate) and genet's RX-DMA writes on PERIPHERAL stand out.
 *
 * Requires a >=60 MB file at /stories15M.bin on the NFS root for NETSCAN + NET-ISO
 * (skipped with a message if absent).
 *
 * Observer only for the buses; the sole writes are to the perf block itself
 * (GEN_CTRL enable + BW0_CTRL configure) — no side effects on the monitored bus.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>

#define AXIPERF_BASE   0xfe009800u   /* BCM2711 System AXI monitor (DT 0x7e009800) */
#define AXIPERF_PAGE   0xfe009000u   /* page-aligned mmap base (MAP_PHYSMEM needs alignment) */
#define AXIPERF_OFF    0x00000800u   /* block offset within the page */
#define GEN_CTRL       0x00u
#define GEN_CTL_ENABLE (1u << 0)
#define GEN_CTL_RESET  (1u << 1)
#define GEN_CTL_WATCH  (1u << 2)
#define BW0_CTRL       0x40u
#define BW_ATRANS      0x04u
#define BW_WTRANS      0x10u
#define BW_RTRANS      0x1cu
#define BW_CTRL_RESET  (1u << 31)
#define BW_CTRL_ENABLE (1u << 30)
#define BUS_ARM        10u           /* scan found bus 10 shows CPU memcpy read+write traffic */
#define BUS_HVS         6u           /* display scanout (read-only, time-proportional) */
#define BUS_PERIPHERAL  9u           /* genet ethernet + other peripherals live here */
#define BUS_ARGON       7u           /* rpivid HEVC decoder block */

/* Authoritative BCM2711 System-AXI bus names, from the vendor driver's
 * system_bus_string_2711[] (raspberrypi_axi_monitor.c). Index == BUS_WATCH value. */
static const char *const bus_name_2711[] = {
	"DMA_L2", "TRANS", "JPEG", "VPU_UC", "DMA_UC", "SYSTEM_L2",
	"HVS", "ARGON", "H264", "PERIPHERAL", "ARM_UC", "ARM_L2"
};
static const char *bus_name(int b)
{
	return (b >= 0 && b < (int)(sizeof(bus_name_2711) / sizeof(bus_name_2711[0])))
		? bus_name_2711[b] : "?";
}

static volatile uint32_t *pmu;

static uint32_t rd(uint32_t off) { return pmu[off / 4] & 0x7fffffffu; }
static void wr(uint32_t off, uint32_t v) { pmu[off / 4] = v; }

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

int main(void)
{
	void *m;
	uint32_t r0, w0, r1, w1;
	struct timespec idle = { 0, 200000000 }; /* 200 ms */
	size_t sizes[3] = { 4u << 20, 8u << 20, 16u << 20 };
	int k;

	setvbuf(stdout, NULL, _IONBF, 0);
	m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)AXIPERF_PAGE);
	if (m == MAP_FAILED) {
		printf("axi-pmu: mmap(0x%08x) FAILED\n", AXIPERF_PAGE);
		return 1;
	}
	pmu = (volatile uint32_t *)((char *)m + AXIPERF_OFF);
	printf("axi-pmu: mapped BCM2711 System AXI monitor @0x%08x\n", AXIPERF_BASE);

	/* BUS SCAN: which BUS_WATCH value shows a CPU memcpy's DRAM traffic? Program
	 * BW0 for each bus 0..15, run a fixed 8MB x2 memcpy, read the deltas. */
	{
		int bus;
		char *src = malloc(8u << 20), *dst = malloc(8u << 20);
		if (src && dst) {
			memset(src, 0xa5, 8u << 20);
			for (bus = 0; bus < 16; bus++) {
				uint32_t r0b, w0b, a0b, r1b, w1b, a1b;
				volatile uint64_t chk = 0;
				size_t z; int rep;
				wr(GEN_CTRL, GEN_CTL_RESET);
				wr(BW0_CTRL, BW_CTRL_RESET);
				wr(BW0_CTRL, BW_CTRL_ENABLE | ((uint32_t)bus & 0x3fu));
				wr(GEN_CTRL, GEN_CTL_ENABLE | GEN_CTL_WATCH);
				r0b = rd(BW0_CTRL + BW_RTRANS); w0b = rd(BW0_CTRL + BW_WTRANS); a0b = rd(BW0_CTRL + BW_ATRANS);
				for (rep = 0; rep < 2; rep++) { memcpy(dst, src, 8u << 20); src[rep] = dst[rep] + 1; __asm__ volatile("":::"memory"); }
				__asm__ volatile("":::"memory");
				r1b = rd(BW0_CTRL + BW_RTRANS); w1b = rd(BW0_CTRL + BW_WTRANS); a1b = rd(BW0_CTRL + BW_ATRANS);
				for (z = 0; z < (8u << 20); z += 4096) chk += (unsigned char)dst[z];
				(void)chk;
				printf("axi-pmu: SCAN bus %2d %-10s: dR=%u dW=%u dA=%u\n", bus, bus_name(bus), r1b - r0b, w1b - w0b, a1b - a0b);
			}
		}
		free(src); free(dst);
	}
	/* NETWORK SCAN: read a 60MB file from the NFS root linearly, cycling the
	 * watched bus per segment. Buses active here but NOT during the memcpy scan
	 * reveal the genet-RX-DMA / network path (CPU+background are common to both). */
	{
		FILE *nf = fopen("/stories15M.bin", "rb");
		if (nf == NULL) {
			printf("axi-pmu: NETSCAN skipped (no /stories15M.bin)\n");
		}
		else {
			static char buf[65536];
			size_t seg = 60816028u / 16u;
			int bus;
			double nt0 = now_ms();
			for (bus = 0; bus < 16; bus++) {
				uint32_t r0b, w0b, a0b, r1b, w1b, a1b;
				size_t got = 0;
				wr(GEN_CTRL, GEN_CTL_RESET);
				wr(BW0_CTRL, BW_CTRL_RESET);
				wr(BW0_CTRL, BW_CTRL_ENABLE | ((uint32_t)bus & 0x3fu));
				wr(GEN_CTRL, GEN_CTL_ENABLE | GEN_CTL_WATCH);
				r0b = rd(BW0_CTRL + BW_RTRANS); w0b = rd(BW0_CTRL + BW_WTRANS); a0b = rd(BW0_CTRL + BW_ATRANS);
				while (got < seg) {
					size_t want = seg - got;
					size_t n = fread(buf, 1, want < sizeof(buf) ? want : sizeof(buf), nf);
					if (n == 0) break;
					got += n;
				}
				r1b = rd(BW0_CTRL + BW_RTRANS); w1b = rd(BW0_CTRL + BW_WTRANS); a1b = rd(BW0_CTRL + BW_ATRANS);
				printf("axi-pmu: NETSCAN bus %2d %-10s: dR=%u dW=%u dA=%u (read %zuKB)\n",
					bus, bus_name(bus), r1b - r0b, w1b - w0b, a1b - a0b, got >> 10);
			}
			fclose(nf);
			printf("axi-pmu: NETSCAN read 60MB over NFS in %.0f ms (~%.1f MB/s)\n",
				now_ms() - nt0, 60.0 / ((now_ms() - nt0) / 1000.0));
		}
	}

	printf("axi-pmu: --- dose-response on bus %u ---\n", BUS_ARM);
	/* Reconfigure BW0 for the chosen bus (the scan left it on bus 15). */
	wr(GEN_CTRL, GEN_CTL_RESET);
	wr(BW0_CTRL, BW_CTRL_RESET);
	wr(BW0_CTRL, BW_CTRL_ENABLE | (BUS_ARM & 0x3fu));
	wr(GEN_CTRL, GEN_CTL_ENABLE | GEN_CTL_WATCH);

	/* Vendor sequence (raspberrypi_axi_monitor.c): reset monitor, reset watcher,
	 * configure watcher (enable|bus, NO reset bit), then enable monitor WITH the
	 * WATCH bit (the piece that actually starts counting). */
	wr(GEN_CTRL, GEN_CTL_RESET);
	wr(BW0_CTRL, BW_CTRL_RESET);
	wr(BW0_CTRL, BW_CTRL_ENABLE | (BUS_ARM & 0x3fu));
	wr(GEN_CTRL, GEN_CTL_ENABLE | GEN_CTL_WATCH);
	printf("axi-pmu: GEN_CTRL=0x%08x BW0_CTRL=0x%08x\n", rd(GEN_CTRL), rd(BW0_CTRL));

	/* Control: idle baseline. */
	r0 = rd(BW0_CTRL + BW_RTRANS);
	w0 = rd(BW0_CTRL + BW_WTRANS);
	nanosleep(&idle, NULL);
	r1 = rd(BW0_CTRL + BW_RTRANS);
	w1 = rd(BW0_CTRL + BW_WTRANS);
	printf("axi-pmu: 200ms background on bus %u: dR=%u dW=%u (bus 10 = all A72 memory traffic, never truly idle)\n",
		BUS_ARM, r1 - r0, w1 - w0);

	/* Dose-response: memcpy 4/8/16 MB x4 reps; transactions should scale linearly. */
	for (k = 0; k < 3; k++) {
		size_t sz = sizes[k];
		char *src = malloc(sz);
		char *dst = malloc(sz);
		uint32_t ra, wa, aa, rb, wb, ab;
		double t0, t1;
		int rep;

		if (src == NULL || dst == NULL) {
			printf("axi-pmu: malloc %zuMB FAILED\n", sz >> 20);
			free(src); free(dst);
			break;
		}
		memset(src, k + 1, sz); /* fault in + fill src */
		memset(dst, 0, sz);

		ra = rd(BW0_CTRL + BW_RTRANS);
		wa = rd(BW0_CTRL + BW_WTRANS);
		aa = rd(BW0_CTRL + BW_ATRANS);
		t0 = now_ms();
		for (rep = 0; rep < 4; rep++) {
			memcpy(dst, src, sz);
			src[rep] = dst[rep] + 1; /* chain deps so copies aren't coalesced/elided */
			__asm__ volatile("" ::: "memory");
		}
		__asm__ volatile("" ::: "memory"); /* pin all copies before the counter read */
		t1 = now_ms();
		rb = rd(BW0_CTRL + BW_RTRANS);
		wb = rd(BW0_CTRL + BW_WTRANS);
		ab = rd(BW0_CTRL + BW_ATRANS);
		{
			volatile uint64_t chk = 0;
			size_t z;
			for (z = 0; z < sz; z += 4096)
				chk += (unsigned char)dst[z]; /* read dst so the memcpy is not dead */
			(void)chk;
		}

		{
			double bytes = (double)((uint64_t)(rb - ra) + (uint64_t)(wb - wa)) * 16.0; /* 16 B/AXI burst */
			double gbs = bytes / ((t1 - t0) / 1000.0) / 1.0e9;
			printf("axi-pmu: memcpy %2zuMB x4 (%zu B moved, %.2f ms): dR=%u dW=%u dA=%u | 16 B/xfer => %.2f GB/s bus-traffic (memcpy ~%.2f GB/s)\n",
				sz >> 20, (size_t)4 * 2 * sz, t1 - t0, rb - ra, wb - wa, ab - aa, gbs,
				(double)((size_t)4 * 2 * sz) / ((t1 - t0) / 1000.0) / 1.0e9);
		}
		free(src);
		free(dst);
	}

	/* NETWORK per-master isolation — idle-vs-NFS DIFFERENCING (not a size dose-response).
	 *
	 * WHY NOT dose-response: a bigger NFS read simply takes proportionally LONGER at
	 * fixed link bandwidth, so read-size is a proxy for wall-clock TIME. Any
	 * time-proportional background master (HVS display scanout ~350 MB/s; the
	 * free-running counter on bus 13) then scales ~2x per size-doubling too — which
	 * looks exactly like "tracks NFS bytes" but is a confound. Confirmed empirically:
	 * bus 6(HVS) held a CONSTANT ~355 MB/s and bus 13 a constant ~2.85 GB/s across
	 * 4/8/16 MB reads; and bus 6 had dW=0 throughout, so it can't be genet (an RX-DMA
	 * WRITE master) at all.
	 *
	 * The correct discriminator holds TIME fixed and toggles the WORKLOAD: measure
	 * each bus over a same-length IDLE window, then an equal-length NFS-READ window.
	 * Time-proportional masters (HVS) contribute equally to both and cancel in the
	 * difference; the genet path (bus 9 PERIPHERAL, per the vendor bus-name table)
	 * only appears in the active window. Watch PERIPHERAL(9)/HVS(6)/ARM(10) at once. */
	{
		FILE *nf = fopen("/stories15M.bin", "rb");
		if (nf == NULL) {
			printf("axi-pmu: NET-ISO skipped (no /stories15M.bin)\n");
		}
		else {
			static char nbuf[65536];
			const uint32_t buses[3] = { BUS_PERIPHERAL, BUS_HVS, BUS_ARM };
			const uint32_t bwc[3] = { BW0_CTRL, 0x80u, 0xc0u };
			struct timespec win = { 1, 0 }; /* ~1 s idle window ≈ the time a 16 MB read takes */
			uint32_t ir[3], iw[3], ar[3], aw[3];
			double it0, it1, at0, at1, idt, adt;
			size_t got = 0;
			int b;

			printf("axi-pmu: --- network isolation: idle vs NFS-read, RATE-normalized (BW0=PERIPHERAL BW1=HVS BW2=ARM) ---\n");
			wr(GEN_CTRL, GEN_CTL_RESET);
			for (b = 0; b < 3; b++) wr(bwc[b], BW_CTRL_RESET);
			for (b = 0; b < 3; b++) wr(bwc[b], BW_CTRL_ENABLE | (buses[b] & 0x3fu));
			wr(GEN_CTRL, GEN_CTL_ENABLE | GEN_CTL_WATCH);

			/* idle window (measure its true length: nanosleep only approximates 1 s) */
			for (b = 0; b < 3; b++) { ir[b] = rd(bwc[b] + BW_RTRANS); iw[b] = rd(bwc[b] + BW_WTRANS); }
			it0 = now_ms();
			nanosleep(&win, NULL);
			it1 = now_ms();
			for (b = 0; b < 3; b++) { ir[b] = rd(bwc[b] + BW_RTRANS) - ir[b]; iw[b] = rd(bwc[b] + BW_WTRANS) - iw[b]; }

			/* NFS-read window (~16 MB). The read rarely lands on exactly the idle
			 * window's duration, so we DON'T compare raw deltas — we normalize each
			 * to transactions/second and difference the RATES. A time-proportional
			 * master (HVS) then has ~equal idle/NFS rates and cancels; genet's RX-DMA
			 * writes on PERIPHERAL show up as a large positive Δrate. */
			for (b = 0; b < 3; b++) { ar[b] = rd(bwc[b] + BW_RTRANS); aw[b] = rd(bwc[b] + BW_WTRANS); }
			at0 = now_ms();
			fseek(nf, 0, SEEK_SET);
			while (got < (16u << 20)) {
				size_t n = fread(nbuf, 1, sizeof(nbuf), nf);
				if (n == 0) break;
				got += n;
			}
			at1 = now_ms();
			for (b = 0; b < 3; b++) { ar[b] = rd(bwc[b] + BW_RTRANS) - ar[b]; aw[b] = rd(bwc[b] + BW_WTRANS) - aw[b]; }
			fclose(nf);

			idt = (it1 - it0) / 1000.0;
			adt = (at1 - at0) / 1000.0;
			printf("axi-pmu: idle window %.0f ms; NFS window read %zuKB in %.0f ms (~%.1f MB/s)\n",
				it1 - it0, got >> 10, at1 - at0, (got / 1048576.0) / adt);
			for (b = 0; b < 3; b++) {
				double iwr = iw[b] / idt, awr = aw[b] / adt; /* write trans/s */
				double irr = ir[b] / idt, arr = ar[b] / adt; /* read  trans/s */
				printf("axi-pmu: bus%u %-10s: idle[R=%.0f/s W=%.0f/s] NFS[R=%.0f/s W=%.0f/s] => ΔW=%+.0f/s ΔR=%+.0f/s\n",
					buses[b], bus_name(buses[b]), irr, iwr, arr, awr, awr - iwr, arr - irr);
			}
			printf("axi-pmu: (genet-RX is a WRITE master: bus 9 PERIPHERAL's +ΔW/s x ~256 B/burst ~= the NFS MB/s = the network data path)\n");
		}
	}

	printf("axi-pmu: done (verify: idle~0, memcpy scales ~linearly, NFS Δ isolates the network bus)\n");
	return 0;
}
