// Sn2FrameCppExport.hpp
//
// Captures one frame's D3D12 command stream and emits a self-contained C++
// source file that replays the same sequence. Modeled after RenderDoc's
// export_cpp.py (util/automation/export_cpp.py).
//
// CONFIG
//   UEVR_SN2_FRAME_CAPTURE_DIR=C:\tmp\frame_capture  — output dir, missing = disabled
//   UEVR_SN2_FRAME_CAPTURE_TRIGGER=key|frame=N        — trigger source (default: 'frame=300')
//
// FLOW
// ----
// 1. On first present, register a CAPTURE_PENDING flag
// 2. When trigger fires, start RECORDING — every D3D12 call from then through
//    next Present is appended to a thread-local event list
// 3. On Present, finalize: emit main.cpp / capture_frame.cpp / CMakeLists.txt
//    / shaders/*.cso / blobs/*.bin
//
// SCOPE
// -----
// First pass covers the most useful operations for SN2 fog debugging:
//   - CreateCommittedResource (UPLOAD + DEFAULT buffers + textures)
//   - CreateRootSignature
//   - CreateGraphicsPipelineState / CreateComputePipelineState
//   - CreateDescriptorHeap
//   - CreateConstantBufferView / SRV / UAV
//   - CopyDescriptors / CopyDescriptorsSimple
//   - Command list recording: SetPipelineState, SetRoot*, draw, dispatch,
//     copy, resource barriers, viewports/scissors
//   - ExecuteCommandLists, Present
//
// Unhandled chunks are emitted as /* TODO unhandled: ChunkName */ to keep
// the file structurally compilable.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

#include <d3d12.h>

namespace sn2_frame_cpp_export {

bool env_enabled();
const char* output_dir();

// Begin recording at the next D3D12 call. Idempotent.
void arm_capture();

// Returns true if we're currently recording (between arm + finalize).
bool is_recording();

// Per-call hooks. Each emits a "chunk" into the in-progress capture if
// recording is active. Cheap (no-op) when not recording.
void on_create_committed_resource(const D3D12_HEAP_PROPERTIES* heap_props,
                                  D3D12_HEAP_FLAGS heap_flags,
                                  const D3D12_RESOURCE_DESC* desc,
                                  D3D12_RESOURCE_STATES initial_state,
                                  ID3D12Resource* resource);
void on_create_graphics_pipeline_state(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                                       ID3D12PipelineState* pso);
void on_create_compute_pipeline_state(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                                      ID3D12PipelineState* pso);
void on_draw_indexed_instanced(ID3D12GraphicsCommandList* cl,
                               UINT index_count, UINT instance_count,
                               UINT start_index, INT base_vertex, UINT start_instance);
void on_dispatch(ID3D12GraphicsCommandList* cl,
                 UINT tg_x, UINT tg_y, UINT tg_z);
void on_set_graphics_root_cbv(ID3D12GraphicsCommandList* cl,
                              UINT root_param, D3D12_GPU_VIRTUAL_ADDRESS va);
void on_set_compute_root_cbv(ID3D12GraphicsCommandList* cl,
                             UINT root_param, D3D12_GPU_VIRTUAL_ADDRESS va);

// Called on Present. Finalizes the current capture if recording.
void on_present();

}  // namespace sn2_frame_cpp_export
