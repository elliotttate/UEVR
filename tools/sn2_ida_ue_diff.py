#!/usr/bin/env python3
"""Side-by-side a shipping-binary function with its UE 5.6.1 source.

For a resolved code site (symbol or RVA), this:
  • locates the matching UE 5.6.1 source function (grep the engine tree for the
    `Class::Method(` definition and brace-match its body), and
  • loads the IDA decompile from a cache if present; otherwise prints the exact
    ida-pro-mcp / idat64 invocation to fetch it.

Answers "is this stock UE behavior under -emulatestereo, or a UWE/game
modification?" for a code site that sn2_symbolizer / sn2_trace pointed at.

ASSETS (env overrides):
  UEVR_UE_SOURCE        UE engine source root (default E:\\Epic Games\\UnrealEngine-5.6.1\\Engine\\Source)
  UEVR_SN2_DECOMPILES   dir of cached IDA decompiles named <rva>.txt or <Symbol>.txt
                        (default <moddingkit>/runs/decompiles)
  UEVR_SN2_I64          IDA database path (default E:\\Github\\Subnautica 2\\Subnautica2.exe.i64)

USAGE
    python sn2_ida_ue_diff.py "FDeferredShadingSceneRenderer::RenderSingleLayerWater"
    python sn2_ida_ue_diff.py 0x142EC70C0
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

try:
    from sn2_symbolizer import Symbolizer, light_demangle
except Exception:
    Symbolizer = None
    def light_demangle(x: str) -> str:  # type: ignore
        return x


def moddingkit() -> Path:
    return Path(os.environ.get("UEVR_SN2_MODDINGKIT", r"E:\Github\Subnautica 2\moddingkit"))


def ue_source_root() -> Path:
    v = os.environ.get("UEVR_UE_SOURCE")
    return Path(v) if v else Path(r"E:\Epic Games\UnrealEngine-5.6.1\Engine\Source")


def decompiles_dir() -> Path:
    v = os.environ.get("UEVR_SN2_DECOMPILES")
    return Path(v) if v else moddingkit() / "runs" / "decompiles"


def i64_path() -> str:
    return os.environ.get("UEVR_SN2_I64", r"E:\Github\Subnautica 2\Subnautica2.exe.i64")


def split_qualified(symbol: str) -> tuple[str | None, str]:
    """('FDeferredShadingSceneRenderer', 'RenderSingleLayerWater') from a qualified name."""
    name = light_demangle(symbol) if symbol.startswith("?") else symbol
    name = name.split("(")[0].strip()
    if "::" in name:
        cls, method = name.rsplit("::", 2)[-2:] if name.count("::") >= 1 else (None, name)
        parts = name.split("::")
        return parts[-2], parts[-1]
    return None, name


def locate_ue_source(cls: str | None, method: str, max_body_lines: int = 220) -> dict[str, Any]:
    root = ue_source_root()
    out: dict[str, Any] = {"ue_source_root": str(root), "available": root.exists(), "matches": []}
    if not root.exists():
        out["note"] = "UE source not found; set $UEVR_UE_SOURCE."
        return out

    needle = f"{cls}::{method}(" if cls else f"{method}("
    scopes = [root / "Runtime" / "Renderer", root / "Runtime", root]
    scopes = [s for s in scopes if s.exists()][:1] or [root]
    files: list[str] = []
    rg = shutil.which("rg")
    try:
        if rg:
            r = subprocess.run(["rg", "-l", "--no-messages", "-F", needle, "-g", "*.cpp", "-g", "*.h",
                                str(scopes[0])], capture_output=True, text=True, timeout=40)
            files = [ln for ln in r.stdout.splitlines() if ln.strip()][:6]
        else:
            for p in scopes[0].rglob("*.cpp"):
                try:
                    if needle in p.read_text(encoding="utf-8", errors="ignore"):
                        files.append(str(p))
                except Exception:
                    pass
                if len(files) >= 6:
                    break
    except Exception as e:
        out["error"] = str(e)
        return out

    for f in files:
        body = _extract_function_body(Path(f), needle, max_body_lines)
        if body:
            out["matches"].append(body)
    if not out["matches"] and files:
        out["matches"] = [{"file": f, "note": "definition not brace-matched; open manually"} for f in files]
    return out


def _extract_function_body(path: Path, needle: str, max_lines: int) -> dict[str, Any] | None:
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return None
    idx = text.find(needle)
    if idx < 0:
        return None
    # back up to the start of the line
    line_start = text.rfind("\n", 0, idx) + 1
    line_no = text.count("\n", 0, line_start) + 1
    # find the opening brace of the body
    brace = text.find("{", idx)
    if brace < 0:
        return {"file": str(path), "line": line_no, "snippet": text[line_start:idx + 200]}
    depth = 0
    end = brace
    for i in range(brace, min(len(text), brace + 200000)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    body = text[line_start:end]
    lines = body.splitlines()
    truncated = len(lines) > max_lines
    if truncated:
        body = "\n".join(lines[:max_lines]) + f"\n    // ... ({len(lines) - max_lines} more lines truncated)"
    return {"file": str(path), "line": line_no, "lines": len(lines), "truncated": truncated, "body": body}


def load_decompile(rva: int | None, symbol: str | None) -> dict[str, Any]:
    d = decompiles_dir()
    candidates = []
    if rva is not None:
        candidates += [d / f"{rva:x}.txt", d / f"0x{rva:x}.txt", d / f"{rva}.txt"]
    if symbol:
        safe = re.sub(r"[^A-Za-z0-9_]+", "_", light_demangle(symbol))[:120]
        candidates += [d / f"{safe}.txt"]
    for c in candidates:
        if c.exists():
            return {"cached": True, "file": str(c), "text": c.read_text(encoding="utf-8", errors="replace")[:20000]}
    # Not cached — emit how to fetch it.
    va = (0x140000000 + rva) if rva is not None else None
    return {
        "cached": False,
        "decompiles_dir": str(d),
        "how_to_fetch": {
            "ida_pro_mcp": f"mcp__ida-pro-mcp__decompile_function(address={hex(va) if va else '<VA>'})  # then save stdout to {d}\\{(f'{rva:x}.txt' if rva is not None else '<rva>.txt')}",
            "idat64": f'idat64 -A -S"decompile_one.py" "{i64_path()}"  # script: idaapi.decompile(ea).__str__() for ea={hex(va) if va else "<VA>"}',
        },
        "note": "IDA decompile not cached. Fetch via ida-pro-mcp (preferred) and drop the text in the decompiles dir to cache it for diffing.",
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", help="symbol (Class::Method or mangled) or VA/RVA")
    ap.add_argument("--rva", action="store_true", help="treat a numeric target as RVA not VA")
    ap.add_argument("--max-lines", type=int, default=220)
    args = ap.parse_args()

    rva: int | None = None
    symbol: str | None = None
    t = args.target.strip()
    if re.fullmatch(r"(0x)?[0-9a-fA-F]+", t) and (t.lower().startswith("0x") or t.isdigit() is False):
        # numeric -> resolve to a symbol via the symbolizer
        addr = int(t, 16) if t.lower().startswith("0x") else int(t)
        if Symbolizer:
            sym = Symbolizer()
            r = sym.resolve(addr, is_rva=args.rva)
            symbol = r.get("demangled") or r.get("symbol")
            rva = int(r["rva"], 16) if isinstance(r.get("rva"), str) else None
            resolved = r
        else:
            resolved = {"error": "symbolizer unavailable"}
    else:
        symbol = t
        resolved = {"symbol": symbol}

    cls, method = split_qualified(symbol) if symbol else (None, t)
    result = {
        "schema": "uevr.sn2.ida_ue_diff.v1",
        "target": t,
        "resolved": resolved,
        "class": cls,
        "method": method,
        "ue_source": locate_ue_source(cls, method, args.max_lines) if method else {},
        "ida_decompile": load_decompile(rva, symbol),
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
