// Sn2PsoBytecodeDumper.hpp
//
// Dump every PSO's shader bytecode to disk at CreateGraphicsPipelineState /
// CreateComputePipelineState time. The dumped .dxbc files can be reflected
// with dxc to get authoritative root-signature parameter → semantic mapping
// (which root holds View, Material, Pass, etc.) instead of inferring from
// observed binding patterns.
//
// CONFIG
//   UEVR_SN2_PSO_BYTECODE_DIR=C:\tmp\dxbc   — output directory; missing = disabled
//
// FILES
//   {dir}\{crc8hex}.dxbc           — pixel shader bytecode (if present)
//   {dir}\{crc8hex}.vs.dxbc        — vertex shader (if present)
//   {dir}\{crc8hex}.cs.dxbc        — compute shader (if present)
//   {dir}\{crc8hex}.meta.json      — root-sig hash + RTV/DSV formats
//
// Skips duplicate (same CRC) shaders.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_set>

#include <d3d12.h>

namespace sn2_pso_bytecode_dumper {

inline bool env_enabled() {
    static const bool e = []() {
        return GetEnvironmentVariableW(L"UEVR_SN2_PSO_BYTECODE_DIR", nullptr, 0) > 0;
    }();
    return e;
}

// Hot path: called from create_graphics_pipeline_state / create_compute_pipeline_state.
// Dumps PS/VS bytecode (graphics) or CS bytecode (compute) keyed by their CRC32.
void on_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                            uint32_t ps_crc, uint32_t vs_crc);
void on_create_compute_pso(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                            uint32_t cs_crc);

// Newer streaming PSO API (CreatePipelineState with D3D12_PIPELINE_STATE_STREAM_DESC).
// Caller already decoded PS/VS/CS bytecode pointers from the stream.
void on_create_pso_stream(const void* ps_bytecode, size_t ps_size, uint32_t ps_crc,
                          const void* vs_bytecode, size_t vs_size, uint32_t vs_crc,
                          const void* cs_bytecode, size_t cs_size, uint32_t cs_crc);

}  // namespace sn2_pso_bytecode_dumper
