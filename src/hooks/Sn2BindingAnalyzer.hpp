// Sn2BindingAnalyzer.hpp
//
// Live runtime analyzer that observes SetGraphicsRootConstantBufferView /
// SetComputeRootConstantBufferView calls, decodes each GPU_VA to a
// (parent_resource, byte_offset) pair using sn2_upload_buf_map::resolve_va,
// and accumulates per-PSO, per-root LEFT vs RIGHT eye samples.
//
// On Present, the accumulator periodically emits a JSON report listing the
// observed LEFT/RIGHT byte-offset deltas for each (PSO, root_param) pair.
// This is the canonical View CB delta — pool-agnostic, so it works whether
// the engine allocated the buffer in pool A or pool B.
//
// USE
// ---
//
// 1. Set UEVR_SN2_BINDING_ANALYZER_JSON=C:\tmp\sn2_analyzer.json
// 2. Run the game for ~60 seconds in a scene that exercises both eyes
// 3. Read the JSON — it lists every PSO's correct per-eye CBV delta
// 4. Feed it into generate_dup_cfg.py to auto-emit a SAFE dup_cfg with
//    measured per-PSO deltas (no more guessing → no more crashes)
//
// SCHEMA
// ------
//
//   {
//     "schema": "uevr.sn2.binding_analyzer.v1",
//     "frame_count": 1234,
//     "psos": {
//       "0x9d14fcf0": {
//         "ps_crc": "0x9d14fcf0",
//         "cs_crc": "0x00000000",
//         "roots": {
//           "3": {
//             "parent_resource_ptr": "0x12345678",
//             "left_offsets": [0, 256, 512],
//             "right_offsets": [4096, 4352, 4608],
//             "computed_delta": 4096,
//             "samples_l": 47,
//             "samples_r": 21
//           }
//         }
//       }
//     }
//   }
//
// When samples_l > 0 and samples_r > 0 and the observed offsets are pairwise
// consistent, "computed_delta" is the byte offset to use for safe dup. When
// samples_r == 0, the PSO is LEFT-only (the engine never binds a right-eye
// CBV here) — this is the "needs writer-dup" case, and the offset for the
// synthesised right CB has to come from elsewhere (e.g. byte-patching LEFT
// content with right-eye matrices).

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <d3d12.h>

namespace sn2_binding_analyzer {

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_BINDING_ANALYZER_JSON");
        return v != nullptr && v[0] != '\0';
    }();
    return e;
}

inline const std::string& output_path() {
    static const std::string p = []() {
        const char* v = std::getenv("UEVR_SN2_BINDING_ANALYZER_JSON");
        return std::string{v != nullptr ? v : ""};
    }();
    return p;
}

inline uint64_t flush_every_frames() {
    static const uint64_t f = []() -> uint64_t {
        const char* v = std::getenv("UEVR_SN2_BINDING_ANALYZER_FLUSH_FRAMES");
        if (v == nullptr || *v == '\0') return 120;  // ~2s @ 60 fps
        char* end = nullptr;
        const auto n = std::strtoull(v, &end, 0);
        if (end == v) return 120;
        return n == 0 ? 120 : n;
    }();
    return f;
}

// One observation of a CBV binding.
struct CbvSample {
    uintptr_t pso{};
    uint32_t ps_crc{};
    uint32_t cs_crc{};
    uint32_t root_param{};
    int eye_bucket{-1};       // 0=left, 1=right, -1=unknown
    void* parent_resource{};  // ID3D12Resource* of the parent upload buffer
    uint64_t byte_offset{};   // offset within the parent
    uint64_t gpu_va{};        // raw VA for reference
    char stage{'G'};          // 'G'raphics or 'C'ompute
};

// Per-(PSO,root) accumulator. Keeps small per-eye offset sets so we can
// detect single-value or repeating-offset patterns + emit a stable delta.
struct PerRootAccum {
    void* representative_resource{};  // first non-null parent seen
    std::vector<uint64_t> left_offsets;
    std::vector<uint64_t> right_offsets;
    uint64_t samples_l{0};
    uint64_t samples_r{0};
    uint64_t samples_unknown{0};
    uint32_t ps_crc{};
    uint32_t cs_crc{};
    char stage{'G'};
};

// Public API.
//
// Record a CBV binding. Called from the existing SetGraphicsRootConstantBufferView /
// SetComputeRootConstantBufferView hooks AFTER the existing binding-ledger record.
void record_root_cbv(uintptr_t pso,
                     uint32_t ps_crc,
                     uint32_t cs_crc,
                     int eye_bucket,
                     uint32_t root_param,
                     D3D12_GPU_VIRTUAL_ADDRESS gpu_va,
                     char stage /* 'G' or 'C' */);

// Increment frame counter; flush to JSON when threshold reached.
void on_present();

// Force an immediate JSON write (for tests / shutdown).
void flush_now();

}  // namespace sn2_binding_analyzer
