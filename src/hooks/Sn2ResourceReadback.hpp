// Sn2ResourceReadback.hpp
//
// At capture time, copy bytes of configured resources (or all mirrors) to
// readback heaps + write to disk. Closes a critical gap in our UEVR
// capture vs RD: RD captures all GPU memory; we capture pointers but not
// contents.
//
// USAGE
//   UEVR_SN2_RES_READBACK_DIR=<path>                     output dir
//   UEVR_SN2_RES_READBACK_MIRRORS=1                       capture all mirrored
//                                                          resources on trigger
//   UEVR_SN2_RES_READBACK_PTRS=0x...,0x...                 specific resource
//                                                          pointers to capture
//
// FLOW
// ----
// 1. Sn2FrameCapture::execute_capture calls
//    sn2_resource_readback::trigger(seq) at frame capture moment.
// 2. We iterate the configured set of resources.
// 3. For each: queue a CopyResource / CopyTextureRegion to our readback
//    buffer (deferred — must run on a CL we can execute).
// 4. On next-frame Present, after GPU has signaled the fence, drain the
//    readback bytes to disk.
//
// OUTPUT FORMAT
//   <out_dir>/res_<seq>_0x<ptr>_<width>x<height>x<depth>_fmt<N>.bin
//   + <out_dir>/res_<seq>_0x<ptr>_meta.json
//
// CAVEAT
// ------
// Reading large resources (e.g., 1280×720 R11G11B10F = ~3.5MB) every capture
// can be expensive. Default-disabled; opt in via env.

#pragma once

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <unordered_set>

namespace sn2_resource_readback {

bool env_enabled();
const std::string& output_dir();
bool capture_all_mirrors();
const std::unordered_set<ID3D12Resource*>& explicit_ptrs();

// Init: allocate readback buffer pool + fence. Idempotent.
bool init(ID3D12Device* device);

// Trigger at next available command list opportunity: queue copies of
// configured resources. Returns number of resources queued.
size_t trigger(uint64_t seq);

// Called on Present: drain completed readbacks to disk.
void on_present();

// How many bytes have been written so far (cumulative across captures).
uint64_t bytes_written_total();

}  // namespace sn2_resource_readback
