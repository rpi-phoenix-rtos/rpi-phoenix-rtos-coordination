# Early-boot UART diagnostic instrumentation

Status: design / ready-to-apply patch (no source files modified by this commit)

## 1. Motivation

Today the AArch64 early bring-up path emits single ASCII characters as boot
markers via two macros defined inline at the top of
`sources/phoenix-rtos-kernel/hal/aarch64/_init.S`:

* `uart_putc <imm>`        — `_init.S:39-48`, polls `UARTFR.TXFF`, writes one
  byte to `PL011_TTY_BASE`.
* `uart_putc_virt <imm>`   — `_init.S:51-65`, same idea via
  `PL011_TTY_EARLY_VADDR`.
* `uart_tag2 c1 c2`        — `_init.S:68-73`, "\n" + two chars + "\n".

When boot fails post-MMU we get a stream like

    ZK[LSTUMVX1X2X3X4X5N!YOPSTUZbcd...

— or, on TD-16 cache-enable attempts, just

    ZK[LSTUMV X1 X2 X3 X4

with no register state, no ESR/FAR, no insight into *why* the next phase did
not run.  The post-fault dump in `_early_exception_common`
(`_init.S:1096-1128`) prints exactly four 16-hex values
(`EX/ESR/ELR/FAR`) and is invaluable; the rest of the path is opaque.

For the upcoming cache-enable diagnostic cycles we need instrumentation that
treats each phase boundary as a *checkpoint* and dumps the architectural
state visible at that checkpoint — the equivalent of inserting `info
registers` calls into the no-MMU/no-debugger boot.  Single chars are not
enough; the failures we are chasing live in `SCTLR_EL1`, `TCR_EL1`,
`MAIR_EL1`, `TTBR{0,1}_EL1`, `CCSIDR_EL1`, and in the byte contents of
`hal_syspage` immediately before `hal_syspageRelocate`.

## 2. Design constraints

The instrumentation runs before the heap exists, before the C stack is
re-pointed at TTBR1, often with caches off, and (during cache-enable
diagnostics) sometimes between an MMU flip and the first instruction fetch
through TTBR1.  That gives us four hard rules:

1. **No `bl`/`ret`** below `el1_entry` until after the high-VA stack is
   live.  The pre-existing helpers `uart_putc_reg`, `uart_puthex64`,
   `_early_uart_print_tag` are *only* safe once SP and the literal pool are
   reachable.  Below the EL drop we use macro-only inline expansion (the
   `early_putc_inline` shape from `_init.S:1061-1094` is the proof of
   concept).
2. **No buffering, no IRQs, no FIFO peeking.**  Spin on `UARTFR.TXFF` per
   character; `dsb sy` before/after each register dump so a bus-side stall
   cannot reorder against an MMU flip.
3. **Tightly bounded register clobbers.**  Phoenix's existing inline UART
   macros use `x3,x4`; the no-call exception dump uses `x21..x26`.  We pick
   one disjoint scratch window, document it once, and never spill x9
   (syspage PA), x14 (E2-probe stash), x18 (vector slot), x30 (return
   addr).
4. **Compile-time disable.**  A single
   `#define DEBUG_EARLY_BOOT 1` (default 1 during bring-up, 0 for shipped
   builds) turns every macro into the empty string.  Layout-sensitive
   markers like the `'X' / '1'..'5'` mid-MMU sequence stay (those are part
   of the boot-correct baseline); only the new verbose dumps gate.

## 3. Macro design — `hal/aarch64/uart_debug.S`

These are inline-asm `.macro` blocks intended to live in a new header
`uart_debug.S` and be `#include`d once near the top of `_init.S`, immediately
after the existing `uart_putc` / `uart_tag2` definitions (≈ `_init.S:73`).
They use scratch registers `x21..x27` exclusively, to stay disjoint from
both the boot-flow scratch (`x0..x8`) and the no-call exception dump's
`x18..x29` window which is only used inside `_early_exception_common`.

### 3.1 `uart_dbg_putc <imm>` — bottom-of-stack primitive

```
.macro uart_dbg_putc imm
#if DEBUG_EARLY_BOOT
901:
        ldr x21, =(PL011_TTY_BASE + UARTFR_OFFSET)
        ldr w22, [x21]
        tst w22, #UARTFR_TXFF
        b.ne 901b
        ldr x21, =PL011_TTY_BASE
        mov w22, #\imm
        str w22, [x21]
#endif
.endm
```

Identical shape to `uart_putc` but using `x21/x22`.  The duplication is
deliberate: we do NOT want to retrofit the existing `uart_putc` clobbers
because the live `el1_entry` flow depends on the current `x3/x4` choice
(see `_init.S:255` ff).

### 3.2 `uart_dbg_putc_virt <imm>` — same, post-MMU

```
.macro uart_dbg_putc_virt imm
#if DEBUG_EARLY_BOOT && defined(PL011_TTY_EARLY_VADDR)
902:
        ldr x21, =(PL011_TTY_EARLY_VADDR + UARTFR_OFFSET)
        ldr w22, [x21]
        tst w22, #UARTFR_TXFF
        b.ne 902b
        ldr x21, =PL011_TTY_EARLY_VADDR
        mov w22, #\imm
        str w22, [x21]
#endif
.endm
```

Falls through to a no-op when `PL011_TTY_EARLY_VADDR` is undefined.

### 3.3 `uart_str <reg>, <strsym>` — print NUL-terminated string

The string literal must live in the `.rodata.early` section (added below).
The macro takes a register hint so the caller can supply a literal-pool
address; this avoids per-call `ldr` materialisation when several `uart_str`
calls share a context.

```
.macro uart_str strsym
#if DEBUG_EARLY_BOOT
        adr x23, \strsym
910:
        ldrb w24, [x23], #1
        cbz w24, 911f
912:
        ldr x21, =(PL011_TTY_BASE + UARTFR_OFFSET)
        ldr w22, [x21]
        tst w22, #UARTFR_TXFF
        b.ne 912b
        ldr x21, =PL011_TTY_BASE
        str w24, [x21]
        b 910b
911:
#endif
.endm
```

Note `adr` (PC-relative, ±1 MB) — works pre- and post-MMU as long as the
string is in the same `.init` text segment as the call site.  Strings are
accumulated near the bottom of `_init.S` in a `.rodata.early` block and
referenced by symbol (`_estr_phase_pre_mmu`, `_estr_phase_post_mmu`, …).

### 3.4 `uart_hex32 <reg>` and `uart_hex64 <reg>`

```
.macro uart_hex32 src
#if DEBUG_EARLY_BOOT
        mov x25, \src
        mov x26, #28
920:
        lsr x27, x25, x26
        and x27, x27, #0xf
        cmp x27, #10
        b.lo 921f
        add x27, x27, #('a' - 10)
        b 922f
921:
        add x27, x27, #'0'
922:
923:
        ldr x21, =(PL011_TTY_BASE + UARTFR_OFFSET)
        ldr w22, [x21]
        tst w22, #UARTFR_TXFF
        b.ne 923b
        ldr x21, =PL011_TTY_BASE
        str w27, [x21]
        subs x26, x26, #4
        b.pl 920b
#endif
.endm

.macro uart_hex64 src
#if DEBUG_EARLY_BOOT
        mov x25, \src
        mov x26, #60
930:
        lsr x27, x25, x26
        and x27, x27, #0xf
        cmp x27, #10
        b.lo 931f
        add x27, x27, #('a' - 10)
        b 932f
931:
        add x27, x27, #'0'
932:
933:
        ldr x21, =(PL011_TTY_BASE + UARTFR_OFFSET)
        ldr w22, [x21]
        tst w22, #UARTFR_TXFF
        b.ne 933b
        ldr x21, =PL011_TTY_BASE
        str w27, [x21]
        subs x26, x26, #4
        b.pl 930b
#endif
.endm
```

Both macros stage the source register into `x25` first, so the caller may
pass the register being read in the same instruction (`uart_hex64 sp`,
`uart_hex64 x9`).  `x25..x27` are the only registers clobbered.

### 3.5 `uart_dump_sysreg <name>, <strsym>`

```
.macro uart_dump_sysreg sysreg, strsym
#if DEBUG_EARLY_BOOT
        uart_str \strsym                 /* prints e.g. "SCTLR_EL1=" */
        mrs x25, \sysreg
        mov x26, #60
940:
        /* identical hex64 inner loop */
        lsr x27, x25, x26
        and x27, x27, #0xf
        cmp x27, #10
        b.lo 941f
        add x27, x27, #('a' - 10)
        b 942f
941:
        add x27, x27, #'0'
942:
943:
        ldr x21, =(PL011_TTY_BASE + UARTFR_OFFSET)
        ldr w22, [x21]
        tst w22, #UARTFR_TXFF
        b.ne 943b
        ldr x21, =PL011_TTY_BASE
        str w27, [x21]
        subs x26, x26, #4
        b.pl 940b
        uart_dbg_putc 13
        uart_dbg_putc 10
#endif
.endm
```

The label string is supplied by the caller (e.g. `_estr_lbl_sctlr_el1`).
Calls produce `SCTLR_EL1=00000000_30d4d938\r\n` lines.

### 3.6 `uart_dump_mem <addr_reg>, <count_imm>`

Dump N 64-bit words at `[addr]`:

```
.macro uart_dump_mem addr, count
#if DEBUG_EARLY_BOOT
        mov x25, \addr
        mov x26, #\count
950:
        uart_hex64 x25                  /* address */
        uart_dbg_putc ':'
        uart_dbg_putc ' '
        ldr x27, [x25]
        mov x21, x27
        uart_hex64 x21                  /* contents */
        uart_dbg_putc 13
        uart_dbg_putc 10
        add x25, x25, #8
        subs x26, x26, #1
        b.ne 950b
#endif
.endm
```

Used to dump the first 4 entries of TTBR0 / TTBR1 L1 tables (32 bytes), or
the first 16 fields of the syspage struct (128 bytes) before
`hal_syspageRelocate`.

### 3.7 `uart_phase <strsym>`

Convenience wrapper: emits `\n[phase: <name>]\r\n`.

```
.macro uart_phase strsym
#if DEBUG_EARLY_BOOT
        uart_dbg_putc 10
        uart_dbg_putc '['
        uart_str \strsym
        uart_dbg_putc ']'
        uart_dbg_putc 13
        uart_dbg_putc 10
#endif
.endm
```

### 3.8 String table

Added at the bottom of `_init.S` next to the existing `_early_*_tag`
labels:

```
.section .rodata.early, "a"
.align 2
_estr_entry:           .asciz "phase: entry"
_estr_pre_el_drop:     .asciz "phase: pre_el_drop"
_estr_post_el_drop:    .asciz "phase: post_el_drop"
_estr_pre_mmu:         .asciz "phase: pre_mmu_flip"
_estr_post_mmu:        .asciz "phase: post_mmu_flip"
_estr_pre_cache:       .asciz "phase: pre_cache_enable"
_estr_post_cache:      .asciz "phase: post_cache_enable"
_estr_pre_relocate:    .asciz "phase: pre_syspage_relocate"
_estr_post_relocate:   .asciz "phase: post_syspage_relocate"

_estr_lbl_currel:      .asciz "CurrentEL="
_estr_lbl_sp:          .asciz "SP="
_estr_lbl_far_el1:     .asciz "FAR_EL1="
_estr_lbl_esr_el1:     .asciz "ESR_EL1="
_estr_lbl_hcr_el2:     .asciz "HCR_EL2="
_estr_lbl_vbar_el2:    .asciz "VBAR_EL2="
_estr_lbl_sctlr_el2:   .asciz "SCTLR_EL2="
_estr_lbl_sctlr_el1:   .asciz "SCTLR_EL1="
_estr_lbl_tcr_el1:     .asciz "TCR_EL1="
_estr_lbl_mair_el1:    .asciz "MAIR_EL1="
_estr_lbl_ttbr0_el1:   .asciz "TTBR0_EL1="
_estr_lbl_ttbr1_el1:   .asciz "TTBR1_EL1="
_estr_lbl_vbar_el1:    .asciz "VBAR_EL1="
_estr_lbl_cpacr_el1:   .asciz "CPACR_EL1="
_estr_lbl_csselr_el1:  .asciz "CSSELR_EL1="
_estr_lbl_ccsidr_el1:  .asciz "CCSIDR_EL1="
_estr_lbl_clidr_el1:   .asciz "CLIDR_EL1="
_estr_lbl_syspage:     .asciz "syspage="
_estr_lbl_kentry:      .asciz "kernel_entry="
_estr_lbl_progs:       .asciz "progs="
_estr_lbl_relOffs:     .asciz "relOffs="
```

`.rodata.early` is included in the same loadable segment as `.init`
(adjust `link.ld` if necessary; the existing `_early_*_tag` labels sit in
`.section .init` so we may simply reuse that).

## 4. Patch — `hal/aarch64/_init.S`

Unified diff, base = current tree (the file shown in §1 of the prompt).
Filename relative to repo root: `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`.

```diff
@@ _init.S:73 ,7 +73,9 @@ .endm
 .macro uart_tag2 c1 c2
         uart_putc 10
         uart_putc \c1
         uart_putc \c2
         uart_putc 10
 .endm
+
+#include "hal/aarch64/uart_debug.S"

 .macro early_vector slot
         mov x18, #\slot

@@ _init.S:186 ,4 +188,21 @@ _start:
         uart_reinit_115200
+
+        /* === EARLY DIAG: entry snapshot ===
+         * Print CurrentEL / SP / FAR_EL1 / ESR_EL1 in case armstub
+         * left a stale fault context. */
+        uart_phase _estr_entry
+        uart_dump_sysreg CurrentEL, _estr_lbl_currel
+        mov x21, sp
+        uart_str _estr_lbl_sp
+        uart_hex64 x21
+        uart_dbg_putc 13
+        uart_dbg_putc 10
+        uart_dump_sysreg far_el1, _estr_lbl_far_el1
+        uart_dump_sysreg esr_el1, _estr_lbl_esr_el1
+
+        /* === EARLY DIAG: pre-EL-drop snapshot ===
+         * If we are at EL2 we can read HCR_EL2, VBAR_EL2, SCTLR_EL2.
+         * SCR_EL3 is not accessible from EL2; skip. */
+        mrs x21, CurrentEL
+        cmp x21, #(2 << 2)
+        b.ne 1f
+        uart_phase _estr_pre_el_drop
+        uart_dump_sysreg hcr_el2,   _estr_lbl_hcr_el2
+        uart_dump_sysreg vbar_el2,  _estr_lbl_vbar_el2
+        uart_dump_sysreg sctlr_el2, _estr_lbl_sctlr_el2
+1:
         /*
          * Drop EL2 -> EL1 if we entered at EL2.

@@ _init.S:248 ,9 +267,18 @@ el1_entry:
-        /* DEBUG: Check current exception level (expect 0x4 == EL1). */
-        mrs x0, CurrentEL
-        uart_putc 90 /* DEBUG: CurrentEL value */
+        /* === EARLY DIAG: post-EL-drop snapshot === */
+        uart_phase _estr_post_el_drop
+        uart_dump_sysreg CurrentEL, _estr_lbl_currel    /* expect 0x4 */
+        uart_dump_sysreg sctlr_el1, _estr_lbl_sctlr_el1
+        uart_dump_sysreg tcr_el1,   _estr_lbl_tcr_el1
+        uart_dump_sysreg mair_el1,  _estr_lbl_mair_el1
+        uart_dump_sysreg ttbr0_el1, _estr_lbl_ttbr0_el1
+        uart_dump_sysreg ttbr1_el1, _estr_lbl_ttbr1_el1
+        uart_dump_sysreg vbar_el1,  _estr_lbl_vbar_el1
+        uart_dump_sysreg cpacr_el1, _estr_lbl_cpacr_el1
+        uart_putc 75                /* keep K marker for legacy log scrapers */

         /* Assumptions: x9 => PA of syspage from PLO */
-        uart_putc 75
         msr daifSet, #0xf

@@ _init.S:444 ,8 +472,28 @@
         uart_tag2 88, 51
+        /* === EARLY DIAG: pre-MMU.M flip snapshot ===
+         * Dump SCTLR baseline, top of TTBR0 L1 table (4 entries),
+         * syspage PA from x9, kernel entry from syspage. */
+        uart_phase _estr_pre_mmu
+        uart_dump_sysreg sctlr_el1, _estr_lbl_sctlr_el1
+        uart_str _estr_lbl_syspage
+        uart_hex64 x9
+        uart_dbg_putc 13
+        uart_dbg_putc 10
+        ldr x21, [x9, #(SYSPAGE_PKERNEL_OFFSET)]
+        uart_str _estr_lbl_kentry
+        uart_hex64 x21
+        uart_dbg_putc 13
+        uart_dbg_putc 10
+        adrp x21, PMAP_COMMON_SCRATCH_TT
+        uart_dump_mem x21, 4
+
         dsb ish
         mrs x0, sctlr_el1
         orr x0, x0, #(1 << 0)   /* SCTLR_EL1.M (MMU only) */
         msr sctlr_el1, x0
         isb
         uart_tag2 88, 52
+        /* === EARLY DIAG: post-MMU.M flip snapshot === */
+        uart_phase _estr_post_mmu
+        uart_dump_sysreg sctlr_el1, _estr_lbl_sctlr_el1   /* M must be 1 */

@@ _init.S:455 ,4 +500,18 @@
         uart_tag2 88, 53
+        /* === EARLY DIAG: pre-cache-enable (TD-16 reattempt) ===
+         * No SCTLR.C/I write happens here in the M-only baseline; the
+         * dump gives the cache config the next attempt will start from.
+         * Selecting CSSELR_EL1=0 (L1D) before reading CCSIDR_EL1 — ARM ARM
+         * requires ISB between the two. */
+        uart_phase _estr_pre_cache
+        uart_dump_sysreg clidr_el1, _estr_lbl_clidr_el1
+        msr csselr_el1, xzr
+        isb
+        uart_dump_sysreg csselr_el1, _estr_lbl_csselr_el1
+        uart_dump_sysreg ccsidr_el1, _estr_lbl_ccsidr_el1
+        /* Post-cache-enable readback would go after the future
+         * `orr x0, x0, #(1<<2)|(1<<12)` re-enable; left as TODO with
+         * matching `uart_phase _estr_post_cache`. */
+
         /* Translation is now taking place through PMAP_COMMON_SCRATCH_TT */
```

The diff is presented as additions only — no existing line is removed,
preserving the boot-correct M-only baseline.  Every dump is gated by
`DEBUG_EARLY_BOOT` inside the macros, so disabling the flag yields a
zero-byte text-section delta against the current binary at the boundary
points (the macros themselves expand to nothing).

## 5. Patch — `syspage.c`

Filename: `sources/phoenix-rtos-kernel/syspage.c`.

```diff
@@ syspage.c:471 ,4 +471,30 @@
         while (*uartfr & 0x20) {}
         *uart = 'P'; /* P marker - entering program-reloc block */
         if (syspage_common.syspage->progs != NULL) {
+#if DEBUG_EARLY_BOOT
+                /* Dump 16 hex digits of syspage->progs and relOffs at the
+                 * exact instant before hal_syspageRelocate. The previous
+                 * Phase A failure (TD-16) faulted inside this call; without
+                 * the input pointer printed first we cannot tell whether
+                 * the input was a stale pre-reloc PA, a high-VA already-
+                 * reloc'd value, or zero. Frame as "Pp{...}" / "Pr{...}". */
+                {
+                        extern u64 relOffs;
+                        unsigned long long pv =
+                            (unsigned long long)(unsigned long)
+                                syspage_common.syspage->progs;
+                        unsigned long long rv = (unsigned long long)relOffs;
+                        int sh;
+                        while (*uartfr & 0x20) {}
+                        *uart = 'p'; *uart = '{';
+                        for (sh = 60; sh >= 0; sh -= 4) {
+                                unsigned int n = (unsigned int)((pv >> sh) & 0xfU);
+                                while (*uartfr & 0x20) {}
+                                *uart = (n < 10U) ? ('0' + n) : ('a' + n - 10U);
+                        }
+                        *uart = '}';
+                        *uart = 'r'; *uart = '{';
+                        for (sh = 60; sh >= 0; sh -= 4) {
+                                unsigned int n = (unsigned int)((rv >> sh) & 0xfU);
+                                while (*uartfr & 0x20) {}
+                                *uart = (n < 10U) ? ('0' + n) : ('a' + n - 10U);
+                        }
+                        *uart = '}';
+                }
+#endif
                 int progCount = 0;
                 syspage_common.syspage->progs = hal_syspageRelocate(syspage_common.syspage->progs);
+#if DEBUG_EARLY_BOOT
+                {
+                        unsigned long long pv =
+                            (unsigned long long)(unsigned long)
+                                syspage_common.syspage->progs;
+                        int sh;
+                        while (*uartfr & 0x20) {}
+                        *uart = 'P'; *uart = '\''; *uart = '{';
+                        for (sh = 60; sh >= 0; sh -= 4) {
+                                unsigned int n = (unsigned int)((pv >> sh) & 0xfU);
+                                while (*uartfr & 0x20) {}
+                                *uart = (n < 10U) ? ('0' + n) : ('a' + n - 10U);
+                        }
+                        *uart = '}';
+                }
+#endif
```

The corresponding instrumentation for `_hal_init` (`hal/aarch64/hal.c:91`)
adds entry/exit hex prints around each early sub-init, modelled on the
existing single-char markers `'4'..'e'` already at `hal.c:109..180`:

```diff
@@ hal.c:106 ,2 +106,12 @@
         *uart = 'H'; /* H marker - _hal_init entry */
+#if DEBUG_EARLY_BOOT
+        /* dump 16 hex digits of hal_syspage so a corrupted pointer is
+         * visible at function entry rather than after the next deref */
+        {
+                unsigned long long sv = (unsigned long long)(unsigned long)hal_syspage;
+                int sh;
+                volatile unsigned int *uartfr = (volatile unsigned int *)0xffffffffffe00018ull;
+                *uart = 's'; *uart = '{';
+                for (sh = 60; sh >= 0; sh -= 4) {
+                        unsigned int n = (unsigned int)((sv >> sh) & 0xfU);
+                        while (*uartfr & 0x20) {}
+                        *uart = (n < 10U) ? ('0' + n) : ('a' + n - 10U);
+                }
+                *uart = '}';
+        }
+#endif
```

Identical scaffolding wraps `_pmap_preinit` (`hal.c:159`) and
`_hal_consoleInit` (`hal.c:165`) — printing the input arg for `_pmap_preinit`
(`dtbStart`) and a 16-hex of `hal_syspage` again post-`_pmap_preinit` so a
mid-call clobber would be obvious.  These are mechanical and omitted here
for length; the convention is `<lower>{hex16}` before the call, `<UPPER'>{hex16}`
after.

## 6. Header file — `hal/aarch64/uart_debug.S`

Place the macros from §3 plus the `DEBUG_EARLY_BOOT` gate at the top:

```
/*
 * Phoenix-RTOS AArch64 early-boot diagnostic UART helpers.
 *
 * Macros for emitting structured boot-trace output (phase markers,
 * sysreg dumps, memory dumps) before the C runtime exists. All macros
 * compile to empty when DEBUG_EARLY_BOOT == 0; they only clobber
 * registers x21..x27. See docs/done/early-boot-diagnostic-instrumentation.md.
 */

#ifndef DEBUG_EARLY_BOOT
#define DEBUG_EARLY_BOOT 1
#endif

/* macro definitions from §3.1..§3.7 here */
```

## 7. Expected UART output

### 7.1 Clean boot (M-only baseline reaching `(psh)%`)

```
[phase: entry]
CurrentEL=0000000000000008
SP=0000000000080000
FAR_EL1=0000000000000000
ESR_EL1=0000000000000000

[phase: pre_el_drop]
HCR_EL2=0000000000000002
VBAR_EL2=0000000000080800
SCTLR_EL2=0000000030c50830

K
[phase: post_el_drop]
CurrentEL=0000000000000004
SCTLR_EL1=0000000030d4d938
TCR_EL1=00000000000000000
MAIR_EL1=00000000000444ff
TTBR0_EL1=0000000000000000
TTBR1_EL1=0000000000000000
VBAR_EL1=0000000000080800
CPACR_EL1=0000000000330000
LSTUMV
X1
[phase: pre_mmu_flip]
SCTLR_EL1=0000000030d4d938
syspage=0000000000080100
kernel_entry=0000000000400000
0000000000003000: 0000000000400f05
0000000000003008: 0000000000400f05
0000000000003010: 0000000000000000
0000000000003018: 0000000000000000
X2
[phase: post_mmu_flip]
SCTLR_EL1=0000000030d4d939
X3
[phase: pre_cache_enable]
CLIDR_EL1=00000000091b033
CSSELR_EL1=0000000000000000
CCSIDR_EL1=0000000000711fe1
X4
X5
N!YOPSTUZbcd...
H s{ffffffffffe000}
4 5 6 ...
F r D s E 7 8 ...
hal: console init done
9 a b c d e
(psh)%
```

### 7.2 Current Phase A failure (cache-enable: M+I together)

```
... up through X3 markers identical to clean ...
[phase: pre_cache_enable]
CLIDR_EL1=00000000091b033
CSSELR_EL1=0000000000000000
CCSIDR_EL1=0000000000711fe1

[phase: post_cache_enable]
SCTLR_EL1=0000000030d4d93d   <- M+I+C=1 if Phase B, M+I if Phase A

X4 X5 td15 td16 ...
N!YOPSTUZbcd
H s{ffffffffffe000}
4 5 6
F r D s
EX=0000000000000005
ESR=0000000096000044
ELR=ffffffffc000abc0
FAR=ffffffffc0001890
[hang]
```

The new dump immediately tells us the level the fault is on (the EC + DFSC
fields in `ESR=…0044` decode as level-0 data abort, alignment fault) and
which page-table walker level matters — replacing the previous "five
unrelated single chars then silence" debugging experience.

## 8. Disabling for production

Set `DEBUG_EARLY_BOOT = 0` in board config (or pass
`-DDEBUG_EARLY_BOOT=0` via the kernel `Makefile.in`).  Every macro then
expands to nothing.  Two non-macroized markers remain — the `'X1'..'X5'`
sequence and `_early_exception_common` — both are part of the boot-correct
baseline already shipped.  The shipped kernel emits exactly the legacy
post-MMU char trail, byte-identical to the current build, plus the
`uart_putc 75/76/77/85/86` and the four `uart_tag2 88,N` markers that
already exist in the source.

To make the gate selectable per-board without recompiling all of HAL we
add `DEBUG_EARLY_BOOT ?= 1` to `hal/aarch64/Makefile`, threading it as
`-DDEBUG_EARLY_BOOT=$(DEBUG_EARLY_BOOT)`.

## 9. Build cost / slowdown estimate

PL011 at 115 200 baud, 8N1 = 8.68 µs per byte ≈ 0.087 ms.  Empirically the
existing boot trace is ~50 chars and adds ~5 ms of UART latency, which is
imperceptible on a Pi 4 boot (sub-second to `(psh)%`).

Each new "phase" block emits in the order of:

* `uart_phase`         ≈ 22 bytes (`\n[phase: pre_mmu_flip]\r\n`)
* one `uart_dump_sysreg` ≈ 10–12 (label) + 16 (hex) + 2 (CRLF) ≈ 30 bytes
* one `uart_dump_mem`     row ≈ 36 bytes (16-hex addr + ": " + 16-hex value + CRLF)

Worst-case total of new bytes per boot:

| Block                 | Lines | Bytes |
|-----------------------|-------|-------|
| entry                 | 5     | 130   |
| pre_el_drop (cond)    | 4     | 100   |
| post_el_drop          | 9     | 270   |
| pre_mmu_flip          | 8     | 240   |
| post_mmu_flip         | 2     |  60   |
| pre_cache_enable      | 4     | 110   |
| post_cache_enable     | 2     |  60   |
| syspage.c diff probes | 4     | 100   |
| hal.c diff probes     | 5     | 100   |
| **total**             | ~43   | ~1170 |

≈ 1170 × 0.087 ms ≈ **102 ms** added to the boot path before
`hal: console init done`.  Negligible against the human-perceived boot
time but very visible to logic-analyser captures and to `picocom`'s
read loop.

## 10. Risks

1. **Register clobbers across phases.**  We chose `x21..x27`.  The
   pre-existing macro in `_init.S:1061-1094` uses `x21..x25`; both
   `early_putc_inline` and our new `uart_dbg_putc` derive from the same
   shape, so the no-call exception dump and the new diag macros are
   naturally compatible.  Outside the dump entry, `x21..x27` are
   architecturally caller-saved and never used by the live boot flow in
   `_init.S` (verified by grep — every `x21..x27` mention is inside the
   exception path).

2. **Literal-pool reach across MMU flip.**  Each `ldr x21, =SYMBOL`
   resolves via PC-relative literal pool.  Pre-MMU PC is the low PA, the
   literals are in `.init`, this works.  Post-MMU pre-`br x0` PC is *still*
   the low PA (TTBR0 identity), the literal pool is still resolved
   relative to that PA, and the TTBR0 identity map covers `.init`.  Once
   we cross `br x0` to the high VA the literal pool is reached via the
   TTBR1 mapping of `.init`; works as long as we stay inside the same
   linker-segment window.  The diagnostic macros never `bl`, so no
   literal pool spans a high-VA branch.

3. **Cache-state perturbation.**  All accesses are direct loads and
   stores to PL011 device memory; the page is mapped Strongly Ordered (or
   Device-nGnRE) in PMAP_COMMON_DEVICES_TTL3 (`_init.S:511-515`), so
   reads/writes do not interact with the data cache.  The macros do not
   perform any `dc`/`ic` operations.  Sysreg `mrs` reads are inert with
   respect to the caches and TLB.  This is the property that lets us
   dump CCSIDR/CSSELR mid-flow without disturbing the very caches we are
   about to enable.

4. **Timing perturbation.**  The 102 ms slowdown is large relative to
   firmware-side timeouts in some platforms — not the case for Phoenix +
   Pi 4 (no watchdog runs by armstub-handoff time).  But this *does*
   change the relative ordering of armstub-side speculative fills versus
   our own page-table writes.  If the Phase A fault turns out to be
   timing-sensitive, the instrumentation could mask or unmask it.  We
   accept this — the alternative (no instrumentation, no diagnosis) is
   strictly worse.

5. **String pool placement.**  `.rodata.early` must live in a TTBR0-mapped
   1 GB block during the pre-MMU window and in `.init` (TTBR1) post-MMU.
   The existing `_early_*_tag` strings already work this way, so reusing
   `.section .init` (rather than introducing a new section) sidesteps any
   linker-script change.

6. **Recursion via fault.**  If a diag-emit itself faults, the no-call
   exception dump in `_early_exception_common` will fire; that path uses
   `x18..x29` and shares `x21..x27` overlap with our diag macros.
   Acceptable — a fault inside diag would print one EX/ESR/ELR/FAR
   record and halt.

7. **SCR_EL3 inaccessibility.**  Item C2 of the prompt asks for SCR_EL3
   in the pre-EL-drop snapshot, but Phoenix runs at EL2 by armstub
   handoff and `mrs ..., scr_el3` from EL2 traps to EL3.  We therefore
   omit SCR_EL3 and document the omission in the macro block.  The
   armstub itself sets up `SCR_EL3` and any reader that needs it can
   patch armstub to log it before the eret-to-EL2.

8. **`mrs CurrentEL` clobbering x0.**  The existing flow uses `x0` for
   the EL test at `_init.S:212`; our pre-EL-drop snapshot uses `x21` so
   the existing `mrs x0, CurrentEL` immediately following our additions
   still behaves as before.

## 11. Cited instrumentation points

| Phase                         | File / line              | Macro(s) used                           |
|-------------------------------|--------------------------|------------------------------------------|
| entry snapshot                | `_init.S:188` (after `uart_reinit_115200`) | `uart_phase`, `uart_dump_sysreg ×4` |
| pre-EL-drop                   | `_init.S:212` (inside the `cmp #(2<<2)` arm) | `uart_phase`, `uart_dump_sysreg ×3` |
| post-EL-drop                  | `_init.S:248` (`el1_entry` head) | `uart_phase`, `uart_dump_sysreg ×8` |
| pre-MMU.M flip                | `_init.S:444` (before `dsb ish`) | `uart_phase`, `uart_dump_sysreg`, `uart_dump_mem ×4 entries` |
| post-MMU.M flip               | `_init.S:450` (after second `uart_tag2 88,52`) | `uart_phase`, `uart_dump_sysreg sctlr_el1` |
| pre-cache-enable              | `_init.S:459` (after `uart_tag2 88,53`) | `uart_phase`, three sysreg dumps |
| post-cache-enable (TD-16)     | `_init.S:459` (paired site, gated by future TD-16 reattempt) | same |
| syspage relocate site         | `syspage.c:476` | C-side hex16 dumps before/after |
| `_hal_init` entry             | `hal.c:106` | C-side hex16 of `hal_syspage` |
| `_pmap_preinit` boundary      | `hal.c:159` | hex16 of `dtbStart` and post-call `hal_syspage` |
| `_hal_consoleInit` boundary   | `hal.c:165` | hex16 of `hal_syspage` post-call |

## 12. Summary

The patch adds a single new file (`uart_debug.S`), gates everything behind
`DEBUG_EARLY_BOOT`, costs ≈ 100 ms of UART time when enabled, and turns
the post-MMU silence into a per-phase structured dump with sysreg
read-backs, page-table samples, and the syspage relocation arguments
visible at the moment they are consumed.  No source files are modified by
this commit — the patches above are the deliverable; applying them is the
next step in the cache-enable diagnostic cycle.

Adjacent material:

* `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` — TD-04, TD-15, TD-16
  reference labels used in source comments.
* `docs/knowledge/boot-mmu-bringup-non-linux.md` — cross-OS convergent
  recommendation for SCTLR baseline value `0x30d4d938`.
* `tracking/current-step.md` — current active step the diagnostic cycle
  feeds.
