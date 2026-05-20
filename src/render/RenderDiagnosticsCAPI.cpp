#include "render/RenderDiagnosticsCAPI.hpp"

#define RENDERDOC_NO_STDINT
#include "renderdoc_app.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Framework.hpp"
#include "hooks/D3D12Hook.hpp"
#include "mods/RenderInspector.hpp"
#include "mods/VR.hpp"
#include "mods/vr/CVarManager.hpp"
#include "mods/vr/D3D12Component.hpp"
#include "render/D3D12Diagnostics.hpp"
#include "render/FrameResourceInspector.hpp"
#include "render/RenderAnalysisExport.hpp"
#include "render/ShaderOverrideRegistry.hpp"

using json = nlohmann::json;
template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

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

const char* backend_name(render::FrameResourceInspector::Backend backend) {
    return backend == render::FrameResourceInspector::Backend::D3D12 ? "D3D12" : "D3D11";
}

const char* backend_name(render::ShaderOverrideRegistry::Backend backend) {
    return backend == render::ShaderOverrideRegistry::Backend::D3D12 ? "D3D12" : "D3D11";
}

const char* stage_name(render::ShaderOverrideRegistry::Stage stage) {
    return stage == render::ShaderOverrideRegistry::Stage::Pixel ? "PS" : "VS";
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

json bound_shader_to_json(const render::ShaderOverrideRegistry::BoundShaderInfo& s) {
    return {
        {"known", s.known},
        {"backend", backend_name(s.backend)},
        {"stage", stage_name(s.stage)},
        {"original_pointer", format_pointer(s.original_pointer)},
        {"bound_pointer", format_pointer(s.bound_pointer)},
        {"hash", s.hash},
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
        {"vs_override", a.vs_override},
        {"ps_override", a.ps_override},
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
        {"generation", e.generation},
        {"status", e.status},
        {"compiler", e.compiler},
        {"last_error", e.last_error},
    };
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
        {"frame", s.frame},
        {"global_override_dir", s.global_override_dir},
        {"profile_override_dir", s.profile_override_dir},
        {"bound_vertex_shader", bound_shader_to_json(s.bound_vertex_shader)},
        {"bound_pixel_shader", bound_shader_to_json(s.bound_pixel_shader)},
        {"capture_next_d3d12_change_armed", s.capture_next_d3d12_change_armed},
        {"total_d3d12_pair_samples", s.total_d3d12_pair_samples},
        {"total_d3d12_pso_samples", s.total_d3d12_pso_samples},
        {"distinct_d3d12_pairs", json::array()},
        {"d3d12_pso_aggregates", json::array()},
        {"overrides", json::array()},
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
                    {"vs_override", agg.vs_override},
                    {"ps_override", agg.ps_override},
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

namespace {

std::mutex g_renderdoc_mutex{};
RENDERDOC_API_1_7_0* g_renderdoc_api{nullptr};
bool g_renderdoc_attempted{false};

void try_load_renderdoc() {
    std::lock_guard lock{g_renderdoc_mutex};
    if (g_renderdoc_api != nullptr) return;

    HMODULE mod = GetModuleHandleA("renderdoc.dll");
    if (mod == nullptr) {
        // RenderDoc can be injected after the diagnostics UI has already queried
        // status once. Treat "module absent" as retryable instead of caching it.
        return;
    }
    if (g_renderdoc_attempted) return;
    g_renderdoc_attempted = true;

    auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
    if (get_api == nullptr) {
        spdlog::warn("[RenderCAPI] renderdoc.dll is loaded but RENDERDOC_GetAPI is missing");
        return;
    }
    if (get_api(eRENDERDOC_API_Version_1_7_0, reinterpret_cast<void**>(&g_renderdoc_api)) != 1 ||
        g_renderdoc_api == nullptr) {
        // Try older API versions in case the loaded runtime is older than our header.
        get_api(eRENDERDOC_API_Version_1_4_0, reinterpret_cast<void**>(&g_renderdoc_api));
    }
    if (g_renderdoc_api == nullptr) {
        spdlog::warn("[RenderCAPI] renderdoc.dll loaded but GetAPI returned no compatible interface");
        return;
    }
    int major{}, minor{}, patch{};
    g_renderdoc_api->GetAPIVersion(&major, &minor, &patch);
    spdlog::info("[RenderCAPI] RenderDoc API loaded: v{}.{}.{}", major, minor, patch);
}

RENDERDOC_API_1_7_0* rdoc() {
    try_load_renderdoc();
    return g_renderdoc_api;
}

} // namespace

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_status_json() {
    try {
        auto* api = rdoc();
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
        json captures = json::array();
        for (uint32_t i = 0; i < num_captures; ++i) {
            uint32_t pathlen = 0;
            uint64_t timestamp = 0;
            if (api->GetCapture(i, nullptr, &pathlen, &timestamp) && pathlen > 0) {
                std::string path(pathlen, '\0');
                if (api->GetCapture(i, path.data(), nullptr, nullptr)) {
                    // GetCapture writes a null terminator; trim it.
                    if (!path.empty() && path.back() == '\0') path.pop_back();
                    captures.push_back({{"index", i}, {"path", path}, {"timestamp", timestamp}});
                }
            }
        }
        const char* tmpl = api->GetCaptureFilePathTemplate();
        return publish(json{
            {"loaded", true},
            {"version", std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch)},
            {"num_captures", num_captures},
            {"is_target_control_connected", api->IsTargetControlConnected() != 0},
            {"is_frame_capturing", api->IsFrameCapturing() != 0},
            {"capture_path_template", tmpl == nullptr ? "" : tmpl},
            {"captures", std::move(captures)},
        });
    } catch (const std::exception& e) {
        return publish(json{{"loaded", false}, {"error", e.what()}});
    }
}

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_renderdoc_trigger_capture(int num_frames) {
    try {
        auto* api = rdoc();
        if (api == nullptr) {
            return publish(json{
                {"ok", false},
                {"error", "RenderDoc API not loaded — launch the game via RenderDoc first."},
            });
        }
        const uint32_t frames = num_frames <= 1 ? 1u : static_cast<uint32_t>(num_frames);
        if (frames == 1) {
            // Late-injected RenderDoc sessions can ignore TriggerCapture() because
            // no active window/device pair has been selected yet. Wildcard
            // Start/EndFrameCapture captures any active graphics API pair.
            api->StartFrameCapture(nullptr, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const auto ended = api->EndFrameCapture(nullptr, nullptr);
            return publish(json{
                {"ok", ended != 0},
                {"queued_frames", frames},
                {"mode", "start_end_wildcard"},
                {"ended", ended != 0},
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
        auto* api = rdoc();
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
        auto* api = rdoc();
        if (api == nullptr) {
            return publish(json{{"ok", false}, {"error", "RenderDoc API not loaded"}});
        }
        if (path_template != nullptr && *path_template != '\0') {
            api->SetCaptureFilePathTemplate(path_template);
        }
        const char* tmpl = api->GetCaptureFilePathTemplate();
        return publish(json{{"ok", true}, {"template", tmpl == nullptr ? "" : tmpl}});
    } catch (const std::exception& e) {
        return publish(json{{"ok", false}, {"error", e.what()}});
    }
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

extern "C" UEVR_RENDER_CAPI const char* uevr_render_diag_eye_pixel_sample_json(
    int side, int sample_w, int sample_h
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
        // Centre the sample within the eye region (NOT the full texture).
        const UINT cx = tgt.region_x + (w - sw) / 2;
        const UINT cy = tgt.region_y + (h - sh) / 2;

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

} // namespace

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
