# Pi 4 Post-MMU UART Split

Date: 2026-04-17

## Summary

The latest real-board UART log
`artifacts/rpi4b-uart/rpi4b-uart-20260417-211048.log` proves the active Pi 4
failure is no longer in firmware handoff or `plo`. The board now reaches:

- `AS0`
- `TR0..TR3`
- `hal: jump entry`
- `hal: jump irq off`
- `hal: jump exit el1`
- `A2`
- `KLM`

`A2` is the `plo` EL2 exit marker. `KLM` are kernel `_start` breadcrumbs. The
remaining silent gap is therefore after kernel breadcrumb `M` and before
`main()`.

## Fix Applied

Added a temporary fixed virtual PL011 mapping for post-MMU kernel diagnostics:

- `phoenix-rtos-project`
  - commit: `a67cdd5`
  - `_projects/aarch64a72-generic-rpi4b/board_config.h`
    - added `PL011_TTY_EARLY_VADDR 0xffffffffffe00000ull`
- `phoenix-rtos-kernel`
  - commit: `cf98c7dc`
  - `hal/aarch64/_init.S`
    - pre-maps PL011 into the first device TTL3 slot before the `ttbr1`
      handoff
    - adds new post-MMU breadcrumbs:
      - `N` after `ttbr1`-backed post-MMU UART becomes valid
      - `O` at `_core_0_virtual`
      - `P` after syspage copy
      - `Q` after `_set_up_vbar_and_stacks`
      - `R` immediately before `b main`
  - `main.c`
    - adds raw earliest `main()` breadcrumb `S` through the fixed temporary
      UART mapping before `_hal_init()`

## Validation

- `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
- QEMU late-handoff continuity now reaches:
  - `hal: jump exit el1`
  - `A3`
  - `KLMNOPQRSconsole: pl011 init done`
- canonical export: pass
- FAT-aware verify: pass

## Exported Image

- Path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `6638e81ec8052beb23bb83a02340b1a1cc3a1e4914ce2c0779b949c04d275c9a`

## Expected Next Real-Board UART Outcomes

- `... KLM` only:
  still dies before `ttbr1`-backed post-MMU UART becomes valid
- `... KLMN` but not `O`:
  dies between the `ttbr1` handoff and `_core_0_virtual`
- `... KLMNO` but not `P`:
  dies in the earliest virtual-mode syspage-copy path
- `... KLMNOP` but not `Q`:
  dies between syspage copy and `_set_up_vbar_and_stacks`
- `... KLMNOPQ` or `... KLMNOPQR` but not `S`:
  dies around final stack/vector setup or immediate branch to `main()`
- `... KLMNOPQRS`:
  reaches `main()`; the next blocker is later in `_hal_init()` or after
