/*
 * hevc_regs.h — BCM2711 rpivid / hevc_dec register map for the M2 decode core.
 *
 * Byte offsets from the HEVC block base (apb_read/apb_write = readl/writel(base + off),
 * no scaling). Address registers take phys>>6; length/stride registers take (bytes+63)>>6.
 * All offsets/constants cited to the in-tree Linux driver
 *   external/linux/drivers/media/platform/raspberrypi/hevc_dec/hevc_d_hw.h  (offsets)
 *   .../hevc_d_h265.c                                                       (packing/emit)
 * and captured in docs/inprogress/2026-08-28-hevc-m2-register-spec.md.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef HEVC_REGS_H
#define HEVC_REGS_H

/* ---- Block bases (ARM-physical; DT bus 0x7exxxxxx -> 0xfexxxxxx). ---- */
#define HEVC_BASE 0xfeb00000u
#define HEVC_SIZE 0x10000u
#define INTC_BASE 0xfeb10000u
#define INTC_SIZE 0x1000u

/* ---- Liveness. ---- */
#define RPI_VERSION     60u
#define HEVC_EXPECT_VER 0x202u

/* ================= Phase-1 registers — emitted into the command buffer ================= */
/* (Class A: written as u64 addr|(data<<32) entries the HW executes; NOT direct APB.) */
#define RPI_SPS0        0u
#define RPI_SPS1        4u
#define RPI_PPS         8u
#define RPI_SLICE       12u
#define RPI_TILESTART   16u
#define RPI_TILEEND     20u
#define RPI_SLICESTART  24u
#define RPI_MODE        28u
#define RPI_QP          48u
#define RPI_CONTROL     52u
#define RPI_STATUS      56u
#define RPI_BFBASE      64u
#define RPI_BFNUM       68u
#define RPI_BFCONTROL   72u
#define RPI_SLICECMDS   96u
#define RPI_BEGINTILEEND 100u
#define RPI_TRANSFER    104u
/* Arrays (base offsets; index scaled per element). */
#define RPI_PROBBASE    0x1000u   /* 40 CABAC init-prob words at +i */
#define RPI_SCALINGBASE 0x2000u   /* scaling factors (only if scaling_list enabled) */
#define RPI_SLICEMSGBASE 0x4000u  /* per-slice ref/weight/qp-offset msgs at +4*i */

/* RPI_MODE bits (h265.c:943). */
#define RPI_MODE_TILE       0xffffu
#define RPI_MODE_WPP        1u
#define RPI_MODE_LASTCOL    (1u << 17)
#define RPI_MODE_LASTROW    (1u << 18)

/* RPI_BFCONTROL bits (h265.c:584). */
#define RPI_BFCONTROL_STOP  (1u << 7)
#define RPI_BFCONTROL_EMU   (1u << 6)   /* emulation-prevention bytes present in bitstream */

/* RPI_TRANSFER CABAC context save/restore (h265.c:131,553). */
#define PROB_BACKUP  ((20u << 12) | (20u << 6))
#define PROB_RELOAD  ((20u << 12) | (20u << 0))

/* ================= Phase-1 kick — direct APB (Class B) ================= */
#define RPI_PUWBASE     80u
#define RPI_PUWSTRIDE   84u
#define RPI_COEFFWBASE  88u
#define RPI_COEFFWSTRIDE 92u
#define RPI_CFNUM       112u   /* count of u64 cmd entries (NOT >>6) */
#define RPI_CFBASE      108u   /* cmd_phys>>6 — apb_write_final STARTS PHASE 1 */
#define RPI_CFSTATUS    116u   /* read: phase-1 success <=> CFSTATUS == CFNUM */

/* RPI_STATUS phase-1 overflow bits (h265.c check_status). */
#define RPI_STATUS_COEFF_EXHAUSTED 8u
#define RPI_STATUS_PU_EXHAUSTED    16u

/* ================= Phase-2 registers — direct APB (Class B), offsets hevc_d_hw.h:58-85 ==== */
#define RPI_PURBASE     0x8000u
#define RPI_PURSTRIDE   0x8004u
#define RPI_COEFFRBASE  0x8008u
#define RPI_COEFFRSTRIDE 0x800Cu
#define RPI_NUMROWS     0x8010u   /* pic_height_in_ctbs_y — apb_write_final STARTS PHASE 2 */
#define RPI_CONFIG2     0x8014u
#define RPI_OUTYBASE    0x8018u
#define RPI_OUTYSTRIDE  0x801Cu
#define RPI_OUTCBASE    0x8020u
#define RPI_OUTCSTRIDE  0x8024u
#define RPI_FRAMESIZE   0x802Cu   /* (height<<16)|width in luma samples */
#define RPI_MVBASE      0x8030u
#define RPI_MVSTRIDE    0x8034u
#define RPI_COLBASE     0x8038u
#define RPI_COLSTRIDE   0x803Cu
#define RPI_CURRPOC     0x8040u
#define RPI_REFBASE     0x9000u   /* 16 slots x 16 bytes */
#define RPI_REFREGS_SIZE 16u      /* per-slot pitch */
#define RPI_REF_YBASE   0u        /* within a slot: +0 Ybase, +4 Ystride, +8 Cbase, +0xc Cstride */
#define RPI_REF_YSTRIDE 4u
#define RPI_REF_CBASE   8u
#define RPI_REF_CSTRIDE 12u

/* ================= ARGON interrupt controller (Class C) ================= */
#define ARG_IC_ICTRL    0u
#define ACTIVE1_EN_SET  (1u << 2)
#define ACTIVE2_EN_SET  (1u << 6)
#define ACTIVE1_INT_SET (1u << 0)   /* phase-1 done (latched, write-1-clear) */
#define ACTIVE2_INT_SET (1u << 4)   /* phase-2 done */
#define ALL_IRQ_MASK    (ACTIVE1_INT_SET | ACTIVE2_INT_SET)
#define SET_ZERO_MASK   ((0xffu << 12) | (1u << 11))  /* ack: write ictrl & ~SET_ZERO_MASK */

/* ---- IRQ: DT hevc_dec interrupts = <GIC_SPI 98 ...>; GIC SPIs base at 32. ---- */
#define HEVC_GIC_SPI 98u
#define HEVC_IRQ     (32u + HEVC_GIC_SPI)   /* = 130 */

/* ---- Address/length scaling helpers (apb_write_vc_addr/len; AXI_BASE64 = 0). ---- */
#define RPI_VC_ADDR(pa)  ((uint32_t)((uint64_t)(pa) >> 6))
#define RPI_VC_LEN(b)    ((uint32_t)(((uint64_t)(b) + 63u) >> 6))

#endif /* HEVC_REGS_H */
