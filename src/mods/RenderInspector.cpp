#include "mods/RenderInspector.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <sstream>
#include <type_traits>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Framework.hpp"
#include "mods/VR.hpp"

namespace {
enum class ResourceColumn : ImGuiID {
    Name = 1,
    Source,
    Backend,
    Type,
    Format,
    Resolution,
    Age,
    Tags,
};

const char* backend_to_string(render::FrameResourceInspector::Backend backend) {
    switch (backend) {
    case render::FrameResourceInspector::Backend::D3D11:
        return "D3D11";
    case render::FrameResourceInspector::Backend::D3D12:
        return "D3D12";
    default:
        return "Unknown";
    }
}

std::string format_pointer_hex(uintptr_t pointer) {
    std::ostringstream ss{};
    ss << "0x" << std::hex << std::uppercase << pointer;
    return ss.str();
}

std::string abbreviate_for_table(std::string_view value, size_t max_chars = 14) {
    if (value.size() <= max_chars) {
        return std::string{value};
    }

    if (max_chars <= 3) {
        return std::string{value.substr(0, max_chars)};
    }

    return std::string{value.substr(0, max_chars - 3)} + "...";
}

std::string format_bytes(uint64_t bytes) {
    constexpr const char* suffixes[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    size_t suffix_index = 0;

    while (value >= 1024.0 && suffix_index < 3) {
        value /= 1024.0;
        ++suffix_index;
    }

    char buffer[64]{};
    sprintf_s(buffer, "%.2f %s", value, suffixes[suffix_index]);
    return buffer;
}

ImTextureID to_imgui_texture_id(uint64_t texture_id) {
    return reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture_id));
}

bool env_flag_enabled(const char* name, bool default_value = false) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }

    return value[0] != '0' && value[0] != 'f' && value[0] != 'F' &&
        value[0] != 'n' && value[0] != 'N';
}

int env_int_value(const char* name, int default_value, int min_value = 0) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }

    const int parsed = std::atoi(value);
    return parsed < min_value ? min_value : parsed;
}

std::string make_d3d12_pair_key(const render::ShaderOverrideRegistry::D3D12PipelinePairInfo& pair) {
    std::ostringstream ss{};
    ss << std::hex << std::uppercase
       << pair.original_pipeline_state << ':'
       << pair.bound_pipeline_state << ':'
       << pair.vertex_shader.hash << ':'
       << pair.pixel_shader.hash << ':'
       << pair.tracking_note;
    return ss.str();
}

std::string make_d3d12_pso_key(const render::ShaderOverrideRegistry::D3D12PsoAggregateInfo& aggregate) {
    std::ostringstream ss{};
    ss << std::hex << std::uppercase
       << aggregate.original_pso << ':'
       << aggregate.vs_hash << ':'
       << aggregate.ps_hash << ':'
       << static_cast<uint32_t>(aggregate.pipeline_stream) << ':'
       << aggregate.tracking_note;
    return ss.str();
}

bool matches_filters(
    const render::FrameResourceInspector::ResourceInfo& resource,
    uint64_t current_frame,
    bool filter_depth_only,
    bool filter_render_targets_only,
    bool filter_ui_only,
    bool filter_swapchain_only,
    bool filter_recent_only,
    int recent_frame_window
) {
    const auto age = current_frame >= resource.last_seen_frame ? (current_frame - resource.last_seen_frame) : 0;

    if (filter_recent_only && age > static_cast<uint64_t>(recent_frame_window)) {
        return false;
    }

    if (filter_depth_only && !resource.is_depth) {
        return false;
    }

    if (filter_render_targets_only && !resource.is_render_target) {
        return false;
    }

    if (filter_ui_only && !resource.is_ui) {
        return false;
    }

    if (filter_swapchain_only && !resource.is_swapchain) {
        return false;
    }

    return true;
}

int compare_uint64(uint64_t lhs, uint64_t rhs) {
    if (lhs < rhs) {
        return -1;
    }

    if (lhs > rhs) {
        return 1;
    }

    return 0;
}

int compare_uint32(uint32_t lhs, uint32_t rhs) {
    if (lhs < rhs) {
        return -1;
    }

    if (lhs > rhs) {
        return 1;
    }

    return 0;
}

int compare_strings(const std::string& lhs, const std::string& rhs) {
    if (lhs < rhs) {
        return -1;
    }

    if (lhs > rhs) {
        return 1;
    }

    return 0;
}

template <typename T>
int apply_direction(int result, T direction) {
    return direction == ImGuiSortDirection_Ascending ? result : -result;
}

void sort_resources(
    std::vector<render::FrameResourceInspector::ResourceInfo>& resources,
    const ImGuiTableSortSpecs* sort_specs,
    uint64_t current_frame
) {
    if (sort_specs == nullptr || sort_specs->SpecsCount <= 0) {
        return;
    }

    std::stable_sort(resources.begin(), resources.end(), [sort_specs, current_frame](const auto& a, const auto& b) {
        for (int sort_index = 0; sort_index < sort_specs->SpecsCount; ++sort_index) {
            const auto& spec = sort_specs->Specs[sort_index];
            int result = 0;

            switch (static_cast<ResourceColumn>(spec.ColumnUserID)) {
            case ResourceColumn::Name:
                result = compare_strings(a.name, b.name);
                break;
            case ResourceColumn::Source:
                result = compare_strings(a.source, b.source);
                break;
            case ResourceColumn::Backend:
                result = compare_strings(backend_to_string(a.backend), backend_to_string(b.backend));
                break;
            case ResourceColumn::Type:
                result = compare_strings(a.type, b.type);
                break;
            case ResourceColumn::Format:
                result = compare_strings(a.format, b.format);
                break;
            case ResourceColumn::Resolution:
                result = compare_uint32(a.width, b.width);
                if (result == 0) {
                    result = compare_uint32(a.height, b.height);
                }
                break;
            case ResourceColumn::Age: {
                const auto age_a = current_frame >= a.last_seen_frame ? (current_frame - a.last_seen_frame) : 0;
                const auto age_b = current_frame >= b.last_seen_frame ? (current_frame - b.last_seen_frame) : 0;
                result = compare_uint64(age_a, age_b);
                break;
            }
            case ResourceColumn::Tags:
                result = compare_strings(a.tags, b.tags);
                break;
            default:
                break;
            }

            if (result != 0) {
                return apply_direction(result, spec.SortDirection) < 0;
            }
        }

        return a.key < b.key;
    });
}

const render::FrameResourceInspector::ResourceInfo* find_resource(
    const std::vector<render::FrameResourceInspector::ResourceInfo>& resources,
    std::optional<uint64_t> key
) {
    if (!key.has_value()) {
        return nullptr;
    }

    const auto it = std::find_if(resources.begin(), resources.end(), [key](const auto& resource) {
        return resource.key == *key;
    });

    return it != resources.end() ? &*it : nullptr;
}

void draw_resource_metadata(const render::FrameResourceInspector::ResourceInfo& resource, uint64_t current_frame) {
    const auto age = current_frame >= resource.last_seen_frame ? (current_frame - resource.last_seen_frame) : 0;

    ImGui::TextWrapped("%s", resource.name.c_str());
    ImGui::Separator();
    ImGui::Text("Pointer: 0x%p", reinterpret_cast<void*>(resource.pointer));
    ImGui::Text("Backend: %s", backend_to_string(resource.backend));
    ImGui::Text("Source: %s", resource.source.c_str());
    ImGui::Text("Type: %s", resource.type.c_str());
    ImGui::Text("Format: %s", resource.format.c_str());
    ImGui::Text("Resolution: %s", resource.resolution.c_str());
    ImGui::Text("Tags: %s", resource.tags.c_str());
    ImGui::Text("First seen: %" PRIu64, resource.first_seen_frame);
    ImGui::Text("Last seen: %" PRIu64, resource.last_seen_frame);
    ImGui::Text("Age: %" PRIu64 " frames", age);
    ImGui::Text("Seen count: %" PRIu64, resource.seen_count);
    ImGui::Text("Change count: %" PRIu64, resource.change_count);
}

void draw_resource_flags(const render::FrameResourceInspector::ResourceInfo& resource) {
    auto draw_flag = [](const char* label, bool value) {
        ImGui::BulletText("%s: %s", label, value ? "yes" : "no");
    };

    draw_flag("Depth", resource.is_depth);
    draw_flag("RT", resource.is_render_target);
    draw_flag("UI", resource.is_ui);
    draw_flag("Eye", resource.is_eye);
    draw_flag("Swapchain", resource.is_swapchain);
    draw_flag("Velocity?", resource.is_velocity_candidate);
    draw_flag("RT Pool", resource.is_rt_pool);
    draw_flag("Transient", resource.is_transient);
}

void draw_dx12_summary_row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", value.c_str());
}

void draw_bound_shader_table_row(const render::ShaderOverrideRegistry::BoundShaderInfo& shader) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(shader.stage == render::ShaderOverrideRegistry::Stage::Vertex ? "VS" : "PS");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(shader.backend == render::ShaderOverrideRegistry::Backend::D3D11 ? "DX11" : "DX12");
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", shader.known ? shader.hash.c_str() : "-");
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", shader.override_active ? shader.override_name.c_str() : "None");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(format_pointer_hex(shader.original_pointer).c_str());
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", shader.note.empty() ? "-" : shader.note.c_str());
}

void draw_d3d12_pair_summary(const render::ShaderOverrideRegistry::D3D12PipelinePairInfo& pair) {
    ImGui::Text("Frame: %" PRIu64, pair.frame);
    ImGui::Text("First seen: %" PRIu64, pair.first_seen_frame);
    ImGui::Text("Last seen: %" PRIu64, pair.last_seen_frame);
    ImGui::Text("Hits: %" PRIu64, pair.hit_count);
    ImGui::Text("Original PSO: %s", format_pointer_hex(pair.original_pipeline_state).c_str());
    ImGui::Text("Bound PSO: %s", format_pointer_hex(pair.bound_pipeline_state).c_str());
    ImGui::Text("Pipeline Stream: %s", pair.pipeline_stream ? "yes" : "no");
    ImGui::TextWrapped("Tracking: %s", pair.tracking_note.empty() ? "-" : pair.tracking_note.c_str());

    if (ImGui::BeginTable("CapturedDX12PairStages", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Backend");
        ImGui::TableSetupColumn("Hash");
        ImGui::TableSetupColumn("Override");
        ImGui::TableSetupColumn("Original");
        ImGui::TableSetupColumn("Note");
        ImGui::TableHeadersRow();
        draw_bound_shader_table_row(pair.vertex_shader);
        draw_bound_shader_table_row(pair.pixel_shader);
        ImGui::EndTable();
    }
}

std::string format_percent(double value) {
    char buffer[32]{};
    sprintf_s(buffer, "%.2f%%", value * 100.0);
    return buffer;
}

void draw_d3d12_pso_summary(const render::ShaderOverrideRegistry::D3D12PsoAggregateInfo& aggregate) {
    ImGui::Text("Samples: %" PRIu64, aggregate.total_samples);
    ImGui::Text("Share: %s", format_percent(aggregate.sample_share).c_str());
    ImGui::Text("Target associations: %" PRIu64, aggregate.bind_count_with_known_targets);
    ImGui::Text("First seen: %" PRIu64, aggregate.first_seen_frame);
    ImGui::Text("Last seen: %" PRIu64, aggregate.last_seen_frame);
    ImGui::Text("Original PSO: %s", format_pointer_hex(aggregate.original_pso).c_str());
    ImGui::Text("Last bound PSO: %s", format_pointer_hex(aggregate.last_bound_pso).c_str());
    ImGui::Text("Pipeline Stream: %s", aggregate.pipeline_stream ? "yes" : "no");
    ImGui::TextWrapped("Tracking: %s", aggregate.tracking_note.empty() ? "-" : aggregate.tracking_note.c_str());
    ImGui::TextWrapped("VS: %s", aggregate.vs_hash.empty() ? "-" : aggregate.vs_hash.c_str());
    ImGui::TextWrapped("PS: %s", aggregate.ps_hash.empty() ? "-" : aggregate.ps_hash.c_str());
    std::string override_summary{};
    if (!aggregate.vs_override.empty()) {
        override_summary += "VS:";
        override_summary += aggregate.vs_override;
    }
    if (!aggregate.ps_override.empty()) {
        if (!override_summary.empty()) {
            override_summary += " | ";
        }
        override_summary += "PS:";
        override_summary += aggregate.ps_override;
    }
    ImGui::TextWrapped("Overrides: %s", override_summary.empty() ? "None" : override_summary.c_str());

    if (aggregate.likely_targets.empty()) {
        ImGui::TextUnformatted("No RT/depth associations recorded.");
        return;
    }

    if (ImGui::BeginTable("SelectedPsoLikelyTargets", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Hits");
        ImGui::TableSetupColumn("Share");
        ImGui::TableSetupColumn("Render Target");
        ImGui::TableSetupColumn("Depth");
        ImGui::TableSetupColumn("Keys");
        ImGui::TableHeadersRow();

        for (const auto& usage : aggregate.likely_targets) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIu64, usage.hit_count);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_percent(usage.share).c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", usage.render_target_name.empty() ? "-" : usage.render_target_name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", usage.depth_target_name.empty() ? "-" : usage.depth_target_name.c_str());
            ImGui::TableNextColumn();
            std::string keys = usage.render_target_key;
            if (!usage.depth_target_key.empty()) {
                if (!keys.empty()) {
                    keys += " | ";
                }
                keys += usage.depth_target_key;
            }
            ImGui::TextWrapped("%s", keys.empty() ? "-" : keys.c_str());
        }

        ImGui::EndTable();
    }
}

const render::ShaderOverrideRegistry::D3D12PipelinePairInfo* find_d3d12_pair(
    const std::vector<render::ShaderOverrideRegistry::D3D12PipelinePairInfo>& pairs,
    const std::string& key
) {
    if (key.empty()) {
        return nullptr;
    }

    const auto it = std::find_if(pairs.begin(), pairs.end(), [&key](const auto& pair) {
        return make_d3d12_pair_key(pair) == key;
    });

    return it != pairs.end() ? &*it : nullptr;
}

const render::ShaderOverrideRegistry::D3D12PsoAggregateInfo* find_d3d12_pso(
    const std::vector<render::ShaderOverrideRegistry::D3D12PsoAggregateInfo>& aggregates,
    const std::string& key
) {
    if (key.empty()) {
        return nullptr;
    }

    const auto it = std::find_if(aggregates.begin(), aggregates.end(), [&key](const auto& aggregate) {
        return make_d3d12_pso_key(aggregate) == key;
    });

    return it != aggregates.end() ? &*it : nullptr;
}
} // namespace

std::shared_ptr<RenderInspector>& RenderInspector::get() {
    static std::shared_ptr<RenderInspector> instance{std::make_shared<RenderInspector>()};
    return instance;
}

render::FrameResourceInspector& RenderInspector::inspector() {
    return m_inspector;
}

const render::FrameResourceInspector& RenderInspector::inspector() const {
    return m_inspector;
}

void RenderInspector::set_force_resources_sampling(bool v) {
    m_force_resources_sampling.store(v);
}

bool RenderInspector::force_resources_sampling() const {
    return m_force_resources_sampling.load();
}

void RenderInspector::set_force_shader_tracking(bool v) {
    m_force_shader_tracking.store(v);
}

bool RenderInspector::force_shader_tracking() const {
    return m_force_shader_tracking.load();
}

void RenderInspector::set_force_d3d12_diagnostics(bool v) {
    m_force_d3d12_diagnostics.store(v);
}

bool RenderInspector::force_d3d12_diagnostics() const {
    return m_force_d3d12_diagnostics.load();
}

void RenderInspector::service_shader_hunter_autotest() {
    ++m_hunter_autotest_frame;

    if (!m_hunter_autotest_initialized) {
        m_hunter_autotest_initialized = true;
        m_hunter_autotest_enabled = env_flag_enabled("UEVR_SHADER_HUNTER_AUTOTEST");
        if (m_hunter_autotest_enabled) {
            m_hunter_autotest_suppress = env_flag_enabled("UEVR_SHADER_HUNTER_AUTOTEST_SUPPRESS", false);
            m_hunter_autotest_delay_frames =
                env_int_value("UEVR_SHADER_HUNTER_AUTOTEST_DELAY_FRAMES", m_hunter_autotest_delay_frames);
            m_hunter_autotest_collect_frames =
                env_int_value("UEVR_SHADER_HUNTER_AUTOTEST_COLLECT_FRAMES", m_hunter_autotest_collect_frames, 1);
            m_hunter_autotest_step_interval_frames =
                env_int_value("UEVR_SHADER_HUNTER_AUTOTEST_STEP_INTERVAL_FRAMES", m_hunter_autotest_step_interval_frames, 1);
            m_hunter_autotest_steps =
                env_int_value("UEVR_SHADER_HUNTER_AUTOTEST_STEPS", m_hunter_autotest_steps, 0);
            m_hunter_autotest_start_index =
                env_int_value("UEVR_SHADER_HUNTER_AUTOTEST_START_INDEX", m_hunter_autotest_start_index, -1);

            set_force_shader_tracking(true);
            set_force_d3d12_diagnostics(true);
            auto& reg = render::ShaderOverrideRegistry::get();
            reg.hunter_set_suppression_enabled(m_hunter_autotest_suppress);
            spdlog::info(
                "[ShaderHunter][autotest] enabled delay={} collect={} step_interval={} steps={} start_index={} suppress={}",
                m_hunter_autotest_delay_frames,
                m_hunter_autotest_collect_frames,
                m_hunter_autotest_step_interval_frames,
                m_hunter_autotest_steps,
                m_hunter_autotest_start_index,
                m_hunter_autotest_suppress ? 1 : 0);
        }
    }

    if (!m_hunter_autotest_enabled || m_hunter_autotest_finished) {
        return;
    }

    set_force_shader_tracking(true);
    set_force_d3d12_diagnostics(true);

    auto& reg = render::ShaderOverrideRegistry::get();

    if (!m_hunter_autotest_started) {
        if (m_hunter_autotest_frame < static_cast<uint64_t>(m_hunter_autotest_delay_frames)) {
            return;
        }

        reg.hunter_set_frame_window(m_hunter_autotest_collect_frames);
        reg.hunter_start();
        m_hunter_autotest_started = true;
        m_hunter_autotest_start_frame = m_hunter_autotest_frame;
        m_hunter_autotest_last_step_frame = m_hunter_autotest_frame;
        spdlog::info("[ShaderHunter][autotest] started at inspector_frame={}", m_hunter_autotest_frame);
        return;
    }

    auto state = reg.hunter_state();
    if ((m_hunter_autotest_frame % 120) == 0) {
        spdlog::info(
            "[ShaderHunter][autotest] state collected={} live={} scene_live={} active={} age={} active_hash={} suppress={}",
            state.collected_count,
            state.live_count,
            state.scene_live_count,
            state.active ? 1 : 0,
            state.active_age_frames,
            state.active_hash,
            state.suppression_enabled ? 1 : 0);
    }

    const uint64_t frames_since_start = m_hunter_autotest_frame >= m_hunter_autotest_start_frame
        ? (m_hunter_autotest_frame - m_hunter_autotest_start_frame)
        : 0;
    const bool collection_elapsed =
        frames_since_start >= static_cast<uint64_t>(m_hunter_autotest_collect_frames + 15);
    const bool collection_done = state.window_stopped || collection_elapsed;
    if (!collection_done) {
        return;
    }

    if (!m_hunter_autotest_start_index_applied && m_hunter_autotest_start_index >= 0) {
        reg.hunter_set_index(m_hunter_autotest_start_index);
        state = reg.hunter_state();
        m_hunter_autotest_start_index_applied = true;
        m_hunter_autotest_last_step_frame = m_hunter_autotest_frame;
        spdlog::info(
            "[ShaderHunter][autotest] start_index active_idx={} active_hash={} crc32=0x{:08X} collected={} live={} scene_live={} age={}",
            state.active_index,
            state.active_hash,
            state.active_crc32,
            state.collected_count,
            state.live_count,
            state.scene_live_count,
            state.active_age_frames);
        return;
    }

    if (m_hunter_autotest_steps_done < m_hunter_autotest_steps) {
        if ((m_hunter_autotest_frame - m_hunter_autotest_last_step_frame) <
                static_cast<uint64_t>(m_hunter_autotest_step_interval_frames)) {
            return;
        }

        reg.hunter_step(+1);
        state = reg.hunter_state();
        ++m_hunter_autotest_steps_done;
        m_hunter_autotest_last_step_frame = m_hunter_autotest_frame;
        spdlog::info(
            "[ShaderHunter][autotest] step {}/{} active_idx={} active_hash={} crc32=0x{:08X} collected={} live={} scene_live={} age={}",
            m_hunter_autotest_steps_done,
            m_hunter_autotest_steps,
            state.active_index,
            state.active_hash,
            state.active_crc32,
            state.collected_count,
            state.live_count,
            state.scene_live_count,
            state.active_age_frames);
        return;
    }

    if ((m_hunter_autotest_frame - m_hunter_autotest_last_step_frame) <
            static_cast<uint64_t>(m_hunter_autotest_step_interval_frames)) {
        return;
    }

    state = reg.hunter_state();
    reg.hunter_stop();
    reg.hunter_set_suppression_enabled(true);
    set_force_shader_tracking(false);
    set_force_d3d12_diagnostics(false);
    m_hunter_autotest_finished = true;
    spdlog::info(
        "[ShaderHunter][autotest] finished collected={} live={} scene_live={} steps={} final_hash={}",
        state.collected_count,
        state.live_count,
        state.scene_live_count,
        m_hunter_autotest_steps_done,
        state.active_hash);
}

void RenderInspector::on_present() {
    if (g_framework == nullptr || !g_framework->is_ready()) {
        return;
    }

    service_shader_hunter_autotest();

    const auto resources_active = g_framework->is_sidebar_entry_selected("Resources") ||
                                   m_force_resources_sampling.load();
    const auto shader_tracking_active =
        g_framework->is_sidebar_entry_selected("PSO Profiler") ||
        g_framework->is_sidebar_entry_selected("Shaders") ||
        g_framework->is_sidebar_entry_selected("Shader Hunter") ||
        m_force_shader_tracking.load();
    const auto dx12_diagnostics_active =
        g_framework->is_dx12() &&
        (g_framework->is_sidebar_entry_selected("DX12 Diagnostics") ||
         shader_tracking_active ||
         m_force_d3d12_diagnostics.load());

    render::D3D12Diagnostics::get().set_enabled(dx12_diagnostics_active);
    render::ShaderOverrideRegistry::get().set_inspector_tracking_enabled(shader_tracking_active);
    render::ShaderOverrideRegistry::get().on_present(*g_framework);

    if (resources_active) {
        if (auto& vr = VR::get(); vr != nullptr) {
            m_inspector.on_present(*g_framework, *vr);
        }
    } else {
        // Keep stale resource inspector state from accumulating when the page is not active.
        m_inspector.reset();
    }
}

void RenderInspector::on_draw_sidebar_entry(std::string_view in_entry) {
    if (in_entry == "Resources") {
        draw_resources();
    } else if (in_entry == "DX12 Diagnostics") {
        draw_dx12_diagnostics();
    } else if (in_entry == "PSO Profiler") {
        draw_pso_profiler();
    } else if (in_entry == "Shaders") {
        draw_shaders();
    } else if (in_entry == "Shader Hunter") {
        draw_shader_hunter();
    } else if (in_entry == "Eye Diff") {
        draw_eye_diff();
    }
}

void RenderInspector::draw_resources() {
    auto resources = m_inspector.snapshot();
    const auto current_frame = m_inspector.current_frame();
    auto preview = m_inspector.preview_info();

    ImGui::Text("Renderer: %s", g_framework->is_dx12() ? "D3D12" : "D3D11");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Tracked: %zu", resources.size());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Frame: %" PRIu64, current_frame);

    if (g_framework->is_dx11() && g_framework->get_d3d11_hook() != nullptr) {
        ImGui::Text(
            "Swapchain: 0x%p | Last DSV: 0x%p",
            g_framework->get_d3d11_hook()->get_swap_chain(),
            g_framework->get_d3d11_hook()->get_last_depthstencil_used().Get()
        );
    } else if (g_framework->is_dx12() && g_framework->get_d3d12_hook() != nullptr) {
        ImGui::Text(
            "Swapchain: 0x%p | Render: %ux%u | Display: %ux%u",
            g_framework->get_d3d12_hook()->get_swap_chain(),
            g_framework->get_d3d12_hook()->get_render_width(),
            g_framework->get_d3d12_hook()->get_render_height(),
            g_framework->get_d3d12_hook()->get_display_width(),
            g_framework->get_d3d12_hook()->get_display_height()
        );
    }

    ImGui::Separator();

    ImGui::Checkbox("Depth only", &m_filter_depth_only);
    ImGui::SameLine();
    ImGui::Checkbox("Render targets only", &m_filter_render_targets_only);
    ImGui::SameLine();
    ImGui::Checkbox("UI only", &m_filter_ui_only);
    ImGui::SameLine();
    ImGui::Checkbox("Swapchain only", &m_filter_swapchain_only);
    ImGui::SameLine();
    ImGui::Checkbox("Recent only", &m_filter_recent_only);

    if (m_filter_recent_only) {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::DragInt("Recent window (frames)", &m_recent_frame_window, 1.0f, 1, 3600);
    }

    if (ImGui::Button("Clear Selection")) {
        m_selected_resource_key.reset();
        m_inspector.set_selected_resource(std::nullopt);
        preview = m_inspector.preview_info();
    }

    ImGui::Separator();

    std::vector<render::FrameResourceInspector::ResourceInfo> filtered{};
    filtered.reserve(resources.size());

    for (const auto& resource : resources) {
        if (matches_filters(
            resource,
            current_frame,
            m_filter_depth_only,
            m_filter_render_targets_only,
            m_filter_ui_only,
            m_filter_swapchain_only,
            m_filter_recent_only,
            m_recent_frame_window
        )) {
            filtered.emplace_back(resource);
        }
    }

    constexpr auto layout_flags =
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_BordersInnerV;

    if (ImGui::BeginTable("RenderInspectorLayout", 2, layout_flags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("ResourceList", ImGuiTableColumnFlags_WidthStretch, 1.55f);
        ImGui::TableSetupColumn("ResourceDetails", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::BeginChild("RenderInspectorResourceList", ImVec2(0.0f, 0.0f), false);
        {
            constexpr auto table_flags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_ScrollX |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_Reorderable |
                ImGuiTableFlags_Sortable |
                ImGuiTableFlags_SizingFixedFit;

            if (ImGui::BeginTable("RenderInspectorResources", 8, table_flags, ImVec2(0.0f, 0.0f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name / Guess", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 2.8f, static_cast<ImGuiID>(ResourceColumn::Name));
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 1.8f, static_cast<ImGuiID>(ResourceColumn::Source));
                ImGui::TableSetupColumn("Backend", ImGuiTableColumnFlags_WidthFixed, 0.9f, static_cast<ImGuiID>(ResourceColumn::Backend));
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 1.0f, static_cast<ImGuiID>(ResourceColumn::Type));
                ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 1.6f, static_cast<ImGuiID>(ResourceColumn::Format));
                ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 1.2f, static_cast<ImGuiID>(ResourceColumn::Resolution));
                ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 0.8f, static_cast<ImGuiID>(ResourceColumn::Age));
                ImGui::TableSetupColumn("Tags", ImGuiTableColumnFlags_WidthStretch, 1.8f, static_cast<ImGuiID>(ResourceColumn::Tags));
                ImGui::TableHeadersRow();

                if (auto* sort_specs = ImGui::TableGetSortSpecs(); sort_specs != nullptr) {
                    if (sort_specs->SpecsDirty) {
                        sort_resources(filtered, sort_specs, current_frame);
                        sort_specs->SpecsDirty = false;
                    } else {
                        sort_resources(filtered, sort_specs, current_frame);
                    }
                }

                for (const auto& resource : filtered) {
                    const auto age = current_frame >= resource.last_seen_frame ? (current_frame - resource.last_seen_frame) : 0;
                    const auto is_selected = m_selected_resource_key.has_value() && *m_selected_resource_key == resource.key;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    const auto label = resource.name + "##resource_" + std::to_string(resource.key);
                    if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                        m_selected_resource_key = resource.key;
                        m_inspector.set_selected_resource(resource.key);
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("Pointer: 0x%p", reinterpret_cast<void*>(resource.pointer));
                        ImGui::Text("First seen: %" PRIu64, resource.first_seen_frame);
                        ImGui::Text("Last seen: %" PRIu64, resource.last_seen_frame);
                        ImGui::Text("Seen count: %" PRIu64, resource.seen_count);
                        ImGui::Text("Change count: %" PRIu64, resource.change_count);
                        ImGui::EndTooltip();
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(resource.source.c_str());

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(backend_to_string(resource.backend));

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(resource.type.c_str());

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(resource.format.c_str());

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(resource.resolution.c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("%" PRIu64, age);

                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", resource.tags.c_str());
                }

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::BeginChild("RenderInspectorDetails", ImVec2(0.0f, 0.0f), false);
        {
            const auto* selected = find_resource(resources, m_selected_resource_key);

            if (selected == nullptr) {
                ImGui::TextWrapped("Select a tracked resource to inspect metadata and preview it.");
            } else {
                draw_resource_metadata(*selected, current_frame);

                if (ImGui::CollapsingHeader("Flags", ImGuiTreeNodeFlags_DefaultOpen)) {
                    draw_resource_flags(*selected);
                }

                if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (!preview.has_selection || preview.resource_key != selected->key) {
                        ImGui::TextWrapped("Preview will update on the next frame.");
                    } else if (!preview.available || preview.texture_id == 0) {
                        ImGui::TextWrapped("%s", preview.status.empty() ? "Preview unavailable" : preview.status.c_str());
                    } else {
                        const auto available_width = std::max(64.0f, ImGui::GetContentRegionAvail().x);
                        float preview_width = available_width;
                        float preview_height = 220.0f;

                        if (preview.width > 0 && preview.height > 0) {
                            preview_height = preview_width * (static_cast<float>(preview.height) / static_cast<float>(preview.width));

                            if (preview_height > 360.0f) {
                                const auto scale = 360.0f / preview_height;
                                preview_height = 360.0f;
                                preview_width *= scale;
                            }
                        }

                        ImGui::Text("Preview format: %s", preview.format.c_str());
                        ImGui::Image(to_imgui_texture_id(preview.texture_id), ImVec2(preview_width, preview_height));

                        if (!preview.status.empty()) {
                            ImGui::TextWrapped("%s", preview.status.c_str());
                        }

                        if (!preview.backend_note.empty()) {
                            ImGui::Spacing();
                            ImGui::TextWrapped("%s", preview.backend_note.c_str());
                        }
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }
}

void RenderInspector::draw_dx12_diagnostics() {
    if (!g_framework->is_dx12()) {
        ImGui::TextWrapped("DX12 diagnostics are only available when the active renderer is D3D12.");
        return;
    }

    const auto snapshot = render::D3D12Diagnostics::get().snapshot();

    if (!snapshot.available) {
        ImGui::TextWrapped("DX12 diagnostics have not captured a frame yet.");
        return;
    }

    ImGui::Text("Frame: %" PRIu64, snapshot.frame);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Render: %ux%u", snapshot.render_width, snapshot.render_height);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Display: %ux%u", snapshot.display_width, snapshot.display_height);

    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragInt("Recent event limit", &m_dx12_event_limit, 1.0f, 5, 128);
    m_dx12_event_limit = std::clamp(m_dx12_event_limit, 5, 128);

    if (ImGui::CollapsingHeader("Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("DX12Summary", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            draw_dx12_summary_row("Device", format_pointer_hex(snapshot.device));
            draw_dx12_summary_row("Swapchain", format_pointer_hex(snapshot.swapchain));
            draw_dx12_summary_row("Command Queue", format_pointer_hex(snapshot.command_queue));
            draw_dx12_summary_row("Active CBV/SRV/UAV Heap", format_pointer_hex(snapshot.active_cbv_srv_uav_heap));
            draw_dx12_summary_row("Active Sampler Heap", format_pointer_hex(snapshot.active_sampler_heap));
            draw_dx12_summary_row("Proton Swapchain", snapshot.proton_swapchain ? "yes" : "no");
            draw_dx12_summary_row("Framegen Swapchain", snapshot.framegen_swapchain ? "yes" : "no");
            draw_dx12_summary_row("Heap sets / frame", std::to_string(snapshot.descriptor_heap_sets_this_frame));
            draw_dx12_summary_row("Heap switches / frame", std::to_string(snapshot.descriptor_heap_switches_this_frame));
            draw_dx12_summary_row("Barriers / frame", std::to_string(snapshot.resource_barriers_this_frame));
            draw_dx12_summary_row("RT binds / frame", std::to_string(snapshot.rtv_binds_this_frame));
            draw_dx12_summary_row("Transient heap creations / frame", std::to_string(snapshot.transient_heap_creations_this_frame));
            draw_dx12_summary_row("Transient resources / frame", std::to_string(snapshot.transient_resource_creations_this_frame));
            draw_dx12_summary_row("Transient bytes / frame", format_bytes(snapshot.transient_resource_bytes_this_frame));
            draw_dx12_summary_row("Tracked resource bytes", format_bytes(snapshot.tracked_resource_bytes_total));
            draw_dx12_summary_row("Tracked transient bytes", format_bytes(snapshot.tracked_transient_resource_bytes_total));
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Descriptor Heaps", ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr auto heap_table_flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("DX12HeapTable", 8, heap_table_flags, ImVec2(0.0f, 220.0f))) {
            ImGui::TableSetupColumn("Heap");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Descriptors");
            ImGui::TableSetupColumn("In Use");
            ImGui::TableSetupColumn("Binds");
            ImGui::TableSetupColumn("Flags");
            ImGui::TableSetupColumn("Last Seen");
            ImGui::TableHeadersRow();

            for (const auto& heap : snapshot.heaps) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(heap.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(heap.type.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", heap.source.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", heap.total_descriptors);
                ImGui::TableNextColumn();
                ImGui::Text("%u", heap.estimated_in_use);
                ImGui::TableNextColumn();
                ImGui::Text("%" PRIu64, heap.bind_count);
                ImGui::TableNextColumn();
                std::string flags{};
                if (heap.shader_visible) {
                    flags += "ShaderVisible";
                }
                if (heap.transient) {
                    if (!flags.empty()) {
                        flags += " | ";
                    }
                    flags += "Transient";
                }
                if (heap.is_active) {
                    if (!flags.empty()) {
                        flags += " | ";
                    }
                    flags += "Active";
                }
                ImGui::TextWrapped("%s", flags.empty() ? "-" : flags.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%" PRIu64, heap.last_seen_frame);
            }

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Recent Barrier Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr auto barrier_table_flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("DX12BarrierTable", 7, barrier_table_flags, ImVec2(0.0f, 220.0f))) {
            ImGui::TableSetupColumn("Frame");
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Resource");
            ImGui::TableSetupColumn("Before");
            ImGui::TableSetupColumn("After");
            ImGui::TableSetupColumn("Note");
            ImGui::TableHeadersRow();

            const auto start = snapshot.recent_barriers.size() > static_cast<size_t>(m_dx12_event_limit)
                ? snapshot.recent_barriers.size() - static_cast<size_t>(m_dx12_event_limit)
                : 0;

            for (size_t i = snapshot.recent_barriers.size(); i-- > start;) {
                const auto& event = snapshot.recent_barriers[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%" PRIu64, event.frame);
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", event.source.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(event.type.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(format_pointer_hex(event.resource).c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", event.before_state.empty() ? "-" : event.before_state.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", event.after_state.empty() ? "-" : event.after_state.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", event.note.empty() ? "-" : event.note.c_str());
            }

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Recent Binding Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr auto binding_table_flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("DX12BindingTable", 4, binding_table_flags, ImVec2(0.0f, 220.0f))) {
            ImGui::TableSetupColumn("Frame");
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("Detail");
            ImGui::TableHeadersRow();

            const auto start = snapshot.recent_bindings.size() > static_cast<size_t>(m_dx12_event_limit)
                ? snapshot.recent_bindings.size() - static_cast<size_t>(m_dx12_event_limit)
                : 0;

            for (size_t i = snapshot.recent_bindings.size(); i-- > start;) {
                const auto& event = snapshot.recent_bindings[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%" PRIu64, event.frame);
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", event.source.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(event.kind.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", event.detail.c_str());
            }

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Recent Warnings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (snapshot.recent_warnings.empty()) {
            ImGui::TextUnformatted("No warnings recorded.");
        } else {
            constexpr auto warning_table_flags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp;

            if (ImGui::BeginTable("DX12WarningTable", 3, warning_table_flags, ImVec2(0.0f, 160.0f))) {
                ImGui::TableSetupColumn("Frame");
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Warning");
                ImGui::TableHeadersRow();

                const auto start = snapshot.recent_warnings.size() > static_cast<size_t>(m_dx12_event_limit)
                    ? snapshot.recent_warnings.size() - static_cast<size_t>(m_dx12_event_limit)
                    : 0;

                for (size_t i = snapshot.recent_warnings.size(); i-- > start;) {
                    const auto& warning = snapshot.recent_warnings[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%" PRIu64, warning.frame);
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", warning.source.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", warning.message.c_str());
                }

                ImGui::EndTable();
            }
        }
    }
}

void RenderInspector::draw_pso_profiler() {
    auto shader_snapshot = render::ShaderOverrideRegistry::get().snapshot();
    auto diagnostics_snapshot = render::D3D12Diagnostics::get().snapshot();
    auto resource_snapshot = m_inspector.snapshot();

    if (!g_framework->is_dx12()) {
        ImGui::TextUnformatted("PSO profiler is only available on DX12.");
        return;
    }

    size_t associated_target_count = 0;
    for (const auto& aggregate : shader_snapshot.d3d12_pso_aggregates) {
        associated_target_count += aggregate.likely_targets.size();
    }

    ImGui::Text("Frame: %" PRIu64, shader_snapshot.frame);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Distinct PSOs: %zu", shader_snapshot.d3d12_pso_aggregates.size());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Samples: %" PRIu64, shader_snapshot.total_d3d12_pso_samples);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Tracked target sets: %zu", associated_target_count);

    ImGui::Checkbox("Overridden only", &m_pso_filter_overridden_only);
    ImGui::SameLine();
    ImGui::Checkbox("Stream only", &m_pso_filter_stream_only);
    ImGui::SameLine();
    ImGui::Checkbox("With target association only", &m_pso_filter_with_targets_only);
    ImGui::SameLine();
    ImGui::Checkbox("Tracking warnings only", &m_pso_filter_tracking_warnings_only);

    static constexpr const char* sort_modes[]{"Hits", "Share", "Last Seen"};
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Sort PSOs", &m_pso_sort_mode, sort_modes, IM_ARRAYSIZE(sort_modes));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragInt("PSO row limit", &m_pso_profiler_limit, 1.0f, 8, 512);
    m_pso_profiler_limit = std::clamp(m_pso_profiler_limit, 8, 512);

    if (ImGui::Button("Export Render Analysis Bundle")) {
        render::RenderAnalysisExportInput export_input{};
        export_input.profile_name = Framework::get_persistent_dir().filename().string();
        export_input.backend = g_framework->is_dx12() ? "D3D12" : "D3D11";
        export_input.frame = shader_snapshot.frame;
        export_input.resources = resource_snapshot;
        export_input.d3d12 = diagnostics_snapshot;
        export_input.shaders = shader_snapshot;

        const auto export_result = render::RenderAnalysisExport::export_bundle(export_input);
        if (export_result.succeeded) {
            m_render_bundle_export_status = "Exported render analysis bundle: " + export_result.bundle_dir.string();
        } else {
            m_render_bundle_export_status = "Render analysis export failed: " + export_result.error;
        }
    }

    if (!m_render_bundle_export_status.empty()) {
        ImGui::TextWrapped("%s", m_render_bundle_export_status.c_str());
    }

    ImGui::Separator();

    std::vector<render::ShaderOverrideRegistry::D3D12PsoAggregateInfo> aggregates{};
    aggregates.reserve(shader_snapshot.d3d12_pso_aggregates.size());

    for (const auto& aggregate : shader_snapshot.d3d12_pso_aggregates) {
        const auto overridden = !aggregate.vs_override.empty() || !aggregate.ps_override.empty();
        const auto has_targets = !aggregate.likely_targets.empty();
        const auto has_warning = !aggregate.tracking_note.empty();

        if (m_pso_filter_overridden_only && !overridden) {
            continue;
        }
        if (m_pso_filter_stream_only && !aggregate.pipeline_stream) {
            continue;
        }
        if (m_pso_filter_with_targets_only && !has_targets) {
            continue;
        }
        if (m_pso_filter_tracking_warnings_only && !has_warning) {
            continue;
        }

        aggregates.emplace_back(aggregate);
    }

    std::stable_sort(aggregates.begin(), aggregates.end(), [this](const auto& lhs, const auto& rhs) {
        switch (m_pso_sort_mode) {
        case 1:
            if (lhs.sample_share != rhs.sample_share) {
                return lhs.sample_share > rhs.sample_share;
            }
            break;
        case 2:
            if (lhs.last_seen_frame != rhs.last_seen_frame) {
                return lhs.last_seen_frame > rhs.last_seen_frame;
            }
            break;
        default:
            if (lhs.total_samples != rhs.total_samples) {
                return lhs.total_samples > rhs.total_samples;
            }
            break;
        }

        return lhs.last_seen_frame > rhs.last_seen_frame;
    });

    if (aggregates.empty()) {
        ImGui::TextUnformatted("No PSO aggregates match the current filters.");
        return;
    }

    constexpr auto table_flags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("D3D12PsoProfilerTable", 12, table_flags, ImVec2(0.0f, 320.0f))) {
        ImGui::TableSetupColumn("View");
        ImGui::TableSetupColumn("Hits");
        ImGui::TableSetupColumn("Share");
        ImGui::TableSetupColumn("Original PSO");
        ImGui::TableSetupColumn("Bound PSO");
        ImGui::TableSetupColumn("VS");
        ImGui::TableSetupColumn("PS");
        ImGui::TableSetupColumn("RT");
        ImGui::TableSetupColumn("Depth");
        ImGui::TableSetupColumn("Stream");
        ImGui::TableSetupColumn("Tracking");
        ImGui::TableSetupColumn("Override");
        ImGui::TableHeadersRow();

        const auto display_count = std::min(static_cast<int>(aggregates.size()), m_pso_profiler_limit);
        for (int i = 0; i < display_count; ++i) {
            const auto& aggregate = aggregates[static_cast<size_t>(i)];
            const auto key = make_d3d12_pso_key(aggregate);
            const auto is_selected = m_selected_pso_key == key;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (is_selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4{0.18f, 0.28f, 0.45f, 0.65f}));
            }

            const auto button_label = std::string{is_selected ? "Selected##psopair_" : "View##psopair_"} + key;
            if (ImGui::SmallButton(button_label.c_str())) {
                m_selected_pso_key = key;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIu64, aggregate.total_samples);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_percent(aggregate.sample_share).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(abbreviate_for_table(format_pointer_hex(aggregate.original_pso)).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(abbreviate_for_table(format_pointer_hex(aggregate.last_bound_pso)).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(aggregate.vs_hash.empty() ? "-" : abbreviate_for_table(aggregate.vs_hash).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(aggregate.ps_hash.empty() ? "-" : abbreviate_for_table(aggregate.ps_hash).c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", aggregate.likely_targets.empty() ? "-" : aggregate.likely_targets.front().render_target_name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", aggregate.likely_targets.empty() ? "-" : aggregate.likely_targets.front().depth_target_name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(aggregate.pipeline_stream ? "yes" : "no");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", aggregate.tracking_note.empty() ? "-" : aggregate.tracking_note.c_str());
            ImGui::TableNextColumn();

            std::string override_summary{};
            if (!aggregate.vs_override.empty()) {
                override_summary += "VS:";
                override_summary += aggregate.vs_override;
            }
            if (!aggregate.ps_override.empty()) {
                if (!override_summary.empty()) {
                    override_summary += " | ";
                }
                override_summary += "PS:";
                override_summary += aggregate.ps_override;
            }
            ImGui::TextWrapped("%s", override_summary.empty() ? "None" : override_summary.c_str());
        }

        ImGui::EndTable();
    }

    if (const auto* selected = find_d3d12_pso(aggregates, m_selected_pso_key); selected != nullptr) {
        ImGui::Separator();
        ImGui::TextUnformatted("Selected PSO details");
        draw_d3d12_pso_summary(*selected);
    }
}

void RenderInspector::draw_shaders() {
    auto snapshot = render::ShaderOverrideRegistry::get().snapshot();

    ImGui::Text("Frame: %" PRIu64, snapshot.frame);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Renderer: %s", g_framework->is_dx12() ? "D3D12" : "D3D11");

    ImGui::TextWrapped("Global override dir: %s", snapshot.global_override_dir.c_str());
    ImGui::TextWrapped("Profile override dir: %s", snapshot.profile_override_dir.c_str());

    if (ImGui::Button("Reload Shader Overrides")) {
        render::ShaderOverrideRegistry::get().request_reload();
        render::ShaderOverrideRegistry::get().on_present(*g_framework);
        snapshot = render::ShaderOverrideRegistry::get().snapshot();
    }

    if (g_framework->is_dx12()) {
        ImGui::SameLine();
        if (ImGui::Button("Capture Next DX12 Change")) {
            render::ShaderOverrideRegistry::get().request_capture_next_d3d12_change();
            snapshot = render::ShaderOverrideRegistry::get().snapshot();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear Captured DX12 Change")) {
            render::ShaderOverrideRegistry::get().clear_captured_d3d12_change();
            snapshot = render::ShaderOverrideRegistry::get().snapshot();
        }

        ImGui::SameLine();
        if (ImGui::Button("Sample DX12 Now")) {
            if (snapshot.current_d3d12_pair.has_value()) {
                m_displayed_dx12_pair = snapshot.current_d3d12_pair;
                m_last_dx12_live_sample_frame = snapshot.frame;
            }
        }
    }

    ImGui::Separator();

    if (g_framework->is_dx12()) {
        ImGui::Text("Capture armed: %s", snapshot.capture_next_d3d12_change_armed ? "yes" : "no");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Distinct DX12 pairs: %zu", snapshot.distinct_d3d12_pairs.size());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Samples: %" PRIu64, snapshot.total_d3d12_pair_samples);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::DragInt("Distinct DX12 pair limit", &m_recent_dx12_shader_pair_limit, 1.0f, 4, 512);
        m_recent_dx12_shader_pair_limit = std::clamp(m_recent_dx12_shader_pair_limit, 4, 512);
        ImGui::Checkbox("Freeze live DX12 view", &m_freeze_dx12_live_view);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::DragInt("DX12 live sample interval (frames)", &m_dx12_live_sample_interval_frames, 1.0f, 1, 300);
        m_dx12_live_sample_interval_frames = std::clamp(m_dx12_live_sample_interval_frames, 1, 300);
        ImGui::Checkbox("Sort recent DX12 pairs by hits", &m_sort_recent_dx12_pairs_by_hits);
        ImGui::SameLine();

        std::filesystem::path export_path{};
        std::string export_error{};
        if (ImGui::Button("Export DX12 Pairs JSON")) {
            if (render::ShaderOverrideRegistry::get().export_d3d12_pairs_json(export_path, export_error)) {
                m_shader_export_status = "Exported JSON: " + export_path.string();
            } else {
                m_shader_export_status = "JSON export failed: " + export_error;
            }
            snapshot = render::ShaderOverrideRegistry::get().snapshot();
        }

        ImGui::SameLine();
        if (ImGui::Button("Export DX12 Pairs CSV")) {
            if (render::ShaderOverrideRegistry::get().export_d3d12_pairs_csv(export_path, export_error)) {
                m_shader_export_status = "Exported CSV: " + export_path.string();
            } else {
                m_shader_export_status = "CSV export failed: " + export_error;
            }
            snapshot = render::ShaderOverrideRegistry::get().snapshot();
        }

        if (!m_shader_export_status.empty()) {
            ImGui::TextWrapped("%s", m_shader_export_status.c_str());
        }

        if (snapshot.current_d3d12_pair.has_value()) {
            if (!m_displayed_dx12_pair.has_value()) {
                m_displayed_dx12_pair = snapshot.current_d3d12_pair;
                m_last_dx12_live_sample_frame = snapshot.frame;
            } else if (!m_freeze_dx12_live_view && snapshot.frame >= (m_last_dx12_live_sample_frame + static_cast<uint64_t>(m_dx12_live_sample_interval_frames))) {
                m_displayed_dx12_pair = snapshot.current_d3d12_pair;
                m_last_dx12_live_sample_frame = snapshot.frame;
            }
        }

        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("Currently Bound Shaders", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("ShaderBoundTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Stage");
            ImGui::TableSetupColumn("Backend");
            ImGui::TableSetupColumn("Hash");
            ImGui::TableSetupColumn("Override");
            ImGui::TableSetupColumn("Original");
            ImGui::TableSetupColumn("Note");
            ImGui::TableHeadersRow();

            std::array<render::ShaderOverrideRegistry::BoundShaderInfo, 2> bound{
                snapshot.bound_vertex_shader,
                snapshot.bound_pixel_shader
            };

            if (g_framework->is_dx12() && m_displayed_dx12_pair.has_value()) {
                bound = {
                    m_displayed_dx12_pair->vertex_shader,
                    m_displayed_dx12_pair->pixel_shader
                };
            }

            for (const auto& shader : bound) {
                draw_bound_shader_table_row(shader);
            }

            ImGui::EndTable();
        }

        if (g_framework->is_dx12() && m_displayed_dx12_pair.has_value()) {
            ImGui::Text(
                "Displayed DX12 sample frame: %" PRIu64 " | original PSO: %s | bound PSO: %s",
                m_displayed_dx12_pair->frame,
                format_pointer_hex(m_displayed_dx12_pair->original_pipeline_state).c_str(),
                format_pointer_hex(m_displayed_dx12_pair->bound_pipeline_state).c_str()
            );
        }
    }

    if (g_framework->is_dx12() && ImGui::CollapsingHeader("Captured DX12 Shader Change", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!snapshot.captured_d3d12_pair.has_value()) {
            ImGui::TextUnformatted("No captured DX12 shader change.");
        } else {
            draw_d3d12_pair_summary(*snapshot.captured_d3d12_pair);
        }
    }

    if (g_framework->is_dx12() && ImGui::CollapsingHeader("Distinct DX12 PSOs / Shader Pairs", ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr auto pair_table_flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingStretchProp;

        auto recent_pairs = snapshot.distinct_d3d12_pairs;

        if (m_sort_recent_dx12_pairs_by_hits) {
            std::stable_sort(recent_pairs.begin(), recent_pairs.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.hit_count != rhs.hit_count) {
                    return lhs.hit_count > rhs.hit_count;
                }

                return lhs.last_seen_frame > rhs.last_seen_frame;
            });
        } else {
            std::stable_sort(recent_pairs.begin(), recent_pairs.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.last_seen_frame > rhs.last_seen_frame;
            });
        }

        if (recent_pairs.empty()) {
            ImGui::TextUnformatted("No distinct DX12 PSO/shader pairs captured yet.");
        } else if (ImGui::BeginTable("RecentDX12Pairs", 12, pair_table_flags, ImVec2(0.0f, 260.0f))) {
            ImGui::TableSetupColumn("View");
            ImGui::TableSetupColumn("Last Frame");
            ImGui::TableSetupColumn("First Frame");
            ImGui::TableSetupColumn("Hits");
            ImGui::TableSetupColumn("Share");
            ImGui::TableSetupColumn("Original PSO");
            ImGui::TableSetupColumn("Bound PSO");
            ImGui::TableSetupColumn("VS Hash");
            ImGui::TableSetupColumn("PS Hash");
            ImGui::TableSetupColumn("Stream");
            ImGui::TableSetupColumn("Tracking");
            ImGui::TableSetupColumn("Override");
            ImGui::TableHeadersRow();

            const auto pair_count = static_cast<int>(recent_pairs.size());
            const auto display_count = std::min(pair_count, m_recent_dx12_shader_pair_limit);

            for (int i = 0; i < display_count; ++i) {
                const auto& pair = recent_pairs[static_cast<size_t>(i)];
                const auto pair_key = make_d3d12_pair_key(pair);
                const auto is_selected = m_selected_recent_dx12_pair_key == pair_key;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (is_selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4{0.18f, 0.28f, 0.45f, 0.65f}));
                }

                const auto button_label = std::string{is_selected ? "Selected##dx12pair_" : "View##dx12pair_"} + pair_key;
                if (ImGui::SmallButton(button_label.c_str())) {
                    m_selected_recent_dx12_pair_key = pair_key;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%" PRIu64, pair.last_seen_frame);
                ImGui::TableNextColumn();
                ImGui::Text("%" PRIu64, pair.first_seen_frame);
                ImGui::TableNextColumn();
                ImGui::Text("%" PRIu64, pair.hit_count);
                ImGui::TableNextColumn();
                const auto share = snapshot.total_d3d12_pair_samples > 0
                    ? static_cast<double>(pair.hit_count) / static_cast<double>(snapshot.total_d3d12_pair_samples)
                    : 0.0;
                ImGui::TextUnformatted(format_percent(share).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(abbreviate_for_table(format_pointer_hex(pair.original_pipeline_state)).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(abbreviate_for_table(format_pointer_hex(pair.bound_pipeline_state)).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pair.vertex_shader.known ? abbreviate_for_table(pair.vertex_shader.hash).c_str() : "-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pair.pixel_shader.known ? abbreviate_for_table(pair.pixel_shader.hash).c_str() : "-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pair.pipeline_stream ? "yes" : "no");
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", pair.tracking_note.empty() ? "-" : pair.tracking_note.c_str());
                ImGui::TableNextColumn();

                std::string override_summary{};
                if (pair.vertex_shader.override_active) {
                    override_summary += "VS:";
                    override_summary += pair.vertex_shader.override_name;
                }
                if (pair.pixel_shader.override_active) {
                    if (!override_summary.empty()) {
                        override_summary += " | ";
                    }
                    override_summary += "PS:";
                    override_summary += pair.pixel_shader.override_name;
                }

                ImGui::TextWrapped("%s", override_summary.empty() ? "None" : override_summary.c_str());
            }

            ImGui::EndTable();
        }

        if (const auto* selected_pair = find_d3d12_pair(recent_pairs, m_selected_recent_dx12_pair_key); selected_pair != nullptr) {
            ImGui::Separator();
            ImGui::TextUnformatted("Selected DX12 pair details");
            draw_d3d12_pair_summary(*selected_pair);
        }
    }

    if (ImGui::CollapsingHeader("Loaded Override Entries", ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr auto table_flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("ShaderOverrideTable", 10, table_flags, ImVec2(0.0f, 280.0f))) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Backend");
            ImGui::TableSetupColumn("Stage");
            ImGui::TableSetupColumn("Target Hash");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Compiler");
            ImGui::TableSetupColumn("Generation");
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Notes");
            ImGui::TableHeadersRow();

            for (const auto& entry : snapshot.overrides) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", entry.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.backend == render::ShaderOverrideRegistry::Backend::D3D11 ? "DX11" : "DX12");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.stage == render::ShaderOverrideRegistry::Stage::Vertex ? "VS" : "PS");
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", entry.target_hash.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", entry.status.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.compiler.empty() ? "-" : entry.compiler.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%" PRIu64, entry.generation);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry.source_kind.empty() ? "-" : entry.source_kind.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", entry.source_path.c_str());
                ImGui::TableNextColumn();
                std::string note{};
                note += entry.from_profile_dir ? "Profile" : "Global";
                if (!entry.apply_supported) {
                    note += " | Observe-only";
                }
                if (!entry.enabled) {
                    note += " | Disabled";
                }
                if (!entry.last_error.empty()) {
                    note += " | ";
                    note += entry.last_error;
                }
                ImGui::TextWrapped("%s", note.c_str());
            }

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Recent Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (snapshot.recent_events.empty()) {
            ImGui::TextUnformatted("No shader override events recorded.");
        } else {
            for (auto it = snapshot.recent_events.rbegin(); it != snapshot.recent_events.rend(); ++it) {
                ImGui::BulletText("%s", it->c_str());
            }
        }
    }
}

// =====================================================================
// Shader Hunter — interactive ShaderToggler-style PS suppression.
// =====================================================================
void RenderInspector::draw_shader_hunter() {
    auto& reg = render::ShaderOverrideRegistry::get();
    auto state = reg.hunter_state();

    ImGui::TextWrapped(
        "Step through every PS hash currently being rendered. The active hash "
        "can be suppressed so you see what disappears. Mark hashes to save "
        "them as JSON override manifests. Hotkeys: 1 prev, 2 next, 3 toggle "
        "mark (only while this panel is open and hunting is active).");
    ImGui::Separator();

    if (state.active) {
        if (ImGui::Button("Stop Hunting")) { reg.hunter_stop(); }
    } else {
        if (ImGui::Button("Start Hunting")) {
            reg.hunter_set_frame_window(m_hunter_frame_window_input);
            reg.hunter_start();
            set_force_shader_tracking(true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(state.active ? "Clear + Restart" : "Clear Collected")) {
        reg.hunter_set_frame_window(m_hunter_frame_window_input);
        reg.hunter_clear_collected();
        if (state.active) {
            set_force_shader_tracking(true);
        }
    }
    ImGui::SameLine();
    bool hide = state.hide_marked;
    if (ImGui::Checkbox("Hide marked", &hide)) { reg.hunter_set_hide_marked(hide); }
    ImGui::SameLine();
    bool suppress = state.suppression_enabled;
    if (ImGui::Checkbox("Suppress active", &suppress)) { reg.hunter_set_suppression_enabled(suppress); }
    ImGui::SameLine();
    bool cycle_highlight = reg.hunter_cycle_highlight_mode();
    if (ImGui::Checkbox("Cycle paints magenta", &cycle_highlight)) {
        // When on, pressing 1/2/3 highlights the active shader in magenta
        // instead of hiding its draws. Lets you SEE which scene pixels each
        // shader paints. Mutually exclusive with the hide-when-cycling default.
        reg.hunter_set_cycle_highlight_mode(cycle_highlight);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save marked -> manifests")) {
        std::string err;
        const bool ok = reg.hunter_save_marked_as_manifests(err);
        if (!ok) ImGui::OpenPopup("HunterSaveErr");
        else {
            m_shader_export_status = "Saved marked shaders as JSON manifests";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Trim to live+scene")) {
        // Drops everything that's not scene-candidate AND currently live
        // (last_seen within recent_frame_age). The result is "what's actually
        // rendering RIGHT NOW", which is usually 10-30 shaders for a static
        // menu vs 800+ accumulated. Also auto-pauses collection.
        const size_t kept = reg.hunter_trim_collected(true, true);
        m_shader_export_status = "Trimmed to " + std::to_string(kept) + " live scene shaders";
    }
    ImGui::SameLine();
    if (ImGui::Button("Trim to top 32 hits")) {
        // Keep only the 32 most-bound shaders. Drops one-off / rare-bind
        // entries that are unlikely to be the visible-pixel offender.
        const size_t kept = reg.hunter_trim_to_top_hits(32);
        m_shader_export_status = "Trimmed to top " + std::to_string(kept) + " by hits";
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all marks")) {
        // In-memory marked sets across PS / VS / CS. Does NOT touch any
        // hunter_ps_*.json manifests on disk — use "Delete saved manifests"
        // below for that.
        reg.hunter_clear_all_marks();
        m_shader_export_status = "Cleared all in-memory marks";
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete saved manifests")) {
        // Delete every hunter_ps_*.json from the profile shader_overrides
        // dir and drop their in-memory override entries. Combined with
        // Clear all marks this is the "fresh slate" reset.
        std::string err;
        const size_t n = reg.hunter_delete_saved_manifests(err);
        m_shader_export_status = err.empty()
            ? ("Deleted " + std::to_string(n) + " hunter_ps_*.json manifests")
            : err;
    }
    if (ImGui::BeginPopupModal("HunterSaveErr", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("No marked shaders to save.");
        if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!m_shader_export_status.empty()) {
        ImGui::TextColored({0.4f, 0.9f, 0.4f, 1.0f}, "%s", m_shader_export_status.c_str());
    }

    ImGui::Separator();
    // Frame-window cap controls
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Frame window (0=unlimited)", &m_hunter_frame_window_input)) {
        if (m_hunter_frame_window_input < 0) m_hunter_frame_window_input = 0;
        reg.hunter_set_frame_window(m_hunter_frame_window_input);
    }
    if (state.frame_window > 0) {
        ImGui::SameLine();
        if (state.window_stopped) {
            ImGui::TextColored({0.9f, 0.6f, 0.3f, 1.0f},
                "Window expired (collection paused). Clear + Restart to restart.");
        } else if (state.active) {
            ImGui::Text("frames left: %llu", (unsigned long long)state.window_frames_left);
        }
    }
    ImGui::SetNextItemWidth(120.0f);
    m_hunter_recent_frame_age_input = state.recent_frame_age;
    if (ImGui::InputInt("Recent frames", &m_hunter_recent_frame_age_input)) {
        if (m_hunter_recent_frame_age_input < 0) m_hunter_recent_frame_age_input = 0;
        reg.hunter_set_recent_frame_age(m_hunter_recent_frame_age_input);
    }

    ImGui::Separator();
    ImGui::Text("Collected: %zu  |  Live scene: %zu/%zu  |  Marked: %zu  |  Active idx: %d",
        state.collected_count, state.scene_live_count, state.live_count,
        state.marked_count, state.active_index);
    // Runtime blocklist status: hashes flagged "Block" via per-row button
    // (or added via UEVR_SHADER_HUNTER_SUPPRESSION_BLOCKLIST env var at startup).
    {
        const auto blocklist = reg.hunter_runtime_blocklist_snapshot();
        if (!blocklist.empty()) {
            ImGui::TextColored({0.9f, 0.5f, 0.5f, 1.0f},
                "Runtime blocklist (%zu): %s%s",
                blocklist.size(),
                blocklist.front().c_str(),
                blocklist.size() > 1 ? ", ..." : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear blocklist")) {
                reg.hunter_clear_runtime_blocklist();
            }
        }
    }
    // Highlight set status: hashes that render magenta instead of being skipped.
    {
        const auto hl = reg.hunter_highlight_snapshot();
        if (!hl.empty()) {
            ImGui::TextColored({1.0f, 0.3f, 1.0f, 1.0f},
                "Highlighted (%zu): %s%s",
                hl.size(), hl.front().c_str(), hl.size() > 1 ? ", ..." : "");
        }
    }
    if (!state.active_hash.empty()) {
        ImGui::Text("Hunted PS hash: %s  (CRC32=0x%08X, age=%llu frames)",
            state.active_hash.c_str(), state.active_crc32,
            (unsigned long long)state.active_age_frames);
    } else {
        ImGui::TextDisabled("No hunted PS yet.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Prev (1)")) reg.hunter_step(-1);
    ImGui::SameLine();
    if (ImGui::Button("Next (2)")) reg.hunter_step(+1);
    ImGui::SameLine();
    if (ImGui::Button("Mark/Unmark (3)")) reg.hunter_toggle_mark_active();

    // === Stage status lines + per-stage Hunt/Mark buttons ===
    // ShaderToggler-style three-stage walk. Hotkeys use top-row digits:
    //   1/2 prev/next, 3 toggle mark  — PIXEL stage
    //   4/5 prev/next, 6 toggle mark  — VERTEX stage
    //   7/8 prev/next, 9 toggle mark  — COMPUTE stage
    using Stage = render::ShaderOverrideRegistry::HunterStage;
    auto stage_row = [&](Stage stage, const char* label,
                          ImGuiKey prev_key, ImGuiKey next_key, ImGuiKey mark_key,
                          int prev_n, int next_n, int mark_n) {
        const int s = static_cast<int>(stage);
        const bool active_marked = state.active_is_marked_per_stage[s];
        // Label/idx/hash in normal color, then a colored [MARKED] tag inline
        // when the currently-hunted hash for this stage is also in the marked set.
        ImGui::Text("%s: idx=%d hash=%s (CRC32=0x%08X, age=%llu, count=%zu, marked=%zu)",
            label,
            state.active_index_per_stage[s],
            state.active_hash_per_stage[s].empty() ? "(none)" : state.active_hash_per_stage[s].c_str(),
            state.active_crc32_per_stage[s],
            (unsigned long long)state.active_age_frames_per_stage[s],
            state.collected_count_per_stage[s],
            state.marked_count_per_stage[s]);
        if (active_marked) {
            ImGui::SameLine();
            ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "[MARKED]");
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Prev (%d)##%s", prev_n, label);
        if (ImGui::Button(buf)) reg.hunter_step(stage, -1);
        ImGui::SameLine();
        std::snprintf(buf, sizeof(buf), "Next (%d)##%s", next_n, label);
        if (ImGui::Button(buf)) reg.hunter_step(stage, +1);
        ImGui::SameLine();
        std::snprintf(buf, sizeof(buf), "Mark (%d)##%s", mark_n, label);
        if (ImGui::Button(buf)) reg.hunter_toggle_mark_active(stage);
        if (state.active) {
            if (ImGui::IsKeyPressed(prev_key, false)) reg.hunter_step(stage, -1);
            if (ImGui::IsKeyPressed(next_key, false)) reg.hunter_step(stage, +1);
            if (ImGui::IsKeyPressed(mark_key, false)) reg.hunter_toggle_mark_active(stage);
        }
    };
    stage_row(Stage::Pixel,   "PIXEL ",   ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, 1, 2, 3);
    stage_row(Stage::Vertex,  "VERTEX",   ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, 4, 5, 6);
    stage_row(Stage::Compute, "COMPUTE",  ImGuiKey_7, ImGuiKey_8, ImGuiKey_9, 7, 8, 9);

    ImGui::Separator();
    ImGui::InputTextWithHint("Filter (PS hash / CRC32)", "hash or crc32 hex...",
        m_hunter_filter, sizeof(m_hunter_filter));
    ImGui::InputTextWithHint("VS filter (substring)", "vs hash hex...",
        m_hunter_vs_filter, sizeof(m_hunter_vs_filter));
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Min hits", &m_hunter_min_hits);
    if (m_hunter_min_hits < 0) m_hunter_min_hits = 0;
    ImGui::SameLine();
    ImGui::Checkbox("Only show scene candidates", &m_hunter_only_scene_candidates);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Sort##hunter", &m_hunter_sort_mode,
        "Capture order\0PS hash\0Hits desc\0PS size desc\0VS hash\0Age asc\0Live then hits desc\0\0");

    // Build sortable index
    std::vector<size_t> indices(state.collected_hashes.size());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
    if (m_hunter_sort_mode == 1) {
        std::sort(indices.begin(), indices.end(),
            [&](size_t a, size_t b) { return state.collected_hashes[a] < state.collected_hashes[b]; });
    } else if (m_hunter_sort_mode == 2) {
        std::sort(indices.begin(), indices.end(),
            [&](size_t a, size_t b) { return state.collected_hits[a] > state.collected_hits[b]; });
    } else if (m_hunter_sort_mode == 3) {
        std::sort(indices.begin(), indices.end(),
            [&](size_t a, size_t b) { return state.collected_sizes[a] > state.collected_sizes[b]; });
    } else if (m_hunter_sort_mode == 4) {
        // VS hash sort groups material variants together (same VS = same mesh
        // shading family). Helpful for spotting "this whole material family
        // is the offender" patterns.
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            if (state.collected_vs_hashes[a] != state.collected_vs_hashes[b])
                return state.collected_vs_hashes[a] < state.collected_vs_hashes[b];
            return state.collected_hits[a] > state.collected_hits[b];
        });
    } else if (m_hunter_sort_mode == 5) {
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            if (state.collected_age_frames[a] != state.collected_age_frames[b])
                return state.collected_age_frames[a] < state.collected_age_frames[b];
            return state.collected_hits[a] > state.collected_hits[b];
        });
    } else if (m_hunter_sort_mode == 6) {
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            const bool a_live = state.collected_age_frames[a] <= static_cast<uint64_t>(state.recent_frame_age);
            const bool b_live = state.collected_age_frames[b] <= static_cast<uint64_t>(state.recent_frame_age);
            if (a_live != b_live) return a_live;
            if (state.collected_hits[a] != state.collected_hits[b])
                return state.collected_hits[a] > state.collected_hits[b];
            return state.collected_age_frames[a] < state.collected_age_frames[b];
        });
    }

    const std::string filter = m_hunter_filter;
    const std::string vs_filter = m_hunter_vs_filter;
    // Use the registry-exposed threshold so UI and suppression-eligibility
    // never drift apart if HUNTER_MIN_SCENE_PS_SIZE is tuned in the .cpp.
    const size_t min_scene_ps_size = state.min_scene_ps_size > 0
        ? state.min_scene_ps_size : 1024;
    if (ImGui::BeginTable("##hunter_table", 8,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 360.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("PS hash (FNV1a-64)");
        ImGui::TableSetupColumn("VS hash");
        ImGui::TableSetupColumn("CRC32 (ShaderToggler)");
        ImGui::TableSetupColumn("PS size");
        ImGui::TableSetupColumn("Hits");
        ImGui::TableSetupColumn("Age");
        ImGui::TableSetupColumn("Actions");
        ImGui::TableHeadersRow();
        int row_n = 0;
        for (size_t idx : indices) {
            const auto& h = state.collected_hashes[idx];
            const auto& vs = state.collected_vs_hashes[idx];
            char crc_str[16]{};
            std::snprintf(crc_str, sizeof(crc_str), "%08X", state.collected_crc32s[idx]);
            if (static_cast<int>(state.collected_hits[idx]) < m_hunter_min_hits) continue;
            if (m_hunter_only_scene_candidates) {
                const bool live = state.collected_age_frames[idx] <= static_cast<uint64_t>(state.recent_frame_age);
                const bool scene_candidate = !vs.empty() && state.collected_sizes[idx] >= min_scene_ps_size;
                if (!live || !scene_candidate) continue;
            }
            if (!filter.empty()) {
                const bool match = h.find(filter) != std::string::npos
                    || std::string{crc_str}.find(filter) != std::string::npos;
                if (!match) continue;
            }
            if (!vs_filter.empty()) {
                if (vs.find(vs_filter) == std::string::npos) continue;
            }
            ImGui::TableNextRow();
            // Stage-aware active check: a row is "active" only for its own
            // stage's hunted hash (avoids highlighting PS rows when the user
            // is on the VS walk and vice versa).
            const auto row_stage = idx < state.collected_stages.size()
                ? static_cast<int>(state.collected_stages[idx]) : 0;
            const bool is_active = (h == state.active_hash_per_stage[row_stage]);
            const bool is_marked = state.collected_marked[idx];
            ImGui::TableNextColumn();
            // Tag with "*" prefix when active+marked so the row stands out
            // even if the user's cursor isn't on it.
            if (is_active && is_marked) ImGui::Text("*%d", row_n);
            else ImGui::Text("%d", row_n);
            ImGui::TableNextColumn();
            // Hash color: orange when active+marked (combo), yellow when active
            // only, light-red when marked only, plain otherwise.
            if (is_active && is_marked) ImGui::TextColored({1.0f, 0.6f, 0.2f, 1.0f}, "%s [MARKED]", h.c_str());
            else if (is_active)         ImGui::TextColored({1.0f, 0.9f, 0.3f, 1.0f}, "%s", h.c_str());
            else if (is_marked)         ImGui::TextColored({1.0f, 0.5f, 0.5f, 1.0f}, "%s", h.c_str());
            else                        ImGui::TextUnformatted(h.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(vs.empty() ? "(none)" : vs.c_str());
            ImGui::TableNextColumn();
            if (is_marked) ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", crc_str);
            else ImGui::TextUnformatted(crc_str);
            ImGui::TableNextColumn();
            ImGui::Text("%zu", state.collected_sizes[idx]);
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIu64, state.collected_hits[idx]);
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIu64, state.collected_age_frames[idx]);
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(idx));
            if (ImGui::SmallButton("Hunt")) {
                reg.hunter_set_index(0);
                auto s2 = reg.hunter_state();
                int n2 = (int)s2.collected_count;
                for (int i = 0; i < n2; ++i) {
                    if (s2.collected_hashes[(size_t)i] == h) { reg.hunter_set_index(i); break; }
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(is_marked ? "Unmark" : "Mark")) {
                reg.hunter_toggle_mark_hash(h);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Block")) {
                // Permanently exclude this hash from suppression (for this session).
                // Use when a hash crashed the game last time it was suppressed.
                reg.hunter_add_runtime_blocklist(h);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hilite")) {
                // Toggle highlight: instead of skipping, render this shader's
                // draws with magenta so the user can SEE where it draws.
                reg.hunter_toggle_highlight_hash(h);
            }
            ImGui::SameLine();
            const bool skip_l = reg.hunter_is_skip_left_only(h);
            if (skip_l) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.3f, 0.3f, 1.0f));
            if (ImGui::SmallButton("SkipL")) reg.hunter_toggle_skip_left_only(h);
            if (skip_l) ImGui::PopStyleColor();
            ImGui::SameLine();
            const bool skip_r = reg.hunter_is_skip_right_only(h);
            if (skip_r) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.3f, 0.3f, 1.0f));
            if (ImGui::SmallButton("SkipR")) reg.hunter_toggle_skip_right_only(h);
            if (skip_r) ImGui::PopStyleColor();
            ImGui::SameLine();
            if (!vs.empty() && ImGui::SmallButton("VS=")) {
                // Quick-filter: set the VS-filter to this row's VS so the user
                // can see every PS sharing the same vertex shader.
                std::snprintf(m_hunter_vs_filter, sizeof(m_hunter_vs_filter), "%s", vs.c_str());
            }
            ImGui::PopID();
            ++row_n;
        }
        ImGui::EndTable();
    }
}

// =====================================================================
// Eye Diff — per-PSO per-eye fingerprint comparison.
// Identifies PSOs whose left-eye and right-eye inputs diverge (the SN2
// right-eye bug pattern: same shader bytecode, different descriptors).
// =====================================================================
void RenderInspector::draw_eye_diff() {
    auto& reg = render::ShaderOverrideRegistry::get();
    bool enabled = reg.eyediff_enabled();

    ImGui::TextWrapped(
        "Per-PSO per-eye fingerprint tracking. Each time a draw fires we "
        "record the bound RTV[0] handle and graphics-root descriptor-table[0] "
        "for the current eye bucket (L / R / Full / Multi). PSOs whose L vs R "
        "fingerprints diverge are the most likely candidates for per-eye "
        "rendering bugs.");
    ImGui::Separator();

    if (ImGui::Checkbox("Enabled (record on every Draw)", &enabled)) {
        reg.eyediff_set_enabled(enabled);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        reg.eyediff_clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Set UEVR_EYE_DIFF_LOG=1 before launch for per-draw log spam)");

    if (!enabled) {
        ImGui::TextColored({0.9f, 0.6f, 0.3f, 1.0f},
            "Disabled — turn on to start recording. Counters update each frame.");
        return;
    }

    const auto entries = reg.eyediff_snapshot_top_divergent(128);
    ImGui::Text("Tracked PSOs: %zu (top 128 by divergence shown)", entries.size());

    if (ImGui::BeginTable("##eyediff_table", 9,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2(0.0f, 480.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("PS hash");
        ImGui::TableSetupColumn("VS hash");
        ImGui::TableSetupColumn("L hits");
        ImGui::TableSetupColumn("R hits");
        ImGui::TableSetupColumn("RTV L != R");
        ImGui::TableSetupColumn("Desc L != R");
        ImGui::TableSetupColumn("Last frame");
        ImGui::TableSetupColumn("Actions");
        ImGui::TableHeadersRow();
        int row_n = 0;
        for (const auto& e : entries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", row_n);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.ps_hash.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.vs_hash.empty() ? "(none)" : e.vs_hash.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIu64, e.bind_count_left);
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIu64, e.bind_count_right);
            ImGui::TableNextColumn();
            if (e.rtv_divergence_seen > 0) {
                ImGui::TextColored({1.0f, 0.5f, 0.3f, 1.0f}, "%" PRIu64
                    " (L=0x%llx R=0x%llx)",
                    e.rtv_divergence_seen, (unsigned long long)e.last_rtv_left, (unsigned long long)e.last_rtv_right);
            } else {
                ImGui::TextDisabled("0");
            }
            ImGui::TableNextColumn();
            if (e.desc_divergence_seen > 0) {
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "%" PRIu64
                    " (L=0x%llx R=0x%llx)",
                    e.desc_divergence_seen, (unsigned long long)e.last_desc_left, (unsigned long long)e.last_desc_right);
            } else {
                ImGui::TextDisabled("0");
            }
            ImGui::TableNextColumn();
            ImGui::Text("%" PRIu64, e.last_seen_frame);
            ImGui::TableNextColumn();
            ImGui::PushID(row_n);
            const std::string& ps = e.ps_hash;
            if (ImGui::SmallButton("Hunt")) {
                // Find this hash in the Pixel-stage order and set as active.
                auto s2 = reg.hunter_state();
                int n2 = (int)s2.collected_count_per_stage[0];
                for (int i = 0; i < n2; ++i) {
                    if (s2.collected_hashes[(size_t)i] == ps) {
                        reg.hunter_set_index(render::ShaderOverrideRegistry::HunterStage::Pixel, i);
                        break;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Mark")) {
                reg.hunter_toggle_mark_hash(ps);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Block")) {
                reg.hunter_add_runtime_blocklist(ps);
            }
            ImGui::PopID();
            ++row_n;
        }
        ImGui::EndTable();
    }
}
