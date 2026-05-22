// Sn2GpuCounters.hpp
//
// Per-PSO GPU timing via D3D12 timestamp queries.
//
// At each draw, end-of-draw timestamp is recorded into a query heap. After
// the command list executes, timestamps are resolved to a CPU buffer and we
// compute per-PSO accumulated GPU time.
//
// USAGE
//   UEVR_SN2_GPU_COUNTERS=1
//   UEVR_SN2_GPU_COUNTERS_DUMP=C:\tmp\gpu_counters.json
//   UEVR_SN2_GPU_COUNTERS_DUMP_EVERY_N_FRAMES=600
//
// OUTPUT FORMAT (JSON)
// {
//   "frame_count": 600,
//   "psos": {
//     "0x9d14fcf0": {
//       "draws": 4267,
//       "total_gpu_ticks": 12345678,
//       "total_gpu_ms": 0.234,
//       "ticks_per_draw_avg": 2893
//     },
//     ...
//   }
// }

#pragma once

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace sn2_gpu_counters {

bool env_enabled();
const std::string& output_path();
uint64_t dump_every_n_frames();

// Init: allocate query heap + readback buffer. Idempotent.
bool init(ID3D12Device* device, ID3D12CommandQueue* queue);

// Per-draw: record begin/end timestamp around the draw.
// Returns query index pair so the caller can resolve later. Currently
// simplified: just records end-of-draw timestamp per draw.
void record_draw_end(ID3D12GraphicsCommandList* cl, uint32_t ps_crc);

// Called from Present: resolve completed timestamps + accumulate per-PSO.
void on_present();

}  // namespace sn2_gpu_counters
