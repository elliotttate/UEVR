#include "render/RenderDiagnosticsCAPI.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Framework.hpp"
#include "hooks/D3D12Hook.hpp"
#include "hooks/Sn2CaptureSidecar.hpp"
#include "mods/RenderInspector.hpp"
#include "mods/VR.hpp"
#include "mods/vr/CVarManager.hpp"
#include "mods/vr/D3D12Component.hpp"
#include "render/D3D12Diagnostics.hpp"
#include "render/FrameResourceInspector.hpp"
#include "render/RenderAnalysisExport.hpp"
#include "render/RenderDocCaptureService.hpp"
#include "render/ShaderOverrideRegistry.hpp"
#include "render/StereoForensics.hpp"

using json = nlohmann::json;
template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

extern "C" int sn2_get_current_fog_view();
extern "C" void sn2_capture_truth_on_renderdoc_trigger(uint64_t seq);

namespace {

thread_local std::string g_capi_return_buffer{};

const char* publish(json value) {
    g_capi_return_buffer = value.dump();
    return g_capi_return_buffer.c_str();
}

const char* publish(std::string value) {
    g_capi_return_buffer = std::move(value);
    return g_capi_return_buffer.c_str();
}

std::string format_pointer(uintptr_t pointer) {
    std::ostringstream ss{};
    ss << "0x" << std::hex << std::uppercase << pointer;
    return ss.str();
}

std::string format_crc32(uint32_t value) {
    std::ostringstream ss{};
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return ss.str();
}

int64_t steady_age_ms(
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point timestamp
) {
    if (timestamp.time_since_epoch().count() == 0) {
        return -1;
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(now - timestamp).count();
}

const char* backend_name(render::FrameResourceInspector::Backend backend) {
    return backend == render::FrameResourceInspector::Backend::D3D12 ? "D3D12" : "D3D11";
}

const char* backend_name(render::ShaderOverrideRegistry::Backend backend) {
    return backend == render::ShaderOverrideRegistry::Backend::D3D12 ? "D3D12" : "D3D11";
}

const char* stage_name(render::ShaderOverrideRegistry::Stage stage) {
    switch (stage) {
    case render::ShaderOverrideRegistry::Stage::Vertex: return "VS";
    case render::ShaderOverrideRegistry::Stage::Pixel: return "PS";
    case render::ShaderOverrideRegistry::Stage::Geometry: return "GS";
    case render::ShaderOverrideRegistry::Stage::Compute: return "CS";
    case render::ShaderOverrideRegistry::Stage::Amplification: return "AS";
    case render::ShaderOverrideRegistry::Stage::Mesh: return "MS";
    default: return "Unknown";
    }
}

template <typename Vec>
void apply_limit(Vec& v, int limit) {
    if (limit > 0 && static_cast<size_t>(limit) < v.size()) {
        v.resize(static_cast<size_t>(limit));
    }
}

json resource_to_json(const render::FrameResourceInspector::ResourceInfo& r) {
    return {
        {"key", r.key},
        {"pointer", format_pointer(r.pointer)},
        {"backend", backend_name(r.backend)},
        {"name", r.name},
        {"source", r.source},
        {"type", r.type},
        {"format", r.format},
        {"resolution", r.resolution},
        {"tags", r.tags},
        {"width", r.width},
        {"height", r.height},
        {"first_seen_frame", r.first_seen_frame},
        {"last_seen_frame", r.last_seen_frame},
        {"seen_count", r.seen_count},
        {"change_count", r.change_count},
        {"is_depth", r.is_depth},
        {"is_render_target", r.is_render_target},
        {"is_ui", r.is_ui},
        {"is_swapchain", r.is_swapchain},
        {"is_eye", r.is_eye},
        {"is_velocity_candidate", r.is_velocity_candidate},
        {"is_rt_pool", r.is_rt_pool},
        {"is_transient", r.is_transient},
        {"is_recent", r.is_recent},
    };
}

json heap_to_json(const render::D3D12Diagnostics::HeapInfo& h) {
    return {
        {"pointer", format_pointer(h.pointer)},
        {"name", h.name},
        {"source", h.source},
        {"type", h.type},
        {"total_descriptors", h.total_descriptors},
        {"estimated_in_use", h.estimated_in_use},
        {"first_seen_frame", h.first_seen_frame},
        {"last_seen_frame", h.last_seen_frame},
        {"bind_count", h.bind_count},
        {"shader_visible", h.shader_visible},
        {"transient", h.transient},
        {"is_active", h.is_active},
    };
}

json bound_target_to_json(const render::D3D12Diagnostics::BoundTargetInfo& t) {
    return {
        {"handle", format_pointer(t.handle)},
        {"resource", format_pointer(t.resource)},
        {"name", t.name},
        {"descriptor_type", t.descriptor_type},
    };
}

json binding_to_json(const render::D3D12Diagnostics::BindingEvent& e) {
    json j{{"frame", e.frame}, {"source", e.source}, {"kind", e.kind}, {"detail", e.detail}};
    if (!e.render_targets.empty()) {
        j["render_targets"] = json::array();
        for (const auto& t : e.render_targets) {
            j["render_targets"].push_back(bound_target_to_json(t));
        }
    }
    if (e.depth_target.has_value()) {
        j["depth_target"] = bound_target_to_json(*e.depth_target);
    }
    return j;
}

json barrier_to_json(const render::D3D12Diagnostics::BarrierEvent& e) {
    return {
        {"frame", e.frame},
        {"source", e.source},
        {"resource", format_pointer(e.resource)},
        {"type", e.type},
        {"before_state", e.before_state},
        {"after_state", e.after_state},
        {"subresource", e.subresource},
        {"note", e.note},
    };
}

json warning_to_json(const render::D3D12Diagnostics::WarningEvent& e) {
    return {{"frame", e.frame}, {"source", e.source}, {"message", e.message}};
}

json pipeline_cache_event_to_json(const render::D3D12Diagnostics::PipelineCacheEvent& e) {
    return {
        {"frame", e.frame},
        {"source", e.source},
        {"action", e.action},
        {"device", format_pointer(e.device)},
        {"library", format_pointer(e.library)},
        {"pipeline_state", format_pointer(e.pipeline_state)},
        {"name", e.name},
        {"cached_blob_size", e.cached_blob_size},
        {"has_cached_pso", e.has_cached_pso},
        {"stripped_cached_pso", e.stripped_cached_pso},
        {"result", format_pointer(static_cast<uintptr_t>(e.result))},
        {"note", e.note},
    };
}

template <typename Array>
json root_slot_array_to_json(const Array& values) {
    json result = json::array();
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] == 0) {
            continue;
        }

        result.push_back({
            {"slot", i},
            {"value", format_pointer(static_cast<uintptr_t>(values[i]))},
        });
    }
    return result;
}

template <typename Array>
json root_hash_array_to_json(const Array& values) {
    json result = json::array();
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] == 0) {
            continue;
        }

        result.push_back({
            {"slot", i},
            {"hash", format_pointer(static_cast<uintptr_t>(values[i]))},
        });
    }
    return result;
}

json root_bind_to_json(const render::D3D12Diagnostics::RootBindEvent& e) {
    return {
        {"frame", e.frame},
        {"sequence", e.sequence},
        {"source", e.source},
        {"pipeline", e.pipeline},
        {"kind", e.kind},
        {"command_list", format_pointer(e.command_list)},
        {"pipeline_state", format_pointer(e.pipeline_state)},
        {"eye_bucket", e.eye_bucket},
        {"root_parameter", e.root_parameter},
        {"value", format_pointer(e.value)},
        {"value_count", e.value_count},
        {"value_hash", format_pointer(static_cast<uintptr_t>(e.value_hash))},
    };
}

json resource_write_to_json(const render::D3D12Diagnostics::ResourceWriteInfo& w) {
    return {
        {"target_index", w.target_index},
        {"descriptor", format_pointer(w.descriptor)},
        {"resource", format_pointer(w.resource)},
        {"name", w.name},
        {"kind", w.kind},
        {"prior_producer_frame", w.prior_producer_frame},
        {"prior_producer_draw", w.prior_producer_draw},
        {"prior_producer_pso", format_pointer(w.prior_producer_pso)},
        {"prior_producer_command_list", format_pointer(w.prior_producer_command_list)},
        {"prior_producer_kind", w.prior_producer_kind},
        {"prior_producer_descriptor", format_pointer(w.prior_producer_descriptor)},
        {"prior_producer_target_index", w.prior_producer_target_index},
        {"prior_producer_eye_bucket", w.prior_producer_eye_bucket},
    };
}

json draw_event_to_json(const render::D3D12Diagnostics::DrawEvent& e) {
    json descriptor_reads = json::array();
    for (const auto& read : e.descriptor_reads) {
        descriptor_reads.push_back({
            {"root_parameter", read.root_parameter},
            {"descriptor_index", read.descriptor_index},
            {"descriptor_cpu", format_pointer(read.descriptor_cpu)},
            {"descriptor_source_cpu", format_pointer(read.descriptor_source_cpu)},
            {"descriptor_source_frame", read.descriptor_source_frame},
            {"resource", format_pointer(read.resource)},
            {"descriptor_type", read.descriptor_type},
            {"producer_frame", read.producer_frame},
            {"producer_draw", read.producer_draw},
            {"producer_pso", format_pointer(read.producer_pso)},
            {"producer_command_list", format_pointer(read.producer_command_list)},
            {"producer_kind", read.producer_kind},
            {"producer_descriptor", format_pointer(read.producer_descriptor)},
            {"producer_target_index", read.producer_target_index},
            {"producer_eye_bucket", read.producer_eye_bucket},
        });
    }

    json render_target_writes = json::array();
    for (const auto& write : e.render_target_writes) {
        render_target_writes.push_back(resource_write_to_json(write));
    }

    json uav_writes = json::array();
    for (const auto& write : e.uav_writes) {
        uav_writes.push_back(resource_write_to_json(write));
    }

    return {
        {"frame", e.frame},
        {"draw_index", e.draw_index},
        {"source", e.source},
        {"kind", e.kind},
        {"command_list", format_pointer(e.command_list)},
        {"pipeline_state", format_pointer(e.pipeline_state)},
        {"root_signature", format_pointer(e.root_signature)},
        {"eye_bucket", e.eye_bucket},
        {"executed", e.executed},
        {"skipped", !e.executed},
        {"has_viewport", e.has_viewport},
        {"viewport_count", e.viewport_count},
        {"viewport0", {
            {"x", e.viewport_top_left_x},
            {"y", e.viewport_top_left_y},
            {"w", e.viewport_width},
            {"h", e.viewport_height},
        }},
        {"has_scissor", e.has_scissor},
        {"scissor_count", e.scissor_count},
        {"scissor0", {
            {"left", e.scissor_left},
            {"top", e.scissor_top},
            {"right", e.scissor_right},
            {"bottom", e.scissor_bottom},
        }},
        {"arg0", e.arg0},
        {"arg1", e.arg1},
        {"arg2", e.arg2},
        {"arg3", e.arg3},
        {"arg4", e.arg4},
        {"copy_dst_byte_offset", e.copy_dst_byte_offset},
        {"copy_src_byte_offset", e.copy_src_byte_offset},
        {"copy_byte_count", e.copy_byte_count},
        {"rtv0", format_pointer(e.rtv0)},
        {"rtv0_resource", format_pointer(e.rtv0_resource)},
        {"prior_rtv0_producer_frame", e.prior_rtv0_producer_frame},
        {"prior_rtv0_producer_draw", e.prior_rtv0_producer_draw},
        {"prior_rtv0_producer_pso", format_pointer(e.prior_rtv0_producer_pso)},
        {"render_target_writes", std::move(render_target_writes)},
        {"uav_writes", std::move(uav_writes)},
        {"graphics_root_descriptor_tables", root_slot_array_to_json(e.graphics_root_descriptor_tables)},
        {"compute_root_descriptor_tables", root_slot_array_to_json(e.compute_root_descriptor_tables)},
        {"graphics_root_cbvs", root_slot_array_to_json(e.graphics_root_cbvs)},
        {"compute_root_cbvs", root_slot_array_to_json(e.compute_root_cbvs)},
        {"graphics_root_srvs", root_slot_array_to_json(e.graphics_root_srvs)},
        {"compute_root_srvs", root_slot_array_to_json(e.compute_root_srvs)},
        {"graphics_root_uavs", root_slot_array_to_json(e.graphics_root_uavs)},
        {"compute_root_uavs", root_slot_array_to_json(e.compute_root_uavs)},
        {"graphics_root_cbv_hash", root_hash_array_to_json(e.graphics_root_cbv_hash)},
        {"compute_root_cbv_hash", root_hash_array_to_json(e.compute_root_cbv_hash)},
        {"graphics_root_constants_hash", root_hash_array_to_json(e.graphics_root_constants_hash)},
        {"compute_root_constants_hash", root_hash_array_to_json(e.compute_root_constants_hash)},
        {"graphics_root_descriptor_table_resource_hash", root_hash_array_to_json(e.graphics_root_descriptor_table_resource_hash)},
        {"compute_root_descriptor_table_resource_hash", root_hash_array_to_json(e.compute_root_descriptor_table_resource_hash)},
        {"descriptor_reads", std::move(descriptor_reads)},
    };
}

json gpu_timing_to_json(const render::D3D12Diagnostics::GpuTimingInfo& t) {
    return {
        {"pipeline_state", format_pointer(t.pipeline_state)},
        {"kind", t.kind},
        {"eye_bucket", t.eye_bucket},
        {"samples", t.samples},
        {"avg_ms", t.avg_ms},
        {"max_ms", t.max_ms},
        {"last_frame", t.last_frame},
    };
}

void hash_u64(uint64_t& hash, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        hash ^= (value >> (i * 8)) & 0xffu;
        hash *= 1099511628211ull;
    }
}

template <typename Array>
void hash_root_array(uint64_t& hash, const Array& values) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] == 0) {
            continue;
        }
        hash_u64(hash, i);
        hash_u64(hash, static_cast<uint64_t>(values[i]));
    }
}

uint64_t draw_resource_fingerprint(const render::D3D12Diagnostics::DrawEvent& e) {
    uint64_t hash = 1469598103934665603ull;
    hash_u64(hash, e.root_signature);
    hash_u64(hash, e.rtv0);
    hash_u64(hash, e.rtv0_resource);
    for (const auto& write : e.render_target_writes) {
        hash_u64(hash, write.target_index);
        hash_u64(hash, write.descriptor);
        hash_u64(hash, write.resource);
        hash_u64(hash, write.prior_producer_draw);
        hash_u64(hash, write.prior_producer_pso);
    }
    for (const auto& write : e.uav_writes) {
        hash_u64(hash, write.target_index);
        hash_u64(hash, write.descriptor);
        hash_u64(hash, write.resource);
        hash_u64(hash, write.prior_producer_draw);
        hash_u64(hash, write.prior_producer_pso);
    }
    hash_root_array(hash, e.graphics_root_descriptor_tables);
    hash_root_array(hash, e.compute_root_descriptor_tables);
    hash_root_array(hash, e.graphics_root_cbvs);
    hash_root_array(hash, e.compute_root_cbvs);
    hash_root_array(hash, e.graphics_root_srvs);
    hash_root_array(hash, e.compute_root_srvs);
    hash_root_array(hash, e.graphics_root_uavs);
    hash_root_array(hash, e.compute_root_uavs);
    hash_root_array(hash, e.graphics_root_cbv_hash);
    hash_root_array(hash, e.compute_root_cbv_hash);
    hash_root_array(hash, e.graphics_root_constants_hash);
    hash_root_array(hash, e.compute_root_constants_hash);
    hash_root_array(hash, e.graphics_root_descriptor_table_resource_hash);
    hash_root_array(hash, e.compute_root_descriptor_table_resource_hash);
    for (const auto& read : e.descriptor_reads) {
        hash_u64(hash, read.root_parameter);
        hash_u64(hash, read.descriptor_index);
        hash_u64(hash, read.descriptor_cpu);
        hash_u64(hash, read.descriptor_source_cpu);
        hash_u64(hash, read.resource);
        hash_u64(hash, read.producer_draw);
        hash_u64(hash, read.producer_pso);
        hash_u64(hash, read.producer_descriptor);
    }
    return hash;
}

json symmetry_oracle_to_json(const std::vector<render::D3D12Diagnostics::DrawEvent>& events) {
    struct Aggregate {
        uintptr_t pipeline_state{};
        uint64_t left_count{};
        uint64_t right_count{};
        uint64_t unknown_count{};
        uint64_t left_fingerprint{};
        uint64_t right_fingerprint{};
        uint64_t left_draw_index{};
        uint64_t right_draw_index{};
    };

    std::unordered_map<uintptr_t, Aggregate> by_pso{};
    uint64_t analyzed = 0;
    for (const auto& e : events) {
        if (e.pipeline_state == 0) {
            continue;
        }

        ++analyzed;
        auto& aggregate = by_pso[e.pipeline_state];
        aggregate.pipeline_state = e.pipeline_state;
        const auto fingerprint = draw_resource_fingerprint(e);

        if (e.eye_bucket == 1) {
            ++aggregate.left_count;
            aggregate.left_fingerprint = fingerprint;
            aggregate.left_draw_index = e.draw_index;
        } else if (e.eye_bucket == 2) {
            ++aggregate.right_count;
            aggregate.right_fingerprint = fingerprint;
            aggregate.right_draw_index = e.draw_index;
        } else {
            ++aggregate.unknown_count;
        }
    }

    std::vector<Aggregate> asymmetric{};
    asymmetric.reserve(by_pso.size());
    for (const auto& [_, aggregate] : by_pso) {
        const bool has_both = aggregate.left_count > 0 && aggregate.right_count > 0;
        const bool count_mismatch = aggregate.left_count != aggregate.right_count;
        const bool bind_mismatch = has_both && aggregate.left_fingerprint != aggregate.right_fingerprint;
        if (count_mismatch || bind_mismatch) {
            asymmetric.push_back(aggregate);
        }
    }

    std::sort(asymmetric.begin(), asymmetric.end(), [](const auto& lhs, const auto& rhs) {
        const auto lhs_total = lhs.left_count + lhs.right_count + lhs.unknown_count;
        const auto rhs_total = rhs.left_count + rhs.right_count + rhs.unknown_count;
        return lhs_total > rhs_total;
    });

    json result{
        {"recent_draws_analyzed", analyzed},
        {"tracked_pso_count", by_pso.size()},
        {"asymmetric_pso_count", asymmetric.size()},
        {"asymmetric_psos", json::array()},
    };

    const size_t take = std::min<size_t>(asymmetric.size(), 64);
    for (size_t i = 0; i < take; ++i) {
        const auto& a = asymmetric[i];
        result["asymmetric_psos"].push_back({
            {"pipeline_state", format_pointer(a.pipeline_state)},
            {"left_count", a.left_count},
            {"right_count", a.right_count},
            {"unknown_count", a.unknown_count},
            {"left_fingerprint", format_pointer(static_cast<uintptr_t>(a.left_fingerprint))},
            {"right_fingerprint", format_pointer(static_cast<uintptr_t>(a.right_fingerprint))},
            {"left_draw_index", a.left_draw_index},
            {"right_draw_index", a.right_draw_index},
            {"count_mismatch", a.left_count != a.right_count},
            {"binding_mismatch", a.left_count > 0 && a.right_count > 0 && a.left_fingerprint != a.right_fingerprint},
        });
    }

    return result;
}

json bind_context_to_json(const render::D3D12Diagnostics::CurrentBindContext& c) {
    json result{
        {"frame", c.frame},
        {"source", c.source},
        {"exact_this_frame", c.exact_this_frame},
        {"render_targets", json::array()},
    };
    for (const auto& t : c.render_targets) {
        result["render_targets"].push_back(bound_target_to_json(t));
    }
    if (c.depth_target.has_value()) {
        result["depth_target"] = bound_target_to_json(*c.depth_target);
    }
    return result;
}

json root_signature_to_json(const render::D3D12Diagnostics::RootSignatureInfo& r) {
    json parameters = json::array();
    for (const auto& p : r.parameters) {
        json ranges = json::array();
        for (const auto& range : p.ranges) {
            ranges.push_back({
                {"type", range.type},
                {"base_shader_register", range.base_shader_register},
                {"num_descriptors", range.num_descriptors},
                {"register_space", range.register_space},
                {"offset_from_table_start", range.offset_from_table_start},
            });
        }

        parameters.push_back({
            {"index", p.index},
            {"parameter_type", p.parameter_type},
            {"visibility", p.visibility},
            {"shader_register", p.shader_register},
            {"register_space", p.register_space},
            {"num_32bit_values", p.num_32bit_values},
            {"ranges", std::move(ranges)},
        });
    }

    return {
        {"pointer", format_pointer(r.pointer)},
        {"first_seen_frame", r.first_seen_frame},
        {"last_seen_frame", r.last_seen_frame},
        {"blob_size", r.blob_size},
        {"blob_hash", format_pointer(r.blob_hash)},
        {"version", r.version},
        {"flags", r.flags},
        {"static_sampler_count", r.static_sampler_count},
        {"parameters", std::move(parameters)},
        {"decode_error", r.decode_error},
    };
}

json bound_shader_to_json(const render::ShaderOverrideRegistry::BoundShaderInfo& s) {
    return {
        {"known", s.known},
        {"backend", backend_name(s.backend)},
        {"stage", stage_name(s.stage)},
        {"original_pointer", format_pointer(s.original_pointer)},
        {"bound_pointer", format_pointer(s.bound_pointer)},
        {"hash", s.hash},
        {"crc32", format_crc32(s.crc32)},
        {"crc32_u32", s.crc32},
        {"override_active", s.override_active},
        {"override_name", s.override_name},
        {"note", s.note},
        {"last_bound_frame", s.last_bound_frame},
    };
}

json pso_pair_to_json(const render::ShaderOverrideRegistry::D3D12PipelinePairInfo& p) {
    return {
        {"frame", p.frame},
        {"first_seen_frame", p.first_seen_frame},
        {"last_seen_frame", p.last_seen_frame},
        {"hit_count", p.hit_count},
        {"original_pipeline_state", format_pointer(p.original_pipeline_state)},
        {"bound_pipeline_state", format_pointer(p.bound_pipeline_state)},
        {"pipeline_stream", p.pipeline_stream},
        {"tracking_note", p.tracking_note},
        {"vertex_shader", bound_shader_to_json(p.vertex_shader)},
        {"pixel_shader", bound_shader_to_json(p.pixel_shader)},
        {"geometry_shader", bound_shader_to_json(p.geometry_shader)},
    };
}

json pso_usage_to_json(const render::ShaderOverrideRegistry::PsoRenderUsageInfo& u) {
    return {
        {"render_target_name", u.render_target_name},
        {"depth_target_name", u.depth_target_name},
        {"render_target_key", u.render_target_key},
        {"depth_target_key", u.depth_target_key},
        {"hit_count", u.hit_count},
        {"share", u.share},
    };
}

json pso_aggregate_to_json(const render::ShaderOverrideRegistry::D3D12PsoAggregateInfo& a) {
    json result{
        {"total_samples", a.total_samples},
        {"sample_share", a.sample_share},
        {"bind_count_with_known_targets", a.bind_count_with_known_targets},
        {"first_seen_frame", a.first_seen_frame},
        {"last_seen_frame", a.last_seen_frame},
        {"original_pso", format_pointer(a.original_pso)},
        {"last_bound_pso", format_pointer(a.last_bound_pso)},
        {"pipeline_stream", a.pipeline_stream},
        {"tracking_note", a.tracking_note},
        {"vs_hash", a.vs_hash},
        {"ps_hash", a.ps_hash},
        {"gs_hash", a.gs_hash},
        {"vs_crc32", format_crc32(a.vs_crc32)},
        {"ps_crc32", format_crc32(a.ps_crc32)},
        {"gs_crc32", format_crc32(a.gs_crc32)},
        {"vs_crc32_u32", a.vs_crc32},
        {"ps_crc32_u32", a.ps_crc32},
        {"gs_crc32_u32", a.gs_crc32},
        {"vs_override", a.vs_override},
        {"ps_override", a.ps_override},
        {"gs_override", a.gs_override},
        {"likely_targets", json::array()},
    };
    for (const auto& u : a.likely_targets) {
        result["likely_targets"].push_back(pso_usage_to_json(u));
    }
    return result;
}

json override_entry_to_json(const render::ShaderOverrideRegistry::OverrideEntryInfo& e) {
    return {
        {"key", e.key},
        {"name", e.name},
        {"backend", backend_name(e.backend)},
        {"stage", stage_name(e.stage)},
        {"target_hash", e.target_hash},
        {"manifest_path", e.manifest_path},
        {"source_path", e.source_path},
        {"source_kind", e.source_kind},
        {"entry_point", e.entry_point},
        {"profile", e.profile},
        {"enabled", e.enabled},
        {"compiled", e.compiled},
        {"apply_supported", e.apply_supported},
        {"from_profile_dir", e.from_profile_dir},
        {"per_eye_variants", e.per_eye_variants},
        {"has_left_payload", e.has_left_payload},
        {"has_right_payload", e.has_right_payload},
        {"left_source_kind", e.left_source_kind},
        {"right_source_kind", e.right_source_kind},
        {"generation", e.generation},
        {"status", e.status},
        {"compiler", e.compiler},
        {"last_error", e.last_error},
    };
}

json bind_override_entry_to_json(const render::ShaderOverrideRegistry::BindOverrideEntryInfo& e) {
    return {
        {"key", e.key},
        {"name", e.name},
        {"target_hash", e.target_hash},
        {"stage", e.stage},
        {"pipeline", e.pipeline},
        {"eye", e.eye},
        {"kind", e.kind},
        {"root_parameter", e.root_parameter},
        {"value_count", e.value_count},
        {"dest_offset", e.dest_offset},
        {"enabled", e.enabled},
        {"from_profile_dir", e.from_profile_dir},
        {"manifest_path", e.manifest_path},
        {"status", e.status},
        {"last_error", e.last_error},
    };
}

json pso_churn_to_json(const render::ShaderOverrideRegistry::D3D12PsoChurnInfo& c) {
    json recent_frames = json::array();
    for (const auto& f : c.recent_frames) {
        recent_frames.push_back({
            {"frame", f.frame},
            {"graphics_creations", f.graphics_creations},
            {"compute_creations", f.compute_creations},
            {"stream_creations", f.stream_creations},
            {"total_creations", f.graphics_creations + f.compute_creations + f.stream_creations},
        });
    }

    return {
        {"tracked_pso_count", c.tracked_pso_count},
        {"current_frame_graphics_creations", c.current_frame_graphics_creations},
        {"current_frame_compute_creations", c.current_frame_compute_creations},
        {"current_frame_stream_creations", c.current_frame_stream_creations},
        {"current_frame_total_creations", c.current_frame_graphics_creations + c.current_frame_compute_creations + c.current_frame_stream_creations},
        {"recent_window_frames", c.recent_window_frames},
        {"recent_graphics_creations", c.recent_graphics_creations},
        {"recent_compute_creations", c.recent_compute_creations},
        {"recent_stream_creations", c.recent_stream_creations},
        {"recent_total_creations", c.recent_graphics_creations + c.recent_compute_creations + c.recent_stream_creations},
        {"recent_frames", std::move(recent_frames)},
    };
}

json shader_chunk_to_json(const render::ShaderContainerChunkInfo& c) {
    return {
        {"fourcc", c.fourcc},
        {"offset", c.offset},
        {"size", c.size},
    };
}

std::string reflection_root_kind(const render::ShaderReflectionResourceBindingInfo& r) {
    if (r.type == "cbv") {
        return "cbv";
    }
    if (r.type == "tbuffer" || r.type == "srv" || r.type == "acceleration_structure") {
        return "srv";
    }
    if (r.type == "uav") {
        return "uav";
    }
    if (r.type == "sampler") {
        return "sampler";
    }
    return {};
}

bool register_in_root_range(const render::D3D12Diagnostics::RootDescriptorRangeInfo& range, uint32_t bind_point) {
    if (bind_point < range.base_shader_register) {
        return false;
    }

    if (range.num_descriptors == UINT32_MAX) {
        return true;
    }

    const uint64_t end = static_cast<uint64_t>(range.base_shader_register) +
                         static_cast<uint64_t>(range.num_descriptors);
    return static_cast<uint64_t>(bind_point) < end;
}

json root_binding_match_to_json(
    const render::ShaderReflectionResourceBindingInfo& resource,
    const render::D3D12Diagnostics::RootSignatureInfo* root_signature
) {
    if (root_signature == nullptr) {
        return nullptr;
    }

    const auto kind = reflection_root_kind(resource);
    if (kind.empty()) {
        return nullptr;
    }

    for (const auto& param : root_signature->parameters) {
        if ((param.parameter_type == kind ||
             (kind == "cbv" && param.parameter_type == "32bit_constants")) &&
            param.shader_register == resource.bind_point &&
            param.register_space == resource.space) {
            return {
                {"root_parameter", param.index},
                {"root_parameter_type", param.parameter_type},
                {"shader_register", param.shader_register},
                {"register_space", param.register_space},
            };
        }

        if (param.parameter_type != "descriptor_table") {
            continue;
        }

        for (size_t range_index = 0; range_index < param.ranges.size(); ++range_index) {
            const auto& range = param.ranges[range_index];
            if (range.type != kind ||
                range.register_space != resource.space ||
                !register_in_root_range(range, resource.bind_point)) {
                continue;
            }

            return {
                {"root_parameter", param.index},
                {"root_parameter_type", param.parameter_type},
                {"root_range_index", range_index},
                {"root_range_type", range.type},
                {"root_range_base_shader_register", range.base_shader_register},
                {"root_range_register_space", range.register_space},
                {"descriptor_index", resource.bind_point - range.base_shader_register},
            };
        }
    }

    return nullptr;
}

json shader_reflection_variable_to_json(const render::ShaderReflectionVariableInfo& v) {
    return {
        {"name", v.name},
        {"start_offset", v.start_offset},
        {"size", v.size},
        {"flags", v.flags},
        {"type_name", v.type_name},
        {"type_class", v.type_class},
        {"type_kind", v.type_kind},
        {"rows", v.rows},
        {"columns", v.columns},
        {"elements", v.elements},
        {"members", v.members},
    };
}

json shader_reflection_cbuffer_to_json(const render::ShaderReflectionConstantBufferInfo& c) {
    json variables = json::array();
    for (const auto& v : c.variables) {
        variables.push_back(shader_reflection_variable_to_json(v));
    }

    return {
        {"name", c.name},
        {"type", c.type},
        {"size", c.size},
        {"variables", std::move(variables)},
    };
}

json shader_reflection_resource_to_json(
    const render::ShaderReflectionResourceBindingInfo& r,
    const render::D3D12Diagnostics::RootSignatureInfo* root_signature
) {
    json out{
        {"name", r.name},
        {"type", r.type},
        {"return_type", r.return_type},
        {"dimension", r.dimension},
        {"bind_point", r.bind_point},
        {"bind_count", r.bind_count},
        {"space", r.space},
        {"flags", r.flags},
    };

    auto match = root_binding_match_to_json(r, root_signature);
    if (!match.is_null()) {
        out["root_binding"] = std::move(match);
    }

    return out;
}

json shader_reflection_signature_to_json(const render::ShaderReflectionSignatureParamInfo& p) {
    return {
        {"semantic_name", p.semantic_name},
        {"semantic_index", p.semantic_index},
        {"register_index", p.register_index},
        {"system_value", p.system_value},
        {"component_type", p.component_type},
        {"mask", p.mask},
        {"read_write_mask", p.read_write_mask},
        {"stream", p.stream},
    };
}

json shader_reflection_to_json(
    const render::ShaderReflectionInfo& r,
    const render::D3D12Diagnostics::RootSignatureInfo* root_signature
) {
    json cbuffers = json::array();
    for (const auto& c : r.constant_buffers) {
        cbuffers.push_back(shader_reflection_cbuffer_to_json(c));
    }

    json resources = json::array();
    for (const auto& b : r.bound_resources) {
        resources.push_back(shader_reflection_resource_to_json(b, root_signature));
    }

    json inputs = json::array();
    for (const auto& p : r.input_parameters) {
        inputs.push_back(shader_reflection_signature_to_json(p));
    }

    json outputs = json::array();
    for (const auto& p : r.output_parameters) {
        outputs.push_back(shader_reflection_signature_to_json(p));
    }

    return {
        {"ok", r.ok},
        {"error", r.error},
        {"creator", r.creator},
        {"instruction_count", r.instruction_count},
        {"constant_buffer_count", r.constant_buffer_count},
        {"bound_resource_count", r.bound_resource_count},
        {"input_parameter_count", r.input_parameter_count},
        {"output_parameter_count", r.output_parameter_count},
        {"constant_buffers", std::move(cbuffers)},
        {"bound_resources", std::move(resources)},
        {"input_parameters", std::move(inputs)},
        {"output_parameters", std::move(outputs)},
    };
}

json shader_bytecode_inspection_to_json(const render::ShaderOverrideRegistry::D3D12ShaderBytecodeInspection& i) {
    auto root_signature = render::D3D12Diagnostics::get().root_signature_for_pipeline(i.pipeline_state);
    if (!root_signature.has_value() && i.root_signature != 0) {
        root_signature = render::D3D12Diagnostics::get().root_signature(i.root_signature);
    }
    const auto* root_signature_ptr = root_signature.has_value() ? &*root_signature : nullptr;

    json bytecode{
        {"ok", i.bytecode.ok},
        {"container", i.bytecode.container},
        {"container_kind", i.bytecode.container_kind},
        {"container_hash", i.bytecode.container_hash},
        {"container_version", i.bytecode.container_version},
        {"declared_size", i.bytecode.declared_size},
        {"bytecode_size", i.bytecode.bytecode_size},
        {"compiler", i.bytecode.compiler},
        {"error", i.bytecode.error},
        {"chunks", json::array()},
    };

    for (const auto& c : i.bytecode.chunks) {
        bytecode["chunks"].push_back(shader_chunk_to_json(c));
    }

    if (!i.bytecode.disassembly.empty()) {
        bytecode["disassembly"] = i.bytecode.disassembly;
    }

    if (!i.bytecode.recovered_sources.empty()) {
        bytecode["recovered_sources"] = json::array();
        for (const auto& source : i.bytecode.recovered_sources) {
            bytecode["recovered_sources"].push_back({
                {"name", source.name},
                {"text", source.text},
            });
        }
    }

    bytecode["reflection"] = shader_reflection_to_json(i.bytecode.reflection, root_signature_ptr);

    json result{
        {"found", i.found},
        {"requested_stage", i.requested_stage},
        {"requested_hash", i.requested_hash},
        {"matched_stage", i.matched_stage},
        {"pipeline_state", format_pointer(i.pipeline_state)},
        {"root_signature_pointer", format_pointer(i.root_signature)},
        {"bytecode", std::move(bytecode)},
    };

    if (root_signature_ptr != nullptr) {
        result["root_signature"] = root_signature_to_json(*root_signature_ptr);
    }

    return result;
}

json preview_to_json(const render::FrameResourceInspector::PreviewInfo& p) {
    return {
        {"has_selection", p.has_selection},
        {"available", p.available},
        {"backend", backend_name(p.backend)},
        {"resource_key", p.resource_key},
        {"texture_id", p.texture_id},
        {"width", p.width},
        {"height", p.height},
        {"format", p.format},
        {"status", p.status},
        {"backend_note", p.backend_note},
    };
}

json d3d12_snapshot_to_json(const render::D3D12Diagnostics::Snapshot& s, int max_events) {
    json result{
        {"available", s.available},
        {"frame", s.frame},
        {"device", format_pointer(s.device)},
        {"swapchain", format_pointer(s.swapchain)},
        {"command_queue", format_pointer(s.command_queue)},
        {"render_width", s.render_width},
        {"render_height", s.render_height},
        {"display_width", s.display_width},
        {"display_height", s.display_height},
        {"proton_swapchain", s.proton_swapchain},
        {"framegen_swapchain", s.framegen_swapchain},
        {"active_cbv_srv_uav_heap", format_pointer(s.active_cbv_srv_uav_heap)},
        {"active_sampler_heap", format_pointer(s.active_sampler_heap)},
        {"descriptor_heap_sets_this_frame", s.descriptor_heap_sets_this_frame},
        {"descriptor_heap_switches_this_frame", s.descriptor_heap_switches_this_frame},
        {"resource_barriers_this_frame", s.resource_barriers_this_frame},
        {"rtv_binds_this_frame", s.rtv_binds_this_frame},
        {"root_binds_this_frame", s.root_binds_this_frame},
        {"draw_events_this_frame", s.draw_events_this_frame},
        {"transient_heap_creations_this_frame", s.transient_heap_creations_this_frame},
        {"transient_resource_creations_this_frame", s.transient_resource_creations_this_frame},
        {"transient_resource_bytes_this_frame", s.transient_resource_bytes_this_frame},
        {"tracked_resource_bytes_total", s.tracked_resource_bytes_total},
        {"tracked_transient_resource_bytes_total", s.tracked_transient_resource_bytes_total},
    };

    if (s.current_bind_context.has_value()) {
        result["current_bind_context"] = bind_context_to_json(*s.current_bind_context);
    }

    json heaps = json::array();
    for (const auto& h : s.heaps) {
        heaps.push_back(heap_to_json(h));
    }
    result["heaps"] = std::move(heaps);

    json root_signatures = json::array();
    for (const auto& r : s.root_signatures) {
        root_signatures.push_back(root_signature_to_json(r));
    }
    result["root_signatures"] = std::move(root_signatures);

    auto tail = [&](const auto& src, auto&& cb) {
        json arr = json::array();
        const size_t take = (max_events > 0 && static_cast<size_t>(max_events) < src.size())
                                ? static_cast<size_t>(max_events)
                                : src.size();
        for (size_t i = src.size() - take; i < src.size(); ++i) {
            arr.push_back(cb(src[i]));
        }
        return arr;
    };

    result["recent_bindings"] = tail(s.recent_bindings, [](const auto& e) { return binding_to_json(e); });
    result["recent_root_binds"] = tail(s.recent_root_binds, [](const auto& e) { return root_bind_to_json(e); });
    result["recent_draw_events"] = tail(s.recent_draw_events, [](const auto& e) { return draw_event_to_json(e); });
    result["gpu_timings"] = tail(s.gpu_timings, [](const auto& e) { return gpu_timing_to_json(e); });
    result["symmetry_oracle"] = symmetry_oracle_to_json(s.recent_draw_events);
    result["recent_pipeline_cache_events"] = tail(s.recent_pipeline_cache_events, [](const auto& e) { return pipeline_cache_event_to_json(e); });
    result["recent_barriers"] = tail(s.recent_barriers, [](const auto& e) { return barrier_to_json(e); });
    result["recent_warnings"] = tail(s.recent_warnings, [](const auto& e) { return warning_to_json(e); });
    return result;
}

json shaders_snapshot_to_json(
    const render::ShaderOverrideRegistry::Snapshot& s,
    int max_distinct_pairs,
    int max_pso_aggregates
) {
    json result{
        {"auto_reload", s.auto_reload},
        {"runtime_overrides_enabled", s.runtime_overrides_enabled},
        {"frame", s.frame},
        {"global_override_dir", s.global_override_dir},
        {"profile_override_dir", s.profile_override_dir},
        {"bound_vertex_shader", bound_shader_to_json(s.bound_vertex_shader)},
        {"bound_pixel_shader", bound_shader_to_json(s.bound_pixel_shader)},
        {"capture_next_d3d12_change_armed", s.capture_next_d3d12_change_armed},
        {"total_d3d12_pair_samples", s.total_d3d12_pair_samples},
        {"total_d3d12_pso_samples", s.total_d3d12_pso_samples},
        {"d3d12_pso_churn", pso_churn_to_json(s.d3d12_pso_churn)},
        {"distinct_d3d12_pairs", json::array()},
        {"d3d12_pso_aggregates", json::array()},
        {"overrides", json::array()},
        {"bind_overrides", json::array()},
        {"recent_events", json::array()},
    };

    if (s.current_d3d12_pair.has_value()) {
        result["current_d3d12_pair"] = pso_pair_to_json(*s.current_d3d12_pair);
    }
    if (s.captured_d3d12_pair.has_value()) {
        result["captured_d3d12_pair"] = pso_pair_to_json(*s.captured_d3d12_pair);
    }

    auto pairs = s.distinct_d3d12_pairs;
    apply_limit(pairs, max_distinct_pairs);
    for (const auto& p : pairs) {
        result["distinct_d3d12_pairs"].push_back(pso_pair_to_json(p));
    }

    auto aggregates = s.d3d12_pso_aggregates;
    apply_limit(aggregates, max_pso_aggregates);
    for (const auto& a : aggregates) {
        result["d3d12_pso_aggregates"].push_back(pso_aggregate_to_json(a));
    }

    for (const auto& e : s.overrides) {
        result["overrides"].push_back(override_entry_to_json(e));
    }
    for (const auto& e : s.bind_overrides) {
        result["bind_overrides"].push_back(bind_override_entry_to_json(e));
    }
    for (const auto& msg : s.recent_events) {
        result["recent_events"].push_back(msg);
    }

    return result;
}

json context_json() {
    json ctx;
    ctx["framework_ready"] = (g_framework != nullptr) && g_framework->is_ready();
    if (g_framework != nullptr) {
        ctx["renderer"] = g_framework->is_dx12() ? "D3D12" : (g_framework->is_dx11() ? "D3D11" : "Unknown");
        ctx["persistent_dir"] = Framework::get_persistent_dir().string();
        ctx["profile_name"] = Framework::get_persistent_dir().filename().string();
    } else {
        ctx["renderer"] = "Unknown";
    }
    auto& inspector = *RenderInspector::get();
    ctx["force_resources_sampling"] = inspector.force_resources_sampling();
    ctx["force_shader_tracking"] = inspector.force_shader_tracking();
    ctx["force_d3d12_diagnostics"] = inspector.force_d3d12_diagnostics();
    return ctx;
}

bool framework_ready() {
    return g_framework != nullptr && g_framework->is_ready();
}

} // namespace

// ── Snapshot exports ──────────────────────────────────────────────────

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_snapshot_json(
    int max_resources,
    int max_d3d12_events,
    int max_distinct_pairs,
    int max_pso_aggregates
) {
    try {
        json result;
        result["context"] = context_json();

        auto& ri = *RenderInspector::get();
        auto resources = ri.inspector().snapshot();
        apply_limit(resources, max_resources);
        result["frame"] = ri.inspector().current_frame();
        result["resource_count"] = resources.size();
        result["resources"] = json::array();
        for (const auto& r : resources) {
            result["resources"].push_back(resource_to_json(r));
        }
        result["preview"] = preview_to_json(ri.inspector().preview_info());

        result["d3d12"] = d3d12_snapshot_to_json(
            render::D3D12Diagnostics::get().snapshot(),
            max_d3d12_events
        );

        result["shaders"] = shaders_snapshot_to_json(
            render::ShaderOverrideRegistry::get().snapshot(),
            max_distinct_pairs,
            max_pso_aggregates
        );

        return publish(std::move(result));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_resources_json(int max_resources) {
    try {
        auto& ri = *RenderInspector::get();
        auto resources = ri.inspector().snapshot();
        apply_limit(resources, max_resources);
        json result{
            {"frame", ri.inspector().current_frame()},
            {"selected_resource", nullptr},
            {"count", resources.size()},
            {"resources", json::array()},
        };
        if (const auto sel = ri.inspector().selected_resource(); sel.has_value()) {
            result["selected_resource"] = *sel;
        }
        for (const auto& r : resources) {
            result["resources"].push_back(resource_to_json(r));
        }
        result["preview"] = preview_to_json(ri.inspector().preview_info());
        return publish(std::move(result));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_d3d12_json(int max_heaps, int max_events) {
    try {
        auto s = render::D3D12Diagnostics::get().snapshot();
        if (max_heaps > 0 && s.heaps.size() > static_cast<size_t>(max_heaps)) {
            s.heaps.resize(static_cast<size_t>(max_heaps));
        }
        return publish(d3d12_snapshot_to_json(s, max_events));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_shaders_json(
    int max_distinct_pairs,
    int max_pso_aggregates
) {
    try {
        return publish(shaders_snapshot_to_json(
            render::ShaderOverrideRegistry::get().snapshot(),
            max_distinct_pairs,
            max_pso_aggregates
        ));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_shader_bytecode_json(
    const char* stage,
    const char* hash,
    int disassemble,
    int max_disassembly_chars
) {
    try {
        if (hash == nullptr || *hash == '\0') {
            return publish(json{{"found", false}, {"error", "hash is required"}});
        }

        const auto max_chars = max_disassembly_chars > 0
            ? static_cast<size_t>(max_disassembly_chars)
            : static_cast<size_t>(128 * 1024);

        auto inspection = render::ShaderOverrideRegistry::get().inspect_d3d12_shader_bytecode(
            stage != nullptr ? stage : "any",
            hash,
            disassemble != 0,
            max_chars);

        return publish(shader_bytecode_inspection_to_json(inspection));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_hunter_capture_active_override_stub(int stage) {
    try {
        auto hunter_stage = render::ShaderOverrideRegistry::HunterStage::Pixel;
        if (stage == 1) {
            hunter_stage = render::ShaderOverrideRegistry::HunterStage::Vertex;
        } else if (stage == 2) {
            hunter_stage = render::ShaderOverrideRegistry::HunterStage::Compute;
        }

        std::filesystem::path manifest_path{};
        std::filesystem::path source_path{};
        std::string error{};
        const bool ok = render::ShaderOverrideRegistry::get().hunter_capture_active_as_override_stub(
            hunter_stage,
            manifest_path,
            source_path,
            error);

        if (!ok) {
            return publish(json{{"ok", false}, {"error", error}});
        }

        return publish(json{
            {"ok", true},
            {"manifest_path", manifest_path.string()},
            {"source_path", source_path.string()},
            {"enabled", false},
        });
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_hunter_highlight_hash_json(const char* hash, int enabled) {
    try {
        auto& registry = render::ShaderOverrideRegistry::get();
        auto normalize = [](std::string value) {
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                return std::isspace(c) != 0;
            }), value.end());
            if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
                value.erase(0, 2);
            }
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        };

        const std::string h = normalize(hash != nullptr ? hash : "");
        if (h.empty()) {
            return publish(json{{"ok", false}, {"error", "hash is required"}});
        }

        if (h == "*" && enabled == 0) {
            for (const auto& item : registry.hunter_highlight_snapshot()) {
                registry.hunter_toggle_highlight_hash(item);
            }
        } else if (h == "*") {
            return publish(json{{"ok", false}, {"error", "hash='*' only supports enabled=false"}});
        } else {
            const auto before = registry.hunter_highlight_snapshot();
            const bool was_enabled = std::find(before.begin(), before.end(), h) != before.end();
            const bool should_enable = enabled != 0;
            if (was_enabled != should_enable) {
                registry.hunter_toggle_highlight_hash(h);
            }
        }

        return publish(json{
            {"ok", true},
            {"hash", h},
            {"enabled", enabled != 0},
            {"highlights", registry.hunter_highlight_snapshot()},
        });
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_hunter_skip_eye_hash_json(
    const char* hash,
    int eye,
    int enabled
) {
    try {
        auto& registry = render::ShaderOverrideRegistry::get();
        auto normalize = [](std::string value) {
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                return std::isspace(c) != 0;
            }), value.end());
            if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
                value.erase(0, 2);
            }
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        };

        const std::string h = normalize(hash != nullptr ? hash : "");
        if (h.empty()) {
            return publish(json{{"ok", false}, {"error", "hash is required"}});
        }

        const bool left = eye == 0;
        const bool was_enabled = left
            ? registry.hunter_is_skip_left_only(h)
            : registry.hunter_is_skip_right_only(h);
        const bool should_enable = enabled != 0;
        if (was_enabled != should_enable) {
            if (left) {
                registry.hunter_toggle_skip_left_only(h);
            } else {
                registry.hunter_toggle_skip_right_only(h);
            }
        }

        return publish(json{
            {"ok", true},
            {"hash", h},
            {"eye", left ? "left" : "right"},
            {"enabled", should_enable},
            {"skip_left", registry.hunter_is_skip_left_only(h)},
            {"skip_right", registry.hunter_is_skip_right_only(h)},
        });
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_preview_info_json() {
    try {
        auto& ri = *RenderInspector::get();
        return publish(preview_to_json(ri.inspector().preview_info()));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_context_json() {
    try {
        return publish(context_json());
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

// ── Mutators ──────────────────────────────────────────────────────────

extern "C" UEVR_RENDER_CAPI void uevr_render_diag_set_selected_resource(uint64_t key) {
    try {
        auto& ri = *RenderInspector::get();
        if (key == 0) {
            ri.inspector().set_selected_resource(std::nullopt);
        } else {
            ri.inspector().set_selected_resource(key);
        }
    } catch (const std::exception& e) {
        spdlog::error("[RenderCAPI] set_selected_resource failed: {}", e.what());
    }
}

extern "C" UEVR_RENDER_CAPI void uevr_render_diag_set_force_resources_sampling(int enabled) {
    try {
        RenderInspector::get()->set_force_resources_sampling(enabled != 0);
    } catch (const std::exception& e) {
        spdlog::error("[RenderCAPI] set_force_resources_sampling failed: {}", e.what());
    }
}

extern "C" UEVR_RENDER_CAPI void uevr_render_diag_set_force_shader_tracking(int enabled) {
    try {
        RenderInspector::get()->set_force_shader_tracking(enabled != 0);
    } catch (const std::exception& e) {
        spdlog::error("[RenderCAPI] set_force_shader_tracking failed: {}", e.what());
    }
}

extern "C" UEVR_RENDER_CAPI void uevr_render_diag_set_force_d3d12_diagnostics(int enabled) {
    try {
        RenderInspector::get()->set_force_d3d12_diagnostics(enabled != 0);
    } catch (const std::exception& e) {
        spdlog::error("[RenderCAPI] set_force_d3d12_diagnostics failed: {}", e.what());
    }
}

extern "C" UEVR_RENDER_CAPI void uevr_render_diag_request_shader_reload() {
    try {
        render::ShaderOverrideRegistry::get().request_reload();
    } catch (const std::exception& e) {
        spdlog::error("[RenderCAPI] request_shader_reload failed: {}", e.what());
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_set_runtime_overrides_enabled(int enabled) {
    try {
        auto& registry = render::ShaderOverrideRegistry::get();
        registry.set_runtime_overrides_enabled(enabled != 0);
        return publish(json{{"ok", true}, {"runtime_overrides_enabled", registry.runtime_overrides_enabled()}});
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI void uevr_render_diag_capture_next_d3d12_change() {
    try {
        render::ShaderOverrideRegistry::get().request_capture_next_d3d12_change();
    } catch (const std::exception& e) {
        spdlog::error("[RenderCAPI] capture_next_d3d12_change failed: {}", e.what());
    }
}

extern "C" UEVR_RENDER_CAPI void uevr_render_diag_clear_captured_d3d12_change() {
    try {
        render::ShaderOverrideRegistry::get().clear_captured_d3d12_change();
    } catch (const std::exception& e) {
        spdlog::error("[RenderCAPI] clear_captured_d3d12_change failed: {}", e.what());
    }
}

extern "C" UEVR_RENDER_CAPI void uevr_render_diag_reset_d3d12() {
    try {
        render::D3D12Diagnostics::get().reset();
    } catch (const std::exception& e) {
        spdlog::error("[RenderCAPI] reset_d3d12 failed: {}", e.what());
    }
}

// ── Disk exports ──────────────────────────────────────────────────────

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_export_d3d12_pairs(int as_csv) {
    try {
        std::filesystem::path path{};
        std::string error{};
        const bool ok = (as_csv != 0)
            ? render::ShaderOverrideRegistry::get().export_d3d12_pairs_csv(path, error)
            : render::ShaderOverrideRegistry::get().export_d3d12_pairs_json(path, error);

        if (ok) {
            return publish(json{{"ok", true}, {"path", path.string()}});
        }
        return publish(json{{"ok", false}, {"error", error}});
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_export_frame_pair_diff_json(int max_events) {
    try {
        if (g_framework == nullptr || !g_framework->is_ready()) {
            return publish(json{{"ok", false}, {"error", "framework not ready"}});
        }

        const auto snapshot = render::D3D12Diagnostics::get().snapshot();
        auto payload = d3d12_snapshot_to_json(snapshot, max_events > 0 ? max_events : 512);
        payload["export_kind"] = "frame_pair_diff";
        payload["export_note"] = "Left/right draw and binding diff from the recent D3D12 diagnostics ring";

        const auto base = Framework::get_persistent_dir("render_inspector") / "frame_diffs";
        std::error_code ec{};
        std::filesystem::create_directories(base, ec);
        if (ec) {
            return publish(json{{"ok", false}, {"error", ec.message()}});
        }

        std::ostringstream name{};
        name << "d3d12_frame_pair_diff_f" << snapshot.frame << ".json";
        const auto path = base / name.str();

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return publish(json{{"ok", false}, {"error", "failed to open output file"}, {"path", path.string()}});
        }

        file << payload.dump(2);
        file.close();

        return publish(json{
            {"ok", true},
            {"path", path.string()},
            {"frame", snapshot.frame},
            {"draw_events", snapshot.recent_draw_events.size()},
            {"root_binds", snapshot.recent_root_binds.size()},
        });
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_export_bundle(
    const char* profile_name,
    const char* backend
) {
    try {
        if (!framework_ready()) {
            return publish(json{{"ok", false}, {"error", "framework not ready"}});
        }

        render::RenderAnalysisExportInput input{};
        input.profile_name = (profile_name != nullptr && *profile_name != '\0')
            ? std::string{profile_name}
            : Framework::get_persistent_dir().filename().string();
        input.backend = (backend != nullptr && *backend != '\0')
            ? std::string{backend}
            : (g_framework->is_dx12() ? "D3D12" : "D3D11");

        auto shader_snapshot = render::ShaderOverrideRegistry::get().snapshot();
        auto d3d12_snapshot = render::D3D12Diagnostics::get().snapshot();
        auto resource_snapshot = RenderInspector::get()->inspector().snapshot();

        input.frame = shader_snapshot.frame;
        input.resources = std::move(resource_snapshot);
        input.d3d12 = std::move(d3d12_snapshot);
        input.shaders = std::move(shader_snapshot);

        const auto result = render::RenderAnalysisExport::export_bundle(input);
        json out{{"ok", result.succeeded}, {"bundle_dir", result.bundle_dir.string()}};
        if (!result.error.empty()) {
            out["error"] = result.error;
        }
        out["files"] = json::array();
        for (const auto& f : result.files) {
            out["files"].push_back(f.string());
        }
        return publish(std::move(out));
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_stereo_forensics_json() {
    try {
        auto& forensics = render::StereoForensics::get();
        const auto dir = forensics.session_dir();
        return publish(json{
            {"enabled", forensics.is_enabled()},
            {"experiments_enabled", forensics.experiments_enabled()},
            {"session_dir", dir.string()},
            {"manifest", dir.empty() ? std::string{} : (dir / "manifest.json").string()},
            {"events_jsonl", dir.empty() ? std::string{} : (dir / "events.jsonl").string()},
            {"eye_diff", dir.empty() ? std::string{} : (dir / "eye_diff.json").string()},
            {"lineage", dir.empty() ? std::string{} : (dir / "lineage.json").string()},
            {"experiments", dir.empty() ? std::string{} : (dir / "experiments.json").string()}
        });
    } catch (const std::exception& e) {
        return publish(json{{"enabled", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_stereo_forensics_arm_json() {
    try {
        auto& forensics = render::StereoForensics::get();
        const bool ok = forensics.arm_capture("capi");
        const auto dir = forensics.session_dir();
        return publish(json{
            {"ok", ok},
            {"enabled", forensics.is_enabled()},
            {"experiments_enabled", forensics.experiments_enabled()},
            {"session_dir", dir.string()},
            {"manifest", dir.empty() ? std::string{} : (dir / "manifest.json").string()},
            {"events_jsonl", dir.empty() ? std::string{} : (dir / "events.jsonl").string()},
            {"eye_diff", dir.empty() ? std::string{} : (dir / "eye_diff.json").string()},
            {"lineage", dir.empty() ? std::string{} : (dir / "lineage.json").string()},
            {"experiments", dir.empty() ? std::string{} : (dir / "experiments.json").string()}
        });
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"enabled", false}, {"error", e.what()}});
    }
}

// ── Stereo / one-eye-bug diagnostics ────────────────────────────────

namespace {

enum class EyeSide : int {
    Unknown = -1,
    Left = 0,
    Right = 1,
};

std::string lowercase_copy(std::string_view v) {
    std::string out{v};
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool contains_word(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

EyeSide classify_eye_text(std::string_view raw) {
    const auto lower = lowercase_copy(raw);
    // Right-eye markers first — many UEVR resources include "right" only.
    if (contains_word(lower, "right") || contains_word(lower, "_r_") ||
        contains_word(lower, " r ") || contains_word(lower, "[1]") ||
        contains_word(lower, " r]") || contains_word(lower, ".r ") ||
        contains_word(lower, "righteye")) {
        return EyeSide::Right;
    }
    if (contains_word(lower, "left") || contains_word(lower, "_l_") ||
        contains_word(lower, " l ") || contains_word(lower, "[0]") ||
        contains_word(lower, " l]") || contains_word(lower, ".l ") ||
        contains_word(lower, "lefteye")) {
        return EyeSide::Left;
    }
    return EyeSide::Unknown;
}

// Classify a resource into Left/Right by examining name + tags + source.
EyeSide classify_resource_eye(const render::FrameResourceInspector::ResourceInfo& r) {
    if (!r.is_eye) {
        // Even if not flagged is_eye, OpenXR swapchain images can carry the
        // eye index in the name. Try anyway, but only return a side if a
        // strong signal exists.
        const auto by_name = classify_eye_text(r.name);
        if (by_name != EyeSide::Unknown) {
            return by_name;
        }
        return EyeSide::Unknown;
    }

    if (const auto side = classify_eye_text(r.name); side != EyeSide::Unknown) {
        return side;
    }
    if (const auto side = classify_eye_text(r.source); side != EyeSide::Unknown) {
        return side;
    }
    if (const auto side = classify_eye_text(r.tags); side != EyeSide::Unknown) {
        return side;
    }
    return EyeSide::Unknown;
}

json minimal_resource_json(const render::FrameResourceInspector::ResourceInfo& r) {
    return {
        {"key", r.key},
        {"pointer", format_pointer(r.pointer)},
        {"backend", backend_name(r.backend)},
        {"name", r.name},
        {"source", r.source},
        {"format", r.format},
        {"resolution", r.resolution},
        {"width", r.width},
        {"height", r.height},
        {"first_seen_frame", r.first_seen_frame},
        {"last_seen_frame", r.last_seen_frame},
        {"seen_count", r.seen_count},
        {"change_count", r.change_count},
        {"is_recent", r.is_recent},
    };
}

} // namespace

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_stereo_summary_json() {
    try {
        auto& ri = *RenderInspector::get();
        const auto resources = ri.inspector().snapshot();
        const auto shaders = render::ShaderOverrideRegistry::get().snapshot();
        const auto d3d12 = render::D3D12Diagnostics::get().snapshot();

        struct EyeBucket {
            std::vector<const render::FrameResourceInspector::ResourceInfo*> resources{};
            uint64_t total_seen{};
            uint64_t total_changes{};
            uint64_t latest_frame{};
            uint64_t bind_hit_count{};
            std::vector<std::pair<size_t, double>> pso_indices{};
        };

        EyeBucket left{}, right{};
        size_t unknown_eye_count{0};

        for (const auto& r : resources) {
            const auto side = classify_resource_eye(r);
            EyeBucket* bucket = nullptr;
            if (side == EyeSide::Left)  bucket = &left;
            if (side == EyeSide::Right) bucket = &right;
            if (bucket == nullptr) {
                if (r.is_eye) ++unknown_eye_count;
                continue;
            }
            bucket->resources.push_back(&r);
            bucket->total_seen   += r.seen_count;
            bucket->total_changes += r.change_count;
            bucket->latest_frame = (std::max)(bucket->latest_frame, r.last_seen_frame);
        }

        for (size_t i = 0; i < shaders.d3d12_pso_aggregates.size(); ++i) {
            const auto& agg = shaders.d3d12_pso_aggregates[i];
            for (const auto& usage : agg.likely_targets) {
                const auto rt_side = classify_eye_text(usage.render_target_name);
                const auto dt_side = classify_eye_text(usage.depth_target_name);
                EyeSide side = (rt_side != EyeSide::Unknown) ? rt_side : dt_side;
                EyeBucket* bucket = (side == EyeSide::Left) ? &left
                                  : (side == EyeSide::Right) ? &right
                                  : nullptr;
                if (bucket != nullptr) {
                    bucket->pso_indices.emplace_back(i, usage.share);
                    bucket->bind_hit_count += usage.hit_count;
                }
            }
        }

        auto bucket_json = [&](const EyeBucket& b) {
            json out{
                {"resource_count", b.resources.size()},
                {"total_seen_count", b.total_seen},
                {"total_change_count", b.total_changes},
                {"latest_frame", b.latest_frame},
                {"bind_hit_count", b.bind_hit_count},
                {"resources", json::array()},
                {"pso_aggregates", json::array()},
            };
            for (const auto* r : b.resources) {
                out["resources"].push_back(minimal_resource_json(*r));
            }
            // De-dupe PSO indices (a PSO can target both an RT and a DSV).
            std::sort(const_cast<EyeBucket&>(b).pso_indices.begin(),
                      const_cast<EyeBucket&>(b).pso_indices.end(),
                      [](const auto& a, const auto& bb) { return a.first < bb.first; });
            size_t last = SIZE_MAX;
            for (const auto& [idx, share] : b.pso_indices) {
                if (idx == last) continue;
                last = idx;
                if (idx >= shaders.d3d12_pso_aggregates.size()) continue;
                const auto& agg = shaders.d3d12_pso_aggregates[idx];
                out["pso_aggregates"].push_back({
                    {"index", idx},
                    {"share", agg.sample_share},
                    {"total_samples", agg.total_samples},
                    {"vs_hash", agg.vs_hash},
                    {"ps_hash", agg.ps_hash},
                    {"gs_hash", agg.gs_hash},
                    {"vs_crc32", format_crc32(agg.vs_crc32)},
                    {"ps_crc32", format_crc32(agg.ps_crc32)},
                    {"gs_crc32", format_crc32(agg.gs_crc32)},
                    {"vs_override", agg.vs_override},
                    {"ps_override", agg.ps_override},
                    {"gs_override", agg.gs_override},
                    {"likely_targets_count", agg.likely_targets.size()},
                });
            }
            return out;
        };

        json result{
            {"frame", ri.inspector().current_frame()},
            {"renderer", g_framework ? (g_framework->is_dx12() ? "D3D12" : "D3D11") : "Unknown"},
            {"left", bucket_json(left)},
            {"right", bucket_json(right)},
            {"unknown_eye_resources", unknown_eye_count},
            {"asymmetries", json::array()},
        };

        auto add_warn = [&](std::string code, std::string message) {
            result["asymmetries"].push_back({{"code", std::move(code)}, {"message", std::move(message)}});
        };

        const bool have_left = !left.resources.empty();
        const bool have_right = !right.resources.empty();
        if (have_left && !have_right) {
            add_warn("missing_right_eye_resources",
                     "Left-eye textures tracked but no right-eye textures present in the resource list. "
                     "Right eye may not be rendering at all — check stereo enable / runtime / VR mod state.");
        }
        if (!have_left && have_right) {
            add_warn("missing_left_eye_resources",
                     "Right-eye textures tracked but no left-eye textures present.");
        }
        if (have_left && have_right) {
            auto rel_diff = [](uint64_t a, uint64_t b) -> double {
                const auto m = (std::max)(a, b);
                if (m == 0) return 0.0;
                const auto d = (a > b) ? (a - b) : (b - a);
                return static_cast<double>(d) / static_cast<double>(m);
            };

            const double seen_drift = rel_diff(left.total_seen, right.total_seen);
            const double change_drift = rel_diff(left.total_changes, right.total_changes);
            const double bind_drift = rel_diff(left.bind_hit_count, right.bind_hit_count);

            result["drift"] = {
                {"seen_count_rel_diff", seen_drift},
                {"change_count_rel_diff", change_drift},
                {"bind_hit_count_rel_diff", bind_drift},
            };

            if (seen_drift > 0.15) {
                add_warn("seen_count_asymmetric",
                         "Left/right eye resources have meaningfully different seen_count totals "
                         "(rel_diff " + std::to_string(seen_drift) + "). "
                         "One eye may be culled or have inconsistent passes.");
            }
            if (change_drift > 0.25 && (left.total_changes > 0 || right.total_changes > 0)) {
                add_warn("change_count_asymmetric",
                         "Per-frame content changes are very uneven between eyes "
                         "(rel_diff " + std::to_string(change_drift) + "). "
                         "One eye may be receiving fewer writes (frozen / not updated).");
            }
            if (left.total_changes == 0 && right.total_changes > 0) {
                add_warn("left_eye_static",
                         "Left eye textures show change_count=0 — no writes detected. Likely black/frozen.");
            }
            if (right.total_changes == 0 && left.total_changes > 0) {
                add_warn("right_eye_static",
                         "Right eye textures show change_count=0 — no writes detected. Likely black/frozen.");
            }
            if (left.pso_indices.empty() && !right.pso_indices.empty()) {
                add_warn("no_pso_to_left_eye",
                         "No PSO aggregates have any usage targeting the left eye. "
                         "Either tracking hasn't sampled enough or the left eye isn't being drawn into.");
            }
            if (right.pso_indices.empty() && !left.pso_indices.empty()) {
                add_warn("no_pso_to_right_eye",
                         "No PSO aggregates have any usage targeting the right eye.");
            }
        }

        // Classify the currently-bound RT (most useful for catching what's
        // being written *right now*).
        json current_class = json::object();
        if (d3d12.current_bind_context.has_value()) {
            const auto& ctx = *d3d12.current_bind_context;
            current_class["frame"] = ctx.frame;
            current_class["exact_this_frame"] = ctx.exact_this_frame;
            current_class["render_targets"] = json::array();
            for (const auto& rt : ctx.render_targets) {
                json item{
                    {"resource", format_pointer(rt.resource)},
                    {"name", rt.name},
                };
                switch (classify_eye_text(rt.name)) {
                case EyeSide::Left:  item["eye"] = "Left";    break;
                case EyeSide::Right: item["eye"] = "Right";   break;
                default:             item["eye"] = "Unknown"; break;
                }
                current_class["render_targets"].push_back(std::move(item));
            }
            if (ctx.depth_target.has_value()) {
                current_class["depth_target"] = {
                    {"resource", format_pointer(ctx.depth_target->resource)},
                    {"name", ctx.depth_target->name},
                };
            }
        }
        result["current_bind_classification"] = std::move(current_class);

        result["hint"] = "If a side reports total_changes=0 but the other > 0, the most common UEVR "
                        "causes are: (a) stereo mode disabled, (b) the engine path culling one eye "
                        "early (e.g. when motion-blur or screenspace effects are misconfigured), "
                        "(c) a custom shader override mishandling SV_RenderTargetArrayIndex or "
                        "VS instance ID. Confirm via uevr_render_capture_next_d3d12_change to "
                        "snapshot the offending PSO.";

        return publish(std::move(result));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_select_eye(int side) {
    try {
        auto& ri = *RenderInspector::get();
        const auto resources = ri.inspector().snapshot();
        const EyeSide target = (side == 1) ? EyeSide::Right : EyeSide::Left;

        const render::FrameResourceInspector::ResourceInfo* best = nullptr;
        for (const auto& r : resources) {
            if (classify_resource_eye(r) != target) continue;
            if (best == nullptr || r.last_seen_frame > best->last_seen_frame) {
                best = &r;
            }
        }
        if (best == nullptr) {
            return publish(json{
                {"selected", false},
                {"reason", "No resource matched the requested eye side. Enable force_resources_sampling "
                           "or open the Resources sidebar so the inspector has data to choose from."},
            });
        }
        ri.inspector().set_selected_resource(best->key);
        return publish(json{
            {"selected", true},
            {"side", side == 1 ? "Right" : "Left"},
            {"key", best->key},
            {"name", best->name},
            {"pointer", format_pointer(best->pointer)},
        });
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

// ── RenderDoc integration ────────────────────────────────────────────

namespace rdc = uevr::renderdoc_capture;

namespace {

struct RenderDocCaptureAttempt {
    bool ended{};
    bool had_exact_pair{};
    bool wildcard_fallback{};
    const char* mode{"wildcard_no_active_pair"};
    rdc::CapturePair pair{};
};

RenderDocCaptureAttempt renderdoc_capture_prefer_active_pair(std::chrono::milliseconds duration) {
    RenderDocCaptureAttempt attempt{};
    attempt.pair = rdc::active_window();
    attempt.had_exact_pair = attempt.pair.device != nullptr && attempt.pair.window != nullptr;

    if (attempt.had_exact_pair) {
        attempt.mode = "exact_pair";
        attempt.ended = rdc::capture_blocking(attempt.pair, duration);
        if (attempt.ended) {
            return attempt;
        }

        attempt.wildcard_fallback = true;
        attempt.mode = "exact_pair_failed_wildcard";
    }

    attempt.ended = rdc::capture_blocking({}, duration);
    if (!attempt.had_exact_pair) {
        attempt.mode = "wildcard_no_active_pair";
    }
    return attempt;
}

json renderdoc_object_snapshot_to_json(const rdc::ObjectSnapshot& snapshot) {
    return json{
        {"seen", snapshot.seen},
        {"pointer", format_pointer(reinterpret_cast<uintptr_t>(snapshot.pointer))},
        {"source", snapshot.source},
        {"vtable_module", snapshot.vtable_module},
        {"renderdoc_wrapped", snapshot.renderdoc_wrapped},
        {"sequence", snapshot.sequence},
    };
}

const rdc::ObjectOwnershipInfo* find_renderdoc_object(
    const std::vector<rdc::ObjectOwnershipInfo>& objects,
    rdc::ObjectKind kind
) {
    for (const auto& object : objects) {
        if (object.kind == kind) {
            return &object;
        }
    }
    return nullptr;
}

bool first_renderdoc_object_wrapped(
    const std::vector<rdc::ObjectOwnershipInfo>& objects,
    rdc::ObjectKind kind
) {
    const auto* object = find_renderdoc_object(objects, kind);
    return object != nullptr && object->first.seen && object->first.renderdoc_wrapped;
}

} // namespace

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_status_json() {
    try {
        auto* api = rdc::api();
        if (api == nullptr) {
            return publish(json{
                {"loaded", false},
                {"hint", "Launch the game via RenderDoc (or attach via 'Inject into Process') so "
                         "renderdoc.dll is in-process. No-op otherwise."},
            });
        }
        int major{}, minor{}, patch{};
        api->GetAPIVersion(&major, &minor, &patch);
        const uint32_t num_captures = api->GetNumCaptures();
        const auto bootstrap = rdc::status();
        const auto active_pair = rdc::active_window();
        const auto active_device_vtable_module = rdc::com_object_vtable_module(active_pair.device);
        const auto object_infos = rdc::object_ownership();
        const bool first_present_objects_wrapped =
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::PresentD3D12Device) &&
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::PresentDXGISwapChain) &&
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::PresentD3D12CommandQueue);
        const bool first_created_command_list_wrapped =
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::CreatedD3D12CommandList) ||
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::ExecuteD3D12CommandList);
        const bool first_dxgi_factory_wrapped =
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::DxgiFactoryCreateResult) ||
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::ObservedDXGIFactory);
        const bool first_resource_wrapped =
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::CreatedD3D12Resource) ||
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::ObservedD3D12Resource);
        const bool first_descriptor_heap_wrapped =
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::CreatedD3D12DescriptorHeap) ||
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::BoundD3D12DescriptorHeap);
        const bool first_root_signature_wrapped =
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::CreatedD3D12RootSignature) ||
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::BoundD3D12RootSignature);
        const bool first_pipeline_state_wrapped =
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::CreatedD3D12PipelineState) ||
            first_renderdoc_object_wrapped(object_infos, rdc::ObjectKind::BoundD3D12PipelineState);
        json captures = json::array();
        for (const auto& capture : rdc::captures()) {
            captures.push_back({
                {"index", capture.index},
                {"path", capture.path},
                {"timestamp", capture.timestamp},
            });
        }
        json ownership = json::array();
        for (const auto& object : object_infos) {
            ownership.push_back({
                {"kind", object.name},
                {"first", renderdoc_object_snapshot_to_json(object.first)},
                {"current", renderdoc_object_snapshot_to_json(object.current)},
            });
        }
        return publish(json{
            {"loaded", true},
            {"version", std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch)},
            {"num_captures", num_captures},
            {"is_target_control_connected", api->IsTargetControlConnected() != 0},
            {"is_frame_capturing", api->IsFrameCapturing() != 0},
            {"loaded_path", bootstrap.loaded_path},
            {"was_preloaded", bootstrap.was_preloaded},
            {"loaded_by_uevr", bootstrap.late_loaded},
            {"capture_safe", bootstrap.capture_safe},
            {"d3d12_loaded_before_bootstrap", bootstrap.d3d12_was_loaded},
            {"dxgi_loaded_before_bootstrap", bootstrap.dxgi_was_loaded},
            {"loaded_before_graphics_modules", bootstrap.loaded_before_graphics_modules},
            {"capture_path_template", rdc::capture_template()},
            {"active_device", format_pointer(reinterpret_cast<uintptr_t>(active_pair.device))},
            {"active_device_vtable_module", active_device_vtable_module},
            {"active_device_renderdoc_wrapped", rdc::com_object_looks_renderdoc_wrapped(active_pair.device)},
            {"active_window", format_pointer(reinterpret_cast<uintptr_t>(active_pair.window))},
            {"first_present_objects_renderdoc_wrapped", first_present_objects_wrapped},
            {"first_dxgi_factory_renderdoc_wrapped", first_dxgi_factory_wrapped},
            {"first_command_list_renderdoc_wrapped", first_created_command_list_wrapped},
            {"first_resource_renderdoc_wrapped", first_resource_wrapped},
            {"first_descriptor_heap_renderdoc_wrapped", first_descriptor_heap_wrapped},
            {"first_root_signature_renderdoc_wrapped", first_root_signature_wrapped},
            {"first_pipeline_state_renderdoc_wrapped", first_pipeline_state_wrapped},
            {"object_ownership", std::move(ownership)},
            {"captures", std::move(captures)},
        });
    } catch (const std::exception& e) {
        return publish(json{{"loaded", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_trigger_capture(int num_frames) {
    try {
        auto* api = rdc::api();
        if (api == nullptr) {
            return publish(json{
                {"ok", false},
                {"error", "RenderDoc API not loaded — launch the game via RenderDoc first."},
            });
        }
        const uint32_t frames = num_frames <= 1 ? 1u : static_cast<uint32_t>(num_frames);
        if (frames == 1) {
            // Prefer the exact pair tracked by D3D12 Present, then retain
            // wildcard capture as a diagnostics-only fallback.
            const auto attempt = renderdoc_capture_prefer_active_pair(std::chrono::milliseconds(250));
            return publish(json{
                {"ok", attempt.ended},
                {"queued_frames", frames},
                {"mode", attempt.mode},
                {"ended", attempt.ended},
                {"had_exact_pair", attempt.had_exact_pair},
                {"wildcard_fallback", attempt.wildcard_fallback},
                {"active_device", format_pointer(reinterpret_cast<uintptr_t>(attempt.pair.device))},
                {"active_window", format_pointer(reinterpret_cast<uintptr_t>(attempt.pair.window))},
                {"newest_capture", rdc::newest_capture_path()},
                {"num_captures", api->GetNumCaptures()},
            });
        } else {
            api->TriggerMultiFrameCapture(frames);
        }
        return publish(json{{"ok", true}, {"queued_frames", frames}});
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_launch_ui() {
    try {
        auto* api = rdc::api();
        if (api == nullptr) {
            return publish(json{{"ok", false}, {"error", "RenderDoc API not loaded"}});
        }
        const uint32_t pid = api->LaunchReplayUI(1, nullptr);
        if (pid == 0) {
            return publish(json{{"ok", false}, {"error", "LaunchReplayUI returned pid=0 (failed)"}});
        }
        return publish(json{{"ok", true}, {"pid", pid}});
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_set_capture_template(
    const char* path_template
) {
    try {
        auto* api = rdc::api();
        if (api == nullptr) {
            return publish(json{{"ok", false}, {"error", "RenderDoc API not loaded"}});
        }
        if (path_template != nullptr && *path_template != '\0') {
            rdc::set_capture_template(path_template);
        }
        return publish(json{{"ok", true}, {"template", rdc::capture_template()}});
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

// ── Proactive bootstrap (called from Framework::Framework) ───────────

extern "C" UEVR_RENDER_CAPI UevrRenderDocBootstrapResult uevr_renderdoc_bootstrap() {
    UevrRenderDocBootstrapResult r{};

    const auto result = rdc::bootstrap(rdc::env_truthy_w(L"UEVR_LOAD_RENDERDOC_DLL"));
    r.module = result.module;
    r.was_preloaded = result.was_preloaded;
    r.late_loaded = result.late_loaded;
    r.api_loaded = result.api_loaded;
    r.capture_safe = result.capture_safe;
    r.d3d12_was_loaded = result.d3d12_was_loaded;
    r.dxgi_was_loaded = result.dxgi_was_loaded;
    r.api_version_major = result.api_version_major;
    r.api_version_minor = result.api_version_minor;
    r.api_version_patch = result.api_version_patch;

    if (!r.api_loaded) {
        return r;
    }

    spdlog::info("[RenderDoc] API ready: v{}.{}.{}  (preloaded={})",
                 r.api_version_major, r.api_version_minor, r.api_version_patch,
                 r.was_preloaded);
    return r;
}

extern "C" UEVR_RENDER_CAPI bool uevr_renderdoc_is_api_loaded() {
    return rdc::is_api_loaded();
}

extern "C" UEVR_RENDER_CAPI bool uevr_renderdoc_capture_wildcard() {
    return renderdoc_capture_prefer_active_pair(std::chrono::milliseconds(250)).ended;
}

namespace {
std::thread g_renderdoc_watcher_thread{};
std::atomic<bool> g_renderdoc_watcher_stop{false};
std::atomic<bool> g_renderdoc_watcher_started{false};
std::atomic<uint64_t> g_renderdoc_watcher_capture_seq{0};

bool renderdoc_watcher_emit_sn2_sidecar() {
    return rdc::env_truthy_w(L"UEVR_SN2_RD_CAPTURE_ALSO_EMIT_SIDECAR") ||
           rdc::env_truthy_w(L"UEVR_RENDERDOC_EMIT_SN2_SIDECAR");
}

bool renderdoc_watcher_notify_sn2_capture() {
    return rdc::env_truthy_w(L"UEVR_SN2_CAPTURE_TRUTH") ||
           rdc::env_truthy_w(L"UEVR_SN2_TARGET_STATE_DUMP") ||
           rdc::env_truthy_w(L"UEVR_SN2_RESOURCE_LINEAGE") ||
           rdc::env_truthy_w(L"UEVR_SN2_DESCRIPTOR_HEAP_SNAPSHOT") ||
           rdc::env_truthy_w(L"UEVR_SN2_PROBE_POINTS") ||
           renderdoc_watcher_emit_sn2_sidecar();
}

uint64_t renderdoc_watcher_sn2_prearm_ms() {
    static const uint64_t ms = []() {
        const auto raw = rdc::env_string_a("UEVR_SN2_RENDERDOC_PREARM_MS");
        if (raw.empty()) return 0ull;
        char* end = nullptr;
        const unsigned long long value = std::strtoull(raw.c_str(), &end, 0);
        return end != raw.c_str() ? static_cast<uint64_t>(value) : 0ull;
    }();
    return ms;
}

void renderdoc_capture_watcher_loop() {
    namespace fs = std::filesystem;
    wchar_t tempw[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempw) == 0) {
        spdlog::warn("[RenderDoc] watcher: GetTempPathW failed");
        return;
    }
    fs::path sentinel = fs::path{tempw} / L"uevr_renderdoc_capture.req";
    spdlog::info("[RenderDoc] watcher: polling {} every 250ms", sentinel.string());

    while (!g_renderdoc_watcher_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        std::error_code ec;
        if (!fs::exists(sentinel, ec)) continue;

        // Read + consume the sentinel atomically.
        std::string content;
        try {
            std::ifstream f(sentinel);
            std::stringstream ss; ss << f.rdbuf();
            content = ss.str();
        } catch (...) {
            // ignore
        }
        fs::remove(sentinel, ec);

        // Parse the sentinel.
        std::string capture_template;
        int frames = 1;
        {
            std::istringstream iss(content);
            std::string line;
            int line_no = 0;
            while (std::getline(iss, line)) {
                // trim CR/whitespace
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                    line.pop_back();
                ++line_no;
                if (line_no == 1) {
                    capture_template = std::move(line);
                } else if (line.rfind("frames=", 0) == 0) {
                    try { frames = std::max(1, std::stoi(line.substr(7))); } catch (...) {}
                }
            }
        }

        auto* api = rdc::api();
        if (api == nullptr) {
            spdlog::warn("[RenderDoc] watcher: trigger received but API not loaded");
            continue;
        }
        if (!capture_template.empty()) {
            rdc::set_capture_template(capture_template);
            spdlog::info("[RenderDoc] watcher: capture template -> {}", capture_template);
        }
        const uint64_t sn2_seq = g_renderdoc_watcher_capture_seq.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool notify_sn2 = renderdoc_watcher_notify_sn2_capture();
        if (notify_sn2) {
            sn2_capture_truth_on_renderdoc_trigger(sn2_seq);
            spdlog::info("[RenderDoc] watcher: notified SN2 capture truth seq={}", sn2_seq);
            const uint64_t prearm_ms = renderdoc_watcher_sn2_prearm_ms();
            if (prearm_ms > 0) {
                spdlog::info("[RenderDoc] watcher: SN2 prearm sleep {} ms before capture", prearm_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(prearm_ms));
            }
        }
        if (frames > 1) {
            api->TriggerMultiFrameCapture(static_cast<uint32_t>(frames));
            spdlog::info("[RenderDoc] watcher: triggered {} frames", frames);
        } else {
            const auto attempt = renderdoc_capture_prefer_active_pair(std::chrono::milliseconds(250));
            spdlog::info("[RenderDoc] watcher: capture mode={} ended={} newest='{}'",
                         attempt.mode, attempt.ended, rdc::newest_capture_path());
        }
        if (notify_sn2 && renderdoc_watcher_emit_sn2_sidecar() && sn2_capture_sidecar::env_enabled()) {
            sn2_capture_sidecar::emit(sn2_seq);
            spdlog::info("[RenderDoc] watcher: emitted SN2 sidecar seq={}", sn2_seq);
        }
    }
}
} // namespace

extern "C" UEVR_RENDER_CAPI void uevr_renderdoc_start_capture_watcher() {
    bool expected = false;
    if (!g_renderdoc_watcher_started.compare_exchange_strong(expected, true)) {
        return; // already started
    }
    g_renderdoc_watcher_stop.store(false);
    g_renderdoc_watcher_thread = std::thread{renderdoc_capture_watcher_loop};
    g_renderdoc_watcher_thread.detach();
}

// ── VR mod state probe ───────────────────────────────────────────────

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_vr_state_json() {
    try {
        if (g_framework == nullptr || !g_framework->is_ready()) {
            return publish(json{{"error", "Framework not ready"}});
        }
        auto& vr = VR::get();
        if (vr == nullptr) {
            return publish(json{{"error", "VR mod not available"}});
        }

        json out;
        out["renderer"] = g_framework->is_dx12() ? "D3D12" : (g_framework->is_dx11() ? "D3D11" : "Unknown");
        out["framework_render_size"] = {g_framework->is_dx12() && g_framework->get_d3d12_hook() != nullptr
                                            ? g_framework->get_d3d12_hook()->get_render_width()
                                            : 0,
                                        g_framework->is_dx12() && g_framework->get_d3d12_hook() != nullptr
                                            ? g_framework->get_d3d12_hook()->get_render_height()
                                            : 0};
        if (g_framework->is_dx12() && g_framework->get_d3d12_hook() != nullptr) {
            out["framework_display_size"] = {g_framework->get_d3d12_hook()->get_display_width(),
                                              g_framework->get_d3d12_hook()->get_display_height()};
        }

        out["hmd_active"] = vr->is_hmd_active();
        out["using_controllers"] = vr->is_using_controllers();
        out["using_afr"] = vr->is_using_afr();
        out["using_synchronized_afr"] = vr->is_using_synchronized_afr();
        out["depth_enabled"] = vr->is_depth_enabled();
        out["depth_scale"] = vr->get_depth_scale();
        out["world_to_meters"] = vr->get_world_to_meters();
        out["decoupled_pitch"] = vr->is_decoupled_pitch_enabled();
        out["native_stereo_fix_enabled"] = vr->is_native_stereo_fix_enabled();
        out["native_stereo_fix_same_pass_enabled"] = vr->is_native_stereo_fix_same_pass_enabled();
        out["sceneview_compatibility"] = vr->is_sceneview_compatibility_enabled();
        out["splitscreen_compatibility"] = vr->is_splitscreen_compatibility_enabled();
        out["frame_count"] = vr->get_frame_count();
        out["hmd_width"] = vr->get_hmd_width();
        out["hmd_height"] = vr->get_hmd_height();

        // Runtime info
        json runtime;
        if (auto* rt = vr->get_runtime(); rt != nullptr) {
            runtime["name"] = std::string{rt->name()};
            runtime["loaded"] = rt->loaded;
            runtime["ready"] = rt->ready();
            runtime["is_openxr"] = rt->is_openxr();
            runtime["is_openvr"] = rt->is_openvr();
            if (rt->is_openxr()) {
                if (auto* openxr = vr->get_openxr_runtime(); openxr != nullptr) {
                    const auto now = std::chrono::steady_clock::now();
                    runtime["session_state"] = static_cast<int>(openxr->session_state);
                    runtime["session_state_name"] = openxr->get_session_state_string(openxr->session_state);
                    runtime["session_ready"] = openxr->session_ready;
                    runtime["can_run_frame_loop"] = openxr->can_run_frame_loop();
                    runtime["frame_synced"] = openxr->frame_synced;
                    runtime["frame_began"] = openxr->frame_began;
                    runtime["got_first_poses"] = openxr->got_first_poses;
                    runtime["got_first_valid_poses"] = openxr->got_first_valid_poses;
                    runtime["accepted_relaxed_startup_poses"] = openxr->accepted_relaxed_startup_poses;
                    runtime["ever_submitted"] = openxr->ever_submitted;
                    runtime["frame_state_should_render"] = openxr->frame_state.shouldRender == XR_TRUE;
                    runtime["internal_frame_count"] = openxr->internal_frame_count;
                    runtime["internal_render_frame_count"] = openxr->internal_render_frame_count;
                    runtime["has_render_frame_count"] = openxr->has_render_frame_count;
                    runtime["last_begin_frame_caller"] = openxr->last_begin_frame_caller != nullptr
                        ? openxr->last_begin_frame_caller
                        : "";
                    runtime["last_frame_synced_clear_reason"] = openxr->last_frame_synced_clear_reason != nullptr
                        ? openxr->last_frame_synced_clear_reason
                        : "";
                    runtime["frame_synced_skip_streak"] = openxr->frame_synced_skip_streak;
                    runtime["frame_began_skip_streak"] = openxr->frame_began_skip_streak;
                    runtime["forced_frame_recovery_count"] = openxr->forced_frame_recovery_count;
                    runtime["focused_frame_loop_recovery_count"] = openxr->focused_frame_loop_recovery_count;
                    runtime["last_successful_wait_age_ms"] = steady_age_ms(now, openxr->last_successful_wait_frame);
                    runtime["last_successful_begin_age_ms"] = steady_age_ms(now, openxr->last_successful_begin_frame);
                    runtime["last_successful_end_age_ms"] = steady_age_ms(now, openxr->last_successful_end_frame);
                    runtime["last_successful_pose_update_age_ms"] = steady_age_ms(now, openxr->last_successful_pose_update);
                }
            }
        } else {
            runtime["name"] = "none";
            runtime["loaded"] = false;
            runtime["ready"] = false;
        }
        out["runtime"] = std::move(runtime);

        // D3D12-specific (the most diagnostic for one-eye bugs)
        if (g_framework->is_dx12()) {
            auto& d12 = vr->d3d12();
            json d12j;
            d12j["initialized"] = d12.is_initialized();
            d12j["backbuffer_size"] = {d12.get_backbuffer_size()[0], d12.get_backbuffer_size()[1]};
            d12j["has_game_and_ui_textures"] = d12.has_game_and_ui_textures();
            d12j["shf_scene_mode"] = d12.get_shf_scene_mode_str();

            auto eye_json = [&](int side) {
                const auto tgt = d12.get_current_eye_target(side);
                json e;
                e["path"] = tgt.path;
                e["note"] = tgt.note;
                e["resource"] = format_pointer(reinterpret_cast<uintptr_t>(tgt.texture.Get()));
                e["region"] = {{"x", tgt.region_x}, {"y", tgt.region_y}, {"w", tgt.region_w}, {"h", tgt.region_h}};
                e["array_slice"] = tgt.array_slice;
                e["available"] = tgt.texture.Get() != nullptr;
                return e;
            };
            d12j["left_eye"]  = eye_json(0);
            d12j["right_eye"] = eye_json(1);
            // Back-compat fields
            d12j["left_eye_resource"]  = d12j["left_eye"]["resource"];
            d12j["right_eye_resource"] = d12j["right_eye"]["resource"];
            out["d3d12"] = std::move(d12j);
        }

        // High-signal hints for stereo bugs based on the state above.
        json hints = json::array();
        if (g_framework->is_dx12()) {
            auto& d12 = vr->d3d12();
            const std::string mode = d12.get_shf_scene_mode_str();
            if (mode == "Mono2D") {
                hints.push_back("ShfSceneMode=Mono2D — UEVR sees the game as MONO this frame. Stereo fix may be missing/disabled.");
            }
            if (mode == "Unknown") {
                hints.push_back("ShfSceneMode=Unknown — UEVR hasn't classified the scene this frame.");
            }
            // D3D12Component::is_initialized only reflects the OpenVR mirror
            // path; we no longer fire that hint here because get_current_eye_target
            // handles OpenXR + native stereo too. Only warn when *no* path
            // resolved (truly missing).
            const auto l_tgt = d12.get_current_eye_target(0);
            const auto r_tgt = d12.get_current_eye_target(1);
            if (l_tgt.texture.Get() == nullptr || r_tgt.texture.Get() == nullptr) {
                std::string m = "Missing one or both eye target textures (left.path=";
                m += l_tgt.path; m += ", right.path="; m += r_tgt.path; m += ").";
                hints.push_back(std::move(m));
            } else if (std::string{l_tgt.path} == "Framework/Swapchain") {
                hints.push_back("Falling back to the game's backbuffer as the eye target — UEVR's "
                                "OpenVR/OpenXR eye textures aren't allocated. Stereo path may be off.");
            }
        }
        if (vr->is_using_afr() && !vr->is_using_synchronized_afr()) {
            hints.push_back("AFR (non-synchronized) — eyes alternate frames; per-frame change_count asymmetry is expected.");
        }
        out["hints"] = std::move(hints);

        return publish(std::move(out));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

// ── CVar dump ────────────────────────────────────────────────────────

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_cvars_json(const char* filter) {
    try {
        auto& vr = VR::get();
        if (vr == nullptr) {
            return publish(json{{"error", "VR mod not available"}});
        }
        auto* mgr = vr->get_cvar_manager();
        if (mgr == nullptr) {
            return publish(json{{"error", "CVarManager not available"}});
        }
        std::string flt = (filter != nullptr) ? std::string{filter} : std::string{};
        std::string flt_lower = flt;
        std::transform(flt_lower.begin(), flt_lower.end(), flt_lower.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });

        const auto snap = mgr->ffi_snapshot();
        json cvars = json::array();
        size_t total = 0;
        for (const auto& c : snap) {
            ++total;
            if (!flt_lower.empty()) {
                std::string combo = c.module + "/" + c.name;
                std::transform(combo.begin(), combo.end(), combo.begin(),
                               [](unsigned char ch){ return (char)std::tolower(ch); });
                if (combo.find(flt_lower) == std::string::npos) continue;
            }
            cvars.push_back({
                {"module", c.module},
                {"name", c.name},
                {"key", c.key},
                {"frozen", c.frozen},
                {"frozen_int", c.frozen_int},
                {"frozen_float", c.frozen_float},
            });
        }
        return publish(json{
            {"total_tracked", total},
            {"returned", cvars.size()},
            {"filter", flt},
            {"cvars", std::move(cvars)},
        });
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

// ── Frame timing stats ───────────────────────────────────────────────

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_frame_timing_json() {
    try {
        if (g_framework == nullptr || !g_framework->is_ready()) {
            return publish(json{{"error", "Framework not ready"}});
        }
        if (!g_framework->is_dx12()) {
            return publish(json{{"error", "Frame timing stats only available on D3D12 currently"}});
        }
        auto& vr = VR::get();
        if (vr == nullptr) {
            return publish(json{{"error", "VR mod not available"}});
        }
        auto& d12 = vr->d3d12();
        auto to_j = [](const auto& t) {
            return json{{"count", t.count}, {"avg_ms", t.avg_ms}, {"max_ms", t.max_ms}};
        };
        return publish(json{
            {"on_frame",          to_j(d12.get_timing_on_frame())},
            {"ui_copy",           to_j(d12.get_timing_ui_copy())},
            {"swapchain_copy",    to_j(d12.get_timing_swapchain_copy())},
            {"openxr_submit",     to_j(d12.get_timing_openxr_submit())},
            {"spectator_mirror",  to_j(d12.get_timing_spectator_mirror())},
            {"post_present",      to_j(d12.get_timing_post_present())},
        });
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}

// ── Eye texture readback ─────────────────────────────────────────────

namespace {

struct ReadbackResult {
    bool ok{false};
    std::string error{};
    std::vector<uint8_t> bytes{};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t row_pitch{0};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
};

bool format_is_bgra8(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_B8G8R8A8_UNORM ||
        f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        f == DXGI_FORMAT_B8G8R8A8_TYPELESS;
}
bool format_is_rgba8(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM ||
        f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
        f == DXGI_FORMAT_R8G8B8A8_TYPELESS;
}
bool format_is_8bit_rgba_like(DXGI_FORMAT f) {
    return format_is_bgra8(f) || format_is_rgba8(f);
}

ReadbackResult readback_texture_region(
    ID3D12Resource* texture,
    UINT region_x, UINT region_y, UINT region_w, UINT region_h,
    D3D12_RESOURCE_STATES expected_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    UINT subresource_index = 0
) {
    ReadbackResult r{};
    if (texture == nullptr) { r.error = "null texture"; return r; }

    auto& hook = g_framework->get_d3d12_hook();
    if (hook == nullptr) { r.error = "no d3d12 hook"; return r; }
    auto* device = hook->get_device();
    if (device == nullptr) { r.error = "no device"; return r; }

    const auto desc = texture->GetDesc();
    const UINT tex_w = static_cast<UINT>(desc.Width);
    const UINT tex_h = desc.Height;
    const UINT subresource_count = static_cast<UINT>(desc.MipLevels) * static_cast<UINT>(desc.DepthOrArraySize);
    if (subresource_count == 0 || subresource_index >= subresource_count) {
        r.error = "subresource index out of range";
        return r;
    }
    if (tex_w == 0 || tex_h == 0) { r.error = "texture has zero dimensions"; return r; }
    if (region_w == 0 || region_h == 0) { region_w = tex_w; region_h = tex_h; region_x = 0; region_y = 0; }
    region_x = (std::min)(region_x, tex_w - 1);
    region_y = (std::min)(region_y, tex_h - 1);
    region_w = (std::min)(region_w, tex_w - region_x);
    region_h = (std::min)(region_h, tex_h - region_y);

    auto region_desc = desc;
    region_desc.Width = region_w;
    region_desc.Height = region_h;
    region_desc.DepthOrArraySize = 1;
    region_desc.MipLevels = 1;
    region_desc.SampleDesc.Count = 1;
    region_desc.SampleDesc.Quality = 0;
    region_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT num_rows{};
    UINT64 row_size_bytes{};
    UINT64 total_bytes{};
    device->GetCopyableFootprints(&region_desc, 0, 1, 0, &layout, &num_rows, &row_size_bytes, &total_bytes);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = total_bytes;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.Format = DXGI_FORMAT_UNKNOWN;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> readback{};
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
        r.error = "CreateCommittedResource(readback) failed"; return r;
    }

    // Dedicated, transient command queue/allocator/list so we don't fight the engine.
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue{};
    if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
        r.error = "CreateCommandQueue failed"; return r;
    }
    ComPtr<ID3D12CommandAllocator> alloc{};
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) {
        r.error = "CreateCommandAllocator failed"; return r;
    }
    ComPtr<ID3D12GraphicsCommandList> cmd{};
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd)))) {
        r.error = "CreateCommandList failed"; return r;
    }

    // Issue a barrier only if the caller specified a non-COMMON expected state.
    // Resources in COMMON state are implicitly promoted to COPY_SOURCE for a
    // single copy operation (and decay back), which is the documented behavior
    // for OpenXR-released swapchain images.
    const bool needs_barrier = (expected_state != D3D12_RESOURCE_STATE_COMMON);
    D3D12_RESOURCE_BARRIER b{};
    if (needs_barrier) {
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = texture;
        b.Transition.Subresource = subresource_index;
        b.Transition.StateBefore = expected_state;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmd->ResourceBarrier(1, &b);
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layout;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = subresource_index;
    D3D12_BOX box{region_x, region_y, 0, region_x + region_w, region_y + region_h, 1};
    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

    if (needs_barrier) {
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter = expected_state;
        cmd->ResourceBarrier(1, &b);
    }

    if (FAILED(cmd->Close())) { r.error = "Close cmd list failed"; return r; }
    ID3D12CommandList* lists[] = {cmd.Get()};
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence{};
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        r.error = "CreateFence failed"; return r;
    }
    HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (evt == nullptr) { r.error = "CreateEvent failed"; return r; }
    queue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, evt);
        WaitForSingleObject(evt, 5000);
    }
    CloseHandle(evt);

    uint8_t* mapped = nullptr;
    D3D12_RANGE rr{0, static_cast<SIZE_T>(total_bytes)};
    if (FAILED(readback->Map(0, &rr, (void**)&mapped)) || mapped == nullptr) {
        r.error = "Map readback failed"; return r;
    }
    r.bytes.assign(mapped, mapped + total_bytes);
    D3D12_RANGE write_nothing{0, 0};
    readback->Unmap(0, &write_nothing);

    r.ok = true;
    r.width = region_w;
    r.height = region_h;
    r.row_pitch = layout.Footprint.RowPitch;
    r.format = desc.Format;
    return r;
}

} // namespace

namespace {

const char* eye_pixel_sample_json_impl(
    int side,
    int sample_w,
    int sample_h,
    int sample_x,
    int sample_y,
    bool explicit_origin
) {
    try {
        if (g_framework == nullptr || !g_framework->is_ready()) {
            return publish(json{{"available", false}, {"error", "Framework not ready"}});
        }
        if (!g_framework->is_dx12()) {
            return publish(json{{"available", false}, {"error", "Eye pixel sampling currently D3D12 only"}});
        }
        auto& vr = VR::get();
        if (vr == nullptr) {
            return publish(json{{"available", false}, {"error", "VR mod not available"}});
        }
        const auto tgt = vr->d3d12().get_current_eye_target(side == 1 ? 1 : 0);
        if (tgt.texture.Get() == nullptr || tgt.region_w == 0 || tgt.region_h == 0) {
            return publish(json{{"available", false},
                                {"side", side == 1 ? "Right" : "Left"},
                                {"path", tgt.path},
                                {"note", tgt.note},
                                {"error", "No eye target resolved on any path (OpenVR / OpenXR / Framework swapchain)."}});
        }
        const UINT w = tgt.region_w;
        const UINT h = tgt.region_h;
        UINT sw = (sample_w <= 0) ? (std::min)(w, 64u) : (UINT)sample_w;
        UINT sh = (sample_h <= 0) ? (std::min)(h, 64u) : (UINT)sample_h;
        sw = (std::min)(sw, w);
        sh = (std::min)(sh, h);

        UINT local_x = (w - sw) / 2;
        UINT local_y = (h - sh) / 2;
        if (explicit_origin) {
            local_x = sample_x <= 0 ? 0u : static_cast<UINT>(sample_x);
            local_y = sample_y <= 0 ? 0u : static_cast<UINT>(sample_y);
            local_x = (std::min)(local_x, w - sw);
            local_y = (std::min)(local_y, h - sh);
        }
        const UINT cx = tgt.region_x + local_x;
        const UINT cy = tgt.region_y + local_y;

        // Pick the right expected state for the path so we don't fight the engine's barriers.
        D3D12_RESOURCE_STATES expected = D3D12_RESOURCE_STATE_COMMON;
        std::string p = tgt.path ? tgt.path : "";
        if (p == "OpenVR")                          expected = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        else if (p == "Framework/Swapchain")        expected = D3D12_RESOURCE_STATE_PRESENT;

        auto rb = readback_texture_region(tgt.texture.Get(), cx, cy, sw, sh, expected, tgt.array_slice);
        if (!rb.ok) {
            return publish(json{{"available", false}, {"error", rb.error}});
        }
        if (!format_is_8bit_rgba_like(rb.format)) {
            return publish(json{
                {"available", false},
                {"width", rb.width},
                {"height", rb.height},
                {"format", (int)rb.format},
                {"error", "Eye texture not in an 8-bit RGBA/BGRA format; stats not computed."},
            });
        }
        const bool is_bgra = format_is_bgra8(rb.format);
        // R, G, B, A across the region.
        uint64_t sum[4] = {0, 0, 0, 0};
        uint8_t mn[4] = {255, 255, 255, 255};
        uint8_t mx[4] = {0, 0, 0, 0};
        uint64_t pixel_count = 0;
        for (UINT y = 0; y < rb.height; ++y) {
            const uint8_t* row = rb.bytes.data() + (size_t)y * rb.row_pitch;
            for (UINT x = 0; x < rb.width; ++x) {
                const uint8_t* p = row + (size_t)x * 4;
                uint8_t r = is_bgra ? p[2] : p[0];
                uint8_t g = p[1];
                uint8_t bl = is_bgra ? p[0] : p[2];
                uint8_t a = p[3];
                uint8_t v[4] = {r, g, bl, a};
                for (int i = 0; i < 4; ++i) {
                    sum[i] += v[i];
                    if (v[i] < mn[i]) mn[i] = v[i];
                    if (v[i] > mx[i]) mx[i] = v[i];
                }
                ++pixel_count;
            }
        }
        json rgba_min  = {mn[0], mn[1], mn[2], mn[3]};
        json rgba_max  = {mx[0], mx[1], mx[2], mx[3]};
        json rgba_mean = {
            pixel_count == 0 ? 0.0 : (double)sum[0] / pixel_count,
            pixel_count == 0 ? 0.0 : (double)sum[1] / pixel_count,
            pixel_count == 0 ? 0.0 : (double)sum[2] / pixel_count,
            pixel_count == 0 ? 0.0 : (double)sum[3] / pixel_count,
        };
        const bool is_black = (mx[0] <= 3 && mx[1] <= 3 && mx[2] <= 3);
        const bool is_uniform = (mn[0] == mx[0] && mn[1] == mx[1] && mn[2] == mx[2]);
        json out{
            {"available", true},
            {"side", side == 1 ? "Right" : "Left"},
            {"path", tgt.path},
            {"region", {{"x", tgt.region_x}, {"y", tgt.region_y}, {"w", w}, {"h", h}}},
            {"width", w},
            {"height", h},
            {"sampled_w", rb.width},
            {"sampled_h", rb.height},
            {"sampled_at", {{"x", cx}, {"y", cy}}},
            {"sampled_at_eye", {{"x", local_x}, {"y", local_y}}},
            {"sample_origin", explicit_origin ? "explicit_eye_relative" : "center"},
            {"array_slice", tgt.array_slice},
            {"format", (int)rb.format},
            {"channels", "RGBA8"},
            {"rgba_min", std::move(rgba_min)},
            {"rgba_max", std::move(rgba_max)},
            {"rgba_mean", std::move(rgba_mean)},
            {"is_black", is_black},
            {"is_uniform", is_uniform},
            {"pixel_count", pixel_count},
        };
        if (is_black) {
            out["hint"] = "Eye region is solid black (max RGB <= 3). Confirm this side isn't being written.";
        } else if (is_uniform) {
            out["hint"] = "Eye region is solid color — probably a clear value without subsequent draws.";
        }
        return publish(std::move(out));
    } catch (const std::exception& e) {
        return publish(json{{"available", false}, {"error", e.what()}});
    }
}

} // namespace

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_eye_pixel_sample_json(
    int side, int sample_w, int sample_h
) {
    return eye_pixel_sample_json_impl(side, sample_w, sample_h, 0, 0, false);
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_eye_region_sample_json(
    int side, int sample_x, int sample_y, int sample_w, int sample_h
) {
    return eye_pixel_sample_json_impl(side, sample_w, sample_h, sample_x, sample_y, true);
}

// ── Eye dump to disk (PNG / JPG / BMP via WIC) ───────────────────────

namespace {

GUID wic_container_for_fmt(int fmt) {
    switch (fmt) {
        case 0: return GUID_ContainerFormatPng;
        case 1: return GUID_ContainerFormatJpeg;
        case 2: return GUID_ContainerFormatBmp;
        default: return GUID_ContainerFormatPng;
    }
}
const char* fmt_extension(int fmt) {
    switch (fmt) { case 0: return ".png"; case 1: return ".jpg"; case 2: return ".bmp"; default: return ".png"; }
}
const char* fmt_name(int fmt) {
    switch (fmt) { case 0: return "PNG"; case 1: return "JPG"; case 2: return "BMP"; default: return "PNG"; }
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

bool save_rgba8_to_file(
    const uint8_t* bytes, uint32_t width, uint32_t height, uint32_t row_pitch,
    bool is_bgra_source, int fmt, const std::wstring& path, std::string& err_out
) {
    ComPtr<IWICImagingFactory> wic{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        // Try again after CoInitializeEx (in case it hasn't been called on this thread).
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
            err_out = "Failed to create IWICImagingFactory"; return false;
        }
    }

    ComPtr<IWICStream> stream{};
    if (FAILED(wic->CreateStream(&stream))) { err_out = "CreateStream failed"; return false; }
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        err_out = "InitializeFromFilename failed"; return false;
    }
    ComPtr<IWICBitmapEncoder> encoder{};
    if (FAILED(wic->CreateEncoder(wic_container_for_fmt(fmt), nullptr, &encoder))) {
        err_out = "CreateEncoder failed"; return false;
    }
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        err_out = "Encoder Initialize failed"; return false;
    }
    ComPtr<IWICBitmapFrameEncode> frame{};
    if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) { err_out = "CreateNewFrame failed"; return false; }
    if (FAILED(frame->Initialize(nullptr))) { err_out = "Frame Initialize failed"; return false; }
    frame->SetSize(width, height);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&pf);

    // Build a tightly-packed buffer in BGRA so we don't depend on the encoder
    // accepting an arbitrary stride.
    std::vector<uint8_t> packed((size_t)width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src_row = bytes + (size_t)y * row_pitch;
        uint8_t* dst_row = packed.data() + (size_t)y * width * 4;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* sp = src_row + (size_t)x * 4;
            uint8_t* dp = dst_row + (size_t)x * 4;
            if (is_bgra_source) {
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            } else {
                dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = sp[3];
            }
        }
    }
    if (FAILED(frame->WritePixels(height, width * 4, (UINT)packed.size(), packed.data()))) {
        err_out = "WritePixels failed"; return false;
    }
    if (FAILED(frame->Commit())) { err_out = "Frame Commit failed"; return false; }
    if (FAILED(encoder->Commit())) { err_out = "Encoder Commit failed"; return false; }
    return true;
}

std::filesystem::path default_eye_dump_path(int side, int fmt) {
    const auto base = Framework::get_persistent_dir("render_inspector") / "eye_dumps";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream name;
    name << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << (side == 1 ? "right" : "left") << fmt_extension(fmt);
    return base / name.str();
}

std::optional<std::string> env_value_a(const char* name) {
    if (name == nullptr || *name == '\0') {
        return std::nullopt;
    }

    char buffer[4096]{};
    const DWORD len = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (len == 0) {
        return std::nullopt;
    }
    if (len >= std::size(buffer)) {
        std::string value(static_cast<size_t>(len) + 1, '\0');
        const DWORD read = GetEnvironmentVariableA(name, value.data(), len + 1);
        if (read == 0) {
            return std::nullopt;
        }
        value.resize(read);
        return value;
    }

    return std::string{buffer, len};
}

bool env_truthy(const std::optional<std::string>& value) {
    if (!value.has_value() || value->empty()) {
        return false;
    }

    std::string lower{};
    lower.reserve(8);
    for (const char c : *value) {
        if (lower.size() >= 16) {
            break;
        }
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    return lower != "0" &&
           lower != "false" &&
           lower != "no" &&
           lower != "off" &&
           lower != "disable" &&
           lower != "disabled";
}

json env_entry_json(const char* name) {
    const auto value = env_value_a(name);
    return {
        {"name", name},
        {"set", value.has_value()},
        {"value", value.value_or("")},
        {"truthy", env_truthy(value)},
    };
}

bool string_in_list(std::string_view value, std::initializer_list<std::string_view> list) {
    for (const auto item : list) {
        if (value == item) {
            return true;
        }
    }
    return false;
}

json sn2_env_snapshot_json() {
    static constexpr const char* kNames[] = {
        "UEVR_DIAG_CLEAN",
        "UEVR_HIDE_MENU_ON_STARTUP",
        "UEVR_FORCE_MENU_CLOSED",
        "UEVR_D3D12_STRIP_CACHED_PSO",
        "UEVR_ENABLE_D3D12_GPU_TIMESTAMPS",
        "UEVR_SN2_FOG_SRV_REDIRECT",
        "UEVR_SN2_FOG_SRV_REDIRECT_SLOTS",
        "UEVR_SN2_COPYRECT_RIGHT_TABLE0_FROM_LEFT",
        "UEVR_SN2_COPYRECT_CB2_MODE",
        "UEVR_SN2_COPYRECT_RIGHT_CBV_FROM_LEFT",
        "UEVR_SN2_UPSTREAM_SKIP_CRCS",
        "UEVR_SN2_SKYATMOS_SKIP_RIGHT",
        "UEVR_SN2_PSO3069_SUBST",
        "UEVR_SN2_PSO3069_CAPTURE_NEXT",
        "UEVR_SN2_PSO3069_CAPTURE_MARK_FILE",
        "UEVR_SN2_BASEPASS_SNAPSHOT",
        "UEVR_SN2_TAIL_SRV_REPAIR",
        "UEVR_SN2_TAIL_SRV_REPAIR_TARGET_CRC",
        "UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_PER_VIEW_HOOK",
        "UEVR_SUBNAUTICA2_DISABLE_VOLUMETRIC_FOG_VIEW_LOOP_FIX",
        "UEVR_SUBNAUTICA2_DISABLE_VOLUMETRIC_RT_REPLAY",
        "UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_VIEW_INDEX_FORCE",
        "UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_STATE_COPY",
        "UEVR_SUBNAUTICA2_DISABLE_COMPOSE_VOLUMETRIC_VIEW_RECT_FIX",
        "UEVR_SUBNAUTICA2_DISABLE_RENDER_FOG_VIEW_RECT_FIX",
        "UEVR_SUBNAUTICA2_DISABLE_UNDERWATER_FOG_VIEW_DATA_FIX",
        "UEVR_SUBNAUTICA2_DISABLE_SINGLE_LAYER_WATER_VIEW_RECT_FIX",
        "UEVR_SUBNAUTICA2_FOG_ALIAS_THUNK_DIAG",
        "UEVR_SUBNAUTICA2_FOG_ALIAS_THUNK_MODE",
        "UEVR_SUBNAUTICA2_LIGHT_AFFECTS_VIEW_DIAG",
        "UEVR_SUBNAUTICA2_LIGHT_AFFECTS_VIEW_MODE",
        "UEVR_SUBNAUTICA2_VISIBLE_LIGHT_INFOS_COPY_MODE",
        "UEVR_SUBNAUTICA2_ENABLE_UWE_TRACE",
        "UEVR_SUBNAUTICA2_ENABLE_UWE_FD0A50_DETAIL",
        "UEVR_SUBNAUTICA2_FD0A50_RIGHT_OUT_FROM_LEFT",
        "UEVR_SUBNAUTICA2_FOG_PATH_A",
        "UEVR_SUBNAUTICA2_FOG_SWAP_SRC_HANDLE",
        "UEVR_SUBNAUTICA2_FOG_SWAP_DST_HANDLE",
        "UEVR_SUBNAUTICA2_FOG_SWAP_SRC_INDEX",
        "UEVR_SUBNAUTICA2_FOG_SWAP_DST_INDEX",
        "UEVR_SUBNAUTICA2_FOG_SWAP_SWEEP",
        "UEVR_SUBNAUTICA2_FOG_SWAP_USE_UAV_TAG",
    };

    json env = json::array();
    json active = json::array();
    json active_mutations = json::array();
    json active_safety = json::array();
    json active_diagnostics = json::array();
    json active_params = json::array();
    json active_ui = json::array();

    auto classify_active = [&](std::string_view name) {
        if (name == "UEVR_HIDE_MENU_ON_STARTUP" || name == "UEVR_FORCE_MENU_CLOSED") {
            active_ui.push_back(name);
            return;
        }
        if (name.rfind("UEVR_SUBNAUTICA2_DISABLE_", 0) == 0) {
            active_safety.push_back(name);
            return;
        }
        if (string_in_list(name, {
                "UEVR_SN2_FOG_SRV_REDIRECT_SLOTS",
                "UEVR_SN2_TAIL_SRV_REPAIR_TARGET_CRC",
                "UEVR_SN2_PSO3069_CAPTURE_MARK_FILE",
                "UEVR_SUBNAUTICA2_FOG_SWAP_SRC_HANDLE",
                "UEVR_SUBNAUTICA2_FOG_SWAP_DST_HANDLE",
                "UEVR_SUBNAUTICA2_FOG_SWAP_SRC_INDEX",
                "UEVR_SUBNAUTICA2_FOG_SWAP_DST_INDEX",
            })) {
            active_params.push_back(name);
            return;
        }
        if (string_in_list(name, {
                "UEVR_DIAG_CLEAN",
                "UEVR_D3D12_STRIP_CACHED_PSO",
                "UEVR_ENABLE_D3D12_GPU_TIMESTAMPS",
                "UEVR_SN2_PSO3069_CAPTURE_NEXT",
                "UEVR_SN2_BASEPASS_SNAPSHOT",
                "UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_PER_VIEW_HOOK",
                "UEVR_SUBNAUTICA2_FOG_ALIAS_THUNK_DIAG",
                "UEVR_SUBNAUTICA2_LIGHT_AFFECTS_VIEW_DIAG",
                "UEVR_SUBNAUTICA2_ENABLE_UWE_TRACE",
                "UEVR_SUBNAUTICA2_ENABLE_UWE_FD0A50_DETAIL",
            })) {
            active_diagnostics.push_back(name);
            return;
        }
        active_mutations.push_back(name);
    };

    for (const char* name : kNames) {
        auto entry = env_entry_json(name);
        if (entry.value("truthy", false)) {
            active.push_back(name);
            classify_active(name);
        }
        env.push_back(std::move(entry));
    }

    return {
        {"entries", std::move(env)},
        {"active_truthy_names", std::move(active)},
        {"active_mutation_names", std::move(active_mutations)},
        {"active_safety_names", std::move(active_safety)},
        {"active_diagnostic_names", std::move(active_diagnostics)},
        {"active_param_names", std::move(active_params)},
        {"active_ui_names", std::move(active_ui)},
    };
}

} // namespace

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_sn2_state_json() {
    try {
        json result{
            {"available", true},
            {"framework_ready", g_framework != nullptr && g_framework->is_ready()},
            {"renderer", g_framework != nullptr
                ? (g_framework->is_dx12() ? "D3D12" : (g_framework->is_dx11() ? "D3D11" : "Unknown"))
                : "Unknown"},
            {"persistent_dir", g_framework != nullptr ? Framework::get_persistent_dir().string() : std::string{}},
            {"current_fog_view", sn2_get_current_fog_view()},
            {"environment", sn2_env_snapshot_json()},
            {"sn2_descriptor_maps", {
                {"fog_srv_total", sn2_fog_srv_map::size()},
                {"fog_srv_view0", sn2_fog_srv_map::count_by_view(0)},
                {"fog_srv_view1", sn2_fog_srv_map::count_by_view(1)},
                {"fog_srv_unknown", sn2_fog_srv_map::count_by_view(-1)},
                {"fog_uav_total", sn2_fog_uav_map::size()},
                {"fog_uav_view0", sn2_fog_uav_map::count_by_view(0)},
                {"fog_uav_view1", sn2_fog_uav_map::count_by_view(1)},
                {"fog_uav_unknown", sn2_fog_uav_map::count_by_view(-1)},
                {"bindless_slot_total", sn2_bindless_slot_map::size()},
                {"bindless_slot_view0", sn2_bindless_slot_map::count_by_view(0)},
                {"bindless_slot_view1", sn2_bindless_slot_map::count_by_view(1)},
                {"bindless_slot_unknown", sn2_bindless_slot_map::count_by_view(-1)},
            }},
        };

        json hints = json::array();
        const auto active_mutations = result["environment"]["active_mutation_names"];
        if (active_mutations.is_array() && !active_mutations.empty()) {
            hints.push_back("One or more SN2/UEVR diagnostic mutations are active. Confirm this is intentional before treating the run as a clean baseline.");
        }
        if (env_truthy(env_value_a("UEVR_D3D12_STRIP_CACHED_PSO"))) {
            hints.push_back("Cached PSO stripping is active. This is useful for making stream PSOs substitutable, but it changes the PSO creation path.");
        }
        if (!env_truthy(env_value_a("UEVR_SUBNAUTICA2_ENABLE_VOLUMETRIC_FOG_PER_VIEW_HOOK"))) {
            hints.push_back("The volumetric-fog per-view hook is not enabled, so current_fog_view and fog SRV/UAV view tagging may stay stale or unknown.");
        }
        result["hints"] = std::move(hints);
        return publish(std::move(result));
    } catch (const std::exception& e) {
        return publish(json{{"available", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_eye_dump_json(
    int side, const char* out_path, int fmt
) {
    try {
        if (g_framework == nullptr || !g_framework->is_ready()) {
            return publish(json{{"ok", false}, {"error", "Framework not ready"}});
        }
        if (!g_framework->is_dx12()) {
            return publish(json{{"ok", false}, {"error", "Eye dump currently D3D12 only"}});
        }
        auto& vr = VR::get();
        if (vr == nullptr) {
            return publish(json{{"ok", false}, {"error", "VR mod not available"}});
        }
        const auto tgt = vr->d3d12().get_current_eye_target(side == 1 ? 1 : 0);
        if (tgt.texture.Get() == nullptr || tgt.region_w == 0 || tgt.region_h == 0) {
            return publish(json{{"ok", false},
                                {"path", tgt.path},
                                {"note", tgt.note},
                                {"error", "No eye target resolved on any path"}});
        }
        D3D12_RESOURCE_STATES expected = D3D12_RESOURCE_STATE_COMMON;
        std::string p = tgt.path ? tgt.path : "";
        if (p == "OpenVR")                          expected = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        else if (p == "Framework/Swapchain")        expected = D3D12_RESOURCE_STATE_PRESENT;

        auto rb = readback_texture_region(tgt.texture.Get(), tgt.region_x, tgt.region_y, tgt.region_w, tgt.region_h, expected, tgt.array_slice);
        if (!rb.ok) {
            return publish(json{{"ok", false}, {"path", tgt.path}, {"error", rb.error}});
        }
        if (!format_is_8bit_rgba_like(rb.format)) {
            return publish(json{
                {"ok", false},
                {"path", tgt.path},
                {"format", (int)rb.format},
                {"error", "Eye texture not in an 8-bit RGBA/BGRA format; cannot encode."},
            });
        }

        std::filesystem::path path = (out_path != nullptr && *out_path != '\0')
            ? std::filesystem::path(out_path)
            : default_eye_dump_path(side, fmt);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::string err;
        const bool saved = save_rgba8_to_file(
            rb.bytes.data(), rb.width, rb.height, rb.row_pitch,
            format_is_bgra8(rb.format),
            fmt, path.wstring(), err
        );
        if (!saved) {
            return publish(json{{"ok", false}, {"side", side == 1 ? "Right" : "Left"}, {"path", path.string()}, {"error", err}});
        }
        return publish(json{
            {"ok", true},
            {"side", side == 1 ? "Right" : "Left"},
            {"path", path.string()},
            {"width", rb.width},
            {"height", rb.height},
            {"format", fmt_name(fmt)},
            {"source_path", tgt.path},
            {"source_region", {{"x", tgt.region_x}, {"y", tgt.region_y}, {"w", tgt.region_w}, {"h", tgt.region_h}}},
            {"array_slice", tgt.array_slice},
        });
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

// ── D3D12 stereo trace (viewport / draw / clear classification) ──────

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_set_stereo_trace_enabled(int enabled) {
    try {
        d3d12_stereo_trace::set_ffi_enabled(enabled != 0);
        return publish(json{{"ok", true}, {"enabled", d3d12_stereo_trace::is_ffi_enabled()}});
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_stereo_trace_json(int reset) {
    try {
        const auto s = d3d12_stereo_trace::peek();
        if (reset != 0) {
            d3d12_stereo_trace::reset();
        }
        auto bucket = [](uint64_t l, uint64_t r, uint64_t f, uint64_t m, uint64_t u) {
            return json{
                {"left", l}, {"right", r}, {"full", f}, {"multi", m}, {"unknown", u},
                {"total", l + r + f + m + u},
                {"left_right_ratio", r == 0 ? (l == 0 ? 0.0 : 9999.0) : (double)l / (double)r},
            };
        };
        json out;
        out["ffi_enabled"] = d3d12_stereo_trace::is_ffi_enabled();
        out["reset_after_read"] = (reset != 0);
        out["viewports"]     = bucket(s.viewport_left, s.viewport_right, s.viewport_full, s.viewport_multi, s.viewport_unknown);
        out["draws"]         = bucket(s.draw_left,     s.draw_right,     s.draw_full,     s.draw_multi,     s.draw_unknown);
        out["draws_indexed"] = bucket(s.draw_indexed_left, s.draw_indexed_right, s.draw_indexed_full, s.draw_indexed_multi, s.draw_indexed_unknown);
        out["clears"]        = bucket(s.clear_left,    s.clear_right,    s.clear_full,    s.clear_multi,    s.clear_unknown);
        out["om_set_render_targets"] = s.om_set_render_targets;
        out["resource_barriers"] = s.resource_barriers;

        json hints = json::array();
        auto add_hint = [&](std::string m) { hints.push_back(std::move(m)); };
        const auto idx_total = s.draw_indexed_left + s.draw_indexed_right;
        if (idx_total > 1000) {
            const double right_share = (double)s.draw_indexed_right / (double)idx_total;
            if (right_share < 0.1) {
                add_hint("Indexed draws are dominated by the LEFT viewport (" +
                    std::to_string(s.draw_indexed_left) + " vs " + std::to_string(s.draw_indexed_right) +
                    "). Right eye is being set up but most scene work is culled or skipped before / during main passes.");
            } else if (right_share > 0.9) {
                add_hint("Indexed draws are dominated by the RIGHT viewport — left eye may be the broken one.");
            }
        }
        if (s.viewport_right == 0 && s.viewport_left > 0) {
            add_hint("RSSetViewports never used the right-eye rect this window — the engine isn't even targeting the right half.");
        }
        if (s.clear_left > 0 && s.clear_right == 0) {
            add_hint("Only the LEFT half is being cleared. Right half retains whatever was there (often black on a fresh RT, or smearing on a reused one).");
        }
        out["hints"] = std::move(hints);
        return publish(std::move(out));
    } catch (const std::exception& e) {
        return publish(json{{"error", e.what()}});
    }
}
