// Sn2DescriptorLineage.hpp
//
// Tracks the FULL lineage of every D3D12 descriptor: from its CreateXxxView
// site, through every CopyDescriptors operation, to its bound location at
// draw/dispatch time. Helps answer "where did this binding ultimately come
// from?" when the descriptor registry's direct lookup returns null.
//
// USAGE
//   UEVR_SN2_DESCRIPTOR_LINEAGE=1
//   UEVR_SN2_DESCRIPTOR_LINEAGE_DUMP=C:\tmp\desc_lineage.json
//   UEVR_SN2_DESCRIPTOR_LINEAGE_DUMP_EVERY_N_FRAMES=600

#pragma once

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sn2_descriptor_lineage {

enum class CreateKind : uint8_t {
    CBV, SRV, UAV, RTV, DSV, Unknown
};

struct LineageEntry {
    CreateKind kind;
    ID3D12Resource* resource;  // original resource
    uint64_t create_frame;
    uint64_t create_event;
    // Lineage chain: every CPU handle this entry was copied into.
    std::vector<SIZE_T> copy_chain;
};

bool env_enabled();
const std::string& output_path();
uint64_t dump_every_n_frames();

// Hook entries.
void note_create(CreateKind kind, ID3D12Resource* res, SIZE_T cpu_handle);
void note_copy(SIZE_T src, SIZE_T dst);
void note_copy_range(SIZE_T src_start, SIZE_T dst_start, UINT count, UINT stride);

// Reverse lookup: given a bound CPU handle, walk back the chain to find the
// original resource. Returns nullptr if no path.
ID3D12Resource* trace_back(SIZE_T cpu_handle);

// Get the full lineage for a given handle (for diagnostic display).
const LineageEntry* lookup_lineage(SIZE_T cpu_handle);

// Per-Present: flush lineage map to disk every N frames.
void on_present();

}  // namespace sn2_descriptor_lineage
