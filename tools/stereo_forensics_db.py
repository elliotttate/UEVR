#!/usr/bin/env python3
"""Persistent SQLite knowledge DB for UEVR Stereo Forensics.

This is the durable write path for investigations. Raw capture bundles are
ingested as facts; hypotheses/conclusions are stored as findings linked to
specific evidence IDs; successful experiments can be promoted to reusable rule
records.
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 5
DEFAULT_DB = Path(os.environ.get("UEVR_STEREO_FORENSICS_DB", r"C:\tmp\uevr_forensics\forensics.db"))


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def canonical_path(path: str | Path) -> str:
    return str(Path(path).resolve())


def parse_json_value(value: str | None, default: Any = None) -> Any:
    if value is None:
        return default
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        return value


def json_text(value: Any) -> str:
    return json.dumps(value if value is not None else {}, sort_keys=True, separators=(",", ":"))


def pretty_json(value: str | Any) -> str:
    if isinstance(value, str):
        try:
            value = json.loads(value)
        except json.JSONDecodeError:
            return value
    return json.dumps(value, indent=2, sort_keys=False)


def connect(db_path: str | Path) -> sqlite3.Connection:
    path = Path(db_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(path)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA foreign_keys = ON")
    con.execute("PRAGMA journal_mode = WAL")
    return con


SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS captures (
  id INTEGER PRIMARY KEY,
  session_dir TEXT NOT NULL UNIQUE,
  schema TEXT,
  game TEXT,
  ue_version TEXT,
  executable_hash TEXT,
  latest_frame INTEGER,
  event_count INTEGER DEFAULT 0,
  resource_count INTEGER DEFAULT 0,
  descriptor_count INTEGER DEFAULT 0,
  issue_count INTEGER DEFAULT 0,
  created_at TEXT NOT NULL,
  ingested_at TEXT NOT NULL,
  manifest_json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS events (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  frame INTEGER,
  event_index INTEGER NOT NULL,
  event_class TEXT,
  kind TEXT,
  eye_bucket INTEGER,
  raw_eye_bucket INTEGER,
  canonical_eye_bucket INTEGER,
  pipeline_state TEXT,
  root_signature TEXT,
  root_signature_hash TEXT,
  shader_key TEXT,
  vs_crc TEXT,
  ps_crc TEXT,
  gs_crc TEXT,
  cs_crc TEXT,
  rtv0_resource TEXT,
  json TEXT NOT NULL,
  UNIQUE(capture_id, event_index)
);

CREATE TABLE IF NOT EXISTS resources (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  resource_key TEXT NOT NULL,
  resource_hex TEXT,
  resource_generation INTEGER,
  resource_instance_uid TEXT,
  desc_key TEXT,
  dimension TEXT,
  width INTEGER,
  height INTEGER,
  depth_or_array_size INTEGER,
  format INTEGER,
  alias_group TEXT,
  classification TEXT,
  first_seen_frame INTEGER,
  last_seen_frame INTEGER,
  first_writer_frame INTEGER,
  last_writer_frame INTEGER,
  json TEXT NOT NULL,
  UNIQUE(capture_id, resource_key)
);

CREATE TABLE IF NOT EXISTS descriptors (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  cpu_key TEXT NOT NULL,
  cpu_hex TEXT,
  kind TEXT,
  resource_key TEXT,
  resource_hex TEXT,
  resource_desc_key TEXT,
  view_key TEXT,
  first_array_slice INTEGER,
  mip INTEGER,
  plane_slice INTEGER,
  json TEXT NOT NULL,
  UNIQUE(capture_id, cpu_key)
);

CREATE TABLE IF NOT EXISTS lineage_edges (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  consumer_event INTEGER,
  consumer_kind TEXT,
  consumer_eye_bucket INTEGER,
  root INTEGER,
  slot INTEGER,
  descriptor_type TEXT,
  resource_key TEXT,
  resource_hex TEXT,
  resource_generation INTEGER,
  resource_instance_uid TEXT,
  view_key TEXT,
  binding_type TEXT,
  shader_register INTEGER,
  shader_register_name TEXT,
  register_space INTEGER,
  producer_event INTEGER,
  producer_kind TEXT,
  producer_eye_bucket INTEGER,
  classification TEXT,
  json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS eye_diff_issues (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  severity TEXT,
  kind TEXT,
  left_event INTEGER,
  right_event INTEGER,
  root INTEGER,
  slot INTEGER,
  resource_hex TEXT,
  pair_confidence REAL,
  status TEXT NOT NULL DEFAULT 'open',
  json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS event_pairs (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  left_event INTEGER,
  right_event INTEGER,
  pair_score REAL,
  pair_confidence REAL,
  pair_reason_json TEXT NOT NULL DEFAULT '[]',
  key TEXT,
  status TEXT NOT NULL DEFAULT 'paired',
  json TEXT NOT NULL DEFAULT '{}',
  UNIQUE(capture_id, left_event, right_event, key)
);

CREATE TABLE IF NOT EXISTS lineage_paths (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  consumer_event INTEGER,
  root INTEGER,
  slot INTEGER,
  resource_hex TEXT,
  resource_generation INTEGER,
  resource_instance_uid TEXT,
  view_key TEXT,
  producer_event INTEGER,
  classification TEXT,
  path_json TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS suspects (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  severity TEXT,
  kind TEXT,
  score REAL,
  left_event INTEGER,
  right_event INTEGER,
  shader_crc TEXT,
  resource_hex TEXT,
  root INTEGER,
  slot INTEGER,
  reason TEXT NOT NULL,
  evidence_json TEXT NOT NULL DEFAULT '{}',
  status TEXT NOT NULL DEFAULT 'open',
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS shader_roles (
  id INTEGER PRIMARY KEY,
  shader_crc TEXT NOT NULL,
  stage TEXT NOT NULL,
  role TEXT NOT NULL,
  confidence REAL NOT NULL,
  reason TEXT,
  semantic_json TEXT NOT NULL DEFAULT '{}',
  updated_at TEXT NOT NULL,
  UNIQUE(shader_crc, stage, role)
);

CREATE TABLE IF NOT EXISTS resource_lifetimes (
  id INTEGER PRIMARY KEY,
  capture_id INTEGER NOT NULL REFERENCES captures(id) ON DELETE CASCADE,
  resource_hex TEXT,
  resource_generation INTEGER,
  resource_instance_uid TEXT,
  view_key TEXT,
  first_event INTEGER,
  last_event INTEGER,
  writer_count INTEGER NOT NULL DEFAULT 0,
  reader_count INTEGER NOT NULL DEFAULT 0,
  classification TEXT,
  alias_group TEXT,
  json TEXT NOT NULL DEFAULT '{}',
  UNIQUE(capture_id, resource_instance_uid, view_key)
);

CREATE TABLE IF NOT EXISTS shaders (
  id INTEGER PRIMARY KEY,
  shader_crc TEXT NOT NULL,
  stage TEXT NOT NULL,
  first_capture_id INTEGER REFERENCES captures(id) ON DELETE SET NULL,
  last_capture_id INTEGER REFERENCES captures(id) ON DELETE SET NULL,
  seen_count INTEGER NOT NULL DEFAULT 0,
  semantic_json TEXT,
  notes TEXT,
  UNIQUE(shader_crc, stage)
);

CREATE TABLE IF NOT EXISTS experiments (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  capture_id INTEGER REFERENCES captures(id) ON DELETE SET NULL,
  status TEXT NOT NULL DEFAULT 'observed',
  action TEXT,
  score REAL,
  right_roi_delta REAL,
  left_roi_delta REAL,
  likely_causal INTEGER,
  rule_json TEXT,
  result_json TEXT,
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS findings (
  id INTEGER PRIMARY KEY,
  finding_id TEXT NOT NULL UNIQUE,
  game TEXT,
  title TEXT NOT NULL,
  status TEXT NOT NULL,
  confidence REAL NOT NULL,
  summary TEXT NOT NULL,
  implication TEXT,
  next_action TEXT,
  tags TEXT NOT NULL DEFAULT '[]',
  supersedes TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS evidence (
  id INTEGER PRIMARY KEY,
  finding_id TEXT REFERENCES findings(finding_id) ON DELETE CASCADE,
  capture_id INTEGER REFERENCES captures(id) ON DELETE SET NULL,
  event_index INTEGER,
  shader_crc TEXT,
  resource_hex TEXT,
  descriptor_slot TEXT,
  evidence_type TEXT NOT NULL,
  path TEXT,
  summary TEXT NOT NULL,
  json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS fix_rules (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  game TEXT,
  status TEXT NOT NULL DEFAULT 'experimental',
  source_finding_id TEXT REFERENCES findings(finding_id) ON DELETE SET NULL,
  source_experiment_id INTEGER REFERENCES experiments(id) ON DELETE SET NULL,
  guards_json TEXT NOT NULL DEFAULT '{}',
  actions_json TEXT NOT NULL DEFAULT '{}',
  rule_json TEXT NOT NULL,
  validation_json TEXT NOT NULL DEFAULT '{}',
  risk TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_events_shader_ps ON events(ps_crc);
CREATE INDEX IF NOT EXISTS idx_events_shader_cs ON events(cs_crc);
CREATE INDEX IF NOT EXISTS idx_events_shader_key ON events(shader_key);
CREATE INDEX IF NOT EXISTS idx_events_root_hash ON events(root_signature_hash);
CREATE INDEX IF NOT EXISTS idx_events_capture_eye ON events(capture_id, eye_bucket);
CREATE INDEX IF NOT EXISTS idx_resources_desc ON resources(desc_key);
CREATE INDEX IF NOT EXISTS idx_descriptors_view ON descriptors(view_key);
CREATE INDEX IF NOT EXISTS idx_lineage_event_slot ON lineage_edges(capture_id, consumer_event, slot);
CREATE INDEX IF NOT EXISTS idx_issues_kind ON eye_diff_issues(kind, severity);
CREATE INDEX IF NOT EXISTS idx_pairs_capture ON event_pairs(capture_id, left_event, right_event);
CREATE INDEX IF NOT EXISTS idx_lineage_paths_consumer ON lineage_paths(capture_id, consumer_event);
CREATE INDEX IF NOT EXISTS idx_suspects_capture_score ON suspects(capture_id, status, score);
CREATE INDEX IF NOT EXISTS idx_shader_roles_crc ON shader_roles(shader_crc, stage);
CREATE INDEX IF NOT EXISTS idx_resource_lifetimes_resource ON resource_lifetimes(capture_id, resource_hex);
CREATE INDEX IF NOT EXISTS idx_findings_status ON findings(status, confidence);
CREATE INDEX IF NOT EXISTS idx_evidence_finding ON evidence(finding_id);
"""


def init_db(con: sqlite3.Connection) -> None:
    con.executescript(SCHEMA_SQL)
    ensure_column(con, "events", "raw_eye_bucket", "INTEGER")
    ensure_column(con, "events", "canonical_eye_bucket", "INTEGER")
    ensure_column(con, "events", "root_signature_hash", "TEXT")
    ensure_column(con, "events", "shader_key", "TEXT")
    ensure_column(con, "events", "vs_crc", "TEXT")
    ensure_column(con, "events", "gs_crc", "TEXT")
    ensure_column(con, "resources", "resource_generation", "INTEGER")
    ensure_column(con, "resources", "resource_instance_uid", "TEXT")
    ensure_column(con, "resources", "first_seen_frame", "INTEGER")
    ensure_column(con, "resources", "last_seen_frame", "INTEGER")
    ensure_column(con, "resources", "first_writer_frame", "INTEGER")
    ensure_column(con, "resources", "last_writer_frame", "INTEGER")
    ensure_column(con, "lineage_edges", "resource_generation", "INTEGER")
    ensure_column(con, "lineage_edges", "resource_instance_uid", "TEXT")
    ensure_column(con, "lineage_edges", "binding_type", "TEXT")
    ensure_column(con, "lineage_edges", "shader_register", "INTEGER")
    ensure_column(con, "lineage_edges", "shader_register_name", "TEXT")
    ensure_column(con, "lineage_edges", "register_space", "INTEGER")
    ensure_column(con, "lineage_paths", "resource_generation", "INTEGER")
    ensure_column(con, "lineage_paths", "resource_instance_uid", "TEXT")
    ensure_column(con, "resource_lifetimes", "resource_generation", "INTEGER")
    ensure_column(con, "resource_lifetimes", "resource_instance_uid", "TEXT")
    con.execute(
        "INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', ?)",
        (str(SCHEMA_VERSION),),
    )
    con.execute(
        "INSERT OR IGNORE INTO meta(key, value) VALUES('created_at', ?)",
        (utc_now(),),
    )
    con.commit()


def ensure_column(con: sqlite3.Connection, table: str, column: str, decl: str) -> None:
    existing = {
        str(row["name"])
        for row in con.execute(f"PRAGMA table_info({table})")
    }
    if column not in existing:
        con.execute(f"ALTER TABLE {table} ADD COLUMN {column} {decl}")


def load_json(path: Path, default: Any) -> Any:
    if not path.exists():
        return default
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def iter_jsonl(path: Path) -> Iterable[dict[str, Any]]:
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


def scalar_hex(value: Any) -> str | None:
    if value is None or value == "":
        return None
    if isinstance(value, str):
        return value
    if isinstance(value, int):
        return f"0x{value:X}"
    return str(value)


def upsert_shader(con: sqlite3.Connection, capture_id: int, crc: Any, stage: str) -> None:
    crc_hex = scalar_hex(crc)
    if not crc_hex or crc_hex in {"0x0", "0"}:
        return
    row = con.execute(
        "SELECT id, seen_count FROM shaders WHERE shader_crc=? AND stage=?",
        (crc_hex, stage),
    ).fetchone()
    if row:
        con.execute(
            "UPDATE shaders SET last_capture_id=?, seen_count=? WHERE id=?",
            (capture_id, int(row["seen_count"]) + 1, row["id"]),
        )
    else:
        con.execute(
            "INSERT INTO shaders(shader_crc, stage, first_capture_id, last_capture_id, seen_count) VALUES(?,?,?,?,1)",
            (crc_hex, stage, capture_id, capture_id),
        )


def ingest_session(con: sqlite3.Connection, session: Path, game: str | None, ue_version: str | None, executable_hash: str | None) -> int:
    init_db(con)
    session = session.resolve()
    manifest = load_json(session / "manifest.json", {})
    events = list(iter_jsonl(session / "events.jsonl"))
    resources = load_json(session / "resources.json", {}).get("resources", [])
    descriptors = load_json(session / "descriptors.json", {}).get("descriptors", [])
    lineage = load_json(session / "lineage.json", {})
    eye_diff = load_json(session / "eye_diff.json", {})
    issues = eye_diff.get("issues", [])
    paired_groups = eye_diff.get("paired_groups", [])
    experiments = load_json(session / "experiments.json", {}).get("experiments", [])
    now = utc_now()

    existing = con.execute("SELECT id FROM captures WHERE session_dir=?", (str(session),)).fetchone()
    refresh_existing = existing is not None
    if existing is None:
        cur = con.execute(
            """
            INSERT OR IGNORE INTO captures(session_dir, schema, game, ue_version, executable_hash, latest_frame,
              event_count, resource_count, descriptor_count, issue_count, created_at, ingested_at, manifest_json)
            VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                str(session),
                manifest.get("schema"),
                game,
                ue_version,
                executable_hash,
                manifest.get("latest_frame"),
                len(events),
                len(resources),
                len(descriptors),
                len(issues),
                now,
                now,
                json_text(manifest),
            ),
        )
        if cur.rowcount == 1:
            capture_id = int(cur.lastrowid)
        else:
            # Another ingest process won the same session_dir between our
            # SELECT and INSERT. Treat it as an update instead of failing.
            existing = con.execute("SELECT id FROM captures WHERE session_dir=?", (str(session),)).fetchone()
            if existing is None:
                raise RuntimeError(f"failed to create or find capture row for {session}")
            capture_id = int(existing["id"])
            refresh_existing = True
    else:
        capture_id = int(existing["id"])

    if refresh_existing:
        for table in (
            "events",
            "resources",
            "descriptors",
            "lineage_edges",
            "eye_diff_issues",
            "event_pairs",
            "lineage_paths",
            "suspects",
            "resource_lifetimes",
        ):
            con.execute(f"DELETE FROM {table} WHERE capture_id=?", (capture_id,))
        con.execute(
            """
            UPDATE captures SET schema=?, game=?, ue_version=?, executable_hash=?, latest_frame=?,
              event_count=?, resource_count=?, descriptor_count=?, issue_count=?, ingested_at=?, manifest_json=?
            WHERE id=?
            """,
            (
                manifest.get("schema"),
                game,
                ue_version,
                executable_hash,
                manifest.get("latest_frame"),
                len(events),
                len(resources),
                len(descriptors),
                len(issues),
                now,
                json_text(manifest),
                capture_id,
            ),
        )

    for event in events:
        con.execute(
            """
            INSERT OR REPLACE INTO events(capture_id, frame, event_index, event_class, kind, eye_bucket,
              raw_eye_bucket, canonical_eye_bucket, pipeline_state, root_signature, root_signature_hash,
              shader_key, vs_crc, ps_crc, gs_crc, cs_crc, rtv0_resource, json)
            VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                event.get("frame"),
                event.get("event_index"),
                event.get("event_class"),
                event.get("kind"),
                event.get("eye_bucket"),
                event.get("eye_bucket_raw", event.get("eye_bucket")),
                event.get("eye_bucket"),
                scalar_hex(event.get("pipeline_state_hex") or event.get("pipeline_state")),
                scalar_hex(event.get("root_signature_hex") or event.get("root_signature")),
                scalar_hex(event.get("root_signature_hash_hex") or event.get("root_signature_hash")),
                event.get("shader_key"),
                scalar_hex(event.get("vs_crc_hex") or event.get("vs_crc")),
                scalar_hex(event.get("ps_crc_hex") or event.get("ps_crc")),
                scalar_hex(event.get("gs_crc_hex") or event.get("gs_crc")),
                scalar_hex(event.get("cs_crc_hex") or event.get("cs_crc")),
                scalar_hex(event.get("rtv0_resource_hex") or event.get("rtv0_resource")),
                json_text(event),
            ),
        )
        upsert_shader(con, capture_id, event.get("vs_crc_hex") or event.get("vs_crc"), "vs")
        upsert_shader(con, capture_id, event.get("ps_crc_hex") or event.get("ps_crc"), "ps")
        upsert_shader(con, capture_id, event.get("gs_crc_hex") or event.get("gs_crc"), "gs")
        upsert_shader(con, capture_id, event.get("cs_crc_hex") or event.get("cs_crc"), "cs")

    for res in resources:
        desc = res.get("desc", {})
        con.execute(
            """
            INSERT OR REPLACE INTO resources(capture_id, resource_key, resource_hex, resource_generation,
              resource_instance_uid, desc_key, dimension, width, height, depth_or_array_size, format,
              alias_group, classification, first_seen_frame, last_seen_frame, first_writer_frame,
              last_writer_frame, json)
            VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                str(res.get("resource_instance_uid") or res.get("resource") or res.get("resource_hex")),
                res.get("resource_hex"),
                res.get("resource_generation"),
                res.get("resource_instance_uid"),
                res.get("desc_key"),
                desc.get("dimension"),
                desc.get("width"),
                desc.get("height"),
                desc.get("depth_or_array_size"),
                desc.get("format"),
                res.get("alias_group"),
                res.get("classification"),
                res.get("first_seen_frame"),
                res.get("last_seen_frame"),
                res.get("first_writer_frame"),
                res.get("last_writer_frame"),
                json_text(res),
            ),
        )

    for desc in descriptors:
        mip = desc.get("most_detailed_mip", desc.get("mip_slice"))
        con.execute(
            """
            INSERT OR REPLACE INTO descriptors(capture_id, cpu_key, cpu_hex, kind, resource_key, resource_hex,
              resource_desc_key, view_key, first_array_slice, mip, plane_slice, json)
            VALUES(?,?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                str(desc.get("cpu", desc.get("cpu_hex"))),
                desc.get("cpu_hex"),
                desc.get("kind"),
                str(desc.get("resource", "")),
                desc.get("resource_hex"),
                desc.get("resource_desc_key"),
                desc.get("view_key"),
                desc.get("first_array_slice"),
                mip,
                desc.get("plane_slice"),
                json_text(desc),
            ),
        )

    for edge in lineage.get("read_edges", []):
        producer = edge.get("producer", {})
        classification = edge.get("classification", {})
        producer_event = producer.get("event_index")
        classification_kind = classification.get("kind") if isinstance(classification, dict) else None
        con.execute(
            """
            INSERT INTO lineage_edges(capture_id, consumer_event, consumer_kind, consumer_eye_bucket,
              root, slot, descriptor_type, resource_key, resource_hex, resource_generation,
              resource_instance_uid, view_key, binding_type, shader_register, shader_register_name,
              register_space, producer_event,
              producer_kind, producer_eye_bucket, classification, json)
            VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                edge.get("consumer_event"),
                edge.get("consumer_kind"),
                edge.get("consumer_eye_bucket"),
                edge.get("root"),
                edge.get("slot"),
                edge.get("descriptor_type"),
                str(edge.get("resource_instance_uid") or edge.get("resource") or ""),
                edge.get("resource_hex"),
                edge.get("resource_generation"),
                edge.get("resource_instance_uid"),
                edge.get("view_key"),
                edge.get("binding_type"),
                edge.get("shader_register"),
                edge.get("shader_register_name"),
                edge.get("register_space"),
                producer_event,
                producer.get("kind"),
                producer.get("eye_bucket"),
                classification_kind,
                json_text(edge),
            ),
        )
        con.execute(
            """
            INSERT INTO lineage_paths(capture_id, consumer_event, root, slot, resource_hex,
              resource_generation, resource_instance_uid, view_key, producer_event, classification, path_json)
            VALUES(?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                edge.get("consumer_event"),
                edge.get("root"),
                edge.get("slot"),
                edge.get("resource_hex"),
                edge.get("resource_generation"),
                edge.get("resource_instance_uid"),
                edge.get("view_key"),
                producer_event,
                classification_kind,
                json_text(edge),
            ),
        )

    for pair in paired_groups:
        con.execute(
            """
            INSERT OR REPLACE INTO event_pairs(capture_id, left_event, right_event, pair_score,
              pair_confidence, pair_reason_json, key, json)
            VALUES(?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                pair.get("left_event"),
                pair.get("right_event"),
                pair.get("pair_score"),
                pair.get("pair_confidence"),
                json_text(pair.get("pair_reason", [])),
                pair.get("key"),
                json_text(pair),
            ),
        )

    for issue in issues:
        sample_left = issue.get("sample_left_event") if isinstance(issue.get("sample_left_event"), dict) else {}
        sample_right = issue.get("sample_right_event") if isinstance(issue.get("sample_right_event"), dict) else {}
        con.execute(
            """
            INSERT INTO eye_diff_issues(capture_id, severity, kind, left_event, right_event, root,
              slot, resource_hex, pair_confidence, json)
            VALUES(?,?,?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                issue.get("severity"),
                issue.get("kind"),
                issue.get("left_event") or sample_left.get("event_index"),
                issue.get("right_event") or sample_right.get("event_index"),
                issue.get("root"),
                issue.get("slot"),
                issue.get("resource") or issue.get("left_resource") or issue.get("right_resource"),
                issue.get("pair_confidence"),
                json_text(issue),
            ),
        )

    lifetimes: dict[tuple[str, str], dict[str, Any]] = {}
    for item in lineage.get("writer_history", []):
        res = item.get("resource_hex")
        view = item.get("view_key") or ""
        if not res:
            continue
        instance_uid = item.get("resource_instance_uid")
        key = (instance_uid or res, view)
        rec = lifetimes.setdefault(
            key,
            {
                "resource_hex": res,
                "resource_generation": item.get("resource_generation"),
                "resource_instance_uid": instance_uid,
                "view_key": view,
                "first_event": None,
                "last_event": None,
                "writer_count": 0,
                "reader_count": 0,
                "classification": "frame_written",
                "alias_group": item.get("alias_group"),
                "writes": [],
                "reads": [],
            },
        )
        ev = item.get("producer_event")
        if ev is not None:
            rec["first_event"] = ev if rec["first_event"] is None else min(rec["first_event"], ev)
            rec["last_event"] = ev if rec["last_event"] is None else max(rec["last_event"], ev)
        rec["writer_count"] += 1
        if len(rec["writes"]) < 64:
            rec["writes"].append(item)

    for edge in lineage.get("read_edges", []):
        res = edge.get("resource_hex")
        view = edge.get("view_key") or ""
        if not res:
            continue
        instance_uid = edge.get("resource_instance_uid")
        key = (instance_uid or res, view)
        classification = edge.get("classification", {})
        rec = lifetimes.setdefault(
            key,
            {
                "resource_hex": res,
                "resource_generation": edge.get("resource_generation"),
                "resource_instance_uid": instance_uid,
                "view_key": view,
                "first_event": None,
                "last_event": None,
                "writer_count": 0,
                "reader_count": 0,
                "classification": classification.get("kind") if isinstance(classification, dict) else None,
                "alias_group": classification.get("alias_group") if isinstance(classification, dict) else None,
                "writes": [],
                "reads": [],
            },
        )
        ev = edge.get("consumer_event")
        if ev is not None:
            rec["first_event"] = ev if rec["first_event"] is None else min(rec["first_event"], ev)
            rec["last_event"] = ev if rec["last_event"] is None else max(rec["last_event"], ev)
        rec["reader_count"] += 1
        if len(rec["reads"]) < 64:
            rec["reads"].append(edge)

    for rec in lifetimes.values():
        con.execute(
            """
            INSERT OR REPLACE INTO resource_lifetimes(capture_id, resource_hex, resource_generation,
              resource_instance_uid, view_key, first_event, last_event, writer_count, reader_count,
              classification, alias_group, json)
            VALUES(?,?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                rec.get("resource_hex"),
                rec.get("resource_generation"),
                rec.get("resource_instance_uid"),
                rec.get("view_key"),
                rec.get("first_event"),
                rec.get("last_event"),
                rec.get("writer_count", 0),
                rec.get("reader_count", 0),
                rec.get("classification"),
                rec.get("alias_group"),
                json_text(rec),
            ),
        )

    for exp in experiments:
        con.execute(
            """
            INSERT INTO experiments(name, capture_id, status, action, rule_json, result_json, created_at)
            VALUES(?,?,?,?,?,?,?)
            """,
            (
                exp.get("name", "unnamed"),
                capture_id,
                "observed",
                exp.get("action"),
                json_text(exp),
                json_text(exp),
                now,
            ),
        )

    con.commit()
    return capture_id


ISSUE_BASE_SCORE = {
    "eye_event_count_mismatch": 0.95,
    "producer_lineage_differs": 0.90,
    "descriptor_resource_differs": 0.82,
    "graphics_cbv_hash_differs": 0.74,
    "pso_or_shader_differs": 0.72,
    "descriptor_missing_on_right": 0.70,
    "same_desc_different_resource": 0.62,
    "same_resource_different_slice": 0.58,
    "same_resource_different_view": 0.45,
}

ISSUE_NEXT_ACTION = {
    "eye_event_count_mismatch": "Generate skip/color probes for the unmatched draw group, then test duplicate-draw or duplicate-dispatch only if the ROI proves causal.",
    "producer_lineage_differs": "Query the exact producer->consumer path and test descriptor/CB swap against the right-eye consumer.",
    "descriptor_resource_differs": "Emit a descriptor-swap candidate and score the right-eye ROI.",
    "graphics_cbv_hash_differs": "Emit a left-CB-into-right probe for the divergent root slot and inspect shader semantics for the byte ranges used.",
    "pso_or_shader_differs": "Run color override first; if visible, cluster PSO permutations and try PS bytecode replacement only on the right eye.",
    "descriptor_missing_on_right": "Inspect the paired right event root table and generate a neutral/forced descriptor probe.",
    "same_desc_different_resource": "Check resource lifetimes and alias group before treating the resource difference as a stereo bug.",
    "same_resource_different_slice": "Treat this as multiview/array-slice evidence; only force slice if the shader should sample the opposite eye.",
    "same_resource_different_view": "Compare the descriptor view metadata and resource lifetime before mutating bindings.",
}


def event_for_index(con: sqlite3.Connection, capture_id: int, event_index: int | None) -> dict[str, Any]:
    if event_index is None:
        return {}
    row = con.execute(
        "SELECT * FROM events WHERE capture_id=? AND event_index=?",
        (capture_id, event_index),
    ).fetchone()
    if not row:
        return {}
    doc = json.loads(row["json"] or "{}")
    doc["_row"] = dict(row)
    return doc


def best_event_shader(event: dict[str, Any]) -> str | None:
    for key in ("ps_crc_hex", "cs_crc_hex", "gs_crc_hex", "ps_crc", "cs_crc", "gs_crc"):
        value = event.get(key)
        crc = scalar_hex(value)
        if crc and crc not in {"0x0", "0"}:
            return crc
    return None


def issue_score(severity: str | None, kind: str | None, pair_confidence: float | None) -> float:
    score = ISSUE_BASE_SCORE.get(kind or "", {"high": 0.78, "medium": 0.58, "info": 0.32}.get(severity or "", 0.35))
    if severity == "high":
        score = max(score, 0.78)
    elif severity == "medium":
        score = max(score, 0.52)
    if pair_confidence is not None:
        score *= 0.70 + min(1.0, max(0.0, float(pair_confidence))) * 0.30
    return round(min(0.99, score), 3)


def issue_reason(kind: str | None, issue: dict[str, Any]) -> str:
    if kind == "producer_lineage_differs":
        return "paired eyes read a matching descriptor slot, but the recorded producer event or producer eye differs"
    if kind == "graphics_cbv_hash_differs":
        return f"paired eyes bind different CB contents at graphics root {issue.get('root')}"
    if kind == "same_resource_different_slice":
        return f"paired eyes use the same resource but different array slices at root {issue.get('root')} slot {issue.get('slot')}"
    if kind == "descriptor_resource_differs":
        return f"paired eyes bind different resource descriptions at root {issue.get('root')} slot {issue.get('slot')}"
    if kind == "same_desc_different_resource":
        return f"paired eyes bind different physical resources with the same descriptor shape at root {issue.get('root')} slot {issue.get('slot')}"
    if kind == "pso_or_shader_differs":
        return "paired eyes use a different PSO or shader CRC"
    if kind == "eye_event_count_mismatch":
        return "left/right event counts differ for a fuzzy-matched work group"
    return kind or "eye-diff issue"


def upsert_auto_finding(
    con: sqlite3.Connection,
    capture_id: int,
    game: str | None,
    suspect_id: int,
    suspect: dict[str, Any],
) -> None:
    finding_id = f"AUTO-CAP{capture_id:04d}-SUS{suspect_id:04d}"
    now = utc_now()
    tags = ["auto", "stereo-forensics", str(suspect.get("kind") or "unknown")]
    con.execute(
        """
        INSERT INTO findings(finding_id, game, title, status, confidence, summary, implication,
          next_action, tags, created_at, updated_at)
        VALUES(?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(finding_id) DO UPDATE SET
          game=excluded.game,
          title=excluded.title,
          confidence=excluded.confidence,
          summary=excluded.summary,
          implication=excluded.implication,
          next_action=excluded.next_action,
          tags=excluded.tags,
          updated_at=excluded.updated_at
        """,
        (
            finding_id,
            game,
            f"{suspect.get('kind')} in capture {capture_id}",
            "open",
            suspect.get("score", 0.5),
            suspect.get("reason", ""),
            "Automatically generated from normalized eye-diff evidence; verify with a scored intervention before promoting to a fix rule.",
            ISSUE_NEXT_ACTION.get(str(suspect.get("kind")), "Inspect event and lineage context, then generate a targeted experiment."),
            json_text(tags),
            now,
            now,
        ),
    )
    con.execute(
        """
        INSERT INTO evidence(finding_id, capture_id, event_index, shader_crc, resource_hex, descriptor_slot,
          evidence_type, summary, json, created_at)
        VALUES(?,?,?,?,?,?,?,?,?,?)
        """,
        (
            finding_id,
            capture_id,
            suspect.get("right_event") or suspect.get("left_event"),
            suspect.get("shader_crc"),
            suspect.get("resource_hex"),
            f"{suspect.get('root')}:{suspect.get('slot')}" if suspect.get("root") is not None else None,
            "auto_suspect",
            suspect.get("reason", ""),
            json_text(suspect),
            now,
        ),
    )


def analyze_capture(con: sqlite3.Connection, capture_id: int, auto_findings: bool = False) -> int:
    init_db(con)
    cap = con.execute("SELECT * FROM captures WHERE id=?", (capture_id,)).fetchone()
    if not cap:
        raise ValueError(f"capture not found: {capture_id}")
    con.execute("DELETE FROM suspects WHERE capture_id=?", (capture_id,))

    rows = con.execute(
        "SELECT * FROM eye_diff_issues WHERE capture_id=? ORDER BY id",
        (capture_id,),
    ).fetchall()
    created = 0
    for row in rows:
        issue = json.loads(row["json"] or "{}")
        left_event = row["left_event"]
        right_event = row["right_event"]
        left = event_for_index(con, capture_id, left_event)
        right = event_for_index(con, capture_id, right_event)
        shader_crc = best_event_shader(right) or best_event_shader(left)
        score = issue_score(row["severity"], row["kind"], row["pair_confidence"])
        reason = issue_reason(row["kind"], issue)
        evidence = {
            "issue_id": row["id"],
            "issue": issue,
            "left_event": left,
            "right_event": right,
            "next_action": ISSUE_NEXT_ACTION.get(row["kind"], "Inspect event and lineage context, then generate a targeted experiment."),
        }
        cur = con.execute(
            """
            INSERT INTO suspects(capture_id, severity, kind, score, left_event, right_event, shader_crc,
              resource_hex, root, slot, reason, evidence_json, created_at)
            VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                capture_id,
                row["severity"],
                row["kind"],
                score,
                left_event,
                right_event,
                shader_crc,
                row["resource_hex"],
                row["root"],
                row["slot"],
                reason,
                json_text(evidence),
                utc_now(),
            ),
        )
        created += 1
        if auto_findings and score >= 0.65:
            suspect = {
                "id": int(cur.lastrowid),
                "kind": row["kind"],
                "score": score,
                "left_event": left_event,
                "right_event": right_event,
                "shader_crc": shader_crc,
                "resource_hex": row["resource_hex"],
                "root": row["root"],
                "slot": row["slot"],
                "reason": reason,
                "evidence": evidence,
            }
            upsert_auto_finding(con, capture_id, cap["game"], int(cur.lastrowid), suspect)
    con.commit()
    return created


def rank_suspects(
    con: sqlite3.Connection,
    game: str | None = None,
    capture_id: int | None = None,
    limit: int = 30,
) -> list[sqlite3.Row]:
    init_db(con)
    sql = """
        SELECT suspects.*, captures.game, captures.session_dir
        FROM suspects JOIN captures ON captures.id=suspects.capture_id
        WHERE suspects.status='open'
    """
    params: list[Any] = []
    if game:
        sql += " AND captures.game=?"
        params.append(game)
    if capture_id is not None:
        sql += " AND suspects.capture_id=?"
        params.append(capture_id)
    sql += " ORDER BY suspects.score DESC, suspects.id LIMIT ?"
    params.append(limit)
    return con.execute(sql, params).fetchall()


def infer_shader_stage(path: str, fallback: str | None = None) -> str:
    name = Path(path).name.lower()
    for stage in ("ps", "vs", "cs", "gs", "hs", "ds", "ms", "as"):
        if name.startswith(stage + "_") or name.startswith(stage + "-") or f".{stage}." in name:
            return stage
    return fallback or "unknown"


def classify_shader_roles(shader: dict[str, Any], stage: str) -> list[tuple[str, float, str]]:
    sem = shader.get("semantics", {})
    roles: list[tuple[str, float, str]] = []
    semantic_source = "DXIL" if sem.get("dxil") else ("DXBC" if sem.get("dxbc") else "container")
    if not sem.get("dxil") and not sem.get("dxbc"):
        roles.append(("container_only", 0.25, "No shader semantic extraction was available"))
        return roles
    if sem.get("store_count", 0) > 0:
        roles.append(("output_writer", 0.72 if semantic_source == "DXBC" else 0.75, f"{semantic_source} indicates output/UAV/depth writes"))
    if stage == "ps" and (sem.get("sample_count", 0) > 0 or sem.get("texture_load_count", 0) > 0):
        roles.append(("visible_texture_consumer", 0.70 if semantic_source == "DXBC" else 0.72, f"{semantic_source} indicates pixel shader texture samples/loads"))
    elif sem.get("sample_count", 0) > 0 or sem.get("texture_load_count", 0) > 0:
        roles.append(("texture_consumer", 0.60 if semantic_source == "DXBC" else 0.62, f"{semantic_source} indicates texture samples/loads"))
    if sem.get("cbuffer_load_count", 0) > 0 or sem.get("constant_buffer_count", 0) > 0:
        conf = 0.70 if sem.get("branch_count", 0) > 0 else 0.55
        reason = f"{semantic_source} indicates constant-buffer use"
        if conf >= 0.70:
            reason += "; branch count suggests possible gating"
        roles.append(("cb_driven", conf, reason))
    if sem.get("uses_discard_or_clip"):
        roles.append(("masked_or_discarding", 0.70, f"{semantic_source} contains discard/clip/kill indicators"))
    if sem.get("branch_count", 0) > 0 and (sem.get("sample_count", 0) > 0 or sem.get("texture_load_count", 0) > 0):
        roles.append(("branch_gated_texture_path", 0.66 if semantic_source == "DXBC" else 0.68, f"texture operations and branches coexist in {semantic_source}"))
    if sem.get("resource_binding_count", 0) > 0:
        roles.append(("has_reflection_bindings", 0.50, f"{semantic_source} reflection lists bound resources"))
    return roles or [(f"unknown_{semantic_source.lower()}", 0.30, f"{semantic_source} parsed but no high-signal stereo role was inferred")]


def import_shader_semantics(con: sqlite3.Connection, path: Path, stage: str | None = None) -> int:
    init_db(con)
    doc = load_json(path, {})
    shaders = doc.get("shaders") if isinstance(doc, dict) else None
    if shaders is None:
        shaders = [doc]
    count = 0
    now = utc_now()
    for shader in shaders:
        if not isinstance(shader, dict):
            continue
        crc = scalar_hex(shader.get("crc32"))
        if not crc:
            continue
        shader_stage = infer_shader_stage(shader.get("path", ""), stage)
        con.execute(
            """
            INSERT INTO shaders(shader_crc, stage, seen_count, semantic_json)
            VALUES(?,?,0,?)
            ON CONFLICT(shader_crc, stage) DO UPDATE SET
              semantic_json=excluded.semantic_json
            """,
            (crc, shader_stage, json_text(shader)),
        )
        for role, confidence, reason in classify_shader_roles(shader, shader_stage):
            con.execute(
                """
                INSERT INTO shader_roles(shader_crc, stage, role, confidence, reason, semantic_json, updated_at)
                VALUES(?,?,?,?,?,?,?)
                ON CONFLICT(shader_crc, stage, role) DO UPDATE SET
                  confidence=excluded.confidence,
                  reason=excluded.reason,
                  semantic_json=excluded.semantic_json,
                  updated_at=excluded.updated_at
                """,
                (crc, shader_stage, role, confidence, reason, json_text(shader), now),
            )
        count += 1
    con.commit()
    return count


def next_finding_id(con: sqlite3.Connection, game: str | None) -> str:
    prefix = (game or "UEVR").upper().replace(" ", "_")
    rows = con.execute("SELECT finding_id FROM findings WHERE finding_id LIKE ?", (f"{prefix}-FIND-%",)).fetchall()
    max_num = 0
    for row in rows:
        tail = str(row["finding_id"]).rsplit("-", 1)[-1]
        if tail.isdigit():
            max_num = max(max_num, int(tail))
    return f"{prefix}-FIND-{max_num + 1:04d}"


def add_finding(con: sqlite3.Connection, args: argparse.Namespace) -> str:
    init_db(con)
    now = utc_now()
    finding_id = args.finding_id or next_finding_id(con, args.game)
    tags = [t.strip() for t in (args.tags or "").split(",") if t.strip()]
    con.execute(
        """
        INSERT INTO findings(finding_id, game, title, status, confidence, summary, implication,
          next_action, tags, supersedes, created_at, updated_at)
        VALUES(?,?,?,?,?,?,?,?,?,?,?,?)
        """,
        (
            finding_id,
            args.game,
            args.title,
            args.status,
            args.confidence,
            args.summary,
            args.implication,
            args.next_action,
            json_text(tags),
            args.supersedes,
            now,
            now,
        ),
    )
    con.commit()
    return finding_id


def add_evidence(con: sqlite3.Connection, args: argparse.Namespace) -> int:
    init_db(con)
    capture_id = args.capture_id
    if capture_id is None and args.session:
        row = con.execute("SELECT id FROM captures WHERE session_dir=?", (canonical_path(args.session),)).fetchone()
        if row:
            capture_id = int(row["id"])
    cur = con.execute(
        """
        INSERT INTO evidence(finding_id, capture_id, event_index, shader_crc, resource_hex, descriptor_slot,
          evidence_type, path, summary, json, created_at)
        VALUES(?,?,?,?,?,?,?,?,?,?,?)
        """,
        (
            args.finding_id,
            capture_id,
            args.event,
            args.shader_crc,
            args.resource_hex,
            args.slot,
            args.type,
            args.path,
            args.summary,
            json_text(parse_json_value(args.json, {})),
            utc_now(),
        ),
    )
    con.commit()
    return int(cur.lastrowid)


def add_experiment(con: sqlite3.Connection, args: argparse.Namespace) -> int:
    init_db(con)
    result = load_json(Path(args.result), {}) if args.result else parse_json_value(args.result_json, {})
    rule = load_json(Path(args.rule), {}) if args.rule else parse_json_value(args.rule_json, {})
    capture_id = args.capture_id
    if capture_id is None and args.session:
        row = con.execute("SELECT id FROM captures WHERE session_dir=?", (canonical_path(args.session),)).fetchone()
        if row:
            capture_id = int(row["id"])
    likely_causal = int(args.likely_causal if args.likely_causal is not None else bool(result.get("likely_causal", False)))
    cur = con.execute(
        """
        INSERT INTO experiments(name, capture_id, status, action, score, right_roi_delta, left_roi_delta,
          likely_causal, rule_json, result_json, created_at)
        VALUES(?,?,?,?,?,?,?,?,?,?,?)
        """,
        (
            args.name or result.get("experiment", "unnamed"),
            capture_id,
            args.status,
            args.action or result.get("action"),
            args.score if args.score is not None else result.get("score"),
            args.right_roi_delta if args.right_roi_delta is not None else result.get("right", {}).get("normalized_delta"),
            args.left_roi_delta if args.left_roi_delta is not None else result.get("left", {}).get("normalized_delta"),
            likely_causal,
            json_text(rule),
            json_text(result),
            utc_now(),
        ),
    )
    experiment_id = int(cur.lastrowid)
    if args.promote_fix_rule and likely_causal and rule:
        first_rule = {}
        if isinstance(rule, dict) and isinstance(rule.get("rules"), list) and rule["rules"]:
            first_rule = rule["rules"][0]
        elif isinstance(rule, dict):
            first_rule = rule
        if first_rule:
            rule_name = args.fix_rule_name or first_rule.get("name") or f"experiment_{experiment_id}_winner"
            guards = {
                "match": first_rule.get("match", {}),
                "guard": first_rule.get("guard", {}),
            }
            actions = first_rule.get("action", {})
            now = utc_now()
            con.execute(
                """
                INSERT INTO fix_rules(name, game, status, source_experiment_id,
                  guards_json, actions_json, rule_json, validation_json, risk, created_at, updated_at)
                VALUES(?,?,?,?,?,?,?,?,?,?,?)
                ON CONFLICT(name) DO UPDATE SET
                  game=excluded.game,
                  status=excluded.status,
                  source_experiment_id=excluded.source_experiment_id,
                  guards_json=excluded.guards_json,
                  actions_json=excluded.actions_json,
                  rule_json=excluded.rule_json,
                  validation_json=excluded.validation_json,
                  risk=excluded.risk,
                  updated_at=excluded.updated_at
                """,
                (
                    rule_name,
                    args.game,
                    args.fix_rule_status,
                    experiment_id,
                    json_text(guards),
                    json_text(actions),
                    json_text(rule),
                    json_text(result),
                    args.risk,
                    now,
                    now,
                ),
            )
    con.commit()
    return experiment_id


def add_fix_rule(con: sqlite3.Connection, args: argparse.Namespace) -> str:
    init_db(con)
    now = utc_now()
    rule = load_json(Path(args.rule), {}) if args.rule else parse_json_value(args.rule_json, {})
    guards = parse_json_value(args.guards, {})
    actions = parse_json_value(args.actions, {})
    con.execute(
        """
        INSERT INTO fix_rules(name, game, status, source_finding_id, source_experiment_id,
          guards_json, actions_json, rule_json, validation_json, risk, created_at, updated_at)
        VALUES(?,?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(name) DO UPDATE SET
          game=excluded.game,
          status=excluded.status,
          source_finding_id=excluded.source_finding_id,
          source_experiment_id=excluded.source_experiment_id,
          guards_json=excluded.guards_json,
          actions_json=excluded.actions_json,
          rule_json=excluded.rule_json,
          validation_json=excluded.validation_json,
          risk=excluded.risk,
          updated_at=excluded.updated_at
        """,
        (
            args.name,
            args.game,
            args.status,
            args.finding_id,
            args.experiment_id,
            json_text(guards),
            json_text(actions),
            json_text(rule),
            json_text(parse_json_value(args.validation, {})),
            args.risk,
            now,
            now,
        ),
    )
    con.commit()
    return args.name


def cmd_init(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        init_db(con)
    print(f"initialized {args.db}")
    return 0


def cmd_ingest(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        capture_id = ingest_session(con, Path(args.session), args.game, args.ue_version, args.executable_hash)
    print(f"ingested capture_id={capture_id} session={Path(args.session).resolve()}")
    return 0


def cmd_analyze_capture(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        count = analyze_capture(con, args.capture_id, args.auto_findings)
    print(f"analyzed capture_id={args.capture_id} suspects={count}")
    return 0


def cmd_analyze_session(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        capture_id = ingest_session(con, Path(args.session), args.game, args.ue_version, args.executable_hash)
        count = analyze_capture(con, capture_id, args.auto_findings)
    print(f"analyzed capture_id={capture_id} suspects={count} session={Path(args.session).resolve()}")
    return 0


def cmd_add_finding(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        fid = add_finding(con, args)
    print(fid)
    return 0


def cmd_add_evidence(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        eid = add_evidence(con, args)
    print(f"evidence_id={eid}")
    return 0


def cmd_add_experiment(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        eid = add_experiment(con, args)
    print(f"experiment_id={eid}")
    return 0


def cmd_add_fix_rule(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        name = add_fix_rule(con, args)
    print(name)
    return 0


def cmd_list_findings(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        init_db(con)
        sql = "SELECT * FROM findings WHERE 1=1"
        params: list[Any] = []
        if args.status:
            sql += " AND status=?"
            params.append(args.status)
        if args.game:
            sql += " AND game=?"
            params.append(args.game)
        sql += " ORDER BY confidence DESC, updated_at DESC LIMIT ?"
        params.append(args.limit)
        rows = con.execute(sql, params).fetchall()
    for row in rows:
        print(f"{row['finding_id']} {row['status']} conf={row['confidence']:.2f} {row['title']}")
    return 0


def cmd_show_finding(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        row = con.execute("SELECT * FROM findings WHERE finding_id=?", (args.finding_id,)).fetchone()
        if not row:
            print(f"finding not found: {args.finding_id}", file=sys.stderr)
            return 1
        evidence = con.execute("SELECT * FROM evidence WHERE finding_id=? ORDER BY id", (args.finding_id,)).fetchall()
    doc = dict(row)
    doc["tags"] = json.loads(doc.get("tags") or "[]")
    doc["evidence"] = [dict(e) | {"json": json.loads(e["json"] or "{}")} for e in evidence]
    print(pretty_json(doc))
    return 0


def cmd_show_shader(args: argparse.Namespace) -> int:
    crc = scalar_hex(args.crc)
    with connect(args.db) as con:
        init_db(con)
        shaders = con.execute("SELECT * FROM shaders WHERE shader_crc=? ORDER BY stage", (crc,)).fetchall()
        roles = con.execute("SELECT * FROM shader_roles WHERE shader_crc=? ORDER BY confidence DESC", (crc,)).fetchall()
        suspects = con.execute(
            """
            SELECT * FROM suspects
            WHERE shader_crc=?
            ORDER BY score DESC LIMIT ?
            """,
            (crc, args.limit),
        ).fetchall()
        events = con.execute(
            """
            SELECT captures.id AS capture_id, captures.session_dir, events.event_index, events.kind,
                   events.eye_bucket, events.ps_crc, events.cs_crc, events.rtv0_resource
            FROM events JOIN captures ON captures.id=events.capture_id
            WHERE events.ps_crc=? OR events.cs_crc=?
            ORDER BY captures.id DESC, events.event_index LIMIT ?
            """,
            (crc, crc, args.limit),
        ).fetchall()
    print(f"shader {crc}")
    for shader in shaders:
        print(f"  stage={shader['stage']} seen={shader['seen_count']} first_capture={shader['first_capture_id']} last_capture={shader['last_capture_id']}")
        if shader["semantic_json"]:
            semantic = json.loads(shader["semantic_json"])
            sem = semantic.get("semantics", {})
            print(f"    semantics: container={semantic.get('container_kind')} samples={sem.get('sample_count')} cb_loads={sem.get('cbuffer_load_count')} stores={sem.get('store_count')} branches={sem.get('branch_count')}")
    for role in roles:
        print(f"  role={role['role']} stage={role['stage']} confidence={role['confidence']:.2f} {role['reason'] or ''}")
    for suspect in suspects:
        print(f"  suspect={suspect['id']} cap={suspect['capture_id']} score={suspect['score']:.2f} kind={suspect['kind']} event={suspect['right_event'] or suspect['left_event']}")
    for event in events:
        print(f"  cap={event['capture_id']} event={event['event_index']} kind={event['kind']} eye={event['eye_bucket']} session={event['session_dir']}")
    return 0


def cmd_show_resource(args: argparse.Namespace) -> int:
    resource = scalar_hex(args.resource)
    with connect(args.db) as con:
        init_db(con)
        rows = con.execute(
            """
            SELECT captures.id AS capture_id, captures.session_dir, resources.*
            FROM resources JOIN captures ON captures.id=resources.capture_id
            WHERE resources.resource_hex=? OR resources.resource_instance_uid=? OR resources.desc_key=? OR resources.alias_group=?
            ORDER BY captures.id DESC LIMIT ?
            """,
            (resource, args.resource, args.resource, args.resource, args.limit),
        ).fetchall()
        lifetimes = con.execute(
            """
            SELECT * FROM resource_lifetimes
            WHERE resource_hex=? OR resource_instance_uid=? OR view_key=? OR alias_group=?
            ORDER BY capture_id DESC, reader_count + writer_count DESC LIMIT ?
            """,
            (resource, args.resource, args.resource, args.resource, args.limit),
        ).fetchall()
    for row in rows:
        print(
            f"cap={row['capture_id']} res={row['resource_hex']} gen={row['resource_generation']} "
            f"uid={row['resource_instance_uid']} desc={row['desc_key']} class={row['classification']} "
            f"alias={row['alias_group']} session={row['session_dir']}"
        )
    for life in lifetimes:
        print(
            f"  lifetime cap={life['capture_id']} res={life['resource_hex']} gen={life['resource_generation']} "
            f"uid={life['resource_instance_uid']} view={life['view_key']} writers={life['writer_count']} "
            f"readers={life['reader_count']} events={life['first_event']}..{life['last_event']} class={life['classification']}"
        )
    return 0


def cmd_rank_suspects(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        rows = rank_suspects(con, args.game, args.capture_id, args.limit)
    for row in rows:
        print(
            f"suspect:{row['id']} cap={row['capture_id']} score={row['score']:.2f} "
            f"{row['severity']} {row['kind']} L={row['left_event']} R={row['right_event']} "
            f"shader={row['shader_crc']} res={row['resource_hex']} root={row['root']} slot={row['slot']}"
        )
        print(f"  {row['reason']}")
    return 0


def cmd_explain_event(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        init_db(con)
        event = con.execute(
            "SELECT * FROM events WHERE capture_id=? AND event_index=?",
            (args.capture_id, args.event),
        ).fetchone()
        if not event:
            print(f"event not found: capture={args.capture_id} event={args.event}", file=sys.stderr)
            return 1
        lineage = con.execute(
            "SELECT * FROM lineage_edges WHERE capture_id=? AND consumer_event=? ORDER BY root, slot",
            (args.capture_id, args.event),
        ).fetchall()
        shader_roles = []
        for crc, stage in ((event["ps_crc"], "ps"), (event["cs_crc"], "cs"), (event["vs_crc"], "vs")):
            if not crc:
                continue
            shader_roles.extend(
                con.execute(
                    """
                    SELECT shader_crc, stage, role, confidence, reason
                    FROM shader_roles
                    WHERE shader_crc=? AND stage=?
                    ORDER BY confidence DESC
                    """,
                    (crc, stage),
                ).fetchall()
            )
        writes = []
        doc = json.loads(event["json"] or "{}")
        for write in doc.get("writes", []):
            writes.append(write)
        issues = con.execute(
            """
            SELECT * FROM eye_diff_issues
            WHERE capture_id=? AND (left_event=? OR right_event=?)
            ORDER BY severity, kind
            """,
            (args.capture_id, args.event, args.event),
        ).fetchall()
    out = {
        "event": dict(event) | {"json": doc},
        "writes": writes,
        "reads": [dict(row) | {"json": json.loads(row["json"] or "{}")} for row in lineage],
        "shader_roles": [dict(row) for row in shader_roles],
        "issues": [dict(row) | {"json": json.loads(row["json"] or "{}")} for row in issues],
    }
    print(pretty_json(out))
    return 0


def cmd_import_shader_semantics(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        count = import_shader_semantics(con, Path(args.path), args.stage)
    print(f"imported shader_semantics={count}")
    return 0


def cmd_next_actions(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        init_db(con)
        rows = con.execute(
            """
            SELECT finding_id, status, confidence, title, next_action
            FROM findings
            WHERE next_action IS NOT NULL AND next_action != ''
              AND status IN ('open','confirmed')
              AND (? IS NULL OR game=?)
            ORDER BY status='confirmed' DESC, confidence DESC, updated_at DESC
            LIMIT ?
            """,
            (args.game, args.game, args.limit),
        ).fetchall()
    for row in rows:
        print(f"{row['finding_id']} [{row['status']}] conf={row['confidence']:.2f}: {row['next_action']} ({row['title']})")
    return 0


def cmd_report(args: argparse.Namespace) -> int:
    with connect(args.db) as con:
        init_db(con)
        caps = con.execute(
            "SELECT * FROM captures WHERE (? IS NULL OR game=?) ORDER BY id DESC LIMIT ?",
            (args.game, args.game, args.capture_limit),
        ).fetchall()
        suspects = con.execute(
            """
            SELECT suspects.*, captures.game
            FROM suspects JOIN captures ON captures.id=suspects.capture_id
            WHERE (? IS NULL OR captures.game=?)
            ORDER BY suspects.score DESC, suspects.id LIMIT ?
            """,
            (args.game, args.game, args.suspect_limit),
        ).fetchall()
        findings = con.execute(
            "SELECT * FROM findings WHERE (? IS NULL OR game=?) ORDER BY status='confirmed' DESC, confidence DESC, updated_at DESC LIMIT ?",
            (args.game, args.game, args.finding_limit),
        ).fetchall()
        rules = con.execute(
            "SELECT * FROM fix_rules WHERE (? IS NULL OR game=?) ORDER BY status, updated_at DESC",
            (args.game, args.game),
        ).fetchall()
    lines = [
        "# Stereo Forensics Findings",
        "",
        f"Generated: {utc_now()}",
        "",
        "## Captures",
        "",
    ]
    for cap in caps:
        lines.append(f"- `capture:{cap['id']}` frame={cap['latest_frame']} events={cap['event_count']} issues={cap['issue_count']} `{cap['session_dir']}`")
    lines += ["", "## Top Suspects", ""]
    for suspect in suspects:
        lines.append(f"- `suspect:{suspect['id']}` cap={suspect['capture_id']} score={suspect['score']:.2f} `{suspect['kind']}` L={suspect['left_event']} R={suspect['right_event']} shader={suspect['shader_crc']} root={suspect['root']} slot={suspect['slot']}")
        lines.append(f"  {suspect['reason']}")
    lines += ["", "## Findings", ""]
    for row in findings:
        lines.append(f"### {row['finding_id']} - {row['title']}")
        lines.append("")
        lines.append(f"- Status: `{row['status']}`")
        lines.append(f"- Confidence: `{row['confidence']:.2f}`")
        if row["summary"]:
            lines.append(f"- Summary: {row['summary']}")
        if row["implication"]:
            lines.append(f"- Implication: {row['implication']}")
        if row["next_action"]:
            lines.append(f"- Next action: {row['next_action']}")
        lines.append("")
    lines += ["## Fix Rules", ""]
    for rule in rules:
        lines.append(f"- `{rule['name']}` status={rule['status']} source={rule['source_finding_id'] or rule['source_experiment_id']}")
    text = "\n".join(lines).rstrip() + "\n"
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    print(text)
    return 0


def add_common_db(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--db", default=str(DEFAULT_DB), help=f"SQLite DB path (default: {DEFAULT_DB})")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    add_common_db(parser)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("init")
    p.set_defaults(func=cmd_init)

    p = sub.add_parser("ingest-session")
    p.add_argument("session")
    p.add_argument("--game")
    p.add_argument("--ue-version")
    p.add_argument("--executable-hash")
    p.set_defaults(func=cmd_ingest)

    p = sub.add_parser("analyze-capture")
    p.add_argument("--capture-id", type=int, required=True)
    p.add_argument("--auto-findings", action="store_true")
    p.set_defaults(func=cmd_analyze_capture)

    p = sub.add_parser("analyze-session")
    p.add_argument("session")
    p.add_argument("--game")
    p.add_argument("--ue-version")
    p.add_argument("--executable-hash")
    p.add_argument("--auto-findings", action="store_true")
    p.set_defaults(func=cmd_analyze_session)

    p = sub.add_parser("add-finding")
    p.add_argument("--finding-id")
    p.add_argument("--game")
    p.add_argument("--title", required=True)
    p.add_argument("--status", default="open", choices=["open", "confirmed", "rejected", "superseded", "fixed"])
    p.add_argument("--confidence", type=float, default=0.5)
    p.add_argument("--summary", required=True)
    p.add_argument("--implication")
    p.add_argument("--next-action")
    p.add_argument("--tags")
    p.add_argument("--supersedes")
    p.set_defaults(func=cmd_add_finding)

    p = sub.add_parser("add-evidence")
    p.add_argument("--finding-id", required=True)
    p.add_argument("--capture-id", type=int)
    p.add_argument("--session")
    p.add_argument("--event", type=int)
    p.add_argument("--shader-crc")
    p.add_argument("--resource-hex")
    p.add_argument("--slot")
    p.add_argument("--type", default="observation")
    p.add_argument("--path")
    p.add_argument("--summary", required=True)
    p.add_argument("--json")
    p.set_defaults(func=cmd_add_evidence)

    p = sub.add_parser("add-experiment")
    p.add_argument("--name")
    p.add_argument("--capture-id", type=int)
    p.add_argument("--session")
    p.add_argument("--game")
    p.add_argument("--status", default="observed")
    p.add_argument("--action")
    p.add_argument("--score", type=float)
    p.add_argument("--right-roi-delta", type=float)
    p.add_argument("--left-roi-delta", type=float)
    p.add_argument("--likely-causal", type=int)
    p.add_argument("--rule")
    p.add_argument("--rule-json")
    p.add_argument("--result")
    p.add_argument("--result-json")
    p.add_argument("--promote-fix-rule", action="store_true", help="If likely-causal, also upsert a durable fix rule from --rule/--rule-json")
    p.add_argument("--fix-rule-name")
    p.add_argument("--fix-rule-status", default="validated", choices=["experimental", "validated", "deprecated"])
    p.add_argument("--risk")
    p.set_defaults(func=cmd_add_experiment)

    p = sub.add_parser("add-fix-rule")
    p.add_argument("--name", required=True)
    p.add_argument("--game")
    p.add_argument("--status", default="experimental", choices=["experimental", "validated", "deprecated"])
    p.add_argument("--finding-id")
    p.add_argument("--experiment-id", type=int)
    p.add_argument("--guards")
    p.add_argument("--actions")
    p.add_argument("--rule")
    p.add_argument("--rule-json")
    p.add_argument("--validation")
    p.add_argument("--risk")
    p.set_defaults(func=cmd_add_fix_rule)

    p = sub.add_parser("list-findings")
    p.add_argument("--game")
    p.add_argument("--status")
    p.add_argument("--limit", type=int, default=50)
    p.set_defaults(func=cmd_list_findings)

    p = sub.add_parser("show-finding")
    p.add_argument("finding_id")
    p.set_defaults(func=cmd_show_finding)

    p = sub.add_parser("show-shader")
    p.add_argument("crc")
    p.add_argument("--limit", type=int, default=40)
    p.set_defaults(func=cmd_show_shader)

    p = sub.add_parser("show-resource")
    p.add_argument("resource")
    p.add_argument("--limit", type=int, default=40)
    p.set_defaults(func=cmd_show_resource)

    p = sub.add_parser("rank-suspects")
    p.add_argument("--game")
    p.add_argument("--capture-id", type=int)
    p.add_argument("--limit", type=int, default=30)
    p.set_defaults(func=cmd_rank_suspects)

    p = sub.add_parser("explain-event")
    p.add_argument("--capture-id", type=int, required=True)
    p.add_argument("--event", type=int, required=True)
    p.set_defaults(func=cmd_explain_event)

    p = sub.add_parser("import-shader-semantics")
    p.add_argument("path")
    p.add_argument("--stage")
    p.set_defaults(func=cmd_import_shader_semantics)

    p = sub.add_parser("next-actions")
    p.add_argument("--game")
    p.add_argument("--limit", type=int, default=20)
    p.set_defaults(func=cmd_next_actions)

    p = sub.add_parser("report")
    p.add_argument("--game")
    p.add_argument("--out")
    p.add_argument("--capture-limit", type=int, default=20)
    p.add_argument("--finding-limit", type=int, default=100)
    p.add_argument("--suspect-limit", type=int, default=25)
    p.set_defaults(func=cmd_report)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
