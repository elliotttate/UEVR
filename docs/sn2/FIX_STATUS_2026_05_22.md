# SN2 Right-Eye Fog Fix — Status as of 2026-05-22

## What we proved

### 1. The "white right eye" is NOT a UEVR bug introduced by SN2 work
It's the engine's natural output for the right viewport when UEVR's
`-emulatestereo` runs and per-view fog data isn't fixed. Without any
UEVR_SUBNAUTICA2_* patches, right eye is **dark/black** (the engine
doesn't render the fog chain at all).

### 2. The minimum UEVR-builtin layer is JUST ONE env var
Of the 11 `UEVR_SUBNAUTICA2_DISABLE_*` env vars in UEVR, **only one
is required** to make the fog chain execute on right eye:

```
UEVR_SUBNAUTICA2_DISABLE_UNDERWATER_FOG_VIEW_DATA_FIX = 0  (enable)
```

Either of these two alone is sufficient (they trigger the chain
identically). The other 9 builtins are not load-bearing.

### 3. FixRuleEngine had a critical ordering bug (NOW FIXED)
Until commit `ba9279f`, the `Skip`-mode early-return in
`sn2_try_duplicate_slw_basepass_right` fired BEFORE the Phase FF
FixRuleEngine consultation. This silently dropped any fix rule that
tried to override a skip-mode PSO.

### 4. The remaining blocker is placed-heap aliasing
With FixRules now correctly consulted, VoxelizePS gets the
`SynthesizeRightCb` mode + mirror RTV redirect. Mirror RTV is created
and bound in OMSetRenderTargets. But LEFT eye still shows artifacts
when the right-eye dup runs.

The dup writes to mirror correctly. But the mirror's GPU memory is
likely aliased with the original via the underlying placed heap
(documented in `sn2_aliasing_CONFIRMED_definitive_2026_05_21.md`).

## Current stable configuration

```powershell
# 1 UEVR builtin (drives fog chain)
$env:UEVR_SUBNAUTICA2_DISABLE_UNDERWATER_FOG_VIEW_DATA_FIX     = '0'
# Disable everything else
$env:UEVR_SUBNAUTICA2_DISABLE_COMPOSE_VOLUMETRIC_VIEW_RECT_FIX = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_LEGACY_FOG_UNIFORM_MUTATIONS     = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_SETUP_VOLUMETRIC_FOG_UB_HOOK     = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_LIGHTSCAT_STORE_MIDHOOK          = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_VOLUMETRIC_RT_REPLAY             = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_SINGLE_LAYER_WATER_PASS_FIX      = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_RENDER_FOG_VIEW_RECT_FIX         = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_VOLUMETRIC_FOG_VIEW_LOOP_FIX     = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_WATER_STATE_SYNC                 = '1'
$env:UEVR_SUBNAUTICA2_DISABLE_SINGLE_LAYER_WATER_VIEW_RECT_FIX = '1'

# Cosmetic: hide bright sky on right eye
$env:UEVR_SN2_SKYATMOS_SKIP_RIGHT = '1'

# Stable dup mechanism (50 view_cb_only entries)
$env:UEVR_SN2_DUP_CONFIG_FILE              = 'sn2_dup_cfg_voxelize_synth.json'
$env:UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT = '1'

# Required infra
$env:UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS = '1'
```

Result: LEFT eye **correct teal underwater**, RIGHT eye **dark underwater
with bright sky-atmos at top**. Not visually-matched but stable + no
artifacts. Better than baseline (which had blown-out white right eye).

## To finish the fix (next session)

The VoxelizePS `synth_right_cb` dup writes to mirror cleanly but LEFT
eye gets corrupted. This is the heap-aliasing issue. Approaches:

1. **Allocate mirror from a SEPARATE heap** (current Sn2UweFogMirror
   uses CreateCommittedResource, which should be its own heap — but
   GPU memory aliasing can still happen at the virtual-address level
   with placed heaps). Verify that VoxelizePS's original RTV resource
   isn't using a placed heap that shares memory with the mirror.

2. **Wire consumer SRV redirect** (`UEVR_SN2_CONSUMER_SRV_REDIRECT=1`)
   so right-eye consumer reads from mirror instead of original.
   Currently the SRV redirect code exists in D3D12Hook.cpp but isn't
   tested live with the FixRules path.

3. **DXIL shader patch on VoxelizePS** to make it eye-aware (read eye
   index from CB, write to mirror slice if right eye). Bigger work
   but architecturally cleaner — no RTV swap, no aliasing risk.

## Tooling we shipped this session

All committed to UEVR + pushed:
- 22+ Sn2* C++ modules (capture, dup, mirror, synth, fix-rules, etc.)
- 20 Python tools (capture orchestration, diff, inspector, fix-rules CLI)
- Full docs at `docs/sn2/`
- CMakeLists.txt registers all sources
- FixRuleEngine ordering bug fix (commit `ba9279f`)

The toolkit is reusable for any per-eye rendering bisection investigation.
