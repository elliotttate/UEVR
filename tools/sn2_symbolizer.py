#!/usr/bin/env python3
"""Resolve runtime addresses / RVAs to engine symbols (the symptom->cause keystone).

Loads the binfold symbol dump (220k UE5 symbols) + the curated SN2 IDA RVA
dictionary and resolves an arbitrary address/RVA (e.g. a frame from a
StereoForensics issuer_stack) to the nearest function symbol, with offset, and
— when the curated dictionary covers it — a role/source/confidence.

This is what turns "PS DE7C3822 missing on right" into
"issued from FDeferredShadingSceneRenderer::RenderSingleLayerWater+0x1c4".

ASSETS (override via env):
  UEVR_SN2_SYMBOLS    binfold symbols.json  (default <moddingkit>/runs/binfold_scratch/symbols.json)
  UEVR_SN2_RVA_DICT   curated RVA dict      (default <moddingkit>/render_names/sn2_ida_rva_dictionary.json)
  UEVR_SN2_MODDINGKIT moddingkit root       (default E:\\Github\\Subnautica 2\\moddingkit)
  UEVR_SN2_IMAGE_BASE default image base VA (default 0x140000000)

A sorted index is cached next to symbols.json (symbols.symidx.pkl) and rebuilt
when symbols.json changes, so only the first resolve pays the 55MB parse.

USAGE
    python sn2_symbolizer.py resolve 0x142EC70C0
    python sn2_symbolizer.py resolve 0x2EC70C0 --rva
    python sn2_symbolizer.py stack 0x142EC70C0,0x1432A2C20 [--base 0x140000000]
    python sn2_symbolizer.py build-cache
"""
from __future__ import annotations

import argparse
import bisect
import json
import os
import pickle
import sys
from pathlib import Path
from typing import Any


def moddingkit() -> Path:
    return Path(os.environ.get("UEVR_SN2_MODDINGKIT", r"E:\Github\Subnautica 2\moddingkit"))


def symbols_path() -> Path:
    v = os.environ.get("UEVR_SN2_SYMBOLS")
    return Path(v) if v else moddingkit() / "runs" / "binfold_scratch" / "symbols.json"


def rva_dict_path() -> Path:
    v = os.environ.get("UEVR_SN2_RVA_DICT")
    return Path(v) if v else moddingkit() / "render_names" / "sn2_ida_rva_dictionary.json"


def image_base() -> int:
    return int(os.environ.get("UEVR_SN2_IMAGE_BASE", "0x140000000"), 0)


def _to_int(x: Any) -> int | None:
    if isinstance(x, int):
        return x
    if isinstance(x, str):
        s = x.strip()
        try:
            return int(s, 16) if s.lower().startswith("0x") else int(s, 0)
        except ValueError:
            return None
    return None


def light_demangle(name: str) -> str:
    """Best-effort MSVC name -> readable Namespace::Class::method.

    Not a full demangler; extracts the qualified-name head, which is enough to
    read engine call sites (templates/args are dropped)."""
    if not name or name[0] != "?":
        return name
    body = name[1:]
    qual = body.split("@@", 1)[0]
    parts = [p for p in qual.split("@") if p and not p.startswith("?$")]
    if not parts:
        return name
    return "::".join(reversed(parts))


class Symbolizer:
    def __init__(self) -> None:
        self._rvas: list[int] = []
        self._names: list[str] = []
        self._dict: dict[int, dict[str, Any]] = {}
        self._loaded = False

    # ── curated RVA dictionary ──────────────────────────────────────────
    def _load_dict(self) -> None:
        p = rva_dict_path()
        if not p.exists():
            return
        try:
            doc = json.loads(p.read_text(encoding="utf-8", errors="replace"))
        except Exception:
            return

        def add_entry(rva: int, node: dict[str, Any]) -> None:
            self._dict[rva] = {
                "name": node.get("name") or node.get("short_name") or node.get("symbol"),
                "role": node.get("role") or node.get("description"),
                "source": node.get("source") or node.get("source_file")
                          or node.get("ue_source") or node.get("ue5_source_hint"),
                "confidence": node.get("confidence"),
                "size": _to_int(node.get("size_bytes")),
                "notes": node.get("notes") or node.get("note"),
            }

        # Primary shape: a "functions" dict keyed by RVA-string -> {name, ...}.
        funcs = doc.get("functions") if isinstance(doc, dict) else None
        if isinstance(funcs, dict):
            for k, v in funcs.items():
                rva = _to_int(k)
                if rva is not None and isinstance(v, dict):
                    add_entry(rva, v)

        # Fallback: also collect any dict anywhere carrying an explicit rva field.
        def walk(node: Any) -> None:
            if isinstance(node, dict):
                rva = _to_int(node.get("rva") or node.get("RVA"))
                if rva is not None and ("name" in node or "symbol" in node or "role" in node):
                    add_entry(rva, node)
                for v in node.values():
                    walk(v)
            elif isinstance(node, list):
                for v in node:
                    walk(v)
        walk(doc)

    # ── binfold symbol index (cached) ───────────────────────────────────
    def _cache_path(self) -> Path:
        return symbols_path().with_suffix(".symidx.pkl")

    def _build_index(self) -> None:
        sp = symbols_path()
        if not sp.exists():
            raise FileNotFoundError(f"symbols not found: {sp} (set $UEVR_SN2_SYMBOLS)")
        data = json.loads(sp.read_text(encoding="utf-8", errors="replace"))
        pairs: list[tuple[int, str]] = []
        for e in data:
            rva = _to_int(e.get("rva"))
            if rva is None:
                ea = _to_int(e.get("ea"))
                rva = (ea - image_base()) if ea is not None else None
            if rva is None:
                continue
            name = e.get("name") or ""
            pairs.append((rva, name))
        pairs.sort(key=lambda t: t[0])
        self._rvas = [p[0] for p in pairs]
        self._names = [p[1] for p in pairs]
        try:
            with self._cache_path().open("wb") as f:
                pickle.dump({"mtime": sp.stat().st_mtime, "size": sp.stat().st_size,
                             "rvas": self._rvas, "names": self._names}, f, protocol=pickle.HIGHEST_PROTOCOL)
        except Exception:
            pass

    def load(self, rebuild: bool = False) -> None:
        if self._loaded and not rebuild:
            return
        self._load_dict()
        sp = symbols_path()
        cache = self._cache_path()
        if not rebuild and cache.exists() and sp.exists():
            try:
                with cache.open("rb") as f:
                    c = pickle.load(f)
                if c.get("mtime") == sp.stat().st_mtime and c.get("size") == sp.stat().st_size:
                    self._rvas = c["rvas"]
                    self._names = c["names"]
                    self._loaded = True
                    return
            except Exception:
                pass
        self._build_index()
        self._loaded = True

    # ── resolution ──────────────────────────────────────────────────────
    def resolve_rva(self, rva: int) -> dict[str, Any]:
        self.load()
        out: dict[str, Any] = {"rva": hex(rva), "va": hex(image_base() + rva)}

        # Exact curated-dictionary hit wins (carries role/source/confidence).
        if rva in self._dict:
            d = self._dict[rva]
            out.update({
                "symbol": d.get("name"),
                "demangled": light_demangle(d.get("name") or ""),
                "offset": 0,
                "via": "rva_dict",
                "role": d.get("role"),
                "source": d.get("source"),
                "confidence": d.get("confidence"),
            })
            return out

        if not self._rvas:
            out["symbol"] = None
            out["via"] = "no_symbols"
            return out

        i = bisect.bisect_right(self._rvas, rva) - 1
        if i < 0:
            out["symbol"] = None
            out["via"] = "below_first_symbol"
            return out
        sym_rva = self._rvas[i]
        name = self._names[i]
        out.update({
            "symbol": name,
            "demangled": light_demangle(name),
            "sym_rva": hex(sym_rva),
            "offset": rva - sym_rva,
            "via": "binfold",
        })
        # Enrich with a curated entry if the nearest dict fn covers this addr.
        # (Best-effort: nearest dict rva <= query.)
        if self._dict:
            keys = sorted(self._dict)
            j = bisect.bisect_right(keys, rva) - 1
            if j >= 0:
                d_rva = keys[j]
                d = self._dict[d_rva]
                size = d.get("size")
                within = (size is None and d_rva >= sym_rva) or (size is not None and rva < d_rva + size)
                if within:
                    out["role"] = d.get("role")
                    out["source"] = d.get("source")
                    out["confidence"] = d.get("confidence")
                    out["curated_name"] = d.get("name")
        return out

    def resolve(self, addr: int, is_rva: bool = False) -> dict[str, Any]:
        base = image_base()
        rva = addr if (is_rva or addr < base) else (addr - base)
        return self.resolve_rva(rva)

    def resolve_stack(self, addrs: list[int], base: int | None = None) -> list[dict[str, Any]]:
        out = []
        for a in addrs:
            rva = a if base is None else (a - base if a >= base else a)
            out.append(self.resolve_rva(rva if base is not None else (a - image_base() if a >= image_base() else a)))
        return out


def _parse_addr(s: str) -> int:
    v = _to_int(s)
    if v is None:
        raise SystemExit(f"bad address: {s}")
    return v


def cmd_resolve(args: argparse.Namespace) -> int:
    sym = Symbolizer()
    r = sym.resolve(_parse_addr(args.addr), is_rva=args.rva)
    print(json.dumps(r, indent=2))
    return 0


def cmd_stack(args: argparse.Namespace) -> int:
    sym = Symbolizer()
    addrs = [_parse_addr(x) for x in args.addrs.split(",") if x.strip()]
    base = _parse_addr(args.base) if args.base else None
    frames = sym.resolve_stack(addrs, base=base)
    print(json.dumps({"schema": "uevr.sn2.symbolized_stack.v1", "frames": frames}, indent=2))
    return 0


def cmd_build_cache(args: argparse.Namespace) -> int:
    sym = Symbolizer()
    sym.load(rebuild=True)
    print(json.dumps({"ok": True, "symbols": len(sym._rvas), "rva_dict_entries": len(sym._dict),
                      "cache": str(sym._cache_path())}, indent=2))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("resolve", help="resolve one address/RVA")
    p.add_argument("addr")
    p.add_argument("--rva", action="store_true", help="treat addr as an RVA, not a VA")
    p.set_defaults(func=cmd_resolve)

    p = sub.add_parser("stack", help="resolve a comma-separated list of addresses")
    p.add_argument("addrs")
    p.add_argument("--base", help="actual loaded module base (for ASLR'd absolute addrs)")
    p.set_defaults(func=cmd_stack)

    p = sub.add_parser("build-cache", help="(re)build the sorted symbol index cache")
    p.set_defaults(func=cmd_build_cache)

    args = ap.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
