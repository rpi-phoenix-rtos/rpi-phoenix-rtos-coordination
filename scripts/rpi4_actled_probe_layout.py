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


CURRENT_LAYOUT_NAME = "pi4_dense_armstub_signature_map_2026_04_10"


_CURRENT_LAYOUT = LayoutDef(
    name=CURRENT_LAYOUT_NAME,
    description="Pi 4 compact GPIO42 telemetry with dense armstub-side signature-check probes and EL2 exception telemetry before the dedicated fixed-address entry trampoline",
    code_bits=5,
    sync_pulses=1,
    stage_order=(1, 2, 3, 23, 24, 25, 26, 27, 28, 29, 30, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
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
        23: StageDef(
            code=23,
            label="armstub late seam entry",
            meaning="Primary-core armstub entered the dense signature-check band after stage 3.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="primary_cpu_late",
        ),
        24: StageDef(
            code=24,
            label="armstub fixed target loaded",
            meaning="Loaded the fixed branch target address into x4.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="primary_cpu_late",
        ),
        25: StageDef(
            code=25,
            label="armstub first signature word read",
            meaning="Read the first signature word from the fixed target.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="primary_cpu_late",
        ),
        26: StageDef(
            code=26,
            label="armstub second signature word read",
            meaning="Read the second signature word from the fixed target.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="primary_cpu_late",
        ),
        27: StageDef(
            code=27,
            label="armstub signature0 constant loaded",
            meaning="Loaded the first expected signature constant.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="primary_cpu_late",
        ),
        28: StageDef(
            code=28,
            label="armstub signature0 compare passed",
            meaning="The first signature-word compare passed.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="primary_cpu_late",
        ),
        29: StageDef(
            code=29,
            label="armstub signature1 constant loaded",
            meaning="Loaded the second expected signature constant.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="primary_cpu_late",
        ),
        30: StageDef(
            code=30,
            label="armstub signature1 compare passed",
            meaning="The second signature-word compare passed, before the final stage-4 signature-ok marker.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="primary_cpu_late",
        ),
        4: StageDef(
            code=4,
            label="armstub target signature ok",
            meaning="Custom armstub verified the expected plo signature at 0x40080000 before branching.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="gpio42_stage4",
        ),
        5: StageDef(
            code=5,
            label="fixed entry veneer",
            meaning="The fixed-address branch target veneer in plo was entered.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start",
        ),
        6: StageDef(
            code=6,
            label="generic _start body",
            meaning="The old generic plo _start body was reached after the fixed-entry veneer.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        7: StageDef(
            code=7,
            label="after clear x0-x7",
            meaning="Reached the first quarter of the register-clearing block.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        8: StageDef(
            code=8,
            label="after clear x8-x15",
            meaning="Reached the second quarter of the register-clearing block.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        9: StageDef(
            code=9,
            label="after clear x16-x23",
            meaning="Reached the third quarter of the register-clearing block.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        10: StageDef(
            code=10,
            label="after clear x24-x30",
            meaning="Completed the register-clearing block.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        11: StageDef(
            code=11,
            label="after dsb/isb",
            meaning="Reached the barrier pair just before sampling currentEL.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        12: StageDef(
            code=12,
            label="after mrs currentEL",
            meaning="Sampled currentEL successfully.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="_start_real",
        ),
        13: StageDef(
            code=13,
            label="start_el3",
            meaning="Entered the EL3-specific plo path.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el3",
        ),
        14: StageDef(
            code=14,
            label="start_el2",
            meaning="Entered the EL2-specific plo path.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el2",
        ),
        15: StageDef(
            code=15,
            label="start_el1",
            meaning="Entered the EL1-specific plo path.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el1",
        ),
        16: StageDef(
            code=16,
            label="el3 path complete",
            meaning="Completed EL3 path, just before start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el3",
        ),
        17: StageDef(
            code=17,
            label="el2 path complete",
            meaning="Completed EL2 path, just before start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el2",
        ),
        18: StageDef(
            code=18,
            label="el1 path complete",
            meaning="Completed EL1 path, just before start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el1",
        ),
        19: StageDef(
            code=19,
            label="start_common",
            meaning="Entered start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_common",
        ),
        20: StageDef(
            code=20,
            label="after stack setup",
            meaning="Completed early stack setup in start_common.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_common",
        ),
        21: StageDef(
            code=21,
            label="core0 branch to _startc",
            meaning="Core 0 reached the _startc branch.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_common",
        ),
        22: StageDef(
            code=22,
            label="unexpected EL trap",
            meaning="Reached the unexpected currentEL trap path.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S",
            source_symbol="start_el_unknown",
        ),
        31: StageDef(
            code=31,
            label="armstub target signature bad",
            meaning="Custom armstub did not find the expected plo entry signature at 0x40080000 and halted before branching.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="gpio42_stage31",
        ),
        0: StageDef(
            code=0,
            label="armstub el2 exception",
            meaning="An EL2 exception vector was taken during the armstub diagnostic seam and the handler halted after emitting code 0.",
            source_file="/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S",
            source_symbol="armstub_exception",
        ),
    },
    notes=(
        "Compact GPIO42 protocol: one sync pulse, then 5 bits MSB-first.",
        "Short on-time encodes 0, long on-time encodes 1.",
        "Long off gap separates stage bursts.",
        "This layout supersedes the earlier dedicated fixed-entry-trampoline map by inserting dense armstub-side probes around the fixed-target signature reads.",
        "Initial ACT LED activity during firmware SD-card reads is preamble noise and must be ignored unless it decodes into a later valid contiguous Phoenix stage run.",
        "Stage 31 is a special terminal mismatch stage, not part of the normal contiguous boot sequence.",
        "Stage 0 is a special EL2 exception-fault stage, not part of the normal contiguous boot sequence.",
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
