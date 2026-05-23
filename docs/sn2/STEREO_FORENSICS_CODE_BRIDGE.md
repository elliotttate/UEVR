# Stereo Forensics — Code Bridge (symptom → cause)

The StereoForensics layer answers **what** differs between the eyes at the D3D12
level (a dispatch missing on the right eye, a descriptor pointing at a different
slice, a CB whose contents diverge). This **code bridge** answers the next
question: **why — and where in the engine/game code.** It ties a captured D3D12
event back through the UE5 render graph to the engine function that issued (or
failed to issue) the work, and to its UE 5.6.1 source / IDA decompile.

> "PS `DE7C3822` missing on the right eye"  →  "issued from
> `FDeferredShadingSceneRenderer::RenderSingleLayerWater` (`SingleLayerWaterRendering.cpp:1676`);
> the per-view loop ran it once, not twice."

This document covers the runtime capture, the four offline analysis tools, the
MCP surface, the assets/config, and the end-to-end workflow.

---

## 1. Architecture

Four layers, joined by stable keys (event index, RVA, shader CRC, resource):

```
 D3D12 forensics            code provenance
 ────────────────           ───────────────────────────────────────────────
 events.jsonl  ── ps/cs_crc ─────────►  sn2_shader_code_map  ─► UE FShader / .usf/.cpp
   (work events)             issuer_stack[] ─► sn2_symbolizer ─► engine function (+source/role)
 eye_diff.json ── issue ───┐
 lineage.json  ── producer ─┴──────────►  sn2_trace  ─► ranked root-cause hypothesis (file:line)
                                                        └─► sn2_ida_ue_diff ─► stock UE vs UWE-modified?
```

- **Capture (runtime, in UEVRBackend):** `Sn2IssuerStack` attaches a CPU
  callstack to each work event.
- **Symbolize (offline):** `sn2_symbolizer` resolves stack addresses to engine
  functions using the binfold symbol dump + the curated RVA dictionary.
- **Attribute (offline):** `sn2_shader_code_map` maps shader CRCs to UE shader
  classes / source; `sn2_ida_ue_diff` shows the binary function vs UE source.
- **Join (offline):** `sn2_trace` walks an eye_diff issue → events → symbolized
  issuer → shader/source → producer lineage → a hypothesis with a code site.

Everything is exposed through the UEVR MCP (see §6).

---

## 2. Runtime stack capture — `Sn2IssuerStack.hpp`

`src/render/Sn2IssuerStack.hpp` adds, to each StereoForensics **work** event, the
CPU return-address callstack at the D3D12 record site:

```jsonc
// an events.jsonl work event, with UEVR_STEREO_FORENSICS_STACKS=1
{ "event_class": "work", "kind": "dispatch", "cs_crc_hex": "0x...",
  "module_base": 5368709120,            // base of the game EXE at runtime
  "issuer_stack": [21474836... , ...] } // raw return-address VAs, outer-most last
```

- **Enable:** `set UEVR_STEREO_FORENSICS_STACKS=1` before injection (alongside
  `UEVR_STEREO_FORENSICS=1`).
- **Cost / safety:** only runs on *captured* frames (the limiter already gates
  these), `RtlCaptureStackBackTrace` is cheap, and `record_*` is exception-wrapped
  (`STEREO_FORENSICS_TRY`). Disabled by default — zero cost when off.
- **ASLR:** addresses are raw runtime VAs plus `module_base`. The symbolizer
  subtracts `module_base` to get an RVA, which is base-independent, so ASLR never
  matters.

It is wired with a single line in `StereoForensics::record_draw_or_dispatch`
(`sn2_issuer_stack::attach(event)` just before `push_event_locked`).

---

## 3. `sn2_symbolizer.py` — address → engine function (the keystone)

Resolves a runtime VA / RVA (e.g. a frame from `issuer_stack`) to the nearest
engine function.

```bash
python tools/sn2_symbolizer.py build-cache          # one-time 55MB parse -> sorted index cache
python tools/sn2_symbolizer.py resolve 0x142EC70C0  # a VA
python tools/sn2_symbolizer.py resolve 0x2EC70C0 --rva
python tools/sn2_symbolizer.py stack 0x142EC70C0,0x1432A2C20 --base 0x140000000
```

- Loads binfold `symbols.json` (~220k UE syms) and the curated
  `sn2_ida_rva_dictionary.json`. A sorted index is cached next to `symbols.json`
  (`symbols.symidx.pkl`) and rebuilt only when `symbols.json` changes.
- Returns: `{symbol (mangled), demangled, sym_rva, offset, via}` and — when the
  curated dictionary covers the address — `{role, source, confidence}`.
- `demangled` is a best-effort MSVC → `Namespace::Class::method` head (enough to
  read; template args dropped).
- Curated dictionary hits (`via: rva_dict`) carry a UE source hint + a prose role
  + confidence; binfold hits (`via: binfold`) carry the symbol + offset, enriched
  with curated source/role when the address falls inside a curated function.

Importable: `from sn2_symbolizer import Symbolizer`.

---

## 4. `sn2_shader_code_map.py` — shader CRC → UE source

Unifies the hand-built `render_names` dictionaries into one `ps_crc` / `cs_name`
lookup and (optionally) locates the backing UE source.

```bash
python tools/sn2_shader_code_map.py dump                       # the merged index
python tools/sn2_shader_code_map.py crc 0x166dba88 --source    # PSO + UE .cpp/.usf
python tools/sn2_shader_code_map.py name Nanite.RasterBinBuild --source
```

- Merges `sn2_render_dictionary.json` (`psos`, `pso_semantics`,
  `compute_shaders`), `pso3069_shader_semantics.json`,
  `overlay_chain_shader_semantics.json`, `sn2_uwe_water_material_catalog.json`.
- `--source` adds keyword→file hints (e.g. SingleLayerWater →
  `SingleLayerWaterRendering.cpp`) plus a targeted ripgrep of the UE renderer +
  shader trees for the entry function / friendly-name keywords.
- Covers the per-eye-missing compute shaders by name: `Nanite.RasterBinBuild`,
  `Nanite.InstanceCull`, `Nanite.MainIndirectDispatchCS`,
  `VSM.VirtualShadowMapProjectionCS`, `VSM.MergeStaticPhysicalPagesIndirectCS`,
  `VolumetricFog.Compose`, `ComputeVolumetricFog.Producer`.

---

## 5. `sn2_ida_ue_diff.py` — binary function vs UE source

For a code site, shows the UE 5.6.1 source function next to the IDA decompile —
the "stock UE under `-emulatestereo`, or UWE/game modification?" check.

```bash
python tools/sn2_ida_ue_diff.py "FDeferredShadingSceneRenderer::RenderSingleLayerWater"
python tools/sn2_ida_ue_diff.py 0x142EC70C0
```

- **UE side (always):** extracts the `Class::Method` definition body from the UE
  source (brace-matched), with file + line.
- **IDA side:** loads a cached decompile from `$UEVR_SN2_DECOMPILES` (`<rva>.txt`
  or `<Symbol>.txt`) if present; otherwise prints the exact `ida-pro-mcp`
  (`decompile_function`) and `idat64` invocations to fetch it, and where to drop
  the result so it caches for next time.

---

## 6. `sn2_trace.py` — the capstone (symptom → cause)

Joins everything for the eye_diff issues in a session and emits a ranked
root-cause hypothesis per issue.

```bash
python tools/sn2_trace.py                              # latest session, top issues
python tools/sn2_trace.py --session <dir> --issue 0    # one issue, deep
python tools/sn2_trace.py --kind eye_event_count_mismatch
```

For each issue it produces:

- `symptom` — what the per-eye divergence means.
- `shaders` — the left/right shader names (via the code map).
- `code_sites` — the symbolized issuer callstack per eye (the engine function
  that issued the work), with source/role when curated. **Populated only when the
  capture ran with `UEVR_STEREO_FORENSICS_STACKS=1`.**
- `producer` — the lineage producer of the involved resource.
- `hypothesis` — a one-line conclusion naming the code site, or, without stacks,
  the shader + a prompt to enable stack capture.

Issues are ranked code-site-resolved + by severity. Without stacks it still maps
shaders and symptoms and tells you to enable stack capture for the exact site.

---

## 7. MCP surface (`uevr-mcp`)

Exposed as MCP tools in `mcp-server/StereoForensicsTools.cs` (shell out to the
tools above; no game required for the offline ones):

| Tool | Wraps |
|---|---|
| `uevr_forensics_symbolize` | `sn2_symbolizer.py stack` |
| `uevr_forensics_shader_code` | `sn2_shader_code_map.py crc|name` |
| `uevr_forensics_ida_ue_diff` | `sn2_ida_ue_diff.py` |
| `uevr_forensics_trace` | `sn2_trace.py` |

These join the existing forensics tools (`uevr_forensics_eye_diff`,
`uevr_forensics_lineage`, `uevr_forensics_suspects`, …) and the live-capture
tools (`uevr_render_stereo_forensics[_arm]`).

---

## 8. Assets & configuration (env)

| Env | Default | Used by |
|---|---|---|
| `UEVR_STEREO_FORENSICS_STACKS` | (off) | runtime: capture issuer_stack on work events |
| `UEVR_SN2_MODDINGKIT` | `E:\Github\Subnautica 2\moddingkit` | asset root |
| `UEVR_SN2_SYMBOLS` | `<moddingkit>/runs/binfold_scratch/symbols.json` | symbolizer |
| `UEVR_SN2_RVA_DICT` | `<moddingkit>/render_names/sn2_ida_rva_dictionary.json` | symbolizer |
| `UEVR_SN2_IMAGE_BASE` | `0x140000000` | symbolizer (dictionary image base) |
| `UEVR_SN2_RENDER_NAMES` | `<moddingkit>/render_names` | shader code map |
| `UEVR_UE_SOURCE` | `E:\Epic Games\UnrealEngine-5.6.1\Engine\Source` | shader map, ida_ue_diff |
| `UEVR_SN2_DECOMPILES` | `<moddingkit>/runs/decompiles` | ida_ue_diff decompile cache |
| `UEVR_SN2_I64` | `E:\Github\Subnautica 2\Subnautica2.exe.i64` | ida_ue_diff fetch hint |
| `UEVR_STEREO_FORENSICS_DIR` | `C:\tmp\uevr_forensics` | trace (locate latest session) |

---

## 9. End-to-end workflow

1. **Capture in the problem scene** with stacks on:
   `UEVR_STEREO_FORENSICS=1`, `UEVR_STEREO_FORENSICS_STACKS=1`, command-list hooks
   on, then `uevr_render_stereo_forensics_arm` while in-water.
2. **See what differs:** `uevr_forensics_eye_diff` (or `sn2_trace` directly).
3. **Trace to code:** `uevr_forensics_trace` → ranked hypotheses with engine
   functions + source files.
4. **Confirm the cause:** `uevr_forensics_ida_ue_diff <function>` → stock UE vs
   UWE-modified.
5. **Cross-check the shader:** `uevr_forensics_shader_code <crc> --source`.

---

## 10. Status & next steps

- **Tested offline:** symbolizer (220,820 syms; exact resolution),
  shader_code_map (9 PS + 8 CS, UE source located), ida_ue_diff (UE body
  extracted), trace (fixture issue diagnosed, graceful degrade).
- **Built, runtime-pending:** `Sn2IssuerStack` capture (UEVRBackend rebuilt
  clean; needs one live capture with `UEVR_STEREO_FORENSICS_STACKS=1` to confirm
  `code_sites` populate with real engine frames).
- **Deferred — dedicated per-view RDG ledger:** "which RDG pass ran for view 0
  not view 1, and the code site" is substantially answered today by issuer stacks
  + `sn2_trace` (group/diff work events by symbolized issuer per eye). A dedicated
  `FRDGBuilder::AddPass` ledger needs that RVA wired into `Sn2RDGPassHook` + live
  validation (the `enter_ue_view_scope` bracket already fires from
  `FFakeStereoRenderingHook`). Build it after a live capture shows where the
  stack-based approach falls short, so it isn't speculative RVA work.

## 11. Maintenance

The symbol/RVA mapping is only valid for the binary it was built from. When the
shipping `Subnautica2-Win64-Shipping.exe` changes:

- Rebuild binfold `symbols.json`, then `python tools/sn2_symbolizer.py build-cache`.
- Re-run the RVA-drift verifier (`moddingkit/runs/sn2_verify_rva_dictionary.py`)
  to flag stale `sn2_ida_rva_dictionary.json` entries (it re-decompiles each RVA
  and fingerprints it).
