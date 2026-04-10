#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import asdict, dataclass


@dataclass(frozen=True)
class StageDef:
    code: int
    label: str
    meaning: str
    source_file: str
    source_symbol: str

    def bits(self, code_bits: int) -> str:
        return format(self.code, f"0{code_bits}b")


@dataclass(frozen=True)
class LayoutDef:
    name: str
    description: str
    code_bits: int
    sync_pulses: int
    stage_order: tuple[int, ...]
    stages: dict[int, StageDef]
    notes: tuple[str, ...]


CURRENT_LAYOUT_NAME = "pi4_fixed_entry_trampoline_2026_04_10"


_CURRENT_LAYOUT = LayoutDef(
    name=CURRENT_LAYOUT_NAME,
    description="Pi 4 compact GPIO42 telemetry after dedicated fixed-address entry trampoline split",
    code_bits=5,
    sync_pulses=1,
    stage_order=tuple(range(1, 22)),
    stages={
        1: StageDef(
            code=1,
            label="armstub primary entry",
            meaning="Primary-core custom armstub entry reached.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="gpio42_stage1",
        ),
        2: StageDef(
            code=2,
            label="armstub after timer/gic",
            meaning="Custom armstub completed early local timer and GIC preparation.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="gpio42_stage2",
        ),
        3: StageDef(
            code=3,
            label="armstub before fixed jump",
            meaning="Custom armstub reached the fixed-address branch into plo.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="gpio42_stage3",
        ),
        4: StageDef(
            code=4,
            label="fixed entry veneer",
            meaning="The fixed-address branch target veneer in plo was entered.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start",
        ),
        5: StageDef(
            code=5,
            label="generic _start body",
            meaning="The old generic plo _start body was reached after the fixed-entry veneer.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        6: StageDef(
            code=6,
            label="after clear x0-x7",
            meaning="Reached the first quarter of the register-clearing block.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        7: StageDef(
            code=7,
            label="after clear x8-x15",
            meaning="Reached the second quarter of the register-clearing block.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        8: StageDef(
            code=8,
            label="after clear x16-x23",
            meaning="Reached the third quarter of the register-clearing block.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        9: StageDef(
            code=9,
            label="after clear x24-x30",
            meaning="Completed the register-clearing block.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        10: StageDef(
            code=10,
            label="after dsb/isb",
            meaning="Reached the barrier pair just before sampling currentEL.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        11: StageDef(
            code=11,
            label="after mrs currentEL",
            meaning="Sampled currentEL successfully.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        12: StageDef(
            code=12,
            label="start_el3",
            meaning="Entered the EL3-specific plo path.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el3",
        ),
        13: StageDef(
            code=13,
            label="start_el2",
            meaning="Entered the EL2-specific plo path.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el2",
        ),
        14: StageDef(
            code=14,
            label="start_el1",
            meaning="Entered the EL1-specific plo path.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el1",
        ),
        15: StageDef(
            code=15,
            label="el3 path complete",
            meaning="Completed EL3 path, just before start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el3",
        ),
        16: StageDef(
            code=16,
            label="el2 path complete",
            meaning="Completed EL2 path, just before start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el2",
        ),
        17: StageDef(
            code=17,
            label="el1 path complete",
            meaning="Completed EL1 path, just before start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el1",
        ),
        18: StageDef(
            code=18,
            label="start_common",
            meaning="Entered start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_common",
        ),
        19: StageDef(
            code=19,
            label="after stack setup",
            meaning="Completed early stack setup in start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_common",
        ),
        20: StageDef(
            code=20,
            label="core0 branch to _startc",
            meaning="Core 0 reached the _startc branch.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_common",
        ),
        21: StageDef(
            code=21,
            label="unexpected EL trap",
            meaning="Reached the unexpected currentEL trap path.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el_unknown",
        ),
    },
    notes=(
        "Compact GPIO42 protocol: one sync pulse, then 5 bits MSB-first.",
        "Short on-time encodes 0, long on-time encodes 1.",
        "Long off gap separates stage bursts.",
        "Only IMG_7137.mov and later clips match this exact layout.",
        "Initial ACT LED activity during firmware SD-card reads is preamble noise and must be ignored unless it decodes into a later valid contiguous Phoenix stage run.",
    ),
)


LAYOUTS: dict[str, LayoutDef] = {
    CURRENT_LAYOUT_NAME: _CURRENT_LAYOUT,
    "current": _CURRENT_LAYOUT,
}


def get_layout(name: str = "current") -> LayoutDef:
    try:
        return LAYOUTS[name]
    except KeyError as exc:
        known = ", ".join(sorted(LAYOUTS))
        raise KeyError(f"unknown layout '{name}', known: {known}") from exc


def layout_to_dict(layout: LayoutDef) -> dict:
    return {
        "name": layout.name,
        "description": layout.description,
        "code_bits": layout.code_bits,
        "sync_pulses": layout.sync_pulses,
        "stage_order": list(layout.stage_order),
        "stages": {
            str(code): {
                **asdict(stage),
                "bits": stage.bits(layout.code_bits),
            }
            for code, stage in sorted(layout.stages.items())
        },
        "notes": list(layout.notes),
    }
