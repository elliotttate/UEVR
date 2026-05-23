#!/usr/bin/env python3
"""Closed-loop A/B runner for Stereo Forensics experiment rules.

The runtime side hot-reloads ``UEVR_STEREO_EXPERIMENTS_FILE`` and the SN2 eye
screenshot hook writes ``left.ppm``/``right.ppm`` after a trigger file appears.
This driver owns the sequencing:

1. write no rules and capture a baseline,
2. write one candidate rule and capture a trial,
3. score baseline-vs-trial on the same eye,
4. record score/rule in the DB and optionally promote likely-causal winners.

It can launch the game with an optional command, but it also works against an
already-running game if the env vars were set before launch.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


DEFAULT_TRIGGER = Path(r"C:\tmp\uevr_shot_req.txt")
DEFAULT_SHOT_DIR = Path(r"C:\tmp\uevr_screenshots")
DEFAULT_MANIFEST = Path(__file__).resolve().parents[1] / "docs" / "sn2" / "sn2_hook_manifest.generated.json"
MUTATION_ACTIONS = {
    "swap_cbv_left_to_right",
    "swap_descriptor_from_left",
    "force_srv_array_slice",
    "neutralize_texture",
}
CONFIRMATION_ACTIONS = {
    "skip",
    "skip_draw",
    "skip_dispatch",
    "color_override",
    *MUTATION_ACTIONS,
}


def utc_now() -> str:
    return datetime.now(UTC).isoformat(timespec="seconds")


def load_json(path: Path, default: Any) -> Any:
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return default


def load_runtime_actions(manifest: str | Path | None) -> dict[str, set[str]]:
    actions = {
        "mutation": set(MUTATION_ACTIONS),
        "confirmation": set(CONFIRMATION_ACTIONS),
    }
    if not manifest:
        return actions
    try:
        doc = load_json(Path(manifest), {})
        runtime = doc.get("stereo_forensics_runtime_actions", {}) if isinstance(doc, dict) else {}
        if isinstance(runtime, dict):
            mutation = runtime.get("mutation_actions")
            executable = runtime.get("executable_actions")
            if isinstance(mutation, list):
                actions["mutation"] = {str(x) for x in mutation}
            if isinstance(executable, list):
                actions["confirmation"] = {str(x) for x in executable}
    except OSError:
        pass
    return actions


def action_type(rule: dict[str, Any]) -> str:
    action = rule.get("action", {})
    if isinstance(action, str):
        return action
    if isinstance(action, dict):
        return str(action.get("type") or "")
    return ""


def load_rules(path: Path) -> list[dict[str, Any]]:
    doc = load_json(path, {})
    if isinstance(doc, list):
        return [r for r in doc if isinstance(r, dict)]
    rules = doc.get("rules") if isinstance(doc, dict) else None
    if isinstance(rules, list):
        return [r for r in rules if isinstance(r, dict)]
    raise ValueError(f"{path} does not contain a rules[] array")


def bundle_for(rules: list[dict[str, Any]], label: str) -> dict[str, Any]:
    return {
        "schema": "uevr.stereo_forensics.rule_bundle.v2",
        "generated_at": utc_now(),
        "label": label,
        "rules": rules,
    }


def write_rules(path: Path, rules: list[dict[str, Any]], label: str) -> dict[str, Any]:
    doc = bundle_for(rules, label)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, path)
    return doc


def run_launch(args: argparse.Namespace) -> None:
    if not args.launch_cmd:
        return
    env = os.environ.copy()
    env["UEVR_STEREO_EXPERIMENTS"] = "1"
    env["UEVR_STEREO_EXPERIMENTS_FILE"] = str(Path(args.rules_file))
    env.setdefault("UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE", str(Path(args.shot_trigger)))
    env.setdefault("UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR", str(Path(args.shot_dir)))
    subprocess.run(args.launch_cmd, shell=True, check=True, env=env)


def capture_pair(args: argparse.Namespace, out_dir: Path, prefix: str) -> dict[str, Path]:
    shot_dir = Path(args.shot_dir)
    trigger = Path(args.shot_trigger)
    done = shot_dir / "done.txt"
    shot_dir.mkdir(parents=True, exist_ok=True)
    trigger.parent.mkdir(parents=True, exist_ok=True)

    stale_files = [
        done,
        trigger,
        shot_dir / "left_sample.json",
        shot_dir / "right_sample.json",
    ]
    for stale in stale_files:
        try:
            stale.unlink()
        except FileNotFoundError:
            pass

    trigger_text = f"stereo-forensics {prefix} {utc_now()}\n"
    if args.score_mode == "sample-json":
        if not args.sample_roi:
            raise ValueError("--score-mode sample-json requires --sample-roi x,y,w,h")
        trigger_text += f"sample={args.sample_roi}\n"
    trigger.write_text(trigger_text, encoding="utf-8")
    deadline = time.time() + args.capture_timeout
    while time.time() < deadline:
        if done.exists():
            time.sleep(args.post_capture_delay)
            break
        time.sleep(0.2)
    else:
        raise TimeoutError(f"timed out waiting for {done}")

    outputs: dict[str, Path] = {}
    for eye in ("left", "right"):
        src = shot_dir / f"{eye}.ppm"
        if not src.exists():
            raise FileNotFoundError(f"capture completed but {src} is missing")
        dst = out_dir / f"{prefix}_{eye}.ppm"
        shutil.copyfile(src, dst)
        outputs[eye] = dst

    backbuffer = shot_dir / "backbuffer.ppm"
    if backbuffer.exists():
        dst = out_dir / f"{prefix}_backbuffer.ppm"
        shutil.copyfile(backbuffer, dst)
        outputs["backbuffer"] = dst

    for eye in ("left", "right"):
        src = shot_dir / f"{eye}_sample.json"
        if src.exists():
            dst = out_dir / f"{prefix}_{eye}_sample.json"
            shutil.copyfile(src, dst)
            outputs[f"{eye}_sample"] = dst

    try:
        done.unlink()
    except FileNotFoundError:
        pass
    return outputs


def read_ppm_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as f:
        magic = f.readline().strip()
        if magic not in (b"P6", b"P3"):
            raise ValueError(f"{path} is not a PPM file")
        tokens: list[bytes] = []
        while len(tokens) < 2:
            line = f.readline()
            if not line:
                raise ValueError(f"{path} has a truncated PPM header")
            line = line.split(b"#", 1)[0]
            tokens.extend(line.split())
        return int(tokens[0]), int(tokens[1])


def normalize_roi(rect: dict[str, Any], image_path: Path) -> str | None:
    try:
        img_w, img_h = read_ppm_size(image_path)
        x = int(rect.get("x", 0))
        y = int(rect.get("y", 0))
        w = int(rect.get("width", 0))
        h = int(rect.get("height", 0))
    except (OSError, ValueError, TypeError):
        return None
    if w <= 0 or h <= 0 or img_w <= 0 or img_h <= 0:
        return None

    # D3D12 event viewports are often side-by-side backbuffer coordinates,
    # while the screenshot hook writes per-eye images. Convert a right-half
    # backbuffer rect to eye-local coordinates when it cleanly fits an eye.
    if x >= img_w and w <= img_w:
        x = 0
    elif x + w > img_w and w <= img_w:
        x = max(0, img_w - w)
    if y >= img_h and h <= img_h:
        y = 0
    elif y + h > img_h and h <= img_h:
        y = max(0, img_h - h)

    x = max(0, min(img_w, x))
    y = max(0, min(img_h, y))
    w = max(0, min(img_w - x, w))
    h = max(0, min(img_h - y, h))
    if w == 0 or h == 0:
        return None
    return f"{x},{y},{w},{h}"


def resolve_score_roi(args: argparse.Namespace, rule: dict[str, Any], right_image: Path) -> str:
    if args.roi.lower() != "auto":
        return args.roi
    rect = rule.get("score_roi")
    if isinstance(rect, dict):
        roi = normalize_roi(rect, right_image)
        if roi:
            return roi
    return "all"


def wait_for_rule_loaded(args: argparse.Namespace, runtime_exp: Path | None, rule_name: str) -> dict[str, Any]:
    if runtime_exp is None:
        time.sleep(args.settle_seconds)
        return {"waited": args.settle_seconds, "loaded": None, "reason": "no runtime experiments.json path"}
    deadline = time.time() + args.reload_timeout
    while time.time() < deadline:
        doc = load_json(runtime_exp, {})
        experiments = doc.get("experiments", []) if isinstance(doc, dict) else []
        for exp in experiments:
            if isinstance(exp, dict) and exp.get("name") == rule_name and exp.get("enabled", True):
                return {"waited": max(0.0, args.reload_timeout - (deadline - time.time())), "loaded": True}
        time.sleep(0.2)
    time.sleep(args.settle_seconds)
    return {"waited": args.reload_timeout + args.settle_seconds, "loaded": False}


def observation_counts(path: Path | None, rule_name: str) -> dict[str, Any]:
    if path is None:
        return {"available": False, "reason": "no runtime experiments.json path was provided"}
    doc = load_json(path, {})
    if not doc:
        return {"available": False, "reason": f"{path} is not readable yet"}

    counts: dict[str, int] = {}
    for rec in doc.get("observations", []):
        if not isinstance(rec, dict) or rec.get("name") != rule_name:
            continue
        outcome = str(rec.get("outcome") or "unknown")
        counts[outcome] = counts.get(outcome, 0) + int(rec.get("count") or 0)

    hits = 0
    for rec in doc.get("experiments", []):
        if isinstance(rec, dict) and rec.get("name") == rule_name:
            hits += int(rec.get("hits") or 0)

    applied = counts.get("applied", 0)
    confirmed = applied + hits
    return {
        "available": True,
        "path": str(path),
        "outcomes": counts,
        "skip_hits": hits,
        "applied_count": applied,
        "confirmed_count": confirmed,
    }


def score_trial(
    args: argparse.Namespace,
    rule: dict[str, Any],
    rule_file: Path,
    baseline: dict[str, Path],
    trial: dict[str, Path],
    score_file: Path,
    observations: dict[str, Any],
    runtime_actions: dict[str, set[str]],
    roi: str,
) -> dict[str, Any]:
    name = str(rule.get("name") or rule_file.stem)
    act = action_type(rule)
    mutation_actions = runtime_actions["mutation"]
    needs_confirmation = act in runtime_actions["confirmation"]
    applied = int(observations.get("applied_count") or 0)
    confirmed = int(observations.get("confirmed_count") or 0)
    trusted = True
    untrusted_reason = ""
    if args.require_applied and needs_confirmation and confirmed <= 0:
        trusted = False
        untrusted_reason = "rule had no runtime hit/apply confirmation"
        if act in mutation_actions and applied <= 0:
            untrusted_reason = "mutation action had no applied observation"
        if not observations.get("available"):
            untrusted_reason = str(observations.get("reason") or "runtime observations unavailable")

    if args.score_mode == "sample-json":
        cmd = [
            sys.executable,
            str(Path(__file__).with_name("stereo_forensics_experiment.py")),
            "score-samples",
            "--experiment",
            name,
            "--baseline-left-json",
            str(baseline["left_sample"]),
            "--baseline-right-json",
            str(baseline["right_sample"]),
            "--trial-left-json",
            str(trial["left_sample"]),
            "--trial-right-json",
            str(trial["right_sample"]),
            "--left-penalty",
            str(args.left_penalty),
            "--threshold",
            str(args.threshold),
            "--min-right-delta",
            str(args.min_right_delta),
            "--out",
            str(score_file),
            "--rule",
            str(rule_file),
            "--applied-count",
            str(applied),
            "--confirmed-count",
            str(confirmed),
        ]
    else:
        cmd = [
            sys.executable,
            str(Path(__file__).with_name("stereo_forensics_experiment.py")),
            "score",
            "--experiment",
            name,
            "--baseline-left",
            str(baseline["left"]),
            "--baseline-right",
            str(baseline["right"]),
            "--trial-left",
            str(trial["left"]),
            "--trial-right",
            str(trial["right"]),
            "--roi",
            roi,
            "--left-penalty",
            str(args.left_penalty),
            "--threshold",
            str(args.threshold),
            "--min-right-delta",
            str(args.min_right_delta),
            "--out",
            str(score_file),
            "--rule",
            str(rule_file),
            "--applied-count",
            str(applied),
            "--confirmed-count",
            str(confirmed),
        ]
    if not trusted:
        cmd.extend(["--untrusted-score", "--untrusted-reason", untrusted_reason])
    if args.db:
        cmd.extend(["--db", args.db])
    if args.session:
        cmd.extend(["--session", args.session])
    if args.game:
        cmd.extend(["--game", args.game])
    if args.promote_fix_rule:
        cmd.append("--promote-fix-rule")

    subprocess.run(cmd, check=True)
    result = load_json(score_file, {})
    result["runtime_observations"] = observations
    result["score_trusted"] = trusted
    if untrusted_reason:
        result["untrusted_reason"] = untrusted_reason
    score_file.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def cmd_run(args: argparse.Namespace) -> int:
    rules = load_rules(Path(args.candidate_rules))
    if args.limit is not None:
        rules = rules[: args.limit]
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    rules_file = Path(args.rules_file)
    runtime_exp = Path(args.runtime_experiments_json) if args.runtime_experiments_json else None
    runtime_actions = load_runtime_actions(args.manifest)

    run_launch(args)
    if args.warmup_seconds > 0:
        time.sleep(args.warmup_seconds)

    results: list[dict[str, Any]] = []
    shared_baseline: dict[str, Path] | None = None
    try:
        write_rules(rules_file, [], "baseline disabled")
        time.sleep(args.settle_seconds)
        if args.baseline_mode == "once":
            if args.score_mode == "sample-json":
                raise ValueError("--score-mode sample-json requires --baseline-mode per-rule so each rule can use the requested sample ROI")
            shared_baseline = capture_pair(args, out_dir, "baseline")

        for index, rule in enumerate(rules, 1):
            name = str(rule.get("name") or f"rule_{index:03d}")
            safe_name = "".join(c if c.isalnum() or c in "._-" else "_" for c in name)[:120]
            trial_dir = out_dir / f"{index:03d}_{safe_name}"
            trial_dir.mkdir(parents=True, exist_ok=True)

            if shared_baseline is None:
                write_rules(rules_file, [], f"baseline before {name}")
                time.sleep(args.settle_seconds)
                baseline = capture_pair(args, trial_dir, "baseline")
            else:
                baseline = shared_baseline

            rule_doc = write_rules(rules_file, [rule], name)
            rule_file = trial_dir / "rule.json"
            rule_file.write_text(json.dumps(rule_doc, indent=2) + "\n", encoding="utf-8")
            reload_status = wait_for_rule_loaded(args, runtime_exp, name)
            trial = capture_pair(args, trial_dir, "trial")
            roi = resolve_score_roi(args, rule, trial["right"])

            observations = observation_counts(runtime_exp, name)
            score = score_trial(
                args,
                rule,
                rule_file,
                baseline,
                trial,
                trial_dir / "score.json",
                observations,
                runtime_actions,
                roi,
            )
            results.append({
                "index": index,
                "name": name,
                "action": action_type(rule),
                "rule_file": str(rule_file),
                "score_file": str(trial_dir / "score.json"),
                "score": score.get("score"),
                "likely_causal": score.get("likely_causal"),
                "score_trusted": score.get("score_trusted"),
                "roi": roi,
                "reload": reload_status,
                "observations": observations,
            })
            write_rules(rules_file, [], f"restore after {name}")
            time.sleep(args.settle_seconds)
    finally:
        if args.restore_rules:
            write_rules(rules_file, [], "final restore")

    results.sort(key=lambda r: (bool(r.get("score_trusted")), float(r.get("score") or -9999.0)), reverse=True)
    summary = {
        "schema": "uevr.stereo_forensics.ab_loop.v1",
        "generated_at": utc_now(),
        "candidate_rules": str(Path(args.candidate_rules)),
        "rules_file": str(rules_file),
        "baseline_mode": args.baseline_mode,
        "roi": args.roi,
        "score_mode": args.score_mode,
        "sample_roi": args.sample_roi,
        "runtime_actions": {
            "mutation": sorted(runtime_actions["mutation"]),
            "confirmation": sorted(runtime_actions["confirmation"]),
        },
        "results": results,
    }
    (out_dir / "results.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("run")
    p.add_argument("--candidate-rules", required=True)
    p.add_argument("--rules-file", required=True, help="Path watched by UEVR_STEREO_EXPERIMENTS_FILE")
    p.add_argument("--out-dir", required=True)
    p.add_argument("--shot-trigger", default=str(DEFAULT_TRIGGER))
    p.add_argument("--shot-dir", default=str(DEFAULT_SHOT_DIR))
    p.add_argument("--runtime-experiments-json", help="Session experiments.json for applied/stale/missing-source counts")
    p.add_argument("--manifest", default=str(DEFAULT_MANIFEST), help="Generated hook manifest containing runtime action capabilities")
    p.add_argument("--launch-cmd", help="Optional command to launch the game before capture")
    p.add_argument("--warmup-seconds", type=float, default=0.0)
    p.add_argument("--settle-seconds", type=float, default=2.5)
    p.add_argument("--reload-timeout", type=float, default=8.0)
    p.add_argument("--capture-timeout", type=float, default=20.0)
    p.add_argument("--post-capture-delay", type=float, default=0.5)
    p.add_argument("--baseline-mode", choices=["per-rule", "once"], default="per-rule")
    p.add_argument("--roi", default="auto", help="x,y,w,h, all, or auto from the candidate event viewport/scissor")
    p.add_argument("--score-mode", choices=["ppm", "sample-json"], default="ppm")
    p.add_argument("--sample-roi", help="x,y,w,h eye-local ROI for runtime uevr_render_diag_eye_region_sample_json sidecars")
    p.add_argument("--left-penalty", type=float, default=1.0)
    p.add_argument("--threshold", type=float, default=0.05)
    p.add_argument("--min-right-delta", type=float, default=0.02)
    p.add_argument("--limit", type=int)
    p.add_argument("--db")
    p.add_argument("--session")
    p.add_argument("--game")
    p.add_argument("--promote-fix-rule", action="store_true")
    p.add_argument("--no-require-applied", dest="require_applied", action="store_false")
    p.add_argument("--no-restore-rules", dest="restore_rules", action="store_false")
    p.set_defaults(func=cmd_run, require_applied=True, restore_rules=True)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
