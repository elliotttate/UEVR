# Next Session Plan

Concrete, well-scoped tasks to finish the right-eye fog fix.

Estimated total: **2-4 focused hours**.

## Pre-flight checklist

Before starting:
1. Read `runs/SN2_RIGHT_EYE_HANDOFF_2026-05-22.md` for full session context
2. Skim `docs/uevr-sn2/ARCHITECTURE.md` for the bucket-shadow design
3. Verify build environment: `E:\Github\UEVRJ\build_run.cmd` succeeds clean
4. Verify Meta XR Simulator runs and shows side-by-side eye view
5. Read these memory files (auto-loaded but worth re-reading):
   - `sn2_fog_chain_definitive_2026_05_22.md`
   - `sn2_consumer_srv_redirect_2026_05_22.md`
   - `sn2_uevr_modules_2026_05_22.md`

## Stage X1: Stabilize the synth+RTV pipeline (Hour 1)

**Goal**: Re-enable VoxelizePS synth without LEFT-eye flicker or game freeze.

### Subtask X1a: Add DSV preservation

The current RTV redirect calls `OMSetRenderTargets(rtv_count, rtvs, FALSE, nullptr)` — passes `nullptr` for DSV. The game may have had a DSV bound. After our restore, the game continues without DSV → depth state corrupted.

1. Find where DSV is tracked in `CommandListCorrelationState` (search for `last_dsv` or `dsv_handle`)
2. If not tracked, add it: hook `OMSetRenderTargets` to record the DSV handle alongside RTVs
3. In our redirect (D3D12Hook.cpp line ~9695), pass the captured DSV in both substitute and restore calls

### Subtask X1b: Add explicit resource barriers

Currently relying on D3D12 implicit promotion (mirror initial state COMMON). May be racy at scale.

1. Add `ResourceBarrier(UAV→RT)` before mirror RTV bind in dup
2. Add `ResourceBarrier(RT→UAV)` after restore
3. Test stability for 5+ minutes — if no freeze, X1 done

### Subtask X1c: Cap RTV cache

Current `mirror_rtv_cache` in `D3D12Hook.cpp` grows unbounded. With 22+ mirrors and frame-by-frame rotation, could exhaust the RTV heap (1024 slots).

1. Add LRU eviction: if cache > 256 entries, drop oldest 64
2. Or pre-allocate RTV per mirror at create-time (move out of dup hot path)

## Stage X2: Wire consumer SRV redirect (Hour 2)

**Goal**: At right-eye `0x37558DE4` draws, redirect the SRV pointing at IntegratedLightScattering to point at the mirror's SRV.

### Subtask X2a: Identify the SRV slot

Add a diagnostic similar to `GfxRtvScan` for `0x37558DE4`:

```cpp
if (ps_crc == 0x37558DE4 && eye_bucket == StereoTraceBucket::Right) {
    // For each graphics root descriptor table, scan slots, log resource
    // ptr at each. Match against mirror registry. Find which slot has the
    // IntegratedLightScattering resource (or its mirror, if redirect is
    // already partially wired).
}
```

Run once, observe log, find the slot.

### Subtask X2b: Build the redirect

Pattern matches the existing compute UAV redirect in `D3D12Hook.cpp` around line 10100:

```cpp
// At right-eye 0x37558de4 draws (NOT in dup, in original draw path):
if (current_ps_crc == 0x37558DE4 && eye_bucket == StereoTraceBucket::Right) {
    // Find the SRV slot pointing at IntegratedLightScattering
    // Find the mirror for that resource
    // Build scratch table with mirror's srv_cpu substituted
    // SetGraphicsRootDescriptorTable with scratch table
    // Continue with original draw
    // Restore game's table after draw
}
```

### Subtask X2c: Test

Re-enable VoxelizePS synth + run game.

Expected: RIGHT eye now shows fog data from mirror (right-eye-projected). LEFT eye unchanged (still reads original).

## Stage X3: Fix wrong-fog-colors (Hour 3)

**Goal**: The fog color on RIGHT eye should match LEFT (or be sensibly right-eye-correct).

### Diagnosis

1. Dump VoxelizePS LEFT CB and donor LEFT CB:
   ```bash
   $env:UEVR_SN2_CB_DUMP_PSOS = '0x9d14fcf0,0x4d44ce74'
   $env:UEVR_SN2_CB_DUMP_ROOTS = '3'
   $env:UEVR_SN2_CB_DUMP_DIR = 'C:\tmp\cb_dumps_diag'
   ```
2. Symbolic diff:
   ```bash
   python decode_view_cb.py \
       G_0x9d14fcf0_root3_eye0_seq0001.bin \
       G_0x4d44ce74_root3_eye0_seq0001.bin
   ```
3. Identify fields where they differ (look for non-trivial Δ values)

### Resolution paths (priority order)

#### Path 1: Proper synthesis
Snapshot VoxelizePS's OWN LEFT CB. Apply per-eye-divergent slots from donor's diff (RIGHT - LEFT vector). Use as the synth source.

Implementation:
1. Extend `Sn2RightCbSynth` to also snapshot LEFT CB of target PSO
2. Compute LEFT + (donor.RIGHT - donor.LEFT) for each of 80 divergent slots
3. Write result to upload buffer; bind that GPU_VA in dup

Estimated ~150 LOC.

#### Path 2: Different donor
Find a both-eye PSO that uses the same view-relative scattering parameters as VoxelizePS. Run binding analyzer, look for PSOs that share the same View CB pool root index AND fire on both eyes.

#### Path 3: Field patching
Identify which specific fields VoxelizePS reads (DXBC reflection). Patch only those.

## Stage X4: Validation (Hour 4)

1. Stability: 10+ minute play session, no freeze
2. Visual: both eyes correct in title menu scene
3. Multi-scene: test in another in-game location (load save, swim around) to verify it's not menu-specific
4. Performance: check GPU frame time doesn't degrade (mirror writes add overhead)
5. Document the final dup_cfg in `sn2_dup_cfg_FINAL.json`
6. Update memory files:
   - Mark `sn2_fog_chain_definitive_2026_05_22.md` as resolved
   - Write a new memory file documenting the final fix
7. Update `runs/SN2_RIGHT_EYE_HANDOFF_2026-05-22.md` with "RESOLVED" status

## Risk register

| Risk | Mitigation |
|---|---|
| DSV tracking is harder than expected | If `last_dsv` not in `CommandListCorrelationState`, may need to add a new field + hook |
| Consumer redirect doesn't find IntegratedLightScattering in tables | Fall back to RTV-based approach (track which resource the LEFT producer last wrote to; substitute SRV based on that match) |
| Wrong-fog-colors persists with proper synthesis | Donor may not be the right approach. Use VoxelizePS's own LEFT CB content as base. |
| Performance regression | Mirror writes ~2x the fog producer cost. If unacceptable, only mirror on actual stereo (skip when LEFT==RIGHT eye state) |

## When you're stuck

1. Re-read [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — many issues have already been hit
2. Check the running log: `C:\Users\ellio\AppData\Roaming\UnrealVRMod\Subnautica2-Win64-Shipping\log.txt`
3. Capture a screenshot via `mcp__wslsnapit__take_screenshot windowTitle:"XR"` for visual state
4. Check task list (`#88 in_progress`, `#89 completed`, `#90 in_progress` at session end)
5. Read the durable memory files

## Definition of done

**Visual**: Both eyes show the same teal underwater fog atmosphere (with proper stereo parallax). No flicker, no corruption, no missing tint.

**Stability**: 10-minute play session without freeze/crash.

**Configuration**: A single launch script (e.g., `launch_sn2_fixed.ps1`) sets all required env vars + dup_cfg path.

**Documentation**: Final fix recipe documented in a new memory file + updated handoff doc.
