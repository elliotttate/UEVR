#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace sn2_runtime_state {

constexpr size_t kDraw13BCbvRootCount = 16;

struct Draw13BCbvProbe {
    uint64_t gpu_va{};
    uint64_t hash{};
    uint32_t bytes{};
    uint32_t cb321x{};
    float reg204_w{};
    int32_t first_640_float_off{-1};
    int32_t first_720_float_off{-1};
    int32_t first_1280_float_off{-1};
    bool mapped{};
    bool has_reg204_w{};
    bool has_cb321x{};
};

struct Draw13BLast {
    uint64_t seq{};
    uint64_t eye_seq{};
    int eye{};
    uint64_t pso{};
    uint64_t effective_pso{};
    uint32_t vs_crc{};
    uint32_t gs_crc{};
    uint32_t index_count{};
    uint32_t instance_count{};
    uint32_t start_index{};
    int32_t base_vertex{};
    uint32_t start_instance{};
    uint32_t topology{};
    float viewport_x{};
    float viewport_y{};
    float viewport_w{};
    float viewport_h{};
    int32_t scissor_l{};
    int32_t scissor_t{};
    int32_t scissor_r{};
    int32_t scissor_b{};
    uint64_t rt0_resource{};
    uint64_t rt0_width{};
    uint32_t rt0_height{};
    uint32_t rt0_depth{};
    uint32_t rt0_format{};
    uint64_t ib_va{};
    uint32_t ib_size{};
    uint32_t ib_format{};
    uint64_t vb0_va{};
    uint32_t vb0_size{};
    uint32_t vb0_stride{};
    uint64_t root_signature{};
    uint64_t cb4{};
    uint64_t cb5{};
    uint64_t cb6{};
    uint64_t cb7{};
    uint64_t table0{};
    uint64_t table1{};
    uint64_t table2{};
    uint64_t table3{};
    std::array<Draw13BCbvProbe, kDraw13BCbvRootCount> cbv_probe{};
    uint64_t rdg_pass{};
    uint64_t submit_cmd{};
    uint64_t submit_scene_args{};
    uint64_t submit_rhi{};
    uint32_t submit_instance_factor{};
    uint32_t submit_primitive_id_offset{};
    uint32_t submit_indirect_args_byte_offset{};
    uint32_t submit_batched_primitive_slot{};
    uint64_t submit_primitive_ids_buffer{};
    uint64_t submit_indirect_args_buffer{};
    uint64_t submit_cmd_index_buffer{};
    uint64_t submit_cmd_cached_pipeline_id{};
    uint32_t submit_cmd_first_index{};
    uint32_t submit_cmd_num_primitives{};
    uint32_t submit_cmd_num_instances{};
    int32_t submit_cmd_primitive_id_stream_index{-1};
    uint64_t rhi_cmd{};
    uint64_t rhi_cmd_list{};
    uint32_t rhi_payload_offset{};
    uint64_t rhi_index_buffer{};
    int32_t rhi_base_vertex{};
    uint32_t rhi_first_instance{};
    uint32_t rhi_num_vertices{};
    uint32_t rhi_start_index{};
    uint32_t rhi_num_primitives{};
    uint32_t rhi_num_instances{};
    std::string src;
    std::string stack;
};

struct AddMeshLast {
    uint64_t seq{};
    int processor_kind{}; // 1=BasePass, 2=SLW
    int stereo_pass{};
    int mesh_pass{};
    uint64_t processor{};
    uint64_t view{};
    uint64_t mesh{};
    uint64_t primitive{};
    uint64_t material_proxy{};
    uint64_t primitive_vtable_rva{};
    uint64_t material_vtable_rva{};
    int32_t static_mesh_id{};
    uint64_t batch_element_mask{};
    uint32_t direct_hit_value{};
    uint32_t direct_hit_offset{};
    uint32_t element_hit_value{};
    uint32_t element_hit_offset{};
};

struct ViewCommandsLast {
    uint64_t seq{};
    int view_slot{};
    int stereo_pass{};
    int mesh_pass{};
    uint64_t view{};
    uint64_t view_commands{};
    int32_t mesh_commands_count{};
    int32_t build_request_count{};
    bool mesh_shape_hit{};
    uint32_t mesh_hits{};
    uint64_t mesh_draw_command{};
    uint32_t max_num_primitives{};
    uint32_t max_elem{};
    uint64_t max_cmd{};
};

struct SubmitDrawLast {
    uint64_t seq{};
    uint64_t cmd{};
    uint64_t scene_args{};
    uint64_t rhi{};
    uint32_t instance_factor{};
    uint32_t first_index{};
    uint32_t num_primitives{};
    uint32_t num_instances{};
    uint64_t index_buffer{};
    uint64_t cached_pipeline_id{};
    int32_t primitive_id_stream_index{-1};
    uint32_t primitive_id_offset{};
    uint32_t indirect_args_byte_offset{};
    uint32_t batched_primitive_slot{};
    uint64_t primitive_ids_buffer{};
    uint64_t indirect_args_buffer{};
    std::string stack;
};

struct SubmitDrawTls {
    bool active{};
    uint64_t cmd{};
    uint64_t scene_args{};
    uint64_t rhi{};
    uint32_t instance_factor{};
    uint32_t first_index{};
    uint32_t num_primitives{};
    uint32_t num_instances{};
    uint64_t index_buffer{};
    uint64_t cached_pipeline_id{};
    int32_t primitive_id_stream_index{-1};
    uint32_t primitive_id_offset{};
    uint32_t indirect_args_byte_offset{};
    uint32_t batched_primitive_slot{};
    uint64_t primitive_ids_buffer{};
    uint64_t indirect_args_buffer{};
};

struct RhiDrawLast {
    uint64_t seq{};
    uint64_t cmd{};
    uint64_t cmd_list{};
    uint32_t payload_offset{};
    uint64_t index_buffer{};
    int32_t base_vertex{};
    uint32_t first_instance{};
    uint32_t num_vertices{};
    uint32_t start_index{};
    uint32_t num_primitives{};
    uint32_t num_instances{};
    std::string stack;
};

struct RhiDrawTls {
    bool active{};
    uint64_t cmd{};
    uint64_t cmd_list{};
    uint32_t payload_offset{};
    uint64_t index_buffer{};
    int32_t base_vertex{};
    uint32_t first_instance{};
    uint32_t num_vertices{};
    uint32_t start_index{};
    uint32_t num_primitives{};
    uint32_t num_instances{};
};

inline std::atomic<uint64_t> draw13_total{0};
inline std::atomic<uint64_t> draw13_left{0};
inline std::atomic<uint64_t> draw13_right{0};
inline std::atomic<uint64_t> draw13_unknown{0};
inline std::atomic<uint64_t> draw13_seen_total{0};
inline std::atomic<uint64_t> draw13_seen_left{0};
inline std::atomic<uint64_t> draw13_seen_right{0};
inline std::atomic<uint64_t> draw13_seen_unknown{0};
inline std::atomic<uint64_t> addmesh_basepass_total{0};
inline std::atomic<uint64_t> addmesh_basepass_left{0};
inline std::atomic<uint64_t> addmesh_basepass_right{0};
inline std::atomic<uint64_t> addmesh_basepass_unknown{0};
inline std::atomic<uint64_t> addmesh_slw_total{0};
inline std::atomic<uint64_t> addmesh_slw_left{0};
inline std::atomic<uint64_t> addmesh_slw_right{0};
inline std::atomic<uint64_t> addmesh_slw_unknown{0};
inline std::atomic<uint64_t> viewcommands_total{0};
inline std::atomic<uint64_t> viewcommands_shape_hits{0};
inline std::atomic<uint64_t> submitdraw_target_total{0};
inline std::atomic<uint64_t> rhi_draw_target_total{0};

inline std::mutex state_mutex;
inline Draw13BLast last_draw13{};
inline AddMeshLast last_addmesh{};
inline ViewCommandsLast last_viewcommands{};
inline SubmitDrawLast last_submitdraw{};
inline RhiDrawLast last_rhi_draw{};
inline thread_local SubmitDrawTls tls_submit_draw{};
inline thread_local RhiDrawTls tls_rhi_draw{};

inline uint64_t note_draw13b_seen(int eye) {
    const uint64_t seq = draw13_seen_total.fetch_add(1, std::memory_order_relaxed) + 1;
    if (eye == 1) {
        draw13_seen_left.fetch_add(1, std::memory_order_relaxed);
    } else if (eye == 2) {
        draw13_seen_right.fetch_add(1, std::memory_order_relaxed);
    } else {
        draw13_seen_unknown.fetch_add(1, std::memory_order_relaxed);
    }
    return seq;
}

inline void note_draw13b(Draw13BLast row) {
    const uint64_t seq = draw13_total.fetch_add(1, std::memory_order_relaxed) + 1;
    row.seq = seq;
    if (row.eye == 1) {
        draw13_left.fetch_add(1, std::memory_order_relaxed);
    } else if (row.eye == 2) {
        draw13_right.fetch_add(1, std::memory_order_relaxed);
    } else {
        draw13_unknown.fetch_add(1, std::memory_order_relaxed);
    }
    std::scoped_lock lock{state_mutex};
    last_draw13 = std::move(row);
}

inline void note_addmesh_shape(AddMeshLast row) {
    const bool basepass = row.processor_kind == 1;
    auto& total = basepass ? addmesh_basepass_total : addmesh_slw_total;
    auto& left = basepass ? addmesh_basepass_left : addmesh_slw_left;
    auto& right = basepass ? addmesh_basepass_right : addmesh_slw_right;
    auto& unknown = basepass ? addmesh_basepass_unknown : addmesh_slw_unknown;
    row.seq = total.fetch_add(1, std::memory_order_relaxed) + 1;
    if (row.stereo_pass == 1) {
        left.fetch_add(1, std::memory_order_relaxed);
    } else if (row.stereo_pass == 2) {
        right.fetch_add(1, std::memory_order_relaxed);
    } else {
        unknown.fetch_add(1, std::memory_order_relaxed);
    }
    std::scoped_lock lock{state_mutex};
    last_addmesh = row;
}

inline void note_viewcommands(ViewCommandsLast row) {
    row.seq = viewcommands_total.fetch_add(1, std::memory_order_relaxed) + 1;
    if (row.mesh_shape_hit) {
        viewcommands_shape_hits.fetch_add(1, std::memory_order_relaxed);
    }
    std::scoped_lock lock{state_mutex};
    last_viewcommands = row;
}

inline void note_submitdraw_target(SubmitDrawLast row) {
    row.seq = submitdraw_target_total.fetch_add(1, std::memory_order_relaxed) + 1;
    std::scoped_lock lock{state_mutex};
    last_submitdraw = std::move(row);
}

inline void note_rhi_draw_target(RhiDrawLast row) {
    row.seq = rhi_draw_target_total.fetch_add(1, std::memory_order_relaxed) + 1;
    std::scoped_lock lock{state_mutex};
    last_rhi_draw = std::move(row);
}

} // namespace sn2_runtime_state
