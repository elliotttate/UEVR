# SN2 right-eye water basepass duplication — plan

## What the bug actually is (confirmed 2026-05-21)

Both eyes' SLW composite read `MRT[6]` (the SLW GBuffer auxiliary RT, e.g. ResourceId::30085 in the May 16 RDC; corresponding heap slot in May 21 is `dh_4345_30 slot 6`). Both eyes' OMSetRenderTargets bind the same 7-RTV array.

**Left eye** runs 11 water-material basepass draws into that MRT (lines 4148–4626 of CommandList04.cpp in the May 21 Cpp2025 project). **Right eye** runs only **1 graphics draw** (PSO 1425) plus 4 unrelated compute dispatches. The 14 water-material draws never reach the right eye.

Confirmed by the colleague: `Pixel binding identical between eyes, single-writer-view-independent classification, all 14 water material PS draws in event range 20618–20793 on LEFT eye only`.

## Existing UEVR precedent: `sn2_try_duplicate_fog_voxelize_right`

D3D12Hook.cpp:8702–8806 already implements the "duplicate a LEFT-eye draw for the right viewport" pattern for a single specific PS+GS combo (`ps_crc=0x9D14FCF0, gs_crc=0x6B1ACA8C`, the GS-voxelize fog path). On a matching LEFT-eye draw it:

1. Computes the right viewport (mirror of left)
2. `RSSetViewports(right)`
3. Re-issues the same draw call
4. `RSSetViewports(left_restored)`

This works for the GS voxelize because both halves project the same world-space content via the GS's viewport-array.

## Why the same trick doesn't quite work for water materials

The water-material basepass uses a **per-eye View CB** that contains:
- TranslatedWorldToClip matrix (per-eye projection)
- WorldCameraOrigin (per-eye IPD offset)
- ViewRect (per-eye `(0, 0, 640, 720)` for left, `(640, 0, 640, 720)` for right)

If we re-issue a LEFT draw with only the viewport changed, the vertex shader still computes positions using LEFT's V-P matrix, then the rasterizer maps them into the right-half of the framebuffer. The geometry shows up in the wrong place because LEFT's V-P doesn't match RIGHT's IPD.

We need to also **swap the View CB** to view-1's pointer before re-issuing.

## Design

### Detection

A draw is a "water-material basepass" candidate if:
- It's a `DrawIndexedInstanced` / `DrawInstanced` (graphics)
- Its bound PSO's PS is one of the water-material variants (PS CRC TBD per binary, identified by being among the 14 LEFT-only basepass writers in the May 16 capture: `0x1ea961e4`, `0x30782963`, `0x9ecbcc2e`). In the May 21 capture the equivalent CRCs are TBD (need cross-reference)
- The current MRT setup has 7 slots bound (the SLW-extended GBuffer layout)
- The current viewport is the LEFT-half viewport

### Duplication

After the original LEFT draw fires:

```cpp
// 1. Snapshot the state we need to swap
const D3D12_VIEWPORT left_vp = state.viewport0;
const D3D12_GPU_VIRTUAL_ADDRESS left_view_cbv = state.last_graphics_root_cbv[VIEW_CB_ROOT];

// 2. Swap to right-eye viewport
D3D12_VIEWPORT right_vp = left_vp;
right_vp.TopLeftX = left_vp.TopLeftX + left_vp.Width;
command_list->RSSetViewports(1, &right_vp);

// 3. Swap to right-eye View CB
//    UEVR captures view-1's view CB pointer from prior right-eye draws
//    (see `g_subnautica2_right_view_cbv_va` — needs new tracking)
if (right_view_cbv_va != 0) {
    command_list->SetGraphicsRootConstantBufferView(VIEW_CB_ROOT, right_view_cbv_va);
}

// 4. Re-issue the original draw
original(command_list, index_count, instance_count, start_index, base_vertex, start_instance);

// 5. Restore left state
command_list->RSSetViewports(1, &left_vp);
if (right_view_cbv_va != 0) {
    command_list->SetGraphicsRootConstantBufferView(VIEW_CB_ROOT, left_view_cbv);
}
```

### Tracking view-1's View CBV pointer

The right-eye View CB pointer is observable on every right-eye basepass draw (it's bound at `root_param[VIEW_CB_ROOT]`). Add a new TLS / global that on every draw with a RIGHT-eye viewport, snapshot the View CBV VA into `g_subnautica2_right_view_cbv_va`. Stable enough to use on the next frame's LEFT-eye duplication.

Caveats:
- VIEW_CB_ROOT is per-shader-class. pso3069 has Pixel CBVs at root 4-7 (per memory) and Vertex CBVs at root 8-11. View CB is typically at root 4 (cb0) for pixel, root 8 (cb0) for vertex. The water-material basepass shader's root sig must be inspected to confirm.
- First few frames: the right CBV VA is unknown until we've observed at least one right-eye draw. Skip duplication until we have a cached value.

### Implementation outline

New file: `Sn2WaterBasepassDupHook.hpp` (analogous to `Sn2VsmUbClampPatch.hpp`):

```cpp
namespace sn2_water_basepass_dup {
    // Configured set of PS CRC32 values for water-material basepass draws.
    // Update per-binary based on the capture analysis at hand.
    inline constexpr std::array<uint32_t, 4> k_water_basepass_ps_crcs = {
        0x1ea961e4,  // May 16 variant A
        0x30782963,  // May 16 variant B
        0x9ecbcc2e,  // May 16 variant C
        0x00000000,  // padding for future variants
    };

    inline constexpr UINT k_view_cb_root_pixel = 4;
    inline constexpr UINT k_view_cb_root_vertex = 8;

    bool env_enabled() {
        const char* v = std::getenv("UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT");
        return v && v[0] && v[0] != '0';
    }
}
```

New function in D3D12Hook.cpp (next to the existing `sn2_try_duplicate_fog_voxelize_right`):

```cpp
static void sn2_try_duplicate_slw_basepass_right(
    ID3D12GraphicsCommandList* command_list,
    const CommandListCorrelationState& state,
    Sn2DrawIndexedInstancedFn original,
    UINT index_count, UINT instance_count,
    UINT start_index, INT base_vertex, UINT start_instance)
{
    if (!sn2_water_basepass_dup::env_enabled() || /*basic guards*/) return;

    // Match PS CRC against known water-material variants
    const uint32_t ps_crc = registry.d3d12_pso_pixel_crc32(reinterpret_cast<uintptr_t>(state.current_pso));
    bool is_water_basepass = false;
    for (uint32_t c : sn2_water_basepass_dup::k_water_basepass_ps_crcs) {
        if (c != 0 && c == ps_crc) { is_water_basepass = true; break; }
    }
    if (!is_water_basepass) return;

    // Must be 7-MRT setup
    if (state.last_rtv_count != 7) return;

    // Must be LEFT viewport
    if (state.last_viewport_bucket != StereoTraceBucket::Left) return;

    // Must have observed view-1 CB pointer
    const auto right_view_cbv = sn2_right_view_cb_va();
    if (right_view_cbv == 0) return;

    // ...build right viewport, swap CB, re-issue, restore...
}
```

Hook installation: call `sn2_try_duplicate_slw_basepass_right` from `D3D12Hook::draw_indexed_instanced` after the original draw, BEFORE the scope-pop code that restores per-draw state.

### Risks

1. **Wrong PS CRC list**. The May 21 binary likely has different PS CRC32 values for the water materials than May 16. Need to identify them by inspecting which PSOs draw into MRT slot 6 of the 7-RTV setup on left eye and capture their PS CRCs.
2. **Wrong root for View CB**. If the water material's root sig doesn't put View CB at root 4 (pixel) or root 8 (vertex), the swap binds the wrong CB and the duplicate draw produces garbage.
3. **Vertex shader uses ViewRect to clip**. If the geometry happens to have already-clipped vertices for the LEFT viewport from CPU-side culling, re-rendering with RIGHT viewport may miss geometry that was outside LEFT's frustum but inside RIGHT's. Acceptable for a first-pass fix.
4. **Existing redirects / overrides on the LEFT draw**. The existing `sn2_begin_pso3069_fog_scratch_table`, `sn2_begin_water_chain_scratch_table` etc. modify the CL state for the LEFT draw. If we re-issue without re-applying them, the duplicate may behave differently. Need to call them again or place the duplication INSIDE the existing override scopes.

## Recommended sequence

1. **Identify water-material PSOs in the May 21 capture.** Cross-reference LEFT basepass region PSOs (1100, 1169, 1435, 1514 + the 2 shared) with which one writes to MRT slot 6 vs which is some other GBuffer-only material. PSOs whose PS produces a SingleLayerWater shading-model output are the targets.
2. **Identify View CB root param** for each water-material PSO's root signature. Most likely root 4 (pixel) and root 8 (vertex) but confirm.
3. **Add view-1 CBV pointer tracker** — `update_subnautica2_right_view_cbv_va(state)` called on right-eye draws.
4. **Build `sn2_try_duplicate_slw_basepass_right`** following the existing fog-voxelize-duplicate pattern.
5. **Test under `UEVR_SN2_DUPLICATE_SLW_BASEPASS_RIGHT=1`**. Expected: right-eye basepass also runs the 14 water material draws into MRT slot 6. The right-eye composite then reads valid data and produces a fog-correct image.
6. **Run Tool 8 (regression bundle) before/after**.

## Estimated effort

- Identify PSOs + roots: 1-2 hours (need a fresh basepass-region capture)
- UEVR implementation: 2-4 hours
- Testing + iteration: 2-4 hours

Total: 5-10 hours, comparable to the VSM clamp + UEVR + verify cycle that already worked.

## Why this is now the most viable fix

- Family A (clamp) — VSM-only, doesn't apply
- Family B (force per-view loop) — the per-view loop ITERATES on right; the bug is one level deeper (mesh-batch list contents)
- Family C (RDG pass clone) — too invasive
- Family D (DXIL UV remap) — moot; data not present
- **Family F (draw duplication, this plan)** — surgical, mirrors existing precedent (`sn2_try_duplicate_fog_voxelize_right`), reversible via env flag

## Status

PLANNED. Not yet implemented. Awaiting PSO + root-sig identification on the live binary.
