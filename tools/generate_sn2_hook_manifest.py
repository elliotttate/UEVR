#!/usr/bin/env python3
"""Generate a compact SN2 UE hook manifest from binfold/decompile output."""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import re
import struct
from pathlib import Path
from typing import Any


DEFAULT_EXE = Path(r"E:\Github\Subnautica 2\Subnautica2\Binaries\Win64\Subnautica2-Win64-Shipping.exe")
DEFAULT_SYMBOLS = Path(r"E:\Github\Subnautica 2\moddingkit\runs\binfold_scratch\symbols.json")
DEFAULT_DECOMP_ROOT = Path(r"E:\Github\Subnautica 2\moddingkit\runs")
DEFAULT_OUTPUT = Path(r"docs\sn2\sn2_hook_manifest.generated.json")
DEFAULT_RUNTIME_SOURCE = Path(__file__).resolve().parents[1] / "src" / "render" / "StereoForensics.cpp"


TARGETS = [
    {
        "key": "scene_renderer_render",
        "needles": ["FSceneRenderer::Render("],
        "raw_needles": ["?Render@FSceneRenderer@@"],
    },
    {
        "key": "setup_fog_uniform_parameters",
        "needles": ["SetupFogUniformParameters"],
        "raw_needles": ["?SetupFogUniformParameters@@"],
    },
    {
        "key": "basepass_mesh_processor_ctor",
        "needles": ["FBasePassMeshProcessor::FBasePassMeshProcessor"],
        "raw_needles": ["??0FBasePassMeshProcessor@@"],
    },
    {
        "key": "basepass_add_mesh_batch",
        "needles": ["FBasePassMeshProcessor::AddMeshBatch"],
        "raw_needles": ["?AddMeshBatch@FBasePassMeshProcessor@@"],
    },
    {
        "key": "basepass_try_add_mesh_batch",
        "needles": ["FBasePassMeshProcessor::TryAddMeshBatch"],
        "raw_needles": ["?TryAddMeshBatch@FBasePassMeshProcessor@@"],
    },
    {
        "key": "basepass_process_uniform_lightmap_policy",
        "needles": ["FBasePassMeshProcessor::Process<FUniformLightMapPolicy>"],
        "raw_needles": ["??$Process@VFUniformLightMapPolicy@@@FBasePassMeshProcessor@@"],
    },
    {
        "key": "render_single_layer_water",
        "needles": ["FDeferredShadingSceneRenderer::RenderSingleLayerWater("],
        "raw_needles": ["?RenderSingleLayerWater@FDeferredShadingSceneRenderer@@"],
    },
    {
        "key": "render_single_layer_water_inner",
        "needles": ["FDeferredShadingSceneRenderer::RenderSingleLayerWaterInner"],
        "raw_needles": ["?RenderSingleLayerWaterInner@FDeferredShadingSceneRenderer@@"],
    },
    {
        "key": "render_single_layer_water_depth_prepass",
        "needles": ["FDeferredShadingSceneRenderer::RenderSingleLayerWaterDepthPrepass"],
        "raw_needles": ["?RenderSingleLayerWaterDepthPrepass@FDeferredShadingSceneRenderer@@"],
    },
    {
        "key": "render_underwater_fog",
        "needles": ["RenderUnderWaterFog", "RenderUnderwaterFog"],
        "raw_needles": ["?RenderUnderWaterFog", "?RenderUnderwaterFog"],
    },
    {
        "key": "scene_renderer_compute_volumetric_fog",
        "needles": ["FSceneRenderer::ComputeVolumetricFog"],
        "raw_needles": ["?ComputeVolumetricFog@FSceneRenderer@@"],
    },
    {
        "key": "rdg_create_texture",
        "needles": ["FRDGBuilder::CreateTexture"],
        "raw_needles": ["?CreateTexture@FRDGBuilder@@"],
    },
    {
        "key": "rdg_create_uav",
        "needles": ["FRDGBuilder::CreateUAV"],
        "raw_needles": ["?CreateUAV@FRDGBuilder@@"],
    },
    {
        "key": "rdg_setup_parameter_pass",
        "needles": [
            "FRDGBuilder::SetupParameterPass",
            "FRDGBuilder::SetupPassInternals",
            "FRDGBuilder::SetupPassResources",
        ],
        "raw_needles": [
            "?SetupParameterPass@FRDGBuilder@@",
            "?SetupPassInternals@FRDGBuilder@@",
            "?SetupPassResources@FRDGBuilder@@",
        ],
    },
    {
        "key": "create_opaque_basepass_uniform_buffer",
        "needles": ["CreateOpaqueBasePassUniformBuffer"],
        "raw_needles": ["?CreateOpaqueBasePassUniformBuffer@@"],
    },
    {
        "key": "setup_shared_basepass_parameters",
        "needles": ["SetupSharedBasePassParameters"],
        "raw_needles": ["?SetupSharedBasePassParameters@@"],
    },
]


HEX_RE = re.compile(r"0x[0-9a-fA-F]+")
HASH_RE = re.compile(r"UE hash match \((strict|loose)\):\s+(\S+)\s+ref_rva=(0x[0-9a-fA-F]+)\s+hash=([0-9a-fA-F]+)")
CALL_TARGET_RE = re.compile(r"^\s*->\s+(0x[0-9a-fA-F]+)\s+(.+)$")
LINE_ADDR_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+")
RUNTIME_ACTION_ARRAY_RE = re.compile(
    r'\{"(?P<name>executable_actions|mutation_actions|probe_actions|unsupported_actions)",\s*json::array\(\{(?P<body>.*?)\}\)\}',
    re.S,
)
RUNTIME_ACTION_STRING_RE = re.compile(r'"([^"]+)"')


def parse_int(value: Any) -> int | None:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        value = value.strip()
        if not value:
            return None
        return int(value, 16) if value.lower().startswith("0x") else int(value)
    return None


def hex_or_none(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value:X}"


def norm_path(path: Any) -> str | None:
    if not path:
        return None
    return str(path)


def text_matches_target(text: str, target: dict[str, Any]) -> bool:
    if not text:
        return False
    return any(n in text for n in target["needles"]) or any(n in text for n in target["raw_needles"])


def matching_target_keys(text: str) -> list[str]:
    return [target["key"] for target in TARGETS if text_matches_target(text, target)]


class PeImage:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes() if path.exists() else b""
        self.image_base = 0x140000000
        self.sections: list[tuple[int, int, int, int]] = []
        if self.data:
            self._parse()

    def _parse(self) -> None:
        if len(self.data) < 0x100 or self.data[:2] != b"MZ":
            return
        pe_off = struct.unpack_from("<I", self.data, 0x3C)[0]
        if pe_off + 0x18 >= len(self.data) or self.data[pe_off:pe_off + 4] != b"PE\0\0":
            return
        num_sections = struct.unpack_from("<H", self.data, pe_off + 6)[0]
        opt_size = struct.unpack_from("<H", self.data, pe_off + 20)[0]
        opt_off = pe_off + 24
        magic = struct.unpack_from("<H", self.data, opt_off)[0]
        if magic == 0x20B:
            self.image_base = struct.unpack_from("<Q", self.data, opt_off + 24)[0]
        elif magic == 0x10B:
            self.image_base = struct.unpack_from("<I", self.data, opt_off + 28)[0]
        sec_off = opt_off + opt_size
        self.sections.clear()
        for i in range(num_sections):
            off = sec_off + i * 40
            if off + 40 > len(self.data):
                break
            virtual_size, virtual_address, raw_size, raw_ptr = struct.unpack_from("<IIII", self.data, off + 8)
            self.sections.append((virtual_address, virtual_size, raw_ptr, raw_size))

    def ea_to_rva(self, ea: int | None) -> int | None:
        if ea is None:
            return None
        if ea >= self.image_base:
            return ea - self.image_base
        return ea

    def rva_to_offset(self, rva: int | None) -> int | None:
        if rva is None:
            return None
        for va, virtual_size, raw_ptr, raw_size in self.sections:
            span = max(virtual_size, raw_size)
            if va <= rva < va + span:
                off = raw_ptr + (rva - va)
                return off if 0 <= off < len(self.data) else None
        return None

    def prologue(self, rva: int | None, length: int = 24) -> str | None:
        off = self.rva_to_offset(rva)
        if off is None:
            return None
        chunk = self.data[off:off + length]
        if not chunk:
            return None
        return " ".join(f"{b:02X}" for b in chunk)


def make_entry(
    *,
    key: str,
    source: str,
    name: str | None = None,
    raw_name: str | None = None,
    ea: int | None = None,
    rva: int | None = None,
    asm: str | None = None,
    c: str | None = None,
    match_kind: str | None = None,
    ref_rva: str | None = None,
    hash_value: str | None = None,
    note: str | None = None,
    matched_constraints: int | None = None,
    total_constraints: int | None = None,
    pe: PeImage | None = None,
) -> dict[str, Any]:
    if rva is None and pe is not None:
        rva = pe.ea_to_rva(ea)
    if ea is None and rva is not None and pe is not None:
        ea = pe.image_base + rva
    entry: dict[str, Any] = {
        "key": key,
        "source": source,
        "name": name,
        "raw_name": raw_name,
        "ea": hex_or_none(ea),
        "rva": hex_or_none(rva),
        "match_kind": match_kind,
        "ref_rva": ref_rva,
        "hash": hash_value,
        "matched_constraints": matched_constraints,
        "total_constraints": total_constraints,
        "prologue_pattern": pe.prologue(rva) if pe is not None else None,
        "decompile_asm": norm_path(asm),
        "decompile_c": norm_path(c),
        "note": note,
    }
    return {k: v for k, v in entry.items() if v is not None}


def entry_score(entry: dict[str, Any]) -> tuple[int, int, int, int]:
    strict = 2 if entry.get("match_kind") == "strict" else 1 if entry.get("match_kind") == "loose" else 0
    has_prologue = 1 if entry.get("prologue_pattern") else 0
    constraints = int(entry.get("matched_constraints") or 0)
    has_decomp = 1 if entry.get("decompile_asm") else 0
    return (strict, has_prologue, constraints, has_decomp)


def add_candidate(candidates: dict[str, dict[str, list[dict[str, Any]]]], entry: dict[str, Any]) -> None:
    candidates.setdefault(entry["key"], {}).setdefault(entry["source"], []).append(entry)


def collect_binfold_symbols(symbols: Path, pe: PeImage, candidates: dict[str, dict[str, list[dict[str, Any]]]]) -> int:
    if not symbols.exists():
        return 0
    with symbols.open("r", encoding="utf-8") as f:
        items = json.load(f)
    matched = 0
    for item in items:
        text = " ".join(str(item.get(k, "")) for k in ("name", "raw_name"))
        for key in matching_target_keys(text):
            matched += 1
            add_candidate(
                candidates,
                make_entry(
                    key=key,
                    source="binfold_symbols",
                    name=item.get("name"),
                    raw_name=item.get("raw_name"),
                    ea=parse_int(item.get("ea")),
                    rva=parse_int(item.get("rva")),
                    matched_constraints=parse_int(item.get("matched_constraints")),
                    total_constraints=parse_int(item.get("total_constraints")),
                    pe=pe,
                ),
            )
    return matched


def collect_selected_functions(root: Path, pe: PeImage, candidates: dict[str, dict[str, list[dict[str, Any]]]]) -> int:
    matched = 0
    for selected in root.rglob("selected_functions.json"):
        try:
            items = json.loads(selected.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        for item in items:
            text = " ".join(str(item.get(k, "")) for k in ("name", "raw_name", "asm", "c"))
            for key in matching_target_keys(text):
                matched += 1
                add_candidate(
                    candidates,
                    make_entry(
                        key=key,
                        source="selected_functions",
                        name=item.get("name"),
                        raw_name=item.get("raw_name"),
                        ea=parse_int(item.get("ea")),
                        rva=parse_int(item.get("rva")),
                        asm=item.get("asm"),
                        c=item.get("c"),
                        note=f"from {selected}",
                        pe=pe,
                    ),
                )
    return matched


def asm_header(lines: list[str]) -> tuple[str | None, str | None, int | None, int | None, str | None, str | None, str | None]:
    name = None
    raw_name = None
    ea = None
    rva = None
    match_kind = None
    ref_rva = None
    hash_value = None
    for line in lines[:48]:
        if line.startswith("ea:"):
            ea = parse_int(line.split(":", 1)[1].strip())
        elif line.startswith("rva:"):
            rva = parse_int(line.split(":", 1)[1].strip())
        elif line.startswith("name:"):
            name = line.split(":", 1)[1].strip()
        elif line.startswith("raw_name:"):
            raw_name = line.split(":", 1)[1].strip()
        if ea is None and not line.startswith("xrefs_to:"):
            m_addr = LINE_ADDR_RE.match(line)
            if m_addr:
                ea = int(m_addr.group(1), 16)
        m_hash = HASH_RE.search(line)
        if m_hash:
            match_kind, _raw, ref_rva, hash_value = m_hash.groups()
            if raw_name is None:
                raw_name = _raw
    return name, raw_name, ea, rva, match_kind, ref_rva, hash_value


def sibling_c_path(asm_path: Path) -> str | None:
    text = str(asm_path)
    if text.endswith(".asm.txt"):
        candidate = Path(text[:-8] + ".c")
        return str(candidate) if candidate.exists() else None
    return None


def collect_asm(root: Path, pe: PeImage, candidates: dict[str, dict[str, list[dict[str, Any]]]]) -> int:
    matched = 0
    for asm_path in root.rglob("*.asm.txt"):
        try:
            lines = asm_path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue

        name, raw_name, ea, rva, match_kind, ref_rva, hash_value = asm_header(lines)
        header_text = " ".join(x for x in (name, raw_name, str(asm_path)) if x)
        for key in matching_target_keys(header_text):
            matched += 1
            add_candidate(
                candidates,
                make_entry(
                    key=key,
                    source="decompile_asm",
                    name=name,
                    raw_name=raw_name,
                    ea=ea,
                    rva=rva,
                    asm=str(asm_path),
                    c=sibling_c_path(asm_path),
                    match_kind=match_kind,
                    ref_rva=ref_rva,
                    hash_value=hash_value,
                    note="asm function header",
                    pe=pe,
                ),
            )

        for line in lines:
            m_hash = HASH_RE.search(line)
            if m_hash:
                h_kind, h_raw, h_ref_rva, h_hash = m_hash.groups()
                m_addr = LINE_ADDR_RE.match(line)
                text = f"{line} {h_raw}"
                for key in matching_target_keys(text):
                    matched += 1
                    add_candidate(
                        candidates,
                        make_entry(
                            key=key,
                            source="decompile_asm",
                            name=name,
                            raw_name=h_raw,
                            ea=int(m_addr.group(1), 16) if m_addr else ea,
                            asm=str(asm_path),
                            c=sibling_c_path(asm_path),
                            match_kind=h_kind,
                            ref_rva=h_ref_rva,
                            hash_value=h_hash,
                            note="UE hash match",
                            pe=pe,
                        ),
                    )

            m_call = CALL_TARGET_RE.match(line)
            if not m_call:
                continue
            target_ea = int(m_call.group(1), 16)
            target_name = m_call.group(2).strip()
            for key in matching_target_keys(target_name):
                matched += 1
                add_candidate(
                    candidates,
                    make_entry(
                        key=key,
                        source="decompile_asm",
                        name=target_name,
                        ea=target_ea,
                        asm=str(asm_path),
                        note="call target reference",
                        pe=pe,
                    ),
                )
    return matched


def collapse(candidates: dict[str, dict[str, list[dict[str, Any]]]]) -> list[dict[str, Any]]:
    output: list[dict[str, Any]] = []
    for target in TARGETS:
        by_source = candidates.get(target["key"], {})
        entries = []
        counts = {}
        for source, source_entries in sorted(by_source.items()):
            counts[source] = len(source_entries)
            uniq: dict[tuple[str | None, str | None, str], dict[str, Any]] = {}
            for entry in source_entries:
                dedup_key = (entry.get("rva"), entry.get("ea"), entry.get("source", ""))
                old = uniq.get(dedup_key)
                if old is None or entry_score(entry) > entry_score(old):
                    uniq[dedup_key] = entry
            best = sorted(uniq.values(), key=entry_score, reverse=True)
            if best:
                entries.append(best[0])
        output.append({
            "key": target["key"],
            "needles": target["needles"],
            "raw_needles": target["raw_needles"],
            "candidate_counts": counts,
            "entries": sorted(entries, key=lambda e: (e["source"], e.get("rva", ""))),
        })
    return output


def collect_runtime_actions(source: Path) -> dict[str, Any]:
    defaults = {
        "executable_actions": [
            "skip",
            "skip_draw",
            "skip_dispatch",
            "color_override",
            "swap_cbv_left_to_right",
            "swap_descriptor_from_left",
            "force_srv_array_slice",
        ],
        "mutation_actions": [
            "swap_cbv_left_to_right",
            "swap_descriptor_from_left",
            "force_srv_array_slice",
        ],
        "probe_actions": ["color_override"],
        "unsupported_actions": [
            "duplicate_left_work_into_right_bucket",
            "replace_shader_from_left_permutation",
            "replace_ps_bytecode",
            "patch_cb_bytes",
        ],
    }
    try:
        text = source.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return {
            "source": str(source),
            "source_status": f"unreadable: {exc}",
            **defaults,
        }

    found: dict[str, list[str]] = {}
    for match in RUNTIME_ACTION_ARRAY_RE.finditer(text):
        found[match.group("name")] = RUNTIME_ACTION_STRING_RE.findall(match.group("body"))
    out = {**defaults, **found}
    out["source"] = str(source)
    out["source_status"] = "parsed" if found else "fallback_defaults"
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--symbols", type=Path, default=DEFAULT_SYMBOLS)
    parser.add_argument("--decomp-root", type=Path, default=DEFAULT_DECOMP_ROOT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--runtime-source", type=Path, default=DEFAULT_RUNTIME_SOURCE)
    args = parser.parse_args()

    pe = PeImage(args.exe)
    candidates: dict[str, dict[str, list[dict[str, Any]]]] = {}
    counts = {
        "binfold_symbols": collect_binfold_symbols(args.symbols, pe, candidates),
        "selected_functions": collect_selected_functions(args.decomp_root, pe, candidates),
        "decompile_asm": collect_asm(args.decomp_root, pe, candidates),
    }

    manifest = {
        "generated_at_utc": _dt.datetime.now(_dt.UTC).isoformat(timespec="seconds"),
        "inputs": {
            "exe": str(args.exe),
            "symbols": str(args.symbols),
            "decomp_root": str(args.decomp_root),
            "image_base": hex_or_none(pe.image_base),
        },
        "candidate_totals": counts,
        "stereo_forensics_runtime_actions": collect_runtime_actions(args.runtime_source),
        "targets": collapse(candidates),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    found = sum(1 for target in manifest["targets"] if target["entries"])
    print(f"wrote {args.output} ({found}/{len(TARGETS)} targets with entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
