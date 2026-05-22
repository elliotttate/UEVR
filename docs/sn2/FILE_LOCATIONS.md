# File Locations — Quick Reference

Every file path mentioned in the SN2 stereo VR debugging effort, organized by category.

## UEVR Source Tree

`E:\Github\UEVRJ\`

### UEVR backend code

```
src\hooks\
├── D3D12Hook.cpp                  ← main D3D12 hook + dup function + redirect logic
├── D3D12Hook.hpp
├── Sn2BindingAnalyzer.cpp/.hpp    ← per-PSO View CB delta tracking
├── Sn2CbDumper.cpp/.hpp           ← CB byte dumps
├── Sn2PsoBytecodeDumper.cpp/.hpp  ← DXBC dumps
├── Sn2FrameCppExport.cpp/.hpp     ← frame→C++ export (scaffold)
├── Sn2RightCbSynth.cpp/.hpp       ← donor snapshot for right CB
├── Sn2MagicInk.hpp                ← live PS skip
├── Sn2RtDiff.cpp/.hpp             ← L+R scene-color pairing
├── Sn2EyeScreenshot.cpp/.hpp      ← file-trigger eye PPM capture
├── Sn2StateInspector.cpp/.hpp     ← on-demand draw-state JSON dump
├── Sn2UweFogMirrorHook.cpp/.hpp   ← mirror UAV/SRV/RTV
├── Sn2ResourceTimeline.hpp        ← per-resource access timeline (scaffold)
├── Sn2FixRuleEngine.hpp           ← declarative fix rules (scaffold)
├── Sn2DupConfigFile.hpp           ← dup_cfg JSON loader + DupMode enum
├── Sn2OverlayUI.cpp/.hpp          ← ImGui debug overlay
└── Sn2HooksInstall.cpp            ← UE5 material/RDG hook installers
```

### UEVR build artifacts

```
build\
├── bin\uevr\UEVRBackend.dll       ← the DLL UEVR injects (~14MB)
└── uevr.vcxproj                   ← MSBuild project file
```

### UEVR build scripts

```
build_run.cmd                       ← top-level rebuild script
```

### Generated artifacts

```
artifacts\
├── sn2_dup_cfg_measured.json       ← 74 entries, measured deltas, some unsafe
├── sn2_dup_cfg_restricted.json     ← 50 entries, all delta=-10240, SAFE BASELINE
├── sn2_dup_cfg_synth_minimal.json  ← test config
└── sn2_dup_cfg_voxelize_synth.json ← Phase U+V config
```

---

## SN2 Modding Kit

`E:\Github\Subnautica 2\moddingkit\`

### Python analysis tools

```
tools\
├── analyze_60s_diag.py
├── generate_dup_cfg_from_analyzer.py
├── diff_cb_dumps.py
├── diff_rt_dumps.py
├── decode_view_cb.py
├── sn2_auto_bisect.py
└── start_subnautica_uevr.ps1       ← launches the game with UEVR
```

### Launch scripts

```
runs\
├── launch_restricted_cfg.ps1       ⭐ STABLE BASELINE
├── launch_phase_u.ps1              ← full pipeline (synth + mirror + screenshot)
├── launch_srv_redirect_layered.ps1 ← restricted + UEVR_SN2_FOG_SRV_REDIRECT
├── launch_phase_r_tools.ps1        ← Phase R investigation tools
├── launch_bisect_h1.ps1            ← bisection iteration scripts
├── launch_bisect_h1_v2.ps1
├── launch_phase_v.ps1              ← Phase V (consumer SRV redirect) [if built]
└── SN2_RIGHT_EYE_HANDOFF_2026-05-22.md  ← handoff doc
```

### Documentation

```
docs\uevr-sn2\
├── README.md
├── ARCHITECTURE.md
├── MODULES.md
├── PYTHON_TOOLS.md
├── WORKFLOWS.md
├── TROUBLESHOOTING.md
├── DUP_CONFIG.md
└── FILE_LOCATIONS.md (this file)
```

---

## Game Install + UEVR Runtime

```
C:\Program Files\Epic Games\Subnautica II\
└── (game install — Subnautica2.exe wrapper + Subnautica2-Win64-Shipping.exe)

C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\
├── log.txt                         ← UEVR runtime log (heavy reads here)
├── crash.dmp                       ← on crash
└── rt_snapshots\                   ← Sn2RtDiff/Sn2EyeScreenshot output
```

---

## Diagnostic Output (Transient)

```
C:\tmp\
├── ink_skip.txt                    ← Magic Ink live skip list (edit to bisect)
├── uevr_shot_req.txt               ← Eye Screenshot trigger file
├── uevr_screenshots\               ← Eye Screenshot output dir
│   ├── left.ppm
│   ├── right.ppm
│   ├── backbuffer.ppm
│   └── done.txt
├── cb_dumps\                       ← CB byte dumps
│   └── G_0x<crc>_root<N>_eye<E>_seq<NNNN>.bin/.json
├── state_inspect\                  ← State Inspector output (when wired)
├── rt_diff\                        ← RT diff PPM heatmaps
├── bisect_run\                     ← Auto-bisect results
│   ├── baseline_left.ppm
│   ├── baseline_right.ppm
│   └── result.json
└── sn2_analyzer.json               ← Binding analyzer output
```

---

## Memory Files (Persistent Across Sessions)

`C:\Users\ellio\.claude\projects\E--Github-Subnautica-2\memory\`

### Index
- `MEMORY.md`                      ← always loaded; one-line entries pointing at memory files

### Critical files for this work (most recent first)
- `sn2_consumer_srv_redirect_2026_05_22.md`
- `sn2_fog_chain_definitive_2026_05_22.md`        ← bug chain + status
- `sn2_uevr_modules_2026_05_22.md`                ← module inventory
- `sn2_lumen_uavs_full_stereo_width_2026_05_22.md`
- `sn2_basepass_pso3069_root_cause.md`
- `sn2_fog_srv_redirect_partial.md`               ← prior partial fix
- `sn2_aliasing_CONFIRMED_definitive_2026_05_21.md`

### Feedback memories
- `feedback_test_everything_yourself.md`          ← test in MetaXR sim, don't hand off
- `feedback_ida_tool_preference.md`               ← use ida-pro-mcp, not nsight wrapper

---

## UE5 Source Reference

```
E:\Epic Games\UnrealEngine-5.6.1\
└── Engine\Source\Runtime\Renderer\Public\ViewUniformShaderParameters.h
                                                   ← canonical View CB struct
```

---

## IDA Pro Binaries

```
Subnautica2-Win64-Shipping.exe.i64  (6GB)         ← USE THIS ONE
Subnautica2.exe.i64                                ← wrapper, don't use
```

Located in: `<game install>\binaries\Win64\` or wherever you set up IDA.

Useful tools: `mcp__ida-pro-mcp__*` (preferred over nsight-graphics' `ngfx_ida_analyze_binary` wrapper).

---

## Critical RVAs (Subnautica 2)

| RVA | Function | Purpose |
|---|---|---|
| `0x32A2C20` | `FRDGBuilder::SetupParameterPass` | Per-pass param binding |
| `0x32827B0` | `FRDGPass::ctor` | Pass construction (Name field at +0x10) |
| `0x2644A70` | `FBasePassMeshProcessor::AddMeshBatch` | Per-mesh basepass routing (hooked by Sn2MaterialNameHook) |
| `0x41CDB30` / `0x41A99B0` | `GetFriendlyName` | Material name retrieval |
| `0x2B50544` | `FVirtualShadowMapArray::GetUniformBuffer` | VSM UB (patched by Sn2VsmUbClampPatch) |

(`UMaterialInterface::GetRenderProxy` at `0x41ABCD0` is a PURE_VIRTUAL stub — DO NOT HOOK.)

---

## Repos / External Dependencies

```
E:\Github\UEVRJ\          ← UEVR Just (this fork, where we build)
E:\Github\renderdoc\      ← RenderDoc reference source (we built tools inspired by)
E:\Github\pix-mcp\        ← PIX MCP for shader/cbuffer analysis
E:\Github\Subnautica 2\   ← This project root
```
