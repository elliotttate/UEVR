#!/usr/bin/env python3
"""Offline scoring helpers for Stereo Forensics experiments.

This does not launch the game. It scores captures produced by UEVR's existing
eye screenshot/readback tooling and emits machine-readable experiment results.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    with path.open("rb") as f:
        magic = f.readline().strip()
        if magic not in (b"P6", b"P3"):
            raise ValueError(f"{path} is not a PPM P6/P3 file")

        tokens: list[bytes] = []
        while len(tokens) < 3:
            line = f.readline()
            if not line:
                raise ValueError(f"{path} truncated PPM header")
            line = line.split(b"#", 1)[0]
            tokens.extend(line.split())

        width, height, maxval = (int(tokens[0]), int(tokens[1]), int(tokens[2]))
        if maxval <= 0 or maxval > 255:
            raise ValueError(f"{path} unsupported maxval {maxval}")

        if magic == b"P6":
            data = f.read(width * height * 3)
            if len(data) != width * height * 3:
                raise ValueError(f"{path} truncated pixel payload")
            return width, height, data

        numbers = tokens[3:]
        for line in f:
            line = line.split(b"#", 1)[0]
            numbers.extend(line.split())
        if len(numbers) < width * height * 3:
            raise ValueError(f"{path} truncated P3 pixel payload")
        data = bytes(max(0, min(255, int(v))) for v in numbers[: width * height * 3])
        return width, height, data


def parse_roi(value: str, width: int, height: int) -> tuple[int, int, int, int]:
    if value.lower() in {"all", "full"}:
        return 0, 0, width, height
    parts = [int(p.strip()) for p in value.replace("x", ",").split(",") if p.strip()]
    if len(parts) != 4:
        raise ValueError("ROI must be x,y,w,h or all")
    x, y, w, h = parts
    x = max(0, min(width, x))
    y = max(0, min(height, y))
    w = max(0, min(width - x, w))
    h = max(0, min(height - y, h))
    return x, y, w, h


def mean_abs_delta(a_path: Path, b_path: Path, roi_text: str) -> dict[str, Any]:
    aw, ah, ad = read_ppm(a_path)
    bw, bh, bd = read_ppm(b_path)
    if (aw, ah) != (bw, bh):
        raise ValueError(f"image sizes differ: {a_path}={aw}x{ah}, {b_path}={bw}x{bh}")
    x, y, w, h = parse_roi(roi_text, aw, ah)
    if w == 0 or h == 0:
        raise ValueError("ROI is empty")

    total = 0
    samples = 0
    for yy in range(y, y + h):
        row = yy * aw * 3
        for xx in range(x, x + w):
            idx = row + xx * 3
            total += abs(ad[idx] - bd[idx])
            total += abs(ad[idx + 1] - bd[idx + 1])
            total += abs(ad[idx + 2] - bd[idx + 2])
            samples += 3
    return {
        "width": aw,
        "height": ah,
        "roi": {"x": x, "y": y, "width": w, "height": h},
        "mean_abs_delta": total / samples,
        "normalized_delta": total / (samples * 255.0),
    }


def load_sample_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not data.get("available", False):
        raise ValueError(f"{path} is not an available C-API sample: {data.get('error')}")
    return data


def rgba_mean_delta(a: dict[str, Any], b: dict[str, Any]) -> dict[str, Any]:
    av = a.get("rgba_mean", [])
    bv = b.get("rgba_mean", [])
    if len(av) < 3 or len(bv) < 3:
        raise ValueError("sample JSON must contain rgba_mean with at least RGB channels")
    deltas = [abs(float(av[i]) - float(bv[i])) for i in range(3)]
    mean_delta = sum(deltas) / 3.0
    return {
        "sample_origin": b.get("sample_origin", a.get("sample_origin")),
        "sampled_at_eye": b.get("sampled_at_eye", a.get("sampled_at_eye")),
        "sampled_w": b.get("sampled_w", a.get("sampled_w")),
        "sampled_h": b.get("sampled_h", a.get("sampled_h")),
        "baseline_rgba_mean": av,
        "trial_rgba_mean": bv,
        "mean_abs_delta": mean_delta,
        "normalized_delta": mean_delta / 255.0,
    }


def score_confirmation(args: argparse.Namespace) -> dict[str, Any]:
    trusted = not bool(getattr(args, "untrusted_score", False))
    return {
        "score_trusted": trusted,
        "applied_count": getattr(args, "applied_count", None),
        "confirmed_count": getattr(args, "confirmed_count", None),
        "reason": getattr(args, "untrusted_reason", None) or (None if trusted else "experiment did not confirm that the runtime action applied"),
    }


def write_score_result(args: argparse.Namespace, result: dict[str, Any], score: float, right_delta: float, left_delta: float) -> None:
    text = json.dumps(result, indent=2)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    if args.db:
        cmd = [
            sys.executable,
            str(Path(__file__).with_name("stereo_forensics_db.py")),
            "--db",
            args.db,
            "add-experiment",
            "--name",
            args.experiment,
            "--status",
            "scored",
            "--score",
            str(score),
            "--right-roi-delta",
            str(right_delta),
            "--left-roi-delta",
            str(left_delta),
            "--likely-causal",
            "1" if result["likely_causal"] else "0",
            "--result-json",
            json.dumps(result),
        ]
        if getattr(args, "rule", None):
            cmd.extend(["--rule", args.rule])
        if getattr(args, "rule_json", None):
            cmd.extend(["--rule-json", args.rule_json])
        if getattr(args, "promote_fix_rule", False):
            cmd.append("--promote-fix-rule")
        if getattr(args, "game", None):
            cmd.extend(["--game", args.game])
        if args.session:
            cmd.extend(["--session", args.session])
        subprocess.run(cmd, check=True)
    print(text)


def cmd_score(args: argparse.Namespace) -> int:
    left = mean_abs_delta(Path(args.baseline_left), Path(args.trial_left), args.roi)
    right = mean_abs_delta(Path(args.baseline_right), Path(args.trial_right), args.roi)
    left_delta = float(left["normalized_delta"])
    right_delta = float(right["normalized_delta"])
    score = right_delta - (left_delta * args.left_penalty)
    confirmation = score_confirmation(args)
    result = {
        "schema": "uevr.stereo_forensics.experiment_score.v2",
        "experiment": args.experiment,
        "control_model": "baseline_vs_intervention_same_eye",
        "roi": right["roi"],
        "left": left,
        "right": right,
        "left_penalty": args.left_penalty,
        "score": score,
        "likely_causal": confirmation["score_trusted"] and score >= args.threshold and right_delta >= args.min_right_delta,
        "confirmation": confirmation,
        "threshold": args.threshold,
        "min_right_delta": args.min_right_delta,
    }
    write_score_result(args, result, score, right_delta, left_delta)
    return 0


def cmd_score_samples(args: argparse.Namespace) -> int:
    left = rgba_mean_delta(load_sample_json(Path(args.baseline_left_json)), load_sample_json(Path(args.trial_left_json)))
    right = rgba_mean_delta(load_sample_json(Path(args.baseline_right_json)), load_sample_json(Path(args.trial_right_json)))
    left_delta = float(left["normalized_delta"])
    right_delta = float(right["normalized_delta"])
    score = right_delta - (left_delta * args.left_penalty)
    confirmation = score_confirmation(args)
    result = {
        "schema": "uevr.stereo_forensics.experiment_score.v2",
        "experiment": args.experiment,
        "control_model": "baseline_vs_intervention_same_eye",
        "input_type": "uevr_render_diag_eye_region_sample_json",
        "left": left,
        "right": right,
        "left_penalty": args.left_penalty,
        "score": score,
        "likely_causal": confirmation["score_trusted"] and score >= args.threshold and right_delta >= args.min_right_delta,
        "confirmation": confirmation,
        "threshold": args.threshold,
        "min_right_delta": args.min_right_delta,
    }
    write_score_result(args, result, score, right_delta, left_delta)
    return 0


def cmd_write_rule(args: argparse.Namespace) -> int:
    match: dict[str, Any] = {"kind": args.kind, "eye": args.eye}
    if args.ps_crc:
        match["ps_crc"] = args.ps_crc
    if args.cs_crc:
        match["cs_crc"] = args.cs_crc
    action_type = {
        "force_srv_slice": "force_srv_array_slice",
        "swap_descriptor": "swap_descriptor_from_left",
        "swap_cbv": "swap_cbv_left_to_right",
    }.get(args.action, args.action)
    action: dict[str, Any] = {"type": action_type}
    if action_type == "color_override" and args.rgb:
        action["rgb"] = [int(v) for v in args.rgb.split(",")]
    rule = {
        "schema": "uevr.stereo_forensics.rule_bundle.v2",
        "rules": [
            {
                "name": args.name,
                "enabled": True,
                "match": match,
                "action": action,
            }
        ]
    }
    text = json.dumps(rule, indent=2)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("score")
    p.add_argument("--experiment", required=True)
    p.add_argument("--baseline-left", required=True)
    p.add_argument("--baseline-right", required=True)
    p.add_argument("--trial-left", required=True)
    p.add_argument("--trial-right", required=True)
    p.add_argument("--roi", default="all", help="x,y,w,h or all")
    p.add_argument("--left-penalty", type=float, default=1.0)
    p.add_argument("--threshold", type=float, default=0.05)
    p.add_argument("--min-right-delta", type=float, default=0.02)
    p.add_argument("--out")
    p.add_argument("--db", help="Optionally write score to Stereo Forensics DB")
    p.add_argument("--session", help="Session path to link when writing to DB")
    p.add_argument("--rule", help="Rule file that produced the trial, used for DB promotion")
    p.add_argument("--rule-json", help="Inline rule JSON that produced the trial")
    p.add_argument("--promote-fix-rule", action="store_true", help="Promote likely-causal scored rule into fix_rules")
    p.add_argument("--game")
    p.add_argument("--applied-count", type=int)
    p.add_argument("--confirmed-count", type=int)
    p.add_argument("--untrusted-score", action="store_true")
    p.add_argument("--untrusted-reason")
    p.set_defaults(func=cmd_score)

    p = sub.add_parser("score-samples")
    p.add_argument("--experiment", required=True)
    p.add_argument("--baseline-left-json", required=True)
    p.add_argument("--baseline-right-json", required=True)
    p.add_argument("--trial-left-json", required=True)
    p.add_argument("--trial-right-json", required=True)
    p.add_argument("--left-penalty", type=float, default=1.0)
    p.add_argument("--threshold", type=float, default=0.05)
    p.add_argument("--min-right-delta", type=float, default=0.02)
    p.add_argument("--out")
    p.add_argument("--db", help="Optionally write score to Stereo Forensics DB")
    p.add_argument("--session", help="Session path to link when writing to DB")
    p.add_argument("--rule", help="Rule file that produced the trial, used for DB promotion")
    p.add_argument("--rule-json", help="Inline rule JSON that produced the trial")
    p.add_argument("--promote-fix-rule", action="store_true", help="Promote likely-causal scored rule into fix_rules")
    p.add_argument("--game")
    p.add_argument("--applied-count", type=int)
    p.add_argument("--confirmed-count", type=int)
    p.add_argument("--untrusted-score", action="store_true")
    p.add_argument("--untrusted-reason")
    p.set_defaults(func=cmd_score_samples)

    p = sub.add_parser("write-rule")
    p.add_argument("--name", required=True)
    p.add_argument("--kind", default="draw")
    p.add_argument("--eye", default="right")
    p.add_argument("--ps-crc")
    p.add_argument("--cs-crc")
    p.add_argument("--action", default="skip", choices=[
        "skip",
        "color_override",
        "swap_cbv_left_to_right",
        "swap_descriptor_from_left",
        "force_srv_array_slice",
        "force_srv_slice",
        "swap_descriptor",
        "swap_cbv",
    ])
    p.add_argument("--rgb", default="255,0,255")
    p.add_argument("--out")
    p.set_defaults(func=cmd_write_rule)

    return parser


def main() -> int:
    args = build_parser().parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
