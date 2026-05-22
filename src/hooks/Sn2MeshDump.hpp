// Sn2MeshDump.hpp
//
// For configured PSOs at draw time, dump vertex + index buffer ranges to disk
// + JSON describing input layout. Offline Python tool converts to OBJ/PLY/GLTF
// for inspection in any 3D viewer.
//
// USAGE
//   UEVR_SN2_MESH_DUMP_DIR=C:\tmp\mesh_dumps
//   UEVR_SN2_MESH_DUMP_PS_CRCS=0x9d14fcf0
//   UEVR_SN2_MESH_DUMP_MAX=8
//
// OUTPUT
//   mesh_<ps_crc>_<seq>.json   metadata: input layout, vertex stride, indices
//   mesh_<ps_crc>_<seq>_vb.bin (best effort: vertex bytes)
//   mesh_<ps_crc>_<seq>_ib.bin (best effort: index bytes)
//
// NOTE: getting actual VB/IB bytes at draw time requires resolved CPU access
// to GPU resources (readback). For now we dump metadata (resource pointers,
// stride, format, draw counts) and leave the byte capture as a future step.

#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <unordered_set>

namespace sn2_mesh_dump {

bool env_enabled();
const std::string& output_dir();
uint64_t max_dumps_per_pso();
const std::unordered_set<uint32_t>& target_ps_crcs();

bool should_dump(uint32_t ps_crc);

// Called at draw_indexed_instanced. Writes a JSON file describing the bind
// state of vertex/index buffers + draw count.
void note_draw(
    uint32_t ps_crc,
    void* pso_ptr,
    UINT index_count, UINT instance_count, UINT start_index, INT base_vertex,
    const D3D12_VERTEX_BUFFER_VIEW* vbs, UINT vb_count,
    const D3D12_INDEX_BUFFER_VIEW* ib);

}  // namespace sn2_mesh_dump
