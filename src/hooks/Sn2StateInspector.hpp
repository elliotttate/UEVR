// Sn2StateInspector.hpp
//
// On-demand "what is bound at THIS draw" inspector. Writes a structured JSON
// snapshot per matching draw with every CBV/SRV/UAV/RTV root binding +
// resolved resource pointer + dimensions + format + GPU_VA.
//
// USAGE
//   UEVR_SN2_STATE_INSPECTOR_DIR=C:\tmp\state_inspect
//   UEVR_SN2_STATE_INSPECTOR_PS_CRCS=0x9d14fcf0,0x37558de4
//   UEVR_SN2_STATE_INSPECTOR_MAX=64       (default 32 dumps per CRC then stop)
//   UEVR_SN2_STATE_INSPECTOR_EYE=both|left|right
//
// INTEGRATION
//   D3D12Hook.cpp's draw hook should:
//     1. const bool dump = sn2_state_inspector::should_dump(ps_crc, eye_bucket);
//     2. If dump, build a small BindingEntry array from the CL correlation state
//     3. Call sn2_state_inspector::write_snapshot(ps_crc, eye, ..., entries, count);

#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_set>
#include <d3d12.h>

namespace sn2_state_inspector {

bool env_enabled();
const std::string& output_dir();
const std::unordered_set<uint32_t>& target_ps_crcs();
int target_eye_bucket();   // -1 = both, 1 = left, 2 = right
uint64_t max_dumps_per_crc();

// Returns true if (ps_crc, eye_bucket) matches the configured filter AND the
// per-CRC dump counter hasn't exceeded the cap. Bumps the counter when it
// returns true.
bool should_dump(uint32_t ps_crc, int eye_bucket);

// A single binding entry passed from the caller.
struct BindingEntry {
    uint8_t kind;     // 0=CBV 1=SRV 2=UAV 3=RTV 4=DSV 5=root_desc_table
                      // 6=root_cbv 7=root_srv 8=root_uav
    uint8_t slot;     // root param index or RTV slot
    uint64_t gpu_va;  // GPU VA for buffers; descriptor GPU handle for tables
    ID3D12Resource* resource;  // resolved resource (may be null)
};

// Serializes the snapshot to <output_dir>/state_<crc>_eye<N>_seq<NNNN>.json.
void write_snapshot(
    uint32_t ps_crc,
    int eye_bucket,
    void* pso_ptr,
    uint32_t viewport_x, uint32_t viewport_y,
    uint32_t viewport_w, uint32_t viewport_h,
    uint32_t rtv_count,
    ID3D12Resource** rtv_resources,
    const BindingEntry* graphics_bindings,
    size_t graphics_binding_count);

}  // namespace sn2_state_inspector
