#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from rpi4_actled_probe_layout import get_layout, layout_to_dict


def load_json(path: str) -> dict[str, Any]:
    if path == "-":
        return json.load(sys.stdin)
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def extract_decodes(analysis: dict[str, Any]) -> list[dict[str, Any]]:
    decodes: list[dict[str, Any]] = []
    for group in analysis.get("stage_groups", []):
        decode = group.get("decode")
        if not decode:
            continue
        if not decode.get("valid", False):
            continue
        code = decode.get("code")
        if isinstance(code, int):
            decodes.append(group)
    return decodes


def build_interpretation(analysis: dict[str, Any], layout_name: str) -> dict[str, Any]:
    layout = get_layout(layout_name)
    valid_groups = extract_decodes(analysis)
    stage_index = {code: idx for idx, code in enumerate(layout.stage_order)}

    best_groups: list[dict[str, Any]] = []
    best_expected_idx = 0

    for start_idx, group in enumerate(valid_groups):
        start_code = int(group["decode"]["code"])
        if start_code not in stage_index:
            continue

        expected_idx = stage_index[start_code] + 1
        candidate = [group]
        last_code = start_code

        for later in valid_groups[start_idx + 1 :]:
            code = int(later["decode"]["code"])
            if code == last_code:
                continue
            if expected_idx < len(layout.stage_order) and code == layout.stage_order[expected_idx]:
                candidate.append(later)
                last_code = code
                expected_idx += 1

        if not best_groups:
            best_groups = candidate
            best_expected_idx = expected_idx
            continue

        best_start_code = int(best_groups[0]["decode"]["code"])
        if len(candidate) > len(best_groups):
            best_groups = candidate
            best_expected_idx = expected_idx
        elif len(candidate) == len(best_groups) and start_code < best_start_code:
            best_groups = candidate
            best_expected_idx = expected_idx
        elif len(candidate) == len(best_groups) and start_code == best_start_code:
            if int(candidate[-1]["decode"]["code"]) > int(best_groups[-1]["decode"]["code"]):
                best_groups = candidate
                best_expected_idx = expected_idx

    matched: list[dict[str, Any]] = []
    for group in best_groups:
        code = int(group["decode"]["code"])
        stage = layout.stages[code]
        matched.append(
            {
                "code": code,
                "bits": group["decode"]["bits"],
                "label": stage.label,
                "meaning": stage.meaning,
                "group_index": group["group_index"],
                "start_s": group["start_s"],
                "end_s": group["end_s"],
            }
        )

    matched_ids = {item["group_index"] for item in matched}
    unmatched = []
    for group in valid_groups:
        if group["group_index"] in matched_ids:
            continue
        unmatched.append(group)

    highest = matched[-1] if matched else None
    next_expected_code = layout.stage_order[best_expected_idx] if best_expected_idx < len(layout.stage_order) else None
    next_expected = None
    if next_expected_code is not None:
        stage = layout.stages[next_expected_code]
        next_expected = {
            "code": next_expected_code,
            "bits": stage.bits(layout.code_bits),
            "label": stage.label,
            "meaning": stage.meaning,
        }

    if highest is None:
        inference = "No valid Phoenix stage burst decoded."
    elif next_expected is None:
        inference = f"Reached final known stage {highest['code']} ({highest['label']})."
    else:
        inference = (
            f"Best contiguous decoded run reaches stage {highest['code']} ({highest['label']}), "
            f"no later valid stage {next_expected['code']} ({next_expected['label']}) seen."
        )

    return {
        "layout": layout_to_dict(layout),
        "matched_sequence": matched,
        "highest_completed": highest,
        "next_expected": next_expected,
        "inference": inference,
        "valid_group_count": len(valid_groups),
        "unmatched_groups": [
            {
                "group_index": group["group_index"],
                "start_s": group["start_s"],
                "end_s": group["end_s"],
                "decode": group["decode"],
            }
            for group in unmatched
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Interpret Raspberry Pi 4 ACT LED analysis JSON")
    parser.add_argument("analysis_json", help="Path to analysis JSON or - for stdin")
    parser.add_argument("--layout", default="current", help="Probe layout name")
    parser.add_argument("--json", action="store_true", help="Emit JSON instead of text")
    args = parser.parse_args()

    analysis = load_json(args.analysis_json)
    interpretation = build_interpretation(analysis, args.layout)

    if args.json:
        json.dump(interpretation, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
        return 0

    print(f"layout={interpretation['layout']['name']}")
    print(f"inference={interpretation['inference']}")

    highest = interpretation["highest_completed"]
    if highest is not None:
        print(
            "highest_completed="
            f"{highest['code']} {highest['bits']} {highest['label']} "
            f"@ group {highest['group_index']} {highest['start_s']:.3f}-{highest['end_s']:.3f}s"
        )
    else:
        print("highest_completed=none")

    next_expected = interpretation["next_expected"]
    if next_expected is not None:
        print(
            "next_expected="
            f"{next_expected['code']} {next_expected['bits']} {next_expected['label']}"
        )

    print("matched_sequence:")
    for item in interpretation["matched_sequence"]:
        print(
            f"  {item['code']:>2} {item['bits']} {item['label']} "
            f"{item['start_s']:.3f}-{item['end_s']:.3f}s"
        )

    if interpretation["unmatched_groups"]:
        print("unmatched_groups:")
        for group in interpretation["unmatched_groups"]:
            decode = group.get("decode", {})
            print(
                f"  group {group['group_index']}: "
                f"{decode.get('bits', '?')} ({decode.get('code', '?')}) "
                f"{group['start_s']:.3f}-{group['end_s']:.3f}s"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
