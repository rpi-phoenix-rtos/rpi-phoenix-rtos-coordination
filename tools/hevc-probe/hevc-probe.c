/*
 * hevc-probe — Raspberry Pi 4 (BCM2711) HEVC/H.265 hardware decoder M0 bring-up
 * smoke test for Phoenix-RTOS.
 *
 * Goal: prove the "rpivid" / hevc_dec decoder block is reachable from Phoenix
 * over plain MMIO + the VideoCore property mailbox, WITHOUT VCHIQ. This is the
 * first step (M0) of the G-FFMPEG-HW hardware-decode thrust.
 *
 * What it does, mirroring Linux's rpivid hevc_d_hw.c hw_setup():
 *   1. Enable the HEVC clock through the VideoCore firmware mailbox (property
 *      channel 8) BEFORE touching the block's APB registers — reading an
 *      unclocked APB block hangs the bus. GET_MAX_CLOCK_RATE(id 11) then
 *      SET_CLOCK_STATE(id 11, on) + SET_CLOCK_RATE(id 11, max). If the clock
 *      cannot be enabled, exit WITHOUT mapping or reading the block.
 *   2. physmem-map (uncached device MMIO) the HEVC register block at PA
 *      0xfeb00000 (0x10000) and the ARGON INTC block at PA 0xfeb10000 (0x1000).
 *   3. Read the version register readl(base_h265 + RPI_VERSION[=60]); Linux
 *      hw_setup rejects anything but 0x202, so 0x202 == "HW reachable + alive".
 *   4. Enable the ARGON interrupt controller exactly as hw_setup does
 *      (irq_write(ARG_IC_ICTRL, ACTIVE1_EN_SET | ACTIVE2_EN_SET); read back;
 *      write the read-back value to clear pending) — a second liveness check:
 *      the h/w only latches IRQ status bits once the enables are set.
 *
 * All register facts verified against the in-tree Linux clone:
 *   drivers/media/platform/raspberrypi/hevc_dec/hevc_d_hw.{c,h}
 *     RPI_VERSION 60, ver != 0x202 rejected (hw_setup);
 *     ARG_IC_ICTRL 0, ACTIVE1_EN_SET BIT(2), ACTIVE2_EN_SET BIT(6);
 *     apb_read/irq_read == readl(base + offset) (plain byte offsets, no scaling).
 *   arch/arm/boot/dts/broadcom/bcm2711.dtsi: hevc 0x7eb00000/0x10000,
 *     intc 0x7eb10000/0x1000 (bus 0x7exxxxxx -> ARM 0xfexxxxxx).
 *   include/soc/bcm2835/raspberrypi-firmware.h: RPI_FIRMWARE_HEVC_CLK_ID == 11.
 *
 * Mailbox traffic is routed through the serializing /dev/vcmbox server via
 * libvcmbox (the project convention — the single BCM2711 mailbox FIFO has no
 * hardware arbitration, so drivers must not drive it directly). The physmem
 * mmap idiom is the one used by every Phoenix Pi4 MMIO driver/probe
 * (MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS).
 *
 * Exit codes: 0 = version 0x202 (HW reachable); 1 = version mismatch;
 *             2 = mailbox / clock enable failure (block NOT touched);
 *             3 = physmem mmap failure.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#include "libvcmbox.h"

/* VideoCore firmware property tags (mailbox channel 8). */
#define VC_GET_MAX_CLOCK_RATE 0x00030004u /* in: {clk_id};        out: {clk_id, rate_hz}         */
#define VC_SET_CLOCK_STATE    0x00038001u /* in: {clk_id, state}; out: {clk_id, state}           */
#define VC_SET_CLOCK_RATE     0x00038002u /* in: {clk_id, rate, skip_turbo}; out: {clk_id, rate} */
#define VC_GET_CLOCK_RATE     0x00030002u /* in: {clk_id};        out: {clk_id, rate_hz}         */

#define RPI_FIRMWARE_HEVC_CLK_ID 11u /* raspberrypi-firmware.h enum rpi_firmware_clk_id */

/* HEVC decoder block: ARM-physical bases (DT bus 0x7exxxxxx -> 0xfexxxxxx). */
#define HEVC_BASE 0xfeb00000u
#define HEVC_SIZE 0x10000u
#define INTC_BASE 0xfeb10000u
#define INTC_SIZE 0x1000u

/* Register offsets (byte offsets; apb_read/irq_read = readl(base + offset)). */
#define RPI_VERSION       60u    /* hevc_d_hw.h: version reg; hw_setup expects 0x202 */
#define HEVC_EXPECT_VER   0x202u
#define ARG_IC_ICTRL      0u     /* ARGON interrupt-controller control reg */
#define ACTIVE1_EN_SET    (1u << 2)
#define ACTIVE2_EN_SET    (1u << 6)


static inline uint32_t readl(const volatile void *addr)
{
	return *(const volatile uint32_t *)addr;
}


static inline void writel(volatile void *addr, uint32_t val)
{
	*(volatile uint32_t *)addr = val;
}


/* Map one BCM2711 MMIO block as uncached device memory. base/size must be
 * page-aligned (all our blocks are). Returns the mapping or MAP_FAILED. */
static void *map_block(uint32_t base, uint32_t size)
{
	return mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)base);
}


/* Enable the HEVC clock via the firmware mailbox. Returns 0 on success (clock
 * on at a non-zero rate), negative on any mailbox failure. On success stores the
 * configured rate in *rate_out. MUST succeed before any APB access. */
static int hevc_clock_enable(uint32_t *rate_out)
{
	uint32_t in[3];
	uint32_t out[2] = { 0u, 0u };
	uint32_t max_rate = 0u, set_rate = 0u, cfg_rate = 0u;
	int rc;

	/* 1) Query the firmware's max HEVC clock rate. */
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	rc = vcmbox_call(VC_GET_MAX_CLOCK_RATE, 8u, in, 1u, out, 2u);
	if (rc != 0) {
		printf("hevc-probe: GET_MAX_CLOCK_RATE(id 11) mailbox call failed (rc=%d)\n", rc);
		return -1;
	}
	max_rate = out[1];
	printf("hevc-probe: HEVC max clock rate = %u Hz (%.1f MHz)\n",
		max_rate, (double)max_rate / 1.0e6);
	if (max_rate == 0u) {
		printf("hevc-probe: firmware reports max rate 0 — cannot enable HEVC clock\n");
		return -1;
	}

	/* 2) Explicitly turn the clock ON (state bit0=1). Belt-and-suspenders: a
	 * non-zero SET_CLOCK_RATE also enables it, but enabling state first is the
	 * safe order and idempotent. */
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	in[1] = 1u; /* on */
	rc = vcmbox_call(VC_SET_CLOCK_STATE, 8u, in, 2u, out, 2u);
	if (rc != 0) {
		printf("hevc-probe: SET_CLOCK_STATE(id 11, on) mailbox call failed (rc=%d)\n", rc);
		return -1;
	}
	printf("hevc-probe: SET_CLOCK_STATE(id 11, on) -> state=0x%x\n", out[1]);

	/* 3) Set the clock to its max rate (skip_setting_turbo=0). */
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	in[1] = max_rate;
	in[2] = 0u;
	rc = vcmbox_call(VC_SET_CLOCK_RATE, 12u, in, 3u, out, 2u);
	if (rc != 0) {
		printf("hevc-probe: SET_CLOCK_RATE(id 11, %u) mailbox call failed (rc=%d)\n", max_rate, rc);
		return -1;
	}
	set_rate = out[1];
	printf("hevc-probe: SET_CLOCK_RATE(id 11, %u) -> %u Hz (%.1f MHz)\n",
		max_rate, set_rate, (double)set_rate / 1.0e6);

	/* 4) Read back the configured rate as final confirmation. */
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	rc = vcmbox_call(VC_GET_CLOCK_RATE, 8u, in, 1u, out, 2u);
	if (rc == 0) {
		cfg_rate = out[1];
		printf("hevc-probe: GET_CLOCK_RATE(id 11) -> %u Hz (%.1f MHz)\n",
			cfg_rate, (double)cfg_rate / 1.0e6);
	}

	*rate_out = (cfg_rate != 0u) ? cfg_rate : set_rate;
	if (*rate_out == 0u) {
		printf("hevc-probe: HEVC clock still reads 0 Hz after enable — refusing to touch APB\n");
		return -1;
	}
	return 0;
}


int main(void)
{
	void *hevc_page, *intc_page;
	volatile uint8_t *hevc, *intc;
	uint32_t rate = 0u, ver, ictrl;

	/* Unbuffered so every step lands on the UART even if a later access hangs. */
	setvbuf(stdout, NULL, _IONBF, 0);

	printf("hevc-probe: BCM2711 HEVC/H.265 decoder M0 bring-up smoke test\n");

	/* STEP 1 — clock first. If this fails, do NOT map/read the block. */
	printf("hevc-probe: step 1 — enabling HEVC clock via VideoCore mailbox\n");
	if (hevc_clock_enable(&rate) != 0) {
		printf("hevc-probe: clock enable FAILED — aborting (block left untouched)\n");
		return 2;
	}
	printf("hevc-probe: HEVC clock enabled (%u Hz) — safe to access APB\n", rate);

	/* STEP 2 — map the register blocks. */
	printf("hevc-probe: step 2 — mapping HEVC @0x%08x (0x%x) + INTC @0x%08x (0x%x)\n",
		HEVC_BASE, HEVC_SIZE, INTC_BASE, INTC_SIZE);
	hevc_page = map_block(HEVC_BASE, HEVC_SIZE);
	if (hevc_page == MAP_FAILED) {
		printf("hevc-probe: mmap HEVC block @0x%08x FAILED\n", HEVC_BASE);
		return 3;
	}
	intc_page = map_block(INTC_BASE, INTC_SIZE);
	if (intc_page == MAP_FAILED) {
		printf("hevc-probe: mmap INTC block @0x%08x FAILED\n", INTC_BASE);
		munmap(hevc_page, HEVC_SIZE);
		return 3;
	}
	hevc = (volatile uint8_t *)hevc_page;
	intc = (volatile uint8_t *)intc_page;
	printf("hevc-probe: mapped both blocks (uncached device MMIO)\n");

	/* STEP 3 — read the version register (the liveness oracle). */
	printf("hevc-probe: step 3 — reading version register (offset %u)\n", RPI_VERSION);
	ver = readl(hevc + RPI_VERSION);
	printf("hevc-probe: RPI_VERSION = 0x%x (expected 0x%x)\n", ver, HEVC_EXPECT_VER);

	/* STEP 4 — enable the ARGON INTC and clear pending, mirroring hw_setup. */
	printf("hevc-probe: step 4 — enabling ARGON INTC (ACTIVE1_EN | ACTIVE2_EN = 0x%x)\n",
		ACTIVE1_EN_SET | ACTIVE2_EN_SET);
	writel(intc + ARG_IC_ICTRL, ACTIVE1_EN_SET | ACTIVE2_EN_SET);
	ictrl = readl(intc + ARG_IC_ICTRL);
	printf("hevc-probe: ARG_IC_ICTRL read-back = 0x%08x\n", ictrl);
	writel(intc + ARG_IC_ICTRL, ictrl); /* write back to clear latched pending bits */
	printf("hevc-probe: ARG_IC_ICTRL cleared (wrote back 0x%08x)\n", ictrl);

	munmap(intc_page, INTC_SIZE);
	munmap(hevc_page, HEVC_SIZE);

	/* Verdict. */
	if (ver == HEVC_EXPECT_VER) {
		printf("hevc-probe: HEVC HW REACHABLE (ver=0x%x)\n", ver);
		return 0;
	}
	printf("hevc-probe: HEVC version MISMATCH (got 0x%x, want 0x%x) — block not confirmed\n",
		ver, HEVC_EXPECT_VER);
	return 1;
}
