/*
 * v3d_phoenix_power.c — self-contained BCM2711 V3D power-on for the Phoenix winsys.
 *
 * Ported verbatim from the HW-proven rpi4-v3d-scout power-on (manifest
 * 2026-06-10-v3d-power-on-alive): mailbox SET_QPU_ENABLE + SET_DOMAIN_STATE +
 * SET_CLOCK_STATE, then the canonical bcm2835_asb_power_on (clock-toggle around a
 * PM_V3DRSTN deassert, then enable the V3D master+slave async-AXI bridges on
 * rpivid_asb). Without this the V3D core MMIO reads 0xdeadbeef (unpowered) and
 * SUBMIT_CL never executes. Idempotent — safe to call once at winsys_init.
 *
 * The winsys owns power-on itself (rather than depending on a separate scout
 * process) to avoid a race: a concurrent scout's clock-toggle/reset overlapping
 * our submit window left core0 reading 0xdeadbeef.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

/* firmware mailbox (property channel) */
#define RPI_MAILBOX_BASE        0xfe00b880u
#define VC_MBOX_STATUS          0x18u
#define VC_MBOX_WRITE           0x20u
#define VC_MBOX_READ            0x00u
#define VC_MBOX_STATUS_FULL     0x80000000u
#define VC_MBOX_STATUS_EMPTY    0x40000000u
#define VC_MBOX_RESP_OK         0x80000000u
#define VC_MBOX_PROP_CHANNEL    8u
#define MBOX_FAIL               0xffffffffu
#define MBOX_SPINS              4000000u
#define VC_PROP_SET_QPU_ENABLE   0x00030012u
#define VC_PROP_SET_DOMAIN_STATE 0x00038030u
#define RPI_POWER_DOMAIN_V3D     10u
#define VC_PROP_SET_CLOCK_STATE  0x00038001u
#define RPI_CLOCK_V3D            5u
/* PM + rpivid_asb (the BCM2711 V3D power/reset path) */
#define PM_BASE                 0xfe100000u
#define RPIVID_ASB_BASE         0xfec11000u
#define PM_GRAFX                0x10cu
#define PM_V3DRSTN              (1u << 6)
#define ASB_V3D_S_CTRL          0x08u
#define ASB_V3D_M_CTRL          0x0cu
#define ASB_REQ_STOP            (1u << 0)
#define ASB_ACK                 (1u << 1)
#define PM_PASSWORD             0x5a000000u
#define ASB_ACK_SPINS           100000u

static uint32_t mboxProp(uint32_t tag, int nw, uint32_t w0, uint32_t w1)
{
	addr_t pa_base = (addr_t)RPI_MAILBOX_BASE & ~(addr_t)(_PAGE_SIZE - 1);
	addr_t pa_offs = (addr_t)RPI_MAILBOX_BASE & (addr_t)(_PAGE_SIZE - 1);
	volatile uint32_t *mbox;
	uint32_t *msg;
	uintptr_t msg_pa;
	uint32_t request, result = MBOX_FAIL, spins;
	void *mbox_page, *msg_page;

	mbox_page = mmap(NULL, _PAGE_SIZE, PROT_READ | PROT_WRITE,
		MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, pa_base);
	if (mbox_page == MAP_FAILED)
		return MBOX_FAIL;
	mbox = (volatile uint32_t *)((volatile uint8_t *)mbox_page + pa_offs);

	msg_page = mmap(NULL, _PAGE_SIZE, PROT_READ | PROT_WRITE,
		MAP_UNCACHED | MAP_CONTIGUOUS | MAP_ANONYMOUS, -1, 0);
	if (msg_page == MAP_FAILED) {
		munmap(mbox_page, _PAGE_SIZE);
		return MBOX_FAIL;
	}
	msg = msg_page;
	msg[0] = (uint32_t)(6 + nw) * 4u;
	msg[1] = 0;
	msg[2] = tag;
	msg[3] = (uint32_t)nw * 4u;
	msg[4] = 0;
	msg[5] = w0;
	if (nw > 1)
		msg[6] = w1;
	msg[5 + nw] = 0;

	msg_pa = (uintptr_t)va2pa(msg);
	if (msg_pa == (uintptr_t)-1) {
		munmap(msg_page, _PAGE_SIZE);
		munmap(mbox_page, _PAGE_SIZE);
		return MBOX_FAIL;
	}
	request = ((uint32_t)msg_pa & ~0xFu) | VC_MBOX_PROP_CHANNEL;

	for (spins = MBOX_SPINS; (mbox[VC_MBOX_STATUS / 4] & VC_MBOX_STATUS_FULL) != 0u; spins--) {
		if (spins == 0u) {
			munmap(msg_page, _PAGE_SIZE);
			munmap(mbox_page, _PAGE_SIZE);
			return MBOX_FAIL;
		}
	}
	mbox[VC_MBOX_WRITE / 4] = request;
	for (spins = MBOX_SPINS; spins != 0u; spins--) {
		if ((mbox[VC_MBOX_STATUS / 4] & VC_MBOX_STATUS_EMPTY) == 0u &&
		    mbox[VC_MBOX_READ / 4] == request)
			break;
	}
	if (spins != 0u && msg[1] == VC_MBOX_RESP_OK)
		result = msg[5 + nw - 1];

	munmap(msg_page, _PAGE_SIZE);
	munmap(mbox_page, _PAGE_SIZE);
	return result;
}

static int asbEnable(volatile uint32_t *asb, uint32_t reg)
{
	uint32_t val = asb[reg / 4] & ~ASB_REQ_STOP;
	uint32_t spins;
	asb[reg / 4] = PM_PASSWORD | val;
	for (spins = ASB_ACK_SPINS; spins != 0u; spins--) {
		if ((asb[reg / 4] & ASB_ACK) == 0u)
			return 0;
	}
	return -1;
}

/* Full HW-proven V3D power-on. Returns 0 on success (both ASB bridges ACK). */
int v3d_phoenix_powerOn(void)
{
	volatile uint32_t *pm, *asb;
	void *pm_page, *asb_page;
	uint32_t grafx;
	int rcM, rcS;

	/* firmware-side enables (QPU + power domain + clock) */
	(void)mboxProp(VC_PROP_SET_QPU_ENABLE, 1, 1u, 0u);
	(void)mboxProp(VC_PROP_SET_DOMAIN_STATE, 2, RPI_POWER_DOMAIN_V3D, 1u);
	(void)mboxProp(VC_PROP_SET_CLOCK_STATE, 2, RPI_CLOCK_V3D, 1u);

	pm_page = mmap(NULL, _PAGE_SIZE, PROT_READ | PROT_WRITE,
		MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (addr_t)PM_BASE);
	if (pm_page == MAP_FAILED)
		return -1;
	pm = (volatile uint32_t *)pm_page;
	asb_page = mmap(NULL, _PAGE_SIZE, PROT_READ | PROT_WRITE,
		MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (addr_t)RPIVID_ASB_BASE);
	if (asb_page == MAP_FAILED) {
		munmap(pm_page, _PAGE_SIZE);
		return -1;
	}
	asb = (volatile uint32_t *)asb_page;

	/* clock-toggle around the reset deassert (canonical bcm2835_asb_power_on) */
	(void)mboxProp(VC_PROP_SET_CLOCK_STATE, 2, RPI_CLOCK_V3D, 1u);
	usleep(50);
	(void)mboxProp(VC_PROP_SET_CLOCK_STATE, 2, RPI_CLOCK_V3D, 0u);
	grafx = pm[PM_GRAFX / 4];
	pm[PM_GRAFX / 4] = PM_PASSWORD | (grafx | PM_V3DRSTN);
	(void)mboxProp(VC_PROP_SET_CLOCK_STATE, 2, RPI_CLOCK_V3D, 1u);
	usleep(50);

	rcM = asbEnable(asb, ASB_V3D_M_CTRL);
	rcS = asbEnable(asb, ASB_V3D_S_CTRL);
	printf("v3d_phoenix_powerOn: PM_GRAFX 0x%08x->0x%08x asb M=%s S=%s\n",
	       grafx, pm[PM_GRAFX / 4], rcM ? "TIMEOUT" : "ok", rcS ? "TIMEOUT" : "ok");
	usleep(2000);
	munmap(asb_page, _PAGE_SIZE);
	munmap(pm_page, _PAGE_SIZE);
	return (rcM == 0 && rcS == 0) ? 0 : -1;
}
