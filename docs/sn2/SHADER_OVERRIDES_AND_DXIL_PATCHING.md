# UEVR Shader Overrides And DXIL Patching

Date: 2026-05-26

> **Post-dye correction:** the shader override system remains important and proven, but the specific
> SN2 store-coordinate fix described below is no longer the current root fix. A later dye ladder
> showed the visible right-eye composite reads the original eye-local half; the real bug is
> CPU-side per-view underwater state/relevance. Keep this document as the UEVR shader override
> reference and use it for dye/probe patches.

This document exists because the shader replacement system was missed during the Subnautica 2 right-eye fog investigation. It should be treated as a first option whenever the bug is a hardcoded shader operation. Do not jump straight to RDG texture copies, descriptor heap swaps, or frame-internal resource mutation before checking this path.

The short version: UEVR can replace D3D12 shaders at PSO level. It supports compute shaders. It supports CRC32 matching. It supports DXIL text patches and bytecode replacement. That is exactly the kind of tool needed for hardcoded shader bugs, and it was useful for the SN2 resolve dye ladder even though the store-coordinate theory is now superseded.

---

## Historical SN2 Use Case

Historical target:

```text
shader:      UWEFogResolveCS
stage:       cs
crc32:       0x0930dd4e
profile:     cs_6_0
bug:         stores UAV output at raw eye-local DispatchThreadID.xy
desired:     store.x = DispatchThreadID.x + ViewRectMin.x
```

The copy-based fix tries to shift the resolved fog texture after the fact. That path repeatedly crashed inside the NVIDIA driver because it mutates RDG-owned transient resources outside UE's resource graph. A shader patch avoids that entire class: it changes the existing store coordinate inside the existing compute dispatch.

Expected patch source:

```text
ViewRectMin.x is likely in the already-bound View UB at reg148.x / byte 0x940.
Left dispatch:  reg148.x = 0
Right dispatch: reg148.x = right-eye SBS origin, e.g. 640 or 712
```

If the capture confirms that field for the resolve dispatch, use one shader variant for both eyes:

```text
left:  DispatchThreadID.x + 0
right: DispatchThreadID.x + ViewRectMin.x
```

This avoids per-eye PSO selection for a compute pass, where viewport eye bucketing may be unreliable.

---

## Code Map

| File | Role |
| --- | --- |
| `src/render/ShaderOverrideRegistry.hpp` | Public registry types and APIs. |
| `src/render/ShaderOverrideRegistry.cpp` | Manifest parsing, directory scanning, DXIL patch build/cache, replacement PSO creation. |
| `src/hooks/D3D12Hook.cpp` | Hooks D3D12 PSO creation and applies replacement/per-eye PSOs. |
| `tools/dxil-patch/DxilPatch.cpp` | Standalone DXIL/container patch tool. |
| `tools/dxil-patch/templates/README.md` | Template transform workflow. |
| `src/hooks/SN2_PATCH_PLAN.md` | Older SN2 patch plan. Useful context, but it predates the current resolve-store conclusion. |

Useful anchors:

- `ShaderOverrideRegistry.cpp:134`: stage strings (`vs`, `ps`, `gs`, `cs`, `as`, `ms`).
- `ShaderOverrideRegistry.cpp:193`: stage parser accepts short and long names.
- `ShaderOverrideRegistry.cpp:307`: default D3D12 profiles (`cs` -> `cs_6_0`).
- `ShaderOverrideRegistry.cpp:336`: source kind names.
- `ShaderOverrideRegistry.cpp:2805`: shader override manifest parser.
- `ShaderOverrideRegistry.cpp:3430`: `dxil_text_patch` build/cache path.
- `ShaderOverrideRegistry.cpp:3924`: CRC32 lookup path for 8-hex `target_hash`.
- `ShaderOverrideRegistry.cpp:4551`: global override directory.
- `ShaderOverrideRegistry.cpp:4555`: profile override directory.
- `D3D12Hook.cpp:4517`: `CreateGraphicsPipelineState` hook.
- `D3D12Hook.cpp:4636`: `CreateComputePipelineState` hook.
- `D3D12Hook.cpp:10615`: per-eye PSO variant application.

---

## Override Directories

The registry scans:

```text
global:  Framework::get_persistent_dir().parent_path() / "shader_overrides"
profile: Framework::get_persistent_dir("shader_overrides")
```

For SN2, use the profile-specific directory unless you deliberately want a global override:

```text
C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\shader_overrides
```

The scanner is recursive and reads `.json` manifests. It skips `cache` directories.

Patch outputs are cached under the profile directory:

```text
shader_overrides\cache\dxil_text_patch
shader_overrides\cache\container_patch
shader_overrides\cache\dxil_patch
shader_overrides\cache\dxil_transform
shader_overrides\cache\dxil_semantic_transform
```

---

## Supported Stages

Use either short or long names:

| Stage | Aliases | D3D12 default profile |
| --- | --- | --- |
| Vertex | `vs`, `vertex` | `vs_6_0` |
| Pixel | `ps`, `pixel` | `ps_6_0` |
| Geometry | `gs`, `geometry` | `gs_6_0` |
| Compute | `cs`, `compute` | `cs_6_0` |
| Amplification | `as`, `amplification` | `as_6_5` |
| Mesh | `ms`, `mesh` | `ms_6_5` |

For `UWEFogResolveCS`:

```json
"stage": "cs",
"profile": "cs_6_0"
```

---

## Matching By CRC Or Full Hash

`target_hash` can be:

- the full normalized shader hash, or
- an 8-hex CRC32.

This means Shader Hunter or PSO bytecode dumper CRCs are enough to create an override manifest.

Example:

```json
"target_hash": "0930dd4e"
```

matches a compute shader whose `d3d12_pso_compute_crc32()` is `0x0930dd4e`.

Make sure `stage` matches the CRC source. A pixel CRC in a `cs` manifest will not hit.

---

## Manifest Source Types

The manifest parser requires exactly one top-level source payload unless it is a per-eye payload-only manifest.

| Payload key | Internal source kind | What it does |
| --- | --- | --- |
| `source` | `hlsl` | Compile replacement HLSL. |
| `bytecode` | `bytecode` | Load a complete replacement DXBC/DXIL container. |
| `patch` or `dxil_patch` | `dxil_patch` | Run the external DXIL patch tool. |
| `dxil_text_patch` or `dxil_ir_patch` | `dxil_text_patch` | Disassemble original DXIL, text-replace, reassemble, validate, sign. |
| `container_patch` or `container_edits` | `container_patch` | Edit DXBC container parts by FourCC. |
| `dxil_transform` or `dxil_stereo_transform` | `dxil_transform` | Run transform JSON/tool flow. |
| `dxil_semantic_transform`, `dxil_module_transform`, or `semantic_transform` | `dxil_semantic_transform` | Run semantic/module transform flow. |

For a one-coordinate store fix, prefer `dxil_text_patch` first. It preserves the original shader and root signature, and edits only the IR snippet that needs to change.

---

## Minimal HLSL Override

Use this when a full replacement shader is easy and compatible with the original root signature.

```json
{
  "backend": "dx12",
  "stage": "cs",
  "target_hash": "0930dd4e",
  "name": "sn2_uwe_fog_resolve_hlsl_test",
  "enabled": false,
  "entry_point": "main",
  "profile": "cs_6_0",
  "compiler": "dxc",
  "source": "main.hlsl"
}
```

For the SN2 resolve bug, this is probably too broad. Use DXIL text patch unless a full HLSL reconstruction is already available.

---

## Raw Bytecode Override

Use this when an external tool has already produced the final patched container.

```json
{
  "backend": "dx12",
  "stage": "cs",
  "target_hash": "0930dd4e",
  "name": "sn2_uwe_fog_resolve_patched_bytecode",
  "enabled": true,
  "bytecode": "UWEFogResolveCS_store_offset.dxbc"
}
```

The bytecode must already be valid and compatible with the original PSO/root signature.

---

## DXIL Text Patch Override

Recommended starting point for `UWEFogResolveCS`.

Manifest:

```json
{
  "backend": "dx12",
  "stage": "cs",
  "target_hash": "0930dd4e",
  "name": "sn2_uwe_fog_resolve_store_x_viewrectmin",
  "enabled": false,
  "entry_point": "UWEFogResolveCS",
  "profile": "cs_6_0",
  "dxil_text_patch": "resolve_store_x_viewrectmin.patch.json"
}
```

Patch file:

```json
{
  "replacements": [
    {
      "find": "EXACT_ORIGINAL_DXIL_TEXT",
      "replace": "EXACT_PATCHED_DXIL_TEXT"
    }
  ]
}
```

Inline form is also accepted:

```json
{
  "backend": "dx12",
  "stage": "cs",
  "target_hash": "0930dd4e",
  "name": "sn2_inline_text_patch",
  "enabled": false,
  "profile": "cs_6_0",
  "dxil_text_patch": [
    {
      "find": "EXACT_ORIGINAL_DXIL_TEXT",
      "replace": "EXACT_PATCHED_DXIL_TEXT"
    }
  ]
}
```

What happens internally:

1. Registry keeps the original bytecode from PSO creation.
2. `ensure_d3d12_patch_entry_compiled()` builds the patched bytecode on demand.
3. `patch_dxil_text()` disassembles, replaces text, reassembles, validates, and signs.
4. The output goes to `shader_overrides\cache\dxil_text_patch`.
5. The replacement PSO is created from the original PSO desc/stream with the patched compute bytecode.

---

## Per-Eye Variants

Supported payload keys:

```text
left_bytecode
right_bytecode
left_dxil_transform
right_dxil_transform
left_transform
right_transform
left_dxil_semantic_transform
right_dxil_semantic_transform
left_semantic_transform
right_semantic_transform
left_dxil_text_patch
right_dxil_text_patch
left_container_patch
right_container_patch
```

Example:

```json
{
  "backend": "dx12",
  "stage": "ps",
  "target_hash": "4e86dc09",
  "name": "example_per_eye",
  "enabled": true,
  "per_eye_variants": true,
  "left_bytecode": "left.dxbc",
  "right_bytecode": "right.dxbc"
}
```

Caution:

- Per-eye variants rely on command-list eye bucket tracking.
- Graphics draws with viewports usually work.
- Compute dispatches may not have reliable eye buckets.
- For `UWEFogResolveCS`, prefer one variant that reads `ViewRectMin.x` from the View UB.

---

## Bind Override Manifests

These patch root CBVs or root constants without replacing the shader.

Example:

```json
{
  "kind": "bind_override",
  "name": "example_right_root_constants",
  "target_hash": "4e86dc09",
  "stage": "ps",
  "pipeline": "graphics",
  "eye": "right",
  "override": "root_constants",
  "root_parameter": 2,
  "dest_offset": 0,
  "values_u32": [0, 0, 0, 0]
}
```

Accepted fields:

- `kind`: `bind_override` or `root_bind_override`.
- `stage`: `any`, `vs`, `ps`, `gs`, `cs`, `as`, `ms`.
- `pipeline`: `graphics`, `compute`, `any`.
- `eye`: `any`, `unknown`, `left`, `right`, `full`, `multi`.
- `override` / `bind_kind` / `type`: `cbv`, `constant_buffer`, `constants`, `root_constants`, `32bit_constants`.
- data: `values_u32`, `data_u32`, `data_hex`, `data_bytes`.

Use this for CB/gate experiments. Do not use it when the shader has no CB lever for the operation, as with the raw resolve store coordinate.

---

## PSO Cache And Timing

The override system depends on PSO tracking and replacement PSO creation. If a target PSO was created before UEVR hooks or before the manifest was scanned, the replacement may not show up until a PSO rebuild.

Ways to force a clean diagnostic:

```text
Place manifest before launch.
Inject early enough for PSO creation hooks.
Disable UE PSO precaching for diagnostics.
Toggle graphics/resolution after injection to force PSO rebuild.
Use logs to confirm replacement PSO creation before judging visuals.
```

Potential UE CVARs for diagnostic launches:

```text
r.PSOPrecaching=0
r.PSOPrecache.GlobalShaders=0
r.PSOPrecache.Components=0
r.PSOPrecache.Resources=0
```

Expect shader compile hitches.

---

## Current Fog Resolve Patch Checklist

Before enabling an override:

1. Decode left/right `UWEFogResolveCS` bound CB values from a capture.
2. Confirm `reg148.x` is `ViewRectMin.x`.
3. Confirm `reg149.xy` is eye-local dispatch bounds.
4. Confirm all relevant `textureStore` calls use raw thread x.
5. Create a disabled `dxil_text_patch` manifest for `target_hash: "0930dd4e"`.
6. Build/reload UEVR and confirm the registry sees the manifest.
7. Enable the patch.
8. Confirm the replacement PSO is created.
9. Capture and verify the right resolve output is written to the SBS-right half.

Expected shader logic:

```text
old: textureStore(uN, tid.x, tid.y, ...)
new: textureStore(uN, tid.x + (uint)ViewRectMin.x, tid.y, ...)
```

Leave the early-out bounds alone unless capture evidence proves otherwise. The running threads already compute the right-eye fog. The bug is where they store it.

---

## Practical Rule

If the bug is "shader computes correct value but stores/samples it in the wrong coordinate space," prefer a shader override.

If the bug is "constant buffer field is wrong," prefer a CB/root-constant bind override or upstream CPU fix.

If the bug is "resource content is wrong because the producer never runs," fix the producer.

Use RDG texture copies only as diagnostics or last-resort workarounds, not as the default fix surface.
