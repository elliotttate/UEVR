#!/usr/bin/env python3
"""Build lightweight shader semantic summaries for Stereo Forensics.

For DXIL containers this can invoke the existing dxil-patch tool to disassemble
and then extracts the operations that matter for stereo debugging. For DXBC
SM4/SM5 containers it still records container/chunk metadata and CRCs.
"""

from __future__ import annotations

import argparse
import binascii
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path
from typing import Any


def read_container(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    result: dict[str, Any] = {
        "path": str(path),
        "size": len(data),
        "crc32": f"0x{binascii.crc32(data) & 0xFFFFFFFF:08X}",
        "container": False,
        "container_kind": "raw",
        "chunks": [],
    }
    if len(data) < 32 or data[:4] != b"DXBC":
        return result
    chunk_count = struct.unpack_from("<I", data, 28)[0]
    result["container"] = True
    result["container_kind"] = "DXBC"
    result["chunk_count"] = chunk_count
    chunks = []
    for i in range(chunk_count):
        off_pos = 32 + i * 4
        if off_pos + 4 > len(data):
            break
        off = struct.unpack_from("<I", data, off_pos)[0]
        if off + 8 > len(data):
            continue
        fourcc = data[off : off + 4].decode("ascii", errors="replace")
        size = struct.unpack_from("<I", data, off + 4)[0]
        chunks.append({"fourcc": fourcc, "offset": off, "size": size})
        if fourcc == "DXIL":
            result["container_kind"] = "DXIL"
    result["chunks"] = chunks
    return result


def disassemble_dxil(path: Path, dxil_patch: str | None) -> str:
    if not dxil_patch:
        return ""
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "shader.ll"
        proc = subprocess.run(
            [dxil_patch, "disasm", str(path), "-o", str(out)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if proc.returncode != 0:
            return ""
        return out.read_text(encoding="utf-8", errors="replace")


def analyze_dxil_text(text: str) -> dict[str, Any]:
    cbuffer_loads = []
    handles = []
    samples = []
    texture_loads = []
    stores = []
    branches = 0

    handle_re = re.compile(
        r"(?P<result>%[-A-Za-z0-9_.$]+)\s*=.*@dx\.op\.createHandle[^()]*\([^)]*i32\s+(?P<resource_class>[0-9]+)\s*,\s*i32\s+(?P<range_id>[^,\)]+)\s*,\s*i32\s+(?P<index>[^,\)]+)"
    )
    cbuffer_re = re.compile(
        r"(?P<result>%[-A-Za-z0-9_.$]+)\s*=.*@dx\.op\.cbufferLoad(?:Legacy)?\.[^(]+\(\s*i32\s+(?P<opcode>[0-9]+)\s*,\s*%dx\.types\.Handle\s+(?P<handle>%[-A-Za-z0-9_.$]+)\s*,\s*i32\s+(?P<index>[^,\)]+)"
    )
    sample_re = re.compile(r"(?P<result>%[-A-Za-z0-9_.$]+)\s*=.*@dx\.op\.(?P<op>sample[^.(]*|textureLoad)[^@]*")
    store_re = re.compile(r"@dx\.op\.(?P<op>storeOutput|storeDepth|storeCoverage|bufferStore|textureStore)")

    for line_no, line in enumerate(text.splitlines(), 1):
        if " br " in line or line.strip().startswith("br "):
            branches += 1
        m = handle_re.search(line)
        if m:
            handles.append({"line": line_no, **m.groupdict()})
        m = cbuffer_re.search(line)
        if m:
            cbuffer_loads.append({"line": line_no, **m.groupdict()})
        m = sample_re.search(line)
        if m:
            item = {"line": line_no, **m.groupdict()}
            if m.group("op") == "textureLoad":
                texture_loads.append(item)
            else:
                samples.append(item)
        for m in store_re.finditer(line):
            stores.append({"line": line_no, "op": m.group("op")})

    text_lower = text.lower()
    return {
        "dxil": True,
        "handle_count": len(handles),
        "handles": handles[:256],
        "cbuffer_load_count": len(cbuffer_loads),
        "cbuffer_loads": cbuffer_loads[:512],
        "sample_count": len(samples),
        "samples": samples[:256],
        "texture_load_count": len(texture_loads),
        "texture_loads": texture_loads[:256],
        "store_count": len(stores),
        "stores": stores[:256],
        "branch_count": branches,
        "uses_discard_or_clip": "discard" in text_lower or "clip" in text_lower or "kill" in text_lower,
        "likely_visible_pixel_shader": bool(stores or samples or texture_loads),
    }


def analyze_shader(path: Path, dxil_patch: str | None) -> dict[str, Any]:
    result = read_container(path)
    if result.get("container_kind") == "DXIL":
        text = disassemble_dxil(path, dxil_patch)
        if text:
            result["semantics"] = analyze_dxil_text(text)
        else:
            result["semantics"] = {"dxil": True, "error": "DXIL disassembly unavailable"}
    else:
        result["semantics"] = {"dxil": False, "note": "DXBC token-level semantic extraction not implemented in this script"}
    return result


def write_semantics_to_db(doc: dict[str, Any], db_path: str, stage: str | None) -> int:
    from stereo_forensics_db import connect, import_shader_semantics

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp) / "shader_semantics.json"
        tmp_path.write_text(json.dumps(doc), encoding="utf-8")
        with connect(db_path) as con:
            return import_shader_semantics(con, tmp_path, stage)


def cmd_analyze(args: argparse.Namespace) -> int:
    doc = analyze_shader(Path(args.shader), args.dxil_patch)
    text = json.dumps(doc, indent=2)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    if args.db:
        write_semantics_to_db(doc, args.db, args.stage)
    print(text)
    return 0


def cmd_analyze_dir(args: argparse.Namespace) -> int:
    root = Path(args.dir)
    files = sorted(p for p in root.rglob("*") if p.suffix.lower() in {".dxbc", ".dxil", ".bin"})
    shaders = [analyze_shader(path, args.dxil_patch) for path in files[: args.limit]]
    doc = {"schema": "uevr.stereo_forensics.shader_semantics.v1", "shader_count": len(shaders), "shaders": shaders}
    text = json.dumps(doc, indent=2)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    if args.db:
        write_semantics_to_db(doc, args.db, args.stage)
    print(text)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("analyze")
    p.add_argument("shader")
    p.add_argument("--dxil-patch")
    p.add_argument("--out")
    p.add_argument("--db")
    p.add_argument("--stage")
    p.set_defaults(func=cmd_analyze)
    p = sub.add_parser("analyze-dir")
    p.add_argument("dir")
    p.add_argument("--dxil-patch")
    p.add_argument("--limit", type=int, default=10000)
    p.add_argument("--out")
    p.add_argument("--db")
    p.add_argument("--stage")
    p.set_defaults(func=cmd_analyze_dir)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
