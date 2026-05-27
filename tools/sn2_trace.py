#!/usr/bin/env python3
"""Capstone: trace a StereoForensics symptom back to a code-side root-cause hypothesis.

Joins the layers built by the other tools:
  eye_diff issue  (what differs between eyes)
    -> the involved D3D12 events (events.jsonl: ps_crc/cs_crc, issuer_stack)
    -> symbolized issuer callstack         (sn2_symbolizer: addr -> engine fn)
    -> shader/code map                      (sn2_shader_code_map: crc -> UE source)
    -> lineage producer                     (lineage.json)
  => a ranked root-cause hypothesis with the engine function / source file.

Gracefully degrades: if events lack issuer_stack (StereoForensics stack capture
not enabled), it still uses ps_crc->shader map + lineage. With stacks, it names
the engine call site that issued (or failed to issue) the work.

USAGE
    python sn2_trace.py [--session <dir>] [--limit 8] [--kind eye_event_count_mismatch]
    python sn2_trace.py --session <dir> --issue 0      # deep-trace one issue
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any

try:
    from sn2_symbolizer import Symbolizer, image_base
    import sn2_shader_code_map as scm
except Exception:  # pragma: no cover
    Symbolizer = None  # type: ignore
    scm = None  # type: ignore


def forensics_dir() -> Path:
    return Path(os.environ.get("UEVR_STEREO_FORENSICS_DIR", r"C:\tmp\uevr_forensics"))


def latest_session() -> Path | None:
    root = forensics_dir()
    if not root.exists():
        return None
    cands = sorted(root.glob("session_*"), key=lambda p: p.stat().st_mtime, reverse=True)
    return cands[0] if cands else None


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except Exception:
        return None


def find_events(session: Path, indices: set[int]) -> dict[int, dict[str, Any]]:
    """Stream events.jsonl and pull the events whose event_index is in `indices`."""
    out: dict[int, dict[str, Any]] = {}
    p = session / "events.jsonl"
    if not p.exists() or not indices:
        return out
    want = set(indices)
    with p.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.strip():
                continue
            try:
                ev = json.loads(line)
            except Exception:
                continue
            ei = ev.get("event_index")
            if ei in want:
                out[ei] = ev
                want.discard(ei)
                if not want:
                    break
    return out


def symbolize_stack(sym, ev: dict[str, Any]) -> list[dict[str, Any]]:
    """Symbolize an event's issuer_stack (list of VAs or RVAs). Skips UEVR frames."""
    if sym is None:
        return []
    stack = ev.get("issuer_stack") or ev.get("stack") or ev.get("callstack")
    base = ev.get("module_base") or ev.get("image_base")
    if not isinstance(stack, list):
        return []
    frames = []
    for a in stack:
        try:
            addr = int(a, 0) if isinstance(a, str) else int(a)
        except Exception:
            continue
        if base:
            try:
                bbase = int(base, 0) if isinstance(base, str) else int(base)
                r = sym.resolve_rva(addr - bbase if addr >= bbase else addr)
            except Exception:
                r = sym.resolve(addr)
        else:
            r = sym.resolve(addr)
        frames.append(r)
    # First frame that looks like engine code (not UEVR/our hook) is the "issuer".
    return frames


def first_engine_frame(frames: list[dict[str, Any]]) -> dict[str, Any] | None:
    for fr in frames:
        if fr.get("plausible") is False:
            continue  # mis-resolved (UEVR DLL frame / between symbols)
        d = (fr.get("demangled") or fr.get("symbol") or "")
        low = d.lower()
        if d and "uevr" not in low and "sn2" not in low and "stereoforensics" not in low \
                and "_fac_tidy" not in low and "atexit" not in low:
            return fr
    for fr in frames:  # fallback: first plausibly-resolved frame
        if fr.get("plausible") is not False and (fr.get("demangled") or fr.get("symbol")):
            return fr
    return None


HYPOTHESIS = {
    "eye_event_count_mismatch":
        "Work present on one eye but not the other. The issuing engine code ran a different number of times per view — look for a per-view loop / visibility gate that bailed for the missing eye (e.g. View.bShouldRender, culling, indirect dispatch args = 0).",
    "pso_or_shader_differs":
        "The paired eyes use a different PSO/shader permutation — a per-view permutation or material selection diverged.",
    "graphics_cbv_hash_differs":
        "Right eye binds different constant-buffer CONTENTS at the same root slot — check the View/Material uniform-buffer setup for that view (FViewInfo / FViewUniformShaderParameters).",
    "descriptor_missing_on_right":
        "A descriptor bound on left is absent on right — the right-eye pass skipped a bind, likely an upstream resource never produced for that view.",
    "same_resource_different_slice":
        "Same Texture2DArray, different array slice per eye — instanced-stereo slice selection; check the SRV creation / per-view slice index.",
    "same_desc_different_resource":
        "Same descriptor shape, different physical resource per eye — pooled/transient RDG resource assigned differently per view.",
    "descriptor_resource_differs":
        "Different physical resource bound per eye at the same slot — check the producer lineage; the right eye's producer may be missing or stale.",
    "producer_lineage_differs":
        "The two eyes read resources with different producers — the upstream producing pass diverged per view (the deepest signal: trace the producer event).",
}


def trace_issue(issue: dict[str, Any], events: dict[int, dict[str, Any]],
                producers: dict[str, Any], sym, shadermap: dict[str, Any]) -> dict[str, Any]:
    kind = issue.get("kind", "")

    # Resolve the per-eye event object. eye_event_count_mismatch embeds the full
    # sample event (with issuer_stack) as sample_{left,right}_event; other kinds
    # reference it by index in {left,right}_event (looked up from events.jsonl).
    def resolve_ev(label: str) -> dict[str, Any] | None:
        s = issue.get(f"sample_{label}_event")
        if isinstance(s, dict) and s:
            return s
        idx = issue.get(f"{label}_event")
        return events.get(idx) if isinstance(idx, int) else None

    left_ev = resolve_ev("left")
    right_ev = resolve_ev("right")
    out: dict[str, Any] = {
        "kind": kind,
        "severity": issue.get("severity"),
        "key": issue.get("key"),
        "root": issue.get("root"),
        "slot": issue.get("slot"),
        "left_event": (left_ev or {}).get("event_index", issue.get("left_event")),
        "right_event": (right_ev or {}).get("event_index", issue.get("right_event")),
        "left_count": issue.get("left_count"),
        "right_count": issue.get("right_count"),
        "symptom": HYPOTHESIS.get(kind, "Per-eye divergence."),
        "shaders": {},
        "code_sites": {},
    }

    for label, ev in (("left", left_ev), ("right", right_ev)):
        if not ev:
            continue
        # work dims: for dispatch/dispatch_mesh arg0/1/2 are thread-group X/Y/Z;
        # for draws they're index/instance counts. Shows what the absent eye is missing.
        out.setdefault("work", {})[label] = {
            "kind": ev.get("kind"),
            "arg0": ev.get("arg0"), "arg1": ev.get("arg1"), "arg2": ev.get("arg2"),
            "eye_bucket": ev.get("eye_bucket"),
        }
        # shader/code map
        for crc_key in ("ps_crc_hex", "ps_crc", "cs_crc_hex", "cs_crc"):
            crc = ev.get(crc_key)
            if crc:
                c = scm.norm_crc(str(crc)) if scm else str(crc)
                entry = shadermap.get(c) if shadermap else None
                if entry:
                    out["shaders"][label] = {"crc": "0x" + c,
                                             "names": _shader_names(entry)}
                break
        # symbolized issuer callstack
        frames = symbolize_stack(sym, ev)
        if frames:
            top = first_engine_frame(frames)
            out["code_sites"][label] = {
                "issuer": (top or {}).get("demangled") or (top or {}).get("symbol"),
                "issuer_offset": (top or {}).get("offset"),
                "source": (top or {}).get("source"),
                "role": (top or {}).get("role"),
                "stack": [s for f in frames if (s := (f.get("demangled") or f.get("symbol")))][:10],
            }

    # lineage producer of the resource (if the issue names one)
    res = issue.get("resource") or issue.get("left_resource") or issue.get("right_resource")
    if res and producers:
        out["producer"] = producers.get(str(res)) or producers.get(res)

    # one-line conclusion
    site = out["code_sites"].get("left") or out["code_sites"].get("right")
    if site and site.get("issuer"):
        out["hypothesis"] = f"{out['symptom']} Issuing/likely code site: {site['issuer']}" + \
            (f" ({site['source']})" if site.get("source") else "") + "."
    else:
        sh = out["shaders"].get("left") or out["shaders"].get("right")
        shn = (sh or {}).get("names")
        out["hypothesis"] = out["symptom"] + (
            f" Shader: {shn}. Enable UEVR_STEREO_FORENSICS_STACKS to get the issuing engine function." if shn
            else " Enable UEVR_STEREO_FORENSICS_STACKS for the issuing code site.")
    return out


def _shader_names(entry: dict[str, Any]) -> list[str]:
    names = []
    for v in (entry.get("psos") or {}).values():
        if isinstance(v, dict) and v.get("name"):
            names.append(v["name"])
    if entry.get("compute_shader_name"):
        names.append(entry["compute_shader_name"])
    sem = entry.get("pso_semantics")
    if isinstance(sem, dict) and sem.get("name"):
        names.append(sem["name"])
    return list(dict.fromkeys(names))


def build_producer_index(lineage: Any) -> dict[str, Any]:
    out: dict[str, Any] = {}
    if not isinstance(lineage, dict):
        return out
    for p in lineage.get("latest_resource_producers", []) or []:
        key = p.get("resource_hex") or str(p.get("resource"))
        if key:
            out[key] = p
            if p.get("resource") is not None:
                out[str(p["resource"])] = p
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--session", help="session dir (default: latest)")
    ap.add_argument("--issue", type=int, help="trace only this issue index")
    ap.add_argument("--kind", help="filter issues by kind")
    ap.add_argument("--limit", type=int, default=8)
    args = ap.parse_args()

    session = Path(args.session) if args.session else latest_session()
    if session is None or not session.exists():
        print(json.dumps({"ok": False, "error": f"no session under {forensics_dir()}"}, indent=2))
        return 0

    eye_diff = load_json(session / "eye_diff.json") or {}
    lineage = load_json(session / "lineage.json")
    issues = eye_diff.get("issues", []) if isinstance(eye_diff, dict) else []
    if args.kind:
        issues = [i for i in issues if i.get("kind") == args.kind]
    if args.issue is not None:
        issues = issues[args.issue:args.issue + 1]
    else:
        issues = issues[:args.limit]

    # Gather the event indices we need, pull just those events.
    want: set[int] = set()
    for i in issues:
        for k in ("left_event", "right_event"):
            if isinstance(i.get(k), int):
                want.add(i[k])
    events = find_events(session, want)

    sym = Symbolizer() if Symbolizer else None
    shadermap = scm.build_index()["by_crc"] if scm else {}
    producers = build_producer_index(lineage)

    traces = [trace_issue(i, events, producers, sym, shadermap) for i in issues]
    # rank: code-site-resolved + higher severity first
    sev_rank = {"high": 0, "medium": 1, "info": 2}
    traces.sort(key=lambda t: (0 if t["code_sites"] else 1, sev_rank.get(t.get("severity") or "", 3)))

    print(json.dumps({
        "schema": "uevr.sn2.trace.v1",
        "session": str(session),
        "issue_count": len(eye_diff.get("issues", []) if isinstance(eye_diff, dict) else []),
        "traced": len(traces),
        "stacks_present": any(t["code_sites"] for t in traces),
        "traces": traces,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
