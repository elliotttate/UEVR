#!/usr/bin/env python3
"""Query UEVR Stereo Forensics capture bundles.

The tool intentionally uses only the Python standard library so it can run on a
fresh debug machine beside a captured `session_*` directory.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable


def parse_u64(value: str | int | None) -> int | None:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    text = value.strip()
    if not text:
        return None
    return int(text, 0)


def parse_slot(value: str | int | None) -> int | None:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    text = value.strip().lower()
    if text.startswith(("t", "s", "u", "b")):
        text = text[1:]
    return int(text, 0)


def eye_name(value: int | None) -> str:
    return {1: "left", 2: "right", 3: "full", 4: "multi", -1: "unknown"}.get(value, str(value))


class Session:
    def __init__(self, path: Path) -> None:
        if path.is_file() and path.name == "manifest.json":
            path = path.parent
        self.path = path
        self.manifest = self._load_json("manifest.json", {})

    def _load_json(self, name: str, default: Any) -> Any:
        path = self.path / name
        if not path.exists():
            return default
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)

    def events(self) -> Iterable[dict[str, Any]]:
        path = self.path / "events.jsonl"
        if not path.exists():
            return
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    yield json.loads(line)
                except json.JSONDecodeError:
                    continue

    def resources(self) -> list[dict[str, Any]]:
        return list(self._load_json("resources.json", {}).get("resources", []))

    def descriptors(self) -> list[dict[str, Any]]:
        return list(self._load_json("descriptors.json", {}).get("descriptors", []))

    def eye_diff(self) -> dict[str, Any]:
        return dict(self._load_json("eye_diff.json", {}))

    def lineage(self) -> dict[str, Any]:
        return dict(self._load_json("lineage.json", {}))

    def find_event(self, event_id: int) -> dict[str, Any] | None:
        for event in self.events():
            if int(event.get("event_index", 0)) == event_id:
                return event
        return None


def print_json(data: Any) -> None:
    print(json.dumps(data, indent=2, sort_keys=False))


def resource_label(resource: dict[str, Any]) -> str:
    desc = resource.get("desc", {})
    dims = f"{desc.get('width', 0)}x{desc.get('height', 0)}x{desc.get('depth_or_array_size', 0)}"
    fmt = desc.get("format", 0)
    alias = resource.get("alias_group") or ""
    name = resource.get("name") or ""
    parts = [resource.get("resource_hex", "0x0"), dims, f"fmt={fmt}"]
    if alias:
        parts.append(f"alias={alias}")
    if name:
        parts.append(f"name={name}")
    return " ".join(parts)


def cmd_summary(args: argparse.Namespace) -> int:
    s = Session(Path(args.session))
    if args.ingest_db:
        db_cmd = "analyze-session" if args.analyze_db else "ingest-session"
        cmd = [
            sys.executable,
            str(Path(__file__).with_name("stereo_forensics_db.py")),
            "--db",
            args.ingest_db,
            db_cmd,
            str(s.path),
        ]
        if args.game:
            cmd.extend(["--game", args.game])
        if args.analyze_db and args.auto_findings:
            cmd.append("--auto-findings")
        subprocess.run(cmd, check=True)
    counts = Counter()
    eyes = Counter()
    shader_counts = Counter()
    for event in s.events():
        counts[event.get("event_class", "?")] += 1
        if event.get("event_class") == "work":
            eyes[eye_name(event.get("eye_bucket", -1))] += 1
            ps = event.get("ps_crc")
            cs = event.get("cs_crc")
            if ps:
                shader_counts[f"ps:{event.get('ps_crc_hex', hex(ps))}"] += 1
            if cs:
                shader_counts[f"cs:{event.get('cs_crc_hex', hex(cs))}"] += 1
    diff = s.eye_diff()
    issues = diff.get("issues", [])
    severities = Counter(i.get("severity", "?") for i in issues)
    print(f"session: {s.path}")
    print(f"latest_frame: {s.manifest.get('latest_frame', '?')}")
    print(f"events: {sum(counts.values())} {dict(counts)}")
    print(f"work_by_eye: {dict(eyes)}")
    print(f"resources: {len(s.resources())}")
    print(f"descriptors: {len(s.descriptors())}")
    print(f"issues: {len(issues)} {dict(severities)}")
    print("top_shaders:")
    for shader, count in shader_counts.most_common(12):
        print(f"  {shader} count={count}")
    return 0


def cmd_issues(args: argparse.Namespace) -> int:
    s = Session(Path(args.session))
    issues = s.eye_diff().get("issues", [])
    if args.severity:
        issues = [i for i in issues if i.get("severity") == args.severity]
    if args.kind:
        issues = [i for i in issues if i.get("kind") == args.kind]
    if args.json:
        print_json(issues)
        return 0
    for issue in issues[: args.limit]:
        left = issue.get("left_event") or issue.get("sample_left_event", {}).get("event_index")
        right = issue.get("right_event") or issue.get("sample_right_event", {}).get("event_index")
        confidence = issue.get("pair_confidence")
        suffix = f" confidence={confidence:.2f}" if isinstance(confidence, (int, float)) else ""
        print(f"{issue.get('severity','?'):>6} {issue.get('kind','?')} left={left} right={right}{suffix}")
        if args.verbose:
            print_json(issue)
    return 0


def cmd_event(args: argparse.Namespace) -> int:
    s = Session(Path(args.session))
    event = s.find_event(args.id)
    if event is None:
        print(f"event {args.id} not found", file=sys.stderr)
        return 1
    if args.json:
        print_json(event)
        return 0
    print(
        f"event {event.get('event_index')} frame={event.get('frame')} "
        f"{event.get('event_class')}:{event.get('kind')} eye={eye_name(event.get('eye_bucket', -1))}"
    )
    print(
        f"pso={event.get('pipeline_state_hex')} "
        f"vs={event.get('vs_crc_hex')} ps={event.get('ps_crc_hex')} gs={event.get('gs_crc_hex')} cs={event.get('cs_crc_hex')}"
    )
    print(
        f"root_sig={event.get('root_signature_hex')} "
        f"root_hash={event.get('root_signature_hash_hex')} shader_key={event.get('shader_key')} "
        f"executed={event.get('executed')}"
    )
    reads = event.get("descriptor_reads", [])
    writes = event.get("writes", [])
    print(f"reads={len(reads)} writes={len(writes)}")
    for read in reads[: args.limit]:
        desc = read.get("descriptor", {})
        producer = read.get("producer", {})
        view_key = read.get("view_key") or desc.get("view_key", "")
        print(
            f"  r root={read.get('root')} slot={read.get('slot')} type={read.get('descriptor_type')} "
            f"res={read.get('resource_hex')} view={view_key} prod={producer.get('event_index')}"
        )
    for write in writes[: args.limit]:
        print(
            f"  w kind={write.get('kind')} target={write.get('target_index')} "
            f"res={write.get('resource_hex')} view={write.get('view_key','')}"
        )
    return 0


def cmd_shader(args: argparse.Namespace) -> int:
    s = Session(Path(args.session))
    ps = parse_u64(args.ps)
    cs = parse_u64(args.cs)
    rows: list[dict[str, Any]] = []
    for event in s.events():
        if event.get("event_class") != "work":
            continue
        if ps is not None and int(event.get("ps_crc", 0)) != ps:
            continue
        if cs is not None and int(event.get("cs_crc", 0)) != cs:
            continue
        rows.append(event)
    if args.json:
        print_json(rows[: args.limit])
        return 0
    by_eye = Counter(eye_name(e.get("eye_bucket", -1)) for e in rows)
    print(f"matches={len(rows)} by_eye={dict(by_eye)}")
    for event in rows[: args.limit]:
        vp = event.get("viewport", {})
        print(
            f"event={event.get('event_index')} eye={eye_name(event.get('eye_bucket', -1))} "
            f"kind={event.get('kind')} pso={event.get('pipeline_state_hex')} "
            f"vp={vp.get('width')}x{vp.get('height')} rtv0={event.get('rtv0_resource_hex')}"
        )
    return 0


def cmd_lineage(args: argparse.Namespace) -> int:
    s = Session(Path(args.session))
    event = s.find_event(args.event)
    if event is None:
        print(f"event {args.event} not found", file=sys.stderr)
        return 1
    slot = parse_slot(args.slot)
    root = parse_u64(args.root)
    matches = []
    for read in event.get("descriptor_reads", []):
        if slot is not None and int(read.get("slot", -1)) != slot:
            continue
        if root is not None and int(read.get("root", -1)) != root:
            continue
        matches.append(read)
    if args.json:
        print_json(matches)
        return 0
    if not matches:
        print("no matching descriptor reads")
        return 1
    for read in matches:
        desc = read.get("descriptor", {})
        print(f"event={args.event} root={read.get('root')} slot={read.get('slot')} type={read.get('descriptor_type')}")
        print(f"  descriptor={read.get('cpu_hex')} source={read.get('source_cpu_hex')}")
        print(f"  resource={read.get('resource_hex')} desc_key={desc.get('resource_desc_key')}")
        print(f"  view_key={read.get('view_key') or desc.get('view_key')}")
        print(f"  mip={desc.get('most_detailed_mip', desc.get('mip_slice'))} slice={desc.get('first_array_slice')} plane={desc.get('plane_slice')}")
        print(f"  classification={read.get('classification', {})}")
        producer = read.get("producer")
        if producer:
            print(
                f"  producer event={producer.get('event_index')} frame={producer.get('frame')} "
                f"kind={producer.get('kind')} eye={eye_name(producer.get('eye_bucket', -1))}"
            )
        else:
            print("  producer=<none observed>")
    return 0


def cmd_alias(args: argparse.Namespace) -> int:
    s = Session(Path(args.session))
    needle = parse_u64(args.resource)
    resources = s.resources()
    target = None
    for resource in resources:
        if int(resource.get("resource", 0)) == needle or int(resource.get("id", 0)) == needle:
            target = resource
            break
    if target is None:
        print(f"resource {args.resource} not found", file=sys.stderr)
        return 1
    alias = target.get("alias_group", "")
    print(resource_label(target))
    if not alias:
        print("alias_group=<none>")
        return 0
    print(f"alias_group={alias}")
    for resource in resources:
        if resource.get("alias_group") == alias and resource is not target:
            print(f"  member {resource_label(resource)}")
    print("alias_barriers:")
    count = 0
    for event in s.events():
        if event.get("kind") != "resource_barrier":
            continue
        for barrier in event.get("barriers", []):
            if barrier.get("resource_before") == target.get("resource") or barrier.get("resource_after") == target.get("resource"):
                print(f"  event={event.get('event_index')} before={barrier.get('resource_before_hex')} after={barrier.get('resource_after_hex')}")
                count += 1
                if count >= args.limit:
                    return 0
    if count == 0:
        print("  <none in captured events>")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("summary")
    p.add_argument("session")
    p.add_argument("--ingest-db", help="Optionally ingest this session into the Stereo Forensics DB before summarizing")
    p.add_argument("--analyze-db", action="store_true", help="With --ingest-db, also materialize pairs, lineage paths, lifetimes, and suspects")
    p.add_argument("--auto-findings", action="store_true", help="With --analyze-db, promote high-scoring suspects to AUTO-* findings")
    p.add_argument("--game", help="Game label used with --ingest-db")
    p.set_defaults(func=cmd_summary)

    p = sub.add_parser("issues")
    p.add_argument("session")
    p.add_argument("--severity")
    p.add_argument("--kind")
    p.add_argument("--limit", type=int, default=80)
    p.add_argument("--verbose", action="store_true")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_issues)

    p = sub.add_parser("event")
    p.add_argument("session")
    p.add_argument("--id", type=int, required=True)
    p.add_argument("--limit", type=int, default=32)
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_event)

    p = sub.add_parser("shader")
    p.add_argument("session")
    p.add_argument("--ps")
    p.add_argument("--cs")
    p.add_argument("--limit", type=int, default=80)
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_shader)

    p = sub.add_parser("lineage")
    p.add_argument("session")
    p.add_argument("--event", type=int, required=True)
    p.add_argument("--slot", required=True)
    p.add_argument("--root")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_lineage)

    p = sub.add_parser("alias")
    p.add_argument("session")
    p.add_argument("--resource", required=True)
    p.add_argument("--limit", type=int, default=40)
    p.set_defaults(func=cmd_alias)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
