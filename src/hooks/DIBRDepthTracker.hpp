#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

// Scene-depth discovery for the DIBR synthetic-stereo pass.
//
// UEVR's render-target-pool hook (the normal SceneDepthZ source) cannot
// install in every title - SN2's UE5 build defeats the FindFreeElement
// signature scan - and bind-time OMSetRenderTargets tracking proved both
// unreliable (UE5 renders mainly through render passes, which this fork does
// not hook) and expensive (it required the diagnostic command-list hooks).
//
// This version feeds ONLY from the always-installed device-level
// CreateDepthStencilView hook: every depth target the game creates is
// remembered (with vrmod depth_select-style rejects - MSAA, square
// shadow-atlas aspect, tiny surfaces) holding a reference, and the consumer
// selects the best candidate by exact backbuffer-dimension match + depth
// format tier. UE's scene depth always matches the swapchain extent
// (double-wide included), while shadow maps / cubemaps / UI depths don't.
namespace dibr_depth_tracker {
void record_dsv(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);

// Best candidate whose dimensions equal (width, height): highest depth
// format tier wins, newest wins within a tier. Returns nullptr when nothing
// matches (caller should skip synthesis for the frame).
Microsoft::WRL::ComPtr<ID3D12Resource> select_scene_depth(uint32_t width, uint32_t height);
} // namespace dibr_depth_tracker
