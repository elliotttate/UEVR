#!/usr/bin/env python3
"""Generate ranked Stereo Forensics experiment candidates from the SQLite DB.

This is the bridge between automatic diagnosis and runtime intervention. It
does not launch the game; it turns persistent suspects into v2 rule files that
UEVR can load with UEVR_STEREO_EXPERIMENTS_FILE, then optionally records the
planned experiments back into the DB.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
from pathlib import Path
from typing import Any

from stereo_forensics_db import DEFAULT_DB, connect, init_db, json_text, rank_suspects, utc_now


DEFAULT_MANIFEST = Path(__file__).resolve().parents[1] / "docs" / "sn2" / "sn2_hook_manifest.generated.json"

EXECUTABLE_ACTIONS = {
    "skip",
    "skip_draw",
    "skip_dispatch",
    "color_override",
    "swap_cbv_left_to_right",
    "swap_descriptor_from_left",
    "force_srv_array_slice",
    "neutralize_texture",
}

MUTATION_ACTIONS = {
    "swap_cbv_left_to_right",
    "swap_descriptor_from_left",
    "force_srv_array_slice",
    "neutralize_texture",
}

UNSUPPORTED_ACTIONS = {
    "duplicate_left_work_into_right_bucket",
    "replace_shader_from_left_permutation",
    "replace_ps_bytecode",
    "patch_cb_bytes",
}


def load_runtime_actions(manifest: str | Path | None) -> dict[str, set[str]]:
    actions = {
        "executable": set(EXECUTABLE_ACTIONS),
        "mutation": set(MUTATION_ACTIONS),
        "unsupported": set(UNSUPPORTED_ACTIONS),
    }
    if not manifest:
        return actions
    try:
        with Path(manifest).open("r", encoding="utf-8") as f:
            doc = json.load(f)
        runtime = doc.get("stereo_forensics_runtime_actions", {})
        if isinstance(runtime, dict):
            if isinstance(runtime.get("executable_actions"), list):
                actions["executable"] = {str(x) for x in runtime["executable_actions"]}
            if isinstance(runtime.get("mutation_actions"), list):
                actions["mutation"] = {str(x) for x in runtime["mutation_actions"]}
            if isinstance(runtime.get("unsupported_actions"), list):
                actions["unsupported"] = {str(x) for x in runtime["unsupported_actions"]}
    except (OSError, json.JSONDecodeError):
        pass
    return actions


def event_doc(con: sqlite3.Connection, capture_id: int, event_index: int | None) -> dict[str, Any]:
    if event_index is None:
        return {}
    row = con.execute(
        "SELECT json FROM events WHERE capture_id=? AND event_index=?",
        (capture_id, event_index),
    ).fetchone()
    if not row:
        return {}
    try:
        return json.loads(row["json"] or "{}")
    except json.JSONDecodeError:
        return {}


def event_shader_match(event: dict[str, Any], fallback_crc: str | None) -> dict[str, Any]:
    match: dict[str, Any] = {"kind": event.get("kind", "draw"), "eye": "right"}
    kind = str(event.get("kind") or "")
    if kind.startswith("dispatch"):
        crc = event.get("cs_crc_hex") or fallback_crc
        if crc:
            match["cs_crc"] = crc
    else:
        crc = event.get("ps_crc_hex") or fallback_crc
        if crc:
            match["ps_crc"] = crc
    if event.get("root_signature_hash_hex"):
        match["root_signature_hash"] = event.get("root_signature_hash_hex")
    if event.get("shader_key"):
        match["shader_key"] = event.get("shader_key")
    return match


def rect_from_event(event: dict[str, Any]) -> dict[str, Any] | None:
    for source in ("scissor", "viewport"):
        item = event.get(source, {})
        if not isinstance(item, dict) or not item.get("valid"):
            continue
        if source == "scissor":
            left = int(item.get("left", 0))
            top = int(item.get("top", 0))
            right = int(item.get("right", left))
            bottom = int(item.get("bottom", top))
            width = max(0, right - left)
            height = max(0, bottom - top)
            x = left
            y = top
        else:
            x = int(round(float(item.get("x", 0))))
            y = int(round(float(item.get("y", 0))))
            width = int(round(float(item.get("width", 0))))
            height = int(round(float(item.get("height", 0))))
        if width > 0 and height > 0:
            return {"source": source, "x": x, "y": y, "width": width, "height": height}
    return None


def action_type(action: dict[str, Any]) -> str:
    return str(action.get("type") or "")


def probe_action_for(kind: str | None, requested: dict[str, Any] | None = None) -> dict[str, Any]:
    rgb = [255, 128, 0] if kind == "eye_event_count_mismatch" else [255, 0, 255]
    action: dict[str, Any] = {
        "type": "color_override",
        "rgb": rgb,
        "note": "probe visible ROI impact before attempting a heavier unsupported mutation",
        "executable": True,
        "fallback_probe": requested is not None,
    }
    if requested:
        action["requested_type"] = action_type(requested)
    return action


def action_for_suspect(kind: str | None, issue: dict[str, Any], mode: str) -> dict[str, Any]:
    if mode == "probes":
        return probe_action_for(kind)

    if kind == "graphics_cbv_hash_differs":
        return {
            "type": "swap_cbv_left_to_right",
            "root": issue.get("root"),
            "source_eye": "left",
            "target_eye": "right",
        }
    if kind in {"descriptor_resource_differs", "same_desc_different_resource", "descriptor_missing_on_right", "producer_lineage_differs"}:
        return {
            "type": "swap_descriptor_from_left",
            "root": issue.get("root"),
            "slot": issue.get("slot"),
            "source_eye": "left",
            "target_eye": "right",
        }
    if kind == "same_resource_different_slice":
        return {
            "type": "force_srv_array_slice",
            "root": issue.get("root"),
            "slot": issue.get("slot"),
            "array_slice": issue.get("left_first_array_slice", 0),
            "target_eye": "right",
        }
    if kind == "pso_or_shader_differs":
        return {"type": "replace_shader_from_left_permutation", "target_eye": "right"}
    if kind == "eye_event_count_mismatch":
        return {"type": "duplicate_left_work_into_right_bucket", "target_eye": "right"}
    return {"type": "color_override", "rgb": [255, 0, 255]}


def effective_action(
    requested: dict[str, Any],
    kind: str | None,
    include_unsupported: bool,
    runtime_actions: dict[str, set[str]],
) -> tuple[dict[str, Any], dict[str, Any]]:
    requested_type = action_type(requested)
    requested = dict(requested)
    executable_actions = runtime_actions["executable"]
    mutation_actions = runtime_actions["mutation"]
    unsupported_actions = runtime_actions["unsupported"]
    requested["executable"] = requested_type in executable_actions

    execution = {
        "requested_action": requested,
        "requested_action_type": requested_type,
        "executable": requested_type in executable_actions,
        "runtime_executable_actions": sorted(executable_actions),
        "runtime_mutation_actions": sorted(mutation_actions),
        "unsupported_actions": sorted(unsupported_actions),
    }
    if requested_type in executable_actions or include_unsupported:
        if requested_type not in executable_actions:
            requested["unsupported_reason"] = "no generic live D3D12 mutator currently executes this action"
        return requested, execution

    fallback = probe_action_for(kind, requested)
    execution.update({
        "executable": True,
        "fallback_probe": True,
        "effective_action_type": action_type(fallback),
        "fallback_reason": "requested action is not executable by the current runtime; using color probe to avoid false non-causal scores",
    })
    return fallback, execution


def candidate_for_suspect(
    con: sqlite3.Connection,
    suspect: sqlite3.Row,
    mode: str,
    include_unsupported: bool,
    runtime_actions: dict[str, set[str]],
) -> dict[str, Any]:
    evidence = json.loads(suspect["evidence_json"] or "{}")
    issue = evidence.get("issue", {})
    event = event_doc(con, int(suspect["capture_id"]), suspect["right_event"] or suspect["left_event"])
    name = f"sf_cap{suspect['capture_id']}_sus{suspect['id']}_{suspect['kind']}"
    requested_action = action_for_suspect(suspect["kind"], issue, mode)
    action, execution = effective_action(requested_action, suspect["kind"], include_unsupported, runtime_actions)
    rule = {
        "name": name,
        "enabled": True,
        "match": event_shader_match(event, suspect["shader_crc"]),
        "action": action,
        "execution": execution,
        "source": {
            "capture_id": suspect["capture_id"],
            "suspect_id": suspect["id"],
            "score": suspect["score"],
            "kind": suspect["kind"],
            "reason": suspect["reason"],
            "left_event": suspect["left_event"],
            "right_event": suspect["right_event"],
            "resource_hex": suspect["resource_hex"],
            "root": suspect["root"],
            "slot": suspect["slot"],
            "prescribed_action": requested_action,
        },
    }
    roi = rect_from_event(event)
    if roi is not None:
        rule["score_roi"] = roi
    return rule


def write_rules(out_dir: Path, rules: list[dict[str, Any]]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    bundle = {
        "schema": "uevr.stereo_forensics.experiment_rules.v2",
        "generated_at": utc_now(),
        "rules": rules,
    }
    (out_dir / "candidate_rules.json").write_text(json.dumps(bundle, indent=2) + "\n", encoding="utf-8")
    for i, rule in enumerate(rules, 1):
        doc = {"schema": bundle["schema"], "generated_at": bundle["generated_at"], "rules": [rule]}
        (out_dir / f"rule_{i:03d}_{rule['name']}.json").write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")


def record_planned_experiments(con: sqlite3.Connection, rules: list[dict[str, Any]]) -> None:
    now = utc_now()
    for rule in rules:
        source = rule.get("source", {})
        con.execute(
            """
            INSERT INTO experiments(name, capture_id, status, action, rule_json, result_json, created_at)
            VALUES(?,?,?,?,?,?,?)
            """,
            (
                rule.get("name"),
                source.get("capture_id"),
                "planned",
                rule.get("action", {}).get("type"),
                json_text(rule),
                json_text({"planned_from_suspect": source}),
                now,
            ),
        )
    con.commit()


def cmd_generate(args: argparse.Namespace) -> int:
    out_dir = Path(args.out_dir)
    with connect(args.db) as con:
        init_db(con)
        runtime_actions = load_runtime_actions(args.manifest)
        suspects = rank_suspects(con, args.game, args.capture_id, args.limit)
        rules = [candidate_for_suspect(con, suspect, args.mode, args.include_unsupported, runtime_actions) for suspect in suspects]
        write_rules(out_dir, rules)
        if args.record_db:
            record_planned_experiments(con, rules)
    print(f"generated rules={len(rules)} out={out_dir.resolve()}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", default=str(DEFAULT_DB))
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("generate")
    p.add_argument("--game")
    p.add_argument("--capture-id", type=int)
    p.add_argument("--limit", type=int, default=12)
    p.add_argument("--mode", choices=["probes", "mutations"], default="probes")
    p.add_argument("--include-unsupported", action="store_true", help="Emit non-executable mutation skeletons instead of substituting a color probe")
    p.add_argument("--manifest", default=str(DEFAULT_MANIFEST), help="Generated hook manifest containing runtime action capabilities")
    p.add_argument("--out-dir", required=True)
    p.add_argument("--record-db", action="store_true")
    p.set_defaults(func=cmd_generate)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
