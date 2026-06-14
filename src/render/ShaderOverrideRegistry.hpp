#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "render/ShaderCompiler.hpp"

class Framework;

namespace render {
class ShaderOverrideRegistry {
public:
    enum class Backend : uint8_t {
        D3D11,
        D3D12,
    };

    enum class Stage : uint8_t {
        Vertex,
        Pixel,
        Geometry,
        Compute,
        Amplification,
        Mesh,
    };

    enum class OverrideSourceKind : uint8_t {
        Hlsl,
        Bytecode,
        DxilPatch,
        DxilTextPatch,
        ContainerPatch,
        DxilTransform,
        DxilSemanticTransform,
    };

    enum class EyeTarget : uint8_t {
        Any,
        Unknown,
        Left,
        Right,
        Full,
        Multi,
    };

    enum class BindOverrideKind : uint8_t {
        Cbv,
        RootConstants,
    };

    using CreateVertexShaderFn = HRESULT (WINAPI*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
    using CreatePixelShaderFn = HRESULT (WINAPI*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);

    struct BoundShaderInfo {
        bool known{};
        Backend backend{Backend::D3D11};
        Stage stage{Stage::Vertex};
        uintptr_t original_pointer{};
        uintptr_t bound_pointer{};
        std::string hash{};
        uint32_t crc32{};
        bool override_active{};
        std::string override_name{};
        std::string note{};
        uint64_t last_bound_frame{};
    };

    struct OverrideEntryInfo {
        std::string key{};
        std::string name{};
        Backend backend{Backend::D3D11};
        Stage stage{Stage::Vertex};
        std::string target_hash{};
        std::string manifest_path{};
        std::string source_path{};
        std::string source_kind{};
        std::string entry_point{};
        std::string profile{};
        bool enabled{};
        bool compiled{};
        bool apply_supported{};
        bool from_profile_dir{};
        bool per_eye_variants{};
        bool has_left_payload{};
        bool has_right_payload{};
        std::string left_source_kind{};
        std::string right_source_kind{};
        uint64_t generation{};
        std::string status{};
        std::string compiler{};
        std::string last_error{};
    };

    struct BindOverrideEntryInfo {
        std::string key{};
        std::string name{};
        std::string target_hash{};
        std::string stage{};
        std::string pipeline{};
        std::string eye{};
        std::string kind{};
        uint32_t root_parameter{};
        uint32_t value_count{};
        uint32_t dest_offset{};
        bool enabled{};
        bool from_profile_dir{};
        std::string manifest_path{};
        std::string status{};
        std::string last_error{};
    };

    struct D3D12PipelinePairInfo {
        uint64_t frame{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        uint64_t hit_count{};
        uintptr_t original_pipeline_state{};
        uintptr_t bound_pipeline_state{};
        bool pipeline_stream{};
        std::string tracking_note{};
        BoundShaderInfo vertex_shader{};
        BoundShaderInfo pixel_shader{};
        BoundShaderInfo geometry_shader{};
    };

    struct PsoRenderUsageInfo {
        std::string render_target_name{};
        std::string depth_target_name{};
        std::string render_target_key{};
        std::string depth_target_key{};
        uint64_t hit_count{};
        double share{};
    };

    struct D3D12PsoAggregateInfo {
        uint64_t total_samples{};
        double sample_share{};
        uint64_t bind_count_with_known_targets{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        uintptr_t original_pso{};
        uintptr_t last_bound_pso{};
        bool pipeline_stream{};
        std::string tracking_note{};
        std::string vs_hash{};
        std::string ps_hash{};
        std::string gs_hash{};
        uint32_t vs_crc32{};
        uint32_t ps_crc32{};
        uint32_t gs_crc32{};
        std::string vs_override{};
        std::string ps_override{};
        std::string gs_override{};
        std::vector<PsoRenderUsageInfo> likely_targets{};
    };

    struct D3D12PsoChurnFrameInfo {
        uint64_t frame{};
        uint64_t graphics_creations{};
        uint64_t compute_creations{};
        uint64_t stream_creations{};
    };

    struct D3D12PsoChurnInfo {
        uint64_t tracked_pso_count{};
        uint64_t current_frame_graphics_creations{};
        uint64_t current_frame_compute_creations{};
        uint64_t current_frame_stream_creations{};
        uint64_t recent_window_frames{};
        uint64_t recent_graphics_creations{};
        uint64_t recent_compute_creations{};
        uint64_t recent_stream_creations{};
        std::vector<D3D12PsoChurnFrameInfo> recent_frames{};
    };

    struct D3D12ShaderBytecodeInspection {
        bool found{};
        std::string requested_stage{};
        std::string requested_hash{};
        std::string matched_stage{};
        uintptr_t pipeline_state{};
        uintptr_t root_signature{};
        ShaderBytecodeInspection bytecode{};
    };

    struct Snapshot {
        bool auto_reload{true};
        bool runtime_overrides_enabled{true};
        uint64_t frame{};
        std::string global_override_dir{};
        std::string profile_override_dir{};
        BoundShaderInfo bound_vertex_shader{};
        BoundShaderInfo bound_pixel_shader{};
        std::optional<D3D12PipelinePairInfo> current_d3d12_pair{};
        bool capture_next_d3d12_change_armed{};
        std::optional<D3D12PipelinePairInfo> captured_d3d12_pair{};
        uint64_t total_d3d12_pair_samples{};
        std::vector<D3D12PipelinePairInfo> distinct_d3d12_pairs{};
        uint64_t total_d3d12_pso_samples{};
        D3D12PsoChurnInfo d3d12_pso_churn{};
        std::vector<D3D12PsoAggregateInfo> d3d12_pso_aggregates{};
        std::vector<OverrideEntryInfo> overrides{};
        std::vector<BindOverrideEntryInfo> bind_overrides{};
        std::vector<std::string> recent_events{};
    };

    struct D3D12CbvBindOverride {
        std::string name{};
        std::vector<uint8_t> data{};
    };

    struct D3D12RootConstantsBindOverride {
        std::string name{};
        std::vector<uint32_t> values{};
        uint32_t dest_offset{};
    };

    // === Shader Hunter (ShaderToggler-equivalent live hash hunting) ===
    // Lets a user interactively cycle through PS/VS/CS hashes that have been
    // bound recently, skip draws for one at a time to see which scene element
    // it draws, mark suppressed ones into a set, and persist them.
    enum class HunterStage : uint8_t { Pixel = 0, Vertex = 1, Compute = 2 };
    struct HunterStateView {
        bool active{};
        bool hide_marked{};
        bool suppression_enabled{true};
        // Per-stage active selection. ShaderToggler-style — separate walks
        // for Pixel/Vertex/Compute. UI hotkeys: 1/2/3 PS, 4/5/6 VS, 7/8/9 CS.
        int active_index_per_stage[3]{-1, -1, -1};
        std::string active_hash_per_stage[3]{};
        uint32_t active_crc32_per_stage[3]{};
        uint64_t active_age_frames_per_stage[3]{};
        // True if the currently-hunted hash for this stage is also in the
        // marked set — lets the UI flag "you're hunting an already-marked one".
        bool active_is_marked_per_stage[3]{};
        // Legacy aliases for the existing UI code that only knows Pixel.
        // Always reflect the Pixel stage values above so older callers keep
        // working with no changes.
        int active_index{-1};
        std::string active_hash{};
        uint32_t active_crc32{};
        uint64_t active_age_frames{};
        size_t collected_count{};
        size_t collected_count_per_stage[3]{};
        size_t live_count{};
        size_t live_count_per_stage[3]{};
        size_t scene_live_count{};
        size_t marked_count{};
        size_t marked_count_per_stage[3]{};
        int frame_window{0};
        int recent_frame_age{30};
        size_t min_scene_ps_size{0};  // = HUNTER_MIN_SCENE_PS_SIZE so UI can match the registry's scene-candidate filter exactly
        uint64_t window_frames_left{0};
        bool window_stopped{};
        std::vector<std::string> collected_hashes{};
        std::vector<std::string> collected_vs_hashes{};
        std::vector<uint32_t> collected_crc32s{};
        std::vector<size_t> collected_sizes{};
        std::vector<uint64_t> collected_hits{};
        std::vector<uint64_t> collected_draw_hits{};
        std::vector<uint64_t> collected_dispatch_hits{};
        std::vector<uint64_t> collected_eye_left_hits{};
        std::vector<uint64_t> collected_eye_right_hits{};
        std::vector<uint64_t> collected_eye_full_hits{};
        std::vector<uint64_t> collected_eye_other_hits{};
        std::vector<uint64_t> collected_draw_age_frames{};
        std::vector<std::string> collected_last_render_targets{};
        std::vector<std::string> collected_last_depth_target{};
        std::vector<uint64_t> collected_age_frames{};
        std::vector<bool> collected_marked{};
        std::vector<HunterStage> collected_stages{};  // matches collected_hashes 1:1
    };
    void hunter_start();
    void hunter_stop();
    void hunter_clear_collected();
    // Stage-tagged variants — pass HunterStage::Pixel for the original
    // single-stage behavior. The legacy overloads below default to Pixel.
    void hunter_step(HunterStage stage, int delta);
    void hunter_set_index(HunterStage stage, int index);
    void hunter_toggle_mark_active(HunterStage stage);
    // Legacy convenience wrappers that target the Pixel stage.
    void hunter_step(int delta);
    void hunter_set_index(int index);
    void hunter_toggle_mark_active();
    void hunter_toggle_mark_hash(std::string_view hash);
    void hunter_set_hide_marked(bool v);
    void hunter_set_suppression_enabled(bool v);
    void hunter_set_frame_window(int frames); // 0 = unlimited
    void hunter_set_recent_frame_age(int frames);
    HunterStateView hunter_state() const;
    bool hunter_save_marked_as_manifests(std::string& error_out);
    bool hunter_export_scene_list_json(std::filesystem::path& out_path, std::string& error_out) const;
    void hunter_toggle_mark_hash(HunterStage stage, std::string_view hash);
    // Runtime suppression blocklist (additive on top of the env-var one).
    // Use when a hunting attempt crashes the game — flag the active hash via
    // the UI so it's permanently excluded from suppression for this session.
    void hunter_add_runtime_blocklist(std::string_view hash);
    void hunter_clear_runtime_blocklist();
    std::vector<std::string> hunter_runtime_blocklist_snapshot() const;
    // Clear all marked sets across PS / VS / CS stages (in-memory only).
    void hunter_clear_all_marks();
    // Trim m_hunter_collected to only entries currently "live" (last_seen
    // within recent_frame_age) AND scene-candidate (vs hash + ps_size>=1024
    // when scene_only=true). Result: cycle iterates a stable, tight list
    // matching what's actually rendering in the current scene.
    size_t hunter_trim_collected(bool scene_only, bool live_only);
    // Trim to top-N hits within the existing list.
    size_t hunter_trim_to_top_hits(size_t keep_count);
    bool hunter_capture_active_as_override_stub(
        HunterStage stage,
        std::filesystem::path& manifest_path,
        std::filesystem::path& source_path,
        std::string& error_out
    );
    // Highlight mode: instead of skipping the draw, substitute the PSO with
    // a variant whose PS outputs solid magenta — so the user can SEE where
    // the shader draws in the scene. Mutually exclusive with mark/skip per
    // hash. Compiles 8 RT-count variants of the magenta PS at first call.
    void hunter_toggle_highlight_hash(std::string_view hash);
    std::vector<std::string> hunter_highlight_snapshot() const;
    // Per-eye selective skip API: target hash gets skipped only when bound
    // on the named eye. The other eye still renders normally.
    void hunter_toggle_skip_left_only(std::string_view hash);
    void hunter_toggle_skip_right_only(std::string_view hash);
    bool hunter_is_skip_left_only(std::string_view hash) const;
    bool hunter_is_skip_right_only(std::string_view hash) const;
    // Cycle-mode: when true, pressing 1/2/3 highlights the active hash in
    // magenta instead of skipping its draws. The skip path is disabled for
    // the cycle-active hash while this is on.
    void hunter_set_cycle_highlight_mode(bool v);
    bool hunter_cycle_highlight_mode() const { return m_hunter_cycle_highlight_mode.load(std::memory_order_relaxed); }
    // Delete any hunter_ps_*.json manifests from the profile shader_overrides
    // dir AND drop their in-memory override entries. Used when the user wants
    // a fully clean session with no previously-saved suppressions still active.
    size_t hunter_delete_saved_manifests(std::string& error_out);
    // Per-command-list cached "should draw be skipped" flag. set_pipeline_state
    // hook calls hunter_record_set_pipeline_state to update the flag based on
    // the just-bound PSO. draw_instanced/draw_indexed_instanced hooks call
    // hunter_should_skip_draw to decide whether to call the original.
    // Returns true when the bound PS hash matches hunter active/marked.
    bool hunter_should_skip_draw(void* command_list) const;
    // Per-eye selective skip evaluated at DRAW time using the current bound
    // PSO + current viewport bucket. Avoids the viewport-after-SetPipelineState
    // race where the skip flag is computed before the eye is known.
    bool hunter_should_skip_draw_per_eye(uintptr_t pso_pointer, int eye_bucket) const;
    // Returns the PS CRC32 for the PSO at `pso_pointer`, or 0 if not tracked.
    // Used by the D3D12 cb0-swap hook to gate on specific shader fingerprints.
    uint32_t d3d12_pso_vertex_crc32(uintptr_t pso_pointer) const;
    uint32_t d3d12_pso_pixel_crc32(uintptr_t pso_pointer) const;
    uint32_t d3d12_pso_geometry_crc32(uintptr_t pso_pointer) const;
    uint32_t d3d12_pso_compute_crc32(uintptr_t pso_pointer) const;
    uint32_t d3d12_pso_amplification_crc32(uintptr_t pso_pointer) const;
    uint32_t d3d12_pso_mesh_crc32(uintptr_t pso_pointer) const;
    void hunter_record_set_pipeline_state(void* command_list, void* original_pso);
    // Extended variant: caller passes the current eye bucket (0 Unknown, 1
    // Left, 2 Right, 3 Full, 4 Multi) so per-eye-selective skip can fire.
    void hunter_record_set_pipeline_state_with_eye(void* command_list, void* original_pso, int eye_bucket);
    void hunter_record_draw_event(uintptr_t pso_pointer, int eye_bucket, bool compute, bool indexed, bool indirect = false);
    void hunter_clear_command_list(void* command_list);
    bool hunter_should_skip_graphics(void* command_list) const;
    bool hunter_should_skip_compute(void* command_list) const;
    bool hunter_collect_compute_events() const;
    bool hunter_collect_indirect_events() const;
    bool hunter_disable_compute_dispatch_hook() const;
    // Increment self-test counters from the Draw* hooks so the periodic
    // stats log can show whether those hooks are even reaching us.
    void hunter_inc_draw_hit();
    void hunter_inc_draw_skipped();
    void hunter_inc_draw_indexed_hit();
    void hunter_inc_draw_indexed_skipped();
    void hunter_inc_dispatch_hit();
    void hunter_inc_dispatch_skipped();
    void hunter_inc_execute_indirect_hit();
    void hunter_inc_execute_indirect_skipped();
    void hunter_inc_execute_bundle_hit();
    void hunter_inc_execute_bundle_skipped();
    void hunter_inc_dispatch_mesh_hit();
    void hunter_inc_dispatch_mesh_skipped();

    static ShaderOverrideRegistry& get();

    void on_present(Framework& framework);
    // D3D12 can start seeing PSO creation/bind events before the Framework
    // present loop is fully initialized. Startup diagnostics and headless
    // shader probes need manifests loaded by then, not only on on_present().
    void scan_override_directories_now();
    void set_inspector_tracking_enabled(bool enabled);
    bool should_track_d3d11_shaders() const;
    bool should_track_d3d12_pipelines() const;
    // The diagnostic-only subset of should_track_d3d12_pipelines(): true when an
    // inspector / Shader Hunter / capture / headless-skip consumer needs the
    // per-bind pipeline-pair + sample bookkeeping, but NOT merely because an
    // override is active. Applying an override only needs resolve_*; the heavy
    // note_d3d12_pipeline_state_bound() is pure diagnostics. Letting an active
    // override take a lean apply-only bind path is the whole point of the split.
    bool should_track_d3d12_pipelines_for_diagnostics() const;
    bool has_active_d3d12_overrides() const { return m_has_active_d3d12_overrides.load(std::memory_order_relaxed); }
    bool should_record_d3d12_pipeline_creations() const;
    void request_reload();
    void set_runtime_overrides_enabled(bool enabled);
    bool runtime_overrides_enabled() const;
    void request_capture_next_d3d12_change();
    void clear_captured_d3d12_change();
    bool export_d3d12_pairs_json(std::filesystem::path& out_path, std::string& error_out);
    bool export_d3d12_pairs_csv(std::filesystem::path& out_path, std::string& error_out);
    D3D12ShaderBytecodeInspection inspect_d3d12_shader_bytecode(
        std::string_view stage,
        std::string_view hash,
        bool disassemble,
        size_t max_disassembly_chars = 128 * 1024
    ) const;
    Snapshot snapshot() const;

    void set_d3d11_create_callbacks(CreateVertexShaderFn create_vs, CreatePixelShaderFn create_ps);
    void register_d3d11_shader_creation(Stage stage, ID3D11Device* device, IUnknown* shader, const void* bytecode, size_t bytecode_size);
    ID3D11VertexShader* resolve_d3d11_vertex_shader(ID3D11Device* device, ID3D11VertexShader* shader);
    ID3D11PixelShader* resolve_d3d11_pixel_shader(ID3D11Device* device, ID3D11PixelShader* shader);
    void note_d3d11_shader_bound(Stage stage, IUnknown* original_shader, IUnknown* bound_shader);
    void register_d3d12_graphics_pipeline_state_creation(ID3D12Device* device, ID3D12PipelineState* pipeline_state, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc);
    void register_d3d12_compute_pipeline_state_creation(ID3D12Device* device, ID3D12PipelineState* pipeline_state, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc);
    void register_d3d12_pipeline_state_stream_creation(ID3D12Device* device, ID3D12PipelineState* pipeline_state, const D3D12_PIPELINE_STATE_STREAM_DESC* desc);
    ID3D12PipelineState* resolve_d3d12_pipeline_state(ID3D12PipelineState* pipeline_state);
    ID3D12PipelineState* resolve_d3d12_pipeline_state_for_eye(ID3D12PipelineState* pipeline_state, int eye_bucket);
    void note_d3d12_pipeline_state_bound(ID3D12PipelineState* original_pipeline_state, ID3D12PipelineState* bound_pipeline_state);
    bool is_d3d12_pipeline_state_tracked(uintptr_t pipeline_state) const;
    std::optional<D3D12CbvBindOverride> resolve_d3d12_cbv_bind_override(
        bool graphics,
        uintptr_t pipeline_state,
        int eye_bucket,
        uint32_t root_parameter
    ) const;
    std::optional<D3D12RootConstantsBindOverride> resolve_d3d12_root_constants_bind_override(
        bool graphics,
        uintptr_t pipeline_state,
        int eye_bucket,
        uint32_t root_parameter
    ) const;

private:
    struct OverrideEntry {
        std::string key{};
        std::string name{};
        Backend backend{Backend::D3D11};
        Stage stage{Stage::Vertex};
        std::string target_hash{};
        OverrideSourceKind source_kind{OverrideSourceKind::Hlsl};
        std::filesystem::path manifest_path{};
        std::filesystem::path source_path{};
        std::filesystem::path bytecode_path{};
        std::filesystem::path patch_path{};
        std::filesystem::path patch_tool_path{};
        std::filesystem::path cached_bytecode_path{};
        std::string compiled_original_hash{};
        std::vector<ShaderTextPatch> dxil_text_patches{};
        std::vector<ShaderContainerEdit> container_edits{};
        std::string entry_point{};
        std::string profile{};
        ShaderCompilerBackend preferred_compiler{ShaderCompilerBackend::Auto};
        bool enabled{true};
        bool from_profile_dir{};
        bool per_eye_variants{};
        bool compiled{};
        bool apply_supported{};
        uint64_t generation{};
        std::string status{};
        std::string compiler{};
        std::string last_error{};
        std::vector<uint8_t> compiled_bytecode{};
        std::filesystem::file_time_type manifest_write_time{};
        std::filesystem::file_time_type source_write_time{};
        std::filesystem::file_time_type bytecode_write_time{};
        std::filesystem::file_time_type patch_write_time{};
        struct EyePayload {
            bool present{};
            OverrideSourceKind source_kind{OverrideSourceKind::Bytecode};
            std::filesystem::path bytecode_path{};
            std::filesystem::path patch_path{};
            std::filesystem::path cached_bytecode_path{};
            std::vector<ShaderTextPatch> dxil_text_patches{};
            std::vector<ShaderContainerEdit> container_edits{};
            std::vector<uint8_t> compiled_bytecode{};
            std::string compiled_original_hash{};
            std::string status{};
            std::string last_error{};
            std::filesystem::file_time_type bytecode_write_time{};
            std::filesystem::file_time_type patch_write_time{};
        };
        EyePayload left_payload{};
        EyePayload right_payload{};
    };

    struct D3D11ShaderRecord {
        Stage stage{Stage::Vertex};
        uintptr_t shader_pointer{};
        uintptr_t device_pointer{};
        std::string hash{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        uint64_t seen_count{};
        uint64_t override_generation{};
        bool override_active{};
        std::string override_name{};
        Microsoft::WRL::ComPtr<ID3D11DeviceChild> override_shader{};
    };

    struct OwnedD3D12GraphicsPipelineStateDesc {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature{};
        std::vector<uint8_t> vertex_shader{};
        std::vector<uint8_t> pixel_shader{};
        std::vector<uint8_t> domain_shader{};
        std::vector<uint8_t> hull_shader{};
        std::vector<uint8_t> geometry_shader{};
        std::vector<std::string> input_semantic_names{};
        std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements{};
        std::vector<std::string> stream_output_semantic_names{};
        std::vector<D3D12_SO_DECLARATION_ENTRY> stream_output_declarations{};
        std::vector<UINT> stream_output_strides{};

        void refresh_views();
    };

    struct OwnedD3D12PipelineStateStream {
        D3D12_PIPELINE_STATE_STREAM_DESC desc{};
        std::vector<uint8_t> stream_bytes{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature{};
        std::vector<uint8_t> vertex_shader{};
        std::vector<uint8_t> pixel_shader{};
        std::vector<uint8_t> domain_shader{};
        std::vector<uint8_t> hull_shader{};
        std::vector<uint8_t> geometry_shader{};
        std::vector<uint8_t> compute_shader{};
        std::vector<uint8_t> amplification_shader{};
        std::vector<uint8_t> mesh_shader{};
        std::vector<std::string> input_semantic_names{};
        std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements{};
        std::vector<std::string> stream_output_semantic_names{};
        std::vector<D3D12_SO_DECLARATION_ENTRY> stream_output_declarations{};
        std::vector<UINT> stream_output_strides{};
        std::vector<D3D12_VIEW_INSTANCE_LOCATION> view_instance_locations{};
        size_t root_signature_offset{static_cast<size_t>(-1)};
        size_t vertex_shader_offset{static_cast<size_t>(-1)};
        size_t pixel_shader_offset{static_cast<size_t>(-1)};
        size_t domain_shader_offset{static_cast<size_t>(-1)};
        size_t hull_shader_offset{static_cast<size_t>(-1)};
        size_t geometry_shader_offset{static_cast<size_t>(-1)};
        size_t compute_shader_offset{static_cast<size_t>(-1)};
        size_t amplification_shader_offset{static_cast<size_t>(-1)};
        size_t mesh_shader_offset{static_cast<size_t>(-1)};
        size_t input_layout_offset{static_cast<size_t>(-1)};
        size_t stream_output_offset{static_cast<size_t>(-1)};
        size_t cached_pso_offset{static_cast<size_t>(-1)};
        size_t view_instancing_offset{static_cast<size_t>(-1)};

        void refresh_views();
        bool empty() const {
            return stream_bytes.empty();
        }
    };

    struct D3D12GraphicsPsoRecord {
        uintptr_t pipeline_state_pointer{};
        std::string vertex_hash{};
        std::string pixel_hash{};
        std::string geometry_hash{};
        std::string compute_hash{};
        std::string amplification_hash{};
        std::string mesh_hash{};
        uint32_t vertex_crc32{};
        uint32_t pixel_crc32{};
        uint32_t geometry_crc32{};
        uint32_t compute_crc32{};
        uint32_t amplification_crc32{};
        uint32_t mesh_crc32{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        uint64_t seen_count{};
        uint64_t applied_override_revision{};
        bool is_pipeline_stream{};
        bool override_active{};
        std::string vertex_override_name{};
        std::string pixel_override_name{};
        std::string geometry_override_name{};
        std::string compute_override_name{};
        std::string amplification_override_name{};
        std::string mesh_override_name{};
        std::string tracking_note{};
        std::string last_error{};
        Microsoft::WRL::ComPtr<ID3D12Device> device{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> override_pipeline_state{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> override_pipeline_state_left{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> override_pipeline_state_right{};
        OwnedD3D12GraphicsPipelineStateDesc owned_desc{};
        OwnedD3D12PipelineStateStream owned_stream{};
        D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc{};
        bool logged_substitution{};
    };

    struct BindOverrideEntry {
        std::string key{};
        std::string name{};
        std::string target_hash{};
        Stage stage{Stage::Pixel};
        bool any_stage{true};
        bool graphics{true};
        bool compute{};
        EyeTarget eye{EyeTarget::Any};
        BindOverrideKind kind{BindOverrideKind::Cbv};
        uint32_t root_parameter{};
        uint32_t dest_offset{};
        bool enabled{true};
        bool from_profile_dir{};
        std::filesystem::path manifest_path{};
        std::filesystem::file_time_type manifest_write_time{};
        std::string status{};
        std::string last_error{};
        std::vector<uint8_t> cbv_data{};
        std::vector<uint32_t> constants{};
    };

    struct PsoRenderUsageRecord {
        std::string render_target_name{};
        std::string depth_target_name{};
        std::string render_target_key{};
        std::string depth_target_key{};
        uint64_t hit_count{};
    };

    struct D3D12PsoAggregateRecord {
        uintptr_t original_pso{};
        uintptr_t last_bound_pso{};
        bool pipeline_stream{};
        std::string tracking_note{};
        std::string vs_hash{};
        std::string ps_hash{};
        std::string gs_hash{};
        uint32_t vs_crc32{};
        uint32_t ps_crc32{};
        uint32_t gs_crc32{};
        std::string vs_override{};
        std::string ps_override{};
        std::string gs_override{};
        uint64_t total_samples{};
        uint64_t bind_count_with_known_targets{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        std::unordered_map<std::string, PsoRenderUsageRecord> usage_by_key{};
    };

    void scan_override_directories();
    void scan_single_directory(
        const std::filesystem::path& dir,
        bool from_profile_dir,
        std::unordered_map<std::string, std::filesystem::path>& discovered_bind_overrides);
    void remove_deleted_entries(const std::unordered_map<std::string, std::filesystem::path>& discovered_entries);
    void remove_deleted_bind_overrides(const std::unordered_map<std::string, std::filesystem::path>& discovered_entries);
    void compile_or_refresh_entry(OverrideEntry& entry);
    std::optional<OverrideEntry> parse_manifest(const std::filesystem::path& manifest_path, bool from_profile_dir);
    std::optional<BindOverrideEntry> parse_bind_override_manifest(const std::filesystem::path& manifest_path, bool from_profile_dir);
    bool compile_entry(OverrideEntry& entry, std::string& error_out);
    bool ensure_d3d12_patch_entry_compiled(OverrideEntry& entry, const void* original_bytecode, size_t original_bytecode_size, std::string_view original_hash, std::string& error_out);
    bool record_matches_bind_override(const D3D12GraphicsPsoRecord& record, const BindOverrideEntry& entry) const;
    void push_event(std::string message);
    void refresh_active_override_flags_locked();
    void update_d3d11_override_shader(D3D11ShaderRecord& record, ID3D11Device* device);
    void update_d3d12_override_pipeline_state(D3D12GraphicsPsoRecord& record);
    static bool copy_pipeline_state_stream(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, OwnedD3D12PipelineStateStream& out, std::string& error_out);
    void record_d3d12_pipeline_pair(const D3D12PipelinePairInfo& info);
    void record_d3d12_pso_sample(const D3D12PipelinePairInfo& info);
    std::string make_d3d12_pair_key(const D3D12PipelinePairInfo& info) const;
    std::string make_d3d12_pso_key(const D3D12PipelinePairInfo& info) const;
    std::filesystem::path make_d3d12_pair_export_path(const char* extension) const;
    std::string make_override_key(Backend backend, Stage stage, std::string_view target_hash) const;
    std::string hash_shader_bytecode(const void* bytecode, size_t bytecode_size) const;
    std::filesystem::path global_override_dir() const;
    std::filesystem::path profile_override_dir() const;

    mutable std::recursive_mutex m_mutex{};
    std::unordered_map<std::string, OverrideEntry> m_overrides{};
    std::unordered_map<std::string, BindOverrideEntry> m_bind_overrides{};
    std::unordered_map<uintptr_t, D3D11ShaderRecord> m_d3d11_shader_records{};
    std::unordered_map<uintptr_t, D3D12GraphicsPsoRecord> m_d3d12_graphics_pso_records{};
    BoundShaderInfo m_bound_vertex_shader{};
    BoundShaderInfo m_bound_pixel_shader{};
    bool m_capture_next_d3d12_change{};
    std::optional<D3D12PipelinePairInfo> m_captured_d3d12_pair{};
    std::optional<D3D12PipelinePairInfo> m_last_d3d12_pair{};
    uint64_t m_total_d3d12_pair_samples{};
    std::vector<D3D12PipelinePairInfo> m_distinct_d3d12_pairs{};
    std::unordered_map<std::string, size_t> m_distinct_d3d12_pair_indices{};
    uint64_t m_total_d3d12_pso_samples{};
    std::unordered_map<std::string, D3D12PsoAggregateRecord> m_d3d12_pso_aggregates{};
    uint64_t m_d3d12_graphics_pso_creations_this_frame{};
    uint64_t m_d3d12_compute_pso_creations_this_frame{};
    uint64_t m_d3d12_stream_pso_creations_this_frame{};
    std::vector<D3D12PsoChurnFrameInfo> m_recent_d3d12_pso_churn{};
    std::vector<std::string> m_recent_events{};
    std::chrono::steady_clock::time_point m_last_scan_time{};
    bool m_force_reload{};
    uint64_t m_frame{};
    uint64_t m_override_revision{};
    CreateVertexShaderFn m_create_vertex_shader{};
    CreatePixelShaderFn m_create_pixel_shader{};
    std::atomic_bool m_has_active_d3d11_overrides{false};
    std::atomic_bool m_has_active_d3d12_overrides{false};
    std::atomic_bool m_runtime_overrides_enabled{true};
    std::atomic_bool m_inspector_tracking_enabled{false};
    std::atomic_bool m_capture_next_d3d12_change_hot_path{false};

    // === Shader Hunter state (see public API above) ===
    struct HunterCollectedEntry {
        uint32_t crc32{};
        size_t ps_size{};
        uint64_t hits{};
        uint64_t draw_hits{};
        uint64_t indexed_draw_hits{};
        uint64_t dispatch_hits{};
        uint64_t eye_hits[5]{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
        uint64_t last_draw_frame{};
        uintptr_t last_pso{};
        std::string last_render_targets{};
        std::string last_render_target_key{};
        std::string last_depth_target{};
        std::string last_depth_target_key{};
        std::string vs_hash{};
        HunterStage stage{HunterStage::Pixel};
    };
    std::atomic_bool m_hunter_active{false};
    std::atomic_bool m_hunter_hide_marked{false};
    std::atomic_bool m_hunter_suppression_enabled{true};
    // Pixel stage — the original / canonical walk state (existing code reads
    // these names directly).
    int m_hunter_active_index{-1};
    std::string m_hunter_active_hash{};
    std::vector<std::string> m_hunter_order{};
    std::unordered_set<std::string> m_hunter_marked{};
    // Vertex stage — independent walk + mark set.
    int m_hunter_active_index_vs{-1};
    std::string m_hunter_active_hash_vs{};
    std::vector<std::string> m_hunter_order_vs{};
    std::unordered_set<std::string> m_hunter_marked_vs{};
    // Compute stage — independent walk + mark set.
    int m_hunter_active_index_cs{-1};
    std::string m_hunter_active_hash_cs{};
    std::vector<std::string> m_hunter_order_cs{};
    std::unordered_set<std::string> m_hunter_marked_cs{};
    std::unordered_map<std::string, HunterCollectedEntry> m_hunter_collected{};
    // Stage-separated supplementary maps for VS / CS walks. The Pixel walk
    // continues to use the canonical m_hunter_collected map above (existing
    // code reads from it). VS entries store ps_size=0 by convention since
    // VS shaders don't have a pixel size; the entry's vs_hash field holds
    // the (single) companion PS hash for any one PSO that uses this VS.
    std::unordered_map<std::string, HunterCollectedEntry> m_hunter_collected_vs{};
    std::unordered_map<std::string, HunterCollectedEntry> m_hunter_collected_cs{};
    std::unordered_set<std::string> m_hunter_runtime_blocklist{};  // hashes flagged crashy at runtime via UI

    // === Eye-Diff Tracker ===
    // Per-PSO per-eye bind/draw fingerprints so we can identify which
    // pixel shaders are being fed DIFFERENT inputs between the left and
    // right eyes. The SN2 bug is a per-eye descriptor divergence (right
    // eye reads wrong 3D LUT at PS slot 5) — by hashing the bound RTV /
    // SRV handles and grouping by (PSO, eye_bucket) we surface candidates.
    // Eye buckets: 0 Unknown, 1 Left, 2 Right, 3 Full, 4 Multi.
    struct EyeDiffPerPsoRecord {
        uint64_t bind_count_per_eye[5]{};        // # of Draw calls with this PSO bound, by eye bucket
        uint64_t last_rtv_handle_per_eye[5]{};   // last OMSetRenderTargets handle bound on this CL before the draw
        uint64_t last_descriptor_table0_per_eye[5]{}; // last SetGraphicsRootDescriptorTable(0,...) gpu handle
        uint64_t rtv_divergence_seen{};          // count of times left rtv != right rtv (both nonzero)
        uint64_t desc_divergence_seen{};         // same for descriptor table
        std::string vs_hash{};
        std::string ps_hash{};
        uint64_t first_seen_frame{};
        uint64_t last_seen_frame{};
    };
    mutable std::mutex m_eyediff_mutex{};
    std::unordered_map<std::string, EyeDiffPerPsoRecord> m_eyediff_by_pshash{};
    std::atomic_bool m_eyediff_enabled{false};

public:
    struct EyeDiffEntry {
        std::string ps_hash{};
        std::string vs_hash{};
        uint64_t bind_count_left{};
        uint64_t bind_count_right{};
        uint64_t bind_count_full{};
        uint64_t bind_count_other{};
        uint64_t last_rtv_left{};
        uint64_t last_rtv_right{};
        uint64_t last_desc_left{};
        uint64_t last_desc_right{};
        uint64_t rtv_divergence_seen{};
        uint64_t desc_divergence_seen{};
        uint64_t last_seen_frame{};
    };
    void eyediff_set_enabled(bool v);
    bool eyediff_enabled() const { return m_eyediff_enabled.load(std::memory_order_relaxed); }
    void eyediff_clear();
    // Fast lookup: returns (ps_hash, vs_hash) for a given PSO pointer, or
    // empty strings if not in the registry.
    std::pair<std::string, std::string> snapshot_pso_hashes_for(uintptr_t pso_pointer) const;
    // Called from each Draw* / Dispatch / ExecuteIndirect hook with the
    // PSO's PS hash, the current eye bucket (0..4), and per-CL descriptor
    // fingerprints. Atomic-safe + fast (early-out when disabled).
    void eyediff_record_draw(const std::string& ps_hash, const std::string& vs_hash,
                             int eye_bucket, uint64_t rtv0_handle, uint64_t desc_table0_handle);
    std::vector<EyeDiffEntry> eyediff_snapshot_top_divergent(size_t max_entries = 64) const;
private:
    std::vector<uint8_t> m_hunter_discard_ps{};
    bool m_hunter_tried_compile_discard{false};
    // Magenta PS variants — one per RT count (1..8). Index 0 unused.
    std::vector<uint8_t> m_hunter_magenta_ps[9]{};
    bool m_hunter_tried_compile_magenta{false};
    // Hashes that should be highlighted (substituted with magenta PS) instead
    // of skipped. Mutually exclusive with marked-suppression per hash.
    std::unordered_set<std::string> m_hunter_highlight{};
    std::atomic_bool m_hunter_cycle_highlight_mode{false};
    // Per-eye selective skip: hashes that should be skipped ONLY when bound
    // on the named eye (left or right). Lets a shader render normally on one
    // eye and be hidden on the other — perfect for shaders whose per-eye
    // math is broken (e.g. ApplyLowerHemisphereColorPS in SN2).
    std::unordered_set<std::string> m_hunter_skip_left_only{};
    std::unordered_set<std::string> m_hunter_skip_right_only{};
    void hunter_ensure_magenta_compiled_locked();
    int m_hunter_frame_window{0};            // 0 = unlimited; otherwise auto-pause after N frames
    int m_hunter_recent_frame_age{30};
    uint64_t m_hunter_window_start_frame{0};
    std::atomic_bool m_hunter_window_stopped{false}; // true when frame-window auto-paused collection
    struct HunterCommandListSkipState {
        bool graphics{};
        bool compute{};
    };
    // Per-command-list skip decisions cached at SetPipelineState time. ReShade
    // tracks PS/VS/CS independently; keep graphics and compute separate so
    // hunting a pixel shader never suppresses unrelated compute dispatches.
    mutable std::mutex m_hunter_skip_mutex{};
    std::unordered_map<void*, HunterCommandListSkipState> m_hunter_skip_by_cmdlist{};
    void hunter_ensure_discard_compiled_locked();
    void hunter_record_bind_locked(const D3D12GraphicsPsoRecord& record);
    bool hunter_should_suppress_locked(const D3D12GraphicsPsoRecord& record) const;
    bool hunter_record_is_safe_suppression_candidate_locked(const D3D12GraphicsPsoRecord& record) const;
    bool hunter_entry_is_scene_candidate_locked(const HunterCollectedEntry& entry) const;
    void hunter_rebuild_active_locked();
    void hunter_rebuild_stage_active_locked(HunterStage stage);
};
} // namespace render
