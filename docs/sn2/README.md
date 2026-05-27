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
| [SHADER_OVERRIDES_AND_DXIL_PATCHING.md](SHADER_OVERRIDES_AND_DXIL_PATCHING.md) | UEVR shader replacement, DXIL text patch, bytecode override, per-eye payload, and bind-override workflow |
| [NEXT_SESSION.md](NEXT_SESSION.md) | Concrete plan to finish the right-eye fog fix |
| [STEREO_FORENSICS_STATUS_2026_05_23.md](STEREO_FORENSICS_STATUS_2026_05_23.md) | Status of the unified Stereo Forensics Layer and remaining 1-8 roadmap |
| [STEREO_FORENSICS_DB_WORKFLOW.md](STEREO_FORENSICS_DB_WORKFLOW.md) | Required durable knowledge DB workflow for captures, findings, evidence, and fix rules |
| [STEREO_FORENSICS_FINDINGS.md](STEREO_FORENSICS_FINDINGS.md) | Generated findings report from the Stereo Forensics DB |
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

The visible underwater teal source is the geometry draw using PS `0x13b00f0c` (VS `0x833a1657`, `DrawIndexedInstanced`, `index_count=76608`). It runs for the left viewport only. The right-eye water/fog consumers run, but this teal-carrying draw is missing on the right, so the right eye shows the washed/above-water-looking state.

> 2026-05-26 late update: the `UWEFogResolveCS` store-position theory was disproven by dye tests. UEVR shader overrides still work and remain useful for probes, but the current fix candidate is replaying the left-only `0x13b00f0c` draw onto the right viewport using the captured real right-eye View CB.

## The Fix Approach (One-Liner)

Replay the missing `0x13b00f0c` underwater draw for the right viewport, swapping only the validated right-eye View CB root that worked in testing.

Current one-switch candidate:

```cmd
set UEVR_SN2_FIX_RIGHT_EYE_UNDERWATER=1
```

That wrapper auto-targets PS `0x13b00f0c`, enables captured right View CB discovery, suppresses the old broad water-basepass replay list, and defaults the View-CB swap to root 4 for this draw. If PSO CRC recording is unavailable in an inject-after-run session, it falls back to the known draw shape (`index_count=76608`, `instance_count=1`, one RTV, left viewport) so the wrapper can still replay the target draw.

See [ARCHITECTURE.md](ARCHITECTURE.md) for full detail.

## Current Status (2026-05-26)

**Working pieces:**
- `UEVR_SN2_FIX_RIGHT_EYE_UNDERWATER=1` enables the required command-list hooks and upload-buffer tracking.
- Captured-right View CB discovery validates a real right-eye View UB (`originX=640` in the 1280x720 menu run).
- The wrapper replays the left-only `0x13b00f0c` draw onto the right viewport with `swaps=1 root0=4`.
- The fallback path handles `ps_crc=0` by matching the target draw shape instead of requiring PSO CRC availability.

**Open issues:**
- Reliable visual validation is still open. Later OpenXR tests proved the first deferred replay point is not final-visible: even a magenta clear after the replay did not appear in the mirror, so that target is probably overwritten.
- The next probe is late-anchor replay: `UEVR_SN2_UNDERWATER_DEFER_ANCHOR_PS=0xac96b4c1` plus `UEVR_SN2_UNDERWATER_DEFER_ANCHOR_TRACE=1`, so replay happens after a known visible right-eye post-water draw instead of at the early left-only draw location.
- The draw-shape fallback is intentionally narrow but still a heuristic. If it duplicates extra geometry, tighten it with VS CRC/root-signature/fixed-SRV checks or a learned PSO pointer allowlist.
- Do not treat the old fog-volume mirror, resolve-copy, or `UWEFogResolveCS store.x += ViewRectMin.x` paths as current fixes for this visible menu bug.

See `E:\Github\Subnautica 2\moddingkit\runs\SN2_RIGHT_EYE_HANDOFF_2026_05_26.md` for the comprehensive handoff doc.
