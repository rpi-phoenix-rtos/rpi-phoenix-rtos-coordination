#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from dataclasses import dataclass
from typing import Any

import cv2
import numpy as np


@dataclass
class Segment:
    start: int
    end: int

    @property
    def frames(self) -> int:
        return self.end - self.start + 1


@dataclass
class BBox:
    x1: int
    y1: int
    x2: int
    y2: int

    @property
    def width(self) -> int:
        return self.x2 - self.x1 + 1

    @property
    def height(self) -> int:
        return self.y2 - self.y1 + 1

    def to_dict(self) -> dict[str, int]:
        return {"x1": int(self.x1), "y1": int(self.y1), "x2": int(self.x2), "y2": int(self.y2)}


def json_ready(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(k): json_ready(v) for k, v in value.items()}
    if isinstance(value, list):
        return [json_ready(v) for v in value]
    if isinstance(value, tuple):
        return [json_ready(v) for v in value]
    if isinstance(value, np.integer):
        return int(value)
    if isinstance(value, np.floating):
        return float(value)
    if isinstance(value, np.bool_):
        return bool(value)
    return value


def parse_roi(value: str) -> BBox:
    parts = [int(part) for part in value.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("ROI must be x1,y1,x2,y2")
    x1, y1, x2, y2 = parts
    if x2 < x1 or y2 < y1:
        raise argparse.ArgumentTypeError("ROI must satisfy x2>=x1 and y2>=y1")
    return BBox(x1, y1, x2, y2)


def seconds(frames: int, fps: float) -> float:
    return frames / fps


def luma(frame: np.ndarray) -> np.ndarray:
    return cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)


def moving_average(values: np.ndarray, window: int) -> np.ndarray:
    if window <= 1:
        return values.copy()
    kernel = np.ones(window, dtype=np.float64) / window
    return np.convolve(values, kernel, mode="same")


def kmeans_threshold(values: np.ndarray, p_lo: float = 15, p_hi: float = 85) -> tuple[float, float, float]:
    lo = float(np.percentile(values, p_lo))
    hi = float(np.percentile(values, p_hi))

    for _ in range(24):
        midpoint = (lo + hi) / 2.0
        lower = values[values <= midpoint]
        upper = values[values > midpoint]
        if len(lower) == 0 or len(upper) == 0:
            break
        new_lo = float(lower.mean())
        new_hi = float(upper.mean())
        if abs(new_lo - lo) < 1e-6 and abs(new_hi - hi) < 1e-6:
            break
        lo, hi = new_lo, new_hi

    return lo, hi, (lo + hi) / 2.0


def kmeans_centers(values: np.ndarray, count: int) -> list[float]:
    percentiles = np.linspace(10, 90, count)
    centers = [float(np.percentile(values, pct)) for pct in percentiles]

    for _ in range(32):
        buckets = [[] for _ in range(count)]
        for value in values:
            idx = min(range(count), key=lambda i: abs(value - centers[i]))
            buckets[idx].append(float(value))

        new_centers: list[float] = []
        for idx, bucket in enumerate(buckets):
            if bucket:
                new_centers.append(float(np.mean(bucket)))
            else:
                new_centers.append(centers[idx])

        if all(abs(a - b) < 1e-6 for a, b in zip(new_centers, centers)):
            break
        centers = new_centers

    centers.sort()
    return centers


def merge_short_false_runs(mask: np.ndarray, max_gap_frames: int) -> np.ndarray:
    out = mask.copy()
    i = 0
    n = len(out)
    while i < n:
        if out[i]:
            i += 1
            continue
        start = i
        while i < n and not out[i]:
            i += 1
        end = i - 1
        if start == 0 or end == n - 1:
            continue
        if end - start + 1 <= max_gap_frames:
            out[start : end + 1] = True
    return out


def mask_to_segments(mask: np.ndarray, min_frames: int) -> list[Segment]:
    segments: list[Segment] = []
    start: int | None = None
    for idx, is_on in enumerate(mask):
        if is_on and start is None:
            start = idx
        elif not is_on and start is not None:
            if idx - start >= min_frames:
                segments.append(Segment(start, idx - 1))
            start = None
    if start is not None and len(mask) - start >= min_frames:
        segments.append(Segment(start, len(mask) - 1))
    return segments


def pad_box(x: int, y: int, w: int, h: int, frame_w: int, frame_h: int, pad: int) -> BBox:
    x1 = max(0, x - pad)
    y1 = max(0, y - pad)
    x2 = min(frame_w - 1, x + w - 1 + pad)
    y2 = min(frame_h - 1, y + h - 1 + pad)
    return BBox(x1, y1, x2, y2)


def auto_detect_led_roi(video_path: str, max_samples: int = 400, percentile: float = 99.85) -> tuple[BBox, dict[str, Any]]:
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise SystemExit(f"failed to open video: {video_path}")

    frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    step = max(1, frame_count // max_samples) if frame_count > 0 else 10

    sample_count = 0
    mean_map = None
    m2_map = None
    min_map = None
    max_map = None
    motion_map = None
    prev_metric = None
    height = 0
    width = 0
    idx = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            break
        if idx % step != 0:
            idx += 1
            continue

        metric = luma(frame)
        if mean_map is None:
            height, width = metric.shape
            mean_map = np.zeros_like(metric, dtype=np.float64)
            m2_map = np.zeros_like(metric, dtype=np.float64)
            min_map = np.full_like(metric, np.inf, dtype=np.float64)
            max_map = np.full_like(metric, -np.inf, dtype=np.float64)
            motion_map = np.zeros_like(metric, dtype=np.float64)

        sample_count += 1
        delta = metric - mean_map
        mean_map += delta / sample_count
        delta2 = metric - mean_map
        m2_map += delta * delta2
        min_map = np.minimum(min_map, metric)
        max_map = np.maximum(max_map, metric)
        if prev_metric is not None:
            motion_map += np.abs(metric - prev_metric)
        prev_metric = metric
        idx += 1

    cap.release()

    if sample_count == 0 or mean_map is None or m2_map is None or min_map is None or max_map is None or motion_map is None:
        raise SystemExit("video has no readable frames")

    variance = m2_map / max(sample_count - 1, 1)
    std_map = np.sqrt(np.maximum(variance, 0.0))
    range_map = np.maximum(max_map - min_map, 0.0)
    score = motion_map + (0.10 * range_map) + (0.10 * std_map)
    score = cv2.GaussianBlur(score, (0, 0), 2.0)

    positive = score[score > np.percentile(score, 70)]
    threshold = float(np.percentile(positive, percentile)) if positive.size else float(score.max())
    mask = np.uint8(score >= threshold)

    kernel = np.ones((3, 3), dtype=np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
    count, labels, stats, _ = cv2.connectedComponentsWithStats(mask, 8)

    candidates: list[dict[str, Any]] = []
    best_box = None
    best_score = -math.inf
    for label in range(1, count):
        x, y, w, h, area = stats[label]
        if area < 4:
            continue
        component = score[labels == label]
        component_score = float(component.max() * 4.0 + component.mean() - (0.02 * area))
        pad = max(4, int(max(w, h) * 1.5))
        box = pad_box(x, y, w, h, width, height, pad)
        candidate = {
            "score": component_score,
            "area": int(area),
            "bbox": box.to_dict(),
        }
        candidates.append(candidate)
        if component_score > best_score:
            best_score = component_score
            best_box = box

    if best_box is None:
        y, x = np.unravel_index(np.argmax(score), score.shape)
        best_box = pad_box(int(x), int(y), 1, 1, width, height, 12)
        candidates.append({"score": float(score.max()), "area": 1, "bbox": best_box.to_dict()})

    candidates.sort(key=lambda item: item["score"], reverse=True)
    return best_box, {
        "sample_count": sample_count,
        "sample_step": step,
        "threshold": threshold,
        "candidates": candidates[:5],
    }


def roi_metric_value(frame: np.ndarray, roi: BBox, baseline: np.ndarray, active_mask: np.ndarray) -> float:
    region = frame[roi.y1 : roi.y2 + 1, roi.x1 : roi.x2 + 1]
    metric = np.clip(luma(region) - baseline, 0.0, None)
    if active_mask.any():
        metric = metric[active_mask]
    else:
        metric = metric.reshape(-1)
    metric = metric.reshape(-1)
    if metric.size == 0:
        return 0.0
    return float(metric.mean())


def extract_series(video_path: str, roi: BBox) -> tuple[np.ndarray, float, dict[str, Any]]:
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise SystemExit(f"failed to open video: {video_path}")

    fps = cap.get(cv2.CAP_PROP_FPS)
    baseline = None
    max_map = None
    motion_map = None
    prev_gray = None
    while True:
        ret, frame = cap.read()
        if not ret:
            break
        region = frame[roi.y1 : roi.y2 + 1, roi.x1 : roi.x2 + 1]
        gray = luma(region)
        if baseline is None:
            baseline = gray
            max_map = gray
            motion_map = np.zeros_like(gray, dtype=np.float64)
        else:
            baseline = np.minimum(baseline, gray)
            max_map = np.maximum(max_map, gray)
            motion_map += np.abs(gray - prev_gray)
        prev_gray = gray
    cap.release()

    if baseline is None or max_map is None or motion_map is None:
        raise SystemExit("video has no frames")

    flat_motion = motion_map.reshape(-1)
    top_n = max(8, flat_motion.size // 128)
    threshold = float(np.partition(flat_motion, flat_motion.size - top_n)[flat_motion.size - top_n])
    active_mask = motion_map >= threshold

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise SystemExit(f"failed to open video: {video_path}")

    values: list[float] = []
    while True:
        ret, frame = cap.read()
        if not ret:
            break
        values.append(roi_metric_value(frame, roi, baseline, active_mask))
    cap.release()
    return np.array(values, dtype=np.float64), fps, {
        "active_pixels": int(active_mask.sum()),
        "active_threshold": threshold,
        "roi_pixel_count": int(active_mask.size),
    }


def classify_durations(values: np.ndarray) -> dict[str, float]:
    if len(values) < 3:
        return {
            "short_s": math.nan,
            "long_s": math.nan,
            "sync_s": math.nan,
            "bit_threshold_s": math.nan,
            "sync_threshold_s": math.nan,
        }
    short_s, long_s, sync_s = kmeans_centers(values, 3)
    return {
        "short_s": short_s,
        "long_s": long_s,
        "sync_s": sync_s,
        "bit_threshold_s": (short_s + long_s) / 2.0,
        "sync_threshold_s": (long_s + sync_s) / 2.0,
    }


def classify_gap_threshold(values: np.ndarray, fallback: float) -> float:
    if len(values) < 2:
        return fallback
    short_gap, long_gap = kmeans_centers(values, 2)
    if abs(long_gap - short_gap) < 0.10:
        return fallback
    return (short_gap + long_gap) / 2.0


def build_stage_groups(
    segments: list[Segment],
    fps: float,
    sync_threshold_s: float,
    group_gap_threshold_s: float,
    max_valid_pulse_s: float,
    bit_threshold_s: float,
) -> list[dict[str, Any]]:
    groups: list[dict[str, Any]] = []
    i = 0
    group_index = 1

    while i < len(segments):
        pulse = segments[i]
        pulse_s = seconds(pulse.frames, fps)
        if not math.isfinite(sync_threshold_s) or pulse_s < sync_threshold_s or pulse_s > max_valid_pulse_s:
            i += 1
            continue

        pulses = [pulse]
        j = i + 1
        valid = True
        while j < len(segments) and len(pulses) < 6:
            prev = pulses[-1]
            gap_s = seconds(segments[j].start - prev.end - 1, fps)
            if gap_s > group_gap_threshold_s:
                valid = False
                break
            pulses.append(segments[j])
            j += 1

        decode = None
        if len(pulses) == 6 and math.isfinite(bit_threshold_s):
            bits = []
            for seg in pulses[1:6]:
                bit = "1" if seconds(seg.frames, fps) > bit_threshold_s else "0"
                bits.append(bit)
            bit_string = "".join(bits)
            decode = {
                "valid": True,
                "bits": bit_string,
                "code": int(bit_string, 2),
            }
        elif len(pulses) >= 2:
            decode = {"valid": False}

        groups.append(
            {
                "group_index": group_index,
                "start_s": seconds(pulses[0].start, fps),
                "end_s": seconds(pulses[-1].end + 1, fps),
                "pulse_count": len(pulses),
                "pulses": [
                    {
                        "index": idx + 1,
                        "start_frame": seg.start,
                        "end_frame": seg.end,
                        "start_s": seconds(seg.start, fps),
                        "end_s": seconds(seg.end + 1, fps),
                        "duration_s": seconds(seg.frames, fps),
                    }
                    for idx, seg in enumerate(pulses)
                ],
                "decode": decode,
                "complete": valid and len(pulses) == 6,
            }
        )
        group_index += 1
        i = max(j, i + 1)

    return groups


def build_output(
    video: str,
    fps: float,
    frame_count: int,
    roi: BBox,
    roi_mode: str,
    roi_detection: dict[str, Any],
    signal_detection: dict[str, Any],
    threshold_info: dict[str, float],
    timing_info: dict[str, float],
    group_gap_threshold_s: float,
    segments: list[Segment],
    groups: list[dict[str, Any]],
    params: dict[str, Any],
) -> dict[str, Any]:
    off_gaps = []
    for left, right in zip(segments, segments[1:]):
        off_frames = right.start - left.end - 1
        off_gaps.append(
            {
                "start_frame": left.end + 1,
                "end_frame": right.start - 1,
                "duration_s": seconds(off_frames, fps),
            }
        )

    return {
        "analysis_version": 2,
        "video": os.path.abspath(video),
        "fps": fps,
        "frame_count": frame_count,
        "roi_mode": roi_mode,
        "roi": roi.to_dict(),
        "roi_detection": roi_detection,
        "signal_detection": signal_detection,
        "threshold": threshold_info,
        "timing": {
            **timing_info,
            "group_gap_threshold_s": group_gap_threshold_s,
        },
        "parameters": params,
        "on_segments": [
            {
                "start_frame": seg.start,
                "end_frame": seg.end,
                "start_s": seconds(seg.start, fps),
                "end_s": seconds(seg.end + 1, fps),
                "duration_s": seconds(seg.frames, fps),
            }
            for seg in segments
        ],
        "off_gaps": off_gaps,
        "stage_groups": groups,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Raspberry Pi 4 ACT LED video and emit normalized JSON")
    parser.add_argument("video")
    parser.add_argument("--roi", type=parse_roi, help="Manual ROI x1,y1,x2,y2")
    parser.add_argument("--smooth", type=int, default=7, help="Moving-average window in frames")
    parser.add_argument("--detrend-window-s", type=float, default=1.50, help="Slow baseline window in seconds")
    parser.add_argument("--min-on", type=float, default=0.04, help="Minimum on-segment duration in seconds")
    parser.add_argument("--merge-gap", type=float, default=0.02, help="Merge off gaps shorter than this, in seconds")
    parser.add_argument("--fallback-group-gap", type=float, default=0.90, help="Fallback stage-group gap in seconds")
    parser.add_argument("--max-pulse", type=float, default=2.50, help="Ignore on-pulses longer than this during decode")
    parser.add_argument("--out", help="Write JSON to this path instead of stdout")
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON")
    args = parser.parse_args()

    if args.roi is None:
        roi, roi_detection = auto_detect_led_roi(args.video)
        roi_mode = "auto"
    else:
        roi = args.roi
        roi_detection = {"manual": True}
        roi_mode = "manual"

    series, fps, signal_detection = extract_series(args.video, roi)
    frame_count = len(series)
    if frame_count == 0:
        raise SystemExit("video has no frames")

    smoothed = moving_average(series, args.smooth)
    detrend_window = max(args.smooth + 2, int(round(args.detrend_window_s * fps)))
    if detrend_window % 2 == 0:
        detrend_window += 1
    slow = moving_average(series, detrend_window)
    filtered = np.clip(smoothed - slow, 0.0, None)

    low_mean, high_mean, threshold = kmeans_threshold(filtered)
    mask = filtered > threshold

    merge_gap_frames = max(1, int(round(args.merge_gap * fps)))
    min_on_frames = max(1, int(round(args.min_on * fps)))
    mask = merge_short_false_runs(mask, merge_gap_frames)
    segments = mask_to_segments(mask, min_on_frames)

    duration_values = np.array(
        [
            seconds(seg.frames, fps)
            for seg in segments
            if args.min_on <= seconds(seg.frames, fps) <= args.max_pulse
        ],
        dtype=np.float64,
    )
    timing_info = classify_durations(duration_values)

    gap_values = np.array(
        [
            seconds(right.start - left.end - 1, fps)
            for left, right in zip(segments, segments[1:])
            if seconds(right.start - left.end - 1, fps) <= 5.0
        ],
        dtype=np.float64,
    )
    group_gap_threshold_s = classify_gap_threshold(gap_values, args.fallback_group_gap)

    sync_threshold_s = timing_info["sync_threshold_s"]
    sync_s = timing_info["sync_s"]
    bit_threshold_s = timing_info["bit_threshold_s"]
    max_valid_pulse_s = min(args.max_pulse, sync_s * 1.75) if math.isfinite(sync_s) else args.max_pulse

    groups = build_stage_groups(
        segments=segments,
        fps=fps,
        sync_threshold_s=sync_threshold_s,
        group_gap_threshold_s=group_gap_threshold_s,
        max_valid_pulse_s=max_valid_pulse_s,
        bit_threshold_s=bit_threshold_s,
    )

    output = json_ready(
        build_output(
        video=args.video,
        fps=fps,
        frame_count=frame_count,
        roi=roi,
        roi_mode=roi_mode,
        roi_detection=roi_detection,
        signal_detection=signal_detection,
        threshold_info={
            "low_mean": low_mean,
            "high_mean": high_mean,
            "threshold": threshold,
        },
        timing_info=timing_info,
        group_gap_threshold_s=group_gap_threshold_s,
        segments=segments,
        groups=groups,
        params={
            "smooth": args.smooth,
            "detrend_window_s": args.detrend_window_s,
            "min_on_s": args.min_on,
            "merge_gap_s": args.merge_gap,
            "fallback_group_gap_s": args.fallback_group_gap,
            "max_pulse_s": args.max_pulse,
        },
        )
    )

    indent = 2 if args.pretty or args.out else None
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(output, f, indent=indent, sort_keys=True)
            f.write("\n")
    else:
        json.dump(output, sys.stdout, indent=indent, sort_keys=True)
        sys.stdout.write("\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
