#pragma once

#include <atomic>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Mod.hpp"
#include "render/D3D12Diagnostics.hpp"
#include "render/FrameResourceInspector.hpp"
#include "render/RenderAnalysisExport.hpp"
#include "render/ShaderOverrideRegistry.hpp"

class RenderInspector : public Mod {
public:
    static std::shared_ptr<RenderInspector>& get();

    std::string_view get_name() const override {
        return "Render Inspector";
    }

    std::vector<SidebarEntryInfo> get_sidebar_entries() override {
        return {
            {"Resources", false},
            {"DX12 Diagnostics", false},
            {"PSO Profiler", false},
            {"Shaders", false},
            {"Shader Hunter", false},
            {"Eye Diff", false},
        };
    }

    void on_present() override;
    void on_draw_sidebar_entry(std::string_view in_entry) override;

    // FFI / external-consumer hooks. The MCP plugin (or other tooling) can flip
    // these on so the inspector keeps sampling even when the user has closed
    // the Render Inspector sidebar in UEVR's overlay.
    render::FrameResourceInspector& inspector();
    const render::FrameResourceInspector& inspector() const;

    void set_force_resources_sampling(bool v);
    bool force_resources_sampling() const;

    void set_force_shader_tracking(bool v);
    bool force_shader_tracking() const;

    void set_force_d3d12_diagnostics(bool v);
    bool force_d3d12_diagnostics() const;

private:
    void draw_resources();
    void draw_dx12_diagnostics();
    void draw_pso_profiler();
    void draw_shaders();
    void draw_shader_hunter();
    void draw_eye_diff();
    void service_shader_hunter_autotest();
    int m_hunter_sort_mode{6};
    bool m_hunter_only_scene_candidates{true};
    char m_hunter_filter[64]{};
    char m_hunter_vs_filter[24]{};
    int m_hunter_min_hits{0};
    int m_hunter_frame_window_input{300};
    int m_hunter_recent_frame_age_input{30};
    bool m_hunter_autotest_initialized{false};
    bool m_hunter_autotest_enabled{false};
    bool m_hunter_autotest_started{false};
    bool m_hunter_autotest_finished{false};
    bool m_hunter_autotest_suppress{false};
    uint64_t m_hunter_autotest_frame{};
    uint64_t m_hunter_autotest_start_frame{};
    uint64_t m_hunter_autotest_last_step_frame{};
    int m_hunter_autotest_delay_frames{20};
    int m_hunter_autotest_collect_frames{180};
    int m_hunter_autotest_step_interval_frames{30};
    int m_hunter_autotest_steps{4};
    int m_hunter_autotest_steps_done{};
    int m_hunter_autotest_start_index{-1};
    bool m_hunter_autotest_start_index_applied{false};

    render::FrameResourceInspector m_inspector{};
    std::optional<uint64_t> m_selected_resource_key{};
    bool m_filter_depth_only{false};
    bool m_filter_render_targets_only{false};
    bool m_filter_ui_only{false};
    bool m_filter_swapchain_only{false};
    bool m_filter_recent_only{true};
    int m_recent_frame_window{180};
    int m_dx12_event_limit{24};
    int m_recent_dx12_shader_pair_limit{16};
    bool m_freeze_dx12_live_view{false};
    int m_dx12_live_sample_interval_frames{15};
    uint64_t m_last_dx12_live_sample_frame{};
    std::optional<render::ShaderOverrideRegistry::D3D12PipelinePairInfo> m_displayed_dx12_pair{};
    bool m_sort_recent_dx12_pairs_by_hits{true};
    std::string m_selected_recent_dx12_pair_key{};
    std::string m_shader_export_status{};
    bool m_pso_filter_overridden_only{false};
    bool m_pso_filter_stream_only{false};
    bool m_pso_filter_with_targets_only{false};
    bool m_pso_filter_tracking_warnings_only{false};
    int m_pso_profiler_limit{32};
    int m_pso_sort_mode{0};
    std::string m_selected_pso_key{};
    std::string m_render_bundle_export_status{};
    std::array<char, 1024> m_shader_editor_path{};
    std::vector<char> m_shader_editor_buffer{};
    std::string m_shader_editor_status{};

    std::atomic<bool> m_force_resources_sampling{false};
    std::atomic<bool> m_force_shader_tracking{false};
    std::atomic<bool> m_force_d3d12_diagnostics{false};
};
