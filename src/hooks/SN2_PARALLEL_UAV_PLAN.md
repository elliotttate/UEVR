# SN2 parallel-per-eye-UAV plan

## Why this is needed

Both prior duplicators (water-basepass and UWE-fog-compute) targeted shared single-writer output resources. Re-issuing the dispatch/draw with right-eye View CB **overwrote** the LEFT eye's result, corrupting both eyes. The visual evidence (orange noise on left, blown-white on right) confirms the architectural flaw.

The fix requires SEPARATE per-eye output resources at the producer, plus per-eye SRV bindings at the consumer.

## Architecture

```
                  ┌─────────────────────────────┐
                  │  CreateCommittedResource    │
                  │  hook detects target UAV    │
                  └──────────────┬──────────────┘
                                 │
                                 ▼
                  ┌─────────────────────────────┐
                  │  Mirror UAV alloc (P2):     │
                  │  same desc, UEVR-owned heap │
                  └──────────────┬──────────────┘
                                 │
                game_resource ───┴─→ mirror_resource
                                     mirror_uav_cpu/gpu
                                     mirror_srv_cpu/gpu

LEFT dispatch:          UWE fog CS bound, root 0 → table containing game's UAV
                        Game writes to game_resource (untouched by us)

DUP dispatch (P3):      Build scratch desc table = copy of game's table
                        BUT slot N (the UAV slot) → mirror_uav
                        Bind View CB at root 2 = right-eye (delta -10240)
                        Dispatch → writes to mirror_resource

RIGHT-eye basepass (P4):  PS root table contains SRV for game_resource at slot M
                          Build scratch table = copy of game's BUT slot M → mirror_srv
                          Bind, draw, restore
```

## Phases

### P1: Detect target UAVs at CreateCommittedResource

UEVR already hooks `D3D12Device::CreateCommittedResource` (see SN2-CreateRes log emits). Extend to:

1. Inspect the `D3D12_RESOURCE_DESC` of every created texture.
2. Match the UWE fog signature:
   - `Dimension = TEXTURE2D` or `TEXTURE3D`
   - `Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`
   - `Format ∈ {R11G11B10F, R16G16B16A16F, R32G32B32A32F}` (typical fog/lighting formats)
   - `Width/Height` near {80, 90, 74, 77, 588, 616} ranges
3. Tag the returned `ID3D12Resource*` in a thread-safe set `g_uwe_fog_candidates`.

Outputs:
- A `std::unordered_map<ID3D12Resource*, UweFogResourceInfo>` global.
- Log `[SN2-UweFogResource] detected ptr=0x... format=... dim=...x...x...` on each match.

### P2: Allocate mirror UAVs

For each candidate at detection time:

```cpp
struct UweFogMirror {
    ComPtr<ID3D12Resource> mirror;
    UINT mirror_uav_heap_idx;
    D3D12_CPU_DESCRIPTOR_HANDLE mirror_uav_cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE mirror_uav_gpu;
    D3D12_CPU_DESCRIPTOR_HANDLE mirror_srv_cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE mirror_srv_gpu;
    D3D12_RESOURCE_DESC desc;
};
```

1. `device->CreateCommittedResource()` with the same `Desc` and initial state `D3D12_RESOURCE_STATE_UNORDERED_ACCESS`.
2. Allocate two slots in a UEVR-owned CBV_SRV_UAV descriptor heap (`g_uwe_fog_mirror_heap`, GPU-visible).
3. `device->CreateUnorderedAccessView()` on the mirror at slot N → UAV CPU + GPU handles.
4. `device->CreateShaderResourceView()` on the mirror at slot N+1 → SRV CPU + GPU handles.

Hot-path requirement: mirror lookups must be O(1) — `std::unordered_map<ID3D12Resource*, UweFogMirror>`.

### P3: Dup compute dispatch with patched UAV descriptor table

In `sn2_try_duplicate_uwe_fog_compute` (existing scaffold), augment the swap:

1. **Identify the UAV slot.** The UAV is bound via a descriptor table at compute root 0. We don't know which slot of the table holds the UAV without inspecting the heap. Two options:
   - **Option A (general)**: Walk all slots in the table range (limit ~16), call `CopyDescriptorsSimple` to a dummy CPU heap, inspect each — too slow per-dispatch.
   - **Option B (specific)**: For each target CS CRC, pre-discover the UAV slot offset relative to the table base via a one-time probe (the existing fog_compute_diag already logs this — extend).

2. **Build a patched scratch table**:
   - Maintain a per-frame ring buffer of "scratch descriptor heap regions" (already exists for `sn2_copyrect_scratch_heap`).
   - Allocate a region of size N descriptors (matching the table range).
   - `CopyDescriptorsSimple` from the LEFT-eye source heap range → scratch region.
   - Override slot K (the UAV slot) by copying `mirror_uav_cpu` over scratch[K].
   - `SetComputeRootDescriptorTable(0, scratch_gpu_base)`.

3. Issue dispatch with right-eye View CB at root 2.
4. Restore: `SetComputeRootDescriptorTable(0, original_gpu_base)`.

### P4: Right-eye basepass SRV read-side intercept

The fog UAV result (now mirrored) is read by the basepass as an SRV. Need to intercept the SRV bind.

1. Hook `SetGraphicsRootDescriptorTable` (already partially hooked).
2. On every right-eye graphics draw with a pixel-stage SRV table:
   - Scan the table's descriptor slots for SRVs matching tracked game UAV resources.
   - If found, build a scratch table with that slot replaced by `mirror_srv_cpu`.
   - Bind the scratch table; restore after draw.

This is the most complex piece because UEVR doesn't currently parse the bound table contents at SRV granularity. Need to track which game resources are at which heap slots.

**Simplification**: instead of scanning every right-eye draw, target only the known basepass PS CRCs (e.g. the 14 water material variants we identified) and only patch their SRV slot that maps to fog.

### P5: Validation strategy

After P3 lands:
- Game runs without crash → check
- LEFT eye visual unchanged (no overwrite) → check
- RIGHT eye still wrong (because basepass still reads game's UAV which is LEFT-only) → expected until P4

After P4 lands:
- RIGHT eye fog atmosphere restored → goal

## Risk mitigation

- **Descriptor heap corruption**: Per-frame scratch region. Never write into the game's heap directly.
- **Resource state mismatch**: Mirror starts as UAV. After dispatch, transition to PIXEL_SHADER_RESOURCE before basepass.
- **Per-frame churn**: Mirror resources are persistent; only descriptor table copies are per-dispatch.
- **Memory cost**: ~6 MB per mirror at 588x616 R16G16B16A16F. With ~5 candidate UAVs, ~30 MB total. Acceptable.

## Estimated effort

| Phase | Effort | Risk |
|-------|--------|------|
| P1 — detection at create | 1 hr | low |
| P2 — mirror alloc + descriptors | 2 hr | medium (descriptor heap mgmt) |
| P3 — dispatch dup with patched table | 3-4 hr | high (per-dispatch heap copy hot path) |
| P4 — basepass SRV intercept | 4-6 hr | high (table parsing + per-draw cost) |
| Test + iterate | 2-4 hr | medium |
| **Total** | **12-17 hours** | |

Realistically: 2-3 work sessions.

## Plan B (free, immediate)

Before committing to the full parallel UAV build, test the EXISTING `UEVR_SN2_FOG_SRV_REDIRECT=1` which copies LEFT t8/t9 SRVs over RIGHT slot positions at the water composite. Prior session noted PARTIAL fix ("darker with clouds"). May be good enough for the current scene's symptom.

If FOG_SRV_REDIRECT alone fixes it: parallel UAV not needed.
If FOG_SRV_REDIRECT helps but imperfect: do parallel UAV for the rest.
If FOG_SRV_REDIRECT does nothing: full parallel UAV build is justified.
