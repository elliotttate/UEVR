# SN2 water-basepass duplication — V2 (descriptor-table-aware)

## Why V1 is incomplete

V1 only swaps the View CBV via a hardcoded byte-delta (default `-10240`). The colleague's latest finding confirms that's not enough:

- **Root params 0–3 = descriptor tables** (Pixel readonly, Pixel sampler, Vertex readonly, Vertex sampler)
- These are also per-eye, sourced from per-eye descriptor heap slices
- **Per-eye deltas are NOT constant** — varies by shader and per-frame:
  - SLW composite VS readonly: Δ=+29
  - Opaque basepass PS readonly: Δ=+100,306
- Sampler tables (heap 306) appear shared across eyes
- → V1's hardcoded delta swap can't handle descriptor tables

## V2 design

**Capture-then-replay.** Instead of computing right-eye binds from left's via fixed deltas, *snapshot* right-eye binds from any prior right-eye draw with a compatible root signature, then use the snapshot when duplicating a left draw.

### Per-root-sig cache structure

```cpp
struct CapturedRootSnapshot {
    // Root descriptor tables: GPU base handle per root parameter.
    std::array<uint64_t, 16> desc_tables{};   // 0 = unbound
    // Root CBVs: GPU virtual address per root parameter.
    std::array<uint64_t, 16> cbvs{};          // 0 = unbound
    // Validity mask — which root params were actually set on this draw.
    uint32_t desc_table_mask = 0;
    uint32_t cbv_mask = 0;
    uint64_t frame_idx = 0;                   // for staleness checks
};

static std::unordered_map<uintptr_t /*root_sig*/, CapturedRootSnapshot> g_right_eye_snapshots;
static std::mutex g_right_snapshot_mutex;
```

### Population hook

In `draw_indexed_instanced` / `draw_instanced`, for every **right-eye** draw, snapshot the bound state to the per-root-sig cache:

```cpp
if (state.last_viewport_bucket == StereoTraceBucket::Right &&
    state.last_graphics_root_signature != 0) {
    auto& snap = g_right_eye_snapshots[state.last_graphics_root_signature];
    snap.desc_tables = state.last_graphics_root_desc_tables;
    snap.cbvs = state.last_graphics_root_cbv;
    // ... set masks based on non-zero entries ...
    snap.frame_idx = current_frame_idx;
}
```

### Consumption — replace the V1 delta swap

In `sn2_try_duplicate_slw_basepass_right`, before re-issuing:

```cpp
auto& snap = lookup_right_snapshot(state.last_graphics_root_signature);
if (snap is empty / stale) {
    // No right-eye draw with this root sig yet — skip dup or fall back to V1 delta
    return;
}

// Apply right-eye snapshot to the CL for this duplicate
for (UINT i = 0; i < 16; ++i) {
    if (snap.desc_table_mask & (1u << i)) {
        D3D12_GPU_DESCRIPTOR_HANDLE h{};
        h.ptr = snap.desc_tables[i];
        command_list->SetGraphicsRootDescriptorTable(i, h);
    }
    if (snap.cbv_mask & (1u << i)) {
        command_list->SetGraphicsRootConstantBufferView(i, snap.cbvs[i]);
    }
}

// Viewport + scissor swap (unchanged from V1)
command_list->RSSetViewports(1, &right_viewport);
if (use_scissor) command_list->RSSetScissorRects(1, &right_scissor);

original(...);

// Restore left state from `state.last_graphics_root_*` (the LEFT draw's bindings)
for (...) ...
```

### Caveats / open questions

1. **Same root sig vs same shader.** The water basepass root sig might also be used by other materials. If a right-eye opaque draw with the same root sig fires *first*, its descriptor table base could be subtly different from what a right-eye water draw would have used (different bindless slices). May still be close enough to work.

2. **Sampler tables shared.** Don't strictly need to swap them — but doing so is harmless (right-eye snapshot will have the same sampler heap pointer as left).

3. **First-frame chicken-and-egg.** No right-eye draws with the SLW root sig exist until *something* fires on right with that sig. Until then, the duplicator is a no-op. In practice this only matters for the very first frame.

4. **Cross-frame staleness.** Descriptor heap pointers can rotate per-frame. The cached value from frame N may be invalid by frame N+1. Mitigations:
   - Invalidate cache at each frame boundary
   - OR refresh cache on every right-eye draw (preferred — cheap)
   - OR record snapshot timestamp and use only if from current frame

5. **Where is "frame"?** UEVR has `sn2_draw_log_v2::current_frame()`. Use that.

## Implementation steps

1. Add `Sn2WaterBasepassDupRootCache.hpp` with the snapshot struct + accessors.
2. In `draw_indexed_instanced`/`draw_instanced`: after `read_cmdlist_state` and before any work, call `cache_right_eye_root_state(state)` (no-op for non-right draws).
3. In `sn2_try_duplicate_slw_basepass_right`: replace the per-CBV swap loop with the full-snapshot-apply loop.
4. Keep the V1 delta path behind an env: `UEVR_SN2_WATER_BASEPASS_USE_V1_DELTA=1` for emergency fallback.

## Verification strategy

Once V2 is built, the log line per-dup expands to show:
- left CBV at root 4
- right CBV (from snapshot) at root 4
- left desc_table at root 0 base GPU handle
- right desc_table (from snapshot) at root 0 base GPU handle
- snapshot age (frames)

If snapshot age > 1 the dup may be using stale state. If snapshot ages always 0, fresh state is captured each frame.

## Risk assessment

| | V1 (delta) | V2 (capture/replay) |
|--|------------|---------------------|
| Effort | Done | 1-2 hr |
| Correctness | Wrong for descriptor tables | Correct — uses actual right-eye binds |
| Per-binary stability | Brittle (fixed delta) | Self-adapting |
| Crash risk if root sig differs | High (binds wrong CBV) | Low (no-op if no cache) |
| Per-frame heap rotation | Vulnerable | Robust if refreshed every right draw |
| First-frame coverage | Always (delta is constant) | Skips first frame |

V2 strictly subsumes V1 — keep V1 only as a fallback for the chicken-and-egg first-frame case.
