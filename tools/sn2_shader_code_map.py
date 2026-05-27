#!/usr/bin/env python3
"""Map a shader CRC / friendly name to its semantic role and UE engine source.

Unifies the hand-built render_names dictionaries (sn2_render_dictionary.json,
pso3069_shader_semantics.json, overlay_chain_shader_semantics.json,
sn2_uwe_water_material_catalog.json) into one ps_crc / cs_name lookup, and
optionally locates the backing UE 5.6.1 source (FShader class .cpp + .usf) by
grepping the engine tree for the entry function / friendly-name keywords.

This is the static half of "is this shader a code issue, and where" — it turns
a captured ps_crc into {friendly name, DXIL semantics, material, candidate
.cpp/.usf source files}.

ASSETS (env overrides):
  UEVR_SN2_MODDINGKIT  moddingkit root (default E:\\Github\\Subnautica 2\\moddingkit)
  UEVR_SN2_RENDER_NAMES  render_names dir (default <moddingkit>/render_names)
  UEVR_UE_SOURCE  UE engine source root (default E:\\Epic Games\\UnrealEngine-5.6.1\\Engine\\Source)

USAGE
    python sn2_shader_code_map.py crc 0x166dba88 [--source]
    python sn2_shader_code_map.py name Nanite.RasterBinBuild [--source]
    python sn2_shader_code_map.py dump            # the whole merged index
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any


def moddingkit() -> Path:
    return Path(os.environ.get("UEVR_SN2_MODDINGKIT", r"E:\Github\Subnautica 2\moddingkit"))


def render_names_dir() -> Path:
    v = os.environ.get("UEVR_SN2_RENDER_NAMES")
    return Path(v) if v else moddingkit() / "render_names"


def artifacts_dir() -> Path:
    v = os.environ.get("UEVR_SN2_ARTIFACTS")
    return Path(v) if v else Path(__file__).resolve().parents[1] / "artifacts"


def ue_source_root() -> Path:
    v = os.environ.get("UEVR_UE_SOURCE")
    return Path(v) if v else Path(r"E:\Epic Games\UnrealEngine-5.6.1\Engine\Source")


def norm_crc(s: str) -> str:
    s = s.strip().lower()
    if s.startswith("0x"):
        s = s[2:]
    return s.zfill(8)


def _load(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except Exception:
        return None


KNOWN_SN2_SHADER_ROLES: dict[str, dict[str, Any]] = {
    "9d14fcf0": {
        "friendly_name": "VoxelizePS / IntegratedLightScattering volume writer",
        "pass": "VolumetricFog.SingleLayerWater.VolumeProducer",
        "keywords": ["VoxelizePS", "IntegratedLightScattering", "SingleLayerWater", "VolumetricFog"],
    },
    "89872979": {
        "friendly_name": "Foreground fog volume DrawInstanced writer",
        "pass": "ForegroundFog.VolumeProducer",
        "keywords": ["Fog", "VolumetricFog", "SingleLayerWater"],
    },
    "5e1e86ce": {
        "friendly_name": "Foreground fog volume DrawInstanced writer",
        "pass": "ForegroundFog.VolumeProducer",
        "keywords": ["Fog", "VolumetricFog", "SingleLayerWater"],
    },
    "de7c3822": {
        "friendly_name": "UWE SingleLayerWater foreground fog/water composite MainPS",
        "pass": "SingleLayerWater.Composite",
        "keywords": ["SingleLayerWater", "BasePass", "Fog"],
    },
    "32040a0d": {
        "friendly_name": "UWE SingleLayerWater foreground fog/water composite MainPS variant",
        "pass": "SingleLayerWater.Composite",
        "keywords": ["SingleLayerWater", "BasePass", "Fog"],
    },
    "b9be2499": {
        "friendly_name": "UWE SingleLayerWater foreground fog/water composite MainPS variant",
        "pass": "SingleLayerWater.Composite",
        "keywords": ["SingleLayerWater", "BasePass", "Fog"],
    },
}


def build_index() -> dict[str, Any]:
    """Return {'by_crc': {crc8: entry}, 'by_name': {name: entry}}."""
    rn = render_names_dir()
    art = artifacts_dir()
    by_crc: dict[str, dict[str, Any]] = {}
    by_name: dict[str, dict[str, Any]] = {}

    def ensure_crc(crc: str) -> dict[str, Any]:
        c = norm_crc(crc)
        return by_crc.setdefault(c, {"ps_crc": "0x" + c, "sources": []})

    rd = _load(rn / "sn2_render_dictionary.json")
    if isinstance(rd, dict):
        for crc, v in (rd.get("psos") or {}).items():
            base = crc.split("_")[0]
            e = ensure_crc(base)
            e.setdefault("psos", {})[crc] = v
        for crc, v in (rd.get("pso_semantics") or {}).items():
            ensure_crc(crc)["pso_semantics"] = v
        for name, v in (rd.get("compute_shaders") or {}).items():
            entry = by_name.setdefault(name, {"name": name, "sources": []})
            entry["compute_shader"] = v
            crc = v.get("cs_crc") or v.get("crc") if isinstance(v, dict) else None
            if crc:
                ensure_crc(crc)["compute_shader_name"] = name
        if rd.get("material_identity_evidence"):
            by_name["__material_identity_evidence__"] = rd["material_identity_evidence"]

    for fname, key in [("pso3069_shader_semantics.json", "dxil_semantics"),
                       ("overlay_chain_shader_semantics.json", "overlay_semantics")]:
        doc = _load(rn / fname)
        if isinstance(doc, dict):
            # Attach to whatever crc(s) the file references; also keep raw under a global.
            for crc in re.findall(r"\b([0-9a-fA-F]{8})\b", json.dumps(doc.get("ps_crc", "")) + " " + json.dumps(doc.get("crc", ""))):
                ensure_crc(crc)[key] = doc
            by_name.setdefault("__" + key + "__", doc)

    matcat = _load(rn / "sn2_uwe_water_material_catalog.json")
    if matcat is not None:
        by_name["__uwe_water_material_catalog__"] = matcat

    # Fallbacks generated by the live/offline forensics pipeline. These are less
    # semantic than the curated dictionaries, but they keep the bridge useful for
    # newly discovered CRCs instead of returning "unknown".
    graph = _load(art / "sn2_pso_rootsig_graph.json")
    if isinstance(graph, dict):
        families = graph.get("families") or []
        if isinstance(families, dict):
            family_iter = families.items()
        else:
            family_iter = enumerate(families)
        for family_id, fam in family_iter:
            if not isinstance(fam, dict):
                continue
            for member in fam.get("members") or []:
                if not isinstance(member, dict):
                    continue
                stages = member.get("stages") or {}
                ps = stages.get("ps") if isinstance(stages, dict) else None
                if not isinstance(ps, dict) or not ps.get("crc32"):
                    continue
                e = ensure_crc(str(ps["crc32"]))
                e.setdefault("pso_graph", []).append({
                    "family": family_id,
                    "root_sig": fam.get("root_sig"),
                    "pso": member.get("pso"),
                    "name": member.get("name"),
                    "stages": stages,
                })

    left_only = _load(art / "left_only_dxil" / "INDEX.json")
    if isinstance(left_only, list):
        for row in left_only:
            if not isinstance(row, dict) or not row.get("ps_crc"):
                continue
            e = ensure_crc(str(row["ps_crc"]))
            e.setdefault("left_only_dxil", []).append(row)

    for crc, role in KNOWN_SN2_SHADER_ROLES.items():
        e = ensure_crc(crc)
        e["known_role"] = role
        by_name.setdefault(role["friendly_name"], e)

    return {"by_crc": by_crc, "by_name": by_name}


# ── UE source location ──────────────────────────────────────────────────

# Friendly-name keyword -> likely engine source basenames (fast targeted hint).
KEYWORD_FILES = {
    "singlelayerwater": ["SingleLayerWaterRendering.cpp", "SingleLayerWaterShading.ush", "SingleLayerWaterCommon.ush"],
    "uwewater": ["SingleLayerWaterRendering.cpp"],
    "skyatmos": ["SkyAtmosphereRendering.cpp", "SkyAtmosphere.usf"],
    "volumetricfog": ["VolumetricFog.cpp", "VolumetricFog.usf", "VolumetricFogShared.ush"],
    "fog": ["VolumetricFog.cpp", "HeightFogCommon.ush", "FogRendering.cpp"],
    "nanite": ["NaniteCullRaster.cpp", "NaniteCull.usf", "NaniteRasterizer.usf"],
    "rasterbin": ["NaniteCullRaster.cpp"],
    "instancecull": ["InstanceCulling/InstanceCullingManager.cpp", "NaniteCullRaster.cpp"],
    "virtualshadowmap": ["VirtualShadowMapClipmap.cpp", "VirtualShadowMapCacheManager.cpp", "VirtualShadowMapProjection.usf"],
    "vsm": ["VirtualShadowMapProjection.usf"],
    "basepass": ["BasePassRendering.cpp", "BasePassPixelShader.usf", "BasePassVertexShader.usf"],
    "lightscattering": ["VolumetricFog.usf"],
    "copyrect": ["PostProcessing.cpp", "ScreenPass.cpp"],
}


def find_ue_source(keywords: list[str], entry_fn: str | None, max_hits: int = 12) -> dict[str, Any]:
    root = ue_source_root()
    out: dict[str, Any] = {"ue_source_root": str(root), "available": root.exists(),
                           "keyword_file_hints": [], "grep_hits": []}
    if not root.exists():
        out["note"] = "UE source root not found; set $UEVR_UE_SOURCE."
        return out

    # 1) Cheap keyword->basename hints.
    seen: set[str] = set()
    for kw in keywords:
        for low, files in KEYWORD_FILES.items():
            if low in kw.lower():
                for f in files:
                    if f not in seen:
                        seen.add(f)
                        out["keyword_file_hints"].append(f)

    # 2) ripgrep the entry function / class name across renderer + shaders.
    rg = shutil.which("rg")
    terms = [t for t in ([entry_fn] + keywords) if t and len(t) >= 4]
    scopes = [root / "Runtime" / "Renderer", root / "Shaders"]
    scopes = [s for s in scopes if s.exists()] or [root]
    for term in terms[:4]:
        try:
            if rg:
                cmd = ["rg", "-l", "--no-messages", "-g", "*.cpp", "-g", "*.h", "-g", "*.usf",
                       "-g", "*.ush", term, *[str(s) for s in scopes]]
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=25)
                hits = [ln for ln in r.stdout.splitlines() if ln.strip()][:max_hits]
            else:
                hits = []
                for s in scopes:
                    for ext in ("*.cpp", "*.usf", "*.ush", "*.h"):
                        for p in s.rglob(ext):
                            try:
                                if term in p.read_text(encoding="utf-8", errors="ignore"):
                                    hits.append(str(p))
                            except Exception:
                                pass
                            if len(hits) >= max_hits:
                                break
                        if len(hits) >= max_hits:
                            break
            if hits:
                out["grep_hits"].append({"term": term, "files": hits})
        except Exception as e:
            out.setdefault("errors", []).append(f"{term}: {e}")
    return out


def collect_keywords(entry: dict[str, Any]) -> list[str]:
    kws: list[str] = []
    role = entry.get("known_role")
    if isinstance(role, dict):
        kws += [str(x) for x in (role.get("keywords") or []) if x]
    blob = json.dumps(entry)
    for m in re.findall(r'"(?:name|friendly_name|pass|pass_tag|material|entry_function|shader)"\s*:\s*"([^"]+)"', blob):
        kws.append(m)
    # Also any dotted friendly names like Nanite.RasterBinBuild / UWEWater.BasePass.MainPS
    kws += re.findall(r"\b([A-Z][A-Za-z0-9]+(?:\.[A-Za-z0-9]+)+)\b", blob)
    return list(dict.fromkeys(kws))


def cmd_crc(args: argparse.Namespace) -> int:
    idx = build_index()
    c = norm_crc(args.crc)
    entry = idx["by_crc"].get(c)
    if entry is None:
        print(json.dumps({"ok": False, "ps_crc": "0x" + c, "error": "not in render dictionaries",
                          "hint": "shader may be uncatalogued; try sn2_shader_semantics.py on its .dxbc"}, indent=2))
        return 0
    result = {"ok": True, "entry": entry}
    if args.source:
        kws = collect_keywords(entry)
        entry_fn = None
        for v in (entry.get("psos") or {}).values():
            if isinstance(v, dict) and v.get("entry_function"):
                entry_fn = v["entry_function"]
        result["ue_source"] = find_ue_source(kws, entry_fn)
    print(json.dumps(result, indent=2))
    return 0


def cmd_name(args: argparse.Namespace) -> int:
    idx = build_index()
    entry = idx["by_name"].get(args.name)
    if entry is None:
        print(json.dumps({"ok": False, "name": args.name, "error": "not found",
                          "known": [k for k in idx["by_name"] if not k.startswith("__")]}, indent=2))
        return 0
    result = {"ok": True, "entry": entry}
    if args.source:
        result["ue_source"] = find_ue_source(collect_keywords(entry) + [args.name], None)
    print(json.dumps(result, indent=2))
    return 0


def cmd_dump(args: argparse.Namespace) -> int:
    idx = build_index()
    print(json.dumps({
        "ok": True,
        "crc_count": len(idx["by_crc"]),
        "name_count": len([k for k in idx["by_name"] if not k.startswith("__")]),
        "crcs": sorted(idx["by_crc"].keys()),
        "names": sorted(k for k in idx["by_name"] if not k.startswith("__")),
    }, indent=2))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("crc"); p.add_argument("crc"); p.add_argument("--source", action="store_true"); p.set_defaults(func=cmd_crc)
    p = sub.add_parser("name"); p.add_argument("name"); p.add_argument("--source", action="store_true"); p.set_defaults(func=cmd_name)
    p = sub.add_parser("dump"); p.set_defaults(func=cmd_dump)
    args = ap.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
