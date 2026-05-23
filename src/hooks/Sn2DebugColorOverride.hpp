// Sn2DebugColorOverride.hpp
//
// Runtime "magenta PS" override. For configured PS CRCs, replaces the bound
// pipeline state with a substitute PSO whose pixel shader outputs a constant
// debug color (default magenta) to RT0. Visualizes EXACTLY which pixels a
// given PSO touches — the inverse of Sn2MagicInk (which makes them disappear).
//
// Usage:
//   UEVR_SN2_DEBUG_COLOR_OVERRIDE_FILE=C:\tmp\debug_color_override.txt
//     (file format: same as Sn2MagicInk skip file; live-reloaded)
//   UEVR_SN2_DEBUG_COLOR_OVERRIDE_EYE=left|right|both
//
//   UEVR_SN2_DEBUG_COLOR_RGB=255,0,255       (default magenta)
//
// IMPLEMENTATION
// --------------
// At PSO create time (CreateGraphicsPipelineState), record each PS CRC →
// {root sig, original PS desc fields} mapping.
//
// At draw time for a configured target PSO:
//   1. Look up cached debug-replacement PSO for this PSO ptr; if absent,
//      create one by cloning the original D3D12_GRAPHICS_PIPELINE_STATE_DESC
//      with the PS bytecode replaced by our compiled magenta shader.
//   2. Call SetPipelineState(replacement).
//   3. Original draw fires.
//   4. Restore original PSO after draw.
//
// Cached per-PSO so steady-state cost is ~one map lookup per affected draw.

#pragma once

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <wrl/client.h>

namespace sn2_debug_color {

bool env_enabled();
const std::string& override_file_path();
int target_eye_bucket();   // -1=both, 1=left, 2=right

// Live-reloadable set of PS CRCs to override.
std::unordered_set<uint32_t> override_crcs();

// True if (ps_crc, eye) is configured for override.
bool should_override(uint32_t ps_crc, int eye_bucket);

// Records a Stereo Forensics confirmation for matching color_override rules.
// Called only after a replacement PSO was successfully rebound for the draw.
void note_override_applied(uint32_t ps_crc, int eye_bucket, const char* kind);

// Called at CreateGraphicsPipelineState — caches the original desc for later
// cloning when override is requested. Lightweight (just stores by PSO ptr).
void note_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                              ID3D12PipelineState* pso);

// Get-or-create the magenta replacement PSO for a target PSO. Returns nullptr
// on failure (e.g., original desc unknown). Caches successfully created
// replacements indefinitely.
ID3D12PipelineState* get_or_create_replacement(ID3D12Device* device,
                                                ID3D12PipelineState* original);

}  // namespace sn2_debug_color
