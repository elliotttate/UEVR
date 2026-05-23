# Next Session: Path C — DXIL VoxelizePS Patch

After exhausting Paths A and B this session, **Path C (DXIL shader patch on VoxelizePS) is the cleanest remaining path** to actually fix the SN2 right-eye fog bug.

## Why Path C wins

| Path | Status | Mechanism | Risk |
|---|---|---|---|
| **A** — verify heap aliasing | Untried | Diagnostic only; doesn't fix | Low risk, high effort to build diagnostic |
| **B** — consumer SRV redirect | **BLOCKED — hangs game** | Even if it worked, dup write still corrupts LEFT | High risk; current code path-dead |
| **C** — DXIL VoxelizePS patch | Untried | Modify shader to be eye-aware. No mirror, no dup, no aliasing | Medium risk; biggest engineering work |

## The architectural insight

The mirror+dup+synth approach was the wrong architecture:
1. UE5's VoxelizePS is LEFT-only by engine design
2. Adding a right-eye DUP requires writing to a SEPARATE resource (mirror)
3. The consumer must read from the right mirror for right-eye
4. **But D3D12 placed-resource aliasing means our mirror writes can corrupt the original**

A DXIL patch sidesteps ALL of this: modify VoxelizePS itself to render to the right slice for right eye. The engine then runs ONE pass that writes BOTH eye projections to the same volume (just to different Z-slices), and the existing consumer naturally picks the right slice.

## Concrete steps for next session

### Step 1: Extract VoxelizePS DXIL bytecode

Already shipped in `Sn2PsoBytecodeDumper`. Set:
```powershell
$env:UEVR_SN2_PSO_BYTECODE_DIR = 'C:\tmp\pso_bytecode'
```
Launch game, capture a frame. Bytecode lands at `C:\tmp\pso_bytecode\pso_0x9d14fcf0_PS.dxbc`.

### Step 2: Disassemble + understand the shader

Use `dxc.exe -dumpbin pso_0x9d14fcf0_PS.dxbc`. The shader:
- Reads `View.RelativeWorldToClip` matrix from cbuffer 0 (root param 3)
- Computes voxel position via inverse-projection
- Writes `IntegratedLightScattering` voxel via GS expansion

### Step 3: Identify the per-eye divergence point

The shader needs ONE conditional based on eye index. UE5's standard approach uses `View.StereoPassIndex`. Look for this in the cbuffer reads. If found, the shader already SUPPORTS per-eye — UEVR just needs to make sure the right eye actually runs the shader with eye_index=1.

If NOT supported, the DXIL patch needs to add it:
```
- Read SV_RenderTargetArrayIndex into eye_slice
- For right eye writes, offset SV_RenderTargetArrayIndex by N (volume_depth)
- Allocate 2x depth IntegratedLightScattering volume
```

### Step 4: Wire the patch

UEVR has `Sn2DebugColorOverride` (in `D3D12Hook.cpp`) which already demonstrates the PSO-clone-and-replace pattern. Adapt this to:
1. Cache original PSO desc on `CreateGraphicsPipelineState`
2. When the cached PSO is bound for draw, swap to patched-PSO
3. Patched PSO uses modified DXIL with eye-aware write logic

The PSO swap mechanism works (Magenta override is verified). The new work is **the DXIL editor**.

### Step 5: DXIL editing options

| Tool | Difficulty | Notes |
|---|---|---|
| Hand-edit DXBC bytes | Hard | Need DXIL spec knowledge + offsets |
| `dxc.exe -recompile` from edited HLSL | Medium | Recompile from source-like HLSL after disassembly editing |
| Microsoft DXIL editor library | Easy if available | Look for `dxil-spv` or similar libraries |
| Use existing patches in `tools/dxil-patch/` | Easy | Check if UEVR already has dxil-patch tooling |

Check `E:\Github\UEVRJ\tools\dxil-patch\` — it's listed in `cmake.toml`. May already have what's needed.

## Files to reference for the next session

| Path | Purpose |
|---|---|
| `E:\Github\UEVRJ\docs\sn2\SN2_RIGHT_EYE_FOG_MASTER.html` | Master doc: every finding |
| `E:\Github\UEVRJ\docs\sn2\FIX_STATUS_2026_05_22.md` | This session's status |
| `E:\Github\Subnautica 2\moddingkit\runs\launch_stable.ps1` | Known-good baseline launcher |
| `E:\Github\UEVRJ\src\hooks\Sn2DebugColorOverride.cpp` | PSO clone/replace pattern template |
| `E:\Github\UEVRJ\src\hooks\Sn2PsoBytecodeDumper.cpp` | DXBC extraction |
| `E:\Github\UEVRJ\tools\dxil-patch\` | Possibly existing DXIL editing tools |

## Estimated effort

- Step 1-2: 30 minutes (extract + disassemble)
- Step 3: 1-2 hours (read the shader, understand what to patch)
- Step 4: 2-3 hours (wire PSO swap, integrate DXIL editor)
- Step 5: variable — if dxil-patch already exists in UEVR, 1 hour; if building from scratch, 4-6 hours

**Total: 4-8 hours for a single-shader patch that completely sidesteps the aliasing problem.**

## What this session shipped that's reusable

- FixRuleEngine ordering bug fixed (commit `ba9279f`) — DXIL patch path will benefit
- Mirror infrastructure built (not needed for DXIL path but reusable for other bugs)
- Bisection toolkit (Sn2FrameCapture + 20 Python tools) — accelerates any future debugging
- `launch_stable.ps1` — minimum config baseline
- Master HTML doc — complete context for the next agent
