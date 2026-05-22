# UEVR Subnautica 2 Stereo Debugging Toolkit

A comprehensive set of UEVR modules and analysis tools built to diagnose and fix Subnautica 2's right-eye fog rendering bug under `-emulatestereo`.

## Documents

| Document | Purpose |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | The bucket-shadow architecture, dup config system, descriptor management |
| [MODULES.md](MODULES.md) | All 12 UEVR C++ modules — API, env vars, internals, examples |
| [PYTHON_TOOLS.md](PYTHON_TOOLS.md) | All Python analysis tools |
| [WORKFLOWS.md](WORKFLOWS.md) | End-to-end debugging workflows (bisection, CB diff, screenshot, etc.) |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Issues encountered + resolutions |
| [DUP_CONFIG.md](DUP_CONFIG.md) | The dup_cfg JSON format + each DupMode explained |
| [FILE_LOCATIONS.md](FILE_LOCATIONS.md) | Quick-reference map of every file path |
| [ENV_VARS.md](ENV_VARS.md) | Every UEVR_SN2_* env var with description |
| [GLOSSARY.md](GLOSSARY.md) | Domain terms used throughout the codebase |
| [NEXT_SESSION.md](NEXT_SESSION.md) | Concrete plan to finish the right-eye fog fix |
| [RENDERDOC_GAP_ANALYSIS.md](RENDERDOC_GAP_ANALYSIS.md) | What RD fork has that UEVR lacks + integration strategy |
| [UEVR_RD_BRIDGE_WORKFLOW.md](UEVR_RD_BRIDGE_WORKFLOW.md) | End-to-end workflow: sidecar emit + qrenderdoc replay-time overrides |

## Quick Start

### 1. Build UEVR
```cmd
E:\Github\UEVRJ\build_run.cmd
```
Output: `E:\Github\UEVRJ\build\bin\uevr\UEVRBackend.dll`

If you see `LNK1104`, the game is running — kill it first:
```cmd
wmic process where "Name='Subnautica2-Win64-Shipping.exe'" delete
```

### 2. Launch with the stable baseline
```cmd
powershell -File "E:\Github\Subnautica 2\moddingkit\runs\launch_restricted_cfg.ps1"
```
This gives you: LEFT eye correct teal fog, RIGHT eye original bug (washed-out sky).

### 3. Iterate on the right-eye fix
Use `launch_phase_u.ps1` for the full pipeline (synth + mirror + screenshot + magic-ink).

### 4. Debug visually
- Meta XR Simulator window shows L/R rendered side-by-side
- Capture: `mcp__wslsnapit__take_screenshot windowTitle:"XR"` or via the eye-screenshot trigger file
- Bisect: edit `C:\tmp\ink_skip.txt` to skip CRCs live (no restart)

## The Bug (One-Liner)

VoxelizePS (`0x9D14FCF0`) writes the IntegratedLightScattering 3D volume only for LEFT eye. The consumer (`0x37558DE4`, SLW water material PS) reads it on both eyes — RIGHT eye samples LEFT-projected data with right matrices → wrong fog → washed-out sky.

## The Fix Approach (One-Liner)

**Bucket-shadow**: allocate a parallel "mirror" 3D volume per fog texture. On right-eye dup of VoxelizePS, write to mirror with synthesized right-eye View CB. On right-eye `0x37558DE4`, redirect SRV to read from mirror instead of original.

See [ARCHITECTURE.md](ARCHITECTURE.md) for full detail.

## Current Status (2026-05-22)

**Working pieces:**
- Synth right-eye View CB (donor snapshot mechanism)
- Mirror allocation for fog 3D textures (54×30×48 R11G11B10F)
- Graphics RTV redirect for the writer (VoxelizePS dup → mirror)

**Open issues:**
- LEFT-eye flicker with synth pipeline active (DSV restore + state barrier issues)
- Consumer SRV redirect not yet wired (the final piece)
- Wrong fog colors when each eye does get different fog data (donor CB compatibility)

See `runs/SN2_RIGHT_EYE_HANDOFF_2026-05-22.md` for the comprehensive handoff doc.
