#!/usr/bin/env python3
"""Build lightweight shader semantic summaries for Stereo Forensics.

For DXIL containers this can invoke the existing dxil-patch tool to disassemble
and then extracts the operations that matter for stereo debugging. For SM4/SM5
DXBC containers it extracts conservative reflection/signature/token facts from
RDEF/ISGN/OSGN/SHEX/SHDR chunks. The DXBC path is intentionally descriptive,
not a full decompiler.
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


DXBC_OPCODE_NAMES = {
    2: "break",
    3: "breakc",
    4: "call",
    5: "callc",
    6: "case",
    7: "continue",
    8: "continuec",
    9: "cut",
    10: "default",
    13: "discard",
    18: "else",
    21: "endif",
    22: "endloop",
    23: "endswitch",
    31: "if",
    44: "label",
    45: "ld",
    46: "ld_ms",
    48: "loop",
    53: "customdata",
    62: "ret",
    63: "retc",
    69: "sample",
    70: "sample_c",
    71: "sample_c_lz",
    72: "sample_l",
    73: "sample_d",
    74: "sample_b",
    76: "switch",
    88: "dcl_resource",
    89: "dcl_constant_buffer",
    90: "dcl_sampler",
    95: "dcl_input",
    96: "dcl_input_sgv",
    97: "dcl_input_siv",
    98: "dcl_input_ps",
    99: "dcl_input_ps_sgv",
    100: "dcl_input_ps_siv",
    101: "dcl_output",
    102: "dcl_output_sgv",
    103: "dcl_output_siv",
    104: "dcl_temps",
    105: "dcl_indexable_temp",
    106: "dcl_global_flags",
}

RESOURCE_TYPE_NAMES = {
    0: "cbuffer",
    1: "tbuffer",
    2: "texture",
    3: "sampler",
    4: "uav_rwtyped",
    5: "structured",
    6: "uav_rwstructured",
    7: "byteaddress",
    8: "uav_rwbyteaddress",
    9: "uav_append_structured",
    10: "uav_consume_structured",
    11: "uav_rwstructured_counter",
    12: "uav_feedbacktexture",
}

RETURN_TYPE_NAMES = {
    0: "unknown",
    1: "unorm",
    2: "snorm",
    3: "sint",
    4: "uint",
    5: "float",
    6: "mixed",
    7: "double",
    8: "continued",
}

DIMENSION_NAMES = {
    0: "unknown",
    1: "buffer",
    2: "texture1d",
    3: "texture1darray",
    4: "texture2d",
    5: "texture2darray",
    6: "texture2dms",
    7: "texture2dmsarray",
    8: "texture3d",
    9: "texturecube",
    10: "texturecubearray",
    11: "bufferex",
}


def u32(data: bytes, off: int, default: int = 0) -> int:
    if off < 0 or off + 4 > len(data):
        return default
    return struct.unpack_from("<I", data, off)[0]


def cstring(data: bytes, off: int) -> str:
    if off <= 0 or off >= len(data):
        return ""
    end = data.find(b"\0", off)
    if end < 0:
        return ""
    return data[off:end].decode("utf-8", errors="replace")


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


def chunk_payloads(path: Path) -> dict[str, list[bytes]]:
    data = path.read_bytes()
    if len(data) < 32 or data[:4] != b"DXBC":
        return {}
    out: dict[str, list[bytes]] = {}
    chunk_count = u32(data, 28)
    for i in range(chunk_count):
        off = u32(data, 32 + i * 4)
        if off + 8 > len(data):
            continue
        fourcc = data[off : off + 4].decode("ascii", errors="replace")
        size = u32(data, off + 4)
        if off + 8 + size > len(data):
            continue
        out.setdefault(fourcc, []).append(data[off + 8 : off + 8 + size])
    return out


def parse_rdef(data: bytes) -> dict[str, Any]:
    if len(data) < 32:
        return {}
    cb_count = u32(data, 0)
    cb_offset = u32(data, 4)
    res_count = u32(data, 8)
    res_offset = u32(data, 12)
    target = u32(data, 16)
    creator = cstring(data, u32(data, 24))

    constant_buffers = []
    if cb_count < 1024 and cb_offset < len(data):
        for i in range(cb_count):
            base = cb_offset + i * 24
            if base + 24 > len(data):
                break
            var_count = u32(data, base + 4)
            var_offset = u32(data, base + 8)
            item = {
                "index": i,
                "name": cstring(data, u32(data, base)),
                "variable_count": var_count,
                "size": u32(data, base + 12),
                "flags": u32(data, base + 16),
                "type": u32(data, base + 20),
                "variables": [],
            }
            if var_count < 4096 and var_offset < len(data):
                variables = []
                for j in range(var_count):
                    vbase = var_offset + j * 40
                    if vbase + 16 > len(data):
                        break
                    variables.append(
                        {
                            "name": cstring(data, u32(data, vbase)),
                            "start_offset": u32(data, vbase + 4),
                            "size": u32(data, vbase + 8),
                            "flags": u32(data, vbase + 12),
                        }
                    )
                item["variables"] = variables[:512]
            constant_buffers.append(item)

    resources = []
    if res_count < 4096 and res_offset < len(data):
        for i in range(res_count):
            base = res_offset + i * 32
            if base + 32 > len(data):
                break
            rtype = u32(data, base + 4)
            ret = u32(data, base + 8)
            dim = u32(data, base + 12)
            bind_point = u32(data, base + 20)
            bind_count = u32(data, base + 24)
            resources.append(
                {
                    "index": i,
                    "name": cstring(data, u32(data, base)),
                    "type": RESOURCE_TYPE_NAMES.get(rtype, str(rtype)),
                    "type_id": rtype,
                    "return_type": RETURN_TYPE_NAMES.get(ret, str(ret)),
                    "dimension": DIMENSION_NAMES.get(dim, str(dim)),
                    "sample_count": u32(data, base + 16),
                    "bind_point": bind_point,
                    "bind_count": bind_count,
                    "flags": u32(data, base + 28),
                }
            )

    return {
        "target": target,
        "creator": creator,
        "constant_buffer_count": len(constant_buffers),
        "constant_buffers": constant_buffers[:256],
        "resource_binding_count": len(resources),
        "resource_bindings": resources[:512],
    }


def parse_signature(data: bytes) -> dict[str, Any]:
    if len(data) < 8:
        return {"parameter_count": 0, "parameters": []}
    count = u32(data, 0)
    offset = u32(data, 4)
    params = []
    if count >= 4096 or offset >= len(data):
        return {"parameter_count": count, "parameters": []}
    # DXBC signatures use 24-byte records, with SG1-style streams extending
    # some records. Keep this conservative and only read the stable prefix.
    for i in range(count):
        base = offset + i * 24
        if base + 24 > len(data):
            break
        params.append(
            {
                "index": i,
                "semantic": cstring(data, u32(data, base)),
                "semantic_index": u32(data, base + 4),
                "system_value": u32(data, base + 8),
                "component_type": u32(data, base + 12),
                "register": u32(data, base + 16),
                "mask": u32(data, base + 20) & 0xFF,
                "read_write_mask": (u32(data, base + 20) >> 8) & 0xFF,
            }
        )
    return {"parameter_count": count, "parameters": params[:256]}


def analyze_dxbc_tokens(data: bytes) -> dict[str, Any]:
    if len(data) < 8:
        return {}
    token_count = len(data) // 4
    major = (u32(data, 0) >> 4) & 0xF
    minor = u32(data, 0) & 0xF
    shader_type = (u32(data, 0) >> 16) & 0xFFFF
    opcode_counts: dict[str, int] = {}
    raw_opcode_counts: dict[str, int] = {}
    i = 2
    instruction_count = 0
    while i < token_count:
        value = u32(data, i * 4)
        opcode = value & 0x7FF
        length = (value >> 24) & 0x7F
        name = DXBC_OPCODE_NAMES.get(opcode, f"opcode_{opcode}")
        opcode_counts[name] = opcode_counts.get(name, 0) + 1
        raw_opcode_counts[str(opcode)] = raw_opcode_counts.get(str(opcode), 0) + 1
        instruction_count += 1
        if length == 0:
            length = 1
        i += length
    sample_names = {"sample", "sample_c", "sample_c_lz", "sample_l", "sample_d", "sample_b"}
    branch_names = {"if", "else", "loop", "switch", "breakc", "continuec", "callc", "retc", "case"}
    output_decl_names = {"dcl_output", "dcl_output_sgv", "dcl_output_siv"}
    return {
        "shader_model": f"{major}_{minor}",
        "shader_type_id": shader_type,
        "token_count": token_count,
        "declared_program_length": u32(data, 4),
        "instruction_count": instruction_count,
        "opcode_counts": opcode_counts,
        "raw_opcode_counts": raw_opcode_counts,
        "sample_count": sum(opcode_counts.get(name, 0) for name in sample_names),
        "texture_load_count": opcode_counts.get("ld", 0) + opcode_counts.get("ld_ms", 0),
        "branch_count": sum(opcode_counts.get(name, 0) for name in branch_names),
        "uses_discard_or_clip": opcode_counts.get("discard", 0) > 0,
        "output_declaration_count": sum(opcode_counts.get(name, 0) for name in output_decl_names),
        "constant_buffer_declaration_count": opcode_counts.get("dcl_constant_buffer", 0),
        "resource_declaration_count": opcode_counts.get("dcl_resource", 0),
    }


def analyze_dxbc_container(path: Path) -> dict[str, Any]:
    chunks = chunk_payloads(path)
    rdef = parse_rdef(chunks.get("RDEF", [b""])[0]) if chunks.get("RDEF") else {}
    input_sig = parse_signature((chunks.get("ISGN") or chunks.get("ISG1") or [b""])[0])
    output_sig = parse_signature((chunks.get("OSGN") or chunks.get("OSG1") or [b""])[0])
    token_chunk = (chunks.get("SHEX") or chunks.get("SHDR") or [b""])[0]
    token_sem = analyze_dxbc_tokens(token_chunk) if token_chunk else {}
    resource_bindings = rdef.get("resource_bindings", [])
    cbuffers = rdef.get("constant_buffers", [])
    sample_count = token_sem.get("sample_count", 0)
    texture_load_count = token_sem.get("texture_load_count", 0)
    branch_count = token_sem.get("branch_count", 0)
    output_count = output_sig.get("parameter_count", 0) or token_sem.get("output_declaration_count", 0)
    cbuffer_ranges = []
    for cbuffer in cbuffers:
        ranges = [
            {
                "name": var.get("name"),
                "start_offset": var.get("start_offset"),
                "size": var.get("size"),
            }
            for var in cbuffer.get("variables", [])
        ]
        cbuffer_ranges.append(
            {
                "name": cbuffer.get("name"),
                "size": cbuffer.get("size"),
                "variables": ranges[:512],
            }
        )
    return {
        "dxil": False,
        "dxbc": True,
        "reflection_available": bool(rdef),
        "creator": rdef.get("creator", ""),
        "constant_buffer_count": len(cbuffers),
        "constant_buffers": cbuffers,
        "cbuffer_load_count": token_sem.get("constant_buffer_declaration_count", 0),
        "cbuffer_byte_ranges": cbuffer_ranges,
        "resource_binding_count": len(resource_bindings),
        "resource_bindings": resource_bindings,
        "sample_count": sample_count,
        "texture_load_count": texture_load_count,
        "store_count": output_count,
        "output_parameter_count": output_sig.get("parameter_count", 0),
        "output_parameters": output_sig.get("parameters", []),
        "input_parameter_count": input_sig.get("parameter_count", 0),
        "input_parameters": input_sig.get("parameters", []),
        "branch_count": branch_count,
        "uses_discard_or_clip": bool(token_sem.get("uses_discard_or_clip")),
        "likely_visible_pixel_shader": bool(output_count or sample_count or texture_load_count),
        "token_analysis": token_sem,
    }


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
        result["semantics"] = analyze_dxbc_container(path)
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
