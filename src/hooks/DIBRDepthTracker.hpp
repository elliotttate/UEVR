#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>
#include <wrl.h>

// Scene-depth discovery for the DIBR synthetic-stereo pass.
//
// UEVR's render-target-pool hook (the normal SceneDepthZ source) cannot
// install in every title - SN2's UE5 build defeats the FindFreeElement
// signature scan.
//
// Candidates feed from the always-installed device-level
// CreateDepthStencilView hook: every depth target the game creates is
// remembered (with vrmod depth_select-style rejects - MSAA, square
// shadow-atlas aspect, tiny surfaces) holding a reference. LIVENESS feeds
// from always-installed bind hooks (OMSetRenderTargets + the GCL4
// BeginRenderPass path UE5's RHI actually uses): a candidate counts as live
// when its DSV was bound within the last couple of presented frames.
// Selection prefers live candidates first - creation-time ranking alone
// proved non-deterministic (it could latch onto a frozen loading-screen
// depth that matched the swapchain extent) - then exact backbuffer extent,
// area, depth-format tier, and recency.
namespace dibr_depth_tracker {
void record_dsv(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);

// Bind-time liveness signal (OMSetRenderTargets / BeginRenderPass). Cheap:
// one mutex-guarded map probe; unknown descriptors are ignored.
void record_dsv_bind(D3D12_CPU_DESCRIPTOR_HANDLE descriptor);

// Best LIVE candidate for the backbuffer: accepts BOTH double-wide
// (full_width x height, two packed views) and single-eye (eye_width x height)
// shaped depth targets - in single-view rendering UE allocates SceneDepthZ at
// the lone view's extent, so an aspect filter against the double-wide target
// alone rejects the real depth. Falls back to stale candidates only when no
// live one qualifies. Returns nullptr when nothing matches (caller should
// skip synthesis for the frame). Also advances the per-present liveness
// window - call once per presented frame.
Microsoft::WRL::ComPtr<ID3D12Resource> select_scene_depth(uint32_t full_width, uint32_t eye_width, uint32_t height);

// Diagnostic snapshot of the current candidates (extent/format/bind stats),
// for periodic logging by the consumer.
std::string describe_candidates();
} // namespace dibr_depth_tracker
