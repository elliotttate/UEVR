#!/usr/bin/env python3
"""Compile a Stereo Forensics event/experiment into a durable rule JSON."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def parse_u64(text: str | None) -> int | None:
    if text is None:
        return None
    return int(text, 0)


def iter_events(session: Path):
    with (session / "events.jsonl").open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                yield json.loads(line)


def find_event(session: Path, event_id: int) -> dict[str, Any]:
    for event in iter_events(session):
        if int(event.get("event_index", 0)) == event_id:
            return event
    raise SystemExit(f"event {event_id} not found in {session}")


def first_descriptor_read(event: dict[str, Any], root: int | None, slot: int | None) -> dict[str, Any] | None:
    for read in event.get("descriptor_reads", []):
        if root is not None and int(read.get("root", -1)) != root:
            continue
        if slot is not None and int(read.get("slot", -1)) != slot:
            continue
        return read
    return None


ACTION_ALIASES = {
    "force_srv_slice": "force_srv_array_slice",
    "swap_descriptor": "swap_descriptor_from_left",
    "swap_cbv": "swap_cbv_left_to_right",
    "neutralize_srv": "neutralize_texture",
    "null_srv": "neutralize_texture",
    "duplicate_draw": "duplicate_left_work_into_right_bucket",
    "duplicate_dispatch": "duplicate_left_work_into_right_bucket",
}


def canonical_action(action: str) -> str:
    return ACTION_ALIASES.get(action, action)


def cmd_compile(args: argparse.Namespace) -> int:
    session = Path(args.session)
    event = find_event(session, args.event)
    root = parse_u64(args.root)
    slot = parse_u64(args.slot)
    read = first_descriptor_read(event, root, slot)
    action_type = canonical_action(args.action)

    match: dict[str, Any] = {
        "kind": event.get("kind"),
        "eye": args.eye or ("right" if event.get("eye_bucket") == 2 else "left" if event.get("eye_bucket") == 1 else "both"),
    }
    if event.get("ps_crc"):
        match["ps_crc"] = event.get("ps_crc_hex")
    if event.get("cs_crc"):
        match["cs_crc"] = event.get("cs_crc_hex")
    if event.get("root_signature_hex"):
        match["root_signature"] = event.get("root_signature_hex")
    if event.get("root_signature_hash_hex"):
        match["root_signature_hash"] = event.get("root_signature_hash_hex")
    if event.get("shader_key"):
        match["shader_key"] = event.get("shader_key")

    guard: dict[str, Any] = {}
    if read:
        guard["descriptor_root"] = read.get("root")
        guard["descriptor_slot"] = read.get("slot")
        guard["descriptor_type"] = read.get("descriptor_type")
        guard["resource_desc_key"] = read.get("descriptor", {}).get("resource_desc_key")
        guard["view_key"] = read.get("view_key") or read.get("descriptor", {}).get("view_key")

    action: dict[str, Any] = {"type": action_type}
    if action_type == "color_override":
        action["rgb"] = [int(v) for v in args.rgb.split(",")]
    elif action_type == "force_srv_array_slice":
        action["root"] = root
        action["slot"] = slot
        action["array_slice"] = parse_u64(args.slice)
        action["target_eye"] = args.eye or "right"
    elif action_type == "swap_descriptor_from_left":
        action["root"] = root
        action["slot"] = slot
        action["source_eye"] = args.to or "left"
        action["target_eye"] = args.eye or "right"
    elif action_type == "neutralize_texture":
        action["root"] = root
        action["slot"] = slot
        action["target_eye"] = args.eye or "right"
    elif action_type == "swap_cbv_left_to_right":
        action["root"] = root
        action["source_eye"] = args.to or "left"
        action["target_eye"] = args.eye or "right"
    elif action_type == "replace_ps_bytecode":
        action["bytecode"] = args.bytecode
    elif action_type == "patch_cb_bytes":
        action["root"] = root
        action["patch"] = args.patch

    rule = {
        "schema": "uevr.stereo_forensics.rule_bundle.v2",
        "rules": [
            {
                "name": args.name or f"{action_type}_event_{args.event}",
                "enabled": True,
                "match": match,
                "guard": guard,
                "action": action,
                "source": {
                    "session": str(session),
                    "event": args.event,
                    "frame": event.get("frame"),
                },
            }
        ],
    }
    text = json.dumps(rule, indent=2)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    if args.db:
        first_rule = rule["rules"][0]
        cmd = [
            sys.executable,
            str(Path(__file__).with_name("stereo_forensics_db.py")),
            "--db",
            args.db,
            "add-fix-rule",
            "--name",
            first_rule["name"],
            "--status",
            args.status,
            "--guards",
            json.dumps(first_rule.get("guard", {})),
            "--actions",
            json.dumps(first_rule.get("action", {})),
            "--rule-json",
            json.dumps(rule),
        ]
        if args.game:
            cmd.extend(["--game", args.game])
        if args.finding_id:
            cmd.extend(["--finding-id", args.finding_id])
        if args.experiment_id:
            cmd.extend(["--experiment-id", str(args.experiment_id)])
        subprocess.run(cmd, check=True)
    print(text)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("compile")
    p.add_argument("--session", required=True)
    p.add_argument("--event", type=int, required=True)
    p.add_argument("--name")
    p.add_argument("--eye")
    p.add_argument("--action", required=True, choices=[
        "skip",
        "color_override",
        "force_srv_slice",
        "force_srv_array_slice",
        "swap_descriptor",
        "swap_descriptor_from_left",
        "neutralize_srv",
        "null_srv",
        "neutralize_texture",
        "swap_cbv",
        "swap_cbv_left_to_right",
        "replace_ps_bytecode",
        "patch_cb_bytes",
        "duplicate_draw",
        "duplicate_dispatch",
        "duplicate_left_work_into_right_bucket",
    ])
    p.add_argument("--root")
    p.add_argument("--slot")
    p.add_argument("--slice")
    p.add_argument("--to")
    p.add_argument("--bytecode")
    p.add_argument("--patch")
    p.add_argument("--rgb", default="255,0,255")
    p.add_argument("--out")
    p.add_argument("--db", help="Optionally write compiled rule to Stereo Forensics DB")
    p.add_argument("--game")
    p.add_argument("--status", default="experimental")
    p.add_argument("--finding-id")
    p.add_argument("--experiment-id", type=int)
    p.set_defaults(func=cmd_compile)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
